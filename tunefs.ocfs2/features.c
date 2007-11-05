/*
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
#include <ocfs2.h>

#include <assert.h>

#include <tunefs.h>

extern ocfs2_tune_opts opts;

/*
 * Check whether we can add or remove a feature.
 *
 * Currently, we only handle "sparse files".
 * More feature check may be added if we want to
 * support more options in tunefs.ocfs2.
 */
errcode_t feature_check(ocfs2_filesys *fs)
{
	errcode_t ret = 1;

	if (opts.set_feature.ro_compat != 0 ||
	    opts.set_feature.compat != 0 ||
	    opts.clear_feature.ro_compat != 0 ||
	    opts.clear_feature.compat != 0)
		goto bail;

	if ((opts.set_feature.incompat ==
	     OCFS2_FEATURE_INCOMPAT_SPARSE_ALLOC) &&
	     !ocfs2_sparse_alloc(OCFS2_RAW_SB(fs->fs_super)))
		ret = 0;
	else if (opts.clear_feature.incompat ==
		 OCFS2_FEATURE_INCOMPAT_SPARSE_ALLOC) {
		if (!ocfs2_sparse_alloc(OCFS2_RAW_SB(fs->fs_super)))
			goto bail;

		ret = clear_sparse_file_check(fs, opts.progname);
		if (ret)
			goto bail;
	}

bail:
	return ret;
}

errcode_t update_feature(ocfs2_filesys *fs)
{
	errcode_t ret = 0;

	if (opts.set_feature.incompat & OCFS2_FEATURE_INCOMPAT_SPARSE_ALLOC)
		ret = set_sparse_file_flag(fs, opts.progname);
	else if (opts.clear_feature.incompat
		 & OCFS2_FEATURE_INCOMPAT_SPARSE_ALLOC)
		ret = clear_sparse_file_flag(fs, opts.progname);

	return ret;
}
