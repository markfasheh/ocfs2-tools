/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * pass1.c
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
#include <string.h>

#include "ocfs2.h"

#include "fsck.h"
#include "pass1.h"
#include "problem.h"
#include "util.h"

const char *whoami = "pass1";

/*
 * for now we're just building up info, we're not actually
 * writing to disk.
 *
 * for each inode:
 * - verify that i_mode is legal
 *
 * We collect the following for future passes:
 *
 * - a bitmap of inodes which are in use
 * - a bitmap of inodes which have bad fields
 * - a bitmap of data blocks on inodes
 * - a bitmap of data blocks duplicated between inodes
 */

static void o2fsck_verify_inode_fields(ocfs2_filesys *fs, o2fsck_state *ost, 
				       uint64_t blkno, ocfs2_dinode *di)
{
	int was_set;

	fprintf(stdout, "inode %llu with size %llu\n",
		blkno, di->i_size);

	/* do we want to detect and delete corrupt system dir/files here
	 * so we can recreate them later ? */

	/* also make sure the journal inode is ok? */

	/* clamp inodes to > OCFS2_SUPER_BLOCK_BLKNO && < fs->fs_blocks? */

	/* what's our deletion story?  i_links_count, dtime, etc.. */

	/* XXX it seems these are expected sometimes? */
	if (memcmp(di->i_signature, OCFS2_INODE_SIGNATURE,
		   strlen(OCFS2_INODE_SIGNATURE))) {
		printf("inode %llu has invalid signature\n", blkno);
		goto bad;
	}
	if (!(di->i_flags & OCFS2_VALID_FL)) {
		printf("inode %llu missing valid flag\n", blkno);
		goto bad;
	}

	/* offer to clear a non-directory root inode so that 
	 * pass3:check_root() can re-create it */
	if ((di->i_blkno == fs->fs_root_blkno) &&
	    should_fix(ost, FIX_DEFYES, "Root inode isn't a directory.")) {
		di->i_dtime = 0ULL;
		di->i_links_count = 0ULL;
		/* icount_store(links_count) */
		o2fsck_write_inode(fs, blkno, di);
	}

	if (di->i_dtime) {
		if (should_fix(ost, FIX_DEFYES, 
		    "Inode %llu is in use but has a non-zero dtime.", 
		    di->i_blkno)) {

			di->i_dtime = 0ULL;
			o2fsck_write_inode(fs, blkno, di);
		}
	}

	ocfs2_bitmap_set(ost->ost_used_inodes, blkno, &was_set);
	if (was_set) {
		fprintf(stderr, "duplicate inode %llu?\n", blkno);
		goto bad;
	}

	if (S_ISDIR(di->i_mode)) {
		/* XXX record dir for dir block walk */
		ocfs2_bitmap_set(ost->ost_dir_inodes, blkno, NULL);
	} else if (S_ISREG(di->i_mode)) {
		ocfs2_bitmap_set(ost->ost_reg_inodes, blkno, NULL);
	} else if (S_ISLNK(di->i_mode)) {
		/* make sure fast symlinks are ok, etc */
	} else {
		if (!S_ISCHR(di->i_mode) && !S_ISBLK(di->i_mode) &&
			!S_ISFIFO(di->i_mode) && !S_ISSOCK(di->i_mode))
			goto bad;

		/* i_size?  what other sanity testing for devices? */
	}

	return;
bad:
	ocfs2_bitmap_set(ost->ost_bad_inodes, blkno, NULL);
}

struct verifying_blocks {
       unsigned		vb_mark_dir_blocks;	
       uint64_t		vb_blocks;	
       o2fsck_state 	*vb_ost;
       ocfs2_dinode	*vb_di;
};

static int verify_block(ocfs2_filesys *fs,
			    uint64_t blkno,
			    uint64_t bcount,
			    void *priv_data)
{
	struct verifying_blocks *vb = priv_data;
	ocfs2_dinode *di = vb->vb_di;
	int was_set;

	ocfs2_bitmap_set(vb->vb_ost->ost_found_blocks, blkno, &was_set);
	if (was_set) {
		fprintf(stderr, "duplicate block %llu?\n", blkno);
		ocfs2_bitmap_set(vb->vb_ost->ost_dup_blocks, blkno, NULL);
	}

	if (S_ISDIR(di->i_mode)) {
		/* do we want libocfs2 to record directory blocks or should
		 * we? */
	}

	vb->vb_blocks++;
	return 0;
}

static errcode_t o2fsck_verify_inode_data(ocfs2_filesys *fs, o2fsck_state *ost,
					  uint64_t blkno, ocfs2_dinode *di)
{
	struct verifying_blocks vb = {0, };
	errcode_t ret;

	vb.vb_ost = ost;
	vb.vb_di = di;

	ret = ocfs2_block_iterate(fs, blkno, 0, verify_block, &vb);
	if (ret) {
		com_err(whoami, ret, "walking data blocks");
		goto out;
	}

#if 0 /* hmm. */
	if (vb.vb_blocks != di->i_blocks) {
		fprintf(stderr, "inode %llu claimed %llu blocks, found"
				" %llu blocks.\n", di->i_ino, di->i_blocks,
				vb.vb_blocks);
	}
#endif

out:
	return ret;
}

errcode_t o2fsck_pass1(ocfs2_filesys *fs, o2fsck_state *ost)
{
	errcode_t ret;
	uint64_t blkno;
	char *buf;
	ocfs2_dinode *di;
	ocfs2_inode_scan *scan;
	ocfs2_filesys *fs = ost->ost_fs;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret) {
		com_err(whoami, ret,
			"while allocating inode buffer");
		goto out;
	}

	di = (ocfs2_dinode *)buf;

	ret = ocfs2_open_inode_scan(fs, &scan);
	if (ret) {
		com_err(whoami, ret,
			"while opening inode scan");
		goto out_free;
	}

	for(;;) {
		ret = ocfs2_get_next_inode(scan, &blkno, buf);
		if (ret) {
			com_err(whoami, ret,
				"while getting next inode");
			goto out_close_scan;
		}
		if (blkno == 0)
			break;

		o2fsck_verify_inode_fields(fs, ost, blkno, di);
		/* XXX be able to mark the blocks in the inode as 
		 * bad if the inode was bad */
		o2fsck_verify_inode_data(fs, ost, blkno, di);
	}

out_close_scan:
	ocfs2_close_inode_scan(scan);
out_free:
	ocfs2_free(&buf);

out:
	return 0;
}
