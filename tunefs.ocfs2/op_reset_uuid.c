/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * op_reset_uuid.c
 *
 * ocfs2 tune utility to reset the volume UUID.
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
#include <uuid/uuid.h>

#include "ocfs2/ocfs2.h"

#include "libocfs2ne.h"


static errcode_t update_volume_uuid(ocfs2_filesys *fs)
{
	errcode_t err;
	unsigned char new_uuid[OCFS2_VOL_UUID_LEN];

	if (!tunefs_interact("Reset the volume UUID on device \"%s\"? ",
			     fs->fs_devname))
		return 0;

	uuid_generate(new_uuid);
	memcpy(OCFS2_RAW_SB(fs->fs_super)->s_uuid, new_uuid,
	       OCFS2_VOL_UUID_LEN);

	tunefs_block_signals();
	err = ocfs2_write_super(fs);
	tunefs_unblock_signals();

	return err;
}

static int reset_uuid_run(struct tunefs_operation *op, ocfs2_filesys *fs,
			  int flags)
{
	int rc = 0;
	errcode_t err;

	err = update_volume_uuid(fs);
	if (err) {
		tcom_err(err, "- unable to reset the uuid on device \"%s\"",
			 fs->fs_devname);
		rc = 1;
	}

	return rc;
}


DEFINE_TUNEFS_OP(reset_uuid,
		 "Usage: op_reset_uuid [opts] <device>\n",
		 TUNEFS_FLAG_RW,
		 NULL,
		 reset_uuid_run);

#ifdef DEBUG_EXE
int main(int argc, char *argv[])
{
	return tunefs_op_main(argc, argv, &reset_uuid_op);
}
#endif
