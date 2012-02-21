/* -*- mode: c; c-basic-offset: 8; -*-
* vim: noexpandtab sw=8 ts=8 sts=0:
*
* op_set_journal_block.c
*
* ocfs2 tune utility for updating the block attribute of all journals.
*
* Copyright (C) 2011, 2012 SUSE.  All rights reserved.
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


static int set_journal_block32_run(struct tunefs_operation *op,
				ocfs2_filesys *fs, int flags)
{
	errcode_t err;
	int rc = 0;
	ocfs2_fs_options mask, options;

	memset(&mask, 0, sizeof(ocfs2_fs_options));
	memset(&options, 0, sizeof(ocfs2_fs_options));
	mask.opt_incompat |= JBD2_FEATURE_INCOMPAT_64BIT;

	if (!tools_interact("Enable block32 journal feature on device \"%s\" ?",
			    fs->fs_devname))
		goto out;

	tunefs_block_signals();
	err = tunefs_set_journal_size(fs, 0, mask, options);
	tunefs_unblock_signals();
	if (err) {
		rc = 1;
		tcom_err(err, "; unable to enable block32 journal feature on "
			 "device \"%s\"", fs->fs_devname);
	}

out:
	return rc;
}

static int set_journal_block64_run(struct tunefs_operation *op,
				ocfs2_filesys *fs, int flags)
{
	errcode_t err;
	int rc = 0;
	ocfs2_fs_options mask, options;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);

	memset(&mask, 0, sizeof(ocfs2_fs_options));
	memset(&options, 0, sizeof(ocfs2_fs_options));
	mask.opt_incompat |= JBD2_FEATURE_INCOMPAT_64BIT;
	options.opt_incompat |= JBD2_FEATURE_INCOMPAT_64BIT;

	if (!tools_interact("Enable block64 journal feature on device \"%s\"? ",
			    fs->fs_devname))
		goto out;

	tunefs_block_signals();
	super->s_feature_compat |= OCFS2_FEATURE_COMPAT_JBD2_SB;
	err = ocfs2_write_super(fs);
	if (!err)
		err = tunefs_set_journal_size(fs, 0, mask, options);
	tunefs_unblock_signals();
	if (err) {
		rc = 1;
		tcom_err(err,
			"; unable to enable block64 journal feature on "
			"device \"%s\"", fs->fs_devname);
	}

out:
	return rc;
}


DEFINE_TUNEFS_OP(set_journal_block32,
		"Usage: op_set_journal_block32 <device>\n",
		TUNEFS_FLAG_RW | TUNEFS_FLAG_ALLOCATION,
		0,
		set_journal_block32_run);

DEFINE_TUNEFS_OP(set_journal_block64,
		"Usage: op_set_journal_block64 <device>\n",
		TUNEFS_FLAG_RW | TUNEFS_FLAG_ALLOCATION,
		0,
		set_journal_block64_run);

#ifdef DEBUG_EXE
int main(int argc, char *argv[])
{
	return tunefs_op_main(argc, argv, &set_journal_block32_op);
}
#endif
