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
#include <assert.h>

#include "ocfs2/byteorder.h"
#include "ocfs2/ocfs2.h"
#include "extent_tree.h"


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

void ocfs2_swap_refcount_list_from_cpu(ocfs2_filesys *fs, void *obj,
				       struct ocfs2_refcount_list *rl)
{
	if (cpu_is_little_endian)
		return;

	ocfs2_swap_refcount_list_secondary(fs, obj, rl);
	ocfs2_swap_refcount_list_primary(rl);
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

void ocfs2_swap_refcount_block_from_cpu(ocfs2_filesys *fs,
					struct ocfs2_refcount_block *rb)
{
	if (cpu_is_little_endian)
		return;

	if (rb->rf_flags & OCFS2_REFCOUNT_TREE_FL)
		ocfs2_swap_extent_list_from_cpu(fs, rb, &rb->rf_list);
	else
		ocfs2_swap_refcount_list_from_cpu(fs, rb, &rb->rf_records);
	ocfs2_swap_refcount_block_header(rb);
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

errcode_t ocfs2_write_refcount_block(ocfs2_filesys *fs, uint64_t blkno,
				     char *rb_buf)
{
	errcode_t ret;
	char *blk;
	struct ocfs2_refcount_block *rb;

	if (!(fs->fs_flags & OCFS2_FLAG_RW))
		return OCFS2_ET_RO_FILESYS;

	if ((blkno < OCFS2_SUPER_BLOCK_BLKNO) ||
	    (blkno > fs->fs_blocks))
		return OCFS2_ET_BAD_BLKNO;

	ret = ocfs2_malloc_block(fs->fs_io, &blk);
	if (ret)
		return ret;

	memcpy(blk, rb_buf, fs->fs_blocksize);

	rb = (struct ocfs2_refcount_block *)blk;
	ocfs2_swap_refcount_block_from_cpu(fs, rb);

	ocfs2_compute_meta_ecc(fs, blk, &rb->rf_check);
	ret = io_write_block(fs->fs_io, blkno, 1, blk);
	if (ret)
		goto out;

	fs->fs_flags |= OCFS2_FLAG_CHANGED;
	ret = 0;

out:
	ocfs2_free(&blk);

	return ret;
}

static void ocfs2_find_refcount_rec_in_rl(char *ref_leaf_buf,
					  uint64_t cpos, unsigned int len,
					  struct ocfs2_refcount_rec *ret_rec,
					  int *index)
{
	int i = 0;
	struct ocfs2_refcount_block *rb =
			(struct ocfs2_refcount_block *)ref_leaf_buf;
	struct ocfs2_refcount_rec *rec = NULL;

	for (; i < rb->rf_records.rl_used; i++) {
		rec = &rb->rf_records.rl_recs[i];

		if (rec->r_cpos + rec->r_clusters <= cpos)
			continue;
		else if (rec->r_cpos > cpos)
			break;

		/* ok, cpos fail in this rec. Just return. */
		if (ret_rec)
			*ret_rec = *rec;
		goto out;
	}

	if (ret_rec) {
		/* We meet with a hole here, so fake the rec. */
		ret_rec->r_cpos = cpos;
		ret_rec->r_refcount = 0;
		if (i < rb->rf_records.rl_used && rec->r_cpos < cpos + len)
			ret_rec->r_clusters = rec->r_cpos - cpos;
		else
			ret_rec->r_clusters = len;
	}

out:
	*index = i;
}

/*
 * Given a cpos and len, try to find the refcount record which contains cpos.
 * 1. If cpos can be found in one refcount record, return the record.
 * 2. If cpos can't be found, return a fake record which start from cpos
 *    and end at a small value between cpos+len and start of the next record.
 *    This fake record has r_refcount = 0.
 */
static int ocfs2_get_refcount_rec(ocfs2_filesys *fs,
				  char *ref_root_buf,
				  uint64_t cpos, unsigned int len,
				  struct ocfs2_refcount_rec *ret_rec,
				  int *index,
				  char *ret_buf)
{
	int ret = 0, i, found;
	uint32_t low_cpos;
	struct ocfs2_extent_list *el;
	struct ocfs2_extent_rec *tmp, *rec = NULL;
	struct ocfs2_extent_block *eb;
	char *eb_buf = NULL, *ref_leaf_buf = NULL;
	struct ocfs2_refcount_block *rb =
			(struct ocfs2_refcount_block *)ref_root_buf;

	if (!(rb->rf_flags & OCFS2_REFCOUNT_TREE_FL)) {
		ocfs2_find_refcount_rec_in_rl(ref_root_buf, cpos, len,
					      ret_rec, index);
		memcpy(ret_buf, ref_root_buf, fs->fs_blocksize);
		return 0;
	}

	el = &rb->rf_list;
	low_cpos = cpos & OCFS2_32BIT_POS_MASK;

	if (el->l_tree_depth) {
		ret = ocfs2_tree_find_leaf(fs, el, rb->rf_blkno,
					   (char *)rb, low_cpos,
					    &eb_buf);
		if (ret)
			goto out;

		eb = (struct ocfs2_extent_block *)eb_buf;
		el = &eb->h_list;

		if (el->l_tree_depth) {
			ret = OCFS2_ET_CORRUPT_EXTENT_BLOCK;
			goto out;
		}
	}

	found = 0;
	for (i = el->l_next_free_rec - 1; i >= 0; i--) {
		rec = &el->l_recs[i];

		if (rec->e_cpos <= low_cpos) {
			found = 1;
			break;
		}
	}

	/* adjust len when we have ocfs2_extent_rec after it. */
	if (found && i < el->l_next_free_rec - 1) {
		tmp = &el->l_recs[i+1];

		if (tmp->e_cpos < cpos + len)
			len = tmp->e_cpos - cpos;
	}

	ret = ocfs2_malloc_block(fs->fs_io, &ref_leaf_buf);
	if (ret)
		goto out;

	ret = ocfs2_read_refcount_block(fs, rec->e_blkno,
					ref_leaf_buf);
	if (ret)
		goto out;

	ocfs2_find_refcount_rec_in_rl(ref_leaf_buf, cpos, len,
				      ret_rec, index);
	memcpy(ret_buf, ref_leaf_buf, fs->fs_blocksize);
out:
	if (eb_buf)
		ocfs2_free(&eb_buf);
	if (ref_leaf_buf)
		ocfs2_free(&ref_leaf_buf);

	return ret;
}

enum ocfs2_ref_rec_contig {
	REF_CONTIG_NONE = 0,
	REF_CONTIG_LEFT,
	REF_CONTIG_RIGHT,
	REF_CONTIG_LEFTRIGHT,
};

static enum ocfs2_ref_rec_contig
	ocfs2_refcount_rec_adjacent(struct ocfs2_refcount_block *rb,
				    int index)
{
	if ((rb->rf_records.rl_recs[index].r_refcount ==
	    rb->rf_records.rl_recs[index + 1].r_refcount) &&
	    (rb->rf_records.rl_recs[index].r_cpos +
	     rb->rf_records.rl_recs[index].r_clusters ==
	    rb->rf_records.rl_recs[index + 1].r_cpos))
		return REF_CONTIG_RIGHT;

	return REF_CONTIG_NONE;
}

static enum ocfs2_ref_rec_contig
	ocfs2_refcount_rec_contig(struct ocfs2_refcount_block *rb,
				  int index)
{
	enum ocfs2_ref_rec_contig ret = REF_CONTIG_NONE;

	if (index < rb->rf_records.rl_used - 1)
		ret = ocfs2_refcount_rec_adjacent(rb, index);

	if (index > 0) {
		enum ocfs2_ref_rec_contig tmp;

		tmp = ocfs2_refcount_rec_adjacent(rb, index - 1);

		if (tmp == REF_CONTIG_RIGHT) {
			if (ret == REF_CONTIG_RIGHT)
				ret = REF_CONTIG_LEFTRIGHT;
			else
				ret = REF_CONTIG_LEFT;
		}
	}

	return ret;
}

static void ocfs2_rotate_refcount_rec_left(struct ocfs2_refcount_block *rb,
					   int index)
{
	assert(rb->rf_records.rl_recs[index].r_refcount ==
	       rb->rf_records.rl_recs[index+1].r_refcount);

	rb->rf_records.rl_recs[index].r_clusters +=
				rb->rf_records.rl_recs[index+1].r_clusters;

	if (index < rb->rf_records.rl_used - 2)
		memmove(&rb->rf_records.rl_recs[index + 1],
			&rb->rf_records.rl_recs[index + 2],
			sizeof(struct ocfs2_refcount_rec) *
			(rb->rf_records.rl_used - index - 2));

	memset(&rb->rf_records.rl_recs[rb->rf_records.rl_used - 1],
	       0, sizeof(struct ocfs2_refcount_rec));
	rb->rf_records.rl_used -= 1;
}

/*
 * Merge the refcount rec if we are contiguous with the adjacent recs.
 */
static void ocfs2_refcount_rec_merge(struct ocfs2_refcount_block *rb,
				     int index)
{
	enum ocfs2_ref_rec_contig contig =
				ocfs2_refcount_rec_contig(rb, index);

	if (contig == REF_CONTIG_NONE)
		return;

	if (contig == REF_CONTIG_LEFT || contig == REF_CONTIG_LEFTRIGHT) {
		assert(index > 0);
		index--;
	}

	ocfs2_rotate_refcount_rec_left(rb, index);

	if (contig == REF_CONTIG_LEFTRIGHT)
		ocfs2_rotate_refcount_rec_left(rb, index);
}

/*
 * Change the refcount indexed by "index" in rb.
 * If refcount reaches 0, remove it.
 */
static int ocfs2_change_refcount_rec(ocfs2_filesys *fs,
				     char *ref_leaf_buf,
				     int index, int merge, int change)
{
	struct ocfs2_refcount_block *rb =
			(struct ocfs2_refcount_block *)ref_leaf_buf;
	struct ocfs2_refcount_list *rl = &rb->rf_records;
	struct ocfs2_refcount_rec *rec = &rl->rl_recs[index];

	rec->r_refcount += change;

	if (!rec->r_refcount) {
		if (index != rl->rl_used - 1) {
			memmove(rec, rec + 1,
				(rl->rl_used - index - 1) *
				sizeof(struct ocfs2_refcount_rec));
			memset(&rl->rl_recs[le16_to_cpu(rl->rl_used) - 1],
			       0, sizeof(struct ocfs2_refcount_rec));
		}

		rl->rl_used -= 1;
	} else if (merge)
		ocfs2_refcount_rec_merge(rb, index);

	return ocfs2_write_refcount_block(fs, rb->rf_blkno, ref_leaf_buf);
}

static int ocfs2_expand_inline_ref_root(ocfs2_filesys *fs,
					char *ref_root_buf,
					char *ret_leaf_buf)
{
	int ret;
	uint64_t new_blkno;
	char *new_buf = NULL;
	struct ocfs2_refcount_block *new_rb;
	struct ocfs2_refcount_block *root_rb =
			(struct ocfs2_refcount_block *)ref_root_buf;

	ret = ocfs2_malloc_block(fs->fs_io, &new_buf);
	if (ret)
		return ret;

	ret = ocfs2_new_refcount_block(fs, &new_blkno, root_rb->rf_blkno,
				       root_rb->rf_generation);
	if (ret)
		goto out;

	ret = ocfs2_read_refcount_block(fs, new_blkno, new_buf);
	if (ret)
		goto out;

	/*
	 * Initialize ocfs2_refcount_block.
	 * It should contain the same refcount information as the
	 * old root. So just memcpy the refcount_list, set the
	 * rf_cpos to 0 and the leaf flag.
	 */
	new_rb = (struct ocfs2_refcount_block *)new_buf;
	memcpy(&new_rb->rf_list, &root_rb->rf_list, fs->fs_blocksize -
	       offsetof(struct ocfs2_refcount_block, rf_list));
	new_rb->rf_cpos = 0;
	new_rb->rf_flags = OCFS2_REFCOUNT_LEAF_FL;

	/* Now change the root. */
	memset(&root_rb->rf_list, 0, fs->fs_blocksize -
	       offsetof(struct ocfs2_refcount_block, rf_list));
	root_rb->rf_list.l_count = ocfs2_extent_recs_per_rb(fs->fs_blocksize);
	root_rb->rf_clusters = 1;
	root_rb->rf_list.l_next_free_rec = 1;
	root_rb->rf_list.l_recs[0].e_blkno = new_blkno;
	root_rb->rf_list.l_recs[0].e_leaf_clusters = 1;
	root_rb->rf_flags = OCFS2_REFCOUNT_TREE_FL;

	/*
	 * We write the new allocated refcount block first. If the write
	 * fails, skip update the root.
	 */
	ret = ocfs2_write_refcount_block(fs, new_rb->rf_blkno, new_buf);
	if (ret)
		goto out;

	ret = ocfs2_write_refcount_block(fs, root_rb->rf_blkno, ref_root_buf);
	if (ret)
		goto out;

	memcpy(ret_leaf_buf, new_buf, fs->fs_blocksize);
out:
	ocfs2_free(&new_buf);
	return ret;
}

static int ocfs2_refcount_rec_no_intersect(struct ocfs2_refcount_rec *prev,
					   struct ocfs2_refcount_rec *next)
{
	if (ocfs2_get_ref_rec_low_cpos(prev) + prev->r_clusters <=
		ocfs2_get_ref_rec_low_cpos(next))
		return 1;

	return 0;
}

static int cmp_refcount_rec_by_low_cpos(const void *a, const void *b)
{
	const struct ocfs2_refcount_rec *l = a, *r = b;
	uint32_t l_cpos = ocfs2_get_ref_rec_low_cpos(l);
	uint32_t r_cpos = ocfs2_get_ref_rec_low_cpos(r);

	if (l_cpos > r_cpos)
		return 1;
	if (l_cpos < r_cpos)
		return -1;
	return 0;
}

static int cmp_refcount_rec_by_cpos(const void *a, const void *b)
{
	const struct ocfs2_refcount_rec *l = a, *r = b;
	uint64_t l_cpos = l->r_cpos;
	uint64_t r_cpos = r->r_cpos;

	if (l_cpos > r_cpos)
		return 1;
	if (l_cpos < r_cpos)
		return -1;
	return 0;
}

/*
 * The refcount cpos are ordered by their 64bit cpos,
 * But we will use the low 32 bit to be the e_cpos in the b-tree.
 * So we need to make sure that this pos isn't intersected with others.
 *
 * Note: The refcount block is already sorted by their low 32 bit cpos,
 *       So just try the middle pos first, and we will exit when we find
 *       the good position.
 */
static int ocfs2_find_refcount_split_pos(struct ocfs2_refcount_list *rl,
					 uint32_t *split_pos, int *split_index)
{
	int num_used = rl->rl_used;
	int delta, middle = num_used / 2;

	for (delta = 0; delta < middle; delta++) {
		/* Let's check delta earlier than middle */
		if (ocfs2_refcount_rec_no_intersect(
					&rl->rl_recs[middle - delta - 1],
					&rl->rl_recs[middle - delta])) {
			*split_index = middle - delta;
			break;
		}

		/* For even counts, don't walk off the end */
		if ((middle + delta + 1) == num_used)
			continue;

		/* Now try delta past middle */
		if (ocfs2_refcount_rec_no_intersect(
					&rl->rl_recs[middle + delta],
					&rl->rl_recs[middle + delta + 1])) {
			*split_index = middle + delta + 1;
			break;
		}
	}

	if (delta >= middle)
		return OCFS2_ET_NO_SPACE;

	*split_pos = ocfs2_get_ref_rec_low_cpos(&rl->rl_recs[*split_index]);
	return 0;
}

static int ocfs2_divide_leaf_refcount_block(char *ref_leaf_buf,
					    char *new_buf,
					    uint32_t *split_cpos)
{
	int split_index = 0, num_moved, ret;
	uint32_t cpos = 0;
	struct ocfs2_refcount_block *rb =
			(struct ocfs2_refcount_block *)ref_leaf_buf;
	struct ocfs2_refcount_list *rl = &rb->rf_records;
	struct ocfs2_refcount_block *new_rb =
			(struct ocfs2_refcount_block *)new_buf;
	struct ocfs2_refcount_list *new_rl = &new_rb->rf_records;

	/*
	 * XXX: Improvement later.
	 * If we know all the high 32 bit cpos is the same, no need to sort.
	 *
	 * In order to make the whole process safe, we do:
	 * 1. sort the entries by their low 32 bit cpos first so that we can
	 *    find the split cpos easily.
	 * 2. call ocfs2_tree_insert_extent to insert the new refcount block.
	 * 3. move the refcount rec to the new block.
	 * 4. sort the entries by their 64 bit cpos.
	 * 5. And we will delay the write out of the leaf block after the
	 *    extent tree is successfully changed by its caller.
	 */
	qsort(&rl->rl_recs, rl->rl_used,
	      sizeof(struct ocfs2_refcount_rec),
	      cmp_refcount_rec_by_low_cpos);

	ret = ocfs2_find_refcount_split_pos(rl, &cpos, &split_index);
	if (ret)
		return ret;

	new_rb->rf_cpos = cpos;

	/* move refcount records starting from split_index to the new block. */
	num_moved = rl->rl_used - split_index;
	memcpy(new_rl->rl_recs, &rl->rl_recs[split_index],
	       num_moved * sizeof(struct ocfs2_refcount_rec));

	/*ok, remove the entries we just moved over to the other block. */
	memset(&rl->rl_recs[split_index], 0,
	       num_moved * sizeof(struct ocfs2_refcount_rec));

	/* change old and new rl_used accordingly. */
	rl->rl_used -= num_moved;
	new_rl->rl_used = num_moved;

	qsort(&rl->rl_recs, rl->rl_used,
	      sizeof(struct ocfs2_refcount_rec),
	      cmp_refcount_rec_by_cpos);

	qsort(&new_rl->rl_recs, new_rl->rl_used,
	      sizeof(struct ocfs2_refcount_rec),
	      cmp_refcount_rec_by_cpos);

	*split_cpos = cpos;
	return 0;
}

static int ocfs2_new_leaf_refcount_block(ocfs2_filesys *fs,
					 char *ref_root_buf,
					 char *ref_leaf_buf)
{
	int ret;
	uint32_t new_cpos;
	uint64_t new_blkno;
	struct ocfs2_refcount_block *root_rb =
			(struct ocfs2_refcount_block *)ref_root_buf;
	char *new_buf = NULL;
	struct ocfs2_refcount_block *rb;
	struct ocfs2_extent_tree ref_et;

	assert(root_rb->rf_flags & OCFS2_REFCOUNT_TREE_FL);

	ret = ocfs2_malloc_block(fs->fs_io, &new_buf);
	if (ret)
		return ret;

	ret = ocfs2_new_refcount_block(fs, &new_blkno, root_rb->rf_blkno,
				       root_rb->rf_generation);
	if (ret)
		goto out;

	ret = ocfs2_read_refcount_block(fs, new_blkno, new_buf);

	ret = ocfs2_divide_leaf_refcount_block(ref_leaf_buf,
					       new_buf, &new_cpos);
	if (ret)
		goto out;

	ocfs2_init_refcount_extent_tree(&ref_et, fs,
					ref_root_buf, root_rb->rf_blkno);

	ret = ocfs2_tree_insert_extent(fs, &ref_et, new_cpos, new_blkno, 1, 0);
	if (ret)
		goto out;

	/*
	 * Write the old refcount block first.
	 * If the write fails, fsck should be able to remove all
	 * the refcounted clusters we have moved to the new refcount block.
	 */
	rb = (struct ocfs2_refcount_block *)ref_leaf_buf;
	ret = ocfs2_write_refcount_block(fs, rb->rf_blkno, ref_leaf_buf);
	if (ret)
		goto out;

	ret = ocfs2_write_refcount_block(fs, new_blkno, new_buf);
	if (ret)
		goto out;
out:
	if (new_buf)
		ocfs2_free(&new_buf);
	return ret;
}

static int ocfs2_expand_refcount_tree(ocfs2_filesys *fs,
				      char *ref_root_buf,
				      char *ref_leaf_buf)
{
	int ret;
	struct ocfs2_refcount_block *root_rb =
			(struct ocfs2_refcount_block *)ref_root_buf;
	struct ocfs2_refcount_block *leaf_rb =
			(struct ocfs2_refcount_block *)ref_leaf_buf;

	if (root_rb->rf_blkno == leaf_rb->rf_blkno) {
		/*
		 * the old root bh hasn't been expanded to a b-tree,
		 * so expand it first.
		 */
		ret = ocfs2_expand_inline_ref_root(fs, ref_root_buf,
						   ref_leaf_buf);
		if (ret)
			goto out;
	}

	/* Now add a new refcount block into the tree.*/
	ret = ocfs2_new_leaf_refcount_block(fs, ref_root_buf, ref_leaf_buf);
out:
	return ret;
}

/*
 * Adjust the extent rec in b-tree representing ref_leaf_buf.
 *
 * Only called when we have inserted a new refcount rec at index 0
 * which means ocfs2_extent_rec.e_cpos may need some change.
 */
static int ocfs2_adjust_refcount_rec(ocfs2_filesys *fs,
				     char *ref_root_buf,
				     char *ref_leaf_buf,
				     struct ocfs2_refcount_rec *rec)
{
	int ret = 0, i;
	uint32_t new_cpos, old_cpos;
	struct ocfs2_path *path = NULL;
	struct ocfs2_extent_list *el;
	struct ocfs2_extent_tree et;
	struct ocfs2_refcount_block *rb =
		(struct ocfs2_refcount_block *)ref_root_buf;
	uint64_t ref_root_blkno = rb->rf_blkno;;

	if (!(rb->rf_flags & OCFS2_REFCOUNT_TREE_FL))
		goto out;

	rb = (struct ocfs2_refcount_block *)ref_leaf_buf;
	old_cpos = rb->rf_cpos;
	new_cpos = rec->r_cpos & OCFS2_32BIT_POS_MASK;
	if (old_cpos <= new_cpos)
		goto out;

	ocfs2_init_refcount_extent_tree(&et, fs, ref_root_buf, ref_root_blkno);

	path = ocfs2_new_path_from_et(&et);
	if (!path) {
		ret = OCFS2_ET_NO_MEMORY;
		goto out;
	}

	ret = ocfs2_find_path(fs, path, old_cpos);
	if (ret)
		goto out;

	/* change the leaf extent block first. */
	el = path_leaf_el(path);

	for (i = 0; i < el->l_next_free_rec; i++)
		if (el->l_recs[i].e_cpos == old_cpos)
			break;

	assert(i < el->l_next_free_rec);

	el->l_recs[i].e_cpos = new_cpos;

	/* change the r_cpos in the leaf block. */
	rb->rf_cpos = new_cpos;

	ret = ocfs2_write_extent_block(fs, path_leaf_blkno(path),
				       path_leaf_buf(path));
	if (ret)
		goto out;

	ret = ocfs2_write_refcount_block(fs, rb->rf_blkno, ref_leaf_buf);
out:
	ocfs2_free_path(path);
	return ret;
}

static int ocfs2_insert_refcount_rec(ocfs2_filesys *fs,
				     char *ref_root_buf,
				     char *ref_leaf_buf,
				     struct ocfs2_refcount_rec *rec,
				     int index, int merge)
{
	int ret;
	struct ocfs2_refcount_block *rb =
			(struct ocfs2_refcount_block *)ref_leaf_buf;
	struct ocfs2_refcount_list *rf_list = &rb->rf_records;

	assert(!(rb->rf_flags & OCFS2_REFCOUNT_TREE_FL));

	if (rf_list->rl_used == rf_list->rl_count) {
		uint64_t cpos = rec->r_cpos;
		uint32_t len = rec->r_clusters;

		ret = ocfs2_expand_refcount_tree(fs, ref_root_buf,
						 ref_leaf_buf);
		if (ret)
			goto out;

		ret = ocfs2_get_refcount_rec(fs, ref_root_buf,
					     cpos, len, NULL, &index,
					     ref_leaf_buf);
		if (ret)
			goto out;
	}

	if (index < rf_list->rl_used)
		memmove(&rf_list->rl_recs[index + 1],
			&rf_list->rl_recs[index],
			(rf_list->rl_used - index) *
			 sizeof(struct ocfs2_refcount_rec));

	rf_list->rl_recs[index] = *rec;

	rf_list->rl_used += 1;

	if (merge)
		ocfs2_refcount_rec_merge(rb, index);

	ret = ocfs2_write_refcount_block(fs, rb->rf_blkno, ref_leaf_buf);
	if (ret)
		goto out;

	if (index == 0)
		ret = ocfs2_adjust_refcount_rec(fs, ref_root_buf,
						ref_leaf_buf, rec);
out:
	return ret;
}

/*
 * Split the refcount_rec indexed by "index" in ref_leaf_buf.
 * This is much simple than our b-tree code.
 * split_rec is the new refcount rec we want to insert.
 * If split_rec->r_refcount > 0, we are changing the refcount(in case we
 * increase refcount or decrease a refcount to non-zero).
 * If split_rec->r_refcount == 0, we are punching a hole in current refcount
 * rec( in case we decrease a refcount to zero).
 */
static int ocfs2_split_refcount_rec(ocfs2_filesys *fs,
				    char *ref_root_buf,
				    char *ref_leaf_buf,
				    struct ocfs2_refcount_rec *split_rec,
				    int index, int merge)
{
	int ret, recs_need;
	uint32_t len;
	struct ocfs2_refcount_block *rb =
			(struct ocfs2_refcount_block *)ref_leaf_buf;
	struct ocfs2_refcount_list *rf_list = &rb->rf_records;
	struct ocfs2_refcount_rec *orig_rec = &rf_list->rl_recs[index];
	struct ocfs2_refcount_rec *tail_rec = NULL;

	assert(!(rb->rf_flags & OCFS2_REFCOUNT_TREE_FL));

	/*
	 * If we just need to split the header or tail clusters,
	 * no more recs are needed, just split is OK.
	 * Otherwise we at least need one new recs.
	 */
	if (!split_rec->r_refcount &&
	    (split_rec->r_cpos == orig_rec->r_cpos ||
	     split_rec->r_cpos + split_rec->r_clusters ==
	     orig_rec->r_cpos + orig_rec->r_clusters))
		recs_need = 0;
	else
		recs_need = 1;

	/*
	 * We need one more rec if we split in the middle and the new rec have
	 * some refcount in it.
	 */
	if (split_rec->r_refcount &&
	    (split_rec->r_cpos != orig_rec->r_cpos &&
	     split_rec->r_cpos + split_rec->r_clusters !=
	     orig_rec->r_cpos + orig_rec->r_clusters))
		recs_need++;

	/* If the leaf block don't have enough record, expand it. */
	if (rf_list->rl_used + recs_need > rf_list->rl_count) {
		struct ocfs2_refcount_rec tmp_rec;
		uint64_t cpos = orig_rec->r_cpos;
		len = orig_rec->r_clusters;
		ret = ocfs2_expand_refcount_tree(fs, ref_root_buf,
						 ref_leaf_buf);
		if (ret)
			goto out;

		/*
		 * We have to re-get it since now cpos may be moved to
		 * another leaf block.
		 */
		ret = ocfs2_get_refcount_rec(fs, ref_root_buf,
					     cpos, len, &tmp_rec, &index,
					     ref_leaf_buf);
		if (ret)
			goto out;

		orig_rec = &rf_list->rl_recs[index];
	}

	/*
	 * We have calculated out how many new records we need and store
	 * in recs_need, so spare enough space first by moving the records
	 * after "index" to the end.
	 */
	if (rf_list->rl_used && index != rf_list->rl_used - 1)
		memmove(&rf_list->rl_recs[index + 1 + recs_need],
			&rf_list->rl_recs[index + 1],
			(rf_list->rl_used - index - 1) *
			 sizeof(struct ocfs2_refcount_rec));

	len = (orig_rec->r_cpos + orig_rec->r_clusters) -
	      (split_rec->r_cpos + split_rec->r_clusters);

	/*
	 * If we have "len", the we will split in the tail and move it
	 * to the end of the space we have just spared.
	 */
	if (len) {
		tail_rec = &rf_list->rl_recs[index + recs_need];

		memcpy(tail_rec, orig_rec, sizeof(struct ocfs2_refcount_rec));
		tail_rec->r_cpos += tail_rec->r_clusters - len;
		tail_rec->r_clusters = len;
	}

	/*
	 * If the split pos isn't the same as the original one, we need to
	 * split in the head.
	 *
	 * Note: We have the chance that split_rec.r_refcount = 0,
	 * recs_need = 0 and len > 0, which means we just cut the head from
	 * the orig_rec and in that case we have done some modification in
	 * orig_rec above, so the check for r_cpos is faked.
	 */
	if (split_rec->r_cpos != orig_rec->r_cpos && tail_rec != orig_rec) {
		len = split_rec->r_cpos - orig_rec->r_cpos;
		orig_rec->r_clusters = len;
		index++;
	}

	rf_list->rl_used += recs_need;

	if (split_rec->r_refcount) {
		rf_list->rl_recs[index] = *split_rec;

		if (merge)
			ocfs2_refcount_rec_merge(rb, index);
	}

	ret = ocfs2_write_refcount_block(fs, rb->rf_blkno, ref_leaf_buf);

out:
	return ret;
}

static int __ocfs2_increase_refcount(ocfs2_filesys *fs,
				     char *ref_root_buf,
				     uint64_t cpos, uint32_t len, int merge)
{
	int ret = 0, index;
	char *ref_leaf_buf = NULL;
	struct ocfs2_refcount_rec rec;
	unsigned int set_len = 0;
	struct ocfs2_refcount_block *root_rb, *rb;

	ret = ocfs2_malloc_block(fs->fs_io, &ref_leaf_buf);
	if (ret)
		return ret;

	root_rb = (struct ocfs2_refcount_block *)ref_root_buf;
	rb = (struct ocfs2_refcount_block *)ref_leaf_buf;
	while (len) {
		ret = ocfs2_get_refcount_rec(fs, ref_root_buf,
					     cpos, len, &rec, &index,
					     ref_leaf_buf);
		if (ret)
			goto out;

		set_len = rec.r_clusters;

		/*
		 * Here we may meet with 3 situations:
		 *
		 * 1. If we find an already existing record, and the length
		 *    is the same, cool, we just need to increase the r_refcount
		 *    and it is OK.
		 * 2. If we find a hole, just insert it with r_refcount = 1.
		 * 3. If we are in the middle of one extent record, split
		 *    it.
		 */
		if (rec.r_refcount && rec.r_cpos == cpos && set_len <= len) {
			ret = ocfs2_change_refcount_rec(fs, ref_leaf_buf, index,
							merge, 1);
			if (ret)
				goto out;
		} else if (!rec.r_refcount) {
			rec.r_refcount = 1;

			ret = ocfs2_insert_refcount_rec(fs, ref_root_buf,
							ref_leaf_buf,
							&rec, index, merge);
			if (ret)
				goto out;
		} else  {
			set_len = ocfs2_min((uint64_t)(cpos + len),
				      (uint64_t)(rec.r_cpos + set_len)) - cpos;
			rec.r_cpos = cpos;
			rec.r_clusters = set_len;
			rec.r_refcount += 1;

			ret = ocfs2_split_refcount_rec(fs, ref_root_buf,
						       ref_leaf_buf,
						       &rec, index, merge);
			if (ret)
				goto out;
		}

		cpos += set_len;
		len -= set_len;
		/* In user space, we have to sync the buf by ourselves. */
		if (rb->rf_blkno == root_rb->rf_blkno)
			memcpy(ref_root_buf, ref_leaf_buf, fs->fs_blocksize);
	}

out:
	ocfs2_free(&ref_leaf_buf);
	return ret;
}

errcode_t ocfs2_increase_refcount(ocfs2_filesys *fs, uint64_t ino,
				  uint64_t cpos, uint32_t len)
{
	errcode_t ret;
	char *ref_root_buf = NULL;
	char *di_buf = NULL;
	struct ocfs2_dinode *di;

	ret = ocfs2_malloc_block(fs->fs_io, &di_buf);
	if (ret)
		goto out;

	ret = ocfs2_read_inode(fs, ino, di_buf);
	if (ret)
		goto out;

	di = (struct ocfs2_dinode *)di_buf;

	assert(di->i_dyn_features & OCFS2_HAS_REFCOUNT_FL);
	assert(di->i_refcount_loc);

	ret = ocfs2_malloc_block(fs->fs_io, &ref_root_buf);
	if (ret)
		goto out;

	ret = ocfs2_read_refcount_block(fs, di->i_refcount_loc, ref_root_buf);
	if (ret)
		goto out;

	ret =  __ocfs2_increase_refcount(fs, ref_root_buf,
					 cpos, len, 1);
out:
	if (ref_root_buf)
		ocfs2_free(&ref_root_buf);
	if (di_buf)
		ocfs2_free(&di_buf);
	return ret;
}

static int ocfs2_remove_refcount_extent(ocfs2_filesys *fs,
					char *ref_root_buf,
					char *ref_leaf_buf)
{
	int ret;
	struct ocfs2_refcount_block *rb =
			(struct ocfs2_refcount_block *)ref_leaf_buf;
	struct ocfs2_refcount_block *root_rb =
			(struct ocfs2_refcount_block *)ref_root_buf;
	struct ocfs2_extent_tree et;

	assert(rb->rf_records.rl_used == 0);

	ocfs2_init_refcount_extent_tree(&et, fs,
					ref_root_buf, root_rb->rf_blkno);
	ret = ocfs2_remove_extent(fs, &et, rb->rf_cpos, 1);
	if (ret)
		goto out;

	ret = ocfs2_delete_refcount_block(fs, rb->rf_blkno);

	root_rb->rf_clusters -= 1;

	/*
	 * check whether we need to restore the root refcount block if
	 * there is no leaf extent block at atll.
	 */
	if (!root_rb->rf_list.l_next_free_rec) {
		assert(root_rb->rf_clusters == 0);

		root_rb->rf_flags = 0;
		root_rb->rf_parent = 0;
		root_rb->rf_cpos = 0;
		memset(&root_rb->rf_records, 0, fs->fs_blocksize -
		       offsetof(struct ocfs2_refcount_block, rf_records));
		root_rb->rf_records.rl_count =
				ocfs2_refcount_recs_per_rb(fs->fs_blocksize);
	}

	ret = ocfs2_write_refcount_block(fs, root_rb->rf_blkno, ref_root_buf);
out:
	return ret;
}

static int ocfs2_decrease_refcount_rec(ocfs2_filesys *fs,
				char *ref_root_buf,
				char *ref_leaf_buf,
				int index, uint64_t cpos, unsigned int len)
{
	int ret;
	struct ocfs2_refcount_block *rb =
			(struct ocfs2_refcount_block *)ref_leaf_buf;
	struct ocfs2_refcount_block *root_rb =
			(struct ocfs2_refcount_block *)ref_root_buf;
	struct ocfs2_refcount_rec *rec = &rb->rf_records.rl_recs[index];

	assert(cpos >= rec->r_cpos);
	assert(cpos + len <= rec->r_cpos + rec->r_clusters);

	if (cpos == rec->r_cpos && len == rec->r_clusters)
		ret = ocfs2_change_refcount_rec(fs, ref_leaf_buf, index, 1, -1);
	else {
		struct ocfs2_refcount_rec split = *rec;
		split.r_cpos = cpos;
		split.r_clusters = len;

		split.r_refcount -= 1;

		ret = ocfs2_split_refcount_rec(fs, ref_root_buf, ref_leaf_buf,
					       &split, index, 1);
	}
	if (ret)
		goto out;

	/* In user space, we have to sync the buf by ourselves. */
	if (rb->rf_blkno == root_rb->rf_blkno)
		memcpy(ref_root_buf, ref_leaf_buf, fs->fs_blocksize);

	/* Remove the leaf refcount block if it contains no refcount record. */
	if (!rb->rf_records.rl_used &&
	    rb->rf_blkno != root_rb->rf_blkno) {
		ret = ocfs2_remove_refcount_extent(fs, ref_root_buf,
						   ref_leaf_buf);
	}

out:
	return ret;
}

static int __ocfs2_decrease_refcount(ocfs2_filesys *fs, char *ref_root_buf,
				     uint64_t cpos, uint32_t len,
				     int delete)
{
	int ret = 0, index = 0;
	struct ocfs2_refcount_rec rec;
	unsigned int r_count = 0, r_len;
	char *ref_leaf_buf = NULL;

	ret = ocfs2_malloc_block(fs->fs_io, &ref_leaf_buf);
	if (ret)
		return ret;

	while (len) {
		ret = ocfs2_get_refcount_rec(fs, ref_root_buf,
					     cpos, len, &rec, &index,
					     ref_leaf_buf);
		if (ret)
			goto out;

		r_count = rec.r_refcount;
		assert(r_count > 0);
		if (!delete)
			assert(r_count == 1);

		r_len = ocfs2_min((uint64_t)(cpos + len),
				(uint64_t)(rec.r_cpos + rec.r_clusters)) - cpos;

		ret = ocfs2_decrease_refcount_rec(fs, ref_root_buf,
						  ref_leaf_buf, index,
						  cpos, r_len);
		if (ret)
			goto out;

		if (rec.r_refcount == 1 && delete) {
			ret = ocfs2_free_clusters(fs, r_len,
					  ocfs2_clusters_to_blocks(fs, cpos));
			if (ret)
				goto out;
		}

		cpos += r_len;
		len -= r_len;
	}

out:
	ocfs2_free(&ref_leaf_buf);
	return ret;
}

errcode_t ocfs2_decrease_refcount(ocfs2_filesys *fs,
				  uint64_t ino, uint32_t cpos,
				  uint32_t len, int delete)
{
	errcode_t ret;
	char *ref_root_buf = NULL;
	char *di_buf = NULL;
	struct ocfs2_dinode *di;

	ret = ocfs2_malloc_block(fs->fs_io, &di_buf);
	if (ret)
		goto out;

	ret = ocfs2_read_inode(fs, ino, di_buf);
	if (ret)
		goto out;

	di = (struct ocfs2_dinode *)di_buf;

	assert(di->i_dyn_features & OCFS2_HAS_REFCOUNT_FL);
	assert(di->i_refcount_loc);

	ret = ocfs2_malloc_block(fs->fs_io, &ref_root_buf);
	if (ret)
		goto out;

	ret = ocfs2_read_refcount_block(fs, di->i_refcount_loc, ref_root_buf);
	if (ret)
		goto out;

	ret = __ocfs2_decrease_refcount(fs, ref_root_buf, cpos, len, delete);
out:
	if (ref_root_buf)
		ocfs2_free(&ref_root_buf);
	if (di_buf)
		ocfs2_free(&di_buf);
	return ret;
}
