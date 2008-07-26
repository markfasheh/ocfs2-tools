/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * ocfs2ne_feature_extended_slotmap.c
 *
 * ocfs2 tune utility to enable and disable the extended slot map feature.
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


static int enable_extended_slotmap(ocfs2_filesys *fs, int flags)
{
	errcode_t err = 0;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);

	if (ocfs2_uses_extended_slot_map(super)) {
		verbosef(VL_APP,
			 "Extended slot map feature is already enabled; "
			 "nothing to enable\n");
		goto out;
	}

	if (!tunefs_interact("Enable the extended slot map feature on "
			     "device \"%s\"? ",
			     fs->fs_devname))
		goto out;

	OCFS2_SET_INCOMPAT_FEATURE(super,
				   OCFS2_FEATURE_INCOMPAT_EXTENDED_SLOT_MAP);
	tunefs_block_signals();
	err = ocfs2_format_slot_map(fs);
	if (!err) {
		err = ocfs2_write_super(fs);
		if (err)
			tcom_err(err, "while writing out the superblock");
	} else
		tcom_err(err, "while formatting the extended slot map");
	tunefs_unblock_signals();

out:
	return err;
}

static int disable_extended_slotmap(ocfs2_filesys *fs, int flags)
{
	errcode_t err = 0;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);

	if (!ocfs2_uses_extended_slot_map(super)) {
		verbosef(VL_APP,
			 "Extended slot map feature is not enabled; "
			 "nothing to disable\n");
		goto out;
	}

	if (!tunefs_interact("Disable the extended slot map feature on "
			     "device \"%s\"? ",
			     fs->fs_devname))
		goto out;

	OCFS2_CLEAR_INCOMPAT_FEATURE(super,
				     OCFS2_FEATURE_INCOMPAT_EXTENDED_SLOT_MAP);

	tunefs_block_signals();
	err = ocfs2_format_slot_map(fs);
	if (!err) {
		err = ocfs2_write_super(fs);
		if (err)
			tcom_err(err, "while writing out the superblock");
	} else
		tcom_err(err, "while formatting the old-style slot map");
	tunefs_unblock_signals();

out:
	return err;
}

DEFINE_TUNEFS_FEATURE_INCOMPAT(extended_slotmap,
			       OCFS2_FEATURE_INCOMPAT_EXTENDED_SLOT_MAP,
			       TUNEFS_FLAG_RW | TUNEFS_FLAG_ALLOCATION,
			       enable_extended_slotmap,
			       disable_extended_slotmap);

#ifdef DEBUG_EXE
int main(int argc, char *argv[])
{
	return tunefs_feature_main(argc, argv, &extended_slotmap_feature);
}
#endif
