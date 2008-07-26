/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * ocfs2ne_feature_local.c
 *
 * ocfs2 tune utility for updating the mount type.
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
#include <string.h>
#include <ctype.h>
#include <inttypes.h>

#include "ocfs2/ocfs2.h"

#include "libocfs2ne.h"

static int disable_local(ocfs2_filesys *fs, int flags)
{
	errcode_t ret = 0;
	struct ocfs2_super_block *sb = OCFS2_RAW_SB(fs->fs_super);
	struct o2cb_cluster_desc desc;

	if (!ocfs2_mount_local(fs)) {
		verbosef(VL_APP,
			 "Device \"%s\" is already a cluster-aware "
			 "filesystem; nothing to do\n",
			 fs->fs_devname);
		goto out;
	}

	if (!tunefs_interact("Make device \"%s\" a cluster-aware "
			     "filesystem? ",
			     fs->fs_devname))
		goto out;

	/*
	 * Since it was a local device, tunefs_open() will not
	 * have connected to o2cb.
	 */
	ret = o2cb_init();
	if (ret) {
		tcom_err(ret, "while connecting to the cluster stack");
		goto out;
	}
	ret = o2cb_running_cluster_desc(&desc);
	if (ret) {
		tcom_err(ret, "while trying to determine the running cluster");
		goto out;
	}
	if (desc.c_stack)
		verbosef(VL_APP,
			 "Cluster stack: %s\n"
			 "Cluster name: %s\n",
			 desc.c_stack, desc.c_cluster);
	else
		verbosef(VL_APP, "Cluster stack: classic o2cb\n");

	sb->s_feature_incompat &=
		~OCFS2_FEATURE_INCOMPAT_LOCAL_MOUNT;
	tunefs_block_signals();
	ret = ocfs2_set_cluster_desc(fs, &desc);
	tunefs_block_signals();
	o2cb_free_cluster_desc(&desc);
	if (ret)
		tcom_err(ret, "while writing setting the cluster descriptor");

out:
	if (ret)
		errorf("Unable to disable the local mount feature on "
		       "device \"%s\"\n",
		       fs->fs_devname);
	return ret;
}

static int enable_local(ocfs2_filesys *fs, int flags)
{
	errcode_t ret = 0;
	struct ocfs2_super_block *sb = OCFS2_RAW_SB(fs->fs_super);

	if (ocfs2_mount_local(fs)) {
		verbosef(VL_APP,
			 "Device \"%s\" is already a single-node "
			 "filesystem; nothing to do\n",
			 fs->fs_devname);
		goto out;
	}

	if (!tunefs_interact("Make device \"%s\" a single-node "
			     "(non-clustered) filesystem? ",
			     fs->fs_devname))
		goto out;

	sb->s_feature_incompat |=
		OCFS2_FEATURE_INCOMPAT_LOCAL_MOUNT;
	sb->s_feature_incompat &=
		~OCFS2_FEATURE_INCOMPAT_USERSPACE_STACK;

	tunefs_block_signals();
	ret = ocfs2_write_super(fs);
	tunefs_unblock_signals();
	if (ret)
		tcom_err(ret,
			 "while writing out the superblock; Unable to "
			 "enable the local mount feature on device \"%s\"",
			 fs->fs_devname);

out:
	return ret;
}

DEFINE_TUNEFS_FEATURE_INCOMPAT(local,
			       OCFS2_FEATURE_INCOMPAT_LOCAL_MOUNT,
			       TUNEFS_FLAG_RW,
			       enable_local,
			       disable_local);

#ifdef DEBUG_EXE
int main(int argc, char *argv[])
{
	return tunefs_feature_main(argc, argv, &local_feature);
}
#endif
