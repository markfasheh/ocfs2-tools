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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301 USA.
 *
 * Authors: Joel Becker
 */

#ifndef _BITMAP_H
#define _BITMAP_H

#include "ocfs2/kernel-rbtree.h"


struct ocfs2_bitmap_region {
	struct rb_node br_node;
	uint64_t br_start_bit;		/* Bit offset. */
	int br_bitmap_start;		/* bit start in br_bitmap. */
	int br_valid_bits;		/* bit length valid in br_bitmap. */
	int br_total_bits;		/* set_bit() and friends can't
					   handle bitmaps larger than
					   int offsets */
	size_t br_bytes;
	int br_set_bits;
	uint8_t *br_bitmap;
	void *br_private;
};

struct ocfs2_bitmap_operations {
	errcode_t (*set_bit)(ocfs2_bitmap *bitmap, uint64_t bit,
			     int *oldval);
	errcode_t (*clear_bit)(ocfs2_bitmap *bitmap, uint64_t bit,
			       int *oldval);
	errcode_t (*test_bit)(ocfs2_bitmap *bitmap, uint64_t bit,
			      int *val);
	errcode_t (*find_next_set)(ocfs2_bitmap *bitmap,
				   uint64_t start, 
				   uint64_t *found);
	errcode_t (*find_next_clear)(ocfs2_bitmap *bitmap,
				     uint64_t start, 
				     uint64_t *found);
	int (*merge_region)(ocfs2_bitmap *bitmap,
			    struct ocfs2_bitmap_region *prev,
			    struct ocfs2_bitmap_region *next);
	errcode_t (*read_bitmap)(ocfs2_bitmap *bitmap);
	errcode_t (*write_bitmap)(ocfs2_bitmap *bitmap);
	void (*destroy_notify)(ocfs2_bitmap *bitmap);
	void (*bit_change_notify)(ocfs2_bitmap *bitmap,
				  struct ocfs2_bitmap_region *br,
				  uint64_t bitno,
				  int new_val);
	errcode_t (*alloc_range)(ocfs2_bitmap *bitmap, uint64_t min_len,
				 uint64_t len, uint64_t *first_bit,
				 uint64_t *bits_found);
	errcode_t (*clear_range)(ocfs2_bitmap *bitmap, uint64_t len, 
				 uint64_t first_bit);
};

struct _ocfs2_bitmap {
	ocfs2_filesys *b_fs;
	uint64_t b_set_bits;
	uint64_t b_total_bits;
	char *b_description;
	struct ocfs2_bitmap_operations *b_ops;
	struct rb_root b_regions;
	void *b_private;
};


errcode_t ocfs2_bitmap_new(ocfs2_filesys *fs,
			   uint64_t total_bits,
			   const char *description,
			   struct ocfs2_bitmap_operations *ops,
			   void *private_data,
			   ocfs2_bitmap **ret_bitmap);
errcode_t ocfs2_bitmap_alloc_region(ocfs2_bitmap *bitmap,
				    uint64_t start_bit,
				    int bitmap_start,
				    int total_bits,
				    struct ocfs2_bitmap_region **ret_br);
void ocfs2_bitmap_free_region(struct ocfs2_bitmap_region *br);
errcode_t ocfs2_bitmap_realloc_region(ocfs2_bitmap *bitmap,
				      struct ocfs2_bitmap_region *br,
				      int total_bits);
errcode_t ocfs2_bitmap_insert_region(ocfs2_bitmap *bitmap,
				     struct ocfs2_bitmap_region *br);
typedef errcode_t (*ocfs2_bitmap_foreach_func)(struct ocfs2_bitmap_region *br,
					       void *private_data);
errcode_t ocfs2_bitmap_foreach_region(ocfs2_bitmap *bitmap,
				      ocfs2_bitmap_foreach_func func,
				      void *private_data);
errcode_t ocfs2_bitmap_set_generic(ocfs2_bitmap *bitmap,
				   uint64_t bitno, int *oldval);
errcode_t ocfs2_bitmap_clear_generic(ocfs2_bitmap *bitmap,
				     uint64_t bitno, int *oldval);
errcode_t ocfs2_bitmap_test_generic(ocfs2_bitmap *bitmap,
				    uint64_t bitno, int *val);
errcode_t ocfs2_bitmap_find_next_set_generic(ocfs2_bitmap *bitmap,
					     uint64_t start,
					     uint64_t *found);
errcode_t ocfs2_bitmap_find_next_clear_generic(ocfs2_bitmap *bitmap,
					       uint64_t start,
					       uint64_t *found);
errcode_t ocfs2_bitmap_alloc_range_generic(ocfs2_bitmap *bitmap,
					   uint64_t min_len,
					   uint64_t len,
					   uint64_t *first_bit,
					   uint64_t *bits_found);
errcode_t ocfs2_bitmap_clear_range_generic(ocfs2_bitmap *bitmap,
					   uint64_t len,
					   uint64_t first_bit);
errcode_t ocfs2_bitmap_set_holes(ocfs2_bitmap *bitmap,
				 uint64_t bitno, int *oldval);
errcode_t ocfs2_bitmap_clear_holes(ocfs2_bitmap *bitmap,
				   uint64_t bitno, int *oldval);
errcode_t ocfs2_bitmap_test_holes(ocfs2_bitmap *bitmap,
				  uint64_t bitno, int *val);
errcode_t ocfs2_bitmap_find_next_set_holes(ocfs2_bitmap *bitmap,
					   uint64_t start,
					   uint64_t *found);
errcode_t ocfs2_bitmap_find_next_clear_holes(ocfs2_bitmap *bitmap,
					     uint64_t start,
					     uint64_t *found);
#endif  /* _BITMAP_H */
