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
 * Just a simple rbtree wrapper to record directory blocks and the inodes
 * that own them.
 */
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#include <assert.h>

#include "ocfs2/ocfs2.h"

#include "fsck.h"
#include "dirblocks.h"
#include "util.h"
#include "extent.h"

errcode_t o2fsck_add_dir_block(o2fsck_dirblocks *db, uint64_t ino,
			       uint64_t blkno, uint64_t blkcount)
{
	struct rb_node ** p = &db->db_root.rb_node;
	struct rb_node * parent = NULL;
	o2fsck_dirblock_entry *dbe, *tmp_dbe;
	errcode_t ret = 0;

	ret = ocfs2_malloc0(sizeof(o2fsck_dirblock_entry), &dbe);
	if (ret)
		goto out;

	dbe->e_ino = ino;
	dbe->e_blkno = blkno;
	dbe->e_blkcount = blkcount;

	while (*p)
	{
		parent = *p;
		tmp_dbe = rb_entry(parent, o2fsck_dirblock_entry, e_node);

		if (dbe->e_blkno < tmp_dbe->e_blkno)
			p = &(*p)->rb_left;
		else if (dbe->e_blkno > tmp_dbe->e_blkno)
			p = &(*p)->rb_right;
	}

	rb_link_node(&dbe->e_node, parent, p);
	rb_insert_color(&dbe->e_node, &db->db_root);

out:
	return ret;
}

/*
 * Go through the dirblocks pre-filling them.  We try to coalesce adjacent
 * ones.  Don't care to return errors, because it's a cache pre-fill.
 */
static int try_to_cache(ocfs2_filesys *fs, struct rb_node *node,
			char *pre_cache_buf, int pre_cache_blocks)
{
	int cached_blocks = 0;
	o2fsck_dirblock_entry *dbe;
	uint64_t io_blkno = 0, next_blkno = 0;
	int count = 0;
	errcode_t err;
	uint64_t blocks_seen = 0;

	o2fsck_reset_blocks_cached();
	for (; node; node = rb_next(node)) {
		blocks_seen++;
		dbe = rb_entry(node, o2fsck_dirblock_entry, e_node);
		if (io_blkno) {
			assert(count);
			assert(next_blkno > io_blkno);

			if ((next_blkno == dbe->e_blkno) &&
			    (count < pre_cache_blocks)) {
				count++;
				next_blkno++;
				continue;
			}

			if (!o2fsck_worth_caching(count)) {
				io_blkno = 0;
				break;
			}

			err = ocfs2_read_blocks(fs, io_blkno, count,
						pre_cache_buf);
			io_blkno = 0;
			next_blkno = 0;

			if (err)
				break;

			cached_blocks += count;
			count = 0;
		}

		assert(!io_blkno);
		io_blkno = dbe->e_blkno;
		next_blkno = io_blkno + 1;
		count = 1;
	}

	/* Catch the last pre-fill buffer */
	if (io_blkno && o2fsck_worth_caching(count)) {
		assert(count);
		err = ocfs2_read_blocks(fs, io_blkno, count, pre_cache_buf);
		if (!err)
			cached_blocks += count;
	}

	return cached_blocks;
}

uint64_t o2fsck_search_reidx_dir(struct rb_root *root, uint64_t dino)
{
	struct rb_node *node = root->rb_node;
	o2fsck_dirblock_entry *dbe;

	while (node) {
		dbe = rb_entry(node, o2fsck_dirblock_entry, e_node);

		if (dino < dbe->e_ino)
			node = node->rb_left;
		else if (dino > dbe->e_ino)
			node = node->rb_right;
		else
			return dbe->e_ino;
	}
	return 0;
}

static errcode_t o2fsck_add_reidx_dir_ino(struct rb_root *root, uint64_t dino)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	o2fsck_dirblock_entry *dp, *tmp_dp;
	errcode_t ret = 0;

	ret = ocfs2_malloc0(sizeof (o2fsck_dirblock_entry), &dp);
	if (ret)
		goto out;

	dp->e_ino = dino;

	while(*p)
	{
		parent = *p;
		tmp_dp = rb_entry(parent, o2fsck_dirblock_entry, e_node);

		if (dp->e_ino < tmp_dp->e_ino)
			p = &(*p)->rb_left;
		else if (dp->e_ino > tmp_dp->e_ino)
			p = &(*p)->rb_right;
		else {
			ret = OCFS2_ET_INTERNAL_FAILURE;
			ocfs2_free(&dp);
			goto out;
		}
	}

	rb_link_node(&dp->e_node, parent, p);
	rb_insert_color(&dp->e_node, root);

out:
	return ret;
}

errcode_t o2fsck_try_add_reidx_dir(struct rb_root *root, uint64_t dino)
{
	errcode_t ret = 0;
	uint64_t ino;
	ino = o2fsck_search_reidx_dir(root, dino);
	if (ino)
		goto out;
	ret = o2fsck_add_reidx_dir_ino(root, dino);

out:
	return ret;
}

void o2fsck_dir_block_iterate(o2fsck_state *ost, dirblock_iterator func,
			      void *priv_data)
{
	o2fsck_dirblocks *db = &ost->ost_dirblocks;
	ocfs2_filesys *fs = ost->ost_fs;
	o2fsck_dirblock_entry *dbe;
	struct rb_node *node;
	unsigned ret;
	errcode_t err;
	char *pre_cache_buf = NULL;
	int pre_cache_blocks = ocfs2_blocks_in_bytes(fs, 1024 * 1024);
	int cached_blocks = 0;

	o2fsck_reset_blocks_cached();
	if (o2fsck_worth_caching(1)) {
		err = ocfs2_malloc_blocks(fs->fs_io, pre_cache_blocks,
					  &pre_cache_buf);
		if (err)
			verbosef("Unable to allocate dirblock pre-cache "
				 "buffer, %s\n",
				 "ignoring");
	}

	for (node = rb_first(&db->db_root); node; node = rb_next(node)) {
		if (!cached_blocks && pre_cache_buf)
			cached_blocks = try_to_cache(fs, node, pre_cache_buf,
						     pre_cache_blocks);

		dbe = rb_entry(node, o2fsck_dirblock_entry, e_node);
		ret = func(dbe, priv_data);
		if (ret & OCFS2_DIRENT_ABORT)
			break;

		if (cached_blocks)
			cached_blocks--;
	}

	if (pre_cache_buf)
		ocfs2_free(&pre_cache_buf);
}

static errcode_t ocfs2_rebuild_indexed_dir(ocfs2_filesys *fs, uint64_t ino)
{
	errcode_t ret = 0;
	char *di_buf = NULL;
	struct ocfs2_dinode *di;


	ret = ocfs2_malloc_block(fs->fs_io, &di_buf);
	if (ret)
		goto out;

	ret = ocfs2_read_inode(fs, ino, di_buf);
	if (ret)
		goto out;
	di = (struct ocfs2_dinode *)di_buf;

	/* do not rebuild indexed tree for inline directory */
	if (di->i_dyn_features & OCFS2_INLINE_DATA_FL)
		goto out;

	ret = ocfs2_dx_dir_truncate(fs, ino);
	if (ret)
		goto out;

	ret = ocfs2_dx_dir_build(fs, ino);
out:
	if (di_buf)
		ocfs2_free(&di_buf);
	return ret;
}


errcode_t o2fsck_rebuild_indexed_dirs(ocfs2_filesys *fs, struct rb_root *root)
{
	struct rb_node *node;
	o2fsck_dirblock_entry *dbe;
	uint64_t ino;
	errcode_t ret = 0;

	for (node = rb_first(root); node; node = rb_next(node)) {
		dbe = rb_entry(node, o2fsck_dirblock_entry, e_node);
		ino = dbe->e_ino;
		ret = ocfs2_rebuild_indexed_dir(fs, ino);
		if (ret)
			goto out;
	}
out:
	return ret;
}

