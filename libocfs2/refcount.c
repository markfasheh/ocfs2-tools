/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * refcount.c
 *
 * Functions for the refcount tree structure.  Part of the OCFS2 userspace
 * library.
 *
 * Copyright (C) 2009 Oracle.  All rights reserved.
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

#define _XOPEN_SOURCE 600  /* Triggers XOPEN2K in features.h */
#define _LARGEFILE64_SOURCE

#include <string.h>
#include <inttypes.h>

#include "ocfs2/byteorder.h"
#include "ocfs2/ocfs2.h"


static void ocfs2_swap_refcount_list_primary(struct ocfs2_refcount_list *rl)
{
	rl->rl_count	= bswap_16(rl->rl_count);
	rl->rl_used	= bswap_16(rl->rl_used);
}

static void ocfs2_swap_refcount_list_secondary(ocfs2_filesys *fs, void *obj,
					       struct ocfs2_refcount_list *rl)
{
	int i;

	for (i = 0; i < rl->rl_count; i++) {
		struct ocfs2_refcount_rec *rec = &rl->rl_recs[i];

		if (ocfs2_swap_barrier(fs, obj, rec,
				       sizeof(struct ocfs2_refcount_rec)))
			break;

		rec->r_cpos	= bswap_64(rec->r_cpos);
		rec->r_clusters	= bswap_32(rec->r_clusters);
		rec->r_refcount	= bswap_32(rec->r_refcount);
	}
}

void ocfs2_swap_refcount_list_to_cpu(ocfs2_filesys *fs, void *obj,
				     struct ocfs2_refcount_list *rl)
{
	if (cpu_is_little_endian)
		return;

	ocfs2_swap_refcount_list_primary(rl);
	ocfs2_swap_refcount_list_secondary(fs, obj, rl);
}

static void ocfs2_swap_refcount_block_header(struct ocfs2_refcount_block *rb)
{

	rb->rf_suballoc_slot	= bswap_16(rb->rf_suballoc_slot);
	rb->rf_suballoc_bit	= bswap_16(rb->rf_suballoc_bit);
	rb->rf_fs_generation	= bswap_32(rb->rf_fs_generation);
	rb->rf_blkno		= bswap_64(rb->rf_blkno);
	rb->rf_parent		= bswap_64(rb->rf_parent);
	rb->rf_last_eb_blk	= bswap_64(rb->rf_last_eb_blk);
	rb->rf_count		= bswap_32(rb->rf_count);
	rb->rf_flags		= bswap_32(rb->rf_flags);
	rb->rf_clusters		= bswap_32(rb->rf_clusters);
	rb->rf_cpos		= bswap_32(rb->rf_cpos);
}

void ocfs2_swap_refcount_block_to_cpu(ocfs2_filesys *fs,
				      struct ocfs2_refcount_block *rb)
{
	if (cpu_is_little_endian)
		return;

	ocfs2_swap_refcount_block_header(rb);
	if (rb->rf_flags & OCFS2_REFCOUNT_TREE_FL)
		ocfs2_swap_extent_list_to_cpu(fs, rb, &rb->rf_list);
	else
		ocfs2_swap_refcount_list_to_cpu(fs, rb, &rb->rf_records);
}

errcode_t ocfs2_read_refcount_block_nocheck(ocfs2_filesys *fs,
					    uint64_t blkno,
					    char *rb_buf)
{
	errcode_t ret;
	char *blk;
	struct ocfs2_refcount_block *rb;

	if ((blkno < OCFS2_SUPER_BLOCK_BLKNO) ||
	    (blkno > fs->fs_blocks))
		return OCFS2_ET_BAD_BLKNO;

	ret = ocfs2_malloc_block(fs->fs_io, &blk);
	if (ret)
		return ret;

	ret = ocfs2_read_blocks(fs, blkno, 1, blk);
	if (ret)
		goto out;

	rb = (struct ocfs2_refcount_block *)blk;

	ret = ocfs2_validate_meta_ecc(fs, blk, &rb->rf_check);
	if (ret)
		goto out;

	if (memcmp(rb->rf_signature, OCFS2_REFCOUNT_BLOCK_SIGNATURE,
		   strlen(OCFS2_REFCOUNT_BLOCK_SIGNATURE))) {
		ret = OCFS2_ET_BAD_EXTENT_BLOCK_MAGIC;
		goto out;
	}

	memcpy(rb_buf, blk, fs->fs_blocksize);

	rb = (struct ocfs2_refcount_block *) rb_buf;
	ocfs2_swap_refcount_block_to_cpu(fs, rb);

out:
	ocfs2_free(&blk);

	return ret;
}

errcode_t ocfs2_read_refcount_block(ocfs2_filesys *fs, uint64_t blkno,
				    char *rb_buf)
{
	errcode_t ret;
	struct ocfs2_refcount_block *rb =
		(struct ocfs2_refcount_block *)rb_buf;

	ret = ocfs2_read_refcount_block_nocheck(fs, blkno, rb_buf);

	/*
	 * Return corruption error here if the user may have a chance
	 * to walk off the end.
	 * XXX: We trust the rb->rf_flags here.
	 */
	if (ret == 0 &&
	    (((rb->rf_flags & OCFS2_REFCOUNT_TREE_FL) &&
	     rb->rf_list.l_next_free_rec > rb->rf_list.l_count) ||
	     (!(rb->rf_flags & OCFS2_REFCOUNT_TREE_FL) &&
	     rb->rf_records.rl_used > rb->rf_records.rl_count)))
		ret = OCFS2_ET_CORRUPT_EXTENT_BLOCK;

	return ret;
}
