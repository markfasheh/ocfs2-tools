/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * op_set_mmp_update_interval.c
 *
 * ocfs2 tune utility for updating the size of all journals.
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

/*
 * Minimum interval for MMP checking in seconds.
 */
#define OCFS2_MMP_MIN_CHECK_INTERVAL    5UL

/*
 * Maximum interval for MMP checking in seconds.
 */
#define OCFS2_MMP_MAX_CHECK_INTERVAL    300U

static int set_mmp_update_interval_parse_option(
				struct tunefs_operation *op, char *arg)
{
	int rc = 1;
	char *ptr = NULL;
	long new_time;

	if (!arg) {
		errorf("No update interval time specified\n");
		goto out;
	}

	new_time = strtol(arg, &ptr, 10);
	if ((new_time == LONG_MIN) || (new_time == LONG_MAX) || (new_time < 0)) {
		errorf("Number of interval time is out of range: %s\n", arg);
		goto out;
	}
	if (*ptr != '\0') {
		errorf("Invalid number: \"%s\"\n", arg);
		goto out;
	}
	if (new_time > OCFS2_MMP_MAX_CHECK_INTERVAL) {
		errorf("Number of seconds bigger than %d\n", OCFS2_MMP_MAX_CHECK_INTERVAL);
		goto out;
	}
	if (new_time == 0)
		new_time = OCFS2_MMP_MIN_CHECK_INTERVAL;

	op->to_private = (void *)(unsigned long)new_time;
	rc = 0;

out:
	return rc;
}

static errcode_t do_mmp_update_interval(ocfs2_filesys *fs, int new_time)
{
	errcode_t ret = 0;
	int orig_time = OCFS2_RAW_SB(fs->fs_super)->s_mmp_update_interval;

	if (new_time == orig_time) {
		verbosef(VL_APP,
			 "Device \"%s\" already set %d seconds interval time; "
			 "nothing to do\n",
			 fs->fs_devname, new_time);
		goto out;
	}

	if (!tools_interact("Change the time of mmp update interval on device "
			    "\"%s\" from %d to %d? ",
			    fs->fs_devname, orig_time, new_time))
		goto out;

	tunefs_block_signals();
	OCFS2_RAW_SB(fs->fs_super)->s_mmp_update_interval = new_time;
	ret = ocfs2_write_super(fs);
	tunefs_unblock_signals();

out:
	return ret;
}

static int set_mmp_update_interval_run(struct tunefs_operation *op,
				ocfs2_filesys *fs, int flags)
{
	errcode_t err;
	int rc = 0;
	int new_time = (int)(unsigned long)op->to_private;

	err = do_mmp_update_interval(fs, new_time);
	if (err) {
		rc = 1;
		tcom_err(err,
			 "- unable to change the mmp update interval on device \"%s\"",
			 fs->fs_devname);
	}

out:
	return rc;
}


DEFINE_TUNEFS_OP(set_mmp_update_interval,
		 "Usage: op_set_mmp_update_interval [opts] <device> <interval in seconds>\n",
		 TUNEFS_FLAG_RW | TUNEFS_FLAG_NOCLUSTER,
		 set_mmp_update_interval_parse_option,
		 set_mmp_update_interval_run);

#ifdef DEBUG_EXE
int main(int argc, char *argv[])
{
	return tunefs_op_main(argc, argv, &set_mmp_update_interval_op);
}
#endif
