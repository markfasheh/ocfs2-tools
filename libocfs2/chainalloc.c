/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * chainalloc.c
 *
 * Functions to use the chain allocators for the OCFS2 userspace
 * library.
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
#include <inttypes.h>

#include "ocfs2.h"

#include "bitmap.h"
#include "bitops.h"
#include "kernel-rbtree.h"


struct chainalloc_bitmap_private {
	ocfs2_cached_inode	*cb_cinode;
	errcode_t		cb_errcode;
	int			cb_dirty;
};

struct chainalloc_region_private {
	struct chainalloc_bitmap_private	*cr_cb;
	struct _ocfs2_group_desc		*cr_ag;
	int					cr_dirty;
};


static void chainalloc_destroy_notify(ocfs2_bitmap *bitmap)
{
	struct rb_node *node = NULL;
	struct ocfs2_bitmap_region *br;
	struct chainalloc_region_private *cr;

	for (node = rb_first(&bitmap->b_regions); node; node = rb_next(node)) {
		br = rb_entry(node, struct ocfs2_bitmap_region, br_node);

		cr = br->br_private;
		if (cr->cr_ag)
			ocfs2_free(&cr->cr_ag);
		ocfs2_free(&br->br_private);
	}

	ocfs2_free(&bitmap->b_private);
}

static uint64_t chainalloc_scale_start_bit(ocfs2_filesys *fs,
					   uint64_t blkno,
					   int bpc)
{
	int bitsize = fs->fs_clustersize / bpc;

	if (bitsize == fs->fs_blocksize)
		return blkno;
	if (bitsize < fs->fs_blocksize)
		return blkno * (fs->fs_blocksize / bitsize);
	else
		return blkno / (bitsize / fs->fs_blocksize);
}

static int chainalloc_process_group(ocfs2_filesys *fs,
				    uint64_t gd_blkno,
				    int chain_num,
				    void *priv_data)
{
	ocfs2_bitmap *bitmap = priv_data;
	struct ocfs2_bitmap_region *br;
	struct chainalloc_bitmap_private *cb = bitmap->b_private;
	struct chainalloc_region_private *cr;
	uint64_t start_bit;
	char *gd_buf;

	cb->cb_errcode = ocfs2_malloc_block(fs->fs_io, &gd_buf);
	if (cb->cb_errcode)
		return OCFS2_CHAIN_ABORT;

	cb->cb_errcode = ocfs2_read_group_desc(fs, gd_blkno, gd_buf);
	if (cb->cb_errcode)
		goto out_free_buf;

	cb->cb_errcode = ocfs2_malloc0(sizeof(struct chainalloc_region_private),
				       &cr);
	if (cb->cb_errcode)
		goto out_free_buf;

	cr->cr_cb = cb;
	cr->cr_ag = (ocfs2_group_desc *)gd_buf;

	cb->cb_errcode = OCFS2_ET_CORRUPT_GROUP_DESC;
	if (cr->cr_ag->bg_size != ocfs2_group_bitmap_size(fs->fs_blocksize))
		goto out_free_cr;

	if (gd_blkno == OCFS2_RAW_SB(fs->fs_super)->s_first_cluster_group)
		gd_blkno = 0;

	start_bit = chainalloc_scale_start_bit(fs, gd_blkno,
					       cb->cb_cinode->ci_inode->id2.i_chain.cl_bpc);
	cb->cb_errcode = ocfs2_bitmap_alloc_region(bitmap,
						   start_bit,
						   cr->cr_ag->bg_bits,
						   &br);
	if (cb->cb_errcode)
		goto out_free_cr;

	br->br_private = cr;
	memcpy(br->br_bitmap, cr->cr_ag->bg_bitmap, br->br_bytes);
	br->br_set_bits = cr->cr_ag->bg_bits - cr->cr_ag->bg_free_bits_count;

	cb->cb_errcode = ocfs2_bitmap_insert_region(bitmap, br);
	if (cb->cb_errcode)
		goto out_free_region;

	return 0;

out_free_region:
	ocfs2_bitmap_free_region(br);

out_free_cr:
	ocfs2_free(&cr);

out_free_buf:
	ocfs2_free(&gd_buf);

	return OCFS2_CHAIN_ABORT;
}


static errcode_t chainalloc_read_bitmap(ocfs2_bitmap *bitmap)
{
	errcode_t ret;
	struct chainalloc_bitmap_private *cb = bitmap->b_private;
	
	if (!cb->cb_cinode)
		return OCFS2_ET_INVALID_ARGUMENT;

	ret = ocfs2_chain_iterate(bitmap->b_fs,
				  cb->cb_cinode->ci_blkno,
				  chainalloc_process_group,
				  bitmap);

	return ret;
}

static errcode_t chainalloc_write_group(struct ocfs2_bitmap_region *br,
					void *private_data)
{
	struct chainalloc_region_private *cr = br->br_private;
	ocfs2_filesys *fs = private_data;
	errcode_t ret = 0;

	if (!cr->cr_dirty)
		return 0;

	memcpy(cr->cr_ag->bg_bitmap, br->br_bitmap, br->br_bytes);

	ret = ocfs2_write_group_desc(fs, cr->cr_ag->bg_blkno, 
				     (char *)cr->cr_ag);
	if (ret == 0)
		cr->cr_dirty = 0;

	return ret;
}

static errcode_t chainalloc_write_bitmap(ocfs2_bitmap *bitmap)
{
	struct chainalloc_bitmap_private *cb = bitmap->b_private;
	ocfs2_filesys *fs;
	errcode_t ret;

	if (!cb->cb_cinode)
		return OCFS2_ET_INVALID_ARGUMENT;

	if (!cb->cb_dirty)
		return 0;

	fs = cb->cb_cinode->ci_fs;

	ret = ocfs2_bitmap_foreach_region(bitmap, chainalloc_write_group, fs);
	if (ret)
		goto out;

	ret = ocfs2_write_cached_inode(fs, cb->cb_cinode);
	if (ret == 0)
		cb->cb_dirty = 0;

out:
	return ret;
}

static int chainalloc_merge_region(ocfs2_bitmap *bitmap,
				   struct ocfs2_bitmap_region *prev,
				   struct ocfs2_bitmap_region *next)
{
	/* Can't merge */
	return 0;
}

/* update the free bit counts in the alloc group, chain rec, and inode so
 * that they are valid if we're asked to write in the future */
static void chainalloc_bit_change_notify(ocfs2_bitmap *bitmap,
					 struct ocfs2_bitmap_region *br,
					 uint64_t bitno,
					 int new_val)
{
	struct chainalloc_bitmap_private *cb = bitmap->b_private;
	struct chainalloc_region_private *cr = br->br_private;
	ocfs2_dinode *di = cb->cb_cinode->ci_inode;
	ocfs2_group_desc *ag = cr->cr_ag;
	ocfs2_chain_rec *rec = &di->id2.i_chain.cl_recs[ag->bg_chain];

	if (new_val) {
		ag->bg_free_bits_count--;
		rec->c_free--;
		di->id1.bitmap1.i_used++;
	} else {
		ag->bg_free_bits_count++;
		rec->c_free++;
		di->id1.bitmap1.i_used--;
	}

	cr->cr_dirty = 1;
	cb->cb_dirty = 1;
}

static struct ocfs2_bitmap_operations chainalloc_bitmap_ops = {
	.set_bit		= ocfs2_bitmap_set_generic,
	.clear_bit		= ocfs2_bitmap_clear_generic,
	.test_bit		= ocfs2_bitmap_test_generic,
	.find_next_set		= ocfs2_bitmap_find_next_set_generic,
	.find_next_clear	= ocfs2_bitmap_find_next_clear_generic,
	.merge_region		= chainalloc_merge_region,
	.read_bitmap		= chainalloc_read_bitmap,
	.write_bitmap		= chainalloc_write_bitmap,
	.destroy_notify		= chainalloc_destroy_notify,
	.bit_change_notify	= chainalloc_bit_change_notify,
};

static errcode_t ocfs2_chainalloc_bitmap_new(ocfs2_filesys *fs,
					     const char *description,
					     uint64_t total_bits,
					     ocfs2_bitmap **ret_bitmap)
{
	errcode_t ret;
	ocfs2_bitmap *bitmap;
	struct chainalloc_bitmap_private *cb;

	ret = ocfs2_malloc0(sizeof(struct chainalloc_bitmap_private),
			    &cb);
	if (ret)
		return ret;

	ret = ocfs2_bitmap_new(fs,
			       total_bits,
			       description ? description :
			       "Generic chain allocator bitmap",
			       &chainalloc_bitmap_ops,
			       cb,
			       &bitmap);
	if (ret)
		return ret;

	*ret_bitmap = bitmap;

	return 0;
}

static void ocfs2_chainalloc_set_cinode(ocfs2_bitmap *bitmap,
					ocfs2_cached_inode *cinode)
{
	struct chainalloc_bitmap_private *cb = bitmap->b_private;

	cb->cb_cinode = cinode;
}

errcode_t ocfs2_load_chain_allocator(ocfs2_filesys *fs,
				     ocfs2_cached_inode *cinode)
{
	errcode_t ret;
	uint64_t total_bits;
	char name[256];

	if (cinode->ci_chains)
		ocfs2_bitmap_free(cinode->ci_chains);

	total_bits = fs->fs_clusters *
		cinode->ci_inode->id2.i_chain.cl_bpc;

	snprintf(name, sizeof(name),
		 "Chain allocator inode %"PRIu64, cinode->ci_blkno);
	ret = ocfs2_chainalloc_bitmap_new(fs, name, total_bits,
					  &cinode->ci_chains);
	if (ret)
		return ret;

	ocfs2_chainalloc_set_cinode(cinode->ci_chains,
				    cinode);
	ret = ocfs2_bitmap_read(cinode->ci_chains);
	if (ret) {
		ocfs2_bitmap_free(cinode->ci_chains);
		return ret;
	}

	return 0;
}

errcode_t ocfs2_write_chain_allocator(ocfs2_filesys *fs,
				      ocfs2_cached_inode *cinode)
{
	if (!cinode->ci_chains)
		return OCFS2_ET_INVALID_ARGUMENT;

	return ocfs2_bitmap_write(cinode->ci_chains);
}

/* FIXME: should take a hint, no? */
/* FIXME: Better name, too */
errcode_t ocfs2_chain_alloc(ocfs2_filesys *fs,
			    ocfs2_cached_inode *cinode,
			    uint64_t *blkno)
{
	errcode_t ret;
	int oldval;

	if (!cinode->ci_chains)
		return OCFS2_ET_INVALID_ARGUMENT;

	ret = ocfs2_bitmap_find_next_clear(cinode->ci_chains, 0, blkno);
	if (ret)
		return ret;

	ret = ocfs2_bitmap_set(cinode->ci_chains, *blkno, &oldval);
	if (ret)
		return ret;
	if (oldval)
		return OCFS2_ET_INTERNAL_FAILURE;

	return 0;
}

errcode_t ocfs2_chain_free(ocfs2_filesys *fs,
			   ocfs2_cached_inode *cinode,
			   uint64_t blkno)
{
	errcode_t ret;
	int oldval;

	if (!cinode->ci_chains)
		return OCFS2_ET_INVALID_ARGUMENT;

	ret = ocfs2_bitmap_clear(cinode->ci_chains, blkno, &oldval);
	if (ret)
		return ret;
	if (!oldval)
		return OCFS2_ET_FREEING_UNALLOCATED_REGION;

	return 0;
}

/* just a variant that won't return failure if it tried to set what
 * was already set */
errcode_t ocfs2_chain_force_val(ocfs2_filesys *fs,
				ocfs2_cached_inode *cinode,
				uint64_t blkno, 
				int newval, 
				int *oldval)
{
	errcode_t ret;

	if (!cinode->ci_chains)
		return OCFS2_ET_INVALID_ARGUMENT;

	if (newval)
		ret = ocfs2_bitmap_set(cinode->ci_chains, blkno, oldval);
	else
		ret = ocfs2_bitmap_clear(cinode->ci_chains, blkno, oldval);

	return ret;
}

errcode_t ocfs2_chain_test(ocfs2_filesys *fs,
			   ocfs2_cached_inode *cinode,
			   uint64_t blkno,
			   int *oldval)
{
	if (!cinode->ci_chains)
		return OCFS2_ET_INVALID_ARGUMENT;

	return ocfs2_bitmap_test(cinode->ci_chains, blkno, oldval);
}

void ocfs2_init_group_desc(ocfs2_filesys *fs, ocfs2_group_desc *gd,
			   uint64_t blkno, uint32_t generation,
			   uint64_t parent_inode, uint16_t bits,
			   uint16_t chain)
{
	memset(gd, 0, fs->fs_blocksize);

	strcpy(gd->bg_signature, OCFS2_GROUP_DESC_SIGNATURE);
	gd->bg_generation = generation;
	gd->bg_size = ocfs2_group_bitmap_size(fs->fs_blocksize);
	gd->bg_bits = bits;
	gd->bg_chain = chain;
	gd->bg_parent_dinode = parent_inode;
	gd->bg_blkno = blkno;

	/* First bit set to account for the descriptor block */
	ocfs2_set_bit(0, gd->bg_bitmap);
	gd->bg_free_bits_count = gd->bg_bits - 1;
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
		"debug_bitmap [-i <blkno>] <filename>\n");
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
	char *filename;
	uint64_t blkno = 0;
	ocfs2_filesys *fs;
	ocfs2_cached_inode *cinode;

	initialize_ocfs_error_table();

	while ((c = getopt(argc, argv, "i:")) != EOF) {
		switch (c) {
			case 'i':
				blkno = read_number(optarg);
				if (!blkno) {
					print_usage();
					return 1;
				}
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

	ret = ocfs2_read_cached_inode(fs, blkno,
				      &cinode);
	if (ret) {
		com_err(argv[0], ret,
			"while reading inode %"PRIu64, blkno);
		goto out_close;
	}

	ret = ocfs2_load_chain_allocator(fs, cinode);
	if (ret) {
		com_err(argv[0], ret,
			"while loading chain allocator");
		goto out_free_inode;
	}

	run_test(cinode->ci_chains);

out_free_inode:
	ocfs2_free_cached_inode(fs, cinode);

out_close:
	ocfs2_close(fs);

	return ret;
}



#endif  /* DEBUG_EXE */
