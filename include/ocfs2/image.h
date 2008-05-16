/* -*- mode: c; c-basic-offset: 8; -*- * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * image.h
 *
 * Header file describing image disk/memory structures for ocfs2 image
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

/*
 * o2image is an ocfs2 tool to save critical ocfs2 filesystem metadata to a
 * specified image-file. Image-file may be examined using debugfs.ocfs2 or
 * may be used to restore using o2image. Image-file can be of two formats:
 *
 * 1. Packed - This format(default) contains a ocfs2 image header, packed
 * 		metadata blocks and a bitmap.
 * 2. raw    - A raw image is a sparse file containing the metadata blocks.
 *
 * 		Usage: o2image [-rI] <device> <imagefile>
 *
 * Packed format contains bitmap towards the end of the image-file. Each bit in
 * the bitmap represents a block in the filesystem.
 *
 * When the packed image is opened using o2image or debugfs.ocfs2, bitmap is
 * loaded into memory and used to map disk blocks to image blocks.
 *
 * Raw image is a sparse file containing metadata blocks at the same offset as
 * the filesystem.
 *
 * debugfs.ocfs2 is modified to detect image-file when the image-file is
 * specified with -i option.
 */

#define OCFS2_IMAGE_MAGIC		0x72a3d45f
#define OCFS2_IMAGE_DESC 		"OCFS2 IMAGE"
#define OCFS2_IMAGE_VERSION		1
#define OCFS2_IMAGE_READ_CHAIN_NO	0
#define OCFS2_IMAGE_READ_INODE_NO	1
#define OCFS2_IMAGE_READ_INODE_YES	2
#define OCFS2_IMAGE_BITMAP_BLOCKSIZE	4096
#define OCFS2_IMAGE_BITS_IN_BLOCK	(OCFS2_IMAGE_BITMAP_BLOCKSIZE * 8)

/* on disk ocfs2 image header format */
struct ocfs2_image_hdr {
	__le32	hdr_magic;
	__le32	hdr_timestamp;		/* Time of image creation */
	__u8	hdr_magic_desc[16];	/* "OCFS2 IMAGE" */
	__le64	hdr_version;		/* ocfs2 image version */
	__le64	hdr_fsblkcnt;		/* blocks in filesystem */
	__le64	hdr_fsblksz;		/* Filesystem block size */
	__le64	hdr_imgblkcnt;		/* Filesystem blocks in image */
	__le64	hdr_bmpblksz;		/* bitmap block size */
	__le64	hdr_superblkcnt;	/* number of super blocks */
	__le64	hdr_superblocks[OCFS2_MAX_BACKUP_SUPERBLOCKS];
};

/*
 * array to hold pointers to bitmap blocks. arr_set_bit_cnt holds cumulative
 * count of bits used previous to the current block. arr_self will be pointing
 * to the memory chunks allocated. arr_map will be pointing to bitmap blocks
 * of size OCFS2_IMAGE_BITMAP_BLOCKSIZE. Each block maps to
 * OCFS2_IMAGE_BITS_IN_BLOCK number of filesystem blocks.
 */
struct _ocfs2_image_bitmap_arr {
	uint64_t	arr_set_bit_cnt;
	char		*arr_self;
	char    	*arr_map;
};
typedef struct _ocfs2_image_bitmap_arr ocfs2_image_bitmap_arr;

/* ocfs2 image state holds runtime values */
struct ocfs2_image_state {
	uint64_t	ost_fsblksz;
	uint64_t	ost_fsblkcnt;
	uint64_t	ost_imgblkcnt;	/* filesystem blocks in image */
	uint64_t 	ost_glbl_bitmap_inode;
	uint64_t 	ost_glbl_inode_alloc;
	uint64_t 	*ost_inode_allocs; 	/* holds inode_alloc inodes */
	uint64_t	ost_bmpblks; 		/* blocks that store bitmaps */
	uint64_t	ost_bmpblksz; 		/* size of each bitmap blk */
	uint64_t	ost_superblocks[OCFS2_MAX_BACKUP_SUPERBLOCKS];
	int		ost_glbl_inode_traversed;
	int		ost_bpc; 		/* blocks per cluster */
	int 		ost_superblkcnt; 	/* number of super blocks */
	ocfs2_image_bitmap_arr	*ost_bmparr; 	/* points to bitmap blocks */
};

errcode_t ocfs2_image_load_bitmap(ocfs2_filesys *ofs);
errcode_t ocfs2_image_free_bitmap(ocfs2_filesys *ofs);
errcode_t ocfs2_image_alloc_bitmap(ocfs2_filesys *ofs);
void ocfs2_image_mark_bitmap(ocfs2_filesys *ofs, uint64_t blkno);
int ocfs2_image_test_bit(ocfs2_filesys *ofs, uint64_t blkno);
uint64_t ocfs2_image_get_blockno(ocfs2_filesys *ofs, uint64_t blkno);
void ocfs2_image_swap_header(struct ocfs2_image_hdr *hdr);
