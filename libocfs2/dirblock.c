/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dirblock.c
 *
 * Directory block routines for the OCFS2 userspace library.
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
 *  This code is a port of e2fsprogs/lib/ext2fs/dirblock.c
 *  Copyright (C) 1995, 1996 Theodore Ts'o.
 */

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE

#include <string.h>

#include "ocfs2.h"


errcode_t ocfs2_read_dir_block(ocfs2_filesys *fs, uint64_t block,
                               void *buf)
{
	errcode_t	retval;
	char		*p, *end;
	struct ocfs2_dir_entry *dirent;
	unsigned int	name_len, rec_len;

 	retval = io_read_block(fs->fs_io, block, 1, buf);
	if (retval)
		return retval;

	p = (char *) buf;
	end = (char *) buf + fs->fs_blocksize;
	while (p < end-12) {
		dirent = (struct ocfs2_dir_entry *) p;

		dirent->inode = le64_to_cpu(dirent->inode);
		dirent->rec_len = le16_to_cpu(dirent->rec_len);
		name_len = dirent->name_len;
		rec_len = dirent->rec_len;

		if ((rec_len < 12) || (rec_len % 4)) {
			rec_len = 12;
			retval = OCFS2_ET_DIR_CORRUPTED;
		}

		if (((name_len & 0xFF) + 12) > dirent->rec_len)
			retval = OCFS2_ET_DIR_CORRUPTED;

		p += rec_len;
	}
	return retval;
}

errcode_t ocfs2_write_dir_block(ocfs2_filesys *fs, uint64_t block,
                                void *inbuf)
{
	errcode_t	retval;
	char		*p, *end;
	char		*buf = NULL;
	struct ocfs2_dir_entry *dirent;

	retval = ocfs2_malloc_block(fs->fs_io, &buf);
	if (retval)
		return retval;

	memcpy(buf, inbuf, fs->fs_blocksize);

	p = buf;
	end = buf + fs->fs_blocksize;
	while (p < end-12) {
		dirent = (struct ocfs2_dir_entry *) p;

		if ((dirent->rec_len < 12) ||
		    (dirent->rec_len % 4)) {
			ocfs2_free(&buf);
			return OCFS2_ET_DIR_CORRUPTED;
		}

		dirent->inode = cpu_to_le64(dirent->inode);
		dirent->rec_len = cpu_to_le16(dirent->rec_len);

		p += dirent->rec_len;
	}

 	retval = io_write_block(fs->fs_io, block, 1, buf);
	ocfs2_free(&buf);
	return retval;
}
