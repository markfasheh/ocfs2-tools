/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * cached_inode.c
 *
 * Cache inode structure for the OCFS2 userspace library.
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301 USA.
 */

#define _XOPEN_SOURCE 600  /* Triggers XOPEN2K in features.h */
#define _LARGEFILE64_SOURCE

#include <string.h>

#include "ocfs2/ocfs2.h"

errcode_t ocfs2_read_cached_inode(ocfs2_filesys *fs, uint64_t blkno,
				  ocfs2_cached_inode **ret_ci)
{
	errcode_t ret;
	char *blk;
	ocfs2_cached_inode *cinode;

	if ((blkno < OCFS2_SUPER_BLOCK_BLKNO) ||
	    (blkno > fs->fs_blocks))
		return OCFS2_ET_BAD_BLKNO;

	ret = ocfs2_malloc0(sizeof(ocfs2_cached_inode), &cinode);
	if (ret)
		return ret;

	cinode->ci_fs = fs;
	cinode->ci_blkno = blkno;

	ret = ocfs2_malloc_block(fs->fs_io, &blk);
	if (ret)
		goto cleanup;

	cinode->ci_inode = (struct ocfs2_dinode *)blk;

	ret = ocfs2_read_inode(fs, blkno, blk);
	if (ret)
		goto cleanup;

	*ret_ci = cinode;

	return 0;

cleanup:
	ocfs2_free_cached_inode(fs, cinode);

	return ret;
}

errcode_t ocfs2_free_cached_inode(ocfs2_filesys *fs,
				  ocfs2_cached_inode *cinode)
{
	if (!cinode)
		return OCFS2_ET_INVALID_ARGUMENT;
	
	if (cinode->ci_chains)
		ocfs2_bitmap_free(&cinode->ci_chains);

	if (cinode->ci_inode)
		ocfs2_free(&cinode->ci_inode);

	ocfs2_free(&cinode);

	return 0;
}

errcode_t ocfs2_write_cached_inode(ocfs2_filesys *fs,
				   ocfs2_cached_inode *cinode)
{
	errcode_t ret;

	if (!(fs->fs_flags & OCFS2_FLAG_RW))
		return OCFS2_ET_RO_FILESYS;

	if ((cinode->ci_blkno < OCFS2_SUPER_BLOCK_BLKNO) ||
	    (cinode->ci_blkno > fs->fs_blocks))
		return OCFS2_ET_BAD_BLKNO;

	ret = ocfs2_write_inode(fs, cinode->ci_blkno,
				(char *)cinode->ci_inode);

	return ret;
}

errcode_t ocfs2_refresh_cached_inode(ocfs2_filesys *fs,
				     ocfs2_cached_inode *cinode)
{
	if (cinode->ci_chains) {
		ocfs2_bitmap_free(&cinode->ci_chains);
		cinode->ci_chains = NULL;
	}

	return ocfs2_read_inode(fs, cinode->ci_blkno,
				(char *)cinode->ci_inode);
}
