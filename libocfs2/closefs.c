/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * closefs.c
 *
 * Close an OCFS2 filesystem.  Part of the OCFS2 userspace library.
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License, version 2,  as published by the Free Software Foundation.
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
 * Ideas taken from e2fsprogs/lib/ext2fs/closefs.c
 *   Copyright (C) 1993, 1994, 1995, 1996 Theodore Ts'o.
 */

#define _XOPEN_SOURCE 600  /* Triggers XOPEN2K in features.h */
#define _LARGEFILE64_SOURCE

#include "ocfs2/ocfs2.h"


errcode_t ocfs2_flush(ocfs2_filesys *fs)
{
	int type;
	errcode_t ret;

	for (type = 0; type < MAXQUOTAS; type++)
		if (fs->qinfo[type].flags & OCFS2_QF_INFO_DIRTY) {
			ret = ocfs2_write_global_quota_info(fs, type);
			if (ret)
				return ret;
			ret = ocfs2_write_cached_inode(fs,
						fs->qinfo[type].qi_inode);
			if (ret)
				return ret;
		}

	return 0;
}

errcode_t ocfs2_close(ocfs2_filesys *fs)
{
	errcode_t ret;

	if (fs->fs_flags & OCFS2_FLAG_DIRTY) {
		ret = ocfs2_flush(fs);
		if (ret)
			return ret;
	}

	ocfs2_freefs(fs);
	return 0;
}
