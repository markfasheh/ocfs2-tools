/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * sysfile.c
 *
 * System inode operations for the OCFS2 userspace library.
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301 USA.
 */

#define _XOPEN_SOURCE 600  /* Triggers XOPEN2K in features.h */
#define _LARGEFILE64_SOURCE
#define _DEFAULT_SOURCE

#include <string.h>

#include "ocfs2/ocfs2.h"

errcode_t ocfs2_lookup_system_inode(ocfs2_filesys *fs, int type,
				    int slot_num, uint64_t *blkno)
{
	errcode_t ret;
	char *buf;

	ret = ocfs2_malloc0(sizeof(char) * (OCFS2_MAX_FILENAME_LEN + 1), &buf);
	if (ret)
		return ret;

	ocfs2_sprintf_system_inode_name(buf, OCFS2_MAX_FILENAME_LEN, 
					type, slot_num);

	ret = ocfs2_lookup(fs, fs->fs_sysdir_blkno, buf,
			   strlen(buf), NULL, blkno);

	ocfs2_free(&buf);

	return ret;
}
