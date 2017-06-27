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

#include "xattr.h"
#include "extent.h"
#include "fsck.h"
#include "problem.h"
#include "util.h"

static const char *whoami = "xattr.c";

#define IS_LAST_ENTRY(entry) (*(uint32_t *)(entry) == 0)
#define HEADER_SIZE	(sizeof(struct ocfs2_xattr_header))
#define ENTRY_SIZE	(sizeof(struct ocfs2_xattr_entry))
#define MIN_VALUE	4
#define XE_OFFSET(xh, xe)	((char *)(xe) - (char *)(xh))

enum xattr_location {
	IN_INODE = 0,
	IN_BLOCK,
	IN_BUCKET
};

/* These must be kept in sync with xattr_location */
static const char *xattr_object[] = {
	[IN_INODE] = "inode",
	[IN_BLOCK] = "block",
	[IN_BUCKET] = "bucket",
};

struct xattr_info {
	enum xattr_location location;
	uint32_t max_offset;
	uint64_t blkno;
};

/*
 * This use to describe the used area of xattr in inode, block and bucket.
 * The used area include all xattr structs, such as header, entry, name+value.
 */
struct used_area {
	struct list_head ua_list;	/* list of used area */
	uint16_t ua_offset;		/* the offset of the area */
	uint16_t ua_length;		/* the length of the area */
	struct ocfs2_xattr_entry ua_xe;	/* store the valid xattr entry */
	uint16_t ua_xe_valid;		/* whether it is a valid xattr entry */
};

struct used_map {
	uint16_t um_size;		/* the size of the map */
	struct list_head um_areas;	/* list of used area */
};

static int check_xattr_count(o2fsck_state *ost,
			     struct ocfs2_dinode *di,
			     struct ocfs2_xattr_header *xh,
			     int *changed,
			     struct xattr_info *xi)
{
	struct ocfs2_xattr_entry *entry = xh->xh_entries;
	struct ocfs2_xattr_entry *pre_xe = entry;
	uint16_t det_count = 0;
	uint16_t max_count = (xi->max_offset - HEADER_SIZE) /
			     (ENTRY_SIZE + MIN_VALUE);

	while (!IS_LAST_ENTRY(entry)) {
		/*
		 * xattr entries in bucket had sorted by name_hash,
		 * so this can help us to detect count.
		 */
		if (xi->location == IN_BUCKET &&
		    entry->xe_name_hash < pre_xe->xe_name_hash)
				break;
		if (det_count >= max_count)
			break;
		det_count++;
		pre_xe = entry;
		entry++;
	}

	if (xh->xh_count > det_count) {
		if (prompt(ost, PY, PR_XATTR_COUNT_INVALID,
			   "Extended attributes in %s #%"PRIu64" claims to"
			   " have %u entries, but fsck believes it is %u,"
			   " Fix the entries count?",
			   xattr_object[xi->location], xi->blkno,
			   xh->xh_count, det_count)) {
			xh->xh_count = det_count;
			if (!det_count && xi->location == IN_BUCKET) {
				xh->xh_free_start = OCFS2_XATTR_BUCKET_SIZE;
				xh->xh_name_value_len = 0;
			}
			*changed = 1;
		} else
			return -1;
	}

	return 0;
}

static struct used_area *new_used_area(uint16_t off, uint16_t len,
				       struct ocfs2_xattr_entry *xe)
{
	struct used_area *ua = NULL;

	ua = malloc(sizeof(struct used_area));
	if (!ua)
		return NULL;

	memset(ua, 0 , sizeof(struct used_area));
	ua->ua_offset = off;
	ua->ua_length = len;

	if (xe) {
		memcpy(&ua->ua_xe, xe, ENTRY_SIZE);
		ua->ua_xe_valid = 1;
	}

	return ua;
}

static errcode_t set_used_area(struct used_map *um,
			       uint16_t off, uint16_t len,
			       struct ocfs2_xattr_entry *xe)
{
	struct used_area *new_area = NULL;

	if (!um)
		return OCFS2_ET_INVALID_ARGUMENT;

	new_area = new_used_area(off, len, xe);
	if (!new_area) {
		com_err(whoami, OCFS2_ET_NO_MEMORY, "Unable to allocate"
			" buffer for extended attribute ");
		return OCFS2_ET_NO_MEMORY;
	}

	INIT_LIST_HEAD(&new_area->ua_list);
	list_add_tail(&new_area->ua_list, &um->um_areas);

	return 0;
}

static void clear_used_area(struct used_map *um, uint16_t off, uint16_t len)
{
	struct used_area *area = NULL;
	struct list_head *ua, *ua2;

	if (list_empty(&um->um_areas))
		return;

	list_for_each_safe(ua, ua2, &um->um_areas) {
		area = list_entry(ua, struct used_area, ua_list);
		if (off == area->ua_offset && len == area->ua_length) {
			list_del(ua);
			free(area);
			return;
		}
	}
	return;
}

static int check_area_fits(struct used_map *um, uint16_t off, uint16_t len)
{
	struct used_area *area = NULL;
	struct list_head *ua, *ua2;

	if (!um || (off + len) > um->um_size)
		return -1;

	if (list_empty(&um->um_areas))
		return 0;

	list_for_each_safe(ua, ua2, &um->um_areas) {
		area = list_entry(ua, struct used_area, ua_list);
		if ((off + len) <= area->ua_offset)
			continue;
		if ((area->ua_offset + area->ua_length) <= off)
			continue;
		return -1;
	}

	return 0;
}

static void free_used_map(struct used_map *um)
{
	struct used_area *area = NULL;
	struct list_head *ua, *ua2;

	if (list_empty(&um->um_areas))
		return;

	list_for_each_safe(ua, ua2, &um->um_areas) {
		area = list_entry(ua, struct used_area, ua_list);
		list_del(ua);
		free(area);
	}
	return;
}

static errcode_t check_xattr_entry(o2fsck_state *ost,
				   struct ocfs2_dinode *di,
				   struct ocfs2_xattr_header *xh,
				   int *changed,
				   struct xattr_info *xi)
{
	int i, ret = 0;
	uint16_t count;
	struct used_map *umap;

	count = xh->xh_count;
	umap = malloc(sizeof(struct used_map));
	if (!umap) {
		com_err(whoami, OCFS2_ET_NO_MEMORY, "Unable to allocate"
			" buffer for extended attribute ");
		return OCFS2_ET_NO_MEMORY;
	}
	umap->um_size = xi->max_offset;
	INIT_LIST_HEAD(&umap->um_areas);

	/* set xattr header as used area */
	set_used_area(umap, 0, sizeof(struct ocfs2_xattr_header), NULL);

	for (i = 0 ; i < xh->xh_count; i++) {
		struct ocfs2_xattr_entry *xe = &xh->xh_entries[i];
		uint16_t value_len;
		uint32_t hash;

		if (check_area_fits(umap, XE_OFFSET(xh, xe), ENTRY_SIZE)) {
			if (!prompt(ost, PY, PR_XATTR_ENTRY_INVALID,
				    "Extended attribute entry in %s #%"
				    PRIu64" refers to a used area at %u,"
				    " clear this entry?",
				    xattr_object[xi->location], xi->blkno,
				    (uint32_t)XE_OFFSET(xh, xe))) {
				ret = -1;
				break;
			} else
				goto wipe_entry;
		}

		/* check and fix name_offset */
		if (xe->xe_name_offset >= xi->max_offset) {
			if (!prompt(ost, PY, PR_XATTR_NAME_OFFSET_INVALID,
				    "Extended attribute entry in %s #%"PRIu64
				    " refers to an invalid name offset %u,"
				    " clear this entry?",
				    xattr_object[xi->location], xi->blkno,
				    xe->xe_name_offset)) {
				ret = -1;
				break;
			} else
				goto wipe_entry;
		}

		/* check type and value size */
		if ((ocfs2_xattr_is_local(xe) &&
		     xe->xe_value_size > OCFS2_XATTR_INLINE_SIZE) ||
		    (!ocfs2_xattr_is_local(xe) &&
		     xe->xe_value_size <= OCFS2_XATTR_INLINE_SIZE)) {
			char *local;

			if (ocfs2_xattr_is_local(xe))
				local = "";
			else
				local = "not ";
			if (!prompt(ost, PY, PR_XATTR_LOCATION_INVALID,
				    "Extended attribute entry in %s #%"PRIu64
				    " claims to have value %sin local, but the"
				    " value size is %"PRIu64
				    ", clear this entry?",
				    xattr_object[xi->location], xi->blkno,
				    local, (uint64_t)xe->xe_value_size)) {
				ret = -1;
				break;
			} else
				goto wipe_entry;
		}

		/* mark the entry area as used*/
		set_used_area(umap, XE_OFFSET(xh, xe), ENTRY_SIZE, xe);
		/* get the value's real size in inode, block or bucket */
		value_len = ocfs2_xattr_value_real_size(xe->xe_name_len,
							xe->xe_value_size);
		if (check_area_fits(umap, xe->xe_name_offset, value_len)) {
			if (!prompt(ost, PY, PR_XATTR_VALUE_INVALID,
				    "Extended attribute entry in %s #%"PRIu64
				    " refers to a used area at %u,"
				    " clear this entry?",
				    xattr_object[xi->location], xi->blkno,
				    xe->xe_name_offset)) {
				ret = -1;
				break;
			} else {
				clear_used_area(umap, XE_OFFSET(xh, xe),
						ENTRY_SIZE);
				goto wipe_entry;
			}
		}

		/* mark the value area as used */
		set_used_area(umap, xe->xe_name_offset, value_len, NULL);

		/* check and fix name hash */
		hash = ocfs2_xattr_name_hash(
			ost->ost_fs->fs_super->id2.i_super.s_uuid_hash,
			(void *)xh + xe->xe_name_offset, xe->xe_name_len);
		if (xe->xe_name_hash != hash &&
		    prompt(ost, PY, PR_XATTR_HASH_INVALID,
			   "Extended attribute entry in %s #%"PRIu64
			   " refers to an invalid name hash %u,"
			   " Fix the name hash?",
			   xattr_object[xi->location], xi->blkno,
			   xe->xe_name_hash)) {
			xe->xe_name_hash = hash;
			*changed = 1;
		}

		continue;
wipe_entry:
		/*
		 * we don't wipe entry at here, just reduce the count,
		 * we will wipe them when we finish the check.
		 */
		count -= 1;
		*changed = 1;
	}

	if (*changed && xh->xh_count != count) {
		struct used_area *area = NULL;
		struct list_head *ua, *ua2;
		/*
		 * according to used map, remove bad entries from entry area,
		 * and left the name+value in the object.
		 */
		i = 0;
		list_for_each_safe(ua, ua2, &umap->um_areas) {
			area = list_entry(ua, struct used_area, ua_list);
			if (!area->ua_xe_valid)
				continue;
			memcpy(&xh->xh_entries[i], &area->ua_xe, ENTRY_SIZE);
			i++;
		}
		xh->xh_count = i;
	}

	free_used_map(umap);
	free(umap);
	return ret;
}

static errcode_t check_xattr_value(o2fsck_state *ost,
				   struct ocfs2_dinode *di,
				   struct ocfs2_xattr_header *xh,
				   uint64_t start,
				   int *changed)
{
	int i;
	struct extent_info ei = {0, };
	errcode_t ret = 0;
	uint64_t owner;

	ei.chk_rec_func = o2fsck_check_extent_rec;
	ei.mark_rec_alloc_func = o2fsck_mark_tree_clusters_allocated;
	ei.para = di;
	for (i = 0 ; i < xh->xh_count; i++) {
		int change = 0;
		struct ocfs2_xattr_entry *xe = &xh->xh_entries[i];

		if (!ocfs2_xattr_is_local(xe)) {
			int offset = xe->xe_name_offset +
					OCFS2_XATTR_SIZE(xe->xe_name_len);
			struct ocfs2_xattr_value_root *xv =
				(struct ocfs2_xattr_value_root *)
				((void *)xh + offset);
			struct ocfs2_extent_list *el = &xv->xr_list;
			owner = start + offset / ost->ost_fs->fs_blocksize;
			ret = check_el(ost, &ei, owner, el, 1,
					0, 0, &change);
			if (ret)
				return ret;
			if (change)
				*changed = 1;
		}
	}

	return ret;
}

static errcode_t check_xattr(o2fsck_state *ost,
			     struct ocfs2_dinode *di,
			     struct ocfs2_xattr_header *xh,
			     int *changed,
			     struct xattr_info *xi)
{
	errcode_t ret;
	uint16_t min_offs, total_len;

	/* At first we check and fix the total xattr entry count */
	if (check_xattr_count(ost, di, xh, changed, xi))
		return 0;
	/* then check and fix the xattr entry */
	if (check_xattr_entry(ost, di, xh, changed, xi))
		return 0;

	ret = check_xattr_value(ost, di, xh, xi->blkno, changed);
	if (ret)
		return ret;

	if (xi->location == IN_BUCKET) {
		/* check and fix xh_free_start */
		min_offs = ocfs2_xattr_min_offset(xh, xi->max_offset);
		if (xh->xh_free_start != min_offs &&
		    prompt(ost, PY, PR_XATTR_FREE_START_INVALID,
			   "Extended attribute in %s #%"PRIu64" claims to"
			   " have free space start at %u , but fsck believes"
			   " it is %u, Fix the value of free start?",
			   xattr_object[xi->location], xi->blkno,
			   xh->xh_free_start, min_offs)) {
			xh->xh_free_start = min_offs;
			*changed = 1;
		}
		/* check and fix xh_name_value_len */
		total_len = ocfs2_xattr_name_value_len(xh);
		if (xh->xh_name_value_len != total_len &&
		    prompt(ost, PY, PR_XATTR_VALUE_LEN_INVALID,
			   "Extended attribute in %s #%"PRIu64" claims to have"
			   " the total length %u of all EAs name and value"
			   " in this object, but fsck believes it is %u,"
			   " Fix the value of the total length?",
			   xattr_object[xi->location], xi->blkno,
			   xh->xh_name_value_len, total_len)) {
			xh->xh_name_value_len = total_len;
			*changed = 1;
		}
	}

	return 0;
}

static uint16_t detect_xattr_bucket_count(char *bucket,
					  uint32_t max_buckets)
{
	int i;
	char *bucket_buf = NULL;
	struct ocfs2_xattr_header *xh;
	uint16_t max_count, max_offset;

	max_offset = OCFS2_XATTR_BUCKET_SIZE;
	max_count = (OCFS2_XATTR_BUCKET_SIZE - HEADER_SIZE) /
		    (ENTRY_SIZE + MIN_VALUE);

	bucket_buf = bucket;
	for (i = 0; i < max_buckets; i++) {
		xh = (struct ocfs2_xattr_header *)bucket_buf;
		if (xh->xh_count < max_count &&
		    xh->xh_free_start > xh->xh_count * ENTRY_SIZE &&
		    xh->xh_free_start <= max_offset &&
		    xh->xh_name_value_len <= max_offset - xh->xh_free_start) {
			bucket_buf += OCFS2_XATTR_BUCKET_SIZE;
			continue;
		} else
			return i;
	}

	return i;
}

static errcode_t ocfs2_check_xattr_buckets(o2fsck_state *ost,
					   struct ocfs2_dinode *di,
					   uint64_t blkno,
					   uint32_t clusters)
{
	int i;
	errcode_t ret = 0;
	char *bucket = NULL;
	char *bucket_buf = NULL;
	struct ocfs2_xattr_header *xh;
	int blk_per_bucket = ocfs2_blocks_per_xattr_bucket(ost->ost_fs);
	uint32_t bpc = ocfs2_xattr_buckets_per_cluster(ost->ost_fs);
	uint32_t max_buckets = clusters * bpc;
	uint32_t max_blocks = max_buckets * blk_per_bucket;
	uint32_t num_buckets = 0;
	uint64_t blk = 0;

	/* malloc space for all buckets */
	ret = ocfs2_malloc_blocks(ost->ost_fs->fs_io, max_blocks, &bucket);
	if (ret) {
		com_err(whoami, ret, "while allocating room to read"
			" extended attributes bucket");
		goto out;
	}

	/* read all buckets for detect (some of them may not be used) */
	bucket_buf = bucket;
	blk = blkno;
	for (i = 0; i < max_buckets; i++) {
		ret = ocfs2_read_xattr_bucket(ost->ost_fs, blk, bucket_buf);
		if (ret) {
			max_buckets = i;
			break;
		}
		blk += blk_per_bucket;
		bucket_buf += OCFS2_XATTR_BUCKET_SIZE;
	}

	/*
	 * The real bucket num in this series of blocks is stored
	 * in the 1st bucket.
	 */
	xh = (struct ocfs2_xattr_header *)bucket;
	if (xh->xh_num_buckets == 0 || xh->xh_num_buckets > max_buckets) {
		num_buckets = detect_xattr_bucket_count(bucket, max_buckets);
		if (prompt(ost, PY, PR_XATTR_BUCKET_COUNT_INVALID,
			   "Extended attribute buckets start at %"PRIu64
			   " claims to have %u buckets, but fsck believes"
			   " it is %u, Fix the bucket count?",
			   blkno, xh->xh_num_buckets,
			   num_buckets ? num_buckets : 1)) {
			if (num_buckets == 0) {
				/*
				 * If buckets count is 0, we need clean
				 * xh_count and set xh_num_buckets to 1.
				 */
				xh->xh_count = 0;
				xh->xh_free_start = OCFS2_XATTR_BUCKET_SIZE;
				xh->xh_num_buckets = 1;
			} else
				xh->xh_num_buckets = num_buckets;
			/* only update first bucket */
			ret = ocfs2_write_xattr_bucket(ost->ost_fs, blkno,
							bucket);
			if (ret) {
				com_err(whoami, ret, "while writing bucket of"
					" extended attributes ");
				goto out;
			}
			if (num_buckets == 0)
				goto out;
		} else
			goto out;

	} else
		num_buckets = xh->xh_num_buckets;

	bucket_buf = bucket;
	for (i = 0; i < num_buckets; i++) {
		int changed = 0;
		struct xattr_info xi = {
			.location = IN_BUCKET,
			.max_offset = OCFS2_XATTR_BUCKET_SIZE,
			.blkno = blkno,
		};

		xh = (struct ocfs2_xattr_header *)bucket_buf;
		ret = check_xattr(ost, di, xh, &changed, &xi);
		if (ret)
			break;
		if (changed) {
			ret = ocfs2_write_xattr_bucket(ost->ost_fs, blkno,
							bucket_buf);
			if (ret) {
				com_err(whoami, ret, "while writing bucket of"
					" extended attributes ");
				goto out;
			}
		}
		blkno += blk_per_bucket;
		bucket_buf += OCFS2_XATTR_BUCKET_SIZE;
	}
out:
	if (bucket)
		ocfs2_free(&bucket);
	return ret;
}

static errcode_t o2fsck_check_xattr_index_block(o2fsck_state *ost,
						struct ocfs2_dinode *di,
						struct ocfs2_xattr_block *xb,
						int *changed)
{
	struct ocfs2_extent_list *el = &xb->xb_attrs.xb_root.xt_list;
	errcode_t ret = 0;
	uint32_t name_hash = UINT_MAX, e_cpos = 0, num_clusters = 0;
	uint64_t p_blkno = 0;
	struct extent_info ei = {0, };

	if (!el->l_next_free_rec)
		return 0;

	ei.chk_rec_func = o2fsck_check_extent_rec;
	ei.mark_rec_alloc_func = o2fsck_mark_tree_clusters_allocated;
	ei.para = di;
	ret = check_el(ost, &ei, xb->xb_blkno, el,
		ocfs2_xattr_recs_per_xb(ost->ost_fs->fs_blocksize), 0, 0,
		changed);
	if (ret)
		return ret;

	/*
	 * We need to write the changed xattr tree first so that the following
	 * ocfs2_xattr_get_rec can get the updated information.
	 */
	if (*changed) {
		ret = ocfs2_write_xattr_block(ost->ost_fs,
					      di->i_xattr_loc, (char *)xb);
		if (ret) {
			com_err(whoami, ret, "while writing root block of"
				" extended attributes ");
			return ret;
		}
	}


	while (name_hash > 0) {
		ret = ocfs2_xattr_get_rec(ost->ost_fs, xb, name_hash, &p_blkno,
					  &e_cpos, &num_clusters);
		if (ret) {
			com_err(whoami, ret, "while getting bucket record"
				" of extended attributes ");
			goto out;
		}

		ret = ocfs2_check_xattr_buckets(ost, di, p_blkno,
						num_clusters);
		if (ret) {
			com_err(whoami, ret, "while iterating bucket"
				" of extended attributes ");
			goto out;
		}

		if (e_cpos == 0)
			break;

		name_hash = e_cpos - 1;
	}

out:
	return ret;
}

static errcode_t o2fsck_check_xattr_block(o2fsck_state *ost,
					  struct ocfs2_dinode *di,
					  int *i_changed)
{
	errcode_t ret;
	char *blk = NULL;
	struct ocfs2_xattr_block *xb = NULL;
	int b_changed = 0;

	ret = ocfs2_malloc_block(ost->ost_fs->fs_io, &blk);
	if (ret) {
		com_err(whoami, ret, "while allocating room to read block"
			"of extended attribute ");
		return ret;
	}

	ret = ocfs2_read_xattr_block(ost->ost_fs, di->i_xattr_loc, blk);
	if (ret) {
		com_err(whoami, ret, "while reading externel block of"
			" extended attributes ");
		goto out;
	}

	xb = (struct ocfs2_xattr_block *)blk;

	if (strcmp((char *)xb->xb_signature, OCFS2_XATTR_BLOCK_SIGNATURE)) {
		if (prompt(ost, PY, PR_XATTR_BLOCK_INVALID,
		    "Extended attributes block %"PRIu64" has bad signature"
		    " %.*s, remove this block?",
		    (uint64_t)di->i_xattr_loc, 7, xb->xb_signature)) {
			di->i_xattr_loc = 0;
			*i_changed = 1;
		}
		goto out;
	}

	if (!(xb->xb_flags & OCFS2_XATTR_INDEXED)) {
		struct ocfs2_xattr_header *xh = &xb->xb_attrs.xb_header;
		struct xattr_info xi = {
			.location = IN_BLOCK,
			.max_offset = ost->ost_fs->fs_blocksize -
				      offsetof(struct ocfs2_xattr_block,
						xb_attrs.xb_header),
			.blkno = di->i_xattr_loc,
		};

		ret = check_xattr(ost, di, xh, &b_changed, &xi);
	} else
		ret = o2fsck_check_xattr_index_block(ost, di, xb, &b_changed);

	if (!ret && b_changed) {
		ret = ocfs2_write_xattr_block(ost->ost_fs,
					      di->i_xattr_loc, blk);
		if (ret)
			com_err(whoami, ret, "while writing externel block of"
				" extended attributes ");
	}
out:
	if (blk)
		ocfs2_free(&blk);
	return ret;
}

static errcode_t o2fsck_check_xattr_ibody(o2fsck_state *ost,
					  struct ocfs2_dinode *di,
					  int *i_changed)
{
	struct ocfs2_xattr_header *xh = NULL;
	struct xattr_info xi = {
		.location = IN_INODE,
		.max_offset = di->i_xattr_inline_size,
		.blkno = di->i_blkno,
	};

	xh = (struct ocfs2_xattr_header *)
		 ((void *)di + ost->ost_fs->fs_blocksize -
		  di->i_xattr_inline_size);

	return check_xattr(ost, di, xh, i_changed, &xi);
}

/*
 * o2fsck_check_xattr
 *
 * Check extended attribute in inode block or external block.
 */
errcode_t o2fsck_check_xattr(o2fsck_state *ost,
			     struct ocfs2_dinode *di)
{
	errcode_t ret = 0;
	int i_changed = 0;

	if (!(di->i_dyn_features & OCFS2_HAS_XATTR_FL))
		return 0;

	if (di->i_dyn_features & OCFS2_INLINE_XATTR_FL) {
		ret = o2fsck_check_xattr_ibody(ost, di, &i_changed);
		if (ret)
			return ret;
		if (i_changed) {
			o2fsck_write_inode(ost, di->i_blkno, di);
			i_changed = 0;
		}
	}

	if (di->i_xattr_loc)
		ret = o2fsck_check_xattr_block(ost, di, &i_changed);
	if (!ret && i_changed)
		o2fsck_write_inode(ost, di->i_blkno, di);

	return ret;
}
