/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * image.c
 *
 * supporting structures/functions to handle ocfs2 image file
 *
 * Copyright (C) 2008 Oracle.  All rights reserved.
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
 */

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <fcntl.h>
#include <ocfs2/bitops.h>

#include "ocfs2/ocfs2.h"
#include "ocfs2/byteorder.h"
#include "ocfs2/image.h"

void ocfs2_image_swap_header(struct ocfs2_image_hdr *hdr)
{
	int i;

	if (cpu_is_little_endian)
		return;

	for (i = 0; i < hdr->hdr_superblkcnt; i++)
		hdr->hdr_superblocks[i]	= bswap_64(hdr->hdr_superblocks[i]);

	hdr->hdr_magic 		= bswap_32(hdr->hdr_magic);
	hdr->hdr_version 	= bswap_32(hdr->hdr_version);
	hdr->hdr_timestamp	= bswap_32(hdr->hdr_timestamp);
	hdr->hdr_fsblkcnt	= bswap_64(hdr->hdr_fsblkcnt);
	hdr->hdr_fsblksz 	= bswap_64(hdr->hdr_fsblksz);
	hdr->hdr_imgblkcnt	= bswap_64(hdr->hdr_imgblkcnt);
	hdr->hdr_bmpblksz	= bswap_64(hdr->hdr_bmpblksz);
	hdr->hdr_superblkcnt	= bswap_64(hdr->hdr_superblkcnt);
}

errcode_t ocfs2_image_free_bitmap(ocfs2_filesys *ofs)
{
	struct ocfs2_image_state *ost = ofs->ost;
	int i;

	if (!ost->ost_bmparr)
		return 0;

	for (i=0; i<ost->ost_bmpblks; i++)
		if (ost->ost_bmparr[i].arr_self)
			ocfs2_free(&ost->ost_bmparr[i].arr_self);

	if (ost->ost_bmparr)
		ocfs2_free(&ost->ost_bmparr);
	return 0;
}

/*
 * allocate ocfs2_image_bitmap_arr and ocfs2 image bitmap blocks. o2image bitmap
 * block is of size OCFS2_IMAGE_BITMAP_BLOCKSIZE and ocfs2_image_bitmap_arr
 * tracks the bitmap blocks
 */
errcode_t ocfs2_image_alloc_bitmap(ocfs2_filesys *ofs)
{
	uint64_t blks, allocsize, leftsize;
	struct ocfs2_image_state *ost = ofs->ost;
	int indx, i, n;
	errcode_t ret;
	char *buf;

	ost->ost_bmpblks =
		((ost->ost_fsblkcnt - 1) / (OCFS2_IMAGE_BITS_IN_BLOCK)) + 1;
	ost->ost_bmpblksz = OCFS2_IMAGE_BITMAP_BLOCKSIZE;
	blks = ost->ost_bmpblks;

	/* allocate memory for an array to track bitmap blocks */
	ret = ocfs2_malloc0((blks * sizeof(ocfs2_image_bitmap_arr)),
			    &ost->ost_bmparr);
	if (ret)
		return ret;

	allocsize = blks * OCFS2_IMAGE_BITMAP_BLOCKSIZE;
	leftsize = allocsize;
	indx = 0;

	/* allocate bitmap blocks and assign blocks to above array */
	while (leftsize) {
		ret = ocfs2_malloc_blocks(ofs->fs_io,
					  allocsize/io_get_blksize(ofs->fs_io),
					  &buf);
		if (ret && (ret != -ENOMEM))
			goto out;

		if (ret == -ENOMEM) {
			if (allocsize == OCFS2_IMAGE_BITMAP_BLOCKSIZE)
				goto out;
			allocsize >>= 1;
			if (allocsize % OCFS2_IMAGE_BITMAP_BLOCKSIZE) {
				allocsize /= OCFS2_IMAGE_BITMAP_BLOCKSIZE;
				allocsize *= OCFS2_IMAGE_BITMAP_BLOCKSIZE;
			}
			continue;
		}

		n = allocsize / OCFS2_IMAGE_BITMAP_BLOCKSIZE;
		for (i = 0; i < n; i++) {
			ost->ost_bmparr[indx].arr_set_bit_cnt = 0;
			ost->ost_bmparr[indx].arr_map =
				((char *)buf + (i *
						OCFS2_IMAGE_BITMAP_BLOCKSIZE));

			/* remember buf address to free it later */
			if (!i)
				ost->ost_bmparr[indx].arr_self = buf;
			indx++;
		}
		leftsize -= allocsize;
		if (leftsize <= allocsize)
			allocsize = leftsize;
	}
out:
	/* If allocation failed free and return error */
	if (leftsize) {
		for (i = 0; i < indx; i++)
			if (ost->ost_bmparr[i].arr_self)
				ocfs2_free(&ost->ost_bmparr[i].arr_self);
		ocfs2_free(&ost->ost_bmparr);
	}

	return ret;
}

/*
 * This routine loads bitmap blocks from an o2image image file into memory.
 * This process happens during file open. bitmap blocks reside towards
 * the end of the imagefile.
 */
errcode_t ocfs2_image_load_bitmap(ocfs2_filesys *ofs)
{
	struct ocfs2_image_state *ost;
	struct ocfs2_image_hdr *hdr;
	uint64_t blk_off, bits_set;
	int i, j, fd;
	ssize_t count;
	errcode_t ret;
	char *blk;

	ret = ocfs2_malloc0(sizeof(struct ocfs2_image_state), &ofs->ost);
	if (ret)
		return ret;

	ost = ofs->ost;
	ret = ocfs2_malloc_block(ofs->fs_io, &blk);
	if (ret)
		return ret;

	/* read ocfs2 image header */
	ret = io_read_block(ofs->fs_io, 0, 1, blk);
	if (ret)
		goto out;

	hdr = (struct ocfs2_image_hdr *)blk;
	ocfs2_image_swap_header(hdr);

	ret = OCFS2_ET_BAD_MAGIC;
	if (hdr->hdr_magic != OCFS2_IMAGE_MAGIC)
		goto out;

	if (memcmp(hdr->hdr_magic_desc, OCFS2_IMAGE_DESC,
		   sizeof(OCFS2_IMAGE_DESC)))
		goto out;

	ret = OCFS2_ET_OCFS_REV;
	if (hdr->hdr_version > OCFS2_IMAGE_VERSION)
		goto out;

	ost->ost_fsblkcnt 	= hdr->hdr_fsblkcnt;
	ost->ost_fsblksz 	= hdr->hdr_fsblksz;
	ost->ost_imgblkcnt 	= hdr->hdr_imgblkcnt;
	ost->ost_bmpblksz 	= hdr->hdr_bmpblksz;

	ret = ocfs2_image_alloc_bitmap(ofs);
	if (ret)
		return ret;

	/* load bitmap blocks ocfs2 image state */
	bits_set = 0;
	fd 	= io_get_fd(ofs->fs_io);
	blk_off = (ost->ost_imgblkcnt + 1) * ost->ost_fsblksz;

	for (i = 0; i < ost->ost_bmpblks; i++) {
		ost->ost_bmparr[i].arr_set_bit_cnt = bits_set;
		/*
		 * we don't use io_read_block as ocfs2 image bitmap block size
		 * could be different from filesystem block size
		 */
		count = pread64(fd, ost->ost_bmparr[i].arr_map,
				ost->ost_bmpblksz, blk_off);
		if (count < 0) {
			ret = OCFS2_ET_IO;
			goto out;
		}

		/* add bits set in this bitmap */
		for (j = 0; j < (ost->ost_bmpblksz * 8); j++)
			if (ocfs2_test_bit(j, ost->ost_bmparr[i].arr_map))
				bits_set++;

		blk_off += ost->ost_bmpblksz;
	}

out:
	if (blk)
		ocfs2_free(&blk);
	return ret;
}

void ocfs2_image_mark_bitmap(ocfs2_filesys *ofs, uint64_t blkno)
{
	struct ocfs2_image_state *ost = ofs->ost;
	int bitmap_blk;
	int bit;

	bit = blkno % OCFS2_IMAGE_BITS_IN_BLOCK;
	bitmap_blk = blkno / OCFS2_IMAGE_BITS_IN_BLOCK;

	ocfs2_set_bit(bit, ost->ost_bmparr[bitmap_blk].arr_map);
}

int ocfs2_image_test_bit(ocfs2_filesys *ofs, uint64_t blkno)
{
	struct ocfs2_image_state *ost = ofs->ost;
	int bitmap_blk;
	int bit;

	bit = blkno % OCFS2_IMAGE_BITS_IN_BLOCK;
	bitmap_blk = blkno / OCFS2_IMAGE_BITS_IN_BLOCK;

	if (ocfs2_test_bit(bit, ost->ost_bmparr[bitmap_blk].arr_map))
		return 1;
	else
		return 0;
}

uint64_t ocfs2_image_get_blockno(ocfs2_filesys *ofs, uint64_t blkno)
{
	struct ocfs2_image_state *ost = ofs->ost;
	uint64_t ret_blk;
	int bitmap_blk;
	int i, bit;

	bit = blkno % OCFS2_IMAGE_BITS_IN_BLOCK;
	bitmap_blk = blkno / OCFS2_IMAGE_BITS_IN_BLOCK;

	if (ocfs2_test_bit(bit, ost->ost_bmparr[bitmap_blk].arr_map)) {
		ret_blk = ost->ost_bmparr[bitmap_blk].arr_set_bit_cnt + 1;

		/* add bits set in this block before the block no */
		for (i = 0; i < bit; i++)
			if (ocfs2_test_bit(i,
					   ost->ost_bmparr[bitmap_blk].arr_map))
				ret_blk++;
	} else
		ret_blk = -1;

	return ret_blk;
}
