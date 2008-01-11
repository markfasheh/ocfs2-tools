/*
 * remove_slot.c
 *
 * The function for removing slots from ocfs2 volume.
 *
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 */

#include <inttypes.h>

#include <assert.h>

#include "tunefs.h"

extern ocfs2_tune_opts opts;

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

	memset(&ctxt, 0, sizeof(ctxt));

	ret = ocfs2_lookup_system_inode(fs, inode_type,
					removed_slot, &blkno);
	if (ret)
		goto bail;

	ret = ocfs2_malloc_block(fs->fs_io, &ctxt.src_inode);
	if (ret) {
		com_err(opts.progname, ret, "while allocating a block "
			"during relinking system alloc");
		goto bail;
	}

	ret = ocfs2_read_inode(fs, blkno, ctxt.src_inode);
	if (ret) {
		com_err(opts.progname, ret, "while reading inode "
			"%"PRIu64" during relinking system alloc", blkno);
		goto bail;
	}

	di = (struct ocfs2_dinode *)ctxt.src_inode;

	if (!(di->i_flags & OCFS2_VALID_FL) ||
	    !(di->i_flags & OCFS2_BITMAP_FL) ||
	    !(di->i_flags & OCFS2_CHAIN_FL)) {
		com_err(opts.progname, 0, "system  alloc %"PRIu64" corrupts."
			"during relinking system alloc", blkno);
		goto bail;
	}

	if (di->id1.bitmap1.i_total == 0)
		goto bail;

	/* Iterate all the groups and modify the group descriptors accordingly. */
	ret = ocfs2_malloc_block(fs->fs_io, &ctxt.ex_buf);
	if (ret) {
		com_err(opts.progname, ret, "while allocating a block "
			"during relinking system alloc");
		goto bail;
	}

	ret = ocfs2_malloc_block(fs->fs_io, &ctxt.dst_inode);
	if (ret) {
		com_err(opts.progname, ret, "while allocating a block "
			"during relinking system alloc");
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
		if (ret)
			goto bail;

		ret = ocfs2_read_inode(fs, ctxt.dst_blkno, ctxt.dst_inode);
		if (ret)
			goto bail;

		ctxt.cr = &cl->cl_recs[i];

		ret = move_chain_rec(fs, &ctxt);
		if (ret) {
			com_err(opts.progname, ret,
				"while iterating system alloc file");
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

bail:
	if (ctxt.ex_buf)
		ocfs2_free(&ctxt.ex_buf);
	if (ctxt.dst_inode)
		ocfs2_free(&ctxt.dst_inode);
	if (ctxt.src_inode)
		ocfs2_free(&ctxt.src_inode);

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

	ret = ocfs2_lookup_system_inode(fs, JOURNAL_SYSTEM_INODE,
					removed_slot, &blkno);
	if (ret)
		goto bail;

	ret = ocfs2_read_cached_inode(fs, blkno, &ci);
	if (ret)
		goto bail;

	/* we have to empty the journal since it may contains some
	 * inode blocks which look like valid(except the i_blkno).
	 * So if this block range is used for future inode alloc
	 * files, fsck.ocfs2 may raise some error.
	 */
	ret = empty_journal(fs, ci);
	if (ret)
		goto bail;

	ret = ocfs2_truncate(fs, blkno, 0);
	if (ret)
		goto bail;
bail:
	if (ci)
		ocfs2_free_cached_inode(fs, ci);
	return ret;
}

static errcode_t truncate_orphan_dir(ocfs2_filesys *fs,
				     uint16_t removed_slot)
{
	errcode_t ret;
	uint64_t blkno;

	ret = ocfs2_lookup_system_inode(fs, ORPHAN_DIR_SYSTEM_INODE,
					removed_slot, &blkno);
	if (ret)
		goto bail;

	ret = ocfs2_truncate(fs, blkno, 0);
bail:
	return ret;
}

static int remove_slot_iterate(struct ocfs2_dir_entry *dirent, int offset,
			       int blocksize, char *buf, void *priv_data)
{
	struct remove_slot_ctxt *ctxt = (struct remove_slot_ctxt *)priv_data;
	char tmp = dirent->name[dirent->name_len];
	int ret_flags = 0;
	errcode_t ret;
	char fname[SYSTEM_FILE_NAME_MAX];

	sprintf(fname, "%04d", ctxt->removed_slot);

	dirent->name[dirent->name_len] = '\0';
	if (strstr(dirent->name, fname)) {
		ret = ocfs2_delete_inode(ctxt->fs, dirent->inode);
		if (ret) {
			ret_flags |= OCFS2_DIRENT_ERROR;
			ctxt->errcode = ret;
			goto out;
		}

		dirent->inode = 0;
		ret_flags |= OCFS2_DIRENT_CHANGED;
	}

out:
	dirent->name[dirent->name_len] = tmp;
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

errcode_t remove_slots(ocfs2_filesys *fs)
{
	errcode_t ret = 0;
	uint16_t old_num = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;
	uint16_t removed_slot = old_num - 1;

	/* we will remove the slots once at a time so that fsck.ocfs2 can work
	 * well and we can continue our work easily in case of any panic.
	 */
	while (removed_slot >= opts.num_slots) {
		/* Link the specified extent alloc file to others. */
		ret = relink_system_alloc(fs, removed_slot, opts.num_slots,
					  EXTENT_ALLOC_SYSTEM_INODE);
		if (ret)
			goto bail;

		/* Link the specified inode alloc file to others. */
		ret = relink_system_alloc(fs, removed_slot, opts.num_slots,
					  INODE_ALLOC_SYSTEM_INODE);
		if (ret)
			goto bail;

		/* Truncate the orphan dir to release its clusters
		 * to the global bitmap.
		 */
		ret = truncate_orphan_dir(fs, removed_slot);
		if (ret)
			goto bail;

		/* empty the content of journal and truncate its clusters. */
		ret = empty_and_truncate_journal(fs, removed_slot);
		if (ret)
			goto bail;

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
		ret = ocfs2_write_super(fs);
		if (ret)
			goto bail;

		/* The extra system dir entries should be removed. */
		ret = remove_slot_entry(fs, removed_slot);
		if (ret)
			goto bail;

		/* Decrease the i_links_count in system file directory
		 * since the orphan_dir is removed.
		 */
		ret = decrease_link_count(fs, fs->fs_sysdir_blkno);
		if (ret)
			goto bail;

		removed_slot--;
	}

bail:
	return ret;
}

static int orphan_iterate(struct ocfs2_dir_entry *dirent, int offset,
			  int blocksize, char *buf, void *priv_data)
{
	int *has_orphan = (int *)priv_data;

	*has_orphan = 1;

	/* we have found some file/dir in the orphan_dir,
	 * so there is no need to go on the iteration.
	 */
	return OCFS2_DIRENT_ABORT;
}

static errcode_t orphan_dir_check(ocfs2_filesys *fs,
				  uint16_t new_slots,
				  int *has_orphan)
{
	errcode_t ret = 0;
	uint64_t blkno;
	int i;
	uint16_t max_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;

	for (i = new_slots ; i < max_slots; ++i) {
		ret = ocfs2_lookup_system_inode(fs, ORPHAN_DIR_SYSTEM_INODE,
						i, &blkno);
		if (ret) {
			com_err(opts.progname, ret, "while looking up "
				"orphan dir for slot %u during orphan dir "
				"check", i);
			goto bail;
		}

		ret = ocfs2_dir_iterate(fs, blkno,
					OCFS2_DIRENT_FLAG_EXCLUDE_DOTS, NULL,
					orphan_iterate, has_orphan);

		if (*has_orphan) {
			com_err(opts.progname, 0, "orphan dir for slot %u "
				"has entries", i);
			goto bail;
		}
	}

bail:
	return ret;
}

static errcode_t local_alloc_check(ocfs2_filesys *fs,
				   uint16_t new_slots,
				  int *has_local_alloc)
{
	errcode_t ret = 0;
	uint16_t i;
	uint64_t blkno;
	char *buf = NULL;
	struct ocfs2_dinode *di = NULL;

	uint16_t max_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret) {
		com_err(opts.progname, ret, "while allocating a block "
			"during local alloc check");
		goto bail;
	}

	for (i = new_slots ; i < max_slots; ++i) {
		ret = ocfs2_lookup_system_inode(fs, LOCAL_ALLOC_SYSTEM_INODE,
						i, &blkno);
		if (ret) {
			com_err(opts.progname, ret, "while looking up "
				"local alloc for slot %u during local alloc "
				"check", i);
			goto bail;
		}

		ret = ocfs2_read_inode(fs, blkno, buf);
		if (ret) {
			com_err(opts.progname, ret, "while reading inode "
				"%"PRIu64" during local alloc check", blkno);
			goto bail;
		}

		di = (struct ocfs2_dinode *)buf;

		if (di->id1.bitmap1.i_total > 0) {
			*has_local_alloc = 1;
			com_err(opts.progname, 0, "local alloc for slot %u "
				"isn't empty", i);
			goto bail;
		}
	}

bail:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

static errcode_t truncate_log_check(ocfs2_filesys *fs,
				    uint16_t new_slots,
				    int *has_truncate_log)
{
	errcode_t ret = 0;
	uint16_t i;
	uint64_t blkno;
	char *buf = NULL;
	struct ocfs2_dinode *di = NULL;

	uint16_t max_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret) {
		com_err(opts.progname, ret, "while allocating a block "
			"during truncate log check");
		goto bail;
	}

	for (i = new_slots; i < max_slots; ++i) {
		ret = ocfs2_lookup_system_inode(fs, TRUNCATE_LOG_SYSTEM_INODE,
						i, &blkno);
		if (ret) {
			com_err(opts.progname, ret, "while looking up "
				"truncate log for slot %u during truncate log "
				"check", i);
			goto bail;
		}

		ret = ocfs2_read_inode(fs, blkno, buf);
		if (ret) {
			com_err(opts.progname, ret, "while reading inode "
				"%"PRIu64" during truncate log check", blkno);
			goto bail;
		}

		di = (struct ocfs2_dinode *)buf;

		if (di->id2.i_dealloc.tl_used > 0) {
			*has_truncate_log = 1;
			com_err(opts.progname, 0, "truncate log for slot %u "
				"isn't empty", i);
			goto bail;
		}
	}

bail:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

errcode_t remove_slot_check(ocfs2_filesys *fs)
{
	errcode_t ret;
	int has_orphan = 0, has_truncate_log = 0, has_local_alloc = 0;

	/* we don't allow remove_slot to coexist with other tunefs
	 * options to keep things simple.
	 */
	if (opts.backup_super ||opts.vol_label ||
	     opts.mount || opts.jrnl_size || opts.num_blocks ||
	     opts.list_sparse || opts.feature_string) {
		com_err(opts.progname, 0, "Cannot remove slot"
			" along with other tasks");
		exit(1);
	}

	ret = orphan_dir_check(fs, opts.num_slots, &has_orphan);
	if (ret || has_orphan) {
		ret = 1;
		goto bail;
	}

	ret = local_alloc_check(fs, opts.num_slots, &has_local_alloc);
	if (ret || has_local_alloc) {
		ret = 1;
		goto bail;
	}

	ret = truncate_log_check(fs, opts.num_slots, &has_truncate_log);
	if (ret || has_truncate_log) {
		ret = 1;
		goto bail;
	}
bail:
	return ret;
}
