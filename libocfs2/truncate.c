/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * truncate.c
 *
 * Truncate an OCFS2 inode.  For the OCFS2 userspace library.
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
 */

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE

#include <string.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "ocfs2.h"

/*
 * We let ocfs2_extent_iterate() update the tree for us by
 * altering the records beyond the new size and returning
 * _CHANGED.  It will delete unused extent blocks and file
 * data for us.  This only works with DEPTH_TRAVERSE..
 */
static int truncate_iterate(ocfs2_filesys *fs,
			    struct ocfs2_extent_rec *rec,
			    int tree_depth, uint32_t ccount,
			    uint64_t ref_blkno, int ref_recno,
			    void *priv_data)
{
	uint64_t new_i_clusters = *(uint64_t *)priv_data;

	if ((rec->e_cpos + rec->e_clusters) <= new_i_clusters)
		return 0;

	if (rec->e_cpos >= new_i_clusters) {
		/* the rec is entirely outside the new size, free it */
		rec->e_blkno = 0;
		rec->e_clusters = 0;
		rec->e_cpos = 0;
	} else {
		/* we're truncating into the middle of the rec */
		rec->e_clusters = new_i_clusters - rec->e_cpos;
	}

	return OCFS2_EXTENT_CHANGED;
}

/* XXX care about zeroing new clusters and final partially truncated 
 * clusters */
errcode_t ocfs2_truncate(ocfs2_filesys *fs, uint64_t ino, uint64_t new_i_size)
{
	errcode_t ret;
	char *buf;
	struct ocfs2_dinode *di;
	uint32_t new_i_clusters;
	uint64_t new_i_blocks;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_read_inode(fs, ino, buf);
	if (ret)
		goto out;
	di = (struct ocfs2_dinode *)buf;

	if (di->i_size == new_i_size)
		goto out;

	new_i_blocks = ocfs2_blocks_in_bytes(fs, new_i_size);
	new_i_clusters = ocfs2_clusters_in_blocks(fs, new_i_blocks);

	if (di->i_clusters < new_i_clusters) {
		ret = ocfs2_extend_allocation(fs, ino,
					new_i_clusters - di->i_clusters);
		if (ret)
			goto out;
	} else {
		ret = ocfs2_extent_iterate_inode(fs, di,
						 OCFS2_EXTENT_FLAG_DEPTH_TRAVERSE,
						 NULL, truncate_iterate,
						 &new_i_clusters);
		if (ret)
			goto out;
	}

	di->i_clusters = new_i_clusters;
	di->i_size = new_i_size;
	ret = ocfs2_write_inode(fs, ino, buf);

out:
	ocfs2_free(&buf);

	return ret;
}
