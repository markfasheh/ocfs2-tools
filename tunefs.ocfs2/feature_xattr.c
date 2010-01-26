/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * feature_xattr.c
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

#include "ocfs2-kernel/kernel-list.h"
#include "ocfs2/ocfs2.h"

#include "libocfs2ne.h"

struct xattr_inode {
	struct list_head list;
	uint64_t blkno;
};

struct xattr_context {
	errcode_t ret;
	struct list_head inodes;
	struct tools_progress *prog;
	uint64_t inode_count;
};

static void empty_xattr_context(struct xattr_context *ctxt)
{
	struct list_head *pos, *n;
	struct xattr_inode *xdi;

	list_for_each_safe(pos, n, &ctxt->inodes) {
		xdi = list_entry(pos, struct xattr_inode, list);
		list_del(&xdi->list);
		ocfs2_free(&xdi);
	}
}

static errcode_t remove_xattr_entry(ocfs2_filesys *fs, uint64_t ino,
				    struct ocfs2_xattr_header *xh)
{
	int i;
	errcode_t ret = 0;

	for (i = 0 ; i < xh->xh_count; i++) {
		struct ocfs2_xattr_entry *xe = &xh->xh_entries[i];

		if (!ocfs2_xattr_is_local(xe)) {
			struct ocfs2_xattr_value_root *xv =
				(struct ocfs2_xattr_value_root *)
				((void *)xh + xe->xe_name_offset +
				OCFS2_XATTR_SIZE(xe->xe_name_len));
			ret = ocfs2_xattr_value_truncate(fs, ino, xv);
			if (ret)
				break;
		}
	}

	return ret;
}

static errcode_t remove_xattr_buckets(ocfs2_filesys *fs,
				      uint64_t ino,
				      uint64_t blkno,
				      uint32_t clusters)
{
	int i;
	errcode_t ret = 0;
	char *bucket = NULL;
	struct ocfs2_xattr_header *xh;
	int blk_per_bucket = ocfs2_blocks_per_xattr_bucket(fs);
	uint32_t bpc = ocfs2_xattr_buckets_per_cluster(fs);
	uint32_t num_buckets = clusters * bpc;

	ret = ocfs2_malloc_blocks(fs->fs_io, blk_per_bucket, &bucket);
	if (ret) {
		tcom_err(ret, "while allocating room to read"
			 " bucket of extended attributes ");
		goto out;
	}

	for (i = 0; i < num_buckets; i++, blkno += blk_per_bucket) {
		ret = ocfs2_read_xattr_bucket(fs, blkno, bucket);
		if (ret) {
			tcom_err(ret, "while reading bucket of"
				 " extended attributes ");
			goto out;
		}

		xh = (struct ocfs2_xattr_header *)bucket;
		/*
		 * The real bucket num in this series of blocks is stored
		 * in the 1st bucket.
		 */
		if (i == 0)
			num_buckets = xh->xh_num_buckets;

		ret = remove_xattr_entry(fs, ino, xh);
		if (ret)
			break;
	}

out:
	ocfs2_free(&bucket);

	return ret;
}


static errcode_t remove_xattr_index_block(ocfs2_filesys *fs, uint64_t ino,
					  struct ocfs2_xattr_block *xb)
{
	struct ocfs2_extent_list *el = &xb->xb_attrs.xb_root.xt_list;
	errcode_t ret = 0;
	uint32_t name_hash = UINT_MAX, e_cpos = 0, num_clusters = 0;
	uint64_t p_blkno = 0;

	if (el->l_next_free_rec == 0)
		return 0;

	while (name_hash > 0) {
		ret = ocfs2_xattr_get_rec(fs, xb, name_hash, &p_blkno,
					  &e_cpos, &num_clusters);
		if (ret) {
			tcom_err(ret, "while getting bucket record"
				 " of extended attributes ");
			goto out;
		}

		ret = remove_xattr_buckets(fs, ino, p_blkno, num_clusters);
		if (ret) {
			tcom_err(ret, "while iterating bucket"
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

static errcode_t remove_xattr_block(ocfs2_filesys *fs,
				    struct ocfs2_dinode *di)
{
	errcode_t ret;
	char *blk = NULL;
	struct ocfs2_xattr_block *xb = NULL;

	ret = ocfs2_malloc_block(fs->fs_io, &blk);
	if (ret) {
		tcom_err(ret, "while allocating room to read block"
			 " of extended attributes ");
		return ret;
	}

	ret = ocfs2_read_xattr_block(fs, di->i_xattr_loc, blk);
	if (ret) {
		tcom_err(ret, "while reading external block of"
			 " extended attributes ");
		goto out;
	}

	xb = (struct ocfs2_xattr_block *)blk;

	if (!(xb->xb_flags & OCFS2_XATTR_INDEXED)) {
		struct ocfs2_xattr_header *xh = &xb->xb_attrs.xb_header;

		ret = remove_xattr_entry(fs, di->i_blkno, xh);
		if (ret) {
			tcom_err(ret, "while trying to remove extended"
				 " attributes in external block ");
			goto out;
		}
	} else {
		ret = remove_xattr_index_block(fs, di->i_blkno, xb);
		if (ret) {
			tcom_err(ret, "while trying to remove extended"
				 " attributes in index block ");
			goto out;
		}
		ret = ocfs2_xattr_tree_truncate(fs, &xb->xb_attrs.xb_root);
		if (ret) {
			tcom_err(ret, "while trying to remove extended"
				 " attributes tree in index block ");
			goto out;
		}
	}

	/* release block*/
	ret = ocfs2_delete_xattr_block(fs, di->i_xattr_loc);
	if (ret)
		goto out;
	/* clean extended attributes */
	memset(blk, 0, fs->fs_blocksize);

out:
	if (blk)
		ocfs2_free(&blk);

	return ret;
}

static errcode_t remove_xattr_ibody(ocfs2_filesys *fs,
				    struct ocfs2_dinode *di)
{
	errcode_t ret;
	struct ocfs2_xattr_header *xh = NULL;

	xh = (struct ocfs2_xattr_header *)
		 ((void *)di + fs->fs_blocksize -
		  di->i_xattr_inline_size);

	ret = remove_xattr_entry(fs, di->i_blkno, xh);
	if (ret) {
		tcom_err(ret, "while trying to remove extended"
			 " attributes in ibody ");
		return ret;
	}

	/* clean inline extended attributs */
	memset((char *)xh, 0, di->i_xattr_inline_size);

	if (di->i_dyn_features & OCFS2_INLINE_DATA_FL) {
		struct ocfs2_inline_data *idata = &di->id2.i_data;
		idata->id_count += di->i_xattr_inline_size;
	} else if (!(S_ISLNK(di->i_mode) && di->i_clusters == 0)) {
		struct ocfs2_extent_list *el = &di->id2.i_list;
		el->l_count += (di->i_xattr_inline_size /
				sizeof(struct ocfs2_extent_rec));
	}
	di->i_xattr_inline_size = 0;

	return ret;
}

static errcode_t remove_xattr(ocfs2_filesys *fs,
			      struct xattr_context *ctxt)
{
	errcode_t ret = 0;
	struct list_head *pos;
	struct xattr_inode *xdi;
	struct ocfs2_dinode *di = NULL;
	ocfs2_cached_inode *ci = NULL;
	struct tools_progress *prog = NULL;

	prog = tools_progress_start("Removing extended attributes", "removing",
				    ctxt->inode_count);
	if (!prog)
		return TUNEFS_ET_NO_MEMORY;

	list_for_each(pos, &ctxt->inodes) {
		xdi = list_entry(pos, struct xattr_inode, list);

		ret = ocfs2_read_cached_inode(fs, xdi->blkno, &ci);
		if (ret)
			break;
		di = ci->ci_inode;
		if (di->i_dyn_features & OCFS2_INLINE_XATTR_FL) {
			ret = remove_xattr_ibody(fs, di);
			if (ret)
				break;
		}
		if (di->i_xattr_loc)
			ret = remove_xattr_block(fs, di);
		di->i_xattr_loc = 0;
		di->i_dyn_features &= ~(OCFS2_INLINE_XATTR_FL |
					OCFS2_HAS_XATTR_FL);
		ret = ocfs2_write_cached_inode(fs, ci);
		ocfs2_free_cached_inode(fs, ci);
		if (ret)
			break;

		tools_progress_step(prog, 1);
	}

	tools_progress_stop(prog);
	return ret;
}

static errcode_t xattr_iterate(ocfs2_filesys *fs,
			       struct ocfs2_dinode *di,
			       void *user_data)
{
	errcode_t ret = 0;
	struct xattr_inode *xdi = NULL;
	struct xattr_context *ctxt = (struct xattr_context *)user_data;

	if (!S_ISREG(di->i_mode) && !S_ISDIR(di->i_mode) &&
	    !S_ISLNK(di->i_mode))
		goto bail;

	if (!(di->i_dyn_features & OCFS2_HAS_XATTR_FL))
		goto bail;

	ret = ocfs2_malloc0(sizeof(struct xattr_inode), &xdi);
	if (ret)
		goto bail;

	xdi->blkno = di->i_blkno;
	list_add_tail(&xdi->list, &ctxt->inodes);

	ctxt->inode_count++;
	tools_progress_step(ctxt->prog, 1);

	return 0;
bail:
	return ret;
}

static int enable_xattr(ocfs2_filesys *fs, int flag)
{
	errcode_t ret = 0;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);
	struct tools_progress *prog = NULL;

	if (ocfs2_support_xattr(super)) {
		verbosef(VL_APP,
			 "The extended attribute feature is already enabled; "
			 "nothing to enable\n");
		goto out;
	}

	if (!tools_interact("Enable the extended attribute feature on device "
			     "\"%s\"? ",
			     fs->fs_devname))
		goto out;

	prog = tools_progress_start("Enabling extended attribute", "xattr", 1);
	if (!prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		tcom_err(ret, "while initializing the progress display");
		goto out;
	}

	super->s_uuid_hash =
			ocfs2_xattr_uuid_hash((unsigned char *)super->s_uuid);
	super->s_xattr_inline_size = OCFS2_MIN_XATTR_INLINE_SIZE;
	OCFS2_SET_INCOMPAT_FEATURE(super, OCFS2_FEATURE_INCOMPAT_XATTR);

	tunefs_block_signals();
	ret = ocfs2_write_super(fs);
	tunefs_unblock_signals();
	if (ret)
		tcom_err(ret, "while writing out the superblock");

	tools_progress_step(prog, 1);
out:
	if (prog)
		tools_progress_stop(prog);

	return ret;
}

static int disable_xattr(ocfs2_filesys *fs, int flag)
{
	errcode_t ret = 0;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);
	struct xattr_context ctxt = {
		.prog = NULL,
		.inode_count = 0,
	};
	struct tools_progress *prog = NULL;

	if (!ocfs2_support_xattr(super)) {
		verbosef(VL_APP,
			 "The extended attribute feature is not enabled; "
			 "nothing to disable\n");
		goto out;
	}

	if (!tools_interact("Disable the extended attribute feature on device"
			     " \"%s\"? ",
			     fs->fs_devname))
		goto out;

	prog = tools_progress_start("Disabling extended attribute",
				    "noxattr", 3);
	if (!prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		tcom_err(ret, "while initializing the progress display");
		goto out;
	}

	ctxt.prog = tools_progress_start("Scanning filesystem", "scanning",
					 0);
	if (!ctxt.prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		goto out;
	}
	INIT_LIST_HEAD(&ctxt.inodes);
	ret = tunefs_foreach_inode(fs, xattr_iterate, &ctxt);
	tools_progress_stop(ctxt.prog);
	if (ret) {
		tcom_err(ret, "while trying to find files with"
			 " extended attributes ");
		goto out_cleanup;
	}
	tools_progress_step(prog, 1);

	ret = remove_xattr(fs, &ctxt);
	if (ret) {
		tcom_err(ret, "while trying to remove extended attributes");
		goto out_cleanup;
	}
	tools_progress_step(prog, 1);

	super->s_uuid_hash = 0;
	super->s_xattr_inline_size = 0;
	OCFS2_CLEAR_INCOMPAT_FEATURE(super, OCFS2_FEATURE_INCOMPAT_XATTR);

	tunefs_block_signals();
	ret = ocfs2_write_super(fs);
	tunefs_unblock_signals();
	if (ret)
		tcom_err(ret, "while writing out the superblock");

	tools_progress_step(prog, 1);

out_cleanup:
	empty_xattr_context(&ctxt);
out:
	if (prog)
		tools_progress_stop(prog);

	return ret;
}

DEFINE_TUNEFS_FEATURE_INCOMPAT(xattr,
			       OCFS2_FEATURE_INCOMPAT_XATTR,
			       TUNEFS_FLAG_RW | TUNEFS_FLAG_ALLOCATION |
			       TUNEFS_FLAG_LARGECACHE,
			       enable_xattr,
			       disable_xattr);

#ifdef DEBUG_EXE
int main(int argc, char *argv[])
{
	return tunefs_feature_main(argc, argv, &xattr_feature);
}
#endif
