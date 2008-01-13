/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * features.c
 *
 * source file for adding or removing features for tunefs.
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
#include <ocfs2/ocfs2.h>

#include <assert.h>

#include <tunefs.h>

extern ocfs2_tune_opts opts;

#define TUNEFS_COMPAT_SET	0
#define TUNEFS_COMPAT_CLEAR	0
#define TUNEFS_RO_COMPAT_SET	OCFS2_FEATURE_RO_COMPAT_UNWRITTEN
#define TUNEFS_RO_COMPAT_CLEAR	OCFS2_FEATURE_RO_COMPAT_UNWRITTEN
#define TUNEFS_INCOMPAT_SET	(OCFS2_FEATURE_INCOMPAT_SPARSE_ALLOC | \
				 OCFS2_FEATURE_INCOMPAT_EXTENDED_SLOT_MAP)
#define TUNEFS_INCOMPAT_CLEAR	(OCFS2_FEATURE_INCOMPAT_SPARSE_ALLOC | \
				 OCFS2_FEATURE_INCOMPAT_EXTENDED_SLOT_MAP)

/*
 * Check whether we can add or remove a feature.
 *
 * Features which can be SET or CLEARed are represented in the TUNEFS
 * bitfields above.
 * More feature check may be added if we want to
 * support more options in tunefs.ocfs2.
 */
errcode_t feature_check(ocfs2_filesys *fs)
{
	errcode_t ret = 1;
	int sparse_on = ocfs2_sparse_alloc(OCFS2_RAW_SB(fs->fs_super));

	if (opts.set_feature.compat & ~TUNEFS_COMPAT_SET ||
	    opts.set_feature.ro_compat & ~TUNEFS_RO_COMPAT_SET ||
	    opts.set_feature.incompat & ~TUNEFS_INCOMPAT_SET)
		goto bail;

	if (opts.clear_feature.compat & ~TUNEFS_COMPAT_CLEAR ||
	    opts.clear_feature.ro_compat & ~TUNEFS_RO_COMPAT_CLEAR ||
	    opts.clear_feature.incompat & ~TUNEFS_INCOMPAT_CLEAR)
		goto bail;

	if (opts.set_feature.incompat & OCFS2_FEATURE_INCOMPAT_SPARSE_ALLOC) {
		/*
		 * Allow sparse to pass on an already-sparse file
		 * system if the user asked for unwritten extents.
		 */
		if (ocfs2_sparse_alloc(OCFS2_RAW_SB(fs->fs_super)) && 
		    !(opts.set_feature.ro_compat & OCFS2_FEATURE_RO_COMPAT_UNWRITTEN))
			goto bail;

		sparse_on = 1;
	} else if (opts.clear_feature.incompat & OCFS2_FEATURE_INCOMPAT_SPARSE_ALLOC) {
		if (!ocfs2_sparse_alloc(OCFS2_RAW_SB(fs->fs_super)))
			goto bail;

		/*
		 * Turning off sparse files means we must also turn
		 * off unwritten extents.
		 */
		if (ocfs2_writes_unwritten_extents(OCFS2_RAW_SB(fs->fs_super)))
			opts.clear_feature.ro_compat |= OCFS2_FEATURE_RO_COMPAT_UNWRITTEN;

		sparse_on = 0;
		ret = clear_sparse_file_check(fs, opts.progname, 0);
		if (ret)
			goto bail;
	}

	if (opts.set_feature.ro_compat & OCFS2_FEATURE_RO_COMPAT_UNWRITTEN) {
		/*
		 * Disallow setting of unwritten extents unless we
		 * either have sparse file support, or will also be
		 * turning it on.
		 */
		if (!sparse_on)
			goto bail;

		/*
		 * We can't use the helper here because it checks for
		 * the sparse flag.
		 */
		if (OCFS2_HAS_RO_COMPAT_FEATURE(OCFS2_RAW_SB(fs->fs_super),
					OCFS2_FEATURE_RO_COMPAT_UNWRITTEN))
		    goto bail;
	} else if (opts.clear_feature.ro_compat &
		   OCFS2_FEATURE_RO_COMPAT_UNWRITTEN) {
		if (!ocfs2_writes_unwritten_extents(OCFS2_RAW_SB(fs->fs_super)))
			goto bail;

		if (sparse_on) {
			/*
			 * If we haven't run through the file system
			 * yet, do it now in order to build up our
			 * list of files with unwritten extents.
			 */
			ret = clear_sparse_file_check(fs, opts.progname, 1);
			if (ret)
				goto bail;
		}
	}

	ret = 0;
bail:
	return ret;
}

errcode_t update_feature(ocfs2_filesys *fs)
{
	errcode_t ret = 0;

	if (opts.set_feature.incompat & OCFS2_FEATURE_INCOMPAT_SPARSE_ALLOC)
		ret = set_sparse_file_flag(fs, opts.progname);
	else if (opts.clear_feature.incompat
		 & OCFS2_FEATURE_INCOMPAT_SPARSE_ALLOC ||
		 opts.clear_feature.ro_compat
		 & OCFS2_FEATURE_RO_COMPAT_UNWRITTEN)
		ret = clear_sparse_file_flag(fs, opts.progname);
	if (ret)
		goto bail;

	if (opts.set_feature.ro_compat & OCFS2_FEATURE_RO_COMPAT_UNWRITTEN)
		set_unwritten_extents_flag(fs);

	if ((opts.set_feature.incompat | opts.clear_feature.incompat) &
	    OCFS2_FEATURE_INCOMPAT_EXTENDED_SLOT_MAP)
		ret = reformat_slot_map(fs);

bail:
	return ret;
}
