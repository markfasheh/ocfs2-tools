/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * backup_super.c
 *
 * Backup superblocks for an OCFS2 volume.
 *
 * Copyright (C) 2006 Oracle.  All rights reserved.
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
 */

#include <errno.h>
#include "ocfs2.h"

/* In case we don't have fs_blocksize, we will return
 * byte offsets and let the caller calculate them by itself.
 */
int ocfs2_get_backup_super_offset(ocfs2_filesys *fs,
				  uint64_t *offsets, size_t len)
{
	size_t i;
	uint64_t blkno;
	uint32_t blocksize;

	memset(offsets, 0, sizeof(uint64_t) * len);
	len = ocfs2_min(len, (size_t)OCFS2_MAX_BACKUP_SUPERBLOCKS);

	if (fs)
		blocksize = fs->fs_blocksize;
	else
		blocksize = 1;

	for (i = 0; i < len; i++) {
		blkno = ocfs2_backup_super_blkno(blocksize, i);
		if (fs && fs->fs_blocks <= blkno)
			break;

		offsets[i] = blkno;
	}
	return i;
}

static errcode_t check_cluster(ocfs2_bitmap *bitmap, uint64_t bit)
{
	errcode_t ret;
	int val;

	ret = ocfs2_bitmap_test(bitmap, bit, &val);
	if (ret)
		goto bail;

	if (val) {
		ret = ENOSPC;
		goto bail;
	}

	ret = 0;
bail:
	return ret;
}

errcode_t ocfs2_set_backup_super(ocfs2_filesys *fs,
				 uint64_t *blocks, size_t len)
{
	size_t i;
	errcode_t ret = 0;
	char *buf = NULL;
	uint64_t bm_blk, *blkno = blocks;
	int val;
	uint32_t cluster, bpc = fs->fs_clustersize / fs->fs_blocksize;

	if (!len || !blocks || !*blocks)
		goto bail;
	len = ocfs2_min(len,(size_t)OCFS2_MAX_BACKUP_SUPERBLOCKS);

	if (!fs->fs_cluster_alloc) {
		ret = ocfs2_lookup_system_inode(fs, GLOBAL_BITMAP_SYSTEM_INODE,
						0, &bm_blk);
		if (ret)
			goto bail;

		ret = ocfs2_read_cached_inode(fs, bm_blk, &fs->fs_cluster_alloc);
		if (ret)
			goto bail;

		ret = ocfs2_load_chain_allocator(fs, fs->fs_cluster_alloc);
		if (ret)
			goto bail;
	}

	if (!OCFS2_HAS_COMPAT_FEATURE(OCFS2_RAW_SB(fs->fs_super),
				      OCFS2_FEATURE_COMPAT_BACKUP_SB)) {
		/* check all the blkno to see whether it is used. */
		for (i = 0; i < len; i++, blkno++) {
			ret = check_cluster(fs->fs_cluster_alloc->ci_chains,
					ocfs2_blocks_to_clusters(fs, *blkno));
			if (ret)
				goto bail;
		}
	}

	ret = ocfs2_malloc_blocks(fs->fs_io, bpc, &buf);
	if (ret)
		goto bail;
	memset(buf, 0, fs->fs_clustersize);

	/* zero all the clusters at first */
	blkno = blocks;
	for (i = 0; i < len; i++, blkno++) {
		cluster = ocfs2_blocks_to_clusters(fs, *blkno);
		ret = io_write_block(fs->fs_io, cluster*bpc, bpc, buf);
		if (ret)
			goto bail;
	}

	ret = ocfs2_refresh_backup_super(fs, blocks, len);
	if (ret)
		goto bail;

	blkno = blocks;
	for (i = 0; i < len; i++, blkno++)
		ocfs2_bitmap_set(fs->fs_cluster_alloc->ci_chains,
				 ocfs2_blocks_to_clusters(fs, *blkno), &val);

	ret = ocfs2_write_chain_allocator(fs, fs->fs_cluster_alloc);

bail:
	if (buf)
		ocfs2_free(&buf);
	if (fs->fs_cluster_alloc) {
		ocfs2_free_cached_inode(fs, fs->fs_cluster_alloc);
		fs->fs_cluster_alloc = NULL;
	}
	return ret;
}

errcode_t ocfs2_refresh_backup_super(ocfs2_filesys *fs,
				     uint64_t *blocks, size_t len)
{
	errcode_t ret = 0;
	size_t i;

	for (i = 0; i < len; i++, blocks++) {
		ret = ocfs2_write_backup_super(fs, *blocks);
		if (ret)
			goto bail;
	}

bail:
	return ret;
}
