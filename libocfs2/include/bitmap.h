/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * bitmap.h
 *
 * Structures for allocation bitmaps for the OCFS2 userspace library.
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
 * Authors: Joel Becker
 */

#ifndef _BITMAP_H
#define _BITMAP_H

#include "kernel-rbtree.h"

struct ocfs2_bitmap_cluster {
	struct rb_node bc_node;
	uint64_t bc_start_bit;		/* Bit offset.  Must be
					   aligned on
					   (clustersize * 8) */
	int bc_total_bits;		/* set_bit() and friends can't
					   handle bitmaps larger than
					   int offsets */
	int bc_set_bits;
	char *bc_bitmap;

	void *bc_private;
};

struct ocfs2_bitmap_operations {
	errcode_t (*set_bit)(ocfs2_bitmap *bm, uint64_t bit,
			     int *oldval);
	errcode_t (*clear_bit)(ocfs2_bitmap *bm, uint64_t bit,
			       int *oldval);
	errcode_t (*test_bit)(ocfs2_bitmap *bm, uint64_t bit,
			      int *val);
	errcode_t (*merge_cluster)(ocfs2_bitmap *bm,
				   struct ocfs2_bitmap_cluster *prev,
				   struct ocfs2_bitmap_cluster *next);
	errcode_t (*read_bitmap)(ocfs2_bitmap *bm);
	errcode_t (*write_bitmap)(ocfs2_bitmap *bm);
	void (*destroy_notify)(ocfs2_bitmap *bm);
};

struct _ocfs2_bitmap {
	ocfs2_filesys *b_fs;
	uint64_t b_set_bits;
	uint64_t b_total_bits;
	char *b_description;
	struct ocfs2_bitmap_operations *b_ops;
	ocfs2_cached_inode *b_cinode;		/* Cached inode this
						   bitmap was loaded
						   from if it is a
						   physical bitmap
						   inode */
	struct rb_root b_clusters;
	void *b_private;
};


errcode_t ocfs2_bitmap_new(ocfs2_filesys *fs,
			   uint64_t total_bits,
			   const char *description,
			   struct ocfs2_bitmap_operations *ops,
			   void *private_data,
			   ocfs2_bitmap **ret_bitmap);
errcode_t ocfs2_bitmap_alloc_cluster(ocfs2_bitmap *bitmap,
				     uint64_t start_bit,
				     int total_bits,
				     struct ocfs2_bitmap_cluster **ret_bc);
void ocfs2_bitmap_free_cluster(struct ocfs2_bitmap_cluster *bc);
errcode_t ocfs2_bitmap_realloc_cluster(ocfs2_bitmap *bitmap,
				       struct ocfs2_bitmap_cluster *bc,
				       int total_bits);
errcode_t ocfs2_bitmap_insert_cluster(ocfs2_bitmap *bitmap,
				      struct ocfs2_bitmap_cluster *bc);
errcode_t ocfs2_bitmap_set_generic(ocfs2_bitmap *bitmap,
				   uint64_t bitno, int *oldval);
errcode_t ocfs2_bitmap_clear_generic(ocfs2_bitmap *bitmap,
				     uint64_t bitno, int *oldval);
errcode_t ocfs2_bitmap_test_generic(ocfs2_bitmap *bitmap,
				    uint64_t bitno, int *val);
errcode_t ocfs2_bitmap_set_holes(ocfs2_bitmap *bitmap,
				 uint64_t bitno, int *oldval);
errcode_t ocfs2_bitmap_clear_holes(ocfs2_bitmap *bitmap,
				   uint64_t bitno, int *oldval);
errcode_t ocfs2_bitmap_test_holes(ocfs2_bitmap *bitmap,
				  uint64_t bitno, int *val);
#endif  /* _BITMAP_H */
