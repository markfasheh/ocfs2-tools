/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * Copyright (C) 1993-2004 by Theodore Ts'o.
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
 * An rbtree to record a directory's parent information.  _dirent records
 * the inode who had a directory entry that points to the directory in
 * question.  _dot_dot records the inode that the directory's ".." points to;
 * who it thinks its parent is.
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
errcode_t o2fsck_add_dir_parent(struct rb_root *root,
				uint64_t ino,
				uint64_t dot_dot,
				uint64_t dirent)
{
	struct rb_node ** p = &root->rb_node;
	struct rb_node * parent = NULL;
	o2fsck_dir_parent *dp, *tmp_dp;
	errcode_t ret = 0;

	dp = calloc(1, sizeof(*dp));
	if (dp == NULL) {
		ret = OCFS2_ET_NO_MEMORY;
		goto out;
	}

	dp->dp_ino = ino;
	dp->dp_dot_dot = dot_dot;
	dp->dp_dirent = dirent;
	dp->dp_connected = 0;
	dp->dp_loop_no = 0;

	while (*p)
	{
		parent = *p;
		tmp_dp = rb_entry(parent, o2fsck_dir_parent, dp_node);

		if (dp->dp_ino < tmp_dp->dp_ino)
			p = &(*p)->rb_left;
		else if (dp->dp_ino > tmp_dp->dp_ino)
			p = &(*p)->rb_right;
		else {
			ret = OCFS2_ET_INTERNAL_FAILURE;
			goto out;
		}
	}

	rb_link_node(&dp->dp_node, parent, p);
	rb_insert_color(&dp->dp_node, root);
out:
	return ret;
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

o2fsck_dir_parent *o2fsck_dir_parent_first(struct rb_root *root)
{
	struct rb_node *node = rb_first(root);
	o2fsck_dir_parent *dp = NULL;

	if (node)
		dp = rb_entry(node, o2fsck_dir_parent, dp_node);

	return dp;
}

o2fsck_dir_parent *o2fsck_dir_parent_next(o2fsck_dir_parent *from)
{
	struct rb_node *node = rb_next(&from->dp_node);
	o2fsck_dir_parent *dp = NULL;

	if (node)
		dp = rb_entry(node, o2fsck_dir_parent, dp_node);

	return dp;
}
