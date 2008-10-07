/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * slot_recovery.c
 *
 * Slot recovery handler.
 *
 * Copyright (C) 2008 Oracle.  All rights reserved.
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

#include <ocfs2/bitops.h>
#include "util.h"
#include "slot_recovery.h"

static errcode_t ocfs2_clear_truncate_log(ocfs2_filesys *fs,
					  struct ocfs2_dinode *di,
					  int slot)
{
	errcode_t ret = 0;
	struct ocfs2_truncate_log *tl;
	struct ocfs2_truncate_rec *tr;
	int i, was_set = 0, cleared = 0;
	int max = ocfs2_truncate_recs_per_inode(fs->fs_blocksize);
	uint64_t blkno;

	if (!(di->i_flags & OCFS2_VALID_FL) ||
	    !(di->i_flags & OCFS2_SYSTEM_FL) ||
	    !(di->i_flags & OCFS2_DEALLOC_FL))
		return OCFS2_ET_INVALID_ARGUMENT;

	tl = &di->id2.i_dealloc;

	if (tl->tl_used > max)
		return OCFS2_ET_INTERNAL_FAILURE;

	for (i = 0; i < tl->tl_used; i++) {
		tr = &tl->tl_recs[i];

		if (tr->t_start == 0)
			continue;

		blkno = ocfs2_clusters_to_blocks(fs, tr->t_start);

		ret = ocfs2_test_clusters(fs, tr->t_clusters,
					  blkno, 1, &was_set);
		if (ret)
			goto bail;

		if (!was_set) {
			ret = OCFS2_ET_INVALID_BIT;
			goto bail;
		}

		ret = ocfs2_free_clusters(fs, tr->t_clusters, blkno);
		if (ret)
			goto bail;

		cleared = 1;
	}

	tl->tl_used = 0;
	memset(tl->tl_recs, 0, fs->fs_blocksize -
	       offsetof(struct ocfs2_dinode, id2.i_dealloc.tl_recs));
	ret = ocfs2_write_inode(fs, di->i_blkno, (char *)di);
	if (!ret && cleared)
		printf("Slot %d's truncate log replayed successfully\n", slot);

bail:
	return ret;
}

errcode_t o2fsck_replay_truncate_logs(ocfs2_filesys *fs)
{
	return handle_slots_system_file(fs,
					TRUNCATE_LOG_SYSTEM_INODE,
					ocfs2_clear_truncate_log);
}

static errcode_t ocfs2_clear_local_alloc(ocfs2_filesys *fs,
					 struct ocfs2_dinode *di,
					 int slot)
{
	errcode_t ret = 0;
	int bit_off, left, count, start, was_set = 0, cleared = 0;
	uint64_t la_start_blk;
	uint64_t blkno;
	void *bitmap;
	struct ocfs2_local_alloc *la;

	if (!(di->i_flags & OCFS2_VALID_FL) ||
	    !(di->i_flags & OCFS2_SYSTEM_FL) ||
	    !(di->i_flags & OCFS2_BITMAP_FL))
		return OCFS2_ET_INVALID_ARGUMENT;

	if (!di->id1.bitmap1.i_total)
		goto bail;

	la = &di->id2.i_lab;

	if (di->id1.bitmap1.i_used == di->id1.bitmap1.i_total)
		goto clear_inode;

	la_start_blk = ocfs2_clusters_to_blocks(fs, la->la_bm_off);
	bitmap = la->la_bitmap;
	start = count = bit_off = 0;
	left = di->id1.bitmap1.i_total;

	while ((bit_off = ocfs2_find_next_bit_clear(bitmap, left, start))
	       != -1) {
		if ((bit_off < left) && (bit_off == start)) {
			count++;
			start++;
			continue;
		}
		if (count) {
			blkno = la_start_blk +
				ocfs2_clusters_to_blocks(fs, start - count);

			ret = ocfs2_test_clusters(fs, count,
						  blkno, 1, &was_set);
			if (ret)
				goto bail;

			if (!was_set) {
				ret = OCFS2_ET_INVALID_BIT;
				goto bail;
			}

			ret = ocfs2_free_clusters(fs, count, blkno);
			if (ret)
				goto bail;

			cleared = 1;
		}

		if (bit_off >= left)
			break;
		count = 1;
		start = bit_off + 1;
	}
clear_inode:
	di->id1.bitmap1.i_total = 0;
	di->id1.bitmap1.i_used = 0;
	la->la_bm_off = 0;
	memset(la->la_bitmap, 0, ocfs2_local_alloc_size(fs->fs_blocksize));

	ret = ocfs2_write_inode(fs, di->i_blkno, (char *)di);
	if (!ret && cleared)
		printf("Slot %d's local alloc replayed successfully\n", slot);

bail:
	return ret;
}

errcode_t o2fsck_replay_local_allocs(ocfs2_filesys *fs)
{
	return handle_slots_system_file(fs,
					LOCAL_ALLOC_SYSTEM_INODE,
					ocfs2_clear_local_alloc);
}
