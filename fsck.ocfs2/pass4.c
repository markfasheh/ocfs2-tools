/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * pass4.c
 *
 * file system checker for OCFS2
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
 * Authors: Zach Brown
 */
#include <inttypes.h>

#include <ocfs2.h>

#include "fsck.h"
#include "icount.h"
#include "pass3.h"
#include "pass4.h"
#include "problem.h"
#include "util.h"

errcode_t o2fsck_pass4(o2fsck_state *ost)
{
	uint64_t ino = 0;
	uint16_t refs, in_inode;
	ocfs2_dinode *di;
	char *buf = NULL;
	errcode_t err;

	printf("Pass 4: Checking inodes link counts.\n");

	for (ino = 0;
	     ocfs2_bitmap_find_next_set(ost->ost_used_inodes, ino, &ino) != 0;
	     ino++) {
		/*
		 * XXX e2fsck skips some inodes by their presence in other
		 * bitmaps.  I think we should use this loop to verify their
		 * i_links_count as well and just make sure that we update refs
		 * to match our expectations in previous passes.
		 */

		refs = o2fsck_icount_get(ost->ost_icount_refs, ino);
		in_inode = o2fsck_icount_get(ost->ost_icount_in_inodes, ino);

		verbosef("ino %"PRIu64", refs %u in %u\n", ino, refs, 
				in_inode);

		if (refs == 0) {
			/* XXX offer to remove files/dirs with no data? */
			if (prompt(ost, PY, "Inode %"PRIu64" isn't referenced "
				   "by any directory entries.  Move it to "
				   "lost+found?", ino)) {
				o2fsck_reconnect_file(ost, ino);
				refs = o2fsck_icount_get(ost->ost_icount_refs,
						ino);
			}
		}

		if (refs == in_inode)
			continue;

		if (buf == NULL) {
			err = ocfs2_malloc_block(ost->ost_fs->fs_io, &buf);
			if (err)
				fatal_error(err, "while allocating inode "
						"buffer to verify an inode's "
						"link count");
		}

		err = ocfs2_read_inode(ost->ost_fs, ino, buf);
		if (err)
			fatal_error(err, "while reading inode %"PRIu64" to "
					"verify its link count", ino);

		di = (ocfs2_dinode *)buf; 

		if (in_inode != di->i_links_count)
			fatal_error(OCFS2_ET_INTERNAL_FAILURE,
				    "fsck's thinks inode %"PRIu64" has a link "
				    "count of %"PRIu16" but on disk it is "
				    "%"PRIu16, ino, in_inode, 
				    di->i_links_count);

		if (prompt(ost, PY, "Inode %"PRIu64" has a link count of "
			   "%"PRIu16" on disk but directory entry references "
			   "come to %"PRIu16". Update the count on disk to "
			   "match?", ino, in_inode, refs)) {
			di->i_links_count = refs;
			o2fsck_icount_set(ost->ost_icount_in_inodes, ino, 
					refs);
			o2fsck_write_inode(ost->ost_fs, ino, di);
		}
	}

	return 0;
}
