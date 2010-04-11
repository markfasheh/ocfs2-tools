/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * feature_indexed_dirs.c
 *
 * ocfs2 tune utility for enabling and disabling the directory indexing
 * feature.
 *
 * Copyright (C) 2009, 2010 Novell.  All rights reserved.
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

#include "ocfs2/ocfs2.h"

#include "libocfs2ne.h"

struct dx_dirs_inode {
	struct list_head list;
	uint64_t ino;
};

struct dx_dirs_context {
	errcode_t ret;
	uint64_t dx_dirs_nr;
	struct list_head inodes;
	struct tools_progress *prog;
};

static int enable_indexed_dirs(ocfs2_filesys *fs, int flags)
{
	errcode_t ret = 0;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);
	struct tools_progress *prog;

	if (ocfs2_supports_indexed_dirs(super)) {
		verbosef(VL_APP,
			 "Directory indexing feature is already enabled; "
			 "nothing to enable\n");
		goto out;
	}


	if (!tools_interact("Enable the directory indexing feature on "
			    "device \"%s\"? ",
			    fs->fs_devname))
		goto out;

	prog = tools_progress_start("Enable directory indexing", "dir idx", 1);
	if (!prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		tcom_err(ret, "while initializing the progress display");
		goto out;
	}

	OCFS2_SET_INCOMPAT_FEATURE(super,
				   OCFS2_FEATURE_INCOMPAT_INDEXED_DIRS);
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

static errcode_t dx_dir_iterate(ocfs2_filesys *fs, struct ocfs2_dinode *di,
				void *user_data)
{
	errcode_t ret = 0;
	struct dx_dirs_inode *dx_di = NULL;
	struct dx_dirs_context *ctxt= (struct dx_dirs_context *)user_data;

	if (!S_ISDIR(di->i_mode))
		goto bail;

	if (!(di->i_dyn_features & OCFS2_INDEXED_DIR_FL))
		goto bail;

	ret = ocfs2_malloc0(sizeof(struct dx_dirs_inode), &dx_di);
	if (ret) {
		ret = TUNEFS_ET_NO_MEMORY;
		goto bail;
	}

	dx_di->ino = di->i_blkno;
	ctxt->dx_dirs_nr ++;
	list_add_tail(&dx_di->list, &ctxt->inodes);

	tools_progress_step(ctxt->prog, 1);

bail:
	return ret;
}


static errcode_t find_indexed_dirs(ocfs2_filesys *fs,
				   struct dx_dirs_context *ctxt)
{
	errcode_t ret;

	ctxt->prog = tools_progress_start("Scanning filesystem", "scanning", 0);
	if (!ctxt->prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		goto bail;
	}

	ret = tunefs_foreach_inode(fs, dx_dir_iterate, ctxt);
	if (ret) {
		if (ret != TUNEFS_ET_NO_MEMORY)
			ret = TUNEFS_ET_DX_DIRS_SCAN_FAILED;
		goto bail;
	}

	verbosef(VL_APP,
		"We have %lu indexed %s to truncate.\n",
		ctxt->dx_dirs_nr,
		(ctxt->dx_dirs_nr > 1)?"directories":"directory");

bail:
	if (ctxt->prog)
		tools_progress_stop(ctxt->prog);

	return ret;
}

static errcode_t clean_indexed_dirs(ocfs2_filesys *fs,
				    struct dx_dirs_context *ctxt)
{
	errcode_t ret = 0;
	struct list_head *pos;
	struct dx_dirs_inode *dx_di;
	struct tools_progress *prog;
	uint64_t dirs_truncated = 0;

	prog = tools_progress_start("Truncating indexed dirs", "truncating",
				    ctxt->dx_dirs_nr);
	if (!prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		goto bail;
	}

	list_for_each(pos, &ctxt->inodes) {
		dx_di = list_entry(pos, struct dx_dirs_inode, list);

		ret = ocfs2_dx_dir_truncate(fs, dx_di->ino);
		if (ret) {
			verbosef(VL_APP,
				"Truncate directory (ino \"%lu\") failed.",
				dx_di->ino);
			ret = TUNEFS_ET_DX_DIRS_TRUNCATE_FAILED;
			goto bail;
		}
		dirs_truncated ++;
		tools_progress_step(prog, 1);
	}

bail:
	tools_progress_stop(prog);
	verbosef(VL_APP,
		"\"%lu\" from \"%lu\" indexed %s truncated.",
		dirs_truncated, ctxt->dx_dirs_nr,
		(dirs_truncated <= 1) ? "directory is" : "directories are");

	return ret;
}

static void release_dx_dirs_context(struct dx_dirs_context *ctxt)
{
	struct list_head *pos, *n;
	struct dx_dirs_inode *dx_di;

	list_for_each_safe(pos, n, &ctxt->inodes) {
		dx_di = list_entry(pos, struct dx_dirs_inode, list);
		list_del(&dx_di->list);
		ocfs2_free(&dx_di);
	}
}

static int disable_indexed_dirs(ocfs2_filesys *fs, int flags)
{
	errcode_t ret = 0;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);
	struct dx_dirs_context ctxt;
	struct tools_progress *prog = NULL;

	if (!ocfs2_supports_indexed_dirs(super)) {
		verbosef(VL_APP,
			"Directory indexing feature is not enabled; "
			"nothing to disable\n");
		goto out;
	}

	if (!tools_interact("Disabling the directory indexing feature on "
			    "device \"%s\"? ",
			    fs->fs_devname))
		goto out;

	prog = tools_progress_start("Disable directory indexing", "no dir idx", 2);
	if (!prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		tcom_err(ret, "while initializing the progress display");
		goto out;
	}

	memset(&ctxt, 0, sizeof (struct dx_dirs_context));
	INIT_LIST_HEAD(&ctxt.inodes);
	ret = find_indexed_dirs(fs, &ctxt);
	if (ret) {
		tcom_err(ret, "while scanning indexed directories");
		goto out_cleanup;
	}

	tools_progress_step(prog, 1);

	tunefs_block_signals();
	ret = clean_indexed_dirs(fs, &ctxt);
	if (ret) {
		tcom_err(ret, "while truncate indexed directories");
	}

	/* We already touched file system, must disable dx dirs flag here.
	 * fsck.ocfs2 will handle the orphan indexed trees. */
	OCFS2_CLEAR_INCOMPAT_FEATURE(super,
				     OCFS2_FEATURE_INCOMPAT_INDEXED_DIRS);
	ret = ocfs2_write_super(fs);
	tunefs_unblock_signals();

	if (ret) {
		ret = TUNEFS_ET_IO_WRITE_FAILED;
		tcom_err(ret, "while writing super block");
	}

	tools_progress_step(prog, 1);
out_cleanup:
	release_dx_dirs_context(&ctxt);
out:
	if (prog)
		tools_progress_stop(prog);

	return ret;
}

/*
 * TUNEFS_FLAG_ALLOCATION because disabling will want to dealloc
 * blocks.
 */
DEFINE_TUNEFS_FEATURE_INCOMPAT(indexed_dirs,
			       OCFS2_FEATURE_INCOMPAT_INDEXED_DIRS,
			       TUNEFS_FLAG_RW | TUNEFS_FLAG_ALLOCATION,
			       enable_indexed_dirs,
			       disable_indexed_dirs);

#ifdef DEBUG_EXE
int main(int argc, char *argv[])
{
	return tunefs_feature_main(argc, argv, &indexed_dirs_feature);
}
#endif
