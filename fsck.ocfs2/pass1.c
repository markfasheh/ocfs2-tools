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

#include "dirblocks.h"
#include "dirparents.h"
#include "icount.h"
#include "fsck.h"
#include "pass1.h"
#include "problem.h"
#include "util.h"

static const char *whoami = "pass1";

static void mark_block_used(o2fsck_state *ost, uint64_t blkno)
{
	int was_set;
	ocfs2_bitmap_set(ost->ost_found_blocks, blkno, &was_set);
	if (was_set) {
		verbosef("duplicate block %"PRIu64"\n", blkno);
		ocfs2_bitmap_set(ost->ost_dup_blocks, blkno, NULL);
	}
}

/* XXX should walk down all the i_fields to make sure we're veryfying
 * those that we can this early */
static void o2fsck_verify_inode_fields(ocfs2_filesys *fs, o2fsck_state *ost, 
				       uint64_t blkno, ocfs2_dinode *di)
{
	int was_set;

	/* do we want to detect and delete corrupt system dir/files here
	 * so we can recreate them later ? */

	/* also make sure the journal inode is ok? */

	/* clamp inodes to > OCFS2_SUPER_BLOCK_BLKNO && < fs->fs_blocks? */

	/* XXX need to compare the lifetime of inodes (uninitialized?
	 * in use?  orphaned?  deleted?  garbage?) to understand what
	 * fsck can do to fix it up */
	if (memcmp(di->i_signature, OCFS2_INODE_SIGNATURE,
		   strlen(OCFS2_INODE_SIGNATURE))) {
		goto bad;
	}
	if (di->i_flags & OCFS2_SUPER_BLOCK_FL)
		goto bad;

	if (di->i_links_count)
		o2fsck_icount_set(ost->ost_icount_in_inodes, di->i_blkno,
					di->i_links_count);

	/* offer to clear a non-directory root inode so that 
	 * pass3:check_root() can re-create it */
	if ((di->i_blkno == fs->fs_root_blkno) && !S_ISDIR(di->i_mode) && 
	    prompt(ost, PY, "Root inode isn't a directory.  Clear it in "
		   "preparation for fixing it?")) {
		di->i_dtime = 0ULL;
		di->i_links_count = 0ULL;
		o2fsck_icount_set(ost->ost_icount_in_inodes, di->i_blkno,
				  di->i_links_count);

		o2fsck_write_inode(fs, blkno, di);
	}

	if (di->i_dtime && prompt(ost, PY, "Inode %"PRIu64" is in use but has "
				  "a non-zero dtime.  Reset the dtime to 0?",  
				   di->i_blkno)) {
		di->i_dtime = 0ULL;
		o2fsck_write_inode(fs, blkno, di);
	}

	ocfs2_bitmap_set(ost->ost_used_inodes, blkno, &was_set);
	if (was_set) {
		fprintf(stderr, "duplicate inode %"PRIu64"?\n", blkno);
		goto bad;
	}

	if (S_ISDIR(di->i_mode)) {
		ocfs2_bitmap_set(ost->ost_dir_inodes, blkno, NULL);
		o2fsck_add_dir_parent(&ost->ost_dir_parents, blkno, 0, 0);
	} else if (S_ISREG(di->i_mode)) {
		ocfs2_bitmap_set(ost->ost_reg_inodes, blkno, NULL);
	} else if (S_ISLNK(di->i_mode)) {
		/* we only make sure a link's i_size matches
		 * the link names length in the file data later when
		 * we walk the inode's blocks */
	} else {
		if (!S_ISCHR(di->i_mode) && !S_ISBLK(di->i_mode) &&
			!S_ISFIFO(di->i_mode) && !S_ISSOCK(di->i_mode))
			goto bad;

		/* i_size?  what other sanity testing for devices? */
	}

	return;
bad:
	/* XXX we don't actually do anything with this bitmap */
	ocfs2_bitmap_set(ost->ost_bad_inodes, blkno, NULL);
}

struct verifying_blocks {
       unsigned		vb_clear:1,
       			vb_saw_link_null:1,
       			vb_link_read_error:1;

       uint64_t		vb_link_len;
       uint64_t		vb_num_blocks;	
       uint64_t		vb_last_block;	
       int		vb_errors;
       o2fsck_state 	*vb_ost;
       ocfs2_dinode	*vb_di;
};

/* last_block and num_blocks would be different in a sparse file */
static void vb_saw_block(struct verifying_blocks *vb, uint64_t bcount)
{
	vb->vb_num_blocks++;
	if (bcount > vb->vb_last_block)
		vb->vb_last_block = bcount;
}

static void process_link_block(struct verifying_blocks *vb, uint64_t blkno)
{
	char *buf, *null;
	errcode_t ret;
	unsigned int blocksize = vb->vb_ost->ost_fs->fs_blocksize;

	if (vb->vb_saw_link_null)
		return;

	ret = ocfs2_malloc_blocks(vb->vb_ost->ost_fs->fs_io, 1, &buf);
	if (ret)
		fatal_error(ret, "while allocating room to read a block of "
				 "link data");

	ret = io_read_block(vb->vb_ost->ost_fs->fs_io, blkno, 1, buf);
	if (ret) {
		goto out;
	}

	null = memchr(buf, 0, blocksize);
	if (null != NULL) {
		vb->vb_link_len += null - buf;
		vb->vb_saw_link_null = 1;
	} else {
		vb->vb_link_len += blocksize;
	}

out:
	ocfs2_free(&buf);
}

/* XXX maybe this should be a helper in libocfs2? */
static uint64_t blocks_holding_bytes(ocfs2_filesys *fs, uint64_t bytes)
{
	int b_bits = OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;

	return (bytes +  fs->fs_blocksize - 1) >> b_bits;
}

static void check_link_data(struct verifying_blocks *vb)
{
	ocfs2_dinode *di = vb->vb_di;
	o2fsck_state *ost = vb->vb_ost;
	uint64_t expected;

	verbosef("found a link: num %"PRIu64" last %"PRIu64" len "
		"%"PRIu64" null %d error %d\n", vb->vb_num_blocks, 
		vb->vb_last_block, vb->vb_link_len, vb->vb_saw_link_null, 
		vb->vb_link_read_error);

	if (vb->vb_link_read_error) {
		if (prompt(ost, PY, "There was an error reading a data block "
			   "for symlink inode %"PRIu64".  Clear the inode?",
			   di->i_blkno)) {
			vb->vb_clear = 1;
			return;
		}
	}

	/* XXX this could offer to null terminate */
	if (!vb->vb_saw_link_null) {
		if (prompt(ost, PY, "The target of symlink inode %"PRIu64" "
			   "isn't null terminated.  Clear the inode?",
			   di->i_blkno)) {
			vb->vb_clear = 1;
			return;
		}
	}

	expected = blocks_holding_bytes(ost->ost_fs, vb->vb_link_len + 1);

	if (di->i_size != vb->vb_link_len) {
		if (prompt(ost, PY, "The target of symlink inode %"PRIu64" "
			   "is %"PRIu64" bytes long on disk, but i_size is "
			   "%"PRIu64" bytes long.  Update i_size to reflect "
			   "the length on disk?",
			   di->i_blkno, vb->vb_link_len, di->i_size)) {
			di->i_size = vb->vb_link_len;
			o2fsck_write_inode(ost->ost_fs, di->i_blkno, di);
			return;
		}
	}

	/* maybe we don't shrink link target allocations, I don't know,
	 * someone will holler if this is wrong :) */
	if (vb->vb_num_blocks != expected) {
		if (prompt(ost, PN, "The target of symlink inode %"PRIu64" "
			   "fits in %"PRIu64" blocks but the inode has "
			   "%"PRIu64" allocated.  Clear the inode?", expected,
			   di->i_blkno)) {
			vb->vb_clear = 1;
			return;
		}
	}
}

static int verify_block(ocfs2_filesys *fs,
			    uint64_t blkno,
			    uint64_t bcount,
			    void *priv_data)
{
	struct verifying_blocks *vb = priv_data;
	ocfs2_dinode *di = vb->vb_di;
	o2fsck_state *ost = vb->vb_ost;
	
	/* someday we may want to worry about holes in files here */

	if ((blkno < OCFS2_SUPER_BLOCK_BLKNO) || (blkno > fs->fs_blocks)) {
		vb->vb_errors++;
#if 0 /* ext2 does this by returning a value to libext2 which clears the 
	 block from the inode's allocation */
		if (prompt(ost, PY, "inode %"PRIu64" references bad physical "
			   "block %"PRIu64" at logical block %"PRIu64", "
			   "should it be cleared?", di->i_blkno, bklno, 
			   bcount)) {
		}
#endif
	}

	/* XXX this logic should be more sophisticated.  It's not really clear
	 * what ext2 is trying to do in theirs. */
	if (vb->vb_errors == 12) {
		if (prompt(ost, PY, "inode %"PRIu64" has seen many errors, "
			   "should it be cleared?", di->i_blkno)) {
			vb->vb_clear = 1;
			return OCFS2_BLOCK_ABORT;
		}
	}

	if (S_ISDIR(di->i_mode)) {
		verbosef("adding dir block %"PRIu64"\n", blkno);
		o2fsck_add_dir_block(&ost->ost_dirblocks, di->i_blkno, blkno,
					bcount);
	}

	if (S_ISLNK(di->i_mode))
		process_link_block(vb, blkno);

	vb_saw_block(vb, bcount);

	return 0;
}

/* XXX maybe this should be a helper in libocfs2? */
static uint64_t clusters_holding_blocks(ocfs2_filesys *fs, uint64_t num_blocks)
{
	int c_to_b_bits = OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits -
		          OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;

	return (num_blocks + ((1 << c_to_b_bits) - 1)) >> c_to_b_bits;
}

static int check_gd_block(ocfs2_filesys *fs, uint64_t gd_blkno, int chain_num,
			   void *priv_data)
{
	struct verifying_blocks *vb = priv_data;
	verbosef("found gd block %"PRIu64"\n", gd_blkno);
	/* don't have bcount */
	mark_block_used(vb->vb_ost, gd_blkno);
	vb_saw_block(vb, vb->vb_num_blocks);
	return 0;
}

static int check_extent_blocks(ocfs2_filesys *fs, ocfs2_extent_rec *rec,
				int tree_depth, uint32_t ccount,
				uint64_t ref_blkno, int ref_recno,
				void *priv_data)
{
	struct verifying_blocks *vb = priv_data;

	if (tree_depth > 0) {
		verbosef("found extent block %"PRIu64"\n", rec->e_blkno);
		mark_block_used(vb->vb_ost, rec->e_blkno);
	}

	return 0;
}

static void o2fsck_check_blocks(ocfs2_filesys *fs, o2fsck_state *ost,
				uint64_t blkno, ocfs2_dinode *di)
{
	struct verifying_blocks vb = {0, };
	uint64_t expected = 0;
	errcode_t ret;

	vb.vb_ost = ost;
	vb.vb_di = di;

	if (di->i_flags & OCFS2_LOCAL_ALLOC_FL)
		ret = 0; /* nothing to iterate over in this case */
	else if (di->i_flags & OCFS2_CHAIN_FL)
		ret = ocfs2_chain_iterate(fs, blkno, check_gd_block, &vb);
	else {
		ret = ocfs2_extent_iterate(fs, blkno, 0, NULL,
					   check_extent_blocks, &vb);
		if (ret == 0)
			ret = ocfs2_block_iterate(fs, blkno, 0,
    					   verify_block, &vb);
	}

	if (ret) {
		fatal_error(ret, "while iterating over the blocks for inode "
			         "%"PRIu64, di->i_blkno);	
	}

	if (S_ISLNK(di->i_mode))
		check_link_data(&vb);

	if (S_ISDIR(di->i_mode) && vb.vb_num_blocks == 0) {
		if (prompt(ost, PY, "Inode %"PRIu64" is a zero length "
			   "directory, clear it?", di->i_blkno)) {
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
		o2fsck_icount_set(ost->ost_icount_in_inodes, di->i_blkno,
				  di->i_links_count);
		di->i_dtime = time(0);
		o2fsck_write_inode(fs, di->i_blkno, di);
		/* XXX clear valid flag and stuff? */
	}

#if 0 /* boy, this is just broken */
	if (vb.vb_num_blocks > 0)
		expected = (vb.vb_last_block + 1) * fs->fs_blocksize;

	/* i_size is checked for symlinks elsewhere */
	if (!S_ISLNK(di->i_mode) && di->i_size > expected &&
	    prompt(ost, PY, "Inode %"PRIu64" has a size of %"PRIu64" but has "
		    "%"PRIu64" bytes of actual data. Correct the file size?",
		    di->i_blkno, di->i_size, expected)) {
		di->i_size = expected;
		o2fsck_write_inode(fs, blkno, di);
	}
#endif

	if (vb.vb_num_blocks > 0)
		expected = clusters_holding_blocks(fs, vb.vb_last_block + 1);

	if (di->i_clusters < expected &&
	    prompt(ost, PY, "inode %"PRIu64" has %"PRIu64" clusters but its "
		   "blocks fit in %"PRIu64" clusters.  Correct the number of "
		   "clusters?", di->i_blkno, di->i_clusters, expected)) {
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

	printf("Pass 1: Checking inodes and blocks.\n");

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

	if (ocfs2_bitmap_get_set_bits(ost->ost_dup_blocks))
		fatal_error(OCFS2_ET_INTERNAL_FAILURE, "duplicate blocks "
				"found, need to learn to fix.");

out_close_scan:
	ocfs2_close_inode_scan(scan);
out_free:
	ocfs2_free(&buf);

out:
	return 0;
}
