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

static errcode_t check_link_counts(o2fsck_state *ost, ocfs2_dinode *di)
{
	uint16_t refs, in_inode;

	refs = o2fsck_icount_get(ost->ost_icount_refs, di->i_blkno);
	in_inode = o2fsck_icount_get(ost->ost_icount_in_inodes, di->i_blkno);

	verbosef("ino %"PRIu64", refs %u in %u\n", di->i_blkno, refs, 
		 in_inode);

	/* XXX offer to remove files/dirs with no data? */
	if (refs == 0 &&
	    prompt(ost, PY, "Inode %"PRIu64" isn't referenced by any "
		   "directory entries.  Move it to lost+found?", 
		   di->i_blkno)) {
		o2fsck_reconnect_file(ost, di->i_blkno);
		refs = o2fsck_icount_get(ost->ost_icount_refs, di->i_blkno);
	}

	if (refs == in_inode)
		goto out;

	if (in_inode != di->i_links_count)
		com_err(whoami, OCFS2_ET_INTERNAL_FAILURE, "fsck's thinks "
			"inode %"PRIu64" has a link count of %"PRIu16" but on "
			"disk it is %"PRIu16, di->i_blkno, in_inode, 
			di->i_links_count);

	if (prompt(ost, PY, "Inode %"PRIu64" has a link count of %"PRIu16" on "
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
	errcode_t ret;
	ocfs2_inode_scan *scan;
	uint64_t blkno;

	printf("Pass 4: Checking inodes link counts.\n");

	ret = ocfs2_malloc_block(ost->ost_fs->fs_io, &buf);
	if (ret) {
		com_err(whoami, ret, "while allocating space to read inodes");
		goto out;
	}

	di = (ocfs2_dinode *)buf;

	ret = ocfs2_open_inode_scan(ost->ost_fs, &scan);
	if (ret) {
		com_err(whoami, ret,
			"while opening inode scan");
		goto out_free;
	}

	for(;;) {
		ret = ocfs2_get_next_inode(scan, &blkno, buf);
		if (ret) {
			com_err(whoami, ret, "while reading next inode");
			break;
		}
		if (blkno == 0)
			break;

		if (!(di->i_flags & OCFS2_VALID_FL))
			continue;

		/*
		 * XXX e2fsck skips some inodes by their presence in other
		 * bitmaps.  I think we should use this loop to verify their
		 * i_links_count as well and just make sure that we update refs
		 * to match our expectations in previous passes.
		 */

		check_link_counts(ost, di);
	}

	ocfs2_close_inode_scan(scan);
out_free:
	ocfs2_free(&buf);
out:
	return ret;
}
