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

#include "ocfs2/byteorder.h"
#include "ocfs2/ocfs2.h"

static void ocfs2_swap_dir_entry(struct ocfs2_dir_entry *dirent)
{
	if (cpu_is_little_endian)
		return;

	dirent->inode = bswap_64(dirent->inode);
	dirent->rec_len = bswap_16(dirent->rec_len);
}

static errcode_t ocfs2_swap_dir_entries_direction(void *buf, uint64_t bytes,
						  int to_cpu)
{
	char		*p, *end;
	struct ocfs2_dir_entry *dirent;
	unsigned int	name_len, rec_len;
	errcode_t retval = 0;

	p = (char *) buf;
	end = (char *) buf + bytes;
	while (p < end-12) {
		dirent = (struct ocfs2_dir_entry *) p;

		if (to_cpu)
			ocfs2_swap_dir_entry(dirent);
		name_len = dirent->name_len;
		rec_len = dirent->rec_len;
		if (!to_cpu)
			ocfs2_swap_dir_entry(dirent);

		if ((rec_len < 12) || (rec_len % 4)) {
			rec_len = 12;
			retval = OCFS2_ET_DIR_CORRUPTED;
		}

		if (((name_len & 0xFF) + 12) > rec_len)
			retval = OCFS2_ET_DIR_CORRUPTED;

		p += rec_len;
	}
	return retval;
}

errcode_t ocfs2_swap_dir_entries_from_cpu(void *buf, uint64_t bytes)
{
	return ocfs2_swap_dir_entries_direction(buf, bytes, 0);
}
errcode_t ocfs2_swap_dir_entries_to_cpu(void *buf, uint64_t bytes)
{
	return ocfs2_swap_dir_entries_direction(buf, bytes, 1);
}

errcode_t ocfs2_read_dir_block(ocfs2_filesys *fs, uint64_t block,
                               void *buf)
{
	errcode_t	retval;

 	retval = io_read_block(fs->fs_io, block, 1, buf);
	if (retval)
		return retval;

	return ocfs2_swap_dir_entries_to_cpu(buf, fs->fs_blocksize);
}

errcode_t ocfs2_write_dir_block(ocfs2_filesys *fs, uint64_t block,
                                void *inbuf)
{
	errcode_t	retval;
	char		*buf = NULL;

	retval = ocfs2_malloc_block(fs->fs_io, &buf);
	if (retval)
		return retval;

	memcpy(buf, inbuf, fs->fs_blocksize);

	retval = ocfs2_swap_dir_entries_from_cpu(buf, fs->fs_blocksize);
	if (retval)
		goto out;
	
 	retval = io_write_block(fs->fs_io, block, 1, buf);
out:
	ocfs2_free(&buf);
	return retval;
}
