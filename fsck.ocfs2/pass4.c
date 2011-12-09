/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * Copyright (C) 1993-2004 by Theodore Ts'o.
 * Copyright (C) 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License, version 2,  as published by the Free Software Foundation.
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
 * --
 *
 * Pass 4 walks all the active inodes and makes sure that they are reachable
 * via directory entries, just like pass 3 did for directories.  It also 
 * makes sure each inode's link_count reflects the number of entries that
 * refer to it.  Inodes that aren't referred to by any entries are moved
 * to lost+found.
 */
#include <inttypes.h>
#include <limits.h>

#include "ocfs2/ocfs2.h"

#include "fsck.h"
#include "icount.h"
#include "pass3.h"
#include "pass4.h"
#include "problem.h"
#include "util.h"

struct orphan_dir_ctxt {
	o2fsck_state *ost;
	uint64_t orphan_dir;
};

static const char *whoami = "pass4";

static void check_link_counts(o2fsck_state *ost,
			      struct ocfs2_dinode *di,
			      uint64_t blkno)
{
	uint16_t refs, in_inode;
	errcode_t ret;

	refs = o2fsck_icount_get(ost->ost_icount_refs, blkno);
	in_inode = o2fsck_icount_get(ost->ost_icount_in_inodes, blkno);

	verbosef("ino %"PRIu64", refs %u in %u\n", blkno, refs, in_inode);

	/* XXX offer to remove files/dirs with no data? */
	if (refs == 0 &&
	    prompt(ost, PY, PR_INODE_NOT_CONNECTED,
		   "Inode %"PRIu64" isn't referenced by any "
		   "directory entries.  Move it to lost+found?", blkno)) {
		o2fsck_reconnect_file(ost, blkno);
		refs = o2fsck_icount_get(ost->ost_icount_refs, blkno);
	}

	if (refs == in_inode)
		goto out;

	ret = ocfs2_read_inode(ost->ost_fs, blkno, (char *)di);
	if (ret) {
		com_err(whoami, ret, "reading inode %"PRIu64" to update its "
			"i_links_count.  Could this be because a directory "
			"entry referenced an invalid inode but wasn't fixed?",
			blkno);
		goto out;
	}

	if (in_inode != di->i_links_count)
		com_err(whoami, OCFS2_ET_INTERNAL_FAILURE, "fsck's thinks "
			"inode %"PRIu64" has a link count of %"PRIu16" but on "
			"disk it is %"PRIu16, (uint64_t)di->i_blkno, in_inode,
			di->i_links_count);

	if (prompt(ost, PY, PR_INODE_COUNT,
		   "Inode %"PRIu64" has a link count of %"PRIu16" on "
		   "disk but directory entry references come to %"PRIu16". "
		   "Update the count on disk to match?",
		   (uint64_t)di->i_blkno, in_inode,
		   refs)) {
		di->i_links_count = refs;
		o2fsck_icount_set(ost->ost_icount_in_inodes, di->i_blkno,
				  refs);
		o2fsck_write_inode(ost, di->i_blkno, di);
	}

out:
	return;
}

static int replay_orphan_iterate(struct ocfs2_dir_entry *dirent,
				 uint64_t blocknr,
				 int	offset,
				 int	blocksize,
				 char	*buf,
				 void	*priv_data)
{
	struct orphan_dir_ctxt *ctxt = priv_data;
	o2fsck_state *ost = ctxt->ost;
	int ret_flags = 0;
	errcode_t ret = 0;

	if (!(ost->ost_fs->fs_flags & OCFS2_FLAG_RW)) {
		printf("** Skipping orphan dir replay because -n was "
		       "given.\n");
		ret_flags |= OCFS2_DIRENT_ABORT;
		goto out;
	}

	ost->ost_orphan_count++;

	/* Only ask for confirmation in force check. */
	if (ost->ost_force) {
		if (!prompt(ost, PY, PR_INODE_ORPHANED,
			   "Inode %"PRIu64" was found in the orphan directory. "
			   "Delete its contents and unlink it?",
			   (uint64_t)dirent->inode))
			goto out;
	}

	ret = ocfs2_truncate(ost->ost_fs, dirent->inode, 0);
	if (ret) {
		com_err(whoami, ret, "while truncating orphan inode %"PRIu64,
			(uint64_t)dirent->inode);
		ret_flags |= OCFS2_DIRENT_ABORT;
		goto out;
	}

	ret = ocfs2_delete_inode(ost->ost_fs, dirent->inode);
	if (ret) {
		com_err(whoami, ret, "while deleting orphan inode %"PRIu64
			"after truncating it", (uint64_t)dirent->inode);
		ret_flags |= OCFS2_DIRENT_ABORT;
		goto out;
	}

	ost->ost_orphan_deleted_count++;

	/* Only calculate icount in force check. */
	if (ost->ost_force) {
		/*
		 * this matches a special case in o2fsck_verify_inode_fields()
		 * where orphan dir members are recorded as having 1 link count,
		 * even though they have 0 on disk
		 */
		o2fsck_icount_delta(ost->ost_icount_in_inodes,
				    dirent->inode, -1);

		/*
		 * dirs have this dirent ref and their '.' dirent and we also
		 * need to handle '..' dirent for their parents.
		 */
		if (dirent->file_type == OCFS2_FT_DIR) {
			o2fsck_icount_delta(ost->ost_icount_refs,
					    dirent->inode, -2);
			o2fsck_icount_delta(ost->ost_icount_refs,
					    ctxt->orphan_dir, -1);
		} else
			o2fsck_icount_delta(ost->ost_icount_refs,
					    dirent->inode, -1);
	}

	dirent->inode = 0;
	ret_flags |= OCFS2_DIRENT_CHANGED;

out:
	ost->ost_err = ret;
	return ret_flags;
}

static errcode_t create_orphan_dir(o2fsck_state *ost, char *fname)
{
	errcode_t ret;
	uint64_t blkno;
	ocfs2_filesys *fs = ost->ost_fs;

	/* create inode for system file */
	ret = ocfs2_new_system_inode(fs, &blkno,
			ocfs2_system_inodes[ORPHAN_DIR_SYSTEM_INODE].si_mode,
			ocfs2_system_inodes[ORPHAN_DIR_SYSTEM_INODE].si_iflags);
	if (ret)
		goto bail;

	ret = ocfs2_init_dir(fs, blkno, fs->fs_sysdir_blkno);
	if (ret)
		goto bail;

	/* Add the inode to the system dir */
	ret = ocfs2_link(fs, fs->fs_sysdir_blkno, fname, blkno,
			 OCFS2_FT_DIR);
	if (ret)
		goto bail;

	/* we have created an orphan dir under system dir and updated the disk,
	 * so we have to update the refs in ost accordingly.
	 */
	o2fsck_icount_delta(ost->ost_icount_refs, fs->fs_sysdir_blkno, 1);
	o2fsck_icount_delta(ost->ost_icount_in_inodes, fs->fs_sysdir_blkno, 1);
bail:
	return ret;
}

/*
 * replay_orphan_dir could happen in 2 places and we handle it diffrently.
 * 1. In slot recovery, we will return any error which lead to a force check.
 * 2. in o2fsck_pass4, all other errors should be fixed in pass0,1,2 and 3, so
 *    we try to fix some errors by ourselves.
 */
errcode_t replay_orphan_dir(o2fsck_state *ost, int slot_recovery)
{
	errcode_t ret = OCFS2_ET_CORRUPT_SUPERBLOCK;
	char name[PATH_MAX];
	uint64_t ino;
	int bytes;
	int i;
	int num_slots = OCFS2_RAW_SB(ost->ost_fs->fs_super)->s_max_slots;
	struct orphan_dir_ctxt ctxt;

	ctxt.ost = ost;
	for (i = 0; i < num_slots; ++i) {
		bytes = ocfs2_sprintf_system_inode_name(name, PATH_MAX,
				ORPHAN_DIR_SYSTEM_INODE, i);
		if (bytes < 1) {
			ret = OCFS2_ET_INTERNAL_FAILURE;
			goto out;
		}

		ret = ocfs2_lookup(ost->ost_fs, ost->ost_fs->fs_sysdir_blkno,
				   name, bytes, NULL, &ino);
		if (ret) {
			if (slot_recovery)
				goto out;

			if (ret != OCFS2_ET_FILE_NOT_FOUND)
				goto out;

			/* orphan dir is missing, it may be caused by an
			 * unsuccessful removing slots in tunefs.ocfs2.
			 * so create it.
			 */
	   		if (prompt(ost, PY, PR_ORPHAN_DIR_MISSING,
				   "%s is missing in system directory. "
				   "Create it?", name)) {
				ret = create_orphan_dir(ost, name);
				if (ret) {
					com_err(whoami, ret, "while creating"
						"orphan directory %s", name);
					continue;
				}
			}
		}

		ctxt.orphan_dir = ino;
		ost->ost_err = 0;
		ret = ocfs2_dir_iterate(ost->ost_fs, ino,
					OCFS2_DIRENT_FLAG_EXCLUDE_DOTS, NULL,
					replay_orphan_iterate, &ctxt);
		if (!ret)
			ret = ost->ost_err;
		if (ret && slot_recovery)
			break;
	}

out:
	return ret;
}

/* return the next inode that has either directory entries pointing to it or
 * that was valid and had a non-zero i_links_count.  OCFS2_ET_BIT_NOT_FOUND
 * will be bubbled up from the next_blkno() calls when there is no such next
 * inode.  It is expected that sometimes these won't match.  If a directory
 * has been lost there can be inodes with i_links_count and no directory
 * entries at all.  If an inode was lost but the user chose not to erase
 * the directory entries then there may be references to inodes that
 * we never saw the i_links_count for */
static errcode_t next_inode_any_ref(o2fsck_state *ost, uint64_t start,
				    uint64_t *blkno_ret)
{
	errcode_t tmp, ret = OCFS2_ET_BIT_NOT_FOUND;
	uint64_t blkno;

	tmp = o2fsck_icount_next_blkno(ost->ost_icount_refs, start, &blkno);
	if (tmp == 0) {
		*blkno_ret = blkno;
		ret = 0;
	}

	tmp = o2fsck_icount_next_blkno(ost->ost_icount_in_inodes, start,
				       &blkno);
	/* use this if we didn't have one yet or this one's lesser */
	if (tmp == 0 && (ret != 0 || (blkno < *blkno_ret))) {
		ret = 0;
		*blkno_ret = blkno;
	}

	return ret;
}

errcode_t o2fsck_pass4(o2fsck_state *ost)
{
	struct ocfs2_dinode *di;
	char *buf = NULL;
	errcode_t ret;
	uint64_t blkno = 0, start;
	ocfs2_filesys *fs = ost->ost_fs;
	struct o2fsck_resource_track rt;

	printf("Pass 4a: Checking for orphaned inodes\n");

	o2fsck_init_resource_track(&rt, fs->fs_io);

	ret = replay_orphan_dir(ost, 0);
	if (ret) {
		com_err(whoami, ret, "while trying to replay the orphan "
			"directory");
		goto out;
	}

	o2fsck_compute_resource_track(&rt, fs->fs_io);
	o2fsck_print_resource_track("Pass 4a", ost, &rt, fs->fs_io);
	o2fsck_add_resource_track(&ost->ost_rt, &rt);

	printf("Pass 4b: Checking inodes link counts\n");

	o2fsck_init_resource_track(&rt, fs->fs_io);

	ret = ocfs2_malloc_block(ost->ost_fs->fs_io, &buf);
	if (ret) {
		com_err(whoami, ret, "while allocating space to read inodes");
		goto out;
	}

	di = (struct ocfs2_dinode *)buf;
	start = 0;

	while (next_inode_any_ref(ost, start, &blkno) == 0) {
		check_link_counts(ost, di, blkno);
		start = blkno + 1;
	}

	o2fsck_compute_resource_track(&rt, fs->fs_io);
	o2fsck_print_resource_track("Pass 4b", ost, &rt, fs->fs_io);
	o2fsck_add_resource_track(&ost->ost_rt, &rt);

out:
	if (buf)
		ocfs2_free(&buf);

	return ret;
}
