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
 *
 * Authors: Joel Becker
 */

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE

#include <string.h>
#include <limits.h>
#include <inttypes.h>

#include <asm/bitops.h>

#include "ocfs2.h"

#include "bitmap.h"
#include "kernel-rbtree.h"


/* The public API */

void ocfs2_bitmap_free(ocfs2_bitmap *bitmap)
{
	struct rb_node *node;
	struct ocfs2_bitmap_cluster *bc;

	if (bitmap->b_ops->destroy_notify)
		(*bitmap->b_ops->destroy_notify)(bitmap);

	/*
	 * If the bitmap needs to do extra cleanup of clusters,
	 * it should have done it in destroy_notify
	 */
	while ((node = rb_first(&bitmap->b_clusters)) != NULL) {
		bc = rb_entry(node, struct ocfs2_bitmap_cluster, bc_node);

		rb_erase(&bc->bc_node, &bitmap->b_clusters);
		ocfs2_bitmap_free_cluster(bc);
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
	bitmap->b_clusters = RB_ROOT;
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

static uint64_t ocfs2_bits_to_clusters(ocfs2_bitmap *bitmap,
				       int num_bits)
{
	uint64_t bpos;
	int bpc;
	uint32_t cpos;

	bpc = bitmap->b_fs->fs_clustersize * 8;
	cpos = ((unsigned int)num_bits + (bpc - 1)) / bpc;
	bpos = (uint64_t)cpos * bpc;

	return bpos;
}

errcode_t ocfs2_bitmap_alloc_cluster(ocfs2_bitmap *bitmap,
				     uint64_t start_bit,
				     int total_bits,
				     struct ocfs2_bitmap_cluster **ret_bc)
{
	errcode_t ret;
	struct ocfs2_bitmap_cluster *bc;
	uint64_t real_bits;

	if (total_bits < 0)
		return OCFS2_ET_INVALID_BIT;

	real_bits = ocfs2_bits_to_clusters(bitmap, total_bits);
	if (real_bits > INT_MAX)
		return OCFS2_ET_INVALID_BIT;

	ret = ocfs2_malloc0(sizeof(struct ocfs2_bitmap_cluster), &bc);
	if (ret)
		return ret;

	start_bit &= ~((bitmap->b_fs->fs_clustersize * 8) - 1);
	bc->bc_start_bit = ocfs2_bits_to_clusters(bitmap, start_bit);
	bc->bc_total_bits = real_bits;

	ret = ocfs2_malloc0((size_t)real_bits / 8, &bc->bc_bitmap);
	if (ret)
		ocfs2_free(&bc);
	else
		*ret_bc = bc;

	return ret;
}

void ocfs2_bitmap_free_cluster(struct ocfs2_bitmap_cluster *bc)
{
	if (bc->bc_bitmap)
		ocfs2_free(&bc->bc_bitmap);
	ocfs2_free(&bc);
}

errcode_t ocfs2_bitmap_realloc_cluster(ocfs2_bitmap *bitmap,
				       struct ocfs2_bitmap_cluster *bc,
				       int total_bits)
{
	errcode_t ret;
	uint64_t real_bits;

	if ((bc->bc_start_bit + total_bits) > bitmap->b_total_bits)
		return OCFS2_ET_INVALID_BIT;

	real_bits = ocfs2_bits_to_clusters(bitmap, total_bits);
	if (real_bits > INT_MAX)
		return OCFS2_ET_INVALID_BIT;

	if (real_bits > bc->bc_total_bits) {
		ret = ocfs2_realloc0((size_t)real_bits / 8,
				     &bc->bc_bitmap,
				     bc->bc_total_bits / 8);
		if (ret)
			return ret;
		bc->bc_total_bits = (int)real_bits;
	}

	return 0;
}

/*
 * Attempt to merge two clusters.  If the merge is successful, 0 will
 * be returned and prev will be the only valid cluster.  Next will
 * be freed.
 */
static errcode_t ocfs2_bitmap_merge_cluster(ocfs2_bitmap *bitmap,
					    struct ocfs2_bitmap_cluster *prev,
					    struct ocfs2_bitmap_cluster *next)
{
	errcode_t ret;
	uint64_t new_bits;
	int prev_bits;

	if ((prev->bc_start_bit + prev->bc_total_bits) !=
	    next->bc_start_bit)
		return OCFS2_ET_INVALID_BIT;

	/*
	 * If at least one cpos is not zero, then these have real disk
	 * locations, and they better be cpos contig as well.
	 */
	if ((prev->bc_cpos || next->bc_cpos) &&
	    ((prev->bc_cpos +
	     ((prev->bc_total_bits / 8) /
	      bitmap->b_fs->fs_clusters)) != next->bc_cpos))
		return OCFS2_ET_INVALID_BIT;

	new_bits = (uint64_t)(prev->bc_total_bits) +
		(uint64_t)(next->bc_total_bits);
	if (new_bits > INT_MAX)
		return OCFS2_ET_INVALID_BIT;

	prev_bits = prev->bc_total_bits;
	ret = ocfs2_bitmap_realloc_cluster(bitmap, prev, new_bits);
	if (ret)
		return ret;

	memcpy(prev->bc_bitmap + (prev_bits / 8), next->bc_bitmap,
	       next->bc_total_bits / 8);

	rb_erase(&next->bc_node, &bitmap->b_clusters);
	ocfs2_bitmap_free_cluster(next);

	return 0;
}

/* Find a bitmap cluster in the tree that intersects the bit region
 * that is passed in.  The rb_node garbage lets insertion share this
 * searching code, most trivial callers will pass in NULLs. */
static
struct ocfs2_bitmap_cluster *ocfs2_bitmap_lookup(ocfs2_bitmap *bitmap, 
						 uint64_t bitno, 
						 uint64_t total_bits, 
						 struct rb_node ***ret_p,
						 struct rb_node **ret_parent)
{
	struct rb_node **p = &bitmap->b_clusters.rb_node;
	struct rb_node *parent = NULL;
	struct ocfs2_bitmap_cluster *bc = NULL;

	while (*p)
	{
		parent = *p;
		bc = rb_entry(parent, struct ocfs2_bitmap_cluster, bc_node);

		if (bitno + total_bits <= bc->bc_start_bit) {
			p = &(*p)->rb_left;
			bc = NULL;
		} else if (bitno >= (bc->bc_start_bit + bc->bc_total_bits)) {
			p = &(*p)->rb_right;
			bc = NULL;
		} else
			break;
	}
	if (ret_p != NULL)
		*ret_p = p;
	if (ret_parent != NULL)
		*ret_parent = parent;
	return bc;
}

errcode_t ocfs2_bitmap_insert_cluster(ocfs2_bitmap *bitmap,
				      struct ocfs2_bitmap_cluster *bc)
{
	struct ocfs2_bitmap_cluster *bc_tmp;
	struct rb_node **p, *parent, *node;

	if (bc->bc_start_bit > bitmap->b_total_bits)
		return OCFS2_ET_INVALID_BIT;

	/* we shouldn't find an existing cluster that intersects our new one */
	bc_tmp = ocfs2_bitmap_lookup(bitmap, bc->bc_start_bit, 
				     bc->bc_total_bits, &p, &parent);
	if (bc_tmp)
		return OCFS2_ET_INVALID_BIT;

	rb_link_node(&bc->bc_node, parent, p);
	rb_insert_color(&bc->bc_node, &bitmap->b_clusters);

	/* try to merge our new extent with its neighbours in the tree */

	node = rb_prev(&bc->bc_node);
	if (node) {
		bc_tmp = rb_entry(node, struct ocfs2_bitmap_cluster, bc_node);
		ocfs2_bitmap_merge_cluster(bitmap, bc_tmp, bc);
	}

	node = rb_next(&bc->bc_node);
	if (node != NULL) {
		bc_tmp = rb_entry(node, struct ocfs2_bitmap_cluster, bc_node);
		ocfs2_bitmap_merge_cluster(bitmap, bc, bc_tmp);
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
	struct ocfs2_bitmap_cluster *bc;
	int old_tmp;
	
	bc = ocfs2_bitmap_lookup(bitmap, bitno, 1, NULL, NULL);
	if (!bc)
		return OCFS2_ET_INVALID_BIT;

	old_tmp = __test_and_set_bit(bitno - bc->bc_start_bit,
				     (unsigned long *)(bc->bc_bitmap));
	if (oldval)
		*oldval = old_tmp;

	if (!old_tmp)
		bc->bc_set_bits++;

	return 0;
}

errcode_t ocfs2_bitmap_clear_generic(ocfs2_bitmap *bitmap,
				     uint64_t bitno, int *oldval)
{
	struct ocfs2_bitmap_cluster *bc;
	int old_tmp;
	
	bc = ocfs2_bitmap_lookup(bitmap, bitno, 1, NULL, NULL);
	if (!bc)
		return OCFS2_ET_INVALID_BIT;

	old_tmp = __test_and_clear_bit(bitno - bc->bc_start_bit,
				       (unsigned long *)bc->bc_bitmap);
	if (oldval)
		*oldval = old_tmp;

	if (old_tmp)
		bc->bc_set_bits--;

	return 0;
}

errcode_t ocfs2_bitmap_test_generic(ocfs2_bitmap *bitmap,
				    uint64_t bitno, int *val)
{
	struct ocfs2_bitmap_cluster *bc;
	
	bc = ocfs2_bitmap_lookup(bitmap, bitno, 1, NULL, NULL);
	if (!bc)
		return OCFS2_ET_INVALID_BIT;

	*val = test_bit(bitno - bc->bc_start_bit,
			(unsigned long *)bc->bc_bitmap) ? 1 : 0;
	return 0;
}


/*
 * Helper functions for a bitmap with holes in it.
 * If a bit doesn't have memory allocated for it, we allocate.
 */
errcode_t ocfs2_bitmap_set_holes(ocfs2_bitmap *bitmap,
				 uint64_t bitno, int *oldval)
{
	errcode_t ret;
	struct ocfs2_bitmap_cluster *bc;

	if (!ocfs2_bitmap_set_generic(bitmap, bitno, oldval))
		return 0;

	ret = ocfs2_bitmap_alloc_cluster(bitmap, bitno, 1, &bc);
	if (ret)
		return ret;

	ret = ocfs2_bitmap_insert_cluster(bitmap, bc);
	if (ret)
		return ret;

	return ocfs2_bitmap_set_generic(bitmap, bitno, oldval);
}

errcode_t ocfs2_bitmap_clear_holes(ocfs2_bitmap *bitmap,
				   uint64_t bitno, int *oldval)
{
	errcode_t ret;
	struct ocfs2_bitmap_cluster *bc;

	if (!ocfs2_bitmap_clear_generic(bitmap, bitno, oldval))
		return 0;

	ret = ocfs2_bitmap_alloc_cluster(bitmap, bitno, 1, &bc);
	if (ret)
		return ret;

	ret = ocfs2_bitmap_insert_cluster(bitmap, bc);

	return ret;
}

errcode_t ocfs2_bitmap_test_holes(ocfs2_bitmap *bitmap,
				  uint64_t bitno, int *val)
{
	if (ocfs2_bitmap_test_generic(bitmap, bitno, val))
		*val = 0;

	return 0;
}

static struct ocfs2_bitmap_operations global_cluster_ops = {
	.set_bit	= ocfs2_bitmap_set_generic,
	.clear_bit	= ocfs2_bitmap_clear_generic,
	.test_bit	= ocfs2_bitmap_test_generic
};

errcode_t ocfs2_cluster_bitmap_new(ocfs2_filesys *fs,
				   const char *description,
				   ocfs2_bitmap **ret_bitmap)
{
	errcode_t ret;
	ocfs2_bitmap *bitmap;
	uint64_t max_bits, num_bits, bitoff, alloc_bits;
	struct ocfs2_bitmap_cluster *bc;

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
		alloc_bits = ocfs2_bits_to_clusters(bitmap,
						    num_bits);
		if (num_bits > max_bits)
			alloc_bits = max_bits;

		ret = ocfs2_bitmap_alloc_cluster(bitmap, bitoff,
						 alloc_bits, &bc);
		if (ret) {
			ocfs2_bitmap_free(bitmap);
			return ret;
		}

		ret = ocfs2_bitmap_insert_cluster(bitmap, bc);
		if (ret) {
			ocfs2_bitmap_free_cluster(bc);
			ocfs2_bitmap_free(bitmap);
			return ret;
		}

		bitoff += alloc_bits;
	}

	*ret_bitmap = bitmap;

	return 0;
}


static struct ocfs2_bitmap_operations global_block_ops = {
	.set_bit	= ocfs2_bitmap_set_holes,
	.clear_bit	= ocfs2_bitmap_clear_holes,
	.test_bit	= ocfs2_bitmap_test_holes
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
	fprintf(stderr, "bitmap [-n <num_bits>] [-a] <filename>\n");
}

extern int opterr, optind;
extern char *optarg;

static void dump_clusters(ocfs2_bitmap *bitmap)
{
	struct ocfs2_bitmap_cluster *bc;
	struct rb_node *node;

	fprintf(stdout, "Bitmap \"%s\": total = %"PRIu64", set = %"PRIu64"\n",
		bitmap->b_description, bitmap->b_total_bits,
		bitmap->b_set_bits);

	for (node = rb_first(&bitmap->b_clusters);node; node = rb_next(node)) {
		bc = rb_entry(node, struct ocfs2_bitmap_cluster, bc_node);

		fprintf(stdout,
			"(start: %"PRIu64", n: %d, set: %d, cpos: %"PRIu32")\n",
			bc->bc_start_bit, bc->bc_total_bits,
			bc->bc_set_bits, bc->bc_cpos);
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


static void run_test(ocfs2_bitmap *bitmap)
{
	char buf[256];
	char *ptr, *cmd;
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
		} else if (!strcmp(cmd, "print")) {
			print_bitmap(bitmap);
		} else if (!strcmp(cmd, "dump")) {
			dump_clusters(bitmap);
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
