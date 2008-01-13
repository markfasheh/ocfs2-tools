/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * format_slotmap.c
 *
 * Switch between slot map formats.
 *
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 */
#include "ocfs2/ocfs2.h"

#include <assert.h>

#include <tunefs.h>

extern ocfs2_tune_opts opts;

errcode_t reformat_slot_map(ocfs2_filesys *fs)
{
	errcode_t ret = 0;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);
	int extended = ocfs2_uses_extended_slot_map(super);

	if (opts.set_feature.incompat &
	    OCFS2_FEATURE_INCOMPAT_EXTENDED_SLOT_MAP) {
		if (extended) {
			printf("Feature \"extended-slotmap\" is already enabled, skipping\n");
			goto out;
		}
		OCFS2_SET_INCOMPAT_FEATURE(super, OCFS2_FEATURE_INCOMPAT_EXTENDED_SLOT_MAP);
	} else if (opts.clear_feature.incompat &
		   OCFS2_FEATURE_INCOMPAT_EXTENDED_SLOT_MAP) {
		if (!extended) {
			printf("Feature \"extended-slotmap\" is not enabled, skipping\n");
			goto out;
		}
		OCFS2_CLEAR_INCOMPAT_FEATURE(super, OCFS2_FEATURE_INCOMPAT_EXTENDED_SLOT_MAP);
	}

	ret = ocfs2_format_slot_map(fs);

out:
	return ret;
}
