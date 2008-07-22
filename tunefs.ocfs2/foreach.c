/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * foreach.c
 *
 * Foreach function for inodes.
 *
 * Copyright (C) 2007,2008 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#define _LARGEFILE64_SOURCE

#include "ocfs2/ocfs2.h"

#include "libocfs2ne.h"

static errcode_t tunefs_validate_inode(ocfs2_filesys *fs,
				       struct ocfs2_dinode *di)
{
	if (memcmp(di->i_signature, OCFS2_INODE_SIGNATURE,
		    strlen(OCFS2_INODE_SIGNATURE)))
		return OCFS2_ET_BAD_INODE_MAGIC;

	ocfs2_swap_inode_to_cpu(di);

	if (di->i_fs_generation != fs->fs_super->i_fs_generation)
		return OCFS2_ET_INODE_NOT_VALID;

	if (!(di->i_flags & OCFS2_VALID_FL))
		return OCFS2_ET_INODE_NOT_VALID;

	return 0;
}

errcode_t tunefs_foreach_inode(ocfs2_filesys *fs, int filetype_mask,
			       errcode_t (*func)(ocfs2_filesys *fs,
						 struct ocfs2_dinode *di,
						 void *user_data),
			       void *user_data)
{
	errcode_t ret;
	uint64_t blkno;
	char *buf;
	struct ocfs2_dinode *di;
	ocfs2_inode_scan *scan;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret) {
		verbosef(VL_LIB,
			 "%s while allocating a buffer for inode scanning\n",
			 error_message(ret));
		goto out;
	}

	di = (struct ocfs2_dinode *)buf;

	ret = ocfs2_open_inode_scan(fs, &scan);
	if (ret) {
		verbosef(VL_LIB,
			 "%s while opening inode scan\n",
			 error_message(ret));
		goto out_free;
	}

	for(;;) {
		ret = ocfs2_get_next_inode(scan, &blkno, buf);
		if (ret) {
			verbosef(VL_LIB, "%s while getting next inode\n",
				 error_message(ret));
			break;
		}
		if (blkno == 0)
			break;

		ret = tunefs_validate_inode(fs, di);
		if (ret)
			continue;

		if (di->i_flags & OCFS2_SYSTEM_FL)
			continue;

		if (!func)
			continue;

		if (di->i_mode & filetype_mask) {
			ret = func(fs, di, user_data);
			if (ret)
				break;
		}
	}

	ocfs2_close_inode_scan(scan);
out_free:
	ocfs2_free(&buf);

out:
	return ret;
}

