/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * op_set_slot_count.c
 *
 * ocfs2 tune utility for setting the number of slots available on the
 * filesystem.
 *
 * Copyright (C) 2004, 2008 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>

#include "ocfs2/ocfs2.h"
#include "ocfs2/bitops.h"

#include "libocfs2ne.h"

struct moved_group {
	uint64_t blkno;
	char *gd_buf;
	struct moved_group *next;
};

struct relink_ctxt {
	int inode_type;
	struct ocfs2_chain_rec *cr;
	uint16_t new_slot;
	uint64_t dst_blkno;
	char *src_inode;
	char *dst_inode;
	char *ex_buf;
};

struct remove_slot_ctxt {
	ocfs2_filesys *fs;
	uint16_t removed_slot;
	errcode_t errcode;
};


static errcode_t add_slots(ocfs2_filesys *fs, int num_slots)
{
	errcode_t ret;
	uint16_t old_num = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);
	char fname[OCFS2_MAX_FILENAME_LEN];
	uint64_t blkno;
	int i, j, max_slots;
	int ftype;
	struct tools_progress *prog = NULL;

	if (ocfs2_uses_extended_slot_map(OCFS2_RAW_SB(fs->fs_super))) {
		ret = TUNEFS_ET_TOO_MANY_SLOTS_EXTENDED;
		max_slots = INT16_MAX;
	} else {
		ret = TUNEFS_ET_TOO_MANY_SLOTS_OLD;
		max_slots = OCFS2_MAX_SLOTS;
	}
	if (num_slots > max_slots)
		goto bail;

	prog = tools_progress_start("Adding slots", "addslots",
				    (NUM_SYSTEM_INODES -
				     OCFS2_LAST_GLOBAL_SYSTEM_INODE - 1) *
				    (num_slots - old_num));
	if (!prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		goto bail;
	}

	ret = 0;
	for (i = OCFS2_LAST_GLOBAL_SYSTEM_INODE + 1; i < NUM_SYSTEM_INODES; ++i) {
		if (i == LOCAL_USER_QUOTA_SYSTEM_INODE &&
		    !OCFS2_HAS_RO_COMPAT_FEATURE(super,
					OCFS2_FEATURE_RO_COMPAT_USRQUOTA))
			continue;
		if (i == LOCAL_GROUP_QUOTA_SYSTEM_INODE &&
		    !OCFS2_HAS_RO_COMPAT_FEATURE(super,
					OCFS2_FEATURE_RO_COMPAT_GRPQUOTA))
			continue;
		for (j = old_num; j < num_slots; ++j) {
			ocfs2_sprintf_system_inode_name(fname,
							OCFS2_MAX_FILENAME_LEN,
							i, j);
			verbosef(VL_APP, "Creating system file \"%s\"\n",
				 fname);

			/* Goto next if file already exists */
			ret = ocfs2_lookup(fs, fs->fs_sysdir_blkno, fname,
					   strlen(fname), NULL, &blkno);
			if (!ret) {
				verbosef(VL_APP,
					 "System file \"%s\" already exists\n",
					 fname);
				tools_progress_step(prog, 1);
				continue;
			}

			/* create inode for system file */
			ret = ocfs2_new_system_inode(fs, &blkno,
						     ocfs2_system_inodes[i].si_mode,
						     ocfs2_system_inodes[i].si_iflags);
			if (ret) {
				verbosef(VL_APP,
					 "%s while creating inode for "
					 "system file \"%s\"\n",
					 error_message(ret), fname);
				goto bail;
			}

			ftype = (S_ISDIR(ocfs2_system_inodes[i].si_mode) ?
				 OCFS2_FT_DIR : OCFS2_FT_REG_FILE);

			/* if dir, alloc space to it */
			if (ftype == OCFS2_FT_DIR) {
				ret = ocfs2_init_dir(fs, blkno,
						     fs->fs_sysdir_blkno);
				if (ret) {
					verbosef(VL_APP,
						 "%s while initializing "
						 "directory \"%s\"\n",
						 error_message(ret),
						 fname);
					goto bail;
				}
			}

			/* Add the inode to the system dir */
			ret = ocfs2_link(fs, fs->fs_sysdir_blkno, fname,
					 blkno, ftype);
			if (ret) {
				verbosef(VL_APP,
					"%s while linking inode %"PRIu64" "
					"as \"%s\" in the system "
					"directory\n",
					error_message(ret), blkno, fname);
				goto bail;
			}
			/* Initialize quota files */
			if (i == LOCAL_USER_QUOTA_SYSTEM_INODE) {
				verbosef(VL_APP, "Initializing local user "
					 "quota file\n");
				ret = ocfs2_init_local_quota_file(fs, USRQUOTA,
								  blkno);
				if (ret) {
					verbosef(VL_APP,
						 "%s while initializing user "
						 "quota file %s\n",
						 error_message(ret), fname);
					goto bail;
				}
			} else if (i == LOCAL_GROUP_QUOTA_SYSTEM_INODE) {
				verbosef(VL_APP, "Initializing local group "
					 "quota file\n");
				ret = ocfs2_init_local_quota_file(fs, GRPQUOTA,
								  blkno);
				if (ret) {
					verbosef(VL_APP,
						 "%s while initializing group "
						 "quota file %s\n",
						 error_message(ret), fname);
					goto bail;
				}
			}
			verbosef(VL_APP, "System file \"%s\" created\n",
				 fname);
			tools_progress_step(prog, 1);
		}
	}

bail:
	if (prog)
		tools_progress_stop(prog);

	return ret;
}

static errcode_t change_sub_alloc_slot(ocfs2_filesys *fs,
				       uint64_t blkno,
				       struct relink_ctxt *ctxt)
{
	errcode_t ret;
	struct ocfs2_dinode *di = NULL;
	struct ocfs2_extent_block *eb = NULL;

	if (ctxt->inode_type == EXTENT_ALLOC_SYSTEM_INODE) {
		/* change sub alloc bit in the extent block. */
		ret = ocfs2_read_extent_block(fs, blkno, ctxt->ex_buf);
		if (ret)
			goto bail;

		eb = (struct ocfs2_extent_block *)ctxt->ex_buf;
		eb->h_suballoc_slot = ctxt->new_slot;

		ret = ocfs2_write_extent_block(fs, blkno, ctxt->ex_buf);
		if (ret)
			goto bail;
	} else {
		/* change sub alloc bit in the inode. */
		ret = ocfs2_read_inode(fs, blkno, ctxt->ex_buf);
		if (ret)
			goto bail;

		di = (struct ocfs2_dinode *)ctxt->ex_buf;
		di->i_suballoc_slot = ctxt->new_slot;

		ret = ocfs2_write_inode(fs, blkno, ctxt->ex_buf);
		if (ret)
			goto bail;
	}
bail:
	return ret;
}

static errcode_t move_group(ocfs2_filesys *fs,
			    struct relink_ctxt *ctxt,
			    struct moved_group *group)
{
	errcode_t ret = 0;
	uint16_t cr_pos;
	struct ocfs2_group_desc *gd = NULL;
	struct ocfs2_dinode *di = NULL;
	struct ocfs2_chain_list *cl = NULL;
	struct ocfs2_chain_rec *cr = NULL;

	if (!group || !group->blkno || !group->gd_buf)
		goto bail;

	di = (struct ocfs2_dinode *)ctxt->dst_inode;
	cl = &di->id2.i_chain;

	/* calculate the insert position. */
	if (cl->cl_next_free_rec < cl->cl_count)
		cr_pos = cl->cl_next_free_rec;
	else {
		/* Now we have all the chain record filled with some groups.
		 * so we figure out all the groups we have and then calculate
		 * the proper place for our insert.
		 */
		cr_pos = di->id1.bitmap1.i_total / (cl->cl_cpg * cl->cl_bpc);
		cr_pos %= cl->cl_count;
	}

	cr = &cl->cl_recs[cr_pos];

	gd = (struct ocfs2_group_desc *)group->gd_buf;
	gd->bg_chain = cr_pos;
	gd->bg_parent_dinode = ctxt->dst_blkno;

	/* we can safely set the bg_next_group here since all the group
	 * below it in the moving chain is already moved to the new
	 * position and we don't need to worry about any "lost" groups.
	 *
	 * Please see how we build up the group list in move_chain_rec.
	 */
	gd->bg_next_group = cr->c_blkno;

	ret = ocfs2_write_group_desc(fs, group->blkno, group->gd_buf);
	if (ret)
		goto bail;

	/* modify the chain record and the new files simultaneously. */
	cr->c_blkno = gd->bg_blkno;
	cr->c_total += gd->bg_bits;
	cr->c_free += gd->bg_free_bits_count;

	/* If the chain isn't full, increase the free_rec. */
	if (cl->cl_next_free_rec != cl->cl_count)
		cl->cl_next_free_rec++;

	di->id1.bitmap1.i_total += gd->bg_bits;
	di->id1.bitmap1.i_used += gd->bg_bits;
	di->id1.bitmap1.i_used -= gd->bg_free_bits_count;
	di->i_clusters += cl->cl_cpg;
	di->i_size += cl->cl_cpg * fs->fs_clustersize;

	ret = ocfs2_write_inode(fs, ctxt->dst_blkno, ctxt->dst_inode);

bail:
	return ret;
}

/*
 * This function will iterate the chain_rec and do the following modifications:
 * 1. record all the groups in the chains.
 * 2. for every group, do:
 *    1) modify  Sub Alloc Slot in extent block/inodes accordingly.
 *    2) change the GROUP_PARENT according to its future owner.
 *    3) link the group to the new slot files.
 */
static errcode_t move_chain_rec(ocfs2_filesys *fs, struct relink_ctxt *ctxt)
{
	errcode_t ret = 0;
	int i, start, end = 1;
	uint64_t blkno, gd_blkno = ctxt->cr->c_blkno;
	struct ocfs2_group_desc *gd = NULL;
	struct moved_group *group = NULL, *group_head = NULL;

	if (gd_blkno == 0)
		goto bail;

	/* Record the group in the relink_ctxt.
	 *
	 * We record the group in a reverse order, so the first group
	 * will be at the end of the group list. This is useful for
	 * fsck.ocfs2 when any error happens during the move of groups
	 * and we can safely move the group also.
	 */
	while (gd_blkno) {
		ret = ocfs2_malloc0(sizeof(struct moved_group), &group);
		if (ret)
			goto bail;
		memset(group, 0, sizeof(struct moved_group));

		/* We insert the group first in case of any further error
		 * will not cause memory leak.
		 */
		group->next = group_head;
		group_head = group;

		ret = ocfs2_malloc_block(fs->fs_io, &group->gd_buf);
		if (ret)
			goto bail;

		ret = ocfs2_read_group_desc(fs, gd_blkno, group->gd_buf);
		if (ret)
			goto bail;

		group->blkno = gd_blkno;
		gd = (struct ocfs2_group_desc *)group->gd_buf;
		gd_blkno = gd->bg_next_group;
	}

	group = group_head;
	while (group) {
		gd = (struct ocfs2_group_desc *)group->gd_buf;

		end = 1;
		/* Modify the "Sub Alloc Slot" in the extent block/inodes. */
		while (end < gd->bg_bits) {
			start = ocfs2_find_next_bit_set(gd->bg_bitmap,
							gd->bg_bits, end);
			if (start >= gd->bg_bits)
				break;

			end = ocfs2_find_next_bit_clear(gd->bg_bitmap,
							gd->bg_bits, start);

			for (i = start; i < end; i++) {
				blkno = group->blkno + i;

				ret = change_sub_alloc_slot(fs, blkno, ctxt);
				if (ret)
					goto bail;

			}
		}

		/* move the group to the new slots. */
		ret = move_group(fs, ctxt, group);
		if (ret)
			goto bail;

		group = group->next;
	}

bail:
	group = group_head;
	while (group) {
		group_head = group->next;
		if (group->gd_buf)
			ocfs2_free(&group->gd_buf);
		ocfs2_free(&group);
		group = group_head;
	}
	return ret;
}

static errcode_t relink_system_alloc(ocfs2_filesys *fs,
				     uint16_t removed_slot,
				     uint16_t new_slots,
				     int inode_type)
{
	errcode_t ret;
	int16_t i;
	uint64_t blkno;
	struct ocfs2_dinode *di = NULL;
	struct ocfs2_chain_list *cl = NULL;
	struct relink_ctxt ctxt;
	char fname[OCFS2_MAX_FILENAME_LEN];

	memset(&ctxt, 0, sizeof(ctxt));

	ocfs2_sprintf_system_inode_name(fname, OCFS2_MAX_FILENAME_LEN,
					inode_type, removed_slot);
	verbosef(VL_APP, "Relinking system allocator \"%s\"\n", fname);

	ret = ocfs2_lookup_system_inode(fs, inode_type,
					removed_slot, &blkno);
	if (ret) {
		verbosef(VL_APP, "%s while looking up the allocator\n",
			 error_message(ret));
		goto bail;
	}

	ret = ocfs2_malloc_block(fs->fs_io, &ctxt.src_inode);
	if (ret) {
		verbosef(VL_APP,
			 "%s while allocating the inode buffer\n",
			 error_message(ret));
		goto bail;
	}

	ret = ocfs2_read_inode(fs, blkno, ctxt.src_inode);
	if (ret) {
		verbosef(VL_APP,
			 "%s while reading allocator inode %"PRIu64"\n",
			 error_message(ret), blkno);
		goto bail;
	}

	di = (struct ocfs2_dinode *)ctxt.src_inode;

	if (!(di->i_flags & OCFS2_VALID_FL) ||
	    !(di->i_flags & OCFS2_BITMAP_FL) ||
	    !(di->i_flags & OCFS2_CHAIN_FL)) {
		verbosef(VL_APP, "Allocator inode %"PRIu64" is corrupt.\n",
			 blkno);
		goto bail;
	}

	if (di->id1.bitmap1.i_total == 0)
		goto bail;

	/* Iterate all the groups and modify the group descriptors accordingly. */
	ret = ocfs2_malloc_block(fs->fs_io, &ctxt.ex_buf);
	if (ret) {
		verbosef(VL_APP,
			 "%s while allocating an extent block buffer\n",
			 error_message(ret));
		goto bail;
	}

	ret = ocfs2_malloc_block(fs->fs_io, &ctxt.dst_inode);
	if (ret) {
		verbosef(VL_APP,
			 "%s while allocating the destination inode buffer\n",
			 error_message(ret));
		goto bail;
	}

	cl = &di->id2.i_chain;
	ctxt.inode_type = inode_type;

	/*iterate all the chain record and move them to the new slots. */
	for (i = cl->cl_next_free_rec - 1; i >= 0; i--) {
		ctxt.new_slot = i % new_slots;
		ret = ocfs2_lookup_system_inode(fs, inode_type,
						ctxt.new_slot,
						&ctxt.dst_blkno);
		if (ret) {
			verbosef(VL_APP,
				 "%s while finding the target allocator "
				 "for slot %d\n",
				 error_message(ret), ctxt.new_slot);
			goto bail;
		}

		ret = ocfs2_read_inode(fs, ctxt.dst_blkno, ctxt.dst_inode);
		if (ret) {
			verbosef(VL_APP,
				 "%s while reading target allocator inode "
				 "%"PRIu64"\n",
				 error_message(ret), ctxt.dst_blkno);
			goto bail;
		}

		ctxt.cr = &cl->cl_recs[i];

		ret = move_chain_rec(fs, &ctxt);
		if (ret) {
			verbosef(VL_APP,
				"%s while trying to move a chain record "
				"to the allocator in slot %d\n",
				error_message(ret), ctxt.new_slot);
			goto bail;
		}
	}


	/* emtpy the original alloc files. */
	di->id1.bitmap1.i_used = 0;
	di->id1.bitmap1.i_total = 0;
	di->i_clusters = 0;
	di->i_size = 0;

	cl = &di->id2.i_chain;
	cl->cl_next_free_rec = 0;
	memset(cl->cl_recs, 0, sizeof(struct ocfs2_chain_rec) * cl->cl_count);

	ret = ocfs2_write_inode(fs, blkno, ctxt.src_inode);
	if (ret)
		verbosef(VL_APP,
			 "%s while writing out the empty allocator inode\n",
			 error_message(ret));

bail:
	if (ctxt.ex_buf)
		ocfs2_free(&ctxt.ex_buf);
	if (ctxt.dst_inode)
		ocfs2_free(&ctxt.dst_inode);
	if (ctxt.src_inode)
		ocfs2_free(&ctxt.src_inode);

	if (!ret)
		verbosef(VL_APP, "Successfully relinked allocator \"%s\"\n",
			 fname);
	return ret;
}

/* Empty the content of the specified journal file.
 * Most of the code is copied from ocfs2_format_journal.
 */
static errcode_t empty_journal(ocfs2_filesys *fs,
			       ocfs2_cached_inode *ci)
{
	errcode_t ret = 0;
	char *buf = NULL;
	int bs_bits = OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;
	uint64_t offset = 0;
	uint32_t wrote, count;

#define BUFLEN	1048576
	ret = ocfs2_malloc_blocks(fs->fs_io, (BUFLEN >> bs_bits), &buf);
	if (ret)
		goto out;
	memset(buf, 0, BUFLEN);

	count = (uint32_t) ci->ci_inode->i_size;
	while (count) {
		ret = ocfs2_file_write(ci, buf, ocfs2_min((uint32_t) BUFLEN, count),
				       offset, &wrote);
		if (ret)
			goto out;
		offset += wrote;
		count -= wrote;
	}

out:
	return ret;
}

static errcode_t empty_and_truncate_journal(ocfs2_filesys *fs,
					    uint16_t removed_slot)
{
	errcode_t ret;
	uint64_t blkno;
	ocfs2_cached_inode *ci = NULL;
	char fname[OCFS2_MAX_FILENAME_LEN];

	ocfs2_sprintf_system_inode_name(fname, OCFS2_MAX_FILENAME_LEN,
					JOURNAL_SYSTEM_INODE,
					removed_slot);
	verbosef(VL_APP, "Truncating journal \"%s\"\n", fname);

	ret = ocfs2_lookup_system_inode(fs, JOURNAL_SYSTEM_INODE,
					removed_slot, &blkno);
	if (ret) {
		verbosef(VL_APP, "%s while looking up journal \"%s\"\n",
			 error_message(ret), fname);
		goto bail;
	}

	ret = ocfs2_read_cached_inode(fs, blkno, &ci);
	if (ret) {
		verbosef(VL_APP,
			 "%s while reading journal inode %"PRIu64"\n",
			 error_message(ret), blkno);
		goto bail;
	}

	/* we have to empty the journal since it may contains some
	 * inode blocks which look like valid(except the i_blkno).
	 * So if this block range is used for future inode alloc
	 * files, fsck.ocfs2 may raise some error.
	 */
	ret = empty_journal(fs, ci);
	if (ret) {
		verbosef(VL_APP, "%s while emptying journal \"%s\"\n",
			 error_message(ret), fname);
		goto bail;
	}

	ret = ocfs2_truncate(fs, blkno, 0);
	if (ret) {
		verbosef(VL_APP, "%s while truncating journal \"%s\"\n",
			 error_message(ret), fname);
		goto bail;
	}

	verbosef(VL_APP, "Journal \"%s\" truncated\n", fname);

bail:
	if (ci)
		ocfs2_free_cached_inode(fs, ci);
	return ret;
}

static errcode_t truncate_quota_file(ocfs2_filesys *fs,
				     uint16_t removed_slot,
				     int type)
{
	errcode_t ret;
	uint64_t blkno;
	char fname[OCFS2_MAX_FILENAME_LEN];
	int local_type = (type == USRQUOTA) ? LOCAL_USER_QUOTA_SYSTEM_INODE :
					      LOCAL_GROUP_QUOTA_SYSTEM_INODE;

	ocfs2_sprintf_system_inode_name(fname, OCFS2_MAX_FILENAME_LEN,
					local_type, removed_slot);
	verbosef(VL_APP, "Truncating quota file \"%s\"\n", fname);

	ret = ocfs2_lookup_system_inode(fs, local_type, removed_slot, &blkno);
	if (!ret) {
		ret = ocfs2_truncate(fs, blkno, 0);
		if (!ret)
			verbosef(VL_APP, "Quota file \"%s\" truncated\n",
				 fname);
		else
			verbosef(VL_APP,
				 "%s while truncating quota file \"%s\"\n",
				 error_message(ret), fname);
	} else
		verbosef(VL_APP,
			 "%s while looking up quota file \"%s\"\n",
			 error_message(ret), fname);

	return ret;
}

static errcode_t truncate_quota_files(ocfs2_filesys *fs,
				      uint16_t removed_slot)
{
	errcode_t ret = 0;

	if (OCFS2_HAS_RO_COMPAT_FEATURE(OCFS2_RAW_SB(fs->fs_super),
					OCFS2_FEATURE_RO_COMPAT_USRQUOTA))
		ret = truncate_quota_file(fs, removed_slot, USRQUOTA);
	if (ret)
		return ret;
	if (OCFS2_HAS_RO_COMPAT_FEATURE(OCFS2_RAW_SB(fs->fs_super),
					OCFS2_FEATURE_RO_COMPAT_GRPQUOTA))
		ret = truncate_quota_file(fs, removed_slot, GRPQUOTA);
	return ret;
}

static errcode_t truncate_orphan_dir(ocfs2_filesys *fs,
				     uint16_t removed_slot)
{
	errcode_t ret;
	uint64_t blkno;
	char fname[OCFS2_MAX_FILENAME_LEN];

	ocfs2_sprintf_system_inode_name(fname, OCFS2_MAX_FILENAME_LEN,
					ORPHAN_DIR_SYSTEM_INODE,
					removed_slot);
	verbosef(VL_APP, "Truncating orphan dir \"%s\"\n", fname);

	ret = ocfs2_lookup_system_inode(fs, ORPHAN_DIR_SYSTEM_INODE,
					removed_slot, &blkno);
	if (!ret) {
		ret = ocfs2_truncate(fs, blkno, 0);
		if (!ret)
			verbosef(VL_APP, "Orphan dir \"%s\" truncated\n",
				 fname);
		else
			verbosef(VL_APP,
				 "%s while truncating orphan dir \"%s\"\n",
				 error_message(ret), fname);
	} else
		verbosef(VL_APP,
			 "%s while looking up orphan dir \"%s\"\n",
			 error_message(ret), fname);

	return ret;
}

static int remove_slot_iterate(struct ocfs2_dir_entry *dirent,
				uint64_t blocknr, int offset, int blocksize,
				char *buf, void *priv_data)
{
	struct remove_slot_ctxt *ctxt =
		(struct remove_slot_ctxt *)priv_data;
	int taillen, ret_flags = 0;
	errcode_t ret;
	char dname[OCFS2_MAX_FILENAME_LEN];
	char tail[OCFS2_MAX_FILENAME_LEN];

	sprintf(tail, ":%04d", ctxt->removed_slot);
	taillen = strlen(tail);

	strncpy(dname, dirent->name, dirent->name_len);
	dname[dirent->name_len] = '\0';

	if (!strcmp(dname + (dirent->name_len - taillen), tail)) {
		verbosef(VL_APP, "Unlinking system file \"%s\"\n",
			 dname);
		ret = ocfs2_delete_inode(ctxt->fs, dirent->inode);
		if (ret) {
			verbosef(VL_APP,
				 "%s while unlinking system file \"%s\"\n",
				 error_message(ret), dname);
			ret_flags |= OCFS2_DIRENT_ERROR;
			ctxt->errcode = ret;
		} else {
			verbosef(VL_APP,
				 "Successfully unlinked system file "
				 "\"%s\"\n",
				 dname);
			dirent->inode = 0;
			ret_flags |= OCFS2_DIRENT_CHANGED;
		}
	}

	return ret_flags;
}

static errcode_t remove_slot_entry(ocfs2_filesys *fs, uint16_t removed_slot)
{
	struct remove_slot_ctxt ctxt = {
		.fs = fs,
		.removed_slot = removed_slot,
		.errcode = 0
	};

	ocfs2_dir_iterate(fs, fs->fs_sysdir_blkno,
			  OCFS2_DIRENT_FLAG_EXCLUDE_DOTS, NULL,
			  remove_slot_iterate, &ctxt);

	return ctxt.errcode;
}

static errcode_t decrease_link_count(ocfs2_filesys *fs, uint16_t blkno)
{
	errcode_t ret;
	char *buf = NULL;
	struct ocfs2_dinode *di  = NULL;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		goto bail;

	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret)
		goto bail;

	di = (struct ocfs2_dinode *)buf;

	if (di->i_links_count > 0)
		di->i_links_count--;
	else {
		ret = OCFS2_ET_INODE_NOT_VALID;
		goto bail;
	}

	ret = ocfs2_write_inode(fs, blkno, buf);
bail:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

static int orphan_iterate(struct ocfs2_dir_entry *dirent,
			uint64_t blocknr, int offset, int blocksize,
			char *buf, void *priv_data)
{
	int *has_orphan = (int *)priv_data;

	*has_orphan = 1;

	/* we have found some file/dir in the orphan_dir,
	 * so there is no need to go on the iteration.
	 */
	return OCFS2_DIRENT_ABORT;
}

static errcode_t orphan_dir_check(ocfs2_filesys *fs,
				  uint16_t new_slots)
{
	errcode_t ret = 0;
	uint64_t blkno;
	int i, has_orphan = 0;
	uint16_t max_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;

	for (i = new_slots ; i < max_slots; ++i) {
		ret = ocfs2_lookup_system_inode(fs, ORPHAN_DIR_SYSTEM_INODE,
						i, &blkno);
		if (ret) {
			verbosef(VL_APP,
				 "%s while looking up orphan dir for "
				 "slot %u during orphan dir check\n",
				 error_message(ret), i);
			break;
		}

		ret = ocfs2_dir_iterate(fs, blkno,
					OCFS2_DIRENT_FLAG_EXCLUDE_DOTS, NULL,
					orphan_iterate, &has_orphan);

		if (has_orphan) {
			ret = TUNEFS_ET_ORPHAN_DIR_NOT_EMPTY;
			verbosef(VL_APP,
				 "Entries found in orphan dir for slot %u\n",
				 i);
			break;
		}
	}

	return ret;
}

static errcode_t local_alloc_check(ocfs2_filesys *fs,
				   uint16_t new_slots)
{
	errcode_t ret = 0;
	uint16_t i;
	uint64_t blkno;
	char *buf = NULL;
	struct ocfs2_dinode *di = NULL;

	uint16_t max_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret) {
		verbosef(VL_APP,
			 "%s while allocating inode buffer for local "
			 "alloc check\n",
			 error_message(ret));
		goto bail;
	}

	for (i = new_slots ; i < max_slots; ++i) {
		ret = ocfs2_lookup_system_inode(fs, LOCAL_ALLOC_SYSTEM_INODE,
						i, &blkno);
		if (ret) {
			verbosef(VL_APP,
				 "%s while looking up local alloc for "
				 "slot %u during local alloc check\n",
				 error_message(ret), i);
			break;
		}

		ret = ocfs2_read_inode(fs, blkno, buf);
		if (ret) {
			verbosef(VL_APP,
				 "%s while reading inode %"PRIu64" "
				 "during local alloc check\n",
				 error_message(ret), blkno);
			break;
		}

		di = (struct ocfs2_dinode *)buf;
		if (di->id1.bitmap1.i_total > 0) {
			ret = TUNEFS_ET_LOCAL_ALLOC_NOT_EMPTY;
			verbosef(VL_APP,
				 "Local alloc for slot %u is not empty\n",
				 i);
			break;
		}
	}

bail:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

static errcode_t truncate_log_check(ocfs2_filesys *fs,
				    uint16_t new_slots)
{
	errcode_t ret = 0;
	uint16_t i;
	uint64_t blkno;
	char *buf = NULL;
	struct ocfs2_dinode *di = NULL;

	uint16_t max_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret) {
		verbosef(VL_APP,
			 "%s while allocating inode buffer for "
			 "truncate log check\n",
			 error_message(ret));
		goto bail;
	}

	for (i = new_slots; i < max_slots; ++i) {
		ret = ocfs2_lookup_system_inode(fs, TRUNCATE_LOG_SYSTEM_INODE,
						i, &blkno);
		if (ret) {
			verbosef(VL_APP,
				 "%s while looking up truncate log for "
				 "slot %u during truncate log check\n",
				 error_message(ret), i);
			goto bail;
		}

		ret = ocfs2_read_inode(fs, blkno, buf);
		if (ret) {
			verbosef(VL_APP,
				 "%s while reading inode %"PRIu64" "
				 "during truncate log check\n",
				 error_message(ret), blkno);
			goto bail;
		}

		di = (struct ocfs2_dinode *)buf;

		if (di->id2.i_dealloc.tl_used > 0) {
			ret = TUNEFS_ET_TRUNCATE_LOG_NOT_EMPTY;
			verbosef(VL_APP,
				 "Truncate log for slot %u is not empty\n",
				 i);
			goto bail;
		}
	}

bail:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

static errcode_t remove_slot_check(ocfs2_filesys *fs, int num_slots)
{
	errcode_t ret;

	ret = orphan_dir_check(fs, num_slots);
	if (!ret)
		ret = local_alloc_check(fs, num_slots);
	if (!ret)
		ret = truncate_log_check(fs, num_slots);

	return ret;
}

static errcode_t remove_slots(ocfs2_filesys *fs, int num_slots)
{
	errcode_t ret;
	uint16_t old_num = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;
	uint16_t removed_slot = old_num - 1;
	struct tools_progress *prog = NULL;

	ret = remove_slot_check(fs, num_slots);
	if (ret)
		goto bail;

	/* We have seven steps in removing each slot */
	prog = tools_progress_start("Removing slots", "rmslots",
				    (old_num - num_slots) * 8);
	if (!prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		goto bail;
	}

	/* This is cleared up in update_slot_count() if everything works */
	ret = tunefs_set_in_progress(fs, OCFS2_TUNEFS_INPROG_REMOVE_SLOT);
	if (ret)
		goto bail;

	/* we will remove the slots once at a time so that fsck.ocfs2 can work
	 * well and we can continue our work easily in case of any panic.
	 */
	while (removed_slot >= num_slots) {
		/* Link the specified extent alloc file to others. */
		ret = relink_system_alloc(fs, removed_slot, num_slots,
					  EXTENT_ALLOC_SYSTEM_INODE);
		if (ret)
			goto bail;
		tools_progress_step(prog, 1);

		/* Link the specified inode alloc file to others. */
		ret = relink_system_alloc(fs, removed_slot, num_slots,
					  INODE_ALLOC_SYSTEM_INODE);
		if (ret)
			goto bail;
		tools_progress_step(prog, 1);

		/* Truncate the orphan dir to release its clusters
		 * to the global bitmap.
		 */
		ret = truncate_orphan_dir(fs, removed_slot);
		if (ret)
			goto bail;
		tools_progress_step(prog, 1);

		/* empty the content of journal and truncate its clusters. */
		ret = empty_and_truncate_journal(fs, removed_slot);
		if (ret)
			goto bail;
		tools_progress_step(prog, 1);

		/* truncate local quota files */
		ret = truncate_quota_files(fs, removed_slot);
		if (ret)
			goto bail;
		tools_progress_step(prog, 1);

		/* Now, we decrease the max_slots first and then remove the
		 * slots for the reason that:
		 *
		 * 1. ocfs2_lock_down_clusters needs to lock all the journal
		 * files. so if we delete the journal entry first and fail
		 * to decrease the max_slots, the whole cluster can't be
		 * locked any more due to the loss of journals.
		 *
		 * 2. Now all the resources except the inodes are freed
		 * so it is safe to decrease the slots first, and if any
		 * panic happens after we decrease the slots, we can ignore
		 * them, and actually if we want to increase the slot in the
		 * future, we can reuse these inodes.
		 */

		/* The slot number is updated in the super block.*/
		OCFS2_RAW_SB(fs->fs_super)->s_max_slots--;
		ret = ocfs2_write_primary_super(fs);
		if (ret)
			goto bail;
		tools_progress_step(prog, 1);

		/* The extra system dir entries should be removed. */
		ret = remove_slot_entry(fs, removed_slot);
		if (ret)
			goto bail;
		tools_progress_step(prog, 1);

		/* Decrease the i_links_count in system file directory
		 * since the orphan_dir is removed.
		 */
		ret = decrease_link_count(fs, fs->fs_sysdir_blkno);
		if (ret)
			goto bail;
		tools_progress_step(prog, 1);

		removed_slot--;
	}

bail:
	if (prog)
		tools_progress_stop(prog);

	return ret;
}

static errcode_t update_slot_count(ocfs2_filesys *fs, int num_slots)
{
	errcode_t ret = 0;
	int orig_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;
	ocfs2_fs_options null_options;

	memset(&null_options, 0, sizeof(ocfs2_fs_options));

	if (num_slots == orig_slots) {
		verbosef(VL_APP,
			 "Device \"%s\" already has %d node slots; "
			 "nothing to do\n",
			 fs->fs_devname, num_slots);
		goto out;
	}

	if (!tools_interact("Change the number of node slots on device "
			    "\"%s\" from %d to %d? ",
			    fs->fs_devname, orig_slots, num_slots))
		goto out;

	tunefs_block_signals();
	if (num_slots > orig_slots)
		ret = add_slots(fs, num_slots);
	else
		ret = remove_slots(fs, num_slots);
	if (ret)
		goto out_unblock;

	OCFS2_RAW_SB(fs->fs_super)->s_max_slots = num_slots;

	if (num_slots > orig_slots) {
		/* Grow the new journals to match the first slot */
		verbosef(VL_APP,
			 "Allocating space for the new journals\n");
		ret = tunefs_set_journal_size(fs, 0, null_options, null_options);
		if (!ret)
			verbosef(VL_APP, "Journal space allocated\n");
		else {
			verbosef(VL_APP,
				 "%s while trying to size the new journals\n",
				 error_message(ret));
			goto out_unblock;
		}
	}

	ret = ocfs2_format_slot_map(fs);
	if (ret)
		goto out_unblock;

	if (num_slots < orig_slots) {
		ret = tunefs_clear_in_progress(fs,
					       OCFS2_TUNEFS_INPROG_REMOVE_SLOT);
		if (ret)
			goto out_unblock;
	}

	ret = ocfs2_write_super(fs);

out_unblock:
	tunefs_unblock_signals();

out:
	return ret;
}

static int set_slot_count_parse_option(struct tunefs_operation *op,
				       char *arg)
{
	int rc = 1;
	char *ptr = NULL;
	long num_slots;

	if (!arg) {
		errorf("Number of slots not specified\n");
		goto out;
	}

	num_slots = strtol(arg, &ptr, 10);
	if ((num_slots == LONG_MIN) || (num_slots == LONG_MAX)) {
		errorf("Number of slots is out of range: %s\n", arg);
		goto out;
	}
	if (*ptr != '\0') {
		errorf("Invalid number: \"%s\"\n", arg);
		goto out;
	}
	if (num_slots < 1) {
		errorf("At least one slot required\n");
		goto out;
	}
	if (num_slots > INT_MAX) {
		errorf("Number of slots is out of range: %s\n", arg);
		goto out;
	}
	/*
	 * It's now safe to treat num_slots as an int.
	 *
	 * We'll re-check the maximum number of slots after we've opened
	 * the filesystem and determined the slot map format.
	 */

	op->to_private = (void *)(unsigned long)num_slots;
	rc = 0;

out:
	return rc;
}

static int set_slot_count_run(struct tunefs_operation *op,
			      ocfs2_filesys *fs, int flags)
{
	errcode_t err;
	int rc = 0;
	int num_slots = (int)(unsigned long)op->to_private;

	err = update_slot_count(fs, num_slots);
	if (err) {
		tcom_err(err,
			 "- unable to update the number of slots on device "
			 "\"%s\"",
			 fs->fs_devname);
		rc = 1;
	}

	return rc;
}


DEFINE_TUNEFS_OP(set_slot_count,
		 "Usage: op_set_slot_count [opts] <device> "
		 "<number_of_slots>\n",
		 TUNEFS_FLAG_RW | TUNEFS_FLAG_ALLOCATION,
		 set_slot_count_parse_option,
		 set_slot_count_run);

#ifdef DEBUG_EXE
int main(int argc, char *argv[])
{
	return tunefs_op_main(argc, argv, &set_slot_count_op);
}
#endif
