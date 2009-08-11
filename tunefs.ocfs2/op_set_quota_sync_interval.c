/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * op_set_quota_sync_interval.c
 *
 * ocfs2 tune utility for updating interval for syncing quota structures
 * to global quota file.
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
#include <string.h>
#include <ctype.h>
#include <inttypes.h>

#include "ocfs2/ocfs2.h"

#include "libocfs2ne.h"

static char *type2name(int type)
{
	if (type == USRQUOTA)
		return "user";
	return "group";
}

static int update_sync_interval(ocfs2_filesys *fs, int type,
				unsigned long syncms)
{
	errcode_t err;
	struct tools_progress *prog;
	int feature = (type == USRQUOTA) ? OCFS2_FEATURE_RO_COMPAT_USRQUOTA :
					   OCFS2_FEATURE_RO_COMPAT_GRPQUOTA;
	struct ocfs2_global_disk_dqinfo *qinfo;

	if (!OCFS2_HAS_RO_COMPAT_FEATURE(OCFS2_RAW_SB(fs->fs_super), feature)) {
		errorf("The %s quota is not enabled on device \"%s\"\n",
		       type2name(type), fs->fs_devname);
		return 1;
	}
	err = ocfs2_init_fs_quota_info(fs, type);
	if (err) {
		tcom_err(err, "while looking up %s quota file on device "
			 "\"%s\"", type2name(type), fs->fs_devname);
		return 1;
	}
	err = ocfs2_read_global_quota_info(fs, type);
	if (err) {
		tcom_err(err, "while reading %s quota info on device \"%s\"",
			 type2name(type), fs->fs_devname);
		return 1;
	}
	qinfo = &fs->qinfo[type].qi_info;
	if (qinfo->dqi_syncms == syncms) {
		verbosef(VL_APP,
			 "Device \"%s\" already has interval %lu set; "
			 "nothing to do\n", fs->fs_devname, syncms);
		return 0;
	}

	if (!tools_interact("Change quota syncing interval on device \"%s\" "
			    "from %lu to %lu? ", fs->fs_devname,
			    (unsigned long)qinfo->dqi_syncms, syncms))
		return 0;

	prog = tools_progress_start("Setting syncing interval", "interval", 1);
	if (!prog) {
		tcom_err(err, "while initializing the progress display");
		return 1;
	}

	tunefs_block_signals();
	qinfo->dqi_syncms = syncms;
	err = ocfs2_write_global_quota_info(fs, type);
	tunefs_unblock_signals();

	tools_progress_step(prog, 1);
	tools_progress_stop(prog);

	if (err) {
		tcom_err(err,
			 "- unable to update %s quota syncing interval on "
			 "device \"%s\"", type2name(type), fs->fs_devname);
		return 1;
	}

	return 0;
}

static int set_quota_sync_interval_parse_option(struct tunefs_operation *op,
						char *arg)
{
	int rc = 1;
	unsigned long interval;
	char *ptr;

	if (!arg) {
		errorf("No interval specified\n");
		goto out;
	}

	interval = strtoul(arg, &ptr, 10);
	if (*ptr != 0) {
		errorf("Invalid number: %s", arg);
		goto out;
	}

	if (interval < 100 || interval == ULONG_MAX ||
	    interval > ~(uint32_t)0) {
		errorf("Quota sync interval is out of range (minimum is 100,"
		       " maximum is 4294967295): %s\n", arg);
		goto out;
	}

	op->to_private = (void *)interval;
	rc = 0;
out:
	return rc;
}

static int set_usrquota_sync_interval_run(struct tunefs_operation *op,
					  ocfs2_filesys *fs,
					  int flags)
{
	return update_sync_interval(fs, USRQUOTA,
				    (unsigned long)op->to_private);
}

static int set_grpquota_sync_interval_run(struct tunefs_operation *op,
					  ocfs2_filesys *fs,
					  int flags)
{
	return update_sync_interval(fs, GRPQUOTA,
				    (unsigned long)op->to_private);
}


DEFINE_TUNEFS_OP(set_usrquota_sync_interval,
		 "Usage: op_set_usrquota_sync_interval [opts] <device> <interval in ms>\n",
		 TUNEFS_FLAG_RW,
		 set_quota_sync_interval_parse_option,
		 set_usrquota_sync_interval_run);

DEFINE_TUNEFS_OP(set_grpquota_sync_interval,
		 "Usage: op_set_grpquota_sync_interval [opts] <device> <interval in ms>\n",
		 TUNEFS_FLAG_RW,
		 set_quota_sync_interval_parse_option,
		 set_grpquota_sync_interval_run);

#ifdef DEBUG_EXE
int main(int argc, char *argv[])
{
	int ret;

	ret = tunefs_op_main(argc, argv, &set_usrquota_sync_interval_op);
	if (ret)
		return ret;
	return tunefs_op_main(argc, argv, &set_grpquota_sync_interval_op);
}
#endif
