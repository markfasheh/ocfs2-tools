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
#include <inttypes.h>
#include <time.h>

#include "ocfs2.h"

#include "icount.h"
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

	/* do we want to detect and delete corrupt system dir/files here
	 * so we can recreate them later ? */

	/* also make sure the journal inode is ok? */

	/* clamp inodes to > OCFS2_SUPER_BLOCK_BLKNO && < fs->fs_blocks? */

	/* what's our deletion story?  i_links_count, dtime, etc.. */

#if 0 /* XXX we don't care about the signature on inodes? */
	if (memcmp(di->i_signature, OCFS2_INODE_SIGNATURE,
		   strlen(OCFS2_INODE_SIGNATURE))) {
		goto bad;
	}
#endif

	if (di->i_links_count)
		o2fsck_icount_update(ost->ost_icount_in_inodes, di->i_blkno,
					di->i_links_count);

	/* offer to clear a non-directory root inode so that 
	 * pass3:check_root() can re-create it */
	if ((di->i_blkno == fs->fs_root_blkno) && !S_ISDIR(di->i_mode) && 
	    should_fix(ost, FIX_DEFYES, "Root inode isn't a directory.")) {
		di->i_dtime = 0ULL;
		di->i_links_count = 0ULL;
		o2fsck_icount_update(ost->ost_icount_in_inodes, 
					di->i_blkno, di->i_links_count);

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
		fprintf(stderr, "duplicate inode %"PRIu64"?\n", blkno);
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
       uint64_t		vb_num_blocks;	
       uint64_t		vb_last_block;	
       int		vb_clear;
       int		vb_errors;
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
	o2fsck_state *ost = vb->vb_ost;
	int was_set;
	
	/* someday we may want to worry about holes in files here */

	if ((blkno < OCFS2_SUPER_BLOCK_BLKNO) || (blkno > fs->fs_blocks)) {
		vb->vb_errors++;
#if 0 /* ext2 does this by returning a value to libext2 which clears the 
	 block from the inode's allocation */
		if (should_fix(ost, FIX_DEFYES, 
				"inode %"PRIu64" references bad physical block"
			       	" %"PRIu64" at logical block %"PRIu64
				", should it be cleared?",
			di->i_blkno, bklno, bcount)) {
		}
#endif
	}

	/* XXX this logic should be more sophisticated.  It's not really clear
	 * what ext2 is trying to do in theirs. */
	if (vb->vb_errors == 12) {
		if (should_fix(ost, FIX_DEFYES, 
			"inode %"PRIu64" has seen many errors, should it "
			"be cleared?", di->i_blkno)) {
			vb->vb_clear = 1;
			return OCFS2_BLOCK_ABORT;
		}
	}

	ocfs2_bitmap_set(ost->ost_found_blocks, blkno, &was_set);
	if (was_set) {
		fprintf(stderr, "duplicate block %"PRIu64"?\n", blkno);
		ocfs2_bitmap_set(ost->ost_dup_blocks, blkno, NULL);
	}

	if (S_ISDIR(di->i_mode)) {
#if 0
		o2fsck_add_dir_block(di, blkno, bcount);
#endif
	}

	vb->vb_num_blocks++;
	if (bcount > vb->vb_last_block)
		vb->vb_last_block = bcount;

	return 0;
}

/* XXX maybe this should be a helper in libocfs2? */
static uint64_t clusters_holding_blocks(ocfs2_filesys *fs, uint64_t num_blocks)
{
	int c_to_b_bits = OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits -
		          OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;

	return (num_blocks + ((1 << c_to_b_bits) - 1)) >> c_to_b_bits;
}

static void o2fsck_check_blocks(ocfs2_filesys *fs, o2fsck_state *ost,
				uint64_t blkno, ocfs2_dinode *di)
{
	struct verifying_blocks vb = {0, };
	uint64_t expected = 0;
	errcode_t ret;

	vb.vb_ost = ost;
	vb.vb_di = di;

	/* XXX it isn't enough to just walk the blocks.  We want to mark
	 * metadata blocks in the extents as used and otherwise validate them
	 * while we're at it. */

	ret = ocfs2_block_iterate(fs, blkno, 0, verify_block, &vb);
	if (ret == OCFS2_ET_INODE_CANNOT_BE_ITERATED) {
		/* XXX I don't understand this.   just check the inode
		 * fields as though there were no blocks? */
		ret = 0;
	}
	if (ret) {
		fatal_error(ret, "while iterating over the blocks for inode "
			         "%"PRIu64, di->i_blkno);	
	}

	if (S_ISDIR(di->i_mode) && vb.vb_num_blocks == 0) {
		if (should_fix(ost, FIX_DEFYES, 
			"inode %"PRIu64" is a zero length directory, "
			"clear it?", di->i_blkno)) {
			vb.vb_clear = 1;
		}
	}

	/*
	 * XXX we should have a helper that clears an inode and backs it out of
	 * any book-keeping that it might have been included in, as though it
	 * was never seen.  the alternative is to restart pass1 which seems
	 * goofy. 
	 */
	if (vb.vb_clear) {
		di->i_links_count = 0;
		o2fsck_icount_update(ost->ost_icount_in_inodes, 
				          di->i_blkno, di->i_links_count);
		di->i_dtime = time(0);
		o2fsck_write_inode(fs, di->i_blkno, di);
		/* XXX clear valid flag and stuff? */
	}

	if (vb.vb_num_blocks > 0)
		expected = (vb.vb_last_block + 1) * fs->fs_blocksize;

	/* i_size is checked for symlinks elsewhere */
	if (!S_ISLNK(di->i_mode) && di->i_size != expected &&
	    should_fix(ost, FIX_DEFYES, "inode %"PRIu64" has a size of "
		       "%"PRIu64" but has %"PRIu64" bytes of actual data. "
		       " Correct the file size?", di->i_blkno, di->i_size,
		       expected)) {
		di->i_size = expected;
		o2fsck_write_inode(fs, blkno, di);
	}

	if (vb.vb_num_blocks > 0)
		expected = clusters_holding_blocks(fs, vb.vb_last_block + 1);

	if (di->i_clusters < expected &&
	    should_fix(ost, FIX_DEFYES, "inode %"PRIu64" has %"PRIu64" "
		       "clusters but its blocks fit in %"PRIu64" clusters. "
		       " Correct the number of clusters?", di->i_blkno, 
		       di->i_clusters, expected)) {
		di->i_clusters = expected;
		o2fsck_write_inode(fs, blkno, di);
	}
}

errcode_t o2fsck_pass1(o2fsck_state *ost)
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
			/* we don't deal with corrupt inode allocation
			 * files yet.  They won't be files for much longer.
			 * In the future the intent is to clean up inode
			 * allocation if scanning returns an error. */
			com_err(whoami, ret,
				"while getting next inode");
			goto out_close_scan;
		}
		if (blkno == 0)
			break;
		/* scanners have to skip over uninitialized inodes */
		if (!(di->i_flags & OCFS2_VALID_FL))
			continue;

		o2fsck_verify_inode_fields(fs, ost, blkno, di);

		/* XXX be able to mark the blocks in the inode as 
		 * bad if the inode was bad */

		o2fsck_check_blocks(fs, ost, blkno, di);
	}

out_close_scan:
	ocfs2_close_inode_scan(scan);
out_free:
	ocfs2_free(&buf);

out:
	return 0;
}
