/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * newdir.c
 *
 * Create a new directory block
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
 *  This code is a port of e2fsprogs/lib/ext2fs/newdir.c
 *  Copyright (C) 1994, 1995 Theodore Ts'o.
 */

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE

#include <stdio.h>
#include <string.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "ocfs2/ocfs2.h"

/*
 * Create new directory block
 */
errcode_t ocfs2_new_dir_block(ocfs2_filesys *fs, uint64_t dir_ino,
			      uint64_t parent_ino, char **block)
{
	struct ocfs2_dir_entry 	*dir;
	errcode_t		ret;
	char			*buf;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	memset(buf, 0, fs->fs_blocksize);

	dir = (struct ocfs2_dir_entry *) buf;
	dir->rec_len = fs->fs_blocksize;

	if (dir_ino) {
		/*
		 * Set up entry for '.'
		 */
		dir->inode = dir_ino;
		dir->rec_len = OCFS2_DIR_REC_LEN(1);
		dir->name_len = 1;
		dir->file_type = OCFS2_FT_DIR;
		dir->name[0] = '.';

		/*
		 * Set up entry for '..'
		 */
		dir = (struct ocfs2_dir_entry *) (buf + dir->rec_len);
		dir->inode = parent_ino;
		dir->rec_len = fs->fs_blocksize - OCFS2_DIR_REC_LEN(1);
		dir->name_len = 2;
		dir->file_type = OCFS2_FT_DIR;
		dir->name[0] = '.';
		dir->name[1] = '.';
	}

	*block = buf;
	return 0;
}
