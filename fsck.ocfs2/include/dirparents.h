/*
 * dirparents.h
 *
 * Copyright (C) 2002 Oracle Corporation.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301 USA.
 *
 * Author: Zach Brown
 */

#ifndef __O2FSCK_DIRPARENTS_H__
#define __O2FSCK_DIRPARENTS_H__

#include "ocfs2/kernel-rbtree.h"

typedef struct _o2fsck_dir_parent {
	struct rb_node	dp_node;
	uint64_t 	dp_ino; /* The dir inode in question. */

	uint64_t 	dp_dot_dot; /* The parent according to the dir's own 
				     * '..' entry */

	uint64_t 	dp_dirent; /* The inode that has a dirent which points
				    * to this directory.  */

	/* used by pass3 to walk the dir_parent structs and ensure 
	 * connectivity */
	uint64_t	dp_loop_no;
	unsigned	dp_connected:1,
			dp_in_orphan_dir:1;
} o2fsck_dir_parent;

errcode_t o2fsck_add_dir_parent(struct rb_root *root,
				uint64_t ino,
				uint64_t dot_dot,
				uint64_t dirent,
				unsigned in_orphan_dir);

o2fsck_dir_parent *o2fsck_dir_parent_lookup(struct rb_root *root, 
						uint64_t ino);
o2fsck_dir_parent *o2fsck_dir_parent_first(struct rb_root *root);
o2fsck_dir_parent *o2fsck_dir_parent_next(o2fsck_dir_parent *from);

void ocfsck_remove_dir_parent(struct rb_root *root, uint64_t ino);
#endif /* __O2FSCK_DIRPARENTS_H__ */

