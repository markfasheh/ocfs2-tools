/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * find_allocation_fragments.c
 *
 * Find fragments of free space in a given allocator
 *
 * Copyright (C) 2006 Oracle.  All rights reserved.
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
 * Authors: Mark Fasheh
 */

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE

#include <unistd.h>

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "ocfs2.h"
#include "bitops.h"

struct fragment {
	uint64_t f_group_blkno;
	uint16_t f_chain;
	uint16_t f_bit_start;
	uint16_t f_num_bits;
};

#define FREE_BIT_STATS	200
int free_bit_stats[FREE_BIT_STATS];

struct fragment largest = {0, };

static void print_usage(void)
{
	fprintf(stderr,
	"Usage: find_allocation_fragments <device> <block #>\n"
	"Will print all free space fragments found in the allocator whose\n"
	"inode is located at <block #> on device <device>\n");
}

static int find_next_region(struct ocfs2_group_desc *gd, int offset,
			    int *start, int *end)
{
	int ret;

	if (offset >= gd->bg_bits)
		return 0;

	ret = ocfs2_find_next_bit_clear(gd->bg_bitmap, gd->bg_bits, offset);
	if (ret == gd->bg_bits)
		return 0;

	*start = ret;

	*end = ocfs2_find_next_bit_set(gd->bg_bitmap, gd->bg_bits, *start);

	return 1;
}

static int print_group(struct ocfs2_group_desc *gd)
{
	int offset, start, end, free;
	int header = 0;

	offset = 0;
	while (find_next_region(gd, offset, &start, &end)) {
		if (!header) {
			printf("%-6s   %-6s   %-12s\n", "Free", "At Bit", "In Group");
			header = 1;
		}

		free = end - start;

		printf("%-6u   %-6u   %"PRIu64"\n", free, start, gd->bg_blkno);

		if (free < FREE_BIT_STATS)
			free_bit_stats[free]++;

		if (largest.f_num_bits < (end - start)) {
			largest.f_group_blkno = gd->bg_blkno;
			largest.f_chain = gd->bg_chain;
			largest.f_bit_start = start;
			largest.f_num_bits = free;
		}

		offset = end;
	}

	printf("\n");

	return 0;
}

static int iterate_chain(ocfs2_filesys *fs, uint64_t start)
{
	errcode_t ret;
	uint64_t gd_blkno;
	char *buf = NULL;
	struct ocfs2_group_desc *gd;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	gd_blkno = start;
	do {
		ret = ocfs2_read_group_desc(fs, gd_blkno, buf);
		if (ret)
			goto out_free;

		gd = (struct ocfs2_group_desc *) buf;

		print_group(gd);

		gd_blkno = gd->bg_next_group;
	} while (gd_blkno);

out_free:
	ocfs2_free(&buf);

	return ret;
}

#define BITMAP_FLAGS (OCFS2_VALID_FL|OCFS2_SYSTEM_FL|OCFS2_BITMAP_FL|OCFS2_CHAIN_FL)

static int iterate_allocator(ocfs2_filesys *fs, uint64_t blkno)
{
	int i;
	errcode_t ret;
	char *buf = NULL;
	struct ocfs2_dinode *di;
	struct ocfs2_chain_list *cl;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret)
		goto out_free;

	di = (struct ocfs2_dinode *) buf;
	if (!(di->i_flags & BITMAP_FLAGS)) {
		ret = OCFS2_ET_CORRUPT_CHAIN;
		goto out_free;
	}

	printf("Allocator Inode: %"PRIu64"\n\n", blkno);

	cl = &di->id2.i_chain;

	for(i = 0; i < cl->cl_next_free_rec; i++) {
		ret = iterate_chain(fs, cl->cl_recs[i].c_blkno);
		if (ret)
			goto out_free;
	}

	if (largest.f_num_bits)
		printf("Largest empty extent of %u bits at offset %u in "
		       "descriptor %"PRIu64"\n", largest.f_num_bits,
		       largest.f_bit_start, largest.f_group_blkno);

out_free:
	ocfs2_free(&buf);

	return ret;
}

int main(int argc, char **argv)
{
	errcode_t ret;
	char *device;
	uint64_t inode;
	ocfs2_filesys *fs;
	int i;

	initialize_ocfs_error_table();

	if (argc != 3) {
		print_usage();
		exit(1);
	}

	device = argv[1];
	inode = atoll(argv[2]);

	memset(free_bit_stats, 0, sizeof(free_bit_stats));

	ret = ocfs2_open(device, OCFS2_FLAG_RO, 0, 0, &fs);
	if (ret) {
		com_err(argv[0], ret,
			"while opening file \"%s\"", device);
		goto out;
	}

	ret = iterate_allocator(fs, inode);
	if (ret) {
		com_err(argv[0], ret,
			"while iterating allocator %"PRIu64"\n", inode);
		goto out_close;
	}

	printf("Statistics:\n");
	printf("%-6s   %-6s\n", "Count", "Bits");
	for (i = 1; i < FREE_BIT_STATS; ++i) {
		if (free_bit_stats[i])
			printf("%-6u   %-6u\n", free_bit_stats[i], i);
	}
	

out_close:
	ret = ocfs2_close(fs);
	if (ret) {
		com_err(argv[0], ret,
			"while closing file \"%s\"", device);
	}

out:
	return ret ? 1 : 0;
}
