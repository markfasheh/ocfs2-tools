/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * unlink.c
 *
 * Remove an entry from an OCFS2 directory.  For the OCFS2 userspace
 * library.
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
 * Authors: Joel Becker
 *
 *  This code is a port of e2fsprogs/lib/ext2fs/unlink.c
 *  Copyright (C) 1993, 1994, 1997 Theodore Ts'o.
 */

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>
#include <time.h>

#include <linux/types.h>

#include <et/com_err.h>
#include "ocfs2_err.h"

#include "unix_io.h"
#include "memory.h"
#include "byteorder.h"

#include "ocfs2_fs.h"

#include "filesys.h"


struct link_struct  {
	const char	*name;
	int		namelen;
	uint64_t	inode;
	int		flags;
	int		done;
};	

#ifdef __TURBOC__
 #pragma argsused
#endif
static int unlink_proc(struct ocfs2_dir_entry *dirent,
		     int	offset,
		     int	blocksize,
		     char	*buf,
		     void	*priv_data)
{
	struct link_struct *ls = (struct link_struct *) priv_data;

	if (ls->name && ((dirent->name_len & 0xFF) != ls->namelen))
		return 0;
	if (ls->name && strncmp(ls->name, dirent->name,
				dirent->name_len & 0xFF))
		return 0;
	if (ls->inode && (dirent->inode != ls->inode))
		return 0;

	dirent->inode = 0;
	ls->done++;
	return OCFS2_DIRENT_ABORT|OCFS2_DIRENT_CHANGED;
}

#ifdef __TURBOC__
 #pragma argsused
#endif
errcode_t ocfs2_unlink(ocfs2_filesys *fs, uint64_t dir,
			const char *name, uint64_t ino,
			int flags)
{
	errcode_t	retval;
	struct link_struct ls;

	if (!(fs->fs_flags & OCFS2_FLAG_RW))
		return OCFS2_ET_RO_FILESYS;

	ls.name = name;
	ls.namelen = name ? strlen(name) : 0;
	ls.inode = ino;
	ls.flags = 0;
	ls.done = 0;

	retval = ocfs2_dir_iterate(fs, dir, 0, 0, unlink_proc, &ls);
	if (retval)
		return retval;

	return (ls.done) ? 0 : OCFS2_ET_DIR_NO_SPACE;
}

