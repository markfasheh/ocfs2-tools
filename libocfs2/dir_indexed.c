/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * Copyright (C) 2009, 2010 Novell.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License, version 2,  as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#include <assert.h>
#include <ocfs2/ocfs2.h>
#include <ocfs2/bitops.h>
#include <ocfs2/kernel-rbtree.h>
#include "ocfs2_err.h"
#include "extent_tree.h"


errcode_t ocfs2_dx_dir_truncate(ocfs2_filesys *fs,
			uint64_t dir)
{
	struct ocfs2_dx_root_block *dx_root;
	char *dx_root_buf = NULL, *di_buf = NULL;
	struct ocfs2_dinode *di;
	uint64_t dx_root_blk;
	errcode_t ret = 0;

	ret = ocfs2_malloc_block(fs->fs_io, &di_buf);
	if (ret)
		goto out;
	ret = ocfs2_read_inode(fs, dir, di_buf);
	if (ret)
		goto out;
	di = (struct ocfs2_dinode *)di_buf;

	/* we have to trust i_dyn_features */
	if (!S_ISDIR(di->i_mode) ||
	    !ocfs2_dir_indexed(di) ||
	    di->i_dyn_features & OCFS2_INLINE_DATA_FL)
		goto out;

	dx_root_blk = di->i_dx_root;

	di->i_dyn_features &= ~OCFS2_INDEXED_DIR_FL;
	di->i_dx_root = 0;

	/* update inode firstly */
	ret = ocfs2_write_inode(fs, di->i_blkno, (char *)di);
	if (ret)
		goto out;

	/* inode is updated, the rested errors are not fatal */
	ret = ocfs2_malloc_block(fs->fs_io, &dx_root_buf);
	if (ret)
		goto out;
	ret = ocfs2_read_dx_root(fs, dx_root_blk, dx_root_buf);
	if (ret)
		goto out;
	dx_root = (struct ocfs2_dx_root_block *)dx_root_buf;

	if (dx_root->dr_flags & OCFS2_DX_FLAG_INLINE)
		goto remove_index;

	ret = ocfs2_dir_indexed_tree_truncate(fs, dx_root);

	/*
	 * even ocfs2_dir_indexed_tree_truncate() failed,
	 * we still want to call ocfs2_delete_dx_root().
	 */

remove_index:
	ret = ocfs2_delete_dx_root(fs, dx_root->dr_blkno);
out:
	if (di_buf)
		ocfs2_free(&di_buf);
	if (dx_root_buf)
		ocfs2_free(&dx_root_buf);
	return ret;
}

static unsigned int ocfs2_figure_dirent_hole(struct ocfs2_dir_entry *de)
{
	unsigned int hole;

	if (de->inode == 0)
		hole = de->rec_len;
	else
		hole = de->rec_len - OCFS2_DIR_REC_LEN(de->name_len);

	return hole;
}

int ocfs2_find_max_rec_len(ocfs2_filesys *fs, char *buf)
{
	int size, this_hole, largest_hole = 0;
	char *de_buf, *limit;
	struct ocfs2_dir_entry *de;

	size = ocfs2_dir_trailer_blk_off(fs);
	limit = buf + size;
	de_buf = buf;
	de = (struct ocfs2_dir_entry *)de_buf;
	do {
		this_hole = ocfs2_figure_dirent_hole(de);
		if (this_hole > largest_hole)
			largest_hole = this_hole;

		de_buf += de->rec_len;
		de = (struct ocfs2_dir_entry *)de_buf;
	} while (de_buf < limit);

	if (largest_hole >= OCFS2_DIR_MIN_REC_LEN)
		return largest_hole;
	return 0;
}

struct trailer_ctxt {
	struct ocfs2_dx_root_block *dx_root;
	struct ocfs2_dinode *di;
	errcode_t err;
};

/* make sure the space for trailer is reserved */
static errcode_t ocfs2_check_dir_trailer_space(ocfs2_filesys *fs,
					struct ocfs2_dinode *di,
					uint64_t blkno,
					char *blk)
{
	errcode_t ret = 0;
	struct ocfs2_dir_entry *dirent;
	unsigned int offset = 0;
	unsigned int toff = ocfs2_dir_trailer_blk_off(fs);
	unsigned int real_rec_len = 0;

	while(offset < fs->fs_blocksize) {
		dirent = (struct ocfs2_dir_entry *)(blk + offset);
		if (!ocfs2_check_dir_entry(fs, dirent, blk, offset)) {
			ret = OCFS2_ET_DIR_CORRUPTED;
			break;
		}

		real_rec_len = dirent->inode ?
			OCFS2_DIR_REC_LEN(dirent->name_len) :
			OCFS2_DIR_REC_LEN(1);
		if ((offset + real_rec_len) <= toff)
			goto next;

		if (dirent->inode) {
			ret = OCFS2_ET_DIR_NO_SPACE;
			break;
		}
next:
		offset += dirent->rec_len;
	}

out:
	return ret;
}

static int dir_trailer_func(ocfs2_filesys *fs,
				uint64_t blkno,
				uint64_t bcount,
				uint16_t ext_flags,
				void *priv_data)
{
	struct trailer_ctxt *ctxt = (struct trailer_ctxt *)priv_data;
	struct ocfs2_dinode *di = ctxt->di;
	struct ocfs2_dx_root_block *dx_root = ctxt->dx_root;
	struct ocfs2_dir_block_trailer *trailer;
	int max_rec_len = 0, ret = 0;
	errcode_t err;
	char *blk = NULL;

	ret = ocfs2_malloc_block(fs->fs_io, &blk);
	if (ret)
		goto out;

	/* here we don't trust trailer, cannot use
	 * ocfs2_read_dir_block() */
	err = ocfs2_read_blocks(fs, blkno, 1, blk);
	if (err) {
		ctxt->err = err;
		ret = OCFS2_EXTENT_ERROR;
		goto out;
	}

	err = ocfs2_check_dir_trailer_space(fs, di, blkno, blk);
	if (err) {
		ctxt->err = err;
		ret = OCFS2_EXTENT_ERROR;
		goto out;
	}

	ocfs2_init_dir_trailer(fs, di, blkno, blk);
	max_rec_len = ocfs2_find_max_rec_len(fs, blk);
	trailer = ocfs2_dir_trailer_from_block(fs, blk);
	trailer->db_free_rec_len = max_rec_len;

	if (max_rec_len) {
		trailer->db_free_next = dx_root->dr_free_blk;
		dx_root->dr_free_blk = blkno;
	}

	/* comput trailer->db_check here, after writes out,
	 * trailer is trustable */
	err = ocfs2_write_dir_block(fs, di, blkno, blk);
	if (err) {
		ctxt->err = err;
		ret = OCFS2_EXTENT_ERROR;
	}

out:
	if (blk)
		ocfs2_free(&blk);
	return ret;
}

static errcode_t ocfs2_init_dir_trailers(ocfs2_filesys *fs,
				struct ocfs2_dinode *di,
				struct ocfs2_dx_root_block *dx_root)
{
	errcode_t ret = 0;
	struct trailer_ctxt ctxt;

	if (di->i_dyn_features & OCFS2_INLINE_DATA_FL) {
		ret = OCFS2_ET_INODE_NOT_VALID;
		goto out;
	}

	ctxt.di = di;
	ctxt.dx_root = dx_root;
	ctxt.err = 0;
	ret = ocfs2_block_iterate_inode(fs, di,
			0, dir_trailer_func, &ctxt);

	/* callback dir_trailer_func() may have error which can not
	 * return to its caller directly. If dir_trailer_func() sets
	 * error in ctxt.err, we should take this REAL error other
	 * than the value returned by ocfs2_block_iterate_inode(). */
	if (ctxt.err)
		ret = ctxt.err;
out:
	return ret;
}

static void ocfs2_dx_entry_list_insert(struct ocfs2_dx_entry_list *entry_list,
					struct ocfs2_dx_hinfo *hinfo,
					uint64_t dirent_blk)
{
	int i;
	struct ocfs2_dx_entry *dx_entry;

	i = entry_list->de_num_used;
	dx_entry = &entry_list->de_entries[i];

	memset(dx_entry, 0, sizeof(struct ocfs2_dx_entry));
	dx_entry->dx_major_hash = hinfo->major_hash;
	dx_entry->dx_minor_hash = hinfo->minor_hash;
	dx_entry->dx_dirent_blk = dirent_blk;

	entry_list->de_num_used += 1;
}

struct dx_insert_ctxt {
	uint64_t dir_blkno;
	uint64_t dx_root_blkno;
	ocfs2_filesys *fs;
};


inline static int ocfs2_inline_dx_has_space(struct ocfs2_dx_root_block *dx_root)
{
	struct ocfs2_dx_entry_list *entry_list;

	entry_list = &dx_root->dr_entries;

	if (entry_list->de_num_used >= entry_list->de_count)
		return 0;

	return 1;
}

static struct ocfs2_dx_leaf **ocfs2_dx_dir_alloc_leaves(ocfs2_filesys *fs,
					int *ret_num_leaves)
{
	errcode_t num_dx_leaves = ocfs2_clusters_to_blocks(fs, 1);
	char **dx_leaves_buf = NULL;

	dx_leaves_buf = calloc(num_dx_leaves, sizeof (void *));
	if (dx_leaves_buf && ret_num_leaves)
		*ret_num_leaves = num_dx_leaves;

	return (struct ocfs2_dx_leaf **)dx_leaves_buf;
}

static errcode_t ocfs2_dx_dir_format_cluster(ocfs2_filesys *fs,
				struct ocfs2_dx_leaf  **dx_leaves,
				int num_dx_leaves,
				uint64_t start_blk)
{
	errcode_t ret;
	int i;
	struct ocfs2_dx_leaf *dx_leaf;
	char *blk;

	for (i = 0; i < num_dx_leaves; i++) {
		ret = ocfs2_malloc_block(fs->fs_io, &blk);
		if (ret)
			goto out;

		dx_leaves[i] = (struct ocfs2_dx_leaf *)blk;
		dx_leaf = (struct ocfs2_dx_leaf *)blk;

		memset(dx_leaf, 0, fs->fs_blocksize);
		strcpy((char *)dx_leaf->dl_signature, OCFS2_DX_LEAF_SIGNATURE);
		dx_leaf->dl_fs_generation = fs->fs_super->i_fs_generation;
		dx_leaf->dl_blkno = start_blk + i;
		dx_leaf->dl_list.de_count = ocfs2_dx_entries_per_leaf(fs->fs_blocksize);

		ret = ocfs2_write_dx_leaf(fs, dx_leaf->dl_blkno, dx_leaf);
		if (ret)
			goto out;
	}
	ret = 0;
out:
	return ret;
}

static inline unsigned int __ocfs2_dx_dir_hash_idx(ocfs2_filesys *fs,
						uint32_t minor_hash)
{
	unsigned int cbits, bbits, dx_mask;

	cbits = OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits;
	bbits = OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;
	dx_mask = (1 << (cbits - bbits)) -1;

	return (minor_hash & dx_mask);
}

static inline unsigned int ocfs2_dx_dir_hash_idx(ocfs2_filesys *fs,
					struct ocfs2_dx_hinfo *hinfo)
{
	return __ocfs2_dx_dir_hash_idx(fs, hinfo->minor_hash);
}

static void ocfs2_dx_dir_leaf_insert_tail(struct ocfs2_dx_leaf *dx_leaf,
				struct ocfs2_dx_entry *dx_new_entry)
{
	int i;

	i = dx_leaf->dl_list.de_num_used;
	dx_leaf->dl_list.de_entries[i] = *dx_new_entry;

	dx_leaf->dl_list.de_num_used += 1;
}

static errcode_t ocfs2_expand_inline_dx_root(ocfs2_filesys *fs,
					struct ocfs2_dx_root_block *dx_root)
{
	errcode_t ret;
	int num_dx_leaves, i, j;
	uint64_t start_blkno = 0;
	uint32_t clusters_found = 0;
	struct ocfs2_dx_leaf **dx_leaves = NULL;
	struct ocfs2_dx_leaf *target_leaf;
	struct ocfs2_dx_entry_list *entry_list;
	struct ocfs2_extent_tree et;
	struct ocfs2_dx_entry *dx_entry;

	dx_leaves = ocfs2_dx_dir_alloc_leaves(fs, &num_dx_leaves);
	if (!dx_leaves) {
		ret = OCFS2_ET_NO_MEMORY;
		goto out;
	}

	ret = ocfs2_new_clusters(fs, 1, 1, &start_blkno, &clusters_found);
	if (ret)
		goto out;
	assert(clusters_found == 1);
	ret = ocfs2_dx_dir_format_cluster(fs, dx_leaves,
				num_dx_leaves, start_blkno);
	if (ret)
		goto out;

	/*
	 * Transfer the entries from inline dx_root into the appropriate
	 * block
	 */
	entry_list = &dx_root->dr_entries;

	for (i = 0; i < entry_list->de_num_used; i++) {
		dx_entry = &entry_list->de_entries[i];
		j = __ocfs2_dx_dir_hash_idx(fs, dx_entry->dx_minor_hash);
		target_leaf = (struct ocfs2_dx_leaf *)dx_leaves[j];
		ocfs2_dx_dir_leaf_insert_tail(target_leaf, dx_entry);
	}

	/*
	 * Write out all leaves.
	 * If ocfs2_write_dx_leaf() failed, since dx_root is not cleared
	 * yet, and the leaves are not inserted into indexed tree yet,
	 * this cluster will be recoganized as orphan in blocks scan of
	 * fsck.ocfs2
	 */
	for (i = 0; i < num_dx_leaves; i ++) {
		target_leaf = (struct ocfs2_dx_leaf *)dx_leaves[i];
		ret = ocfs2_write_dx_leaf(fs, target_leaf->dl_blkno,
					  target_leaf);
		if (ret)
			goto out;
	}

	dx_root->dr_flags &= ~OCFS2_DX_FLAG_INLINE;
	memset(&dx_root->dr_list, 0, fs->fs_blocksize -
		offsetof(struct ocfs2_dx_root_block, dr_list));
	dx_root->dr_list.l_count =
		ocfs2_extent_recs_per_dx_root(fs->fs_blocksize);

	/* This should never fail considering we start with an empty
	 * dx_root */
	ocfs2_init_dx_root_extent_tree(&et, fs, (char *)dx_root, dx_root->dr_blkno);
	ret = ocfs2_tree_insert_extent(fs, &et, 0, start_blkno, 1, 0);
	if (ret)
		goto out;

out:
	return ret;
}

static errcode_t ocfs2_dx_dir_lookup_rec(ocfs2_filesys *fs,
		struct ocfs2_dx_root_block *dx_root,
		struct ocfs2_extent_list *el,
		uint32_t major_hash,
		uint32_t *ret_cpos,
		uint64_t *ret_phys_blkno,
		unsigned int *ret_clen)
{
	errcode_t ret = 0;
	int i, found;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_rec *rec = NULL;
	char *eb_buf = NULL;

	if (el->l_tree_depth) {
		ret = ocfs2_tree_find_leaf(fs,
					&dx_root->dr_list,
					dx_root->dr_blkno,
					(char *)dx_root,
					major_hash, &eb_buf);
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
		if (rec->e_cpos <= major_hash) {
			found = 1;
			break;
		}
	}
	if (!found) {
		ret = OCFS2_ET_CORRUPT_EXTENT_BLOCK;
		goto out;
	}

	if (ret_phys_blkno)
		*ret_phys_blkno = rec->e_blkno;
	if (ret_cpos)
		*ret_cpos = rec->e_cpos;
	if (ret_clen)
		*ret_clen = rec->e_leaf_clusters;

out:
	if (eb_buf)
		ocfs2_free(&eb_buf);
	return ret;
}

errcode_t ocfs2_dx_dir_lookup(ocfs2_filesys *fs,
			struct ocfs2_dx_root_block *dx_root,
			struct ocfs2_extent_list *el,
			struct ocfs2_dx_hinfo *hinfo,
			uint32_t *ret_cpos,
			uint64_t *ret_phys_blkno)
{
	errcode_t ret = 0;
	unsigned int cend = 0, clen = 0;
	uint32_t cpos = 0;
	uint64_t blkno = 0;
	uint32_t name_hash = hinfo->major_hash;

	ret = ocfs2_dx_dir_lookup_rec(fs, dx_root, el,
			name_hash, &cpos, &blkno, &clen);
	if (ret)
		goto out;
	cend = cpos + clen;
	if (name_hash >= cend) {
		blkno += ocfs2_clusters_to_blocks(fs, clen - 1);
		cpos += clen - 1;
	} else {
		blkno += ocfs2_clusters_to_blocks(fs, name_hash - cpos);
		cpos = name_hash;
	}

	blkno += ocfs2_dx_dir_hash_idx(fs, hinfo);

	if (ret_phys_blkno)
		*ret_phys_blkno = blkno;
	if (ret_cpos)
		*ret_cpos = cpos;

out:
	return ret;
}

static int dx_leaf_sort_cmp(const void *a, const void *b)
{
	const struct ocfs2_dx_entry *e1 = a;
	const struct ocfs2_dx_entry *e2 = b;
	uint32_t major_hash1 = e1->dx_major_hash;
	uint32_t major_hash2 = e2->dx_major_hash;
	uint32_t minor_hash1 = e1->dx_minor_hash;
	uint32_t minor_hash2 = e2->dx_minor_hash;

	if (major_hash1 > major_hash2)
		return 1;
	if (major_hash1 < major_hash2)
		return -1;

	/* it is not strictly necessary to sort by minor */
	if (minor_hash1 > minor_hash2)
		return 1;
	if (minor_hash1 < minor_hash2)
		return -1;
	return 0;
}

static void dx_leaf_sort_swap(void *a, void *b, int size)
{
	struct ocfs2_dx_entry *e1 = a;
	struct ocfs2_dx_entry *e2 = b;
	struct ocfs2_dx_entry tmp;

	assert(size == sizeof (struct ocfs2_dx_entry));

	tmp = *e1;
	*e1 = *e2;
	*e2 = tmp;
}

static int ocfs2_dx_leaf_same_major(struct ocfs2_dx_leaf *dx_leaf)
{
	struct ocfs2_dx_entry_list *dl_list = &dx_leaf->dl_list;
	int i, num = dl_list->de_num_used;

	for (i = 0; i < (num - 1); i++) {
		if (dl_list->de_entries[i].dx_major_hash !=
		    dl_list->de_entries[i + 1].dx_major_hash)
			return 0;
	}
	return 1;
}

/*
 * Find the optimal value to split this leaf on. This expects the leaf
 * entries to be in sorted order.
 *
 * leaf_cpos is the cpos of the leaf we're splitting. insert_hash is
 * the hash we want to insert.
 *
 * This function is only concerned with the major hash - that which
 * determines which cluster an item belongs to.
 */
static int ocfs2_dx_dir_find_leaf_split(struct ocfs2_dx_leaf *dx_leaf,
					uint32_t leaf_cpos,
					uint32_t insert_hash,
					uint32_t *split_hash)
{
	struct ocfs2_dx_entry_list *dl_list = &dx_leaf->dl_list;
	int i, num_used = dl_list->de_num_used;
	int allsame;

	/*
	 * There's a couple rare, but nasty corner cases we have to
	 * check for here. All of them involve a leaf where all value
	 * have the same hash, which is what we look for first.
	 *
	 * Most of the time, all of the above is false, and we simply
	 * pick the median value for a split.
	 */
	allsame = ocfs2_dx_leaf_same_major(dx_leaf);
	if (allsame) {
		uint32_t val = dl_list->de_entries[0].dx_major_hash;
		if (val == insert_hash) {
			/*
			 * No matter where we would choose to split,
			 * the new entry would want to occupy the same
			 * block as these. Since there's no space left
			 * in their existing block, we know there
			 * won't be space after the split.
			 */
			return OCFS2_ET_DIR_NO_SPACE;
		}

		if (val == leaf_cpos) {
			/*
			 * Because val is the same as leaf_cpos (which
			 * is the smallest value this leaf can have),
			 * yet is not equal to insert_hash, then we
			 * know that insert_hash *must* be larger than
			 * val (and leaf_cpos). At least cpos+1 in value.
			 *
			 * We also know then, that there cannot be an
			 * adjacent extent (otherwise we'd be looking
			 * at it). Choosing this value gives us a
			 * chance to get some continguousness.
			 */
			*split_hash = leaf_cpos + 1;
			return 0;
		}

		if (val > insert_hash) {
			/*
			 * val can not be the same as insert_hash, and
			 * also must be larger than leaf_cpos. Also,
			 * we know that there can't be a leaf between
			 * cpos and val, otherwise the entries with
			 * hash 'val' would be there.
			 */
			*split_hash = val;
			return 0;
		}

		*split_hash = insert_hash;
		return 0;
	}

	/*
	 * Since the records are sorted and the checks above
	 * guaranteed that not all records in this block are the same,
	 * we simple travel forward, from the median, and pick the 1st
	 * record whose value is larger than leaf_cpos.
	 */
	for (i = (num_used /2); i < num_used; i++) {
		if (dl_list->de_entries[i].dx_major_hash > leaf_cpos)
			break;
	}

	assert(i < num_used); /* Should be impossible */
	*split_hash = dl_list->de_entries[i].dx_major_hash;
	return 0;
}

static errcode_t ocfs2_read_dx_leaves(ocfs2_filesys *fs,
				uint64_t start,
				int num,
				struct ocfs2_dx_leaf **dx_leaves)
{
	errcode_t ret;
	int i;
	struct ocfs2_dx_leaf *dx_leaf;
	for (i = 0; i < num; i++) {
		assert(!dx_leaves[i]);
		ret = ocfs2_malloc_block(fs->fs_io, (char **)&dx_leaf);
		if (ret)
			goto bail;
		ret = ocfs2_read_dx_leaf(fs, start + i, (char *)dx_leaf);
		if (ret)
			goto bail;
		dx_leaves[i] = dx_leaf;
	}
	goto out;

bail:
	for (; i >= 0; i--) {
		if (dx_leaves[i])
			ocfs2_free(&dx_leaves[i]);
	}
out:
	return ret;
}

static errcode_t __ocfs2_dx_dir_new_cluster(ocfs2_filesys *fs,
					uint32_t cpos,
					struct ocfs2_dx_leaf **dx_leaves,
					int num_dx_leaves,
					uint64_t *ret_phys_blkno)
{
	errcode_t ret;
	uint32_t num;
	uint64_t phys;

	ret = ocfs2_new_clusters(fs, 1, 1, &phys, &num);
	if (ret)
		goto out;
	assert(num == 1);
	ret = ocfs2_dx_dir_format_cluster(fs, dx_leaves,
				num_dx_leaves, phys);
	if (ret)
		goto out;

	*ret_phys_blkno = phys;

out:
	return ret;
}

static errcode_t ocfs2_dx_dir_new_cluster(ocfs2_filesys *fs,
				struct ocfs2_extent_tree *et,
				uint32_t cpos,
				uint64_t *phys_blocknr,
				struct ocfs2_dx_leaf **dx_leaves,
				int num_dx_leaves)
{
	errcode_t ret;
	uint64_t blkno;
	ret = __ocfs2_dx_dir_new_cluster(fs, cpos, dx_leaves,
					num_dx_leaves, &blkno);
	 if (ret)
		 goto out;

	 *phys_blocknr = blkno;
	 ret = ocfs2_tree_insert_extent(fs, et, cpos, blkno, 1, 0);

out:
	 return ret;
}


static errcode_t ocfs2_dx_dir_transfer_leaf(ocfs2_filesys *fs,
				uint32_t split_hash,
				struct ocfs2_dx_leaf *tmp_dx_leaf,
				struct ocfs2_dx_leaf **orig_dx_leaves,
				uint64_t orig_dx_leaves_blkno,
				struct ocfs2_dx_leaf **new_dx_leaves,
				uint64_t new_dx_leaves_blkno,
				int num_dx_leaves)
{
	errcode_t ret;
	int i, j, num_used;
	uint32_t major_hash;
	struct ocfs2_dx_leaf *orig_dx_leaf, *new_dx_leaf;
	struct ocfs2_dx_entry_list *orig_list, *new_list, *tmp_list;
	struct ocfs2_dx_entry *dx_entry;

	tmp_list = &tmp_dx_leaf->dl_list;

	for (i = 0; i < num_dx_leaves; i++) {
		orig_dx_leaf = orig_dx_leaves[i];
		orig_list = &orig_dx_leaf->dl_list;
		new_dx_leaf = new_dx_leaves[i];
		new_list = &new_dx_leaf->dl_list;

		num_used = orig_list->de_num_used;

		memcpy(tmp_dx_leaf, orig_dx_leaf, fs->fs_blocksize);
		tmp_list->de_num_used = 0;
		memset(&tmp_list->de_entries, 0,
				sizeof(struct ocfs2_dx_entry) * num_used);

		for (j = 0; j < num_used; j++) {
			dx_entry = &orig_list->de_entries[j];
			major_hash = dx_entry->dx_major_hash;
			if (major_hash >= split_hash)
				ocfs2_dx_dir_leaf_insert_tail(new_dx_leaf,
								dx_entry);
			else
				ocfs2_dx_dir_leaf_insert_tail(tmp_dx_leaf,
								dx_entry);
		}
		memcpy(orig_dx_leaf, tmp_dx_leaf, fs->fs_blocksize);

		ret = ocfs2_write_dx_leaf(fs, orig_dx_leaves_blkno + i,
						(char *)orig_dx_leaf);
		if (ret)
			goto out;
		ret = ocfs2_write_dx_leaf(fs, new_dx_leaves_blkno + i,
						(char *)new_dx_leaf);
		if (ret)
			goto out;
	}

out:
	return ret;
}

static int ocfs2_dx_dir_free_leaves(ocfs2_filesys *fs,
				struct ocfs2_dx_leaf **dx_leaves)
{
	int i, num;

	num = ocfs2_clusters_to_blocks(fs, 1);
	for (i = 0; i < num; i++) {
		if (dx_leaves[i])
			ocfs2_free(&dx_leaves[i]);
	}
	free(dx_leaves);
	return 0;
}

/* from Linux kernel lib/sort.c */
static void ocfs2_sort(void *base, size_t num, size_t size,
			int (*cmp_func)(const void *, const void *),
			void (*swap_func)(void *, void *, int size))
{
	/* pre-scale counters for performance */
	int i = (num/2 - 1) * size, n = num * size, c, r;

	/* heapify */
	for (; i >= 0; i -= size) {
		for (r = i; r * 2 + size < n; r = c) {
			c = r * 2 + size;
			if (c < n - size &&
			    cmp_func(base + c, base + c + size) < 0)
				c += size;
			if (cmp_func(base + r, base + c) >= 0)
				break;
			swap_func(base + r, base + c, size);
		}
	}

	/* sort */
	for (i = n - size; i > 0; i -= size) {
		swap_func(base, base + i, size);
		for (r = 0; r * 2 + size < i; r = c) {
			c = r * 2 + size;
			if (c < i - size &&
			    cmp_func(base + c, base + c + size) < 0)
				c += size;
			if (cmp_func(base + r, base + c) >= 0)
				break;
			swap_func(base + r, base + c, size);
		}
	}

}

static errcode_t ocfs2_dx_dir_rebalance(ocfs2_filesys *fs,
			struct ocfs2_dx_root_block *dx_root,
			struct ocfs2_dx_leaf *dx_leaf,
			struct ocfs2_dx_hinfo *hinfo,
			uint32_t leaf_cpos,
			uint64_t leaf_blkno)
{
	struct ocfs2_extent_tree et;
	struct ocfs2_dx_leaf **orig_dx_leaves = NULL;
	struct ocfs2_dx_leaf **new_dx_leaves = NULL;
	struct ocfs2_dx_leaf *tmp_dx_leaf = NULL;
	uint32_t insert_hash = hinfo->major_hash;
	uint32_t split_hash, cpos;
	uint64_t orig_leaves_start, new_leaves_start;
	errcode_t ret;
	int num_used, num_dx_leaves;

	ocfs2_init_dx_root_extent_tree(&et, fs, (char *)dx_root, dx_root->dr_blkno);

	if (dx_root->dr_clusters == UINT_MAX) {
		ret = OCFS2_ET_DIR_NO_SPACE;
		goto out;
	}

	num_used = dx_leaf->dl_list.de_num_used;
	if (num_used < dx_leaf->dl_list.de_count) {
		ret = OCFS2_ET_DX_BALANCE_EMPTY_LEAF;
		goto out;
	}

	orig_dx_leaves = ocfs2_dx_dir_alloc_leaves(fs, &num_dx_leaves);
	if (!orig_dx_leaves) {
		ret = OCFS2_ET_NO_MEMORY;
		goto out;
	}

	new_dx_leaves = ocfs2_dx_dir_alloc_leaves(fs, NULL);
	if (!new_dx_leaves) {
		ret = OCFS2_ET_NO_MEMORY;
		goto out;
	}

	ocfs2_sort(dx_leaf->dl_list.de_entries, num_used,
		sizeof(struct ocfs2_dx_entry), dx_leaf_sort_cmp,
		dx_leaf_sort_swap);

	ret = ocfs2_dx_dir_find_leaf_split(dx_leaf, leaf_cpos,
				insert_hash, &split_hash);
	if (ret)
		goto out;

	ret = ocfs2_malloc_block(fs->fs_io, (char **)(&tmp_dx_leaf));
	if (ret)
		goto out;
	orig_leaves_start = ocfs2_blocks_to_clusters(fs, leaf_blkno);
	ret = ocfs2_read_dx_leaves(fs, orig_leaves_start, num_dx_leaves,
					orig_dx_leaves);
	if (ret)
		goto out;

	cpos = split_hash;
	ret = ocfs2_dx_dir_new_cluster(fs, &et, cpos, &new_leaves_start,
					new_dx_leaves, num_dx_leaves);
	if (ret)
		goto out;
	ret = ocfs2_dx_dir_transfer_leaf(fs, split_hash, tmp_dx_leaf,
				orig_dx_leaves, orig_leaves_start,
				new_dx_leaves, new_leaves_start,
				num_dx_leaves);

out:
	if (tmp_dx_leaf)
		ocfs2_free((char **)(&tmp_dx_leaf));

	if (orig_dx_leaves)
		ocfs2_dx_dir_free_leaves(fs, orig_dx_leaves);
	if (new_dx_leaves)
		ocfs2_dx_dir_free_leaves(fs, new_dx_leaves);
	return ret;
}

static errcode_t ocfs2_find_dir_space_dx(ocfs2_filesys *fs,
			struct ocfs2_dx_root_block *dx_root,
			const char *name, int namelen,
			struct ocfs2_dir_lookup_result *lookup)
{
	errcode_t ret;
	int rebalanced = 0;
	struct ocfs2_dx_leaf *dx_leaf;
	char *dx_leaf_buf = NULL;
	uint64_t blkno;
	uint32_t leaf_cpos;

	ret = ocfs2_malloc_block(fs->fs_io, &dx_leaf_buf);
	if (ret)
		goto out;

restart_search:
	ret = ocfs2_dx_dir_lookup(fs, dx_root, &dx_root->dr_list,
				&lookup->dl_hinfo, &leaf_cpos, &blkno);
	if (ret)
		goto out;
	ret = ocfs2_read_dx_leaf(fs, blkno, dx_leaf_buf);
	if (ret)
		goto out;
	dx_leaf = (struct ocfs2_dx_leaf *)dx_leaf_buf;
	if (dx_leaf->dl_list.de_num_used >= dx_leaf->dl_list.de_count) {
		if (rebalanced) {
			/*
			 * Rebalancing should have provided us with
			 * space in an appropriate leaf.
			 */
			ret = OCFS2_ET_DIR_NO_SPACE;
			goto out;
		}

		ret = ocfs2_dx_dir_rebalance(fs, dx_root, dx_leaf,
					&lookup->dl_hinfo, leaf_cpos, blkno);
		if (ret)
			goto out;
		rebalanced = 1;
		goto restart_search;
	}
	lookup->dl_dx_leaf_blkno = blkno;

out:
	if (dx_leaf_buf)
		ocfs2_free(&dx_leaf_buf);
	return ret;
}

/*
 * Hashing code adapted from ext3
 */
#define DELTA 0x9E3779B9

static void TEA_transform(uint32_t buf[4], uint32_t const in[])
{
	uint32_t sum = 0;
	uint32_t b0 = buf[0], b1 = buf[1];
	uint32_t a = in[0], b = in[1], c = in[2], d = in[3];
	int n = 16;

	do {
		sum += DELTA;
		b0 += ((b1 << 4)+a) ^ (b1+sum) ^ ((b1 >> 5)+b);
		b1 += ((b0 << 4)+c) ^ (b0+sum) ^ ((b0 >> 5)+d);
	} while (--n);

	buf[0] += b0;
	buf[1] += b1;
}

static void str2hashbuf(const char *msg, int len, uint32_t *buf, int num)
{
	uint32_t pad, val;
	int i;

	pad = (uint32_t)len | ((uint32_t)len << 8);
	pad |= pad << 16;

	val = pad;
	if (len > (num * 4))
		len = num * 4;
	for (i = 0; i < len; i++) {
		if ((i % 4) == 0)
			val = pad;
		val = msg[i] + (val << 8);
		if ((i % 4) == 3) {
			*buf++ = val;
			val = pad;
			num --;
		}
	}
	if (--num >= 0)
		*buf++ = val;
	while(--num >= 0)
		*buf++ = pad;
}

void ocfs2_dx_dir_name_hash(ocfs2_filesys *fs,
				const char *name,
				int len,
				struct ocfs2_dx_hinfo *hinfo)
{
	const char *p;
	uint32_t in[8], buf[4];

	/*
	 * XXX: Is this really necessary, if the index is never looked
	 * at by readdir? Is a hash value of '0' a bad idea ?
	 */
	if ((len == 1 && !strncmp(".", name, 1)) ||
	    (len == 2 && !strncmp("..", name, 2))) {
		buf[0] = buf[1] = 0;
		goto out;
	}

	memcpy(buf, OCFS2_RAW_SB(fs->fs_super)->s_dx_seed, sizeof(buf));

	p = name;
	while(len > 0) {
		str2hashbuf(p, len, in, 4);
		TEA_transform(buf, in);
		len -= 16;
		p += 16;
	}

out:
	hinfo->major_hash = buf[0];
	hinfo->minor_hash = buf[1];
}

static int ocfs2_dx_dir_insert(struct ocfs2_dir_entry *dentry,
				uint64_t blocknr,
				int offset,
				int blocksize,
				char *buf,
				void *priv_data)
{
	errcode_t ret = 0;
	char *dx_buf = NULL;
	char *dx_leaf_buf = NULL;
	struct ocfs2_dx_root_block *dx_root = NULL;
	struct ocfs2_dx_leaf *dx_leaf = NULL;
	struct ocfs2_dir_lookup_result lookup;
	struct ocfs2_dx_entry_list *entry_list;
	struct dx_insert_ctxt *ctxt = (struct dx_insert_ctxt *)priv_data;
	ocfs2_filesys *fs = ctxt->fs;
	uint64_t dx_root_blkno = ctxt->dx_root_blkno;
	int write_dx_leaf = 0;

	ret = ocfs2_malloc_block(fs->fs_io, &dx_buf);
	if (ret)
		goto out;

	ret = ocfs2_malloc_block(fs->fs_io, &dx_leaf_buf);
	if (ret)
		goto out;

	ret = ocfs2_read_dx_root(fs, dx_root_blkno, dx_buf);
	if (ret)
		goto out;

	dx_root = (struct ocfs2_dx_root_block *)dx_buf;
	memset(&lookup, 0, sizeof(struct ocfs2_dir_lookup_result));
	ocfs2_dx_dir_name_hash(fs, dentry->name,
				dentry->name_len, &lookup.dl_hinfo);

	if (dx_root->dr_flags & OCFS2_DX_FLAG_INLINE) {
		if (ocfs2_inline_dx_has_space(dx_root)) {
			entry_list = &dx_root->dr_entries;
			goto insert_into_entries;
		} else {
			/* root block is full, expand it to an extent */
			ret = ocfs2_expand_inline_dx_root(fs, dx_root);
			if (ret)
				goto out;
		}
	}

	ret = ocfs2_find_dir_space_dx(fs, dx_root,
				dentry->name, dentry->name_len, &lookup);
	if (ret)
		goto out;
	ret = ocfs2_read_dx_leaf(fs, lookup.dl_dx_leaf_blkno, dx_leaf_buf);
	if (ret)
		goto out;
	dx_leaf = (struct ocfs2_dx_leaf *)dx_leaf_buf;
	entry_list = &dx_leaf->dl_list;
	write_dx_leaf = 1;

insert_into_entries:
	ocfs2_dx_entry_list_insert(entry_list, &lookup.dl_hinfo, blocknr);
	if (write_dx_leaf) {
		ret = ocfs2_write_dx_leaf(fs, dx_leaf->dl_blkno, dx_leaf);
		if (ret)
			goto out;
	}
	dx_root->dr_num_entries += 1;
	ret = ocfs2_write_dx_root(fs, dx_root_blkno, dx_buf);
out:
	if (dx_leaf_buf)
		ocfs2_free(&dx_leaf_buf);
	if (dx_buf)
		ocfs2_free(&dx_buf);
	return ret;
}

errcode_t ocfs2_dx_dir_insert_entry(ocfs2_filesys *fs, uint64_t dir, const char *name,
				uint64_t ino, uint64_t blkno)
{
	struct ocfs2_dir_entry dummy_de;
	struct dx_insert_ctxt dummy_ctxt;
	char *di_buf = NULL;
	struct ocfs2_dinode *di;
	errcode_t ret = 0;

	if (!ocfs2_supports_indexed_dirs(OCFS2_RAW_SB(fs->fs_super)))
		goto out;

	assert(name);
	memset(&dummy_de, 0, sizeof(struct ocfs2_dir_entry));
	memcpy(dummy_de.name, name, strlen(name));
	dummy_de.name_len = strlen(name);

	ret = ocfs2_malloc_block(fs->fs_io, &di_buf);
	if (ret)
		goto out;
	ret = ocfs2_read_inode(fs, dir, di_buf);
	if (ret)
		goto out;
	di = (struct ocfs2_dinode *)di_buf;

	if (!(di->i_dyn_features & OCFS2_INDEXED_DIR_FL))
		goto out;

	memset(&dummy_ctxt, 0, sizeof(struct dx_insert_ctxt));
	dummy_ctxt.dir_blkno = dir;
	dummy_ctxt.fs = fs;
	dummy_ctxt.dx_root_blkno = di->i_dx_root;

	ret = ocfs2_dx_dir_insert(&dummy_de, blkno, 0,
			fs->fs_blocksize, NULL, &dummy_ctxt);
out:
	if (di_buf)
		ocfs2_free(&di_buf);
	return ret;
}


/*
 * This function overwite the indexed dir attribute of
 * the given inode. The caller should make sure the dir's
 * indexed tree is truncated.
 * Currently tunefs.ocfs2 is the only user, before calling
 * this function, tunefs.ocfs2 makes sure there is space
 * for directory trailer. So directory entry moves here.
 */
errcode_t ocfs2_dx_dir_build(ocfs2_filesys *fs,
			uint64_t dir)
{
	errcode_t ret = 0, err;
	uint64_t dr_blkno;
	char *dx_buf = NULL, *di_buf = NULL;
	struct ocfs2_dinode *di;
	struct ocfs2_dx_root_block *dx_root;
	struct dx_insert_ctxt ctxt;
	ocfs2_quota_hash *usrhash = NULL, *grphash = NULL;
	uint32_t uid, gid;
	long long change;

	ret = ocfs2_load_fs_quota_info(fs);
	if (ret)
		goto out;

	ret = ocfs2_init_quota_change(fs, &usrhash, &grphash);
	if (ret)
		goto out;

	ret = ocfs2_malloc_block(fs->fs_io, &di_buf);
	if (ret)
		goto out;

	ret = ocfs2_read_inode(fs, dir, di_buf);
	if (ret)
		goto out;
	di = (struct ocfs2_dinode *)di_buf;

	if ((ocfs2_dir_indexed(di)) ||
	    (di->i_dyn_features & OCFS2_INLINE_DATA_FL))
		goto out;

	ret = ocfs2_new_dx_root(fs, di, &dr_blkno);
	if (ret)
		goto out;

	ret = ocfs2_malloc_block(fs->fs_io, &dx_buf);
	if (ret)
		goto out;

	ret = ocfs2_read_dx_root(fs, dr_blkno, dx_buf);
	if (ret)
		goto out;
	dx_root = (struct ocfs2_dx_root_block *)dx_buf;

	ret = ocfs2_init_dir_trailers(fs, di, dx_root);
	if (ret)
		goto out;

	dx_root->dr_dir_blkno = di->i_blkno;
	dx_root->dr_num_entries = 0;
	dx_root->dr_entries.de_count = ocfs2_dx_entries_per_root(fs->fs_blocksize);

	di->i_dx_root = dr_blkno;
	di->i_dyn_features |= OCFS2_INDEXED_DIR_FL;

	ret = ocfs2_write_dx_root(fs, dr_blkno, dx_buf);
	if (ret)
		goto out;
	ret = ocfs2_write_inode(fs, dir, di_buf);
	if (ret)
		goto out;

	ctxt.dir_blkno = dir;
	ctxt.dx_root_blkno = dr_blkno;
	ctxt.fs = fs;
	ret = ocfs2_dir_iterate(fs, dir, 0, NULL,
				ocfs2_dx_dir_insert,  &ctxt);

	/* check quota for dx_leaf */
	ret = ocfs2_read_dx_root(fs, dr_blkno, dx_buf);
	if (ret)
		goto out;
	ret = ocfs2_read_inode(fs, dir, di_buf);
	if (ret)
		goto out;

	change = ocfs2_clusters_to_bytes(fs,
				dx_root->dr_clusters);
	uid = di->i_uid;
	gid = di->i_gid;

	ret = ocfs2_apply_quota_change(fs, usrhash, grphash,
					uid, gid, change, 0);
	if (ret) {
		/* exceed quota, truncate the indexed tree */
		ret = ocfs2_dx_dir_truncate(fs, dir);
	}

out:
	err = ocfs2_finish_quota_change(fs, usrhash, grphash);
	if (!ret)
		ret = err;

	if (di_buf)
		ocfs2_free(&di_buf);
	if (dx_buf)
		ocfs2_free(&dx_buf);

	return ret;
}

void ocfs2_dx_list_remove_entry(struct ocfs2_dx_entry_list *entry_list,
				int index)
{
	int num_used = entry_list->de_num_used;
	if (num_used == 1 || index == (num_used - 1))
		goto clear;

	memmove(&entry_list->de_entries[index],
		&entry_list->de_entries[index + 1],
		(num_used - index - 1)*sizeof(struct ocfs2_dx_entry));
clear:
	num_used --;
	memset(&entry_list->de_entries[num_used], 0,
		sizeof(struct ocfs2_dx_entry));
	entry_list->de_num_used = num_used;
}

static int ocfs2_match(int len,
			const char *name,
			struct ocfs2_dir_entry *de)
{
	if (len != de->name_len)
		return 0;
	if (!de->inode)
		return 0;
	return !memcmp((char *)name, de->name, len);
}

int ocfs2_check_dir_entry(ocfs2_filesys *fs,
			struct ocfs2_dir_entry *de,
			char *dir_buf,
			unsigned int offset)
{
	int rlen = de->rec_len;
	int ret = 1;

	if ((rlen < OCFS2_DIR_REC_LEN(1)) ||
	    (rlen % 4 != 0) ||
	    (rlen < OCFS2_DIR_REC_LEN(de->name_len)) ||
	    (((char *)de - dir_buf) > fs->fs_blocksize))
		ret = 0;

	return ret;
}

int ocfs2_search_dirblock(ocfs2_filesys *fs,
				char *dir_buf,
				const char *name,
				int namelen,
				unsigned int bytes,
				struct ocfs2_dir_entry **res_dir)
{
	struct ocfs2_dir_entry *de;
	char *dlimit, *de_buf;
	int de_len, offset = 0;
	int ret = 0;

	de_buf = (char *)dir_buf;
	dlimit = de_buf + bytes;

	while(de_buf < dlimit) {
		de = (struct ocfs2_dir_entry *)de_buf;

		if ((de_buf + namelen <= dlimit) &&
		     ocfs2_match(namelen, name, de)) {
			if (!ocfs2_check_dir_entry(fs, de, dir_buf, offset)) {
				ret = -1;
				goto out;
			}
			if (res_dir)
				*res_dir = de;
			ret = 1;
			goto out;
		}

		de_len = de->rec_len;
		if (de_len <= 0) {
			ret = -1;
			goto out;
		}
		de_buf += de_len;
		offset += de_len;
	}
out:
	return ret;
}

errcode_t ocfs2_dx_dir_search(ocfs2_filesys *fs,
			const char *name,
			int namelen,
			struct ocfs2_dx_root_block *dx_root,
			struct ocfs2_dir_lookup_result *lookup)
{
	errcode_t ret;
	char *di_buf = NULL, *dir_buf = NULL, *dx_leaf_buf = NULL;
	struct ocfs2_dx_entry_list *entry_list;
	struct ocfs2_dx_leaf *dx_leaf;
	struct ocfs2_dx_entry *dx_entry;
	struct ocfs2_dir_entry *dir_ent;
	uint32_t leaf_cpos;
	uint64_t blkno;
	int i, found;

	if (dx_root->dr_flags & OCFS2_DX_FLAG_INLINE)
		entry_list = &dx_root->dr_entries;
	else {
		ret = ocfs2_dx_dir_lookup(fs, dx_root, &dx_root->dr_list,
				&lookup->dl_hinfo, &leaf_cpos, &blkno);
		if (ret)
			goto out;

		ret = ocfs2_malloc_block(fs->fs_io, &dx_leaf_buf);
		if (ret)
			goto out;

		ret = ocfs2_read_dx_leaf(fs, blkno, dx_leaf_buf);
		if (ret)
			goto out;
		dx_leaf = (struct ocfs2_dx_leaf *)dx_leaf_buf;
		entry_list = &dx_leaf->dl_list;
	}

	assert(entry_list->de_count > 0);
	assert(entry_list->de_num_used > 0);
	assert(dx_root->dr_num_entries > 0);

	ret = ocfs2_malloc_block(fs->fs_io, &dir_buf);
	if (ret)
		goto out;

	found = 0;
	for (i = 0; i < entry_list->de_num_used; i++) {
		dx_entry = &entry_list->de_entries[i];
		if ((lookup->dl_hinfo.major_hash != dx_entry->dx_major_hash) ||
		    (lookup->dl_hinfo.minor_hash != dx_entry->dx_minor_hash))
			continue;

		ret = ocfs2_read_blocks(fs, dx_entry->dx_dirent_blk, 1, dir_buf);
		if (ret)
			goto out;

		found = ocfs2_search_dirblock(fs, dir_buf, name, namelen,
						fs->fs_blocksize, &dir_ent);
		if (found == 1)
			break;

		if (found == -1) {
			ret = OCFS2_ET_DIR_CORRUPTED;
			goto out;
		}
	}

	if (found <= 0) {
		ret = OCFS2_ET_DIRENT_NOT_FOUND;
		goto out;
	}

	lookup->dl_leaf = dir_buf;
	lookup->dl_leaf_blkno = dx_entry->dx_dirent_blk;
	lookup->dl_entry = dir_ent;
	lookup->dl_dx_entry = dx_entry;
	lookup->dl_dx_entry_idx = i;
	if (!(dx_root->dr_flags & OCFS2_DX_FLAG_INLINE)) {
		lookup->dl_dx_leaf = (struct ocfs2_dx_leaf *)dx_leaf_buf;
		lookup->dl_dx_leaf_blkno = blkno;
	}
	ret = 0;
out:
	if (di_buf)
		ocfs2_free(&di_buf);
	if (ret) {
		if (dir_buf)
			ocfs2_free(&dir_buf);
		if (dx_leaf_buf)
			ocfs2_free(&dx_leaf_buf);
	}
	return ret;
}

void release_lookup_res(struct ocfs2_dir_lookup_result *res)
{
	if (res->dl_leaf)
		ocfs2_free(&res->dl_leaf);
	if (res->dl_dx_leaf)
		ocfs2_free(&res->dl_dx_leaf);
}


