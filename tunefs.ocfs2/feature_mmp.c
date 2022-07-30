/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * feature_append_mmp.c
 *
 * ocfs2 tune utility for enabling and disabling the multiple
 * mount protection feature.
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

/*
 * Default interval for MMP update in seconds.
 */
#define OCFS2_MMP_UPDATE_INTERVAL    5

/*
 * Maximum interval for MMP update in seconds.
 */
#define OCFS2_MMP_MAX_UPDATE_INTERVAL    300

static int __check_mounted_rdonly(ocfs2_filesys *fs)
{
	errcode_t ret = 0;
	int mount_flags;

	ret = ocfs2_check_if_mounted(fs->fs_devname, &mount_flags);
	if (ret) {
		tcom_err(ret, "while determining whether %s is mounted.",
			fs->fs_devname);
		goto out;
	}

	if ((mount_flags & OCFS2_MF_MOUNTED) ||
		(mount_flags & OCFS2_MF_READONLY)) {
		ret = TUNEFS_ET_PERFORM_ONLINE;
		tcom_err(ret, "The multiple mount protection feature can't\n"
					"be set if the filesystem is mounted or\n"
					"read-only.\n");
	}

out:
	return ret;
}

static int enable_mmp(ocfs2_filesys *fs, int flags)
{
	errcode_t ret = 0;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);
	struct tools_progress *prog;

	if (ocfs2_supports_mmp(super)) {
		verbosef(VL_APP,
			 "Multiple mount protection feature is already enabled; "
			 "nothing to enable\n");
		goto out;
	}

	ret = __check_mounted_rdonly(fs);
	if (ret)
		goto out;

	if (!tools_interact("Enable the multiple mount protection feature on "
			    "device \"%s\"? ",
			    fs->fs_devname))
		goto out;

	prog = tools_progress_start("Enable MMP", "mmp", 1);
	if (!prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		tcom_err(ret, "while initializing the progress display");
		goto out;
	}

	OCFS2_SET_INCOMPAT_FEATURE(super,
				    OCFS2_FEATURE_INCOMPAT_MMP);

	if (super->s_mmp_update_interval == 0) {
		super->s_mmp_update_interval = OCFS2_MMP_UPDATE_INTERVAL;
	} else if (super->s_mmp_update_interval > OCFS2_MMP_MAX_UPDATE_INTERVAL) {
		ret = TUNEFS_ET_INVALID_ARGUMENT;
		goto out;
	}

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

static int disable_mmp(ocfs2_filesys *fs, int flags)
{
	errcode_t ret = 0;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);
	struct tools_progress *prog = NULL;

	if (!ocfs2_supports_mmp(super)) {
		verbosef(VL_APP,
			 "Multiple mount protection is not enabled; "
			 "nothing to disable\n");
		goto out;
	}

	ret = __check_mounted_rdonly(fs);
	if (ret)
		goto out;

	if (!tools_interact("Disable the Multiple mount protection feature on "
			    "device \"%s\"? ",
			    fs->fs_devname))
		goto out;

	prog = tools_progress_start("Disabling MMP", "nommp", 3);
	if (!prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		tcom_err(ret, "while initializing the progress display");
		goto out;
	}

	OCFS2_CLEAR_INCOMPAT_FEATURE(super,
				      OCFS2_FEATURE_INCOMPAT_MMP);
	super->s_mmp_update_interval = 0;
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

DEFINE_TUNEFS_FEATURE_INCOMPAT(mmp,
				OCFS2_FEATURE_INCOMPAT_MMP,
				TUNEFS_FLAG_RW | TUNEFS_FLAG_NOCLUSTER,
				enable_mmp,
				disable_mmp);

#ifdef DEBUG_EXE
int main(int argc, char *argv[])
{
	return tunefs_feature_main(argc, argv, &mmp_feature);
}
#endif
