/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * bitmap.c
 *
 * Basic bitmap routines for the OCFS2 userspace library.
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
 */

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE

#include <string.h>
#include <limits.h>
#include <inttypes.h>

#include "ocfs2.h"

#include "bitops.h"
#include "bitmap.h"
#include "kernel-rbtree.h"


/* The public API */

void ocfs2_bitmap_free(ocfs2_bitmap *bitmap)
{
	struct rb_node *node;
	struct ocfs2_bitmap_region *br;

	/*
	 * If the bitmap needs to do extra cleanup of region,
	 * it should have done it in destroy_notify.  Same with the
	 * private pointers.
	 */
	if (bitmap->b_ops->destroy_notify)
		(*bitmap->b_ops->destroy_notify)(bitmap);

	while ((node = rb_first(&bitmap->b_regions)) != NULL) {
		br = rb_entry(node, struct ocfs2_bitmap_region, br_node);

		rb_erase(&br->br_node, &bitmap->b_regions);
		ocfs2_bitmap_free_region(br);
	}

	ocfs2_free(&bitmap->b_description);
	ocfs2_free(&bitmap);
}

errcode_t ocfs2_bitmap_set(ocfs2_bitmap *bitmap, uint64_t bitno,
			   int *oldval)
{
	errcode_t ret;
	int old_tmp;

	if (bitno >= bitmap->b_total_bits)
		return OCFS2_ET_INVALID_BIT;

	ret = (*bitmap->b_ops->set_bit)(bitmap, bitno, &old_tmp);
	if (ret)
		return ret;

	if (!old_tmp)
		bitmap->b_set_bits++;
	if (oldval)
		*oldval = old_tmp;

	return 0;
}

errcode_t ocfs2_bitmap_clear(ocfs2_bitmap *bitmap, uint64_t bitno,
			     int *oldval)
{
	errcode_t ret;
	int old_tmp;

	if (bitno >= bitmap->b_total_bits)
		return OCFS2_ET_INVALID_BIT;

	ret = (*bitmap->b_ops->clear_bit)(bitmap, bitno, &old_tmp);
	if (ret)
		return ret;

	if (old_tmp)
		bitmap->b_set_bits--;
	if (oldval)
		*oldval = old_tmp;

	return 0;
}

errcode_t ocfs2_bitmap_test(ocfs2_bitmap *bitmap, uint64_t bitno,
			    int *val)
{
	errcode_t ret;

	if (bitno >= bitmap->b_total_bits)
		return OCFS2_ET_INVALID_BIT;

	ret = (*bitmap->b_ops->test_bit)(bitmap, bitno, val);

	return ret;
}

errcode_t ocfs2_bitmap_find_next_set(ocfs2_bitmap *bitmap,
				     uint64_t start, 
				     uint64_t *found)
{
	if (start >= bitmap->b_total_bits)
		return OCFS2_ET_INVALID_BIT;

	return (*bitmap->b_ops->find_next_set)(bitmap, start, found);
}

errcode_t ocfs2_bitmap_find_next_clear(ocfs2_bitmap *bitmap,
				       uint64_t start, 
				       uint64_t *found)
{
	if (start >= bitmap->b_total_bits)
		return OCFS2_ET_INVALID_BIT;

	return (*bitmap->b_ops->find_next_clear)(bitmap, start, found);
}

errcode_t ocfs2_bitmap_read(ocfs2_bitmap *bitmap)
{
	if (!bitmap->b_ops->read_bitmap)
		return OCFS2_ET_INVALID_ARGUMENT;

	/* FIXME: Some sane error, or handle in ->read_bitmap() */
	if (rb_first(&bitmap->b_regions))
		return OCFS2_ET_INVALID_BIT;

	return (*bitmap->b_ops->read_bitmap)(bitmap);
}

errcode_t ocfs2_bitmap_write(ocfs2_bitmap *bitmap)
{
	if (!bitmap->b_ops->write_bitmap)
		return OCFS2_ET_INVALID_ARGUMENT;

	return (*bitmap->b_ops->write_bitmap)(bitmap);
}

uint64_t ocfs2_bitmap_get_set_bits(ocfs2_bitmap *bitmap)
{
	return bitmap->b_set_bits;
}

/*
 * The remaining functions are private to the library.
 */

/*
 * This function is private to the library.  Bitmap subtypes will
 * use this to allocate their structure, but their b_ops will
 * determine how they work.
 */
errcode_t ocfs2_bitmap_new(ocfs2_filesys *fs,
			   uint64_t total_bits,
			   const char *description,
			   struct ocfs2_bitmap_operations *ops,
			   void *private_data,
			   ocfs2_bitmap **ret_bitmap)
{
	errcode_t ret;
	ocfs2_bitmap *bitmap;

	if (!ops->set_bit || !ops->clear_bit || !ops->test_bit)
		return OCFS2_ET_INVALID_ARGUMENT;

	ret = ocfs2_malloc0(sizeof(struct _ocfs2_bitmap), &bitmap);
	if (ret)
		return ret;

	bitmap->b_fs = fs;
	bitmap->b_total_bits = total_bits;
	bitmap->b_ops = ops;
	bitmap->b_regions = RB_ROOT;
	bitmap->b_private = private_data;
	if (description) {
		ret = ocfs2_malloc0(sizeof(char) *
				    (strlen(description) + 1),
				    &bitmap->b_description);
		if (ret)
			goto out_free;

		strcpy(bitmap->b_description, description);
	}

	*ret_bitmap = bitmap;
	return 0;

out_free:
	ocfs2_free(&bitmap);

	return ret;
}

static size_t ocfs2_align_total(int total_bits)
{
	return (total_bits + 7) / 8;
}

errcode_t ocfs2_bitmap_alloc_region(ocfs2_bitmap *bitmap,
				    uint64_t start_bit,
				    int total_bits,
				    struct ocfs2_bitmap_region **ret_br)
{
	errcode_t ret;
	struct ocfs2_bitmap_region *br;

	if (total_bits < 0)
		return OCFS2_ET_INVALID_BIT;

	ret = ocfs2_malloc0(sizeof(struct ocfs2_bitmap_region), &br);
	if (ret)
		return ret;


	br->br_bytes = ocfs2_align_total(total_bits);
	br->br_start_bit = start_bit;
	br->br_total_bits = total_bits;

	ret = ocfs2_malloc0(br->br_bytes, &br->br_bitmap);
	if (ret)
		ocfs2_free(&br);
	else
		*ret_br = br;

	return ret;
}

void ocfs2_bitmap_free_region(struct ocfs2_bitmap_region *br)
{
	if (br->br_bitmap)
		ocfs2_free(&br->br_bitmap);
	ocfs2_free(&br);
}

errcode_t ocfs2_bitmap_realloc_region(ocfs2_bitmap *bitmap,
				      struct ocfs2_bitmap_region *br,
				      int total_bits)
{
	errcode_t ret;
	size_t new_bytes;

	if ((br->br_start_bit + total_bits) > bitmap->b_total_bits)
		return OCFS2_ET_INVALID_BIT;

	new_bytes = ocfs2_align_total(total_bits);

	if (new_bytes > br->br_bytes) {
		ret = ocfs2_realloc0(new_bytes, &br->br_bitmap, br->br_bytes);
		if (ret)
			return ret;
		br->br_bytes = new_bytes;
	}
	br->br_total_bits = total_bits;

	return 0;
}

errcode_t ocfs2_bitmap_foreach_region(ocfs2_bitmap *bitmap,
				      ocfs2_bitmap_foreach_func func,
				      void *private_data)
{
	struct ocfs2_bitmap_region *br;
	struct rb_node *node;
	errcode_t ret = 0;

	for (node = rb_first(&bitmap->b_regions); node; node = rb_next(node)) {
		br = rb_entry(node, struct ocfs2_bitmap_region, br_node);

		ret = func(br, private_data);
		if (ret)
			break;
	}

	return ret;
}

/*
 * Attempt to merge two regions.  If the merge is successful, 0 will
 * be returned and prev will be the only valid region.  Next will
 * be freed.
 */
static errcode_t ocfs2_bitmap_merge_region(ocfs2_bitmap *bitmap,
					   struct ocfs2_bitmap_region *prev,
					   struct ocfs2_bitmap_region *next)
{
	errcode_t ret;
	uint64_t new_bits;
	size_t prev_bytes;
	uint8_t *pbm, *nbm, offset, diff;

	if ((prev->br_start_bit + prev->br_total_bits) !=
	    next->br_start_bit)
		return OCFS2_ET_INVALID_BIT;

	if (bitmap->b_ops->merge_region &&
	    !(*bitmap->b_ops->merge_region)(bitmap, prev, next))
		return OCFS2_ET_INVALID_BIT;

	new_bits = (uint64_t)(prev->br_total_bits) +
		(uint64_t)(next->br_total_bits);
	if (new_bits > INT_MAX)
		return OCFS2_ET_INVALID_BIT;

	/* grab before realloc changes them */
	prev_bytes = prev->br_bytes;
	offset = prev->br_total_bits % 8;

	ret = ocfs2_bitmap_realloc_region(bitmap, prev, (int)new_bits);
	if (ret)
		return ret;

	/* if prev's last bit ends on a byte boundary then we can just
	 * copy everything over */
	if (offset == 0) {
		memcpy(prev->br_bitmap + prev_bytes, next->br_bitmap, 
				next->br_bytes);
		goto done;
	}

	/* otherwise we have to shift next in.  we're about to free
	 * next, so we consume it as we go. */
	pbm = &prev->br_bitmap[prev_bytes - 1];
	nbm = &next->br_bitmap[0];
	diff = 8 - offset;
	while(next->br_bytes-- && next->br_total_bits > 0) {
		/* mask off just the offset bytes in the prev */
		*pbm &= ((1 << offset) - 1);
		/* move 'diff' LSB from next into prevs MSB */
		*pbm |= (*nbm & ((1 << diff) - 1)) << offset;
		pbm++;
		next->br_total_bits -= diff;

		if (next->br_total_bits > 0) {
			/* then set prev's LSB to the next offset MSB.  this relies
			 * on 0s being shifted into the MSB */
			*pbm = *nbm >> diff;
			nbm++;
			next->br_total_bits -= offset;
		}
	}

done:
	prev->br_set_bits = prev->br_set_bits + next->br_set_bits;
	rb_erase(&next->br_node, &bitmap->b_regions);
	ocfs2_bitmap_free_region(next);

	return 0;
}

/* 
 * Find a bitmap_region in the tree that intersects the bit region
 * that is passed in.  
 *
 * _p and _parent are set so that callers can use rb_link_node and 
 * rb_insert_color to insert a node after finding that their bit
 * wasn't found.
 *
 * _next is only used if a bitmap_region isn't found.  it is set
 * to the next node in the tree greater than the bitmap range
 * that was searched.
 */
static
struct ocfs2_bitmap_region *ocfs2_bitmap_lookup(ocfs2_bitmap *bitmap, 
						uint64_t bitno, 
						uint64_t total_bits, 
						struct rb_node ***ret_p,
						struct rb_node **ret_parent,
						struct rb_node **ret_next)
{
	struct rb_node **p = &bitmap->b_regions.rb_node;
	struct rb_node *parent = NULL, *last_left = NULL;
	struct ocfs2_bitmap_region *br = NULL;

	while (*p)
	{
		parent = *p;
		br = rb_entry(parent, struct ocfs2_bitmap_region, br_node);

		if (bitno + total_bits <= br->br_start_bit) {
			p = &(*p)->rb_left;
			last_left = *p;
			br = NULL;
		} else if (bitno >= (br->br_start_bit + br->br_total_bits)) {
			p = &(*p)->rb_right;
			br = NULL;
		} else
			break;
	}
	if (ret_p != NULL)
		*ret_p = p;
	if (ret_parent != NULL)
		*ret_parent = parent;
	if (br == NULL && ret_next != NULL)
		*ret_next = last_left;
	return br;
}

errcode_t ocfs2_bitmap_insert_region(ocfs2_bitmap *bitmap,
				     struct ocfs2_bitmap_region *br)
{
	struct ocfs2_bitmap_region *br_tmp;
	struct rb_node **p, *parent, *node;

	if (br->br_start_bit > bitmap->b_total_bits)
		return OCFS2_ET_INVALID_BIT;

	/* we shouldn't find an existing region that intersects our new one */
	br_tmp = ocfs2_bitmap_lookup(bitmap, br->br_start_bit, 
				     br->br_total_bits, &p, &parent, NULL);
	if (br_tmp)
		return OCFS2_ET_INVALID_BIT;

	rb_link_node(&br->br_node, parent, p);
	rb_insert_color(&br->br_node, &bitmap->b_regions);

	/* try to merge our new extent with its neighbours in the tree */

	node = rb_prev(&br->br_node);
	if (node) {
		br_tmp = rb_entry(node, struct ocfs2_bitmap_region, br_node);
		ocfs2_bitmap_merge_region(bitmap, br_tmp, br);
		br = br_tmp;
	}

	node = rb_next(&br->br_node);
	if (node != NULL) {
		br_tmp = rb_entry(node, struct ocfs2_bitmap_region, br_node);
		ocfs2_bitmap_merge_region(bitmap, br, br_tmp);
	}

	return 0;
}


/*
 * Helper functions for the most generic of bitmaps.  If there is no
 * memory allocated for the bit, it fails.
 */
errcode_t ocfs2_bitmap_set_generic(ocfs2_bitmap *bitmap, uint64_t bitno,
				   int *oldval)
{
	struct ocfs2_bitmap_region *br;
	int old_tmp;
	
	br = ocfs2_bitmap_lookup(bitmap, bitno, 1, NULL, NULL, NULL);
	if (!br)
		return OCFS2_ET_INVALID_BIT;

	old_tmp = ocfs2_set_bit(bitno - br->br_start_bit,
				br->br_bitmap);
	if (oldval)
		*oldval = old_tmp;

	if (!old_tmp) {
		br->br_set_bits++;
		if (bitmap->b_ops->bit_change_notify)
			(*bitmap->b_ops->bit_change_notify)(bitmap, br, bitno,
							    1);
	}

	return 0;
}

errcode_t ocfs2_bitmap_clear_generic(ocfs2_bitmap *bitmap,
				     uint64_t bitno, int *oldval)
{
	struct ocfs2_bitmap_region *br;
	int old_tmp;
	
	br = ocfs2_bitmap_lookup(bitmap, bitno, 1, NULL, NULL, NULL);
	if (!br)
		return OCFS2_ET_INVALID_BIT;

	old_tmp = ocfs2_clear_bit(bitno - br->br_start_bit,
				  br->br_bitmap);
	if (oldval)
		*oldval = old_tmp;

	if (old_tmp) {
		br->br_set_bits--;
		if (bitmap->b_ops->bit_change_notify)
			(*bitmap->b_ops->bit_change_notify)(bitmap, br, bitno,
							    0);
	}

	return 0;
}

errcode_t ocfs2_bitmap_test_generic(ocfs2_bitmap *bitmap,
				    uint64_t bitno, int *val)
{
	struct ocfs2_bitmap_region *br;
	
	br = ocfs2_bitmap_lookup(bitmap, bitno, 1, NULL, NULL, NULL);
	if (!br)
		return OCFS2_ET_INVALID_BIT;

	*val = ocfs2_test_bit(bitno - br->br_start_bit,
			      br->br_bitmap) ? 1 : 0;
	return 0;
}

errcode_t ocfs2_bitmap_find_next_set_generic(ocfs2_bitmap *bitmap,
					     uint64_t start,
					     uint64_t *found)
{
	struct ocfs2_bitmap_region *br;
	struct rb_node *node = NULL;
	int offset, ret;

	/* start from either the node whose's br contains the bit or 
	 * the next greatest node in the tree */
	br = ocfs2_bitmap_lookup(bitmap, start, 1, NULL, NULL, &node);
	if (br)
		node = &br->br_node;

	for (; node != NULL; node = rb_next(node)) {
		br = rb_entry(node, struct ocfs2_bitmap_region, br_node);

		if (start > br->br_start_bit)
			offset = start - br->br_start_bit;
		else
			offset = 0;

		ret = ocfs2_find_next_bit_set(br->br_bitmap,
					      br->br_total_bits,
					      offset);
		if (ret != br->br_total_bits) {
			*found = br->br_start_bit + ret;
			return 0;
		}
	}

	return OCFS2_ET_BIT_NOT_FOUND;
}

errcode_t ocfs2_bitmap_find_next_clear_generic(ocfs2_bitmap *bitmap,
					       uint64_t start,
					       uint64_t *found)
{
	struct ocfs2_bitmap_region *br;
	struct rb_node *node = NULL;
	int offset, ret;

	/* start from either the node whose's br contains the bit or 
	 * the next greatest node in the tree */
	br = ocfs2_bitmap_lookup(bitmap, start, 1, NULL, NULL, &node);
	if (br)
		node = &br->br_node;

	for (; node != NULL; node = rb_next(node)) {
		br = rb_entry(node, struct ocfs2_bitmap_region, br_node);

		if (start > br->br_start_bit)
			offset = start - br->br_start_bit;
		else
			offset = 0;

		ret = ocfs2_find_next_bit_clear(br->br_bitmap,
						br->br_total_bits,
						offset);
		if (ret != br->br_total_bits) {
			*found = br->br_start_bit + ret;
			return 0;
		}
	}

	return OCFS2_ET_BIT_NOT_FOUND;
}


/*
 * Helper functions for a bitmap with holes in it.
 * If a bit doesn't have memory allocated for it, we allocate.
 */
errcode_t ocfs2_bitmap_set_holes(ocfs2_bitmap *bitmap,
				 uint64_t bitno, int *oldval)
{
	errcode_t ret;
	struct ocfs2_bitmap_region *br;

	if (!ocfs2_bitmap_set_generic(bitmap, bitno, oldval))
		return 0;

	ret = ocfs2_bitmap_alloc_region(bitmap, bitno, 1, &br);
	if (ret)
		return ret;

	ret = ocfs2_bitmap_insert_region(bitmap, br);
	if (ret)
		return ret;

	return ocfs2_bitmap_set_generic(bitmap, bitno, oldval);
}

errcode_t ocfs2_bitmap_clear_holes(ocfs2_bitmap *bitmap,
				   uint64_t bitno, int *oldval)
{
	errcode_t ret;
	struct ocfs2_bitmap_region *br;

	if (!ocfs2_bitmap_clear_generic(bitmap, bitno, oldval))
		return 0;

	ret = ocfs2_bitmap_alloc_region(bitmap, bitno, 1, &br);
	if (ret)
		return ret;

	ret = ocfs2_bitmap_insert_region(bitmap, br);

	return ret;
}

errcode_t ocfs2_bitmap_test_holes(ocfs2_bitmap *bitmap,
				  uint64_t bitno, int *val)
{
	if (ocfs2_bitmap_test_generic(bitmap, bitno, val))
		*val = 0;

	return 0;
}

errcode_t ocfs2_bitmap_find_next_set_holes(ocfs2_bitmap *bitmap, 
					   uint64_t start,
					   uint64_t *found)
{
	return ocfs2_bitmap_find_next_set_generic(bitmap, start, found);
}

errcode_t ocfs2_bitmap_find_next_clear_holes(ocfs2_bitmap *bitmap,
					     uint64_t start,
					     uint64_t *found)
{
	struct ocfs2_bitmap_region *br;
	struct rb_node *node = NULL;
	uint64_t seen;
	int offset, ret;

	/* start from either the node whose's br contains the bit or 
	 * the next greatest node in the tree */
	br = ocfs2_bitmap_lookup(bitmap, start, 1, NULL, NULL, &node);
	if (br)
		node = &br->br_node;
	else if (!node) {
		/* There was nothing past start */
		*found = start;
		return 0;
	}

	seen = start;
	for (; node != NULL; node = rb_next(node)) {
		br = rb_entry(node, struct ocfs2_bitmap_region, br_node);

		/* Did we find a hole? */
		if (seen < br->br_start_bit) {
			*found = seen;
			return 0;
		}

		if (start > br->br_start_bit)
			offset = start - br->br_start_bit;
		else
			offset = 0;

		ret = ocfs2_find_next_bit_clear(br->br_bitmap,
						br->br_total_bits,
						offset);
		if (ret != br->br_total_bits) {
			*found = br->br_start_bit + ret;
			return 0;
		}
		seen = br->br_start_bit + br->br_total_bits;
	}

	return OCFS2_ET_BIT_NOT_FOUND;
}

static struct ocfs2_bitmap_operations global_cluster_ops = {
	.set_bit		= ocfs2_bitmap_set_generic,
	.clear_bit		= ocfs2_bitmap_clear_generic,
	.test_bit		= ocfs2_bitmap_test_generic,
	.find_next_set		= ocfs2_bitmap_find_next_set_generic,
	.find_next_clear	= ocfs2_bitmap_find_next_clear_generic,
};

errcode_t ocfs2_cluster_bitmap_new(ocfs2_filesys *fs,
				   const char *description,
				   ocfs2_bitmap **ret_bitmap)
{
	errcode_t ret;
	ocfs2_bitmap *bitmap;
	uint64_t max_bits, num_bits, bitoff, alloc_bits;
	struct ocfs2_bitmap_region *br;

	num_bits = fs->fs_clusters;
	ret = ocfs2_bitmap_new(fs,
			       num_bits,
			       description ? description :
			       "Generic cluster bitmap",
			       &global_cluster_ops,
			       NULL,
			       &bitmap);
	if (ret)
		return ret;

	bitoff = 0;
	max_bits = INT_MAX - (fs->fs_clustersize - 1);
	while (bitoff < num_bits) {
		alloc_bits = num_bits;
		if (num_bits > max_bits)
			alloc_bits = max_bits;
		ret = ocfs2_bitmap_alloc_region(bitmap, bitoff,
						alloc_bits, &br);
		if (ret) {
			ocfs2_bitmap_free(bitmap);
			return ret;
		}

		ret = ocfs2_bitmap_insert_region(bitmap, br);
		if (ret) {
			ocfs2_bitmap_free_region(br);
			ocfs2_bitmap_free(bitmap);
			return ret;
		}

		bitoff += alloc_bits;
	}

	*ret_bitmap = bitmap;

	return 0;
}


static struct ocfs2_bitmap_operations global_block_ops = {
	.set_bit		= ocfs2_bitmap_set_holes,
	.clear_bit		= ocfs2_bitmap_clear_holes,
	.test_bit		= ocfs2_bitmap_test_holes,
	.find_next_set		= ocfs2_bitmap_find_next_set_holes,
	.find_next_clear	= ocfs2_bitmap_find_next_clear_holes,
};

errcode_t ocfs2_block_bitmap_new(ocfs2_filesys *fs,
				 const char *description,
				 ocfs2_bitmap **ret_bitmap)
{
	errcode_t ret;
	ocfs2_bitmap *bitmap;

	ret = ocfs2_bitmap_new(fs,
			       fs->fs_blocks,
			       description ? description :
			       "Generic block bitmap",
			       &global_block_ops,
			       NULL,
			       &bitmap);
	if (ret)
		return ret;

	*ret_bitmap = bitmap;

	return 0;
}


#ifdef DEBUG_EXE
#include <stdlib.h>
#include <getopt.h>
#include <limits.h>

static uint64_t read_number(const char *num)
{
	uint64_t val;
	char *ptr;

	val = strtoull(num, &ptr, 0);
	if (!ptr || *ptr)
		return 0;

	return val;
}

static void print_usage(void)
{
	fprintf(stderr,
		"debug_bitmap [-a] <filename>\n");
}

extern int opterr, optind;
extern char *optarg;

static void dump_regions(ocfs2_bitmap *bitmap)
{
	struct ocfs2_bitmap_region *br;
	struct rb_node *node;

	fprintf(stdout, "Bitmap \"%s\": total = %"PRIu64", set = %"PRIu64"\n",
		bitmap->b_description, bitmap->b_total_bits,
		bitmap->b_set_bits);

	for (node = rb_first(&bitmap->b_regions);node; node = rb_next(node)) {
		br = rb_entry(node, struct ocfs2_bitmap_region, br_node);

		fprintf(stdout,
			"(start: %"PRIu64", n: %d, set: %d)\n",
			br->br_start_bit, br->br_total_bits,
			br->br_set_bits);
	}
}

static void print_bitmap(ocfs2_bitmap *bitmap)
{
	uint64_t bitno;
	uint64_t gap_start = 0;  /* GCC is dumb */
	errcode_t ret;
	int val, gap;

	gap = 0;
	for (bitno = 0; bitno < bitmap->b_total_bits; bitno++) {
		ret = ocfs2_bitmap_test(bitmap, bitno, &val);
		if (ret) {
			if (ret == OCFS2_ET_INVALID_BIT) {
				if (!gap) {
					gap = 1;
					gap_start = bitno;
				}
				continue;
			}
			com_err("print_bitmap", ret,
				"while testing bit %"PRIu64"\n", bitno);
			break;
		}
		if (gap) {
			fprintf(stdout,
				"\nGap of length %"PRIu64" at %"PRIu64"\n",
				bitno - gap_start, gap_start);
			gap = bitno % 72;
			gap += gap / 8;
			for (; gap; gap--)
				fprintf(stdout, " ");
			fflush(stdout);
		} else {
			if (bitno && !(bitno % 72))
				fprintf(stdout, "\n");
			else if (bitno && !(bitno % 8))
				fprintf(stdout, " ");
		}
		fprintf(stdout, "%d", val);
		fflush(stdout);
	}

	if ((bitno - 1) % 72)
		fprintf(stdout, "\n");
}

static int try_op(ocfs2_bitmap *bitmap,
		  errcode_t (*func)(ocfs2_bitmap *bitmap,
				    uint64_t bitno,
				    int *val),
		  char *bit_val, int *ret_val)
{
	errcode_t ret;
	uint64_t bitno;
	char *ptr;

	if (!bit_val) {
		fprintf(stderr, "You must provide a bit offset\n");
		return 1;
	}

	bitno = read_number(bit_val);
	if (!bitno) {
		for (ptr = bit_val; *ptr; ptr++) {
			if (*ptr != '0')
				break;
		}
		if ((ptr == bit_val) || *ptr) {
			fprintf(stderr, "Invalid bit offset: %s\n",
				bit_val);
			return 1;
		}
	}

	ret = (*func)(bitmap, bitno, ret_val);
	if (ret) {
		com_err("try_op", ret, "while setting bit %"PRIu64"\n", bitno);
		return 1;
	}

	return 0;
}

static int try_op64(ocfs2_bitmap *bitmap,
		    errcode_t (*func)(ocfs2_bitmap *bitmap,
				      uint64_t bitno,
				      uint64_t *val),
		    char *bit_val, uint64_t *ret_val)
{
	errcode_t ret;
	uint64_t bitno;
	char *ptr;

	if (!bit_val) {
		fprintf(stderr, "You must provide a bit offset\n");
		return 1;
	}

	bitno = read_number(bit_val);
	if (!bitno) {
		for (ptr = bit_val; *ptr; ptr++) {
			if (*ptr != '0')
				break;
		}
		if ((ptr == bit_val) || *ptr) {
			fprintf(stderr, "Invalid bit offset: %s\n",
				bit_val);
			return 1;
		}
	}

	ret = (*func)(bitmap, bitno, ret_val);
	if (ret) {
		com_err("try_op64", ret, "while setting bit %"PRIu64"\n", bitno);
		return 1;
	}

	return 0;
}


static void run_test(ocfs2_bitmap *bitmap)
{
	char buf[256];
	char *ptr, *cmd;
	uint64_t val64;
	int val;

	while (1) {
		fprintf(stdout, "Command: ");
		fflush(stdout);

		if (!fgets(buf, sizeof(buf), stdin))
			break;

		ptr = buf + strlen(buf) - 1;
		if (*ptr == '\n')
			*ptr = '\0';

		for (cmd = buf; (*cmd == ' ') || (*cmd == '\t'); cmd++);

		if (!(*cmd))
			continue;

		ptr = strchr(cmd, ' ');
		if (ptr) {
			*ptr = '\0';
			ptr++;
		}

		if (!strcmp(cmd, "set")) {
			try_op(bitmap, ocfs2_bitmap_set, ptr, NULL);
		} else if (!strcmp(cmd, "clear")) {
			try_op(bitmap, ocfs2_bitmap_clear, ptr, NULL);
		} else if (!strcmp(cmd, "test")) {
			if (!try_op(bitmap, ocfs2_bitmap_test, ptr,
				    &val)) {
				fprintf(stdout, "Bit %s is %s\n",
					ptr, val ? "set" : "clear");
			}
		} else if (!strcmp(cmd, "fns")) {
			if (!try_op64(bitmap,
				      ocfs2_bitmap_find_next_set,
				      ptr, &val64)) {
				fprintf(stdout, "Found %"PRIu64"\n",
					val64);
			}
		} else if (!strcmp(cmd, "fnc")) {
			if (!try_op64(bitmap,
				      ocfs2_bitmap_find_next_clear,
				      ptr, &val64)) {
				fprintf(stdout, "Found %"PRIu64"\n",
					val64);
			}
		} else if (!strcmp(cmd, "print")) {
			print_bitmap(bitmap);
		} else if (!strcmp(cmd, "dump")) {
			dump_regions(bitmap);
		} else if (!strcmp(cmd, "quit")) {
			break;
		} else {
			fprintf(stderr, "Invalid command: \"%s\"\n",
				cmd);
		}
	}
}

int main(int argc, char *argv[])
{
	errcode_t ret;
	int c;
	int alloc = 0;
	char *filename;
	ocfs2_filesys *fs;
	ocfs2_bitmap *bitmap;

	initialize_ocfs_error_table();

	while ((c = getopt(argc, argv, "a")) != EOF) {
		switch (c) {
			case 'a':
				alloc = 1;
				break;

			default:
				print_usage();
				return 1;
				break;
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "Missing filename\n");
		print_usage();
		return 1;
	}
	filename = argv[optind];

	ret = ocfs2_open(filename, OCFS2_FLAG_RO, 0, 0, &fs);
	if (ret) {
		com_err(argv[0], ret,
			"while opening file \"%s\"", filename);
		return 1;
	}

	if (alloc)
		ret = ocfs2_block_bitmap_new(fs, "Testing", &bitmap);
	else
		ret = ocfs2_cluster_bitmap_new(fs, "Testing", &bitmap);
	if (ret) {
		com_err(argv[0], ret,
			"while creating bitmap");
		goto out_close;
	}

	run_test(bitmap);

	ocfs2_bitmap_free(bitmap);

out_close:
	ocfs2_close(fs);

	return ret;
}



#endif  /* DEBUG_EXE */
