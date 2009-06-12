/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * feature_metaecc.c
 *
 * ocfs2 tune utility for enabling and disabling the metaecc feature.
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
#include <sys/stat.h>
#include <ctype.h>
#include <inttypes.h>
#include <assert.h>

#include "ocfs2-kernel/kernel-list.h"
#include "ocfs2/kernel-rbtree.h"
#include "ocfs2/ocfs2.h"

#include "libocfs2ne.h"



/* A dirblock we have to add a trailer to */
struct tunefs_trailer_dirblock {
	struct list_head db_list;
	uint64_t db_blkno;
	char *db_buf;

	/*
	 * These require a little explanation.  They point to
	 * ocfs2_dir_entry structures inside db_buf.
	 *
	 * db_last entry we're going to *keep*.  If the last entry in the
	 * dirblock has enough extra rec_len to allow the trailer, db_last
	 * points to it.  We will shorten its rec_len and insert the
	 * trailer.
	 *
	 * However, if the last entry in the dirblock cannot be truncated,
	 * db_move points to the entry we have to move out, and db_last
	 * points to the entry before that - the last entry we're keeping
	 * in this dirblock.
	 *
	 * Examples:
	 *
	 * - The last entry in the dirblock has a name_len of 1 and a
	 *   rec_len of 128.  We can easily change the rec_len to 64 and
	 *   insert the trailer.  db_last points to this entry.
	 *
	 * - The last entry in the dirblock has a name_len of 1 and a
	 *   rec_len of 48.  The previous entry has a name_len of 1 and a
	 *   rec_len of 32.  We have to move the last entry out.  The
	 *   second-to-last entry can have its rec_len truncated to 16, so
	 *   we put it in db_last.
	 */
	struct ocfs2_dir_entry *db_last;
};

/* A directory inode we're adding trailers to */
struct tunefs_trailer_context {
	struct list_head d_list;
	uint64_t d_blkno;		/* block number of the dir */
	struct ocfs2_dinode *d_di;	/* The directory's inode */
	struct list_head d_dirblocks;	/* List of its dirblocks */
	uint64_t d_bytes_needed;	/* How many new bytes will
					   cover the dirents we are moving
					   to make way for trailers */
	uint64_t d_blocks_needed;	/* How many blocks covers
					   d_bytes_needed */
	char *d_new_blocks;		/* Buffer of new blocks to fill */
	char *d_cur_block;		/* Which block we're filling in
					   d_new_blocks */
	struct ocfs2_dir_entry *d_next_dirent;	/* Next dentry to use */
	errcode_t d_err;		/* Any processing error during
					   iteration of the directory */
};

static void tunefs_trailer_context_free(struct tunefs_trailer_context *tc)
{
	struct tunefs_trailer_dirblock *db;
	struct list_head *n, *pos;

	if (!list_empty(&tc->d_list))
		list_del(&tc->d_list);

	list_for_each_safe(pos, n, &tc->d_dirblocks) {
		db = list_entry(pos, struct tunefs_trailer_dirblock, db_list);
		list_del(&db->db_list);
		ocfs2_free(&db->db_buf);
		ocfs2_free(&db);
	}

	ocfs2_free(&tc);
}

/*
 * We're calculating how many bytes we need to add to make space for
 * the dir trailers.  But we need to make sure that the added directory
 * blocks also have room for a trailer.
 */
static void add_bytes_needed(ocfs2_filesys *fs,
			     struct tunefs_trailer_context *tc,
			     unsigned int rec_len)
{
	unsigned int toff = ocfs2_dir_trailer_blk_off(fs);
	unsigned int block_offset = tc->d_bytes_needed % fs->fs_blocksize;

	/*
	 * If the current byte offset would put us into a trailer, push
	 * it out to the start of the next block.  Remember, dirents have
	 * to be at least 16 bytes, which is why we check against the
	 * smallest rec_len.
	 */
	if ((block_offset + rec_len) > (toff - OCFS2_DIR_REC_LEN(1)))
		tc->d_bytes_needed += fs->fs_blocksize - block_offset;

	tc->d_bytes_needed += rec_len;
	tc->d_blocks_needed =
		ocfs2_blocks_in_bytes(fs, tc->d_bytes_needed);
}

static errcode_t walk_dirblock(ocfs2_filesys *fs,
			       struct tunefs_trailer_context *tc,
			       struct tunefs_trailer_dirblock *db)
{
	errcode_t ret;
	struct ocfs2_dir_entry *dirent, *prev = NULL;
	unsigned int real_rec_len;
	unsigned int offset = 0;
	unsigned int toff = ocfs2_dir_trailer_blk_off(fs);

	while (offset < fs->fs_blocksize) {
		dirent = (struct ocfs2_dir_entry *) (db->db_buf + offset);
		if (((offset + dirent->rec_len) > fs->fs_blocksize) ||
		    (dirent->rec_len < 8) ||
		    ((dirent->rec_len % 4) != 0) ||
		    (((dirent->name_len & 0xFF)+8) > dirent->rec_len)) {
			ret = OCFS2_ET_DIR_CORRUPTED;
			break;
		}

		real_rec_len = dirent->inode ?
			OCFS2_DIR_REC_LEN(dirent->name_len) :
			OCFS2_DIR_REC_LEN(1);
		if ((offset + real_rec_len) <= toff)
			goto next;

		/*
		 * The first time through, we store off the last dirent
		 * before the trailer.
		 */
		if (!db->db_last)
			db->db_last = prev;

		/* Only live dirents need to be moved */
		if (dirent->inode) {
			verbosef(VL_DEBUG,
				 "Will move dirent %.*s out of "
				 "directory block %"PRIu64" to make way "
				 "for the trailer\n",
				 dirent->name_len, dirent->name,
				 db->db_blkno);
			add_bytes_needed(fs, tc, real_rec_len);
		}

next:
		prev = dirent;
		offset += dirent->rec_len;
	}

	/* There were no dirents across the boundary */
	if (!db->db_last)
		db->db_last = prev;

	return ret;
}

static int dirblock_scan_iterate(ocfs2_filesys *fs, uint64_t blkno,
				 uint64_t bcount, uint16_t ext_flags,
				 void *priv_data)
{
	errcode_t ret = 0;
	struct tunefs_trailer_dirblock *db = NULL;
	struct tunefs_trailer_context *tc = priv_data;

	ret = ocfs2_malloc0(sizeof(struct tunefs_trailer_dirblock), &db);
	if (ret)
		goto out;

	ret = ocfs2_malloc_block(fs->fs_io, &db->db_buf);
	if (ret)
		goto out;

	db->db_blkno = blkno;

	verbosef(VL_DEBUG,
		 "Reading dinode %"PRIu64" dirblock %"PRIu64" at block "
		 "%"PRIu64"\n",
		 tc->d_di->i_blkno, bcount, blkno);
	ret = ocfs2_read_dir_block(fs, tc->d_di, blkno, db->db_buf);
	if (ret)
		goto out;

	ret = walk_dirblock(fs, tc, db);
	if (ret)
		goto out;

	list_add_tail(&db->db_list, &tc->d_dirblocks);
	db = NULL;

out:
	if (db) {
		if (db->db_buf)
			ocfs2_free(&db->db_buf);
		ocfs2_free(&db);
	}

	if (ret) {
		tc->d_err = ret;
		return OCFS2_BLOCK_ABORT;
	}

	return 0;
}

static errcode_t tunefs_prepare_dir_trailer(ocfs2_filesys *fs,
					    struct ocfs2_dinode *di,
					    struct tunefs_trailer_context **tc_ret)
{
	errcode_t ret = 0;
	struct tunefs_trailer_context *tc = NULL;

	if (ocfs2_dir_has_trailer(fs, di))
		goto out;

	ret = ocfs2_malloc0(sizeof(struct tunefs_trailer_context), &tc);
	if (ret)
		goto out;

	tc->d_blkno = di->i_blkno;
	tc->d_di = di;
	INIT_LIST_HEAD(&tc->d_list);
	INIT_LIST_HEAD(&tc->d_dirblocks);

	ret = ocfs2_block_iterate_inode(fs, tc->d_di, 0,
					dirblock_scan_iterate, tc);
	if (!ret)
		ret = tc->d_err;
	if (ret)
		goto out;

	*tc_ret = tc;
	tc = NULL;

out:
	if (tc)
		tunefs_trailer_context_free(tc);

	return ret;
}

/*
 * We are hand-coding the directory expansion because we're going to
 * build the new directory blocks ourselves.  We can't just use
 * ocfs2_expand_dir() and ocfs2_link(), because we're moving around
 * entries.
 */
static errcode_t expand_dir_if_needed(ocfs2_filesys *fs,
				      struct ocfs2_dinode *di,
				      uint64_t blocks_needed)
{
	errcode_t ret = 0;
	uint64_t used_blocks, total_blocks;
	uint32_t clusters_needed;

	/* This relies on the fact that i_size of a directory is a
	 * multiple of blocksize */
	used_blocks = ocfs2_blocks_in_bytes(fs, di->i_size);
	total_blocks = ocfs2_clusters_to_blocks(fs, di->i_clusters);
	if ((used_blocks + blocks_needed) <= total_blocks)
		goto out;

	clusters_needed =
		ocfs2_clusters_in_blocks(fs,
					 (used_blocks + blocks_needed) -
					 total_blocks);
	ret = ocfs2_extend_allocation(fs, di->i_blkno, clusters_needed);
	if (ret)
		goto out;

	/* Pick up changes to the inode */
	ret = ocfs2_read_inode(fs, di->i_blkno, (char *)di);

out:
	return ret;
}

static void shift_dirent(ocfs2_filesys *fs,
			 struct tunefs_trailer_context *tc,
			 struct ocfs2_dir_entry *dirent)
{
	/* Using the real rec_len */
	unsigned int rec_len = OCFS2_DIR_REC_LEN(dirent->name_len);
	unsigned int offset, remain;

	/*
	 * If the current byte offset would put us into a trailer, push
	 * it out to the start of the next block.  Remember, dirents have
	 * to be at least 16 bytes, which is why we check against the
	 * smallest rec_len.
	 */
	if (rec_len > (tc->d_next_dirent->rec_len - OCFS2_DIR_REC_LEN(1))) {
		tc->d_cur_block += fs->fs_blocksize;
		tc->d_next_dirent = (struct ocfs2_dir_entry *)tc->d_cur_block;
	}

	assert(ocfs2_blocks_in_bytes(fs,
				     tc->d_cur_block - tc->d_new_blocks) <
	       tc->d_blocks_needed);

	offset = (char *)(tc->d_next_dirent) - tc->d_cur_block;
	remain = tc->d_next_dirent->rec_len - rec_len;

	memcpy(tc->d_cur_block + offset, dirent, rec_len);
	tc->d_next_dirent->rec_len = rec_len;

	verbosef(VL_DEBUG,
		 "Installed dirent %.*s at offset %u of new block "
		 "%"PRIu64", rec_len %u\n",
		 tc->d_next_dirent->name_len, tc->d_next_dirent->name,
		 offset,
		 ocfs2_blocks_in_bytes(fs, tc->d_cur_block - tc->d_new_blocks),
		 rec_len);


	offset += rec_len;
	tc->d_next_dirent =
		(struct ocfs2_dir_entry *)(tc->d_cur_block + offset);
	tc->d_next_dirent->rec_len = remain;

	verbosef(VL_DEBUG,
		 "New block %"PRIu64" has its last dirent at %u, with %u "
		 "bytes left\n",
		 ocfs2_blocks_in_bytes(fs, tc->d_cur_block - tc->d_new_blocks),
		 offset, remain);
}

static errcode_t fixup_dirblock(ocfs2_filesys *fs,
				struct tunefs_trailer_context *tc,
				struct tunefs_trailer_dirblock *db)
{
	errcode_t ret = 0;
	struct ocfs2_dir_entry *dirent;
	unsigned int real_rec_len;
	unsigned int offset;
	unsigned int toff = ocfs2_dir_trailer_blk_off(fs);

	/*
	 * db_last is the last dirent we're *keeping*.  So we need to 
	 * move out every valid dirent *after* db_last.
	 *
	 * tunefs_prepare_dir_trailer() should have calculated this
	 * correctly.
	 */
	offset = ((char *)db->db_last) - db->db_buf;
	offset += db->db_last->rec_len;
	while (offset < fs->fs_blocksize) {
		dirent = (struct ocfs2_dir_entry *) (db->db_buf + offset);
		if (((offset + dirent->rec_len) > fs->fs_blocksize) ||
		    (dirent->rec_len < 8) ||
		    ((dirent->rec_len % 4) != 0) ||
		    (((dirent->name_len & 0xFF)+8) > dirent->rec_len)) {
			ret = OCFS2_ET_DIR_CORRUPTED;
			break;
		}

		real_rec_len = dirent->inode ?
			OCFS2_DIR_REC_LEN(dirent->name_len) :
			OCFS2_DIR_REC_LEN(1);

		assert((offset + real_rec_len) > toff);

		/* Only live dirents need to be moved */
		if (dirent->inode) {
			verbosef(VL_DEBUG,
				 "Moving dirent %.*s out of directory "
				 "block %"PRIu64" to make way for the "
				 "trailer\n",
				 dirent->name_len, dirent->name,
				 db->db_blkno);
			shift_dirent(fs, tc, dirent);
		}

		offset += dirent->rec_len;
	}

	/*
	 * Now that we've moved any dirents out of the way, we need to
	 * fix up db_last and install the trailer.
	 */
	offset = ((char *)db->db_last) - db->db_buf;
	verbosef(VL_DEBUG,
		 "Last valid dirent of directory block %"PRIu64" "
		 "(\"%.*s\") is %u bytes in.  Setting rec_len to %u and "
		 "installing the trailer\n",
		 db->db_blkno, db->db_last->name_len, db->db_last->name,
		 offset, toff - offset);
	db->db_last->rec_len = toff - offset;
	ocfs2_init_dir_trailer(fs, tc->d_di, db->db_blkno, db->db_buf);

	return ret;
}

static errcode_t run_dirblocks(ocfs2_filesys *fs,
			       struct tunefs_trailer_context *tc)
{
	errcode_t ret = 0;
	struct list_head *pos;
	struct tunefs_trailer_dirblock *db;

	list_for_each(pos, &tc->d_dirblocks) {
		db = list_entry(pos, struct tunefs_trailer_dirblock, db_list);
		ret = fixup_dirblock(fs, tc, db);
		if (ret)
			break;
	}

	return ret;
}

static errcode_t write_dirblocks(ocfs2_filesys *fs,
				 struct tunefs_trailer_context *tc)
{
	errcode_t ret = 0;
	struct list_head *pos;
	struct tunefs_trailer_dirblock *db;

	list_for_each(pos, &tc->d_dirblocks) {
		db = list_entry(pos, struct tunefs_trailer_dirblock, db_list);
		ret = ocfs2_write_dir_block(fs, tc->d_di, db->db_blkno,
					    db->db_buf);
		if (ret) {
			verbosef(VL_DEBUG,
				 "Error writing dirblock %"PRIu64"\n",
				 db->db_blkno);
			break;
		}
	}

	return ret;
}

static errcode_t init_new_dirblocks(ocfs2_filesys *fs,
				    struct tunefs_trailer_context *tc)
{
	int i;
	errcode_t ret;
	uint64_t blkno;
	uint64_t orig_block = ocfs2_blocks_in_bytes(fs, tc->d_di->i_size);
	ocfs2_cached_inode *cinode;
	char *blockptr;
	struct ocfs2_dir_entry *first;

	ret = ocfs2_read_cached_inode(fs, tc->d_blkno, &cinode);
	if (ret)
		goto out;
	assert(!memcmp(tc->d_di, cinode->ci_inode, fs->fs_blocksize));

	for (i = 0; i < tc->d_blocks_needed; i++) {
		ret = ocfs2_extent_map_get_blocks(cinode, orig_block + i,
						  1, &blkno, NULL, NULL);
		if (ret)
			goto out;
		blockptr = tc->d_new_blocks + (i * fs->fs_blocksize);
		memset(blockptr, 0, fs->fs_blocksize);
		first = (struct ocfs2_dir_entry *)blockptr;
		first->rec_len = ocfs2_dir_trailer_blk_off(fs);
		ocfs2_init_dir_trailer(fs, tc->d_di, blkno, blockptr);
	}

out:
	return ret;
}

static errcode_t write_new_dirblocks(ocfs2_filesys *fs,
				     struct tunefs_trailer_context *tc)
{
	int i;
	errcode_t ret;
	uint64_t blkno;
	uint64_t orig_block = ocfs2_blocks_in_bytes(fs, tc->d_di->i_size);
	ocfs2_cached_inode *cinode;
	char *blockptr;

	ret = ocfs2_read_cached_inode(fs, tc->d_blkno, &cinode);
	if (ret)
		goto out;
	assert(!memcmp(tc->d_di, cinode->ci_inode, fs->fs_blocksize));

	for (i = 0; i < tc->d_blocks_needed; i++) {
		ret = ocfs2_extent_map_get_blocks(cinode, orig_block + i,
						  1, &blkno, NULL, NULL);
		if (ret)
			goto out;
		blockptr = tc->d_new_blocks + (i * fs->fs_blocksize);
		ret = ocfs2_write_dir_block(fs, tc->d_di, blkno, blockptr);
		if (ret) {
			verbosef(VL_DEBUG,
				 "Error writing dirblock %"PRIu64"\n",
				 blkno);
			goto out;
		}
	}

out:
	return ret;
}


static errcode_t tunefs_install_dir_trailer(ocfs2_filesys *fs,
					    struct ocfs2_dinode *di,
					    struct tunefs_trailer_context *tc)
{
	errcode_t ret = 0;
	struct tunefs_trailer_context *our_tc = NULL;

	if (!tc) {
		ret = tunefs_prepare_dir_trailer(fs, di, &our_tc);
		if (ret)
			goto out;
		tc = our_tc;
	}

	if (tc->d_di != di) {
		ret = OCFS2_ET_INVALID_ARGUMENT;
		goto out;
	}

	if (tc->d_blocks_needed) {
		ret = ocfs2_malloc_blocks(fs->fs_io, tc->d_blocks_needed,
					  &tc->d_new_blocks);
		if (ret)
			goto out;

		tc->d_cur_block = tc->d_new_blocks;

		ret = expand_dir_if_needed(fs, di, tc->d_blocks_needed);
		if (ret)
			goto out;

		ret = init_new_dirblocks(fs, tc);
		if (ret)
			goto out;
		tc->d_next_dirent = (struct ocfs2_dir_entry *)tc->d_cur_block;
		verbosef(VL_DEBUG, "t_next_dirent has rec_len of %u\n",
			 tc->d_next_dirent->rec_len);
	}

	ret = run_dirblocks(fs, tc);
	if (ret)
		goto out;

	/*
	 * We write in a specific order.  We write any new dirblocks first
	 * so that they are on disk.  Then we write the new i_size in the
	 * inode.  If we crash at this point, the directory has duplicate
	 * entries but no lost entries.  fsck can clean it up.  Finally, we
	 * write the modified dirblocks with trailers.
	 */
	if (tc->d_blocks_needed) {
		ret = write_new_dirblocks(fs, tc);
		if (ret)
			goto out;

		di->i_size += ocfs2_blocks_to_bytes(fs, tc->d_blocks_needed);
		ret = ocfs2_write_inode(fs, di->i_blkno, (char *)di);
		if (ret)
			goto out;
	}

	ret = write_dirblocks(fs, tc);

out:
	if (our_tc)
		tunefs_trailer_context_free(our_tc);
	return ret;
}


/*
 * Since we have to scan the inodes in our first pass to find directories
 * that need trailers, we might as well store them off and avoid reading
 * them again when its time to write ECC data.  In fact, we'll do all the
 * scanning up-front, including extent blocks and group descriptors.  The
 * only metadata block we don't store is the superblock, because we'll
 * write that last from fs->fs_super.
 *
 * We store all of this in an rb-tree of block_to_ecc structures.  We can
 * look blocks back up if needed, and we have writeback functions attached.
 *
 * For directory inodes, we pass e_buf into tunefs_prepare_dir_trailer(),
 * which does not copy off the inode.  Thus, when
 * tunefs_install_dir_trailer() modifies the inode, this is the one that
 * gets updated.
 *
 * For directory blocks, tunefs_prepare_dir_trailer() makes its own copies.
 * After we run tunefs_install_dir_trailer(), we'll have to copy the
 * changes back to our copy.
 */
struct block_to_ecc {
	struct rb_node e_node;
	uint64_t e_blkno;
	struct ocfs2_dinode *e_di;
	char *e_buf;
	errcode_t (*e_write)(ocfs2_filesys *fs, struct block_to_ecc *block);
};

/*
 * We have to do chain allocators at the end, because we may use them
 * as we add dirblock trailers.  Really, we only need the inode block
 * number.
 */
struct chain_to_ecc {
	struct list_head ce_list;
	uint64_t ce_blkno;
};

struct add_ecc_context {
	errcode_t ae_ret;
	struct tools_progress *ae_prog;

	uint32_t ae_clusters;
	struct list_head ae_dirs;
	uint64_t ae_dircount;
	struct list_head ae_chains;
	uint64_t ae_chaincount;
	struct rb_root ae_blocks;
	uint64_t ae_blockcount;
};

static void block_free(struct block_to_ecc *block)
{
	if (block->e_buf)
		ocfs2_free(&block->e_buf);
	ocfs2_free(&block);
}

static struct block_to_ecc *block_lookup(struct add_ecc_context *ctxt,
					 uint64_t blkno)
{
	struct rb_node *p = ctxt->ae_blocks.rb_node;
	struct block_to_ecc *block;

	while (p) {
		block = rb_entry(p, struct block_to_ecc, e_node);
		if (blkno < block->e_blkno) {
			p = p->rb_left;
		} else if (blkno > block->e_blkno) {
			p = p->rb_right;
		} else
			return block;
	}

	return NULL;
}

static void dump_ecc_tree(struct add_ecc_context *ctxt)
{
	struct rb_node *n;
	struct block_to_ecc *tmp;

	verbosef(VL_DEBUG, "Dumping ecc block tree\n");
	n = rb_first(&ctxt->ae_blocks);
	while (n) {
		tmp = rb_entry(n, struct block_to_ecc, e_node);
		verbosef(VL_DEBUG, "Block %"PRIu64", struct %p, buf %p\n",
			 tmp->e_blkno, tmp, tmp->e_buf);
		n = rb_next(n);
	}
}

static errcode_t block_insert(struct add_ecc_context *ctxt,
			      struct block_to_ecc *block)
{
	errcode_t ret;
	struct block_to_ecc *tmp;
	struct rb_node **p = &ctxt->ae_blocks.rb_node;
	struct rb_node *parent = NULL;

	while (*p) {
		parent = *p;
		tmp = rb_entry(parent, struct block_to_ecc, e_node);
		if (block->e_blkno < tmp->e_blkno) {
			p = &(*p)->rb_left;
			tmp = NULL;
		} else if (block->e_blkno > tmp->e_blkno) {
			p = &(*p)->rb_right;
			tmp = NULL;
		} else {
			dump_ecc_tree(ctxt);
			assert(0);  /* We shouldn't find it. */
		}
	}

	rb_link_node(&block->e_node, parent, p);
	rb_insert_color(&block->e_node, &ctxt->ae_blocks);
	ctxt->ae_blockcount++;
	ret = 0;

	return ret;
}

static errcode_t dinode_write_func(ocfs2_filesys *fs,
				   struct block_to_ecc *block)
{
	return ocfs2_write_inode(fs, block->e_blkno, block->e_buf);
}

static errcode_t block_insert_dinode(ocfs2_filesys *fs,
				     struct add_ecc_context *ctxt,
				     struct ocfs2_dinode *di)
{
	errcode_t ret;
	struct block_to_ecc *block = NULL;

	ret = ocfs2_malloc0(sizeof(struct block_to_ecc), &block);
	if (ret)
		goto out;

	ret = ocfs2_malloc_block(fs->fs_io, &block->e_buf);
	if (ret)
		goto out;

	memcpy(block->e_buf, di, fs->fs_blocksize);
	block->e_di = (struct ocfs2_dinode *)block->e_buf;
	block->e_blkno = di->i_blkno;
	block->e_write = dinode_write_func;
	block_insert(ctxt, block);

out:
	if (ret && block)
		block_free(block);
	return ret;
}

static errcode_t eb_write_func(ocfs2_filesys *fs,
			       struct block_to_ecc *block)
{
	return ocfs2_write_extent_block(fs, block->e_blkno, block->e_buf);
}

static errcode_t block_insert_eb(ocfs2_filesys *fs,
				 struct add_ecc_context *ctxt,
				 struct ocfs2_dinode *di,
				 struct ocfs2_extent_block *eb)
{
	errcode_t ret;
	struct block_to_ecc *block = NULL;

	ret = ocfs2_malloc0(sizeof(struct block_to_ecc), &block);
	if (ret)
		goto out;

	ret = ocfs2_malloc_block(fs->fs_io, &block->e_buf);
	if (ret)
		goto out;

	memcpy(block->e_buf, eb, fs->fs_blocksize);
	block->e_blkno = eb->h_blkno;
	block->e_write = eb_write_func;
	block_insert(ctxt, block);

out:
	if (ret && block)
		block_free(block);
	return ret;
}

static errcode_t gd_write_func(ocfs2_filesys *fs,
			       struct block_to_ecc *block)
{
	return ocfs2_write_group_desc(fs, block->e_blkno, block->e_buf);
}

static errcode_t block_insert_gd(ocfs2_filesys *fs,
				 struct add_ecc_context *ctxt,
				 struct ocfs2_dinode *di,
				 struct ocfs2_group_desc *gd)
{
	errcode_t ret;
	struct block_to_ecc *block = NULL;

	ret = ocfs2_malloc0(sizeof(struct block_to_ecc), &block);
	if (ret)
		goto out;

	ret = ocfs2_malloc_block(fs->fs_io, &block->e_buf);
	if (ret)
		goto out;

	memcpy(block->e_buf, gd, fs->fs_blocksize);
	block->e_blkno = gd->bg_blkno;
	block->e_write = gd_write_func;
	block_insert(ctxt, block);

out:
	if (ret && block)
		block_free(block);
	return ret;
}

static errcode_t dirblock_write_func(ocfs2_filesys *fs,
				     struct block_to_ecc *block)
{
	return ocfs2_write_dir_block(fs, block->e_di, block->e_blkno,
				     block->e_buf);
}

static errcode_t block_insert_dirblock(ocfs2_filesys *fs,
				       struct add_ecc_context *ctxt,
				       struct ocfs2_dinode *di,
				       uint64_t blkno, char *buf)
{
	errcode_t ret;
	struct block_to_ecc *block = NULL;

	ret = ocfs2_malloc0(sizeof(struct block_to_ecc), &block);
	if (ret)
		goto out;

	ret = ocfs2_malloc_block(fs->fs_io, &block->e_buf);
	if (ret)
		goto out;

	memcpy(block->e_buf, buf, fs->fs_blocksize);
	block->e_di = di;
	block->e_blkno = blkno;
	block->e_write = dirblock_write_func;
	block_insert(ctxt, block);

out:
	if (ret && block)
		block_free(block);
	return ret;

}

static void empty_ecc_blocks(struct add_ecc_context *ctxt)
{
	struct block_to_ecc *block;
	struct rb_node *node;

	while ((node = rb_first(&ctxt->ae_blocks)) != NULL) {
		block = rb_entry(node, struct block_to_ecc, e_node);

		rb_erase(&block->e_node, &ctxt->ae_blocks);
		ocfs2_free(&block->e_buf);
		ocfs2_free(&block);
	}
}

static errcode_t add_ecc_chain(struct add_ecc_context *ctxt,
			       uint64_t blkno)
{
	errcode_t ret;
	struct chain_to_ecc *cte;

	ret = ocfs2_malloc0(sizeof(struct chain_to_ecc), &cte);
	if (!ret) {
		cte->ce_blkno = blkno;
		list_add_tail(&cte->ce_list, &ctxt->ae_chains);
		ctxt->ae_chaincount++;
	}

	return ret;
}

static void empty_add_ecc_context(struct add_ecc_context *ctxt)
{
	struct tunefs_trailer_context *tc;
	struct chain_to_ecc *cte;
	struct list_head *n, *pos;

	list_for_each_safe(pos, n, &ctxt->ae_chains) {
		cte = list_entry(pos, struct chain_to_ecc, ce_list);
		list_del(&cte->ce_list);
		ocfs2_free(&cte);
	}

	list_for_each_safe(pos, n, &ctxt->ae_dirs) {
		tc = list_entry(pos, struct tunefs_trailer_context, d_list);
		tunefs_trailer_context_free(tc);
	}

	empty_ecc_blocks(ctxt);
}

struct add_ecc_iterate {
	struct add_ecc_context *ic_ctxt;
	struct ocfs2_dinode *ic_di;
};

static int chain_iterate(ocfs2_filesys *fs, uint64_t gd_blkno,
			 int chain_num, void *priv_data)
{
	struct add_ecc_iterate *iter = priv_data;
	struct ocfs2_group_desc *gd = NULL;
	errcode_t ret;
	int iret = 0;

	ret = ocfs2_malloc_block(fs->fs_io, &gd);
	if (ret)
		goto out;

	verbosef(VL_DEBUG, "Reading group descriptor at %"PRIu64"\n",
		 gd_blkno);
	ret = ocfs2_read_group_desc(fs, gd_blkno, (char *)gd);
	if (ret)
		goto out;

	ret = block_insert_gd(fs, iter->ic_ctxt, iter->ic_di, gd);

out:
	if (gd)
		ocfs2_free(&gd);
	if (ret) {
		iter->ic_ctxt->ae_ret = ret;
		iret = OCFS2_CHAIN_ABORT;
	}

	return iret;
}

/*
 * Right now, this only handles directory data.  Quota stuff will want
 * to genericize this or copy it.
 */
static int dirdata_iterate(ocfs2_filesys *fs, struct ocfs2_extent_rec *rec,
			   int tree_depth, uint32_t ccount,
			   uint64_t ref_blkno, int ref_recno,
			   void *priv_data)
{
	errcode_t ret = 0;
	struct add_ecc_iterate *iter = priv_data;
	char *buf = NULL;
	struct ocfs2_extent_block *eb;
	int iret = 0;
	uint64_t blocks, i;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		goto out;

	if (tree_depth) {
		verbosef(VL_DEBUG, "Reading extent block at %"PRIu64"\n",
			 rec->e_blkno);
		eb = (struct ocfs2_extent_block *)buf;
		ret = ocfs2_read_extent_block(fs, rec->e_blkno, (char *)eb);
		if (ret)
			goto out;

		ret = block_insert_eb(fs, iter->ic_ctxt, iter->ic_di, eb);
	} else {
		blocks = ocfs2_clusters_to_blocks(fs, rec->e_leaf_clusters);
		for (i = 0; i < blocks; i++) {
			ret = ocfs2_read_dir_block(fs, iter->ic_di,
						   rec->e_blkno + i, buf);
			if (ret)
				break;

			ret = block_insert_dirblock(fs, iter->ic_ctxt,
						    iter->ic_di,
						    rec->e_blkno + i, buf);
			if (ret)
				break;
		}
	}

out:
	if (buf)
		ocfs2_free(&buf);
	if (ret) {
		iter->ic_ctxt->ae_ret = ret;
		iret = OCFS2_EXTENT_ABORT;
	}

	return iret;
}

static int metadata_iterate(ocfs2_filesys *fs, struct ocfs2_extent_rec *rec,
			    int tree_depth, uint32_t ccount,
			    uint64_t ref_blkno, int ref_recno,
			    void *priv_data)
{
	errcode_t ret = 0;
	struct add_ecc_iterate *iter = priv_data;
	struct ocfs2_extent_block *eb = NULL;
	int iret = 0;

	if (!tree_depth)
		goto out;

	ret = ocfs2_malloc_block(fs->fs_io, &eb);
	if (ret)
		goto out;

	verbosef(VL_DEBUG, "Reading extent block at %"PRIu64"\n",
		 rec->e_blkno);
	ret = ocfs2_read_extent_block(fs, rec->e_blkno, (char *)eb);
	if (ret)
		goto out;

	ret = block_insert_eb(fs, iter->ic_ctxt, iter->ic_di, eb);

out:
	if (eb)
		ocfs2_free(&eb);
	if (ret) {
		iter->ic_ctxt->ae_ret = ret;
		iret = OCFS2_EXTENT_ABORT;
	}

	return iret;
}

/*
 * This walks all the chain allocators we've stored off and adds their
 * blocks to the list.
 */
static errcode_t find_chain_blocks(ocfs2_filesys *fs,
				   struct add_ecc_context *ctxt)
{
	errcode_t ret;
	struct list_head *pos;
	struct chain_to_ecc *cte;
	struct ocfs2_dinode *di;
	struct block_to_ecc *block;
	char *buf = NULL;
	struct tools_progress *prog;
	struct add_ecc_iterate iter = {
		.ic_ctxt = ctxt,
	};

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		goto out;

	prog = tools_progress_start("Scanning allocators", "chains",
				    ctxt->ae_chaincount);
	if (!prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		goto out;
	}

	list_for_each(pos, &ctxt->ae_chains) {
		cte = list_entry(pos, struct chain_to_ecc, ce_list);
		ret = ocfs2_read_inode(fs, cte->ce_blkno, buf);
		if (ret)
			break;

		di = (struct ocfs2_dinode *)buf;
		ret = block_insert_dinode(fs, ctxt, di);
		if (ret)
			break;

		/* We need the inode, look it back up */
		block = block_lookup(ctxt, di->i_blkno);
		if (!block) {
			ret = TUNEFS_ET_INTERNAL_FAILURE;
			break;
		}

		/* Now using our copy of the inode */
		di = (struct ocfs2_dinode *)block->e_buf;
		assert(di->i_blkno == cte->ce_blkno);

		iter.ic_di = di;
		ret = ocfs2_chain_iterate(fs, di->i_blkno, chain_iterate,
					  &iter);
		if (ret)
			break;
		tools_progress_step(prog, 1);
	}

	tools_progress_stop(prog);

out:
	if (buf)
		ocfs2_free(&buf);

	return ret;
}


static errcode_t inode_iterate(ocfs2_filesys *fs, struct ocfs2_dinode *di,
			       void *user_data)
{
	errcode_t ret;
	struct block_to_ecc *block = NULL;
	struct tunefs_trailer_context *tc;
	struct add_ecc_context *ctxt = user_data;
	struct add_ecc_iterate iter = {
		.ic_ctxt = ctxt,
	};

	/*
	 * We have to handle chain allocators later, after the dir
	 * trailer code has done any allocation it needs.
	 */
	if (di->i_flags & OCFS2_CHAIN_FL) {
		ret = add_ecc_chain(ctxt, di->i_blkno);
		goto out;
	}

	ret = block_insert_dinode(fs, ctxt, di);
	if (ret)
		goto out;

	/* These inodes have no other metadata on them */
	if ((di->i_flags & (OCFS2_SUPER_BLOCK_FL | OCFS2_LOCAL_ALLOC_FL |
			    OCFS2_DEALLOC_FL)) ||
	    (S_ISLNK(di->i_mode) && di->i_clusters == 0) ||
	    (di->i_dyn_features & OCFS2_INLINE_DATA_FL))
		goto out;

	/* We need the inode, look it back up */
	block = block_lookup(ctxt, di->i_blkno);
	if (!block) {
		ret = TUNEFS_ET_INTERNAL_FAILURE;
		goto out;
	}

	/* Now using our copy of the inode */
	di = (struct ocfs2_dinode *)block->e_buf;
	iter.ic_di = di;

	/*
	 * Ok, it's a regular file or directory.
	 * If it's a regular file, gather extent blocks for this inode.
	 * If it's a directory that has trailers, we need to gather all
	 * of its blocks, data and metadata.
	 *
	 * We don't gather extent info for directories that need trailers
	 * yet, because they might get modified as they gain trailers.
	 * We'll add them after we insert their trailers.
	 */
	if (!S_ISDIR(di->i_mode))
		ret = ocfs2_extent_iterate_inode(fs, di, 0, NULL,
						 metadata_iterate, &iter);
	else if (ocfs2_dir_has_trailer(fs, di))
		ret = ocfs2_extent_iterate_inode(fs, di, 0, NULL,
						 dirdata_iterate, &iter);
	else {
		ret = tunefs_prepare_dir_trailer(fs, di, &tc);
		if (!ret) {
			verbosef(VL_DEBUG,
				 "Directory %"PRIu64" needs %"PRIu64" "
				 "more blocks\n",
				 tc->d_blkno, tc->d_blocks_needed);
			list_add(&tc->d_list, &ctxt->ae_dirs);
			ctxt->ae_dircount++;
			ctxt->ae_clusters +=
				ocfs2_clusters_in_blocks(fs,
							 tc->d_blocks_needed);
		}
	}

out:
	tools_progress_step(ctxt->ae_prog, 1);

	return ret;
}

static errcode_t find_blocks(ocfs2_filesys *fs, struct add_ecc_context *ctxt)
{
	errcode_t ret;
	uint32_t free_clusters = 0;

	ctxt->ae_prog = tools_progress_start("Scanning filesystem",
					     "scanning", 0);
	if (!ctxt->ae_prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		goto bail;
	}

	ret = tunefs_foreach_inode(fs, inode_iterate, ctxt);
	if (ret)
		goto bail;
	tools_progress_stop(ctxt->ae_prog);
	ctxt->ae_prog = NULL;

	ret = tunefs_get_free_clusters(fs, &free_clusters);
	if (ret)
		goto bail;

	verbosef(VL_APP,
		 "We have %u clusters free, and need %u clusters to add "
		 "trailers to every directory\n",
		 free_clusters, ctxt->ae_clusters);

	if (free_clusters < ctxt->ae_clusters)
		ret = OCFS2_ET_NO_SPACE;

bail:
	if (ctxt->ae_prog)
		tools_progress_stop(ctxt->ae_prog);
	return ret;
}

static errcode_t install_trailers(ocfs2_filesys *fs,
				  struct add_ecc_context *ctxt)
{
	errcode_t ret = 0;
	struct tunefs_trailer_context *tc;
	struct list_head *n, *pos;
	struct tools_progress *prog;
	struct add_ecc_iterate iter = {
		.ic_ctxt = ctxt,
	};

	prog = tools_progress_start("Installing dir trailers",
				    "trailers", ctxt->ae_dircount);
	list_for_each_safe(pos, n, &ctxt->ae_dirs) {
		tc = list_entry(pos, struct tunefs_trailer_context, d_list);
		verbosef(VL_DEBUG,
			 "Writing trailer for dinode %"PRIu64"\n",
			 tc->d_di->i_blkno);
		tunefs_block_signals();
		ret = tunefs_install_dir_trailer(fs, tc->d_di, tc);
		tunefs_unblock_signals();
		if (ret)
			break;

		iter.ic_di = tc->d_di;
		tunefs_trailer_context_free(tc);

		/*
		 * Now that we've put trailers on the directory, we know
		 * its allocation won't change.  Add its blocks to the
		 * block list.  These will be cached, as installing the
		 * trailer will have just touched them.
		 */
		ret = ocfs2_extent_iterate_inode(fs, tc->d_di, 0, NULL,
						 dirdata_iterate, &iter);
		if (ret)
			break;

		tools_progress_step(prog, 1);
	}
	tools_progress_stop(prog);

	return ret;
}

static errcode_t write_ecc_blocks(ocfs2_filesys *fs,
				  struct add_ecc_context *ctxt)
{
	errcode_t ret = 0;
	struct rb_node *n;
	struct block_to_ecc *block;
	struct tools_progress *prog;

	prog = tools_progress_start("Writing blocks", "ECC",
				    ctxt->ae_blockcount);
	if (!prog)
		return TUNEFS_ET_NO_MEMORY;

	n = rb_first(&ctxt->ae_blocks);
	while (n) {
		block = rb_entry(n, struct block_to_ecc, e_node);
		verbosef(VL_DEBUG, "Writing block %"PRIu64"\n",
			 block->e_blkno);

		tools_progress_step(prog, 1);
		ret = block->e_write(fs, block);
		if (ret)
			break;

		n = rb_next(n);
	}
	tools_progress_stop(prog);

	return ret;
}

static int enable_metaecc(ocfs2_filesys *fs, int flags)
{
	errcode_t ret = 0;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);
	struct add_ecc_context ctxt;
	struct tools_progress *prog = NULL;

	if (ocfs2_meta_ecc(super)) {
		verbosef(VL_APP,
			 "The metadata ECC feature is already enabled; "
			 "nothing to enable\n");
		goto out;
	}

	if (!tools_interact("Enable the metadata ECC feature on device "
			    "\"%s\"? ",
			    fs->fs_devname))
		goto out;

	prog = tools_progress_start("Enabling metaecc", "metaecc", 5);
	if (!prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		tcom_err(ret, "while initializing the progress display");
		goto out;
	}

	memset(&ctxt, 0, sizeof(ctxt));
	INIT_LIST_HEAD(&ctxt.ae_dirs);
	INIT_LIST_HEAD(&ctxt.ae_chains);
	ctxt.ae_blocks = RB_ROOT;
	ret = find_blocks(fs, &ctxt);
	if (ret) {
		if (ret == OCFS2_ET_NO_SPACE)
			errorf("There is not enough space to add directory "
			       "trailers to the directories on device "
			       "\"%s\"\n",
			       fs->fs_devname);
		else
			tcom_err(ret,
				 "while trying to find directory blocks");
		goto out_cleanup;
	}
	tools_progress_step(prog, 1);

	ret = tunefs_set_in_progress(fs, OCFS2_TUNEFS_INPROG_DIR_TRAILER);
	if (ret)
		goto out_cleanup;

	ret = install_trailers(fs, &ctxt);
	if (ret) {
		tcom_err(ret,
			 "while trying to install directory trailers on "
			 "device \"%s\"",
			 fs->fs_devname);
		goto out_cleanup;
	}

	ret = tunefs_clear_in_progress(fs, OCFS2_TUNEFS_INPROG_DIR_TRAILER);
	if (ret)
		goto out_cleanup;

	tools_progress_step(prog, 1);

	/* We're done with allocation, scan the chain allocators */
	ret = find_chain_blocks(fs, &ctxt);
	if (ret)
		goto out_cleanup;

	tools_progress_step(prog, 1);

	/* Set the feature bit in-memory and rewrite all our blocks */
	OCFS2_SET_INCOMPAT_FEATURE(super, OCFS2_FEATURE_INCOMPAT_META_ECC);
	ret = write_ecc_blocks(fs, &ctxt);
	if (ret)
		goto out_cleanup;

	tools_progress_step(prog, 1);

	tunefs_block_signals();
	ret = ocfs2_write_super(fs);
	tunefs_unblock_signals();
	if (ret)
		tcom_err(ret, "while writing out the superblock");

	tools_progress_step(prog, 1);

out_cleanup:
	empty_add_ecc_context(&ctxt);

out:
	if (prog)
		tools_progress_stop(prog);

	return ret;
}

static int disable_metaecc(ocfs2_filesys *fs, int flags)
{
	errcode_t ret = 0;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);
	struct tools_progress *prog = NULL;

	if (!ocfs2_meta_ecc(super)) {
		verbosef(VL_APP,
			 "The metadata ECC feature is not enabled; "
			 "nothing to disable\n");
		goto out;
	}

	if (!tools_interact("Disable the metadata ECC feature on device "
			    "\"%s\"? ",
			    fs->fs_devname))
		goto out;

	prog = tools_progress_start("Disabling metaecc", "nometaecc", 1);
	if (!prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		goto out;
	}


	OCFS2_CLEAR_INCOMPAT_FEATURE(super,
				     OCFS2_FEATURE_INCOMPAT_META_ECC);
	tunefs_block_signals();
	ret = ocfs2_write_super(fs);
	tunefs_unblock_signals();
	if (ret)
		tcom_err(ret, "while writing out the superblock");

	tools_progress_step(prog, 1);

out:
	if (prog)
		tools_progress_stop(prog);

	return ret;
}


DEFINE_TUNEFS_FEATURE_INCOMPAT(metaecc,
			       OCFS2_FEATURE_INCOMPAT_META_ECC,
			       TUNEFS_FLAG_RW | TUNEFS_FLAG_ALLOCATION |
			       TUNEFS_FLAG_LARGECACHE,
			       enable_metaecc,
			       disable_metaecc);

#ifdef DEBUG_EXE
int main(int argc, char *argv[])
{
	return tunefs_feature_main(argc, argv, &metaecc_feature);
}
#endif
