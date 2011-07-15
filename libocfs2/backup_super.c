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
#include "ocfs2/ocfs2.h"

/* In case we don't have fs_blocksize, we will return
 * byte offsets and let the caller calculate them by itself.
 */
int ocfs2_get_backup_super_offsets(ocfs2_filesys *fs,
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

errcode_t ocfs2_clear_backup_super_list(ocfs2_filesys *fs,
					uint64_t *blocks, size_t len)
{
	size_t i;
	errcode_t ret = 0;

	if (!len || !blocks || !*blocks)
		goto bail;
	len = ocfs2_min(len,(size_t)OCFS2_MAX_BACKUP_SUPERBLOCKS);

	/*
	 * Don't clear anything if backup superblocks aren't enabled -
	 * there might be real data there!  If backup superblocks are
	 * enabled, we know these blocks are backups, and we're
	 * safe to clear them.
	 */
	if (!OCFS2_HAS_COMPAT_FEATURE(OCFS2_RAW_SB(fs->fs_super),
				      OCFS2_FEATURE_COMPAT_BACKUP_SB))
		goto bail;


	for (i = 0; i < len; i++) {
		ret = ocfs2_free_clusters(fs, 1, blocks[i]);
		if (ret)
			break;
	}

bail:
	return ret;
}

static errcode_t check_cluster(ocfs2_filesys *fs, uint32_t cpos)
{
	errcode_t ret;
	int val;

	ret = ocfs2_test_cluster_allocated(fs, cpos, &val);
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

errcode_t ocfs2_set_backup_super_list(ocfs2_filesys *fs,
				      uint64_t *blocks, size_t len)
{
	size_t i;
	errcode_t ret = 0;
	char *buf = NULL;
	uint64_t *blkno = blocks;
	uint32_t cluster, bpc = fs->fs_clustersize / fs->fs_blocksize;

	if (!len || !blocks || !*blocks)
		goto bail;
	len = ocfs2_min(len,(size_t)OCFS2_MAX_BACKUP_SUPERBLOCKS);

	if (!OCFS2_HAS_COMPAT_FEATURE(OCFS2_RAW_SB(fs->fs_super),
				      OCFS2_FEATURE_COMPAT_BACKUP_SB)) {
		/* check all the blkno to see whether it is used. */
		for (i = 0; i < len; i++, blkno++) {
			ret = check_cluster(fs,
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

	ret = ocfs2_refresh_backup_super_list(fs, blocks, len);
	if (ret)
		goto bail;

	/* We just tested the clusters, so the allocation can't fail */
	blkno = blocks;
	for (i = 0; i < len; i++, blkno++)
		ocfs2_new_specific_cluster(fs, ocfs2_blocks_to_clusters(fs, *blkno));

bail:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

errcode_t ocfs2_refresh_backup_super_list(ocfs2_filesys *fs,
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

errcode_t ocfs2_refresh_backup_supers(ocfs2_filesys *fs)
{
	int num;
	uint64_t blocks[OCFS2_MAX_BACKUP_SUPERBLOCKS];

	if (!OCFS2_HAS_COMPAT_FEATURE(OCFS2_RAW_SB(fs->fs_super),
				      OCFS2_FEATURE_COMPAT_BACKUP_SB))
		return 0;  /* Do nothing */

	num = ocfs2_get_backup_super_offsets(fs, blocks,
					     ARRAY_SIZE(blocks));
	return num ? ocfs2_refresh_backup_super_list(fs, blocks, num) : 0;
}

errcode_t ocfs2_read_backup_super(ocfs2_filesys *fs, int backup, char *sbbuf)
{
	int numsb;
	uint64_t blocks[OCFS2_MAX_BACKUP_SUPERBLOCKS];

	if (!OCFS2_HAS_COMPAT_FEATURE(OCFS2_RAW_SB(fs->fs_super),
				      OCFS2_FEATURE_COMPAT_BACKUP_SB))
		return OCFS2_ET_NO_BACKUP_SUPER;

	numsb = ocfs2_get_backup_super_offsets(fs, blocks,
					       ARRAY_SIZE(blocks));
	if (backup < 0 || backup >= numsb)
		return OCFS2_ET_NO_BACKUP_SUPER;

	return ocfs2_read_super(fs, blocks[backup], sbbuf);
}


/* These were terrible names, don't use them */
int ocfs2_get_backup_super_offset(ocfs2_filesys *fs,
				  uint64_t *offsets, size_t len)
{
	return ocfs2_get_backup_super_offsets(fs, offsets, len);
}
errcode_t ocfs2_refresh_backup_super(ocfs2_filesys *fs,
				     uint64_t *blocks, size_t len)
{
	return ocfs2_refresh_backup_super_list(fs, blocks, len);
}
errcode_t ocfs2_set_backup_super(ocfs2_filesys *fs,
				 uint64_t *blocks, size_t len)
{
	return ocfs2_set_backup_super_list(fs, blocks, len);
}


