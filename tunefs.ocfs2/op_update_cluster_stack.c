/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * op_update_cluster_stack.c
 *
 * ocfs2 tune utility for updating the cluster stack.
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


static errcode_t update_cluster(ocfs2_filesys *fs)
{
	errcode_t ret;
	struct o2cb_cluster_desc desc;

	if (!tunefs_interact_critical(
		"Updating on-disk cluster information "
		"to match the running cluster.\n"
		"DANGER: YOU MUST BE ABSOLUTELY SURE THAT NO OTHER NODE "
		"IS USING THIS FILESYSTEM BEFORE MODIFYING "
		"ITS CLUSTER CONFIGURATION.\n"
	        "Update the on-disk cluster information? "
		))
		return 0;

	ret = o2cb_running_cluster_desc(&desc);
	if (!ret) {
		tunefs_block_signals();
		ret = ocfs2_set_cluster_desc(fs, &desc);
		tunefs_unblock_signals();
		o2cb_free_cluster_desc(&desc);
	}

	return ret;
}

static int update_cluster_stack_run(struct tunefs_operation *op,
				    ocfs2_filesys *fs, int flags)
{
	int rc = 0;
	errcode_t err;

	if (flags & TUNEFS_FLAG_NOCLUSTER) {
		err = update_cluster(fs);
		if (err) {
			rc = 1;
			tcom_err(err,
				 "- unable to update the cluster stack "
				 "information on device \"%s\"",
				 fs->fs_devname);
		}
	} else {
		verbosef(VL_APP,
			 "Device \"%s\" is already configured for the "
			 "running cluster; nothing to do\n",
			 fs->fs_devname);
	}

	return rc;
}


DEFINE_TUNEFS_OP(update_cluster_stack,
		 "Usage: op_update_cluster_stack [opts] <device>\n",
		 TUNEFS_FLAG_RW | TUNEFS_FLAG_NOCLUSTER,
		 NULL,
		 update_cluster_stack_run);

#ifdef DEBUG_EXE
int main(int argc, char *argv[])
{
	return tunefs_op_main(argc, argv, &update_cluster_stack_op);
}
#endif
