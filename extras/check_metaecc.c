/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * check_metaecc.c
 *
 * Simple tool to check ecc of a metadata block.
 *
 * Copyright (C) 2010 Novell.  All rights reserved.
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
 * Authors: Coly Li <coly.li@suse.de>
 *
 */

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE

#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>

#include "ocfs2/ocfs2.h"
#include "ocfs2/byteorder.h"

static void print_usage(void)
{
	fprintf(stderr,
		"Usage: check_metaecc <device> <block #>\n");
	exit(1);
}

/* copied from find_inode_paths.c */
static uint64_t read_number(const char *num)
{
	uint64_t val;
	char *ptr;

	val = strtoull(num, &ptr, 0);
	if (!ptr || *ptr)
		return 0;

	return val;
}

static errcode_t check_metaecc(ocfs2_filesys *fs,
				uint64_t blk,
				char *dev,
				char *block)
{
	char signature[8];
	char name[256] = {0, };
	struct ocfs2_block_check check;
	int do_check = 1;
	errcode_t err = 0;

	memcpy(signature, block, sizeof(signature));

	if (!strncmp(signature, OCFS2_SUPER_BLOCK_SIGNATURE,
			sizeof(OCFS2_SUPER_BLOCK_SIGNATURE))) {
		struct ocfs2_dinode *di = (struct ocfs2_dinode *)block;
		check = di->i_check;
		snprintf(name, sizeof(name), OCFS2_SUPER_BLOCK_SIGNATURE);
	} else if (!strncmp(signature, OCFS2_INODE_SIGNATURE,
			sizeof(OCFS2_INODE_SIGNATURE))) {
		struct ocfs2_dinode *di;
		di = (struct ocfs2_dinode *)block;
		check = di->i_check;
		snprintf(name, sizeof(name), OCFS2_INODE_SIGNATURE);
	} else if (!strncmp(signature, OCFS2_EXTENT_BLOCK_SIGNATURE,
			sizeof(OCFS2_EXTENT_BLOCK_SIGNATURE))) {
		struct ocfs2_extent_block *eb;
		eb = (struct ocfs2_extent_block *)block;
		check = eb->h_check;
		snprintf(name, sizeof(name), OCFS2_EXTENT_BLOCK_SIGNATURE);
	} else if (!strncmp(signature, OCFS2_GROUP_DESC_SIGNATURE,
			sizeof(OCFS2_GROUP_DESC_SIGNATURE))) {
		struct ocfs2_group_desc *gd;
		gd = (struct ocfs2_group_desc *)block;
		check = gd->bg_check;
		snprintf(name, sizeof(name), OCFS2_GROUP_DESC_SIGNATURE);
	} else if (!strncmp(signature, OCFS2_XATTR_BLOCK_SIGNATURE,
			sizeof(OCFS2_XATTR_BLOCK_SIGNATURE))) {
		struct ocfs2_xattr_block *xb;
		xb = (struct ocfs2_xattr_block *)block;
		check = xb->xb_check;
		snprintf(name, sizeof(name), OCFS2_XATTR_BLOCK_SIGNATURE);
	} else if (!strncmp(signature, OCFS2_REFCOUNT_BLOCK_SIGNATURE,
			sizeof(OCFS2_REFCOUNT_BLOCK_SIGNATURE))) {
		struct ocfs2_refcount_block *rb;
		rb = (struct ocfs2_refcount_block *)block;
		check = rb->rf_check;
		snprintf(name, sizeof(name), OCFS2_REFCOUNT_BLOCK_SIGNATURE);
	} else if (!strncmp(signature, OCFS2_DX_ROOT_SIGNATURE,
			sizeof(OCFS2_DX_ROOT_SIGNATURE))) {
		struct ocfs2_dx_root_block *dx_root;
		dx_root = (struct ocfs2_dx_root_block *)block;
		check = dx_root->dr_check;
		snprintf(name, sizeof(name), OCFS2_DX_ROOT_SIGNATURE);
	} else if (!strncmp(signature, OCFS2_DX_LEAF_SIGNATURE,
			sizeof(OCFS2_DX_LEAF_SIGNATURE))) {
		struct ocfs2_dx_leaf *dx_leaf;
		dx_leaf = (struct ocfs2_dx_leaf *)block;
		check = dx_leaf->dl_check;
		snprintf(name, sizeof(name), OCFS2_DX_LEAF_SIGNATURE);
	} else {
		if (ocfs2_supports_dir_trailer(fs)) {
			struct ocfs2_dir_block_trailer *trailer;
			trailer = ocfs2_dir_trailer_from_block(fs, block);
			if (!strncmp((char *)trailer->db_signature,
					OCFS2_DIR_TRAILER_SIGNATURE,
					sizeof(OCFS2_DIR_TRAILER_SIGNATURE))) {
				check = trailer->db_check;
				snprintf(name, sizeof(name),
					OCFS2_DIR_TRAILER_SIGNATURE);
			}
		} else {
			snprintf(name, sizeof(name),
				"Unknow: 0x%x%x%x%x%x%x%x%x\n",
				signature[0], signature[1],
				signature[2], signature[3],
				signature[4], signature[5],
				signature[6], signature[7]);
			do_check = 0;
		}
	}

	fprintf(stderr, "Signature of block #%"PRIu64" on "
		"device %s : \"%s\"\n", blk, dev, name);

	/* modified from ocfs2_block_check_validate(),
	 * rested code is only display format related  */
	if (do_check) {
		struct ocfs2_block_check new_check;
		uint32_t crc, ecc;
		int crc_offset, result_offset, offset;
		char outbuf[256] = {0,};

		new_check.bc_crc32e = le32_to_cpu(check.bc_crc32e);
		new_check.bc_ecc = le16_to_cpu(check.bc_ecc);
		memset(&check, 0, sizeof(struct ocfs2_block_check));

		crc_offset = snprintf(outbuf, sizeof(outbuf),
				"Block %4"PRIu64"    ", blk);
		result_offset = snprintf(outbuf + crc_offset,
					sizeof(outbuf) - crc_offset,
					"CRC32: %.8"PRIx32"    "
					"ECC: %.4"PRIx16"    ",
					new_check.bc_crc32e, new_check.bc_ecc);
		result_offset += crc_offset;

		/* Fast path - if the crc32 validates, we're good to go */
		crc = crc32_le(~0, (void *)block, fs->fs_blocksize);
		if (crc == new_check.bc_crc32e) {
			snprintf(outbuf + result_offset,
				sizeof(outbuf) - result_offset, "PASS\n");
			fprintf(stderr, outbuf);
			goto do_check_end;
		}

		/* OK, try ECC fixups */
		ecc = ocfs2_hamming_encode_block(block, fs->fs_blocksize);
		ocfs2_hamming_fix_block(block, fs->fs_blocksize,
					ecc ^ new_check.bc_ecc);

		crc = crc32_le(~0, (void *)block, fs->fs_blocksize);
		if (crc == new_check.bc_crc32e) {
			snprintf(outbuf + result_offset,
				sizeof(outbuf) - result_offset, "ECC Fixup\n");
			fprintf(stderr, outbuf);
			goto do_check_end;
		}

		snprintf(outbuf + result_offset,
			sizeof(outbuf) - result_offset, "FAIL\n");
		fprintf(stderr, outbuf);

		offset = snprintf(outbuf, sizeof(outbuf), "Calculated");
		while (offset < crc_offset)
			outbuf[offset++] = ' ';
		snprintf(outbuf + crc_offset, sizeof(outbuf) - crc_offset,
			"CRC32: %.8"PRIx32"    ECC: %.4"PRIx16"\n",
			crc, ecc);
		fprintf(stderr, outbuf);
		err = -1;
do_check_end:
		check.bc_crc32e = cpu_to_le32(new_check.bc_crc32e);
		check.bc_ecc = cpu_to_le16(new_check.bc_ecc);
	}

	return err;
}

int main(int argc, char *argv[])
{
	errcode_t err;
	int ret = 1;
	int force = 0;
	ocfs2_filesys *fs;
	char *dev, *block;
	uint64_t blkno;
	int c;

	static struct option long_options[] = {
		{"force", 0, 0, 'F'},
		{0, 0, 0, 0}
	};

	while (1) {
		c = getopt_long(argc, argv, "F", long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'F':
			force = 1;
			break;
		default:
			print_usage();
			break;
		}
	}
	if (optind != (argc - 2))
		print_usage();

	initialize_ocfs_error_table();

	dev = argv[optind];
	blkno = read_number(argv[optind + 1]);
	if (blkno == 0) {
		fprintf(stderr, "invalid block number\n");
		print_usage();
	}

	err = ocfs2_open(dev, OCFS2_FLAG_RO, 0, 0, &fs);
	if (err) {
		com_err(argv[0], err,
			"while opening device \"%s\"", dev);
		goto out;
	}

	if (!ocfs2_meta_ecc(OCFS2_RAW_SB(fs->fs_super))) {
		fprintf(stderr,
			"metaecc feature is not enabled on volume %s, "
			"validation might be invalid.\n", dev);
		if (!force) {
			fprintf(stderr,
				"To skip this check, use --force or -F\n");
			goto out;
		}
	}

	err = ocfs2_malloc_block(fs->fs_io, &block);
	if (err) {
		com_err(argv[0], err,
			"while reading block #%"PRIu64" on \"%s\"\n",
			blkno, dev);
		goto out_close;
	}

	err = ocfs2_read_blocks(fs, blkno, 1, block);
	if (err) {
		com_err(argv[0], err,
			"while reading block #%"PRIu64" on \"%s\"\n",
			blkno, dev);
		goto out_free;
	}

	err = check_metaecc(fs, blkno, dev, block);

out_free:
	ocfs2_free(&block);
out_close:
	err = ocfs2_close(fs);
	if (err) {
		com_err(argv[0], err,
			"while closing device \"%s\"", dev);
		ret = 1;
	}
out:
	return ret;
}

