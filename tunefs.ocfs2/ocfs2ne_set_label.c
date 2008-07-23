/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * ocfs2ne_set_label.c
 *
 * ocfs2 tune utility for updating the volume label.
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


static errcode_t update_volume_label(ocfs2_filesys *fs, const char *label)
{
	errcode_t err;
	int len = strlen(label);

	if (len > OCFS2_MAX_VOL_LABEL_LEN)
		len = OCFS2_MAX_VOL_LABEL_LEN;
	if (!memcmp(OCFS2_RAW_SB(fs->fs_super)->s_label, label, len)) {
		verbosef(VL_APP,
			 "Device \"%s\" already has the label \"%.*s\"; "
			 "nothing to do\n",
			 fs->fs_devname, OCFS2_MAX_VOL_LABEL_LEN, label);
		return 0;
	}

	if (!tunefs_interact("Change the label on device \"%s\" from "
			     "\"%.*s\" to \"%.*s\"? ",
			     fs->fs_devname, OCFS2_MAX_VOL_LABEL_LEN,
			     OCFS2_RAW_SB(fs->fs_super)->s_label,
			     OCFS2_MAX_VOL_LABEL_LEN, label))
		return 0;

	memset(OCFS2_RAW_SB(fs->fs_super)->s_label, 0,
	       OCFS2_MAX_VOL_LABEL_LEN);
	strncpy((char *)OCFS2_RAW_SB(fs->fs_super)->s_label, label,
		OCFS2_MAX_VOL_LABEL_LEN);

	tunefs_block_signals();
	err = ocfs2_write_super(fs);
	tunefs_unblock_signals();

	return err;
}

static int set_label_parse_option(char *arg, void *user_data)
{
	int rc = 0;
	char **new_label = user_data;

	if (arg)
		*new_label = arg;
	else {
		errorf("No label specified\n");
		rc = 1;
	}

	return rc;
}

static int set_label_run(ocfs2_filesys *fs, int flags, void *user_data)
{
	errcode_t err;
	int rc = 0;
	char *new_label = *(char **)user_data;

	err = update_volume_label(fs, new_label);
	if (err) {
		tcom_err(err,
			 "- unable to update the label on device \"%s\"",
			 fs->fs_devname);
		rc = 1;
	}

	return rc;
}


static char *new_label;
DEFINE_TUNEFS_OP(set_label,
		 "Usage: ocfs2ne_set_label [opts] <device> <label>\n",
		 TUNEFS_FLAG_RW,
		 set_label_parse_option,
		 set_label_run,
		 &new_label);

#ifdef DEBUG_EXE
int main(int argc, char *argv[])
{
	return tunefs_op_main(argc, argv, &set_label_op);
}
#endif
