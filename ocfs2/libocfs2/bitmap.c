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

#include <linux/bitops.h>

#include "ocfs2.h"

#include "bitmap.h"


/* The public API */

void ocfs2_bitmap_free(ocfs2_bitmap *bitmap)
{
	struct list_head *pos, *next;
	struct ocfs2_bitmap_cluster *bc;

	if (bitmap->b_ops->destroy_notify)
		(*bitmap->b_ops->destroy_notify)(bitmap);

	/*
	 * If the bitmap needs to do extra cleanup of clusters,
	 * it should have done it in destroy_notify
	 */
	for (pos = bitmap->b_clusters.next, next = pos->next;
	     pos != &bitmap->b_clusters;
	     pos = next, next = pos->next) {
		bc = list_entry(pos, struct ocfs2_bitmap_cluster,
				bc_list);
		list_del(pos);
		ocfs2_bitmap_free_cluster(bc);
	}

	ocfs2_free(&bitmap->b_description);
	ocfs2_free(&bitmap);
}

errcode_t ocfs2_bitmap_set(ocfs2_bitmap *bitmap, uint64_t bitno,
			   int *oldval)
{
	errcode_t ret;

	if (bitno >= bitmap->b_total_bits)
		return OCFS2_ET_INVALID_BIT;

	ret = (*bitmap->b_ops->set_bit)(bitmap, bitno, oldval);
	if (ret)
		return ret;

	bitmap->b_set_bits++;
	return 0;
}

errcode_t ocfs2_bitmap_clear(ocfs2_bitmap *bitmap, uint64_t bitno,
			     int *oldval)
{
	errcode_t ret;

	if (bitno >= bitmap->b_total_bits)
		return OCFS2_ET_INVALID_BIT;

	ret = (*bitmap->b_ops->clear_bit)(bitmap, bitno, oldval);
	if (ret)
		return ret;

	bitmap->b_set_bits--;
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
			   char *description,
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
	INIT_LIST_HEAD(&bitmap->b_clusters);
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


errcode_t ocfs2_bitmap_alloc_cluster(ocfs2_bitmap *bitmap,
				     uint64_t start_bit,
				     int total_bits,
				     struct ocfs2_bitmap_cluster **ret_bc)
{
	errcode_t ret;
	struct ocfs2_bitmap_cluster *bc;
	int cl_bits;
	ocfs2_filesys *fs = bitmap->b_fs;

	if (total_bits < 0)
		return OCFS2_ET_INVALID_BIT;

	ret = ocfs2_malloc0(sizeof(struct ocfs2_bitmap_cluster), &bc);
	if (ret)
		return ret;

	bc->bc_start_bit = start_bit;
	bc->bc_total_bits = total_bits;

	cl_bits = OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits;
	bc->bc_size = (size_t)(((unsigned int)total_bits + 7) / 8);
	bc->bc_size = ((bc->bc_size + (fs->fs_clustersize - 1)) >> cl_bits) << cl_bits;

	ret = ocfs2_malloc0(bc->bc_size, &bc->bc_bitmap);
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
	ocfs2_filesys *fs = bitmap->b_fs;
	size_t new_size;
	int cl_bits;

	if ((bc->bc_start_bit + total_bits) > bitmap->b_total_bits)
		return OCFS2_ET_INVALID_BIT;

	cl_bits = OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits;
	new_size = (size_t)(((unsigned int)total_bits + 7) / 8);
	new_size = ((new_size + (fs->fs_clustersize - 1)) >> cl_bits) << cl_bits;

	if (new_size > bc->bc_size) {
		ret = ocfs2_realloc0(new_size, &bc->bc_bitmap,
				     bc->bc_size);
		if (ret)
			return ret;
		bc->bc_size = new_size;
	}

	bc->bc_total_bits = total_bits;

	return 0;
}

errcode_t ocfs2_bitmap_insert_cluster(ocfs2_bitmap *bitmap,
				      struct ocfs2_bitmap_cluster *bc)
{
	struct list_head *pos, *prev;
	struct ocfs2_bitmap_cluster *bc_tmp;

	if ((bc->bc_start_bit + bc->bc_total_bits) >
	    bitmap->b_total_bits)
		return OCFS2_ET_INVALID_BIT;

	prev = &bitmap->b_clusters;
	list_for_each(pos, &bitmap->b_clusters) {
		bc_tmp = list_entry(pos, struct ocfs2_bitmap_cluster,
				    bc_list);
		if (bc->bc_start_bit >=
		    (bc_tmp->bc_start_bit + bc_tmp->bc_total_bits)) {
			prev = pos;
			continue;
		}
		if ((bc->bc_start_bit + bc->bc_total_bits) <=
		    bc_tmp->bc_start_bit)
			break;

		return OCFS2_ET_INVALID_BIT;
	}

	list_add(&bc->bc_list, prev);

	return 0;
}


/*
 * Helper functions for the most generic of bitmaps.  If there is no
 * memory allocated for the bit, it fails.
 */
errcode_t ocfs2_bitmap_set_generic(ocfs2_bitmap *bitmap, uint64_t bitno,
				   int *oldval)
{
	int old_tmp;
	struct list_head *pos;
	struct ocfs2_bitmap_cluster *bc;

	list_for_each(pos, &bitmap->b_clusters) {
		bc = list_entry(pos, struct ocfs2_bitmap_cluster,
				bc_list);
		if (bitno < bc->bc_start_bit)
			break;
		if (bitno >= (bc->bc_start_bit + bc->bc_total_bits))
			continue;

		old_tmp = __test_and_set_bit(bitno - bc->bc_start_bit,
					     (unsigned long *)(bc->bc_bitmap));
		if (oldval)
			*oldval = old_tmp;

		return 0;
	}

	return OCFS2_ET_INVALID_BIT;
}

errcode_t ocfs2_bitmap_clear_generic(ocfs2_bitmap *bitmap,
				     uint64_t bitno, int *oldval)
{
	int old_tmp;
	struct list_head *pos;
	struct ocfs2_bitmap_cluster *bc;

	list_for_each(pos, &bitmap->b_clusters) {
		bc = list_entry(pos, struct ocfs2_bitmap_cluster,
				bc_list);
		if (bitno < bc->bc_start_bit)
			break;
		if (bitno > (bc->bc_start_bit + bc->bc_total_bits))
			continue;

		old_tmp = __test_and_clear_bit(bitno - bc->bc_start_bit,
					       (unsigned long *)bc->bc_bitmap);
		if (oldval)
			*oldval = old_tmp;

		return 0;
	}

	return OCFS2_ET_INVALID_BIT;
}

errcode_t ocfs2_bitmap_test_generic(ocfs2_bitmap *bitmap,
				    uint64_t bitno, int *val)
{
	struct list_head *pos;
	struct ocfs2_bitmap_cluster *bc;

	list_for_each(pos, &bitmap->b_clusters) {
		bc = list_entry(pos, struct ocfs2_bitmap_cluster,
				bc_list);
		if (bitno < bc->bc_start_bit)
			break;
		if (bitno >= (bc->bc_start_bit + bc->bc_total_bits))
			continue;

		*val = test_bit(bitno - bc->bc_start_bit,
				(unsigned long *)bc->bc_bitmap) ? 1 : 0;
		return 0;
	}

	return OCFS2_ET_INVALID_BIT;
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

static struct ocfs2_bitmap_operations generic_ops = {
	.set_bit	= ocfs2_bitmap_set_generic,
	.clear_bit	= ocfs2_bitmap_clear_generic,
	.test_bit	= ocfs2_bitmap_test_generic
};

static errcode_t create_bitmap(ocfs2_filesys *fs, int num_bits,
			       ocfs2_bitmap **ret_bitmap)
{
	errcode_t ret;
	ocfs2_bitmap *bitmap;
	struct ocfs2_bitmap_cluster *bc;

	ret = ocfs2_bitmap_new(fs,
			       num_bits,
			       "Test bitmap",
			       &generic_ops,
			       NULL,
			       &bitmap);
	if (ret)
		return ret;

	ret = ocfs2_bitmap_alloc_cluster(bitmap, 0, num_bits, &bc);
	if (ret) {
		ocfs2_bitmap_free(bitmap);
		return ret;
	}

	ret = ocfs2_bitmap_insert_cluster(bitmap, bc);
	if (ret) {
		ocfs2_bitmap_free_cluster(bc);
		ocfs2_bitmap_free(bitmap);
	} else
		*ret_bitmap = bitmap;

	return ret;
}

static void print_bitmap(ocfs2_bitmap *bitmap)
{
	uint64_t bitno, gap_start;
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
				"while testing bit %llu\n", bitno);
			break;
		}
		if (gap) {
			fprintf(stdout,
				"\nGap of length %llu at %llu\n",
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
		com_err("try_op", ret,
			"while setting bit %llu\n", bitno);
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
	uint64_t val;
	int num_bits = 4096;
	char *filename;
	ocfs2_filesys *fs;
	ocfs2_bitmap *bitmap;

	initialize_ocfs_error_table();

	while ((c = getopt(argc, argv, "s:a")) != EOF) {
		switch (c) {
			case 'a':
				alloc = 1;
				break;

			case 's':
				val = read_number(optarg);
				if (!val || (val > INT_MAX)) {
					fprintf(stderr,
						"Invalid size: %s\n",
						optarg);
					print_usage();
					return 1;
				}
				num_bits = (int)val;
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

	ret = create_bitmap(fs, num_bits, &bitmap);
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
