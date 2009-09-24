/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * extent_tree.c
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
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "ocfs2/byteorder.h"
#include "ocfs2/ocfs2.h"
#include "extent_tree.h"

static void ocfs2_dinode_set_last_eb_blk(struct ocfs2_extent_tree *et,
					 uint64_t blkno)
{
	struct ocfs2_dinode *di = et->et_object;

	di->i_last_eb_blk = blkno;
}

static uint64_t ocfs2_dinode_get_last_eb_blk(struct ocfs2_extent_tree *et)
{
	struct ocfs2_dinode *di = et->et_object;

	return di->i_last_eb_blk;
}

static void ocfs2_dinode_update_clusters(struct ocfs2_extent_tree *et,
					 uint32_t clusters)
{
	struct ocfs2_dinode *di = et->et_object;

	di->i_clusters += clusters;
}
static void ocfs2_dinode_fill_root_el(struct ocfs2_extent_tree *et)
{
	struct ocfs2_dinode *di = et->et_object;

	et->et_root_el = &di->id2.i_list;
}

static struct ocfs2_extent_tree_operations ocfs2_dinode_et_ops = {
	.eo_set_last_eb_blk	= ocfs2_dinode_set_last_eb_blk,
	.eo_get_last_eb_blk	= ocfs2_dinode_get_last_eb_blk,
	.eo_update_clusters	= ocfs2_dinode_update_clusters,
	.eo_fill_root_el	= ocfs2_dinode_fill_root_el,
};

static void __ocfs2_init_extent_tree(struct ocfs2_extent_tree *et,
				     ocfs2_filesys *fs,
				     char *buf,
				     void *obj,
				     struct ocfs2_extent_tree_operations *ops)
{
	et->et_ops = ops;
	et->et_root_buf = buf;
	et->et_object = obj;

	et->et_ops->eo_fill_root_el(et);
	if (!et->et_ops->eo_fill_max_leaf_clusters)
		et->et_max_leaf_clusters = 0;
	else
		et->et_ops->eo_fill_max_leaf_clusters(fs, et);
}

void ocfs2_init_dinode_extent_tree(struct ocfs2_extent_tree *et,
				   ocfs2_filesys *fs,
				   char *buf)
{
	__ocfs2_init_extent_tree(et, fs, buf,
				 buf, &ocfs2_dinode_et_ops);
}
