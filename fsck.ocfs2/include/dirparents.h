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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 * Author: Zach Brown
 */

#ifndef __O2FSCK_DIRPARENTS_H__
#define __O2FSCK_DIRPARENTS_H__

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
	int		dp_connected;
} o2fsck_dir_parent;

void o2fsck_add_dir_parent(struct rb_root *root, uint64_t ino, 
			uint64_t dot_dot, uint64_t dirent);

o2fsck_dir_parent *o2fsck_dir_parent_lookup(struct rb_root *root, 
						uint64_t ino);
o2fsck_dir_parent *o2fsck_dir_parent_first(struct rb_root *root);
o2fsck_dir_parent *o2fsck_dir_parent_next(o2fsck_dir_parent *from);
#endif /* __O2FSCK_DIRPARENTS_H__ */

