/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * filesys.h
 *
 * Filesystem object routines for the OCFS2 userspace library.
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

#ifndef _FILESYS_H
#define _FILESYS_H

#define OCFS2_LIB_FEATURE_INCOMPAT_SUPP		OCFS2_FEATURE_INCOMPAT_SUPP
#define OCFS2_LIB_FEATURE_RO_COMPAT_SUPP	OCFS2_FEATURE_RO_COMPAT_SUPP

/* Flags for the ocfs2_filesys structure */
#define OCFS2_FLAG_RO		0x00
#define OCFS2_FLAG_RW		0x01
#define OCFS2_FLAG_CHANGED	0x02
#define OCFS2_FLAG_DIRTY	0x04
#define OCFS2_FLAG_SWAP_BYTES	0x08

/* Return flags for the extent iterator functions */
#define OCFS2_EXTENT_CHANGED	0x01
#define OCFS2_EXTENT_ABORT	0x02
#define OCFS2_EXTENT_ERROR	0x04

/*
 * Extent iterate flags
 *
 * OCFS2_EXTENT_FLAG_APPEND indicates that the iterator function should
 * be called on extents past the leaf next_free_rec.  This is used by
 * ocfs2_expand_dir() to add a new extent to a directory (via
 * OCFS2_BLOCK_FLAG_APPEND and the block iteration functions).
 *
 * OCFS2_EXTENT_FLAG_DEPTH_TRAVERSE indicates that the iterator
 * function for tree_depth > 0 records (ocfs2_extent_blocks, iow)
 * should be called after all of the extents contained in the
 * extent_block are processed.  This is useful if you are going to be
 * deallocating extents.
 *
 * OCFS2_EXTENT_FLAG_DATA_ONLY indicates that the iterator function
 * should be called for data extents (depth == 0) only.
 */
#define OCFS2_EXTENT_FLAG_APPEND		0x01
#define OCFS2_EXTENT_FLAG_DEPTH_TRAVERSE	0x02
#define OCFS2_EXTENT_FLAG_DATA_ONLY		0x04


/* Return flags for the block iterator functions */
#define OCFS2_BLOCK_CHANGED	0x01
#define OCFS2_BLOCK_ABORT	0x02
#define OCFS2_BLOCK_ERROR	0x03

/*
 * Block iterate flags
 *
 * In OCFS2, block iteration runs through the blocks contained in an
 * inode's data extents.  As such, "DATA_ONLY" and "DEPTH_TRAVERSE"
 * can't really apply.
 * 
 * OCFS2_BLOCK_FLAG_APPEND is as OCFS2_EXTENT_FLAG_APPEND, except on a
 * blocksize basis.  This may mean that the underlying extent already
 * contains the space for a new block, and i_size is updated
 * accordingly.
 */
#define OCFS2_BLOCK_FLAG_APPEND		0x01


typedef struct _ocfs2_filesys ocfs2_filesys;

struct _ocfs2_filesys {
	char *fs_devname;
	uint32_t fs_flags;
	io_channel *fs_io;
	ocfs2_dinode *fs_super;
	ocfs2_dinode *fs_orig_super;
	unsigned int fs_blocksize;
	unsigned int fs_clustersize;
	uint64_t fs_blocks;
	uint32_t fs_umask;

	/* Reserved for the use of the calling application. */
	void *fs_private;
};


errcode_t ocfs2_open(const char *name, int flags, int superblock,
		     unsigned int blksize, ocfs2_filesys **ret_fs);
errcode_t ocfs2_flush(ocfs2_filesys *fs);
errcode_t ocfs2_close(ocfs2_filesys *fs);
void ocfs2_freefs(ocfs2_filesys *fs);

errcode_t ocfs2_read_inode(ocfs2_filesys *fs, uint64_t blkno,
			   char *inode_buf);
errcode_t ocfs2_write_inode(ocfs2_filesys *fs, uint64_t blkno,
			    char *inode_buf);

errcode_t ocfs2_create_journal_superblock(ocfs2_filesys *fs,
					  uint32_t size, int flags,
					  char **ret_jsb);

errcode_t ocfs2_read_extent_block(ocfs2_filesys *fs, uint64_t blkno,
       				  char *eb_buf);
errcode_t ocfs2_write_extent_block(ocfs2_filesys *fs, uint64_t blkno,
       				   char *eb_buf);
errcode_t ocfs2_extent_iterate(ocfs2_filesys *fs,
			       uint64_t blkno,
			       int flags,
			       char *block_buf,
			       int (*func)(ocfs2_filesys *fs,
					   ocfs2_extent_rec *rec,
					   int tree_depth,
					   uint32_t ccount,
					   uint64_t ref_blkno,
					   int ref_recno,
					   void *priv_data),
			       void *priv_data);

#endif  /* _FILESYS_H */

