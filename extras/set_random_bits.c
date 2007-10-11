/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * set_random_bits.c
 *
 * Set a (not-so-random) pattern of alternating set bits on the global
 * bitmap (or any file you give me the block offset of). Note that
 * this will not clear any bits that have already been set.
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
 * Authors: Mark Fasheh
 */

#define _XOPEN_SOURCE 600  /* Triggers XOPEN2K in features.h */
#define _LARGEFILE64_SOURCE

#include <stdlib.h>
#include <getopt.h>
#include <inttypes.h>
#include <string.h>
#include <stdint.h>
#include "ocfs2.h"

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
		"Usage: set_random_bits -i <inode_blkno> <filename>\n");
}

struct walk_block {
	struct ocfs2_dinode *di;
	char *buf;
	uint32_t used;
};

#define BITCOUNT(x)     (((BX_(x)+(BX_(x)>>4)) & 0x0F0F0F0F) % 255)
#define BX_(x)          ((x) - (((x)>>1)&0x77777777) \
			     - (((x)>>2)&0x33333333) \
			     - (((x)>>3)&0x11111111))

static int walk_blocks_func(ocfs2_filesys *fs,
			    uint64_t blkno,
			    uint64_t bcount,
			    uint16_t ext_flags,
			    void *priv_data)
{
	struct walk_block *wb = priv_data;
	errcode_t ret;
	int i;
	uint32_t *up;

	ret = io_read_block(fs->fs_io, blkno, 1, wb->buf);
	if (ret) {
		com_err("walk_blocks_func", ret,
			"while reading block %"PRIu64, blkno);
		return OCFS2_BLOCK_ABORT;
	}

	/* set every other bit */
	up = (uint32_t *) wb->buf;
	for(i = 0; i < (fs->fs_blocksize / sizeof(uint32_t)); i++) {
		up[i] |= 0x55555555;
		wb->used += BITCOUNT(up[i]);
	}

	ret = io_write_block(fs->fs_io, blkno, 1, wb->buf);
	if (ret) {
		com_err("walk_blocks_func", ret,
			"while writing block %"PRIu64, blkno);
		return OCFS2_BLOCK_ABORT;
	}
	return 0;
}

extern int opterr, optind;
extern char *optarg;

int main(int argc, char *argv[])
{
	errcode_t ret;
	uint64_t blkno, sys_blkno;
	int c;
	char *filename, *buf;
	const char *bitmap_name = ocfs2_system_inodes[GLOBAL_BITMAP_SYSTEM_INODE].si_name;
	ocfs2_filesys *fs;
	struct ocfs2_dinode *di;
	struct walk_block wb;

	blkno = OCFS2_SUPER_BLOCK_BLKNO;

	initialize_ocfs_error_table();

	while ((c = getopt(argc, argv, "i:")) != EOF) {
		switch (c) {
			case 'i':
				blkno = read_number(optarg);
				if (blkno <= OCFS2_SUPER_BLOCK_BLKNO) {
					fprintf(stderr,
						"Invalid inode block: %s\n",
						optarg);
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

	ret = ocfs2_open(filename, OCFS2_FLAG_RW, 0, 0, &fs);
	if (ret) {
		com_err(argv[0], ret,
			"while opening file \"%s\"", filename);
		goto out;
	}

	if (blkno == OCFS2_SUPER_BLOCK_BLKNO) {
		sys_blkno = OCFS2_RAW_SB(fs->fs_super)->s_system_dir_blkno;

		ret = ocfs2_lookup(fs, sys_blkno, bitmap_name, 
				   strlen(bitmap_name), NULL, &blkno);
		if (ret) {
			com_err(argv[0], ret,
				"while looking up \"%s\"", bitmap_name);
			goto out;
		}
	}

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret) {
		com_err(argv[0], ret,
			"while allocating inode buffer");
		goto out_close;
	}

	memset(&wb, 0, sizeof(wb));

	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret) {
		com_err(argv[0], ret, "while reading inode %"PRIu64, blkno);
		goto out_free;
	}

	di = (struct ocfs2_dinode *)buf;

	wb.di = di;
	ret = ocfs2_malloc_block(fs->fs_io, &wb.buf);
	if (ret) {
		com_err(argv[0], ret,
			"while allocating block buffer");
		goto out_free;
	}

	ret = ocfs2_block_iterate(fs, blkno, 0,
				  walk_blocks_func,
				  &wb);
	if (ret) {
		com_err(argv[0], ret,
			"while walking blocks");
		goto out_free;
	}

	di->id1.bitmap1.i_used = wb.used;

	ret = ocfs2_write_inode(fs, blkno, buf);
	if (ret) {
		com_err(argv[0], ret, "while reading inode %"PRIu64, blkno);
		goto out_free;
	}

out_free:
	if (wb.buf)
		ocfs2_free(&wb.buf);

	ocfs2_free(&buf);

out_close:
	ret = ocfs2_close(fs);
	if (ret) {
		com_err(argv[0], ret,
			"while closing file \"%s\"", filename);
	}

out:
	return 0;
}


