/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * fileio.c
 *
 * I/O to files.  Part of the OCFS2 userspace library.
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
 *
 * Ideas taken from e2fsprogs/lib/ext2fs/fileio.c
 *   Copyright (C) 1997 Theodore Ts'o.
 */

#define _XOPEN_SOURCE 600  /* Triggers XOPEN2K in features.h */
#define _LARGEFILE64_SOURCE

#include <string.h>
#include <limits.h>

#include "ocfs2.h"

struct read_whole_context {
	char		*buf;
	char		*ptr;
	int		size;
	int		offset;
	errcode_t	errcode;
};

static int read_whole_func(ocfs2_filesys *fs,
			   uint64_t blkno,
			   uint64_t bcount,
			   void *priv_data)
{
	struct read_whole_context *ctx = priv_data;

	ctx->errcode = io_read_block(fs->fs_io, blkno,
				     1, ctx->ptr);
	if (ctx->errcode)
		return OCFS2_BLOCK_ABORT;

	ctx->ptr += fs->fs_blocksize;
	ctx->offset += fs->fs_blocksize;

	return 0;
}

errcode_t ocfs2_read_whole_file(ocfs2_filesys *fs,
				uint64_t blkno,
				char **buf,
				int *len)
{
	struct read_whole_context	ctx;
	errcode_t			retval;
	char *inode_buf;
	ocfs2_dinode *di;
	int c_to_b_bits = 
		OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits -
		OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;

	/* So the caller can see nothing was read */
	*len = 0;
	*buf = NULL;

	retval = ocfs2_malloc_block(fs->fs_io, &inode_buf);
	if (retval)
		return retval;

	retval = ocfs2_read_inode(fs, blkno, inode_buf);
	if (retval)
		goto out_free;

	di = (ocfs2_dinode *)inode_buf;

	/* Arbitrary limit for our malloc */
	retval = OCFS2_ET_INVALID_ARGUMENT;
	if (di->i_size > INT_MAX) 
		goto out_free;

	retval = ocfs2_malloc_blocks(fs->fs_io,
				     di->i_clusters << c_to_b_bits,
				     buf);
	if (retval)
		goto out_free;

	ctx.buf = *buf;
	ctx.ptr = *buf;
	ctx.size = di->i_size;
	ctx.offset = 0;
	ctx.errcode = 0;
	retval = ocfs2_block_iterate(fs, blkno, 0,
				     read_whole_func, &ctx);

	*len = ctx.size;
	if (ctx.offset < ctx.size)
		*len = ctx.offset;

out_free:
	ocfs2_free(&inode_buf);

	if (!(*len)) {
		ocfs2_free(buf);
		*buf = NULL;
	}

	if (retval)
		return retval;
	return ctx.errcode;
}

