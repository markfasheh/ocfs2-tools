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


unsigned int ocfs2_dir_trailer_blk_off(ocfs2_filesys *fs)
{
	return fs->fs_blocksize - sizeof(struct ocfs2_dir_block_trailer);
}

struct ocfs2_dir_block_trailer *ocfs2_dir_trailer_from_block(ocfs2_filesys *fs,
							     void *data)
{
	char *p = data;

	p += ocfs2_dir_trailer_blk_off(fs);
	return (struct ocfs2_dir_block_trailer *)p;
}

int ocfs2_dir_has_trailer(ocfs2_filesys *fs, struct ocfs2_dinode *di)
{
	if (ocfs2_support_inline_data(OCFS2_RAW_SB(fs->fs_super)) &&
	    (di->i_dyn_features & OCFS2_INLINE_DATA_FL))
		return 0;

	return ocfs2_meta_ecc(OCFS2_RAW_SB(fs->fs_super));
}

int ocfs2_supports_dir_trailer(ocfs2_filesys *fs)
{
	return ocfs2_meta_ecc(OCFS2_RAW_SB(fs->fs_super));
}

int ocfs2_skip_dir_trailer(ocfs2_filesys *fs, struct ocfs2_dinode *di,
			   struct ocfs2_dir_entry *de, unsigned long offset)
{
	if (!ocfs2_dir_has_trailer(fs, di))
		return 0;

	if (offset != ocfs2_dir_trailer_blk_off(fs))
		return 0;

	return 1;
}

void ocfs2_init_dir_trailer(ocfs2_filesys *fs, void *buf)
{
	struct ocfs2_dir_block_trailer *trailer =
		ocfs2_dir_trailer_from_block(fs, buf);

	memcpy(trailer->db_signature, OCFS2_DIR_TRAILER_SIGNATURE,
	       strlen(OCFS2_DIR_TRAILER_SIGNATURE));
	trailer->db_compat_rec_len = sizeof(struct ocfs2_dir_block_trailer);
}

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

void ocfs2_swap_dir_trailer(struct ocfs2_dir_block_trailer *trailer)
{
	if (cpu_is_little_endian)
		return;

	bswap_64(trailer->db_compat_inode);
	bswap_64(trailer->db_compat_rec_len);
}

errcode_t ocfs2_read_dir_block(ocfs2_filesys *fs, struct ocfs2_dinode *di,
			       uint64_t block, void *buf)
{
	errcode_t	retval;
	int		end = fs->fs_blocksize;
	struct ocfs2_dir_block_trailer *trailer = NULL;

	retval = ocfs2_read_blocks(fs, block, 1, buf);
	if (retval)
		goto out;

	if (ocfs2_dir_has_trailer(fs, di)) {
		end = ocfs2_dir_trailer_blk_off(fs);
		trailer = ocfs2_dir_trailer_from_block(fs, buf);

		if (memcmp(trailer->db_signature, OCFS2_DIR_TRAILER_SIGNATURE,
			   strlen(OCFS2_DIR_TRAILER_SIGNATURE))) {
			retval = OCFS2_ET_BAD_DIR_BLOCK_MAGIC;
			goto out;
		}
	}

	retval = ocfs2_swap_dir_entries_to_cpu(buf, end);
	if (!retval)
		goto out;

	if (trailer)
		ocfs2_swap_dir_trailer(trailer);

out:
	return retval;
}

errcode_t ocfs2_write_dir_block(ocfs2_filesys *fs, struct ocfs2_dinode *di,
				uint64_t block, void *inbuf)
{
	errcode_t	retval;
	char		*buf = NULL;
	int		end = fs->fs_blocksize;
	struct ocfs2_dir_block_trailer *trailer = NULL;

	retval = ocfs2_malloc_block(fs->fs_io, &buf);
	if (retval)
		return retval;

	memcpy(buf, inbuf, fs->fs_blocksize);

	if (ocfs2_dir_has_trailer(fs, di)) {
		end = ocfs2_dir_trailer_blk_off(fs);
		trailer = ocfs2_dir_trailer_from_block(fs, buf);
	}

	retval = ocfs2_swap_dir_entries_from_cpu(buf, end);
	if (retval)
		goto out;
	
	if (trailer)
		ocfs2_swap_dir_trailer(trailer);

 	retval = io_write_block(fs->fs_io, block, 1, buf);
out:
	ocfs2_free(&buf);
	return retval;
}
