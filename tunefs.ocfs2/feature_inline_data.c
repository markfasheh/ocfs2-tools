/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * feature_inline_data.c
 *
 * ocfs2 tune utility for enabling and disabling the inline data feature.
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


/*
 * We scan up-front to find out how many files we have to expand.  We keep
 * track of them so that we don't have to scan again to do the work.
 */
struct inline_data_inode {
	struct list_head list;
	uint64_t blkno;
};

struct inline_data_context {
	errcode_t ret;
	uint32_t more_clusters;
	struct list_head inodes;
	struct tools_progress *prog;
};


static int enable_inline_data(ocfs2_filesys *fs, int flags)
{
	errcode_t ret = 0;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);
	struct tools_progress *prog = NULL;

	if (ocfs2_support_inline_data(super)) {
		verbosef(VL_APP,
			 "The inline data feature is already enabled; "
			 "nothing to enable\n");
		goto out;
	}

	if (!tools_interact("Enable the inline data feature on device "
			    "\"%s\"? ",
			    fs->fs_devname))
		goto out;

	prog = tools_progress_start("Enabling inline-data", "inline-data", 1);
	if (!prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		tcom_err(ret, "while initializing the progress display");
		goto out;
	}

	OCFS2_SET_INCOMPAT_FEATURE(super,
				   OCFS2_FEATURE_INCOMPAT_INLINE_DATA);
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

static errcode_t inline_iterate(ocfs2_filesys *fs, struct ocfs2_dinode *di,
				void *user_data)
{
	errcode_t ret = 0;
	struct inline_data_inode *idi = NULL;
	struct inline_data_context *ctxt = user_data;

	if (!S_ISREG(di->i_mode) && !S_ISDIR(di->i_mode))
		goto bail;

	if (!(di->i_dyn_features & OCFS2_INLINE_DATA_FL))
		goto bail;

	ret = ocfs2_malloc0(sizeof(struct inline_data_inode), &idi);
	if (ret)
		goto bail;

	idi->blkno = di->i_blkno;
	ctxt->more_clusters++;
	list_add_tail(&idi->list, &ctxt->inodes);

	tools_progress_step(ctxt->prog, 1);

	return 0;

bail:
	return ret;
}

static errcode_t find_inline_data(ocfs2_filesys *fs,
				  struct inline_data_context *ctxt)
{
	errcode_t ret;
	uint32_t free_clusters = 0;

	ctxt->prog = tools_progress_start("Scanning filesystem", "scanning",
					  0);
	if (!ctxt->prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		goto bail;
	}

	ret = tunefs_foreach_inode(fs, inline_iterate, ctxt);
	if (ret)
		goto bail;

	ret = tunefs_get_free_clusters(fs, &free_clusters);
	if (ret)
		goto bail;

	verbosef(VL_APP,
		 "We have %u clusters free, and need %u clusters to expand "
		 "all inline data\n",
		 free_clusters, ctxt->more_clusters);

	if (free_clusters < ctxt->more_clusters)
		ret = OCFS2_ET_NO_SPACE;

bail:
	if (ctxt->prog)
		tools_progress_stop(ctxt->prog);

	return ret;
}

static void empty_inline_data_context(struct inline_data_context *ctxt)
{
	struct list_head *pos, *n;
	struct inline_data_inode *idi;

	list_for_each_safe(pos, n, &ctxt->inodes) {
		idi = list_entry(pos, struct inline_data_inode, list);
		list_del(&idi->list);
		ocfs2_free(&idi);
	}
}

static errcode_t expand_inline_data(ocfs2_filesys *fs,
				    struct inline_data_context *ctxt)
{
	errcode_t ret = 0, err;
	struct list_head *pos;
	struct inline_data_inode *idi;
	ocfs2_cached_inode *ci = NULL;
	struct tools_progress *prog;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);
	ocfs2_quota_hash *usrhash = NULL, *grphash = NULL;
	uint32_t uid, gid;
	long long change;

	prog = tools_progress_start("Expanding inline files", "expanding",
				    ctxt->more_clusters);
	if (!ctxt->prog)
		return TUNEFS_ET_NO_MEMORY;

	ret = ocfs2_load_fs_quota_info(fs);
	if (ret)
		goto out;

	ret = ocfs2_init_quota_change(fs, &usrhash, &grphash);
	if (ret)
		goto out;

	list_for_each(pos, &ctxt->inodes) {
		idi = list_entry(pos, struct inline_data_inode, list);

		ret = ocfs2_read_cached_inode(fs, idi->blkno, &ci);
		if (ret)
			break;

		ret = ocfs2_convert_inline_data_to_extents(ci);
		if (ret) {
			ocfs2_free_cached_inode(fs, ci);
			break;
		}
		if (ci->ci_inode->i_flags & OCFS2_SYSTEM_FL &&
		    idi->blkno != super->s_root_blkno) {
			ocfs2_free_cached_inode(fs, ci);
			goto next;
		}
		change = ocfs2_clusters_to_bytes(fs,
						 ci->ci_inode->i_clusters);
		uid = ci->ci_inode->i_uid;
		gid = ci->ci_inode->i_gid;
		ocfs2_free_cached_inode(fs, ci);

		ret = ocfs2_apply_quota_change(fs, usrhash, grphash, uid, gid,
					       change, 0);
		if (ret)
			break;
next:
		tools_progress_step(prog, 1);
	}
out:
	err = ocfs2_finish_quota_change(fs, usrhash, grphash);
	if (!ret)
		ret = err;

	tools_progress_stop(prog);

	return ret;
}

static int disable_inline_data(ocfs2_filesys *fs, int flags)
{
	errcode_t ret = 0;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);
	struct inline_data_context ctxt;
	struct tools_progress *prog = NULL;

	if (!ocfs2_support_inline_data(super)) {
		verbosef(VL_APP,
			 "The inline data feature is not enabled; "
			 "nothing to disable\n");
		goto out;
	}

	if (!tools_interact("Disable the inline data feature on device "
			    "\"%s\"? ",
			    fs->fs_devname))
		goto out;

	prog = tools_progress_start("Disabling inline-data", "noinline-data", 3);

	if (!prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		tcom_err(ret, "while initializing the progress display");
		goto out;
	}

	memset(&ctxt, 0, sizeof(ctxt));
	INIT_LIST_HEAD(&ctxt.inodes);
	ret = find_inline_data(fs, &ctxt);
	if (ret) {
		if (ret == OCFS2_ET_NO_SPACE)
			errorf("There is not enough space to expand all of "
			       "the inline data on device \"%s\"\n",
			       fs->fs_devname);
		else
			tcom_err(ret,
				 "while trying to find files with inline data");
		goto out_cleanup;
	}

	tools_progress_step(prog, 1);

	ret = expand_inline_data(fs, &ctxt);
	if (ret) {
		tcom_err(ret,
			 "while trying to expand the inline data on device "
			 "\"%s\"",
			 fs->fs_devname);
		goto out_cleanup;
	}

	tools_progress_step(prog, 1);

	OCFS2_CLEAR_INCOMPAT_FEATURE(super,
				     OCFS2_FEATURE_INCOMPAT_INLINE_DATA);
	tunefs_block_signals();
	ret = ocfs2_write_super(fs);
	tunefs_unblock_signals();
	if (ret)
		tcom_err(ret, "while writing out the superblock");

	tools_progress_step(prog, 1);

out_cleanup:
	empty_inline_data_context(&ctxt);

out:
	if (prog)
		tools_progress_stop(prog);

	return ret;
}

DEFINE_TUNEFS_FEATURE_INCOMPAT(inline_data,
			       OCFS2_FEATURE_INCOMPAT_INLINE_DATA,
			       TUNEFS_FLAG_RW | TUNEFS_FLAG_ALLOCATION |
			       TUNEFS_FLAG_LARGECACHE,
			       enable_inline_data,
			       disable_inline_data);

#ifdef DEBUG_EXE
int main(int argc, char *argv[])
{
	return tunefs_feature_main(argc, argv, &inline_data_feature);
}
#endif
