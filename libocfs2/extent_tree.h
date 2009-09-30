/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * extent_tree.h
 *
 * Copyright (C) 2009 Oracle.  All rights reserved.
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

/* Useful typedef for passing around writing functions for extent tree root. */
typedef errcode_t (*ocfs2_root_write_func)(ocfs2_filesys *fs,
					   uint64_t blkno,
					   char *root_buf);
struct ocfs2_extent_tree {
	struct ocfs2_extent_tree_operations	*et_ops;
	char					*et_root_buf;
	uint64_t				et_root_blkno;
	ocfs2_root_write_func			et_root_write;
	struct ocfs2_extent_list		*et_root_el;
	void					*et_object;
	uint32_t				et_max_leaf_clusters;
};

/*
 * Operations for a specific extent tree type.
 *
 * To implement an on-disk btree (extent tree) type in ocfs2, add
 * an ocfs2_extent_tree_operations structure and the matching
 * ocfs2_init_<thingy>_extent_tree() function.  That's pretty much it
 * for the allocation portion of the extent tree.
 */
struct ocfs2_extent_tree_operations {
	/*
	 * last_eb_blk is the block number of the right most leaf extent
	 * block.  Most on-disk structures containing an extent tree store
	 * this value for fast access.  The ->eo_set_last_eb_blk() and
	 * ->eo_get_last_eb_blk() operations access this value.  They are
	 *  both required.
	 */
	void (*eo_set_last_eb_blk)(struct ocfs2_extent_tree *et,
				   uint64_t blkno);
	uint64_t (*eo_get_last_eb_blk)(struct ocfs2_extent_tree *et);

	/*
	 * The on-disk structure usually keeps track of how many total
	 * clusters are stored in this extent tree.  This function updates
	 * that value.  new_clusters is the delta, and must be
	 * added to the total.  Required.
	 */
	void (*eo_update_clusters)(/*struct inode *inode,*/
				   struct ocfs2_extent_tree *et,
				   uint32_t new_clusters);
	uint32_t (*eo_get_clusters)(struct ocfs2_extent_tree *et);

	/*
	 * If ->eo_insert_check() exists, it is called before rec is
	 * inserted into the extent tree.  It is optional.
	 */
	/*
	int (*eo_insert_check)(struct inode *inode,
			       struct ocfs2_extent_tree *et,
			       struct ocfs2_extent_rec *rec);
	int (*eo_sanity_check)(struct inode *inode,
			       struct ocfs2_extent_tree *et);
	*/
	int (*eo_sanity_check)(struct ocfs2_extent_tree *et);

	/*
	 * --------------------------------------------------------------
	 * The remaining are internal to ocfs2_extent_tree and don't have
	 * accessor functions
	 */

	/*
	 * ->eo_fill_root_el() takes et->et_object and sets et->et_root_el.
	 * It is required.
	 */
	void (*eo_fill_root_el)(struct ocfs2_extent_tree *et);

	/*
	 * ->eo_fill_max_leaf_clusters sets et->et_max_leaf_clusters if
	 * it exists.  If it does not, et->et_max_leaf_clusters is set
	 * to 0 (unlimited).  Optional.
	 */
	void (*eo_fill_max_leaf_clusters)(ocfs2_filesys *fs,
					  struct ocfs2_extent_tree *et);
};

void ocfs2_init_dinode_extent_tree(struct ocfs2_extent_tree *et,
				   ocfs2_filesys *fs,
				   char *buf, uint64_t blkno);
errcode_t ocfs2_tree_insert_extent(ocfs2_filesys *fs,
				   struct ocfs2_extent_tree *et,
				   uint32_t cpos, uint64_t c_blkno,
				   uint32_t clusters, uint16_t flag);
