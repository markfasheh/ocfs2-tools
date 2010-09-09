/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * xattr.c
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
#include <string.h>
#include <inttypes.h>

#include "ocfs2/byteorder.h"
#include "ocfs2/ocfs2.h"

struct ocfs2_xattr_def_value_root {
	struct ocfs2_xattr_value_root   xv;
	struct ocfs2_extent_rec         er;
};

#define OCFS2_XATTR_ROOT_SIZE	(sizeof(struct ocfs2_xattr_def_value_root))

uint32_t ocfs2_xattr_uuid_hash(unsigned char *uuid)
{
	uint32_t i, hash = 0;

	for (i = 0; i < OCFS2_VOL_UUID_LEN; i++) {
		hash = (hash << OCFS2_HASH_SHIFT) ^
			(hash >> (8*sizeof(hash) - OCFS2_HASH_SHIFT)) ^
			*uuid++;
	}
	return hash;
}

uint32_t ocfs2_xattr_name_hash(uint32_t uuid_hash,
			 const char *name,
			 int name_len)
{
	/* Get hash value of uuid from super block */
	uint32_t hash = uuid_hash;
	int i;

	/* hash extended attribute name */
	for (i = 0; i < name_len; i++) {
		hash = (hash << OCFS2_HASH_SHIFT) ^
		       (hash >> (8*sizeof(hash) - OCFS2_HASH_SHIFT)) ^
		       *name++;
	}

	return hash;
}

uint16_t ocfs2_xattr_buckets_per_cluster(ocfs2_filesys *fs)
{
	return fs->fs_clustersize / OCFS2_XATTR_BUCKET_SIZE;
}

uint16_t ocfs2_blocks_per_xattr_bucket(ocfs2_filesys *fs)
{
	return OCFS2_XATTR_BUCKET_SIZE / fs->fs_blocksize;
}

static void ocfs2_swap_xattr_entry(struct ocfs2_xattr_entry *xe)
{
	xe->xe_name_hash	= bswap_32(xe->xe_name_hash);
	xe->xe_name_offset	= bswap_16(xe->xe_name_offset);
	xe->xe_value_size	= bswap_64(xe->xe_value_size);
}

static void ocfs2_swap_xattr_tree_root(struct ocfs2_xattr_tree_root *xt)
{
	xt->xt_clusters		= bswap_32(xt->xt_clusters);
	xt->xt_last_eb_blk	= bswap_64(xt->xt_last_eb_blk);
}

static void ocfs2_swap_xattr_value_root(struct ocfs2_xattr_value_root *xr)
{
	xr->xr_clusters		= bswap_32(xr->xr_clusters);
	xr->xr_last_eb_blk	= bswap_64(xr->xr_last_eb_blk);
}

static void ocfs2_swap_xattr_block_header(struct ocfs2_xattr_block *xb)
{
	xb->xb_suballoc_slot	= bswap_16(xb->xb_suballoc_slot);
	xb->xb_suballoc_bit	= bswap_16(xb->xb_suballoc_bit);
	xb->xb_fs_generation	= bswap_32(xb->xb_fs_generation);
	xb->xb_blkno		= bswap_64(xb->xb_blkno);
	xb->xb_flags		= bswap_16(xb->xb_flags);
	xb->xb_suballoc_loc	= bswap_64(xb->xb_suballoc_loc);
}

static void ocfs2_swap_xattr_header(struct ocfs2_xattr_header *xh)
{
	if (cpu_is_little_endian)
		return;

	xh->xh_count		= bswap_16(xh->xh_count);
	xh->xh_free_start	= bswap_16(xh->xh_free_start);
	xh->xh_name_value_len	= bswap_16(xh->xh_name_value_len);
	xh->xh_num_buckets	= bswap_16(xh->xh_num_buckets);
}

/*
 * The swap barriers for xattrs are the hardest.  The ocfs2_xattr_header
 * can be at the start of a bucket, inside an xattr block, or at the end
 * of an inode.  Thus, we need to pass obj for the containing object.
 * On top of that, buckets are always 4K, regardless of blocksize.  Thus,
 * we take objsize as an argument and fake the ocfs2_filesys we pass to
 * ocfs2_swap_barrier().
 *
 * Much of this is internal to xattr.c, thankfully.  The callers of the
 * pubic ocfs2_swap_xattr*() APIs don't have to worry about objsize!
 */
static void ocfs2_swap_xattr_entries_to_cpu(ocfs2_filesys *fs, void *obj,
					    size_t objsize,
					    struct ocfs2_xattr_header *xh)
{
	uint16_t i;
	char *value;
	ocfs2_filesys fake_fs = {
		.fs_blocksize = objsize,
	};

	if (cpu_is_little_endian)
		return;

	for (i = 0; i < xh->xh_count; i++) {
		struct ocfs2_xattr_entry *xe = &xh->xh_entries[i];

		if (ocfs2_swap_barrier(&fake_fs, obj, xe,
				       sizeof(struct ocfs2_xattr_entry)))
			break;

		ocfs2_swap_xattr_entry(xe);

		value = (char *)xh + xe->xe_name_offset +
			OCFS2_XATTR_SIZE(xe->xe_name_len);

		if (!ocfs2_xattr_is_local(xe)) {
			struct ocfs2_xattr_value_root *xr =
				(struct ocfs2_xattr_value_root *)value;

			if (ocfs2_swap_barrier(&fake_fs, obj, xr,
					       OCFS2_XATTR_ROOT_SIZE))
				break;

			ocfs2_swap_xattr_value_root(xr);
			ocfs2_swap_extent_list_to_cpu(&fake_fs, xh,
						      &xr->xr_list);
		} else if (ocfs2_swap_barrier(&fake_fs, obj, value,
					      OCFS2_XATTR_SIZE(xe->xe_value_size)))
			break;
	}
}

static void ocfs2_swap_xattr_entries_from_cpu(ocfs2_filesys *fs, void *obj,
					      size_t objsize,
					      struct ocfs2_xattr_header *xh)
{
	uint16_t i;
	char *value;
	ocfs2_filesys fake_fs = {
		.fs_blocksize = objsize,
	};

	if (cpu_is_little_endian)
		return;

	for (i = 0; i < xh->xh_count; i++) {
		struct ocfs2_xattr_entry *xe = &xh->xh_entries[i];

		if (ocfs2_swap_barrier(&fake_fs, obj, xe,
				       sizeof(struct ocfs2_xattr_entry)))
			break;

		value = (char *)xh + xe->xe_name_offset +
			OCFS2_XATTR_SIZE(xe->xe_name_len);

		if (!ocfs2_xattr_is_local(xe)) {
			struct ocfs2_xattr_value_root *xr =
				(struct ocfs2_xattr_value_root *)value;

			if (ocfs2_swap_barrier(&fake_fs, obj, xr,
					       OCFS2_XATTR_ROOT_SIZE))
				break;

			ocfs2_swap_extent_list_from_cpu(&fake_fs, xh,
							&xr->xr_list);
			ocfs2_swap_xattr_value_root(xr);
		} else if (ocfs2_swap_barrier(&fake_fs, obj, value,
					      OCFS2_XATTR_SIZE(xe->xe_value_size)))
			break;

		ocfs2_swap_xattr_entry(xe);
	}
}

static void __ocfs2_swap_xattrs_to_cpu(ocfs2_filesys *fs, void *obj,
				       size_t objsize,
				       struct ocfs2_xattr_header *xh)
{
	ocfs2_swap_xattr_header(xh);
	ocfs2_swap_xattr_entries_to_cpu(fs, obj, objsize, xh);
}

void ocfs2_swap_xattrs_to_cpu(ocfs2_filesys *fs, void *obj,
			      struct ocfs2_xattr_header *xh)
{
	return __ocfs2_swap_xattrs_to_cpu(fs, obj, fs->fs_blocksize, xh);
}

static void __ocfs2_swap_xattrs_from_cpu(ocfs2_filesys *fs, void *obj,
					 size_t objsize,
					 struct ocfs2_xattr_header *xh)
{
	ocfs2_swap_xattr_entries_from_cpu(fs, obj, objsize, xh);
	ocfs2_swap_xattr_header(xh);
}

void ocfs2_swap_xattrs_from_cpu(ocfs2_filesys *fs, void *obj,
				struct ocfs2_xattr_header *xh)
{
	return __ocfs2_swap_xattrs_from_cpu(fs, obj, fs->fs_blocksize, xh);
}

void ocfs2_swap_xattr_block_to_cpu(ocfs2_filesys *fs,
				   struct ocfs2_xattr_block *xb)
{
	if (cpu_is_little_endian)
		return;

	ocfs2_swap_xattr_block_header(xb);
	if (!(xb->xb_flags & OCFS2_XATTR_INDEXED))
		ocfs2_swap_xattrs_to_cpu(fs, xb, &xb->xb_attrs.xb_header);
	else {
		ocfs2_swap_xattr_tree_root(&xb->xb_attrs.xb_root);
		ocfs2_swap_extent_list_to_cpu(fs, xb,
					      &xb->xb_attrs.xb_root.xt_list);
	}
}

void ocfs2_swap_xattr_block_from_cpu(ocfs2_filesys *fs,
				     struct ocfs2_xattr_block *xb)
{
	if (cpu_is_little_endian)
		return;

	if (!(xb->xb_flags & OCFS2_XATTR_INDEXED))
		ocfs2_swap_xattrs_from_cpu(fs, xb, &xb->xb_attrs.xb_header);
	else {
		ocfs2_swap_extent_list_from_cpu(fs, xb,
						&xb->xb_attrs.xb_root.xt_list);
		ocfs2_swap_xattr_tree_root(&xb->xb_attrs.xb_root);
	}

	ocfs2_swap_xattr_block_header(xb);
}

errcode_t ocfs2_read_xattr_block(ocfs2_filesys *fs,
				 uint64_t blkno,
				 char *xb_buf)
{
	errcode_t ret = 0;
	char *blk;
	struct ocfs2_xattr_block *xb;

	if ((blkno < OCFS2_SUPER_BLOCK_BLKNO) ||
	    (blkno > fs->fs_blocks))
		return OCFS2_ET_BAD_BLKNO;

	ret = ocfs2_malloc_block(fs->fs_io, &blk);
	if (ret)
		return ret;

	ret = ocfs2_read_blocks(fs, blkno, 1, blk);
	if (ret)
		goto out;

	xb = (struct ocfs2_xattr_block *)blk;

	ret = ocfs2_validate_meta_ecc(fs, blk, &xb->xb_check);
	if (ret)
		goto out;

	if (memcmp(xb->xb_signature, OCFS2_XATTR_BLOCK_SIGNATURE,
		strlen(OCFS2_XATTR_BLOCK_SIGNATURE))) {
		ret = OCFS2_ET_BAD_XATTR_BLOCK_MAGIC;
		goto out;
	}

	memcpy(xb_buf, blk, fs->fs_blocksize);
	xb = (struct ocfs2_xattr_block *)xb_buf;
	ocfs2_swap_xattr_block_to_cpu(fs, xb);
out:
	ocfs2_free(&blk);
	return ret;
}

errcode_t ocfs2_write_xattr_block(ocfs2_filesys *fs,
				  uint64_t blkno,
				  char *xb_buf)
{
	errcode_t ret = 0;
	char *blk;
	struct ocfs2_xattr_block *xb;

	if (!(fs->fs_flags & OCFS2_FLAG_RW))
		return OCFS2_ET_RO_FILESYS;

	if ((blkno < OCFS2_SUPER_BLOCK_BLKNO) ||
	    (blkno > fs->fs_blocks))
		return OCFS2_ET_BAD_BLKNO;

	ret = ocfs2_malloc_block(fs->fs_io, &blk);
	if (ret)
		return ret;

	memcpy(blk, xb_buf, fs->fs_blocksize);

	xb = (struct ocfs2_xattr_block *)blk;
	ocfs2_swap_xattr_block_from_cpu(fs, xb);

	ocfs2_compute_meta_ecc(fs, blk, &xb->xb_check);
	ret = io_write_block(fs->fs_io, blkno, 1, blk);
	if (!ret)
		fs->fs_flags |= OCFS2_FLAG_CHANGED;

	ocfs2_free(&blk);
	return ret;
}

errcode_t ocfs2_xattr_get_rec(ocfs2_filesys *fs,
			      struct ocfs2_xattr_block *xb,
			      uint32_t name_hash,
			      uint64_t *p_blkno,
			      uint32_t *e_cpos,
			      uint32_t *num_clusters)
{
	int i;
	errcode_t ret = 0;
	char *eb_buf = NULL;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_rec *rec = NULL;
	struct ocfs2_extent_list *el = &xb->xb_attrs.xb_root.xt_list;
	uint64_t e_blkno = 0;

	if (!(xb->xb_flags & OCFS2_XATTR_INDEXED))
		return OCFS2_ET_INVALID_ARGUMENT;

	if (el->l_tree_depth) {
		ret = ocfs2_tree_find_leaf(fs, el, xb->xb_blkno, (char *)xb,
					   name_hash, &eb_buf);
		if (ret)
			goto out;

		eb = (struct ocfs2_extent_block *) eb_buf;
		el = &eb->h_list;

		if (el->l_tree_depth) {
			ret = OCFS2_ET_INVALID_ARGUMENT;
			goto out;
		}
	}

	for (i = el->l_next_free_rec - 1; i >= 0; i--) {
		rec = &el->l_recs[i];

		if (rec->e_cpos <= name_hash) {
			e_blkno = rec->e_blkno;
			break;
		}
	}

	if (!e_blkno) {
		ret = OCFS2_ET_INVALID_ARGUMENT;
		goto out;
	}

	*p_blkno = rec->e_blkno;
	*num_clusters = rec->e_leaf_clusters;
	if (e_cpos)
		*e_cpos = rec->e_cpos;
out:
	if (eb_buf)
		ocfs2_free(&eb_buf);
	return ret;
}

uint16_t ocfs2_xattr_value_real_size(uint16_t name_len,
				     uint16_t value_len)
{
	uint16_t size = 0;

	if (value_len <= OCFS2_XATTR_INLINE_SIZE)
		size = OCFS2_XATTR_SIZE(name_len) + OCFS2_XATTR_SIZE(value_len);
	else
		size = OCFS2_XATTR_SIZE(name_len) + OCFS2_XATTR_ROOT_SIZE;

	return size;
}

uint16_t ocfs2_xattr_min_offset(struct ocfs2_xattr_header *xh, uint16_t size)
{
	int i;
	uint16_t min_offs = size;

	for (i = 0 ; i < xh->xh_count; i++) {
		struct ocfs2_xattr_entry *xe = &xh->xh_entries[i];
		size_t offs = xe->xe_name_offset;

		if (offs < min_offs)
			min_offs = offs;
	}
	return min_offs;
}

uint16_t ocfs2_xattr_name_value_len(struct ocfs2_xattr_header *xh)
{
	int i;
	uint16_t total_len = 0;

	for (i = 0 ; i < xh->xh_count; i++) {
		struct ocfs2_xattr_entry *xe = &xh->xh_entries[i];

		total_len += ocfs2_xattr_value_real_size(xe->xe_name_len,
							 xe->xe_value_size);
	}
	return total_len;
}

errcode_t ocfs2_read_xattr_bucket(ocfs2_filesys *fs,
				  uint64_t blkno,
				  char *bucket_buf)
{
	errcode_t ret = 0;
	char *bucket;
	struct ocfs2_xattr_header *xh;
	int blk_per_bucket = ocfs2_blocks_per_xattr_bucket(fs);

	ret = ocfs2_malloc_blocks(fs->fs_io, blk_per_bucket, &bucket);
	if (ret)
		return ret;

	ret = ocfs2_read_blocks(fs, blkno, blk_per_bucket, bucket);
	if (ret)
		goto out;

	xh = (struct ocfs2_xattr_header *)bucket;
	if (ocfs2_meta_ecc(OCFS2_RAW_SB(fs->fs_super)) &&
	    !(fs->fs_flags & OCFS2_FLAG_NO_ECC_CHECKS)) {
		ret = ocfs2_block_check_validate(bucket,
						 OCFS2_XATTR_BUCKET_SIZE,
						 &xh->xh_check);
		if (ret)
			goto out;
	}

	memcpy(bucket_buf, bucket, OCFS2_XATTR_BUCKET_SIZE);
	xh = (struct ocfs2_xattr_header *)bucket_buf;
	__ocfs2_swap_xattrs_to_cpu(fs, xh, OCFS2_XATTR_BUCKET_SIZE, xh);
out:
	ocfs2_free(&bucket);
	return ret;
}

errcode_t ocfs2_write_xattr_bucket(ocfs2_filesys *fs,
				   uint64_t blkno,
				   char *bucket_buf)
{

	errcode_t ret = 0;
	char *bucket;
	struct ocfs2_xattr_header *xh;
	int blk_per_bucket = ocfs2_blocks_per_xattr_bucket(fs);

	if (!(fs->fs_flags & OCFS2_FLAG_RW))
		return OCFS2_ET_RO_FILESYS;

	if ((blkno < OCFS2_SUPER_BLOCK_BLKNO) ||
	    (blkno > fs->fs_blocks))
		return OCFS2_ET_BAD_BLKNO;

	ret = ocfs2_malloc_blocks(fs->fs_io, blk_per_bucket, &bucket);
	if (ret)
		return ret;

	memcpy(bucket, bucket_buf, OCFS2_XATTR_BUCKET_SIZE);

	xh = (struct ocfs2_xattr_header *)bucket;
	__ocfs2_swap_xattrs_from_cpu(fs, xh, OCFS2_XATTR_BUCKET_SIZE, xh);

	if (ocfs2_meta_ecc(OCFS2_RAW_SB(fs->fs_super)))
		ocfs2_block_check_compute(bucket, OCFS2_XATTR_BUCKET_SIZE,
					  &xh->xh_check);

	ret = io_write_block(fs->fs_io, blkno, blk_per_bucket, bucket);
	if (!ret)
		fs->fs_flags |= OCFS2_FLAG_CHANGED;

	ocfs2_free(&bucket);
	return ret;
}

struct xattr_iterate_ctxt {
	ocfs2_cached_inode *ci;
	int (*func)(ocfs2_cached_inode *ci,
		    char *xe_buf,
		    uint64_t xe_blkno,
		    struct ocfs2_xattr_entry *xe,
		    char *value_buf,
		    uint64_t value_blkno,
		    void *value,
		    int  in_bucket,
		    void *priv_data);
	errcode_t errcode;
	void *priv_data;
};

static int ocfs2_xattr_iterate_entries(struct xattr_iterate_ctxt *ctxt,
				       char *xattr_buf, uint64_t xe_blkno,
				       struct ocfs2_xattr_header *xh,
				       int is_bucket)
{
	int i, value_offset, block_offset;
	struct ocfs2_xattr_entry *xe = NULL;
	int iret = 0;
	char *value_buf;
	void *value;

	for (i = 0 ; i < xh->xh_count; i++) {
		xe = &xh->xh_entries[i];
		value_offset = xe->xe_name_offset +
				OCFS2_XATTR_SIZE(xe->xe_name_len);
		block_offset = value_offset / ctxt->ci->ci_fs->fs_blocksize;
		value_buf = xattr_buf +
				block_offset * ctxt->ci->ci_fs->fs_blocksize;
		value = (char *)xh + value_offset;

		if (ctxt->func) {
			iret = ctxt->func(ctxt->ci, xattr_buf, xe_blkno, xe,
					  value_buf, xe_blkno + block_offset,
					  value, is_bucket,
					  ctxt->priv_data);
			if (iret & (OCFS2_XATTR_ABORT | OCFS2_XATTR_ERROR))
				break;
		}
	}

	return iret;
}

static int ocfs2_xattr_iterate_ibody(struct xattr_iterate_ctxt *ctxt)
{
	struct ocfs2_xattr_header *xh = NULL;
	struct ocfs2_dinode *di = ctxt->ci->ci_inode;

	if (!(di->i_dyn_features & OCFS2_INLINE_XATTR_FL))
		return 0;

	xh = (struct ocfs2_xattr_header *)((char *)di +
		ctxt->ci->ci_fs->fs_blocksize - di->i_xattr_inline_size);

	return ocfs2_xattr_iterate_entries(ctxt, (char *)di, di->i_blkno,
					   xh, 0);
}

static int ocfs2_xattr_iterate_bucket(struct xattr_iterate_ctxt *ctxt,
				      uint64_t blkno, uint32_t clusters)
{
	int i, iret = 0 ;
	char *bucket = NULL;
	struct ocfs2_xattr_header *xh;
	ocfs2_filesys *fs = ctxt->ci->ci_fs;
	int blk_per_bucket = ocfs2_blocks_per_xattr_bucket(fs);
	uint32_t bpc = ocfs2_xattr_buckets_per_cluster(fs);
	uint32_t num_buckets = clusters * bpc;

	ctxt->errcode = ocfs2_malloc_blocks(fs->fs_io, blk_per_bucket, &bucket);
	if (ctxt->errcode)
		goto out;

	for (i = 0; i < num_buckets; i++, blkno += blk_per_bucket) {
		ctxt->errcode = ocfs2_read_xattr_bucket(fs, blkno, bucket);
		if (ctxt->errcode)
			goto out;

		xh = (struct ocfs2_xattr_header *)bucket;
		/*
		 * The real bucket num in this series of blocks is stored
		 * in the 1st bucket.
		 */
		if (i == 0)
			num_buckets = xh->xh_num_buckets;
		iret = ocfs2_xattr_iterate_entries(ctxt, bucket, blkno, xh, 1);
	}

out:
	if (bucket)
		ocfs2_free(&bucket);

	if (ctxt->errcode)
		iret |= OCFS2_XATTR_ERROR;

	return iret;
}

static int ocfs2_xattr_iterate_index_block(struct xattr_iterate_ctxt *ctxt,
					   struct ocfs2_xattr_block *xb)
{
	ocfs2_filesys *fs = ctxt->ci->ci_fs;
	struct ocfs2_extent_list *el = &xb->xb_attrs.xb_root.xt_list;
	uint32_t name_hash = UINT_MAX, e_cpos = 0, num_clusters = 0;
	uint64_t p_blkno = 0;
	int iret = 0;

	if (!el->l_next_free_rec)
		return 0;

	while (name_hash > 0) {
		ctxt->errcode = ocfs2_xattr_get_rec(fs, xb,
						    name_hash, &p_blkno,
						    &e_cpos, &num_clusters);
		if (ctxt->errcode)
			break;

		iret = ocfs2_xattr_iterate_bucket(ctxt, p_blkno, num_clusters);
		if (iret & (OCFS2_XATTR_ERROR | OCFS2_XATTR_ABORT))
			break;

		if (e_cpos == 0)
			break;

		name_hash = e_cpos - 1;
	}

	if (ctxt->errcode)
		iret |= OCFS2_XATTR_ERROR;
	return iret;
}

static int ocfs2_xattr_iterate_block(struct xattr_iterate_ctxt *ctxt)
{
	char *blk = NULL;
	ocfs2_filesys *fs = ctxt->ci->ci_fs;
	struct ocfs2_dinode *di = ctxt->ci->ci_inode;
	struct ocfs2_xattr_block *xb;
	int iret = 0;

	if (!di->i_xattr_loc)
		return 0;

	ctxt->errcode = ocfs2_malloc_block(fs->fs_io, &blk);
	if (ctxt->errcode)
		goto out;

	ctxt->errcode = ocfs2_read_xattr_block(fs, di->i_xattr_loc, blk);
	if (ctxt->errcode)
		goto out;

	xb = (struct ocfs2_xattr_block *)blk;
	if (xb->xb_flags & OCFS2_XATTR_INDEXED)
		iret = ocfs2_xattr_iterate_index_block(ctxt, xb);
	else {
		struct ocfs2_xattr_header *header = &xb->xb_attrs.xb_header;
		iret = ocfs2_xattr_iterate_entries(ctxt, blk,
						   di->i_xattr_loc, header, 0);
	}

out:
	if (blk)
		ocfs2_free(&blk);

	if (ctxt->errcode)
		iret |= OCFS2_XATTR_ERROR;

	return iret;
}


/*
 * Iterate the xattr entries on inode 'ci'.  If 'func' returns
 * OCFS2_XATTR_ABORT or OCFS2_XATTR_ERROR, stop iteration.
 * If OCFS2_XATTR_ERROR, return an error from ocfs2_xattr_iterate.
 *
 * If you modify an xattr, you must restart your iteration - there is
 * no guarantee it is in a consistent state.
 */
errcode_t ocfs2_xattr_iterate(ocfs2_cached_inode *ci,
			      int (*func)(ocfs2_cached_inode *ci,
					  char *xe_buf,
					  uint64_t xe_blkno,
					  struct ocfs2_xattr_entry *xe,
					  char *value_buf,
					  uint64_t value_blkno,
					  void *value,
					  int in_bucket,
					  void *priv_data),
			      void *priv_data)
{
	errcode_t ret = 0;
	int iret = 0;
	struct xattr_iterate_ctxt ctxt;

	if (!ocfs2_support_xattr(OCFS2_RAW_SB(ci->ci_fs->fs_super)) ||
	    (!(ci->ci_inode->i_dyn_features & OCFS2_HAS_XATTR_FL)))
		return 0;

	ctxt.ci = ci;
	ctxt.func = func;
	ctxt.priv_data = priv_data;
	ctxt.errcode = 0;

	iret = ocfs2_xattr_iterate_ibody(&ctxt);
	if (!(iret & (OCFS2_XATTR_ABORT | OCFS2_XATTR_ERROR)))
		iret = ocfs2_xattr_iterate_block(&ctxt);

	if (iret & OCFS2_XATTR_ERROR)
		ret = ctxt.errcode;

	return ret;
}
