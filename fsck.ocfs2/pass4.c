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

#include <ocfs2.h>

#include "fsck.h"
#include "icount.h"
#include "pass3.h"
#include "pass4.h"
#include "problem.h"
#include "util.h"

static const char *whoami = "pass4";

static errcode_t check_link_counts(o2fsck_state *ost, ocfs2_dinode *di,
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
		   "directory entries.  Move it to lost+found?", 
		   di->i_blkno)) {
		o2fsck_reconnect_file(ost, blkno);
		refs = o2fsck_icount_get(ost->ost_icount_refs, blkno);
	}

	if (refs == in_inode)
		goto out;

	ret = ocfs2_read_inode(ost->ost_fs, blkno, (char *)di);
	if (ret) {
		com_err(whoami, ret, "reading inode %"PRIu64" to update its "
			"i_links_count", blkno);
		goto out;
	}

	if (in_inode != di->i_links_count)
		com_err(whoami, OCFS2_ET_INTERNAL_FAILURE, "fsck's thinks "
			"inode %"PRIu64" has a link count of %"PRIu16" but on "
			"disk it is %"PRIu16, di->i_blkno, in_inode, 
			di->i_links_count);

	if (prompt(ost, PY, PR_INODE_COUNT,
		   "Inode %"PRIu64" has a link count of %"PRIu16" on "
		   "disk but directory entry references come to %"PRIu16". "
		   "Update the count on disk to match?", di->i_blkno, in_inode, 
		   refs)) {
		di->i_links_count = refs;
		o2fsck_icount_set(ost->ost_icount_in_inodes, di->i_blkno,
				  refs);
		o2fsck_write_inode(ost, di->i_blkno, di);
	}

out:
	return 0;
}

static int replay_orphan_iterate(struct ocfs2_dir_entry *dirent,
				 int	offset,
				 int	blocksize,
				 char	*buf,
				 void	*priv_data)
{
	o2fsck_state *ost = priv_data;
	int ret_flags = 0;
	errcode_t ret;

	if (!(ost->ost_fs->fs_flags & OCFS2_FLAG_RW)) {
		printf("** Skipping orphan dir replay because -n was "
		       "given.\n");
		ret_flags |= OCFS2_DIRENT_ABORT;
		goto out;
	}

	if (!prompt(ost, PY, PR_INODE_ORPHANED,
		   "Inode %"PRIu64" was found in the orphan directory. "
		   "Delete its contents and unlink it?", dirent->inode)) {
		goto out;
	}

	ret = ocfs2_truncate(ost->ost_fs, dirent->inode, 0);
	if (ret) {
		com_err(whoami, ret, "while truncating orphan inode %"PRIu64,
			dirent->inode);
		ret_flags |= OCFS2_DIRENT_ABORT;
		goto out;
	}

	ret = ocfs2_delete_inode(ost->ost_fs, dirent->inode);
	if (ret) {
		com_err(whoami, ret, "while deleting orphan inode %"PRIu64
			"after truncating it", dirent->inode);
		ret_flags |= OCFS2_DIRENT_ABORT;
		goto out;
	}

	/* this matches a special case in o2fsck_verify_inode_fields() where
	 * orphan dir members are recorded as having 1 link count, even
	 * though they have 0 on disk */
	o2fsck_icount_delta(ost->ost_icount_in_inodes, dirent->inode, -1);

	/* dirs have this dirent ref and their '.' dirent */
	if (dirent->file_type == OCFS2_FT_DIR)
		o2fsck_icount_delta(ost->ost_icount_refs, dirent->inode, -2);
	else
		o2fsck_icount_delta(ost->ost_icount_refs, dirent->inode, -1);

	dirent->inode = 0;
	ret_flags |= OCFS2_DIRENT_CHANGED;

out:
	return ret_flags;
}

static errcode_t replay_orphan_dir(o2fsck_state *ost)
{
	errcode_t ret = OCFS2_ET_CORRUPT_SUPERBLOCK;
	char name[PATH_MAX];
	uint64_t ino;
	int bytes;
	int i;
	int num_nodes = OCFS2_RAW_SB(ost->ost_fs->fs_super)->s_max_nodes;

	for (i = 0; i < num_nodes; ++i) {
		bytes = ocfs2_sprintf_system_inode_name(name, PATH_MAX,
				ORPHAN_DIR_SYSTEM_INODE, i);
		if (bytes < 1) {
			ret = OCFS2_ET_INTERNAL_FAILURE;
			goto out;
		}

		ret = ocfs2_lookup(ost->ost_fs, ost->ost_fs->fs_sysdir_blkno,
				   name, bytes, NULL, &ino);
		if (ret)
			goto out;

		ret = ocfs2_dir_iterate(ost->ost_fs, ino,
					OCFS2_DIRENT_FLAG_EXCLUDE_DOTS, NULL,
					replay_orphan_iterate, ost);
	}

out:
	return ret;
}

errcode_t o2fsck_pass4(o2fsck_state *ost)
{
	ocfs2_dinode *di;
	char *buf = NULL;
	errcode_t ret, ref_ret, inode_ret;
	uint64_t blkno, ref_blkno, inode_blkno;

	printf("Pass 4a: checking for orphaned inodes\n");

	ret = replay_orphan_dir(ost);
	if (ret) {
		com_err(whoami, ret, "while trying to replay the orphan "
			"directory");
		goto out;
	}

	printf("Pass 4b: Checking inodes link counts.\n");

	ret = ocfs2_malloc_block(ost->ost_fs->fs_io, &buf);
	if (ret) {
		com_err(whoami, ret, "while allocating space to read inodes");
		goto out;
	}

	di = (ocfs2_dinode *)buf;

	for(blkno = 0, ref_ret = 0; ref_ret != OCFS2_ET_BIT_NOT_FOUND ;
	    blkno = ref_blkno + 1) {
		ref_blkno = 0;
		ref_ret = o2fsck_icount_next_blkno(ost->ost_icount_refs, blkno,
						   &ref_blkno);
		inode_blkno = 0;
		inode_ret = o2fsck_icount_next_blkno(ost->ost_icount_in_inodes,
						     blkno, &inode_blkno);

		verbosef("ref %"PRIu64" ino %"PRIu64"\n", ref_blkno,
			 inode_blkno);

		if (ref_ret != inode_ret || ref_blkno != inode_blkno) {
			printf("fsck's internal inode link count tracking "
			       "isn't consistent. (ref_ret = %d ref_blkno = "
			       "%"PRIu64" inode_ret = %d inode_blkno = "
			       "%"PRIu64")\n", (int)ref_ret, ref_blkno,
			       (int)inode_ret, inode_blkno);
			ret = OCFS2_ET_INTERNAL_FAILURE;
			break;
		}

		if (ref_ret == 0)
			check_link_counts(ost, di, ref_blkno);
	}

out:
	if (buf)
		ocfs2_free(&buf);

	return ret;
}
