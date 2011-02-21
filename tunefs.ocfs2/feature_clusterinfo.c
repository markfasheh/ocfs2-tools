/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * feature_clusterinfo.c
 *
 * ocfs2 tune utility to enable and disable the clusterinfo feature flag
 *
 * Copyright (C) 2010 Oracle.  All rights reserved.
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
#include <string.h>
#include <ctype.h>
#include <inttypes.h>

#include "ocfs2/ocfs2.h"

#include "libocfs2ne.h"

static int enable_clusterinfo(ocfs2_filesys *fs, int flags)
{
	errcode_t err = 0;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);
	struct tools_progress *prog;
	struct o2cb_cluster_desc desc;

	if (OCFS2_HAS_INCOMPAT_FEATURE(super,
				       OCFS2_FEATURE_INCOMPAT_CLUSTERINFO)) {
		verbosef(VL_APP,
			 "Clusterinfo feature is already enabled; "
			 "nothing to enable\n");
		goto out;
	}

	if (!tools_interact("Enable the clusterinfo feature on "
			    "device \"%s\"? ", fs->fs_devname))
		goto out;

	prog = tools_progress_start("Enable clusterinfo", "clusterinfo", 1);
	if (!prog) {
		err = TUNEFS_ET_NO_MEMORY;
		tcom_err(err, "while initializing the progress display");
		goto out;
	}

	/* With clusterinfo set, userspace flag becomes superfluous */
	OCFS2_SET_INCOMPAT_FEATURE(super, OCFS2_FEATURE_INCOMPAT_CLUSTERINFO);
	OCFS2_CLEAR_INCOMPAT_FEATURE(super,
				     OCFS2_FEATURE_INCOMPAT_USERSPACE_STACK);

	err = o2cb_running_cluster_desc(&desc);
	if (!err) {
		tunefs_block_signals();
		err = ocfs2_set_cluster_desc(fs, &desc);
		tunefs_unblock_signals();
		o2cb_free_cluster_desc(&desc);
	}

	tools_progress_step(prog, 1);
	tools_progress_stop(prog);

out:
	return err;
}

static int disable_clusterinfo(ocfs2_filesys *fs, int flags)
{
	errcode_t err = 0;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);
	struct tools_progress *prog;

	if (!OCFS2_HAS_INCOMPAT_FEATURE(super,
					OCFS2_FEATURE_INCOMPAT_CLUSTERINFO)) {
		verbosef(VL_APP,
			 "Clusterinfo feature is already disabled; "
			 "nothing to disable\n");
		goto out;
	}

	if (!tools_interact("Disable the clusterinfo feature on "
			    "device \"%s\"? ", fs->fs_devname))
		goto out;

	prog = tools_progress_start("Disable clusterinfo", "noclusterinfo", 1);
	if (!prog) {
		err = TUNEFS_ET_NO_MEMORY;
		tcom_err(err, "while initializing the progress display");
		goto out;
	}

	/* When clearing clusterinfo, set userspace if clusterstack != o2cb */
	if (ocfs2_userspace_stack(super))
		OCFS2_SET_INCOMPAT_FEATURE(super,
					OCFS2_FEATURE_INCOMPAT_USERSPACE_STACK);
	OCFS2_CLEAR_INCOMPAT_FEATURE(super, OCFS2_FEATURE_INCOMPAT_CLUSTERINFO);

	tunefs_block_signals();
	err = ocfs2_write_super(fs);
	if (err)
		tcom_err(err, "while writing out the superblock");
	tunefs_unblock_signals();

	tools_progress_step(prog, 1);
	tools_progress_stop(prog);

out:
	return err;

}

DEFINE_TUNEFS_FEATURE_INCOMPAT(clusterinfo,
			       OCFS2_FEATURE_INCOMPAT_CLUSTERINFO,
			       TUNEFS_FLAG_RW,
			       enable_clusterinfo,
			       disable_clusterinfo);

#ifdef DEBUG_EXE
int main(int argc, char *argv[])
{
	return tunefs_feature_main(argc, argv, &clusterinfo_feature);
}
#endif
