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

enum ocfs2_contig_type {
	CONTIG_NONE = 0,
	CONTIG_LEFT,
	CONTIG_RIGHT,
	CONTIG_LEFTRIGHT,
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

	/*
	 * ->eo_extent_contig test whether the 2 ocfs2_extent_rec
	 * are contiguous or not. Optional. Don't need to set it if use
	 * ocfs2_extent_rec as the tree leaf.
	 */
	enum ocfs2_contig_type
		(*eo_extent_contig)(ocfs2_filesys *fs,
				    struct ocfs2_extent_tree *et,
				    struct ocfs2_extent_rec *ext,
				    struct ocfs2_extent_rec *insert_rec);
};

void ocfs2_init_dinode_extent_tree(struct ocfs2_extent_tree *et,
				   ocfs2_filesys *fs,
				   char *buf, uint64_t blkno);
void ocfs2_init_refcount_extent_tree(struct ocfs2_extent_tree *et,
				     ocfs2_filesys *fs,
				     char *buf, uint64_t blkno);
void ocfs2_init_xattr_value_extent_tree(struct ocfs2_extent_tree *et,
					ocfs2_filesys *fs,
					char *buf, uint64_t blkno,
					ocfs2_root_write_func write,
					struct ocfs2_xattr_value_root *xv);
errcode_t ocfs2_tree_insert_extent(ocfs2_filesys *fs,
				   struct ocfs2_extent_tree *et,
				   uint32_t cpos, uint64_t c_blkno,
				   uint32_t clusters, uint16_t flag);
int ocfs2_change_extent_flag(ocfs2_filesys *fs,
			     struct ocfs2_extent_tree *et,
			     uint32_t cpos, uint32_t len,
			     uint64_t p_blkno,
			     int new_flags, int clear_flags);
int ocfs2_remove_extent(ocfs2_filesys *fs,
			struct ocfs2_extent_tree *et,
			uint32_t cpos, uint32_t len);
/*
 * Structures which describe a path through a btree, and functions to
 * manipulate them.
 *
 * The idea here is to be as generic as possible with the tree
 * manipulation code.
 */
struct ocfs2_path_item {
	uint64_t			blkno;
	char				*buf;
	struct ocfs2_extent_list	*el;
};

#define OCFS2_MAX_PATH_DEPTH	5

struct ocfs2_path {
	int			p_tree_depth;
	struct ocfs2_path_item	p_node[OCFS2_MAX_PATH_DEPTH];
};

#define path_root_blkno(_path) ((_path)->p_node[0].blkno)
#define path_root_buf(_path) ((_path)->p_node[0].buf)
#define path_root_el(_path) ((_path)->p_node[0].el)
#define path_leaf_blkno(_path) ((_path)->p_node[(_path)->p_tree_depth].blkno)
#define path_leaf_buf(_path) ((_path)->p_node[(_path)->p_tree_depth].buf)
#define path_leaf_el(_path) ((_path)->p_node[(_path)->p_tree_depth].el)
#define path_num_items(_path) ((_path)->p_tree_depth + 1)

struct ocfs2_path *ocfs2_new_path_from_et(struct ocfs2_extent_tree *et);
int ocfs2_find_path(ocfs2_filesys *fs, struct ocfs2_path *path,
		    uint32_t cpos);
void ocfs2_free_path(struct ocfs2_path *path);
