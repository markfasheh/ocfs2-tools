/*
 * dirblocks.h
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

#ifndef __O2FSCK_DIRBLOCKS_H__
#define __O2FSCK_DIRBLOCKS_H__

#include "ocfs2/ocfs2.h"
#include "ocfs2/kernel-rbtree.h"

typedef struct _o2fsck_dirblocks {
	struct rb_root	db_root;
} o2fsck_dirblocks;

typedef struct _o2fsck_dirblock_entry {
	struct rb_node	e_node;
	uint64_t	e_ino;
	uint64_t	e_blkno;
	uint64_t	e_blkcount;
} o2fsck_dirblock_entry;

typedef unsigned (*dirblock_iterator)(o2fsck_dirblock_entry *,
					void *priv_data);

errcode_t o2fsck_add_dir_block(o2fsck_dirblocks *db, uint64_t ino,
			       uint64_t blkno, uint64_t blkcount);

struct _o2fsck_state;
void o2fsck_dir_block_iterate(struct _o2fsck_state *ost, dirblock_iterator func,
                              void *priv_data);
		     
uint64_t o2fsck_search_reidx_dir(struct rb_root *root, uint64_t dino);
errcode_t o2fsck_try_add_reidx_dir(struct rb_root *root, uint64_t dino);
errcode_t o2fsck_rebuild_indexed_dirs(ocfs2_filesys *fs, struct rb_root *root);
errcode_t o2fsck_check_dir_index(struct _o2fsck_state *ost, struct ocfs2_dinode *di);

#endif /* __O2FSCK_DIRBLOCKS_H__ */

