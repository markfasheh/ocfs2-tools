/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * op_cloned_volume.c
 *
 * ocfs2 tune utility to change the uuid and label of cloned volumes.
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

#define _GNU_SOURCE  /* For strnlen() */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include <uuid/uuid.h>

#include "ocfs2/ocfs2.h"

#include "libocfs2ne.h"


#define CLONED_LABEL "-cloned"
#define CLONED_LABEL_LEN (strlen(CLONED_LABEL))

static void update_volume_label(ocfs2_filesys *fs,
				const char *new_label)
{
	int len;
	char label_buf[OCFS2_MAX_VOL_LABEL_LEN];
	struct ocfs2_super_block *sb = OCFS2_RAW_SB(fs->fs_super);

	memset(label_buf, 0, OCFS2_MAX_VOL_LABEL_LEN);

	if (new_label) {
		len = strlen(new_label);
		if (len > OCFS2_MAX_VOL_LABEL_LEN)
			len = OCFS2_MAX_VOL_LABEL_LEN;
		if (!memcmp(sb->s_label, new_label, len)) {
			verbosef(VL_APP,
				 "Device \"%s\" already has the label "
				 "\"%.*s\"; label not updated\n",
				 fs->fs_devname, OCFS2_MAX_VOL_LABEL_LEN,
				 new_label);
			return;
		}
		strncpy(label_buf, new_label, OCFS2_MAX_VOL_LABEL_LEN);
	} else {
		strncpy(label_buf, (char *)sb->s_label,
			OCFS2_MAX_VOL_LABEL_LEN);
		len = strnlen(label_buf, OCFS2_MAX_VOL_LABEL_LEN);

		/*
		 * If we don't have CLONED_LABEL at the end of the label,
		 * add it.  Truncate the existing label if necessary to
		 * fit CLONED_LABEL.
		 */
		if (strncmp(label_buf + len - CLONED_LABEL_LEN,
			    CLONED_LABEL, CLONED_LABEL_LEN)) {
			if ((len + CLONED_LABEL_LEN) > OCFS2_MAX_VOL_LABEL_LEN)
				len = OCFS2_MAX_VOL_LABEL_LEN -
					CLONED_LABEL_LEN;
			strncpy(label_buf + len, CLONED_LABEL,
				CLONED_LABEL_LEN);
		}
	}

	verbosef(VL_APP,
		 "Setting the label \"%.*s\" on device \"%s\"\n",
		 OCFS2_MAX_VOL_LABEL_LEN, label_buf, fs->fs_devname);
	memset(OCFS2_RAW_SB(fs->fs_super)->s_label, 0,
	       OCFS2_MAX_VOL_LABEL_LEN);
	strncpy((char *)sb->s_label, label_buf,
		OCFS2_MAX_VOL_LABEL_LEN);
}

static void update_volume_uuid(ocfs2_filesys *fs)
{
	unsigned char new_uuid[OCFS2_VOL_UUID_LEN];

	uuid_generate(new_uuid);
	memcpy(OCFS2_RAW_SB(fs->fs_super)->s_uuid, new_uuid,
	       OCFS2_VOL_UUID_LEN);
}

static errcode_t cloned_volume(ocfs2_filesys *fs, const char *new_label)
{
	errcode_t err;

	if (!tools_interact_critical(
		"Updating the UUID and label on cloned volume \"%s\".\n"
		"DANGER: THIS WILL MODIFY THE UUID WITHOUT ACCESSING THE "
		"CLUSTER SOFTWARE.  YOU MUST BE ABSOLUTELY SURE THAT NO "
		"OTHER NODE IS USING THIS FILESYSTEM BEFORE MODIFYING "
		"ITS UUID.\n"
	        "Update the UUID and label? ",
		fs->fs_devname))
		return 0;

	update_volume_uuid(fs);
	update_volume_label(fs, new_label);

	tunefs_block_signals();
	err = ocfs2_write_super(fs);
	tunefs_unblock_signals();

	return err;
}

static int cloned_volume_run(struct tunefs_operation *op, ocfs2_filesys *fs,
			     int flags)
{
	errcode_t err;
	int rc = 0;
	char *new_label = op->to_private;

	err = cloned_volume(fs, new_label);
	if (err) {
		tcom_err(err,
			 "- unable to update the uuid and label on "
			 "device \"%s\"",
			 fs->fs_devname);
		rc = 1;
	}

	return rc;
}


DEFINE_TUNEFS_OP(cloned_volume,
		 "Usage: cloned_volume [opts] <device> [<label>]\n",
		 TUNEFS_FLAG_RW | TUNEFS_FLAG_SKIPCLUSTER,
		 NULL,
		 cloned_volume_run);

#ifdef DEBUG_EXE
int main(int argc, char *argv[])
{
	return tunefs_op_main(argc, argv, &cloned_volume_op);
}
#endif
