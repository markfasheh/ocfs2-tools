/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * op_set_label.c
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


static errcode_t update_volume_label(ocfs2_filesys *fs, const char *label)
{
	errcode_t err;
	int len = strlen(label) + 1;  /* Compare the NUL too */
	struct tools_progress *prog;

	if (len > OCFS2_MAX_VOL_LABEL_LEN)
		len = OCFS2_MAX_VOL_LABEL_LEN;
	if (!memcmp(OCFS2_RAW_SB(fs->fs_super)->s_label, label, len)) {
		verbosef(VL_APP,
			 "Device \"%s\" already has the label \"%.*s\"; "
			 "nothing to do\n",
			 fs->fs_devname, OCFS2_MAX_VOL_LABEL_LEN, label);
		return 0;
	}

	if (!tools_interact("Change the label on device \"%s\" from "
			    "\"%.*s\" to \"%.*s\"? ",
			    fs->fs_devname, OCFS2_MAX_VOL_LABEL_LEN,
			    OCFS2_RAW_SB(fs->fs_super)->s_label,
			    OCFS2_MAX_VOL_LABEL_LEN, label))
		return 0;

	prog = tools_progress_start("Setting label", "label", 1);
	if (!prog) {
		err = TUNEFS_ET_NO_MEMORY;
		tcom_err(err, "while initializing the progress display");
		return err;
	}

	memset(OCFS2_RAW_SB(fs->fs_super)->s_label, 0,
	       OCFS2_MAX_VOL_LABEL_LEN);
	strncpy((char *)OCFS2_RAW_SB(fs->fs_super)->s_label, label,
		OCFS2_MAX_VOL_LABEL_LEN);

	tunefs_block_signals();
	err = ocfs2_write_super(fs);
	tunefs_unblock_signals();

	tools_progress_step(prog, 1);
	tools_progress_stop(prog);

	return err;
}

static int set_label_parse_option(struct tunefs_operation *op, char *arg)
{
	int rc = 0;

	if (arg)
		op->to_private = arg;
	else {
		errorf("No label specified\n");
		rc = 1;
	}

	return rc;
}

static int set_label_run(struct tunefs_operation *op, ocfs2_filesys *fs,
			 int flags)
{
	errcode_t err;
	int rc = 0;
	char *new_label = op->to_private;

	err = update_volume_label(fs, new_label);
	if (err) {
		tcom_err(err,
			 "- unable to update the label on device \"%s\"",
			 fs->fs_devname);
		rc = 1;
	}

	return rc;
}


DEFINE_TUNEFS_OP(set_label,
		 "Usage: op_set_label [opts] <device> <label>\n",
		 TUNEFS_FLAG_RW,
		 set_label_parse_option,
		 set_label_run);

#ifdef DEBUG_EXE
int main(int argc, char *argv[])
{
	return tunefs_op_main(argc, argv, &set_label_op);
}
#endif
