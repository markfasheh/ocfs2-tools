/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
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
	    prompt(ost, PY, 0, "Inode %"PRIu64" isn't referenced by any "
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

	if (prompt(ost, PY, 0, "Inode %"PRIu64" has a link count of %"PRIu16" on "
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

errcode_t o2fsck_pass4(o2fsck_state *ost)
{
	ocfs2_dinode *di;
	char *buf = NULL;
	errcode_t ret, ref_ret, inode_ret;
	uint64_t blkno, ref_blkno, inode_blkno;

	printf("Pass 4: Checking inodes link counts.\n");

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
			       "%"PRIu64"\n", (int)ref_ret, ref_blkno,
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
