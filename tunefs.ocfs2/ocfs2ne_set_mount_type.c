/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * ocfs2ne_set_mount_type.c
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
#include "libocfs2ne_err.h"


static errcode_t update_mount_type(ocfs2_filesys *fs, int local)
{
	errcode_t ret = 0;
	struct o2cb_cluster_desc desc;
	struct ocfs2_super_block *sb = OCFS2_RAW_SB(fs->fs_super);

	if (local && ocfs2_mount_local(fs)) {
		verbosef(VL_APP,
			 "Device \"%s\" is already a single-node "
			 "filesystem; nothing to do\n",
			 fs->fs_devname);
		goto out;
	}

	if (!local && !ocfs2_mount_local(fs)) {
		verbosef(VL_APP,
			 "Device \"%s\" is already a cluster-aware "
			 "filesystem; nothing to do\n",
			 fs->fs_devname);
		goto out;
	}

	if (local) {
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
	} else {
		if (!tunefs_interact("Make device \"%s\" a cluster-aware "
				     "filesystem? ",
				     fs->fs_devname))
			goto out;

		/*
		 * Since it was a local device, tunefs_open() will not
		 * have connected to o2cb.
		 */
		ret = o2cb_init();
		if (ret)
			goto out;
		ret = o2cb_running_cluster_desc(&desc);
		if (ret)
			goto out;
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
	}

out:
	return ret;
}

static int set_mount_type_parse_option(char *arg, void *user_data)
{
	int rc = 0;
	int *local = user_data;

	if (!arg) {
		errorf("No mount type specified\n");
		rc = 1;
	} else if (!strcmp(arg, "local"))
		*local = 1;
	else if (!strcmp(arg, "cluster"))
		*local = 0;
	else {
		errorf("Invalid mount type: \"%s\"\n", arg);
		rc = 1;
	}

	return rc;
}

static int set_mount_type_run(ocfs2_filesys *fs, int flags, void *user_data)
{
	errcode_t err;
	int rc = 0;
	int *local = user_data;

	err = update_mount_type(fs, *local);
	if (err) {
		tcom_err(err,
			 "- unable to update the mount type on device "
			 "\"%s\"", fs->fs_devname);
		rc = 1;
	}

	return rc;
}


static int mount_is_local;
DEFINE_TUNEFS_OP(set_mount_type,
		 "Usage: ocfs2ne_set_mount_type [opts] <device> "
		 "{local|cluster}\n",
		 TUNEFS_FLAG_RW,
		 set_mount_type_parse_option,
		 set_mount_type_run,
		 &mount_is_local);

int main(int argc, char *argv[])
{
	return tunefs_main(argc, argv, &set_mount_type_op);
}

