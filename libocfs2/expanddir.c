/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * expanddir.c
 *
 * Expands an OCFS2 directory.  For the OCFS2 userspace library.
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
 * Authors: Sunil Mushran
 *
 *  This code is a port of e2fsprogs/lib/ext2fs/expanddir.c
 *  Copyright (C) 1993, 1999 Theodore Ts'o.
 */

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE

#include <stdio.h>
#include <string.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "ocfs2.h"


struct expand_dir_struct {
	ocfs2_dinode   *inode;
	int		done;
	int		newblocks;
	errcode_t	err;
};

/*
 * expand_dir_proc()
 *
 *  TODO expand_dir_proc needs to extend the allocation when needed
 */
static int expand_dir_proc(ocfs2_filesys *fs, uint64_t blocknr,
			   uint64_t blockcnt, void *priv_data)
{
	struct expand_dir_struct *es = (struct expand_dir_struct *) priv_data;
	ocfs2_dinode *inode;
	uint64_t i_size_in_blks;
	char *new_blk = NULL;
	errcode_t ret;

	inode = es->inode;

	i_size_in_blks = inode->i_size >>
	       			OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;

	if (i_size_in_blks == 0) {
		es->err = OCFS2_ET_DIR_CORRUPTED;
		return OCFS2_BLOCK_ABORT;
	}

	if (i_size_in_blks == blockcnt) {
		/* init new dir block */
		ret = ocfs2_new_dir_block(fs, 0, 0, &new_blk);
		if (ret) {
			es->err = ret;
			return OCFS2_BLOCK_ABORT;
		}

		/* write new dir block */
		ret = ocfs2_write_dir_block(fs, blocknr, new_blk);
		if (ret) {
			es->err = ret;
			return OCFS2_BLOCK_ABORT;
		}
		es->done = 1;
	}

	if (es->done)
		return OCFS2_BLOCK_CHANGED | OCFS2_BLOCK_ABORT;
	else
		return 0;

/***********************************************************************
	struct expand_dir_struct *es = (struct expand_dir_struct *) priv_data;
	blk_t	new_blk;
	static blk_t	last_blk = 0;
	char		*block;
	errcode_t	ret;

	if (*blocknr) {
		last_blk = *blocknr;
		return 0;
	}
	ret = ocfs2_new_block(fs, last_blk, 0, &new_blk);
	if (ret) {
		es->err = ret;
		return OCFS2_BLOCK_ABORT;
	}
	if (blockcnt > 0) {
		ret = ocfs2_new_dir_block(fs, 0, 0, &block);
		if (ret) {
			es->err = ret;
			return OCFS2_BLOCK_ABORT;
		}
		es->done = 1;
		ret = ocfs2_write_dir_block(fs, new_blk, block);
	} else {
		ret = ocfs2_malloc_block(fs->fs_io, (void **)&block);
		if (ret) {
			es->err = ret;
			return OCFS2_BLOCK_ABORT;
		}
		memset(block, 0, fs->blocksize);
		ret = io_channel_write_blk(fs->io, new_blk, 1, block);
	}	
	if (ret) {
		es->err = ret;
		return OCFS2_BLOCK_ABORT;
	}
	ext2fs_free_mem((void **) &block);
	*blocknr = new_blk;
	ext2fs_block_alloc_stats(fs, new_blk, +1);
	es->newblocks++;

	if (es->done)
		return (OCFS2_BLOCK_CHANGED | OCFS2_BLOCK_ABORT);
	else
		return OCFS2_BLOCK_CHANGED;
****************************************************************/
}

errcode_t ocfs2_expand_dir(ocfs2_filesys *fs, uint64_t dir)
{
	errcode_t ret = 0;
	struct expand_dir_struct es;
	ocfs2_dinode *inode;
	char *buf = NULL;

	if (!(fs->fs_flags & OCFS2_FLAG_RW))
		return OCFS2_ET_RO_FILESYS;

	/* ensure it is a dir */
	ret = ocfs2_check_directory(fs, dir);
	if (ret)
		return ret;

	/* malloc block to read inode */
	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;
	else
		inode = (ocfs2_dinode *)buf;

	/* read inode */
	ret = ocfs2_read_inode(fs, dir, (char *)inode);
	if (ret)
		goto bail;

	es.inode = inode;
	es.done = 0;
	es.err = 0;
	es.newblocks = 0;

	ret = ocfs2_block_iterate(fs, dir, OCFS2_BLOCK_FLAG_APPEND,
				  expand_dir_proc, &es);
	if (es.err)
		return es.err;
	if (!es.done)
		return OCFS2_ET_EXPAND_DIR_ERR;

	/*
	 * Update the size and block count fields in the inode.
	 */
	ret = ocfs2_read_inode(fs, dir, (char *)inode);
	if (ret)
		goto bail;

	inode->i_size += fs->fs_blocksize;
//	inode->i_blocks += (fs->fs_blocksize / 512) * es.newblocks;

	ret = ocfs2_write_inode(fs, dir, (char *)inode);
	if (ret)
		goto bail;

bail:
	if (buf)
		ocfs2_free(&buf);

	return ret;
}
