/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * feature_refcount.c
 *
 * ocfs2 tune utility for enabling and disabling the refcount feature.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>
#include <inttypes.h>
#include <assert.h>

#include "ocfs2/kernel-rbtree.h"
#include "ocfs2-kernel/kernel-list.h"
#include "ocfs2/ocfs2.h"

#include "libocfs2ne.h"

struct refcount_file {
	struct list_head list;
	uint64_t blkno;
};

struct refcount_block {
	struct rb_node ref_node;
	uint64_t blkno;
	struct list_head files_list;
};

struct disable_refcount_ctxt {
	errcode_t ret;
	struct tools_progress *prog;
	uint32_t more_clusters;
	uint32_t more_ebs;
	struct rb_root ref_blknos;
	int files_count;
};

/* See if the recount_file rbtree has the given ref_blkno.  */
static struct refcount_block*
refcount_block_lookup(struct disable_refcount_ctxt *ctxt,
		      uint64_t ref_blkno)
{
	struct rb_node *p = ctxt->ref_blknos.rb_node;
	struct refcount_block *ref_blk;

	while (p) {
		ref_blk = rb_entry(p, struct refcount_block, ref_node);
		if (ref_blkno < ref_blk->blkno)
			p = p->rb_left;
		else if (ref_blkno > ref_blk->blkno)
			p = p->rb_right;
		else
			return ref_blk;
	}

	return NULL;
}

static void refcount_block_insert(struct disable_refcount_ctxt *ctxt,
				  struct refcount_block *insert_rb)
{
	struct rb_node **p = &ctxt->ref_blknos.rb_node;
	struct rb_node *parent = NULL;
	struct refcount_block *ref_blk = NULL;

	while (*p) {
		parent = *p;
		ref_blk = rb_entry(parent, struct refcount_block, ref_node);
		if (insert_rb->blkno < ref_blk->blkno)
			p = &(*p)->rb_left;
		else if (insert_rb->blkno > ref_blk->blkno)
			p = &(*p)->rb_right;
		else
			assert(0);  /* Caller checked */
	}

	rb_link_node(&insert_rb->ref_node, parent, p);
	rb_insert_color(&insert_rb->ref_node, &ctxt->ref_blknos);
}

static void empty_refcount_file_context(struct disable_refcount_ctxt *ctxt)
{
	struct refcount_block *ref_blk;
	struct refcount_file *file;
	struct rb_node *node;
	struct list_head *p, *next;

	while ((node = rb_first(&ctxt->ref_blknos)) != NULL) {
		ref_blk = rb_entry(node, struct refcount_block, ref_node);

		list_for_each_safe(p, next, &ref_blk->files_list) {
			file = list_entry(p, struct refcount_file, list);
			list_del(&file->list);
			ocfs2_free(&file);
		}

		rb_erase(&ref_blk->ref_node, &ctxt->ref_blknos);
		ocfs2_free(&ref_blk);
	}
}

static int ocfs2_xattr_get_refcount_clusters(ocfs2_cached_inode *ci,
					     char *xe_buf,
					     uint64_t xe_blkno,
					     struct ocfs2_xattr_entry *xe,
					     char *value_buf,
					     uint64_t value_blkno,
					     void *value,
					     int in_bucket,
					     void *priv_data)
{
	errcode_t ret;
	uint32_t *clusters = priv_data;
	uint32_t cpos = 0, len, p_cluster, num_clusters;
	uint16_t ext_flags;
	struct ocfs2_xattr_value_root *xv;

	if (ocfs2_xattr_is_local(xe))
		return 0;

	xv = (struct ocfs2_xattr_value_root *)value;
	len = xv->xr_clusters;
	while (len) {
		ret = ocfs2_xattr_get_clusters(ci->ci_fs,
					       &xv->xr_list,
					       value_blkno,
					       value_buf,
					       cpos,
					       &p_cluster,
					       &num_clusters,
					       &ext_flags);
		if (ret)
			break;

		if (ext_flags & OCFS2_EXT_REFCOUNTED)
			*clusters += num_clusters;

		len -= num_clusters;
		cpos += num_clusters;
	}

	if (ret)
		return OCFS2_XATTR_ERROR;

	return 0;
}

static errcode_t ocfs2_find_refcounted_clusters(ocfs2_filesys *fs,
						uint64_t blkno,
						uint32_t *clusters)
{
	errcode_t ret;
	ocfs2_cached_inode *ci = NULL;
	uint32_t cpos, len, p_cluster, num_clusters;
	uint16_t ext_flags;

	ret = ocfs2_read_cached_inode(fs, blkno, &ci);
	if (ret)
		goto out;

	*clusters = 0;
	cpos = 0;
	if (!(ci->ci_inode->i_dyn_features & OCFS2_INLINE_DATA_FL)) {
		len = ocfs2_clusters_in_bytes(fs, ci->ci_inode->i_size);
		while (len) {
			ret = ocfs2_get_clusters(ci, cpos, &p_cluster,
						 &num_clusters, &ext_flags);
			if (ret)
				break;

			if (ext_flags & OCFS2_EXT_REFCOUNTED)
				*clusters += num_clusters;

			len -= num_clusters;
			cpos += num_clusters;
		}
	}

	if (ci->ci_inode->i_dyn_features & OCFS2_HAS_XATTR_FL)
		ret = ocfs2_xattr_iterate(ci,
					  ocfs2_xattr_get_refcount_clusters,
					  clusters);
out:
	if (ci)
		ocfs2_free_cached_inode(fs, ci);

	return ret;
}

static errcode_t refcount_iterate(ocfs2_filesys *fs, struct ocfs2_dinode *di,
				  void *user_data)
{
	errcode_t ret = 0;
	uint32_t clusters;
	uint64_t blk_num;
	struct refcount_file *file = NULL;
	struct refcount_block *ref_blk = NULL;
	struct disable_refcount_ctxt *ctxt = user_data;
	uint32_t recs_per_eb = ocfs2_extent_recs_per_eb(fs->fs_blocksize);

	if (!S_ISREG(di->i_mode))
		goto bail;

	if (di->i_flags & OCFS2_SYSTEM_FL)
		goto bail;

	if (!(di->i_dyn_features & OCFS2_HAS_REFCOUNT_FL))
		goto bail;

	ret = ocfs2_find_refcounted_clusters(fs, di->i_blkno,
					     &clusters);
	if (ret)
		goto bail;

	ret = ocfs2_malloc0(sizeof(struct refcount_file), &file);
	if (ret)
		goto bail;

	file->blkno = di->i_blkno;
	INIT_LIST_HEAD(&file->list);

	ref_blk = refcount_block_lookup(ctxt, di->i_refcount_loc);
	if (!ref_blk) {
		ret = ocfs2_malloc0(sizeof(struct refcount_block), &ref_blk);
		if (ret)
			goto bail;

		ref_blk->blkno = di->i_refcount_loc;
		INIT_LIST_HEAD(&ref_blk->files_list);
		refcount_block_insert(ctxt, ref_blk);
	}
	list_add_tail(&file->list, &ref_blk->files_list);

	ctxt->more_clusters += clusters;
	blk_num = (clusters + recs_per_eb - 1) / recs_per_eb;
	ctxt->more_ebs += ocfs2_clusters_in_blocks(fs, blk_num);
	ctxt->files_count++;

	tools_progress_step(ctxt->prog, 1);

	return 0;

bail:
	if (file)
		ocfs2_free(&file);

	return ret;
}

static errcode_t find_refcounted_files(ocfs2_filesys *fs,
				       struct disable_refcount_ctxt *ctxt)
{
	errcode_t ret;
	uint32_t free_clusters = 0;

	ctxt->prog = tools_progress_start("Scanning filesystem", "scanning",
					  0);
	if (!ctxt->prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		goto bail;
	}

	ret = tunefs_foreach_inode(fs, refcount_iterate, ctxt);
	if (ret)
		goto bail;

	ret = tunefs_get_free_clusters(fs, &free_clusters);
	if (ret)
		goto bail;

	verbosef(VL_APP,
		 "We have %u clusters free, and need %u clusters to fill "
		 "every sparse file and %u clusters for more extent "
		 "blocks\n",
		 free_clusters, ctxt->more_clusters,
		 ctxt->more_ebs);

	if (free_clusters < (ctxt->more_clusters + ctxt->more_ebs))
		ret = OCFS2_ET_NO_SPACE;

bail:
	if (ctxt->prog) {
		tools_progress_stop(ctxt->prog);
		ctxt->prog = NULL;
	}

	return ret;
}

static int ocfs2_xattr_cow_refcount_clusters(ocfs2_cached_inode *ci,
					     char *xe_buf,
					     uint64_t xe_blkno,
					     struct ocfs2_xattr_entry *xe,
					     char *value_buf,
					     uint64_t value_blkno,
					     void *value,
					     int in_bucket,
					     void *priv_data)
{
	errcode_t ret;
	uint32_t cpos = 0, len, p_cluster, num_clusters;
	uint16_t ext_flags;
	struct ocfs2_xattr_value_root *xv;

	if (ocfs2_xattr_is_local(xe))
		return 0;

	xv = (struct ocfs2_xattr_value_root *)value;
	len = xv->xr_clusters;
	while (len) {
		ret = ocfs2_xattr_get_clusters(ci->ci_fs,
					       &xv->xr_list,
					       value_blkno,
					       value_buf,
					       cpos,
					       &p_cluster,
					       &num_clusters,
					       &ext_flags);
		if (ret)
			break;

		if (ext_flags & OCFS2_EXT_REFCOUNTED) {
			ret = ocfs2_refcount_cow_xattr(ci, xe_buf, xe_blkno,
						       value_buf, value_blkno,
						       xv, 0, xv->xr_clusters);
			break;
		}

		len -= num_clusters;
		cpos += num_clusters;
	}

	if (ret)
		return OCFS2_XATTR_ERROR;

	return 0;
}

static errcode_t refcount_one_file(ocfs2_filesys *fs,
				   struct refcount_file *file)
{
	errcode_t ret;
	ocfs2_cached_inode *ci = NULL;
	uint32_t len;

	ret = ocfs2_read_cached_inode(fs, file->blkno, &ci);
	if (ret)
		goto out;

	if (!(ci->ci_inode->i_dyn_features & OCFS2_INLINE_DATA_FL)) {
		len = ocfs2_clusters_in_bytes(fs, ci->ci_inode->i_size);
		ret = ocfs2_refcount_cow(ci, 0, len, UINT_MAX);
		if (ret)
			goto out;
	}

	if (ci->ci_inode->i_dyn_features & OCFS2_HAS_XATTR_FL) {
		ret = ocfs2_xattr_iterate(ci,
					  ocfs2_xattr_cow_refcount_clusters,
					  NULL);
		if (ret)
			goto out;
	}

	ci->ci_inode->i_dyn_features &= ~OCFS2_HAS_REFCOUNT_FL;
	ci->ci_inode->i_refcount_loc = 0;

	ret = ocfs2_write_cached_inode(fs, ci);

out:
	if (ci)
		ocfs2_free_cached_inode(fs, ci);

	return ret;
}

static errcode_t free_refcount_tree(ocfs2_filesys *fs,
				    struct refcount_block *ref_blk)
{
	errcode_t ret;
	char *buf = NULL;
	struct ocfs2_refcount_block *rb = NULL;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		goto out;

	ret = ocfs2_read_refcount_block(fs, ref_blk->blkno, buf);
	if (ret)
		goto out;

	rb = (struct ocfs2_refcount_block *)buf;

	/* Now the refcount tree should be an empty leaf one. */
	assert(!(rb->rf_flags & OCFS2_REFCOUNT_TREE_FL));
	assert(!rb->rf_records.rl_used);

	ret = ocfs2_delete_refcount_block(fs, ref_blk->blkno);

out:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

static errcode_t replace_refcounted_files(ocfs2_filesys *fs,
					  struct disable_refcount_ctxt *ctxt)
{
	errcode_t ret = 0;
	struct tools_progress *prog;
	struct refcount_block *ref_blk;
	struct refcount_file *file;
	struct rb_node *node;
	struct list_head *p, *next;

	prog = tools_progress_start("Replacing files", "replacing",
				    ctxt->files_count);
	if (!prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		goto out;
	}

	while ((node = rb_first(&ctxt->ref_blknos)) != NULL) {
		ref_blk = rb_entry(node, struct refcount_block, ref_node);

		list_for_each_safe(p, next, &ref_blk->files_list) {
			file = list_entry(p, struct refcount_file, list);
			ret = refcount_one_file(fs, file);
			if (ret)
				goto out;
			tools_progress_step(prog, 1);
		}

		ret = free_refcount_tree(fs, ref_blk);
		if (ret)
			goto out;
		rb_erase(&ref_blk->ref_node, &ctxt->ref_blknos);
		ocfs2_free(&ref_blk);
	}

out:
	if (prog)
		tools_progress_stop(prog);

	return ret;
}

static int disable_refcount(ocfs2_filesys *fs, int flags)
{
	errcode_t ret = 0;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);
	struct disable_refcount_ctxt ctxt;
	struct tools_progress *prog = NULL;

	if (!ocfs2_refcount_tree(super)) {
		verbosef(VL_APP,
			 "Refcount feature is not enabled; "
			 "nothing to disable\n");
		goto out;
	}

	if (!tools_interact("Disable the refcount feature on device "
			    "\"%s\"? ", fs->fs_devname))
		goto out;

	prog = tools_progress_start("Disabling refcount", "norefcount", 3);
	if (!prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		tcom_err(ret, "while initializing the progress display");
		goto out;
	}

	memset(&ctxt, 0, sizeof(ctxt));
	ctxt.ref_blknos = RB_ROOT;

	ret = find_refcounted_files(fs, &ctxt);
	if (ret) {
		if (ret == OCFS2_ET_NO_SPACE)
			errorf("There is not enough space to fill all of "
			       "the refcounted files on device \"%s\"\n",
			       fs->fs_devname);
		else
			tcom_err(ret, "while trying to find refcounted files");
		goto out_cleanup;
	}
	tools_progress_step(prog, 1);

	ret = replace_refcounted_files(fs, &ctxt);
	if (ret) {
		tcom_err(ret,
			 "while trying to replace refcounted files on device "
			 "\"%s\"", fs->fs_devname);
		goto out_cleanup;
	}
	tools_progress_step(prog, 1);

	OCFS2_CLEAR_INCOMPAT_FEATURE(super,
				     OCFS2_FEATURE_INCOMPAT_REFCOUNT_TREE);
	tunefs_block_signals();
	ret = ocfs2_write_super(fs);
	tunefs_unblock_signals();
	if (ret)
		tcom_err(ret, "while writing out the superblock");

	tools_progress_step(prog, 1);

out_cleanup:
	empty_refcount_file_context(&ctxt);

out:
	if (prog)
		tools_progress_stop(prog);

	return ret;
}

static int enable_refcount(ocfs2_filesys *fs, int flags)
{
	errcode_t ret = 0;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);
	struct tools_progress *prog;

	if (ocfs2_refcount_tree(super)) {
		verbosef(VL_APP,
			 "Refcount feature is already enabled; "
			 "nothing to enable\n");
		goto out;
	}

	if (!tools_interact("Enable the refcount feature on "
			    "device \"%s\"? ",
			    fs->fs_devname))
		goto out;

	prog = tools_progress_start("Enable refcount", "refcount", 1);
	if (!prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		tcom_err(ret, "while initializing the progress display");
		goto out;
	}

	OCFS2_SET_INCOMPAT_FEATURE(super,
				 OCFS2_FEATURE_INCOMPAT_REFCOUNT_TREE);
	tunefs_block_signals();
	ret = ocfs2_write_super(fs);
	tunefs_unblock_signals();
	if (ret)
		tcom_err(ret, "while writing out the superblock");

	tools_progress_step(prog, 1);
	tools_progress_stop(prog);
out:
	return ret;
}

DEFINE_TUNEFS_FEATURE_INCOMPAT(refcount,
			       OCFS2_FEATURE_INCOMPAT_REFCOUNT_TREE,
			       TUNEFS_FLAG_RW | TUNEFS_FLAG_ALLOCATION |
			       TUNEFS_FLAG_LARGECACHE,
			       enable_refcount,
			       disable_refcount);

#ifdef DEBUG_EXE
int main(int argc, char *argv[])
{
	return tunefs_feature_main(argc, argv, &refcount_feature);
}
#endif
