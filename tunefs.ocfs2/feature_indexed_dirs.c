/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * feature_indexed_dirs.c
 *
 * ocfs2 tune utility for enabling and disabling the directory indexing
 * feature.
 *
 * Copyright (C) 2009 Novell.  All rights reserved.
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

/*
 * TUNEFS_FLAG_ALLOCATION because disabling will want to dealloc
 * blocks.
 */
DEFINE_TUNEFS_FEATURE_INCOMPAT(indexed_dirs,
			       OCFS2_FEATURE_INCOMPAT_INDEXED_DIRS,
			       TUNEFS_FLAG_RW | TUNEFS_FLAG_ALLOCATION,
			       enable_indexed_dirs,
			       NULL);

#ifdef DEBUG_EXE
int main(int argc, char *argv[])
{
	return tunefs_feature_main(argc, argv, &indexed_dirs_feature);
}
#endif
