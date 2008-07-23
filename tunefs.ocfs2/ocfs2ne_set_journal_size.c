/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * ocfs2ne_set_journal_size.c
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
#include "libocfs2ne_err.h"


static int set_journal_size_parse_option(char *arg, void *user_data)
{
	int rc = 0;
	errcode_t err;
	uint64_t *new_size = user_data;

	if (arg) {
		err = tunefs_get_number(arg, new_size);
		if (err) {
			tcom_err(err, "- journal size is invalid\n");
			rc = 1;
		}
	} else {
		errorf("No size specified\n");
		rc = 1;
	}

	return rc;
}

static int set_journal_size_run(ocfs2_filesys *fs, int flags, void *user_data)
{
	errcode_t err;
	int rc = 0;
	uint64_t new_size = *(uint64_t *)user_data;

	if (!tunefs_interact("Resize journals on device \"%s\" to "
			     "%"PRIu64"? ",
			     fs->fs_devname, new_size))
		goto out;

	tunefs_block_signals();
	err = tunefs_set_journal_size(fs, new_size);
	tunefs_unblock_signals();
	if (err) {
		rc = 1;
		tcom_err(err,
			 "- unable to resize the journals on device \"%s\"",
			 fs->fs_devname);
	}

out:
	return rc;
}


static uint64_t new_size;
DEFINE_TUNEFS_OP(set_journal_size,
		 "Usage: ocfs2ne_set_journal_size [opts] <device> <size>\n",
		 TUNEFS_FLAG_RW | TUNEFS_FLAG_ALLOCATION,
		 set_journal_size_parse_option,
		 set_journal_size_run,
		 &new_size);

#ifdef DEBUG_EXE
int main(int argc, char *argv[])
{
	return tunefs_op_main(argc, argv, &set_journal_size_op);
}
#endif
