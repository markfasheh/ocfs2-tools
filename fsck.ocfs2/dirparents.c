/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dirparents.c
 *
 * As always, we let e2fsck lead the way.  A bitmap for
 * inodes with a single i_count (the vast majority), and a
 * tree of inode numbers with a greater count. 
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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "ocfs2.h"

#include "fsck.h"
#include "dirparents.h"
#include "util.h"

/* XXX callers are supposed to make sure they don't call with dup inodes.
 * we'll see. */
void o2fsck_add_dir_parent(struct rb_root *root, uint64_t ino, uint64_t dot_dot,
			   uint64_t dirent)
{
	struct rb_node ** p = &root->rb_node;
	struct rb_node * parent = NULL;
	o2fsck_dir_parent *dp, *tmp_dp;

	dp = calloc(1, sizeof(*dp));
	if (dp == NULL)
		fatal_error(OCFS2_ET_NO_MEMORY, 
				"while allocating directory entries");

	dp->dp_ino = ino;
	dp->dp_dot_dot = dot_dot;
	dp->dp_dirent = dirent;

	while (*p)
	{
		parent = *p;
		tmp_dp = rb_entry(parent, o2fsck_dir_parent, dp_node);

		if (dp->dp_ino < tmp_dp->dp_ino)
			p = &(*p)->rb_left;
		else if (dp->dp_ino > tmp_dp->dp_ino)
			p = &(*p)->rb_right;
		else
			fatal_error(OCFS2_ET_INTERNAL_FAILURE, "while adding "
				    "unique dir parent tracking for dir inode "
				    "%"PRIu64, ino);
	}

	rb_link_node(&dp->dp_node, parent, p);
	rb_insert_color(&dp->dp_node, root);
}

o2fsck_dir_parent *o2fsck_dir_parent_lookup(struct rb_root *root, uint64_t ino)
{
	struct rb_node *node = root->rb_node;
	o2fsck_dir_parent *dp;

	while (node) {
		dp = rb_entry(node, o2fsck_dir_parent, dp_node);

		if (ino < dp->dp_ino)
			node = node->rb_left;
		else if (ino > dp->dp_ino)
			node = node->rb_right;
		else
			return dp;
	}
	return NULL;
}

