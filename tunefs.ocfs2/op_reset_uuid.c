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

/*
 *	Translate 32 bytes uuid to 36 bytes uuid format.
 *	for example:
 *	32 bytes uuid: 178BDC83D50241EF94EB474A677D498B
 *	36 bytes uuid: 178BDC83-D502-41EF-94EB-474A677D498B
 */
static void translate_uuid(char *uuid_32, char *uuid_36)
{
	int i;
	char *cp = uuid_32;

	for (i = 0; i < 36; i++) {
		if ((i == 8) || (i == 13) || (i == 18) || (i == 23)) {
			uuid_36[i] = '-';
			continue;
		}
		uuid_36[i] = *cp++;
	}
}

static errcode_t update_volume_uuid(ocfs2_filesys *fs, char *uuid)
{
	errcode_t err;
	unsigned char new_uuid[OCFS2_VOL_UUID_LEN];
	struct tools_progress *prog;
	char uuid_36[37] = {'\0'};
	char *uuid_p;

	if (!tools_interact("Reset the volume UUID on device \"%s\"? ",
			    fs->fs_devname))
		return 0;

	if (uuid != NULL) {
		if (!tools_interact_critical(
			"WARNING!!! OCFS2 uses the UUID to uniquely identify "
			"a file system. Having two OCFS2 file systems with "
			"the same UUID could, in the least, cause erratic "
			"behavior, and if unlucky, cause file system damage. "
			"Please choose the UUID with care.\n"
			"Update the UUID ?"))
			return 0;
	}

	prog = tools_progress_start("Resetting UUID", "resetuuid", 1);
	if (!prog)
		return TUNEFS_ET_NO_MEMORY;

	if (uuid == NULL)
		uuid_generate(new_uuid);
	else {
		if (strlen(uuid) == 32) {
			/*translate 32 bytes uuid to 36 bytes uuid*/
			translate_uuid(uuid, uuid_36);
			uuid_p = uuid_36;
		} else
			uuid_p = uuid;
		/*uuid_parse only support 36 bytes uuid*/
		if (uuid_parse(uuid_p, new_uuid))
			return TUNEFS_ET_OPERATION_FAILED;
	}
	memcpy(OCFS2_RAW_SB(fs->fs_super)->s_uuid, new_uuid,
	       OCFS2_VOL_UUID_LEN);

	tunefs_block_signals();
	err = ocfs2_write_super(fs);
	tunefs_unblock_signals();

	tools_progress_step(prog, 1);
	tools_progress_stop(prog);
	return err;
}

static int reset_uuid_parse_option(struct tunefs_operation *op, char *arg)
{
	int i, len;
	char *cp = arg;
	unsigned char new_uuid[OCFS2_VOL_UUID_LEN];

	if (arg == NULL)
		goto out;

	len = strlen(cp);
	/*check the uuid length, we support 32/36 bytes uuid format*/
	if (len != 32 && len != 36)
		goto bail;

	if (len == 36) {
		if (uuid_parse(cp, new_uuid))
			goto bail;
		goto out;
	}

	/*check whether each character of uuid is a hexadecimal digit*/
	for (i = 0; i < len; i++, cp++) {
		if (!isxdigit(*cp))
			goto bail;
	}
out:
	op->to_private = arg;
	return 0;
bail:
	errorf("Invalid UUID\n");
	return 1;
}

static int reset_uuid_run(struct tunefs_operation *op, ocfs2_filesys *fs,
			  int flags)
{
	int rc = 0;
	errcode_t err;

	err = update_volume_uuid(fs, op->to_private);
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
		 reset_uuid_parse_option,
		 reset_uuid_run);

#ifdef DEBUG_EXE
int main(int argc, char *argv[])
{
	return tunefs_op_main(argc, argv, &reset_uuid_op);
}
#endif
