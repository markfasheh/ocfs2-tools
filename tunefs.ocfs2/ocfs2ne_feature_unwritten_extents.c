/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * ocfs2ne_feature_unwritten_extents.c
 *
 * ocfs2 tune utility for enabling and disabling the unwritten extents
 * feature.
 *
 * Copyright (C) 2004, 2008 Oracle.  All rights reserved.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>
#include <inttypes.h>
#include <assert.h>

#include "ocfs2/ocfs2.h"

#include "libocfs2ne.h"


static int enable_unwritten_extents(ocfs2_filesys *fs, int flags)
{
	errcode_t ret = 0;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);

	if (ocfs2_writes_unwritten_extents(super)) {
		verbosef(VL_APP,
			 "Unwritten extents feature is already enabled; "
			 "nothing to enable\n");
		goto out;
	}

	if (!ocfs2_sparse_alloc(super)) {
		errorf("Sparse files are not enabled on device \"%s\"; "
		       "unwritten extents cannot be enabled\n",
		       fs->fs_devname);
		ret = TUNEFS_ET_SPARSE_MISSING;
		goto out;
	}

	if (!tunefs_interact("Enable the unwritten extents feature on "
			     "device \"%s\"? ",
			     fs->fs_devname))
		goto out;

	OCFS2_SET_RO_COMPAT_FEATURE(super,
				    OCFS2_FEATURE_RO_COMPAT_UNWRITTEN);
	tunefs_block_signals();
	ret = ocfs2_write_super(fs);
	tunefs_unblock_signals();
	if (ret)
		tcom_err(ret, "while writing out the superblock");

out:
	return ret;
}

static errcode_t empty_clusters(ocfs2_filesys *fs,
				uint64_t start_blk,
				uint32_t num_clusters)
{
	errcode_t ret;
	char *buf = NULL;
	uint16_t bpc = fs->fs_clustersize / fs->fs_blocksize;
	uint64_t total_blocks = bpc * num_clusters;
	uint64_t io_blocks = total_blocks;

	ret = ocfs2_malloc_blocks(fs->fs_io, io_blocks, &buf);
	if (ret == OCFS2_ET_NO_MEMORY) {
		io_blocks = bpc;
		ret = ocfs2_malloc_blocks(fs->fs_io, io_blocks, &buf);
	}
	if (ret)
		goto bail;

	memset(buf, 0, io_blocks * fs->fs_blocksize);

	while (total_blocks) {
		ret = io_write_block(fs->fs_io, start_blk, io_blocks, buf);
		if (ret)
			goto bail;

		total_blocks -= io_blocks;
		start_blk += io_blocks;
	}

bail:
	if (buf)
		ocfs2_free(&buf);

	return ret;
}

static errcode_t unwritten_iterate(ocfs2_filesys *fs,
				   struct ocfs2_dinode *di,
				   void *unused)
{
	errcode_t ret;
	uint32_t clusters, v_cluster = 0, p_cluster, num_clusters;
	uint16_t extent_flags;
	uint64_t p_blkno;
	ocfs2_cached_inode *ci = NULL;

	clusters = (di->i_size + fs->fs_clustersize -1 ) /
			fs->fs_clustersize;

	ret = ocfs2_read_cached_inode(fs, di->i_blkno, &ci);
	if (ret)
		goto bail;

	while (v_cluster < clusters) {
		ret = ocfs2_get_clusters(ci,
					 v_cluster, &p_cluster,
					 &num_clusters, &extent_flags);
		if (ret)
			break;

		if (extent_flags & OCFS2_EXT_UNWRITTEN) {
			p_blkno = ocfs2_clusters_to_blocks(fs, p_cluster);
			ret = empty_clusters(fs, p_blkno, num_clusters);
			if (ret)
				break;

			tunefs_block_signals();
			ret = ocfs2_mark_extent_written(fs, di, v_cluster,
							num_clusters,
							p_blkno);
			tunefs_unblock_signals();
			if (ret)
				break;
		}

		v_cluster += num_clusters;
	}

bail:
	if (ci)
		ocfs2_free_cached_inode(fs, ci);
	return ret;
}

static errcode_t clear_unwritten_extents(ocfs2_filesys *fs)
{
	return tunefs_foreach_inode(fs, S_IFREG, unwritten_iterate, NULL);
}

static int disable_unwritten_extents(ocfs2_filesys *fs, int flags)
{
	errcode_t ret = 0;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);

	if (!ocfs2_writes_unwritten_extents(super)) {
		verbosef(VL_APP,
			 "Unwritten extents feature is not enabled; "
			 "nothing to disable\n");
		goto out;
	}

	if (!tunefs_interact("Disable the unwritten extents feature on "
			     "device \"%s\"? ",
			     fs->fs_devname))
		goto out;

	ret = clear_unwritten_extents(fs);
	if (ret) {
		tcom_err(ret,
			 "while trying to clear the unwritten extents on "
			 "device \"%s\"",
			 fs->fs_devname);
		goto out;
	}

	OCFS2_CLEAR_RO_COMPAT_FEATURE(super,
				      OCFS2_FEATURE_RO_COMPAT_UNWRITTEN);
	tunefs_block_signals();
	ret = ocfs2_write_super(fs);
	tunefs_unblock_signals();
	if (ret)
		tcom_err(ret, "while writing out the superblock");

out:
	return ret;
}

DEFINE_TUNEFS_FEATURE_RO_COMPAT(unwritten_extents,
				OCFS2_FEATURE_RO_COMPAT_UNWRITTEN,
				TUNEFS_FLAG_RW | TUNEFS_FLAG_ALLOCATION,
				enable_unwritten_extents,
				disable_unwritten_extents);

#ifdef DEBUG_EXE
int main(int argc, char *argv[])
{
	return tunefs_feature_main(argc, argv, &unwritten_extents_feature);
}
#endif
