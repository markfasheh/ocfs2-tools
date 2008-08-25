/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * op_set_journal_size.c
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


static int set_journal_size_parse_option(struct tunefs_operation *op,
					 char *arg)
{
	int rc = 1;
	errcode_t err;
	uint64_t *new_size;


	if (!arg) {
		errorf("No size specified\n");
		goto out;
	}

	err = ocfs2_malloc0(sizeof(uint64_t), &new_size);
	if (err) {
		tcom_err(err, "while processing journal options");
		goto out;
	}

	err = tunefs_get_number(arg, new_size);
	if (!err) {
		op->to_private = new_size;
		rc = 0;
	} else {
		ocfs2_free(&new_size);
		tcom_err(err, "- journal size is invalid\n");
	}

out:
	return rc;
}

static int set_journal_size_run(struct tunefs_operation *op,
				ocfs2_filesys *fs, int flags)
{
	errcode_t err;
	int rc = 0;
	uint64_t *argp = (uint64_t *)op->to_private;
	uint64_t new_size = *argp;

	ocfs2_free(&argp);
	op->to_private = NULL;

	if (!tools_interact("Resize journals on device \"%s\" to "
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


DEFINE_TUNEFS_OP(set_journal_size,
		 "Usage: op_set_journal_size [opts] <device> <size>\n",
		 TUNEFS_FLAG_RW | TUNEFS_FLAG_ALLOCATION,
		 set_journal_size_parse_option,
		 set_journal_size_run);

#ifdef DEBUG_EXE
int main(int argc, char *argv[])
{
	return tunefs_op_main(argc, argv, &set_journal_size_op);
}
#endif
