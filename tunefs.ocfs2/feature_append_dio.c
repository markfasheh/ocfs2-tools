/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * feature_append_dio.c
 *
 * ocfs2 tune utility for enabling and disabling the append direct
 * io feature.
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


static int enable_append_dio(ocfs2_filesys *fs, int flags)
{
	errcode_t ret = 0;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);
	struct tools_progress *prog;

	if (ocfs2_supports_append_dio(super)) {
		verbosef(VL_APP,
			 "Append direct io feature is already enabled; "
			 "nothing to enable\n");
		goto out;
	}

	if (!tools_interact("Enable the append direct io feature on "
			    "device \"%s\"? ",
			    fs->fs_devname))
		goto out;

	prog = tools_progress_start("Enable append direct io", "append-dio", 1);
	if (!prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		tcom_err(ret, "while initializing the progress display");
		goto out;
	}

	OCFS2_SET_INCOMPAT_FEATURE(super,
				    OCFS2_FEATURE_INCOMPAT_APPEND_DIO);
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

static int disable_append_dio(ocfs2_filesys *fs, int flags)
{
	errcode_t ret = 0;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);
	struct tools_progress *prog = NULL;

	if (!ocfs2_supports_append_dio(super)) {
		verbosef(VL_APP,
			 "Append direct io feature is not enabled; "
			 "nothing to disable\n");
		goto out;
	}

	if (!tools_interact("Disable the append direct io feature on "
			    "device \"%s\"? ",
			    fs->fs_devname))
		goto out;

	prog = tools_progress_start("Disabling append direct io", "noappend-dio", 0);
	if (!prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		tcom_err(ret, "while initializing the progress display");
		goto out;
	}

	OCFS2_CLEAR_INCOMPAT_FEATURE(super,
				      OCFS2_FEATURE_INCOMPAT_APPEND_DIO);
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

DEFINE_TUNEFS_FEATURE_INCOMPAT(append_dio,
				OCFS2_FEATURE_INCOMPAT_APPEND_DIO,
				TUNEFS_FLAG_RW | TUNEFS_FLAG_ONLINE,
				enable_append_dio,
				disable_append_dio);

#ifdef DEBUG_EXE
int main(int argc, char *argv[])
{
	return tunefs_feature_main(argc, argv, &append_dio_feature);
}
#endif
