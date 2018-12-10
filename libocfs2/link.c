/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * link.c
 *
 * Create links in OCFS2 directories.  For the OCFS2 userspace library.
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
 *
 *  This code is a port of e2fsprogs/lib/ext2fs/link.c
 *  Copyright (C) 1993, 1994 Theodore Ts'o.
 */

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE
#define _DEFAULT_SOURCE

#include <string.h>

#include "ocfs2/ocfs2.h"


struct link_struct  {
	const char		*name;
	int			namelen;
	uint64_t		inode;
	int			flags;
	int			done;
	int			blockend;  /* What to consider the end
					      of the block.  This handles
					      the directory trailer if it
					      exists */
	int			blkno;
	struct ocfs2_dinode	*sb;
};	

static int link_proc(struct ocfs2_dir_entry *dirent,
		     uint64_t	blocknr,
		     int	offset,
		     int	blocksize,
		     char	*buf,
		     void	*priv_data)
{
	struct link_struct *ls = (struct link_struct *) priv_data;
	struct ocfs2_dir_entry *next;
	int rec_len, min_rec_len;
	int ret = 0;

	rec_len = OCFS2_DIR_REC_LEN(ls->namelen);

	/*
	 * See if the following directory entry (if any) is unused;
	 * if so, absorb it into this one.
	 */
	next = (struct ocfs2_dir_entry *) (buf + offset + dirent->rec_len);
	if ((offset + dirent->rec_len < ls->blockend - 8) &&
	    (next->inode == 0) &&
	    (offset + dirent->rec_len + next->rec_len <= ls->blockend)) {
		dirent->rec_len += next->rec_len;
		ret = OCFS2_DIRENT_CHANGED;
	}

	/*
	 * If the directory entry is used, see if we can split the
	 * directory entry to make room for the new name.  If so,
	 * truncate it and return.
	 */
	if (dirent->inode) {
		min_rec_len = OCFS2_DIR_REC_LEN(dirent->name_len & 0xFF);
		if (dirent->rec_len < (min_rec_len + rec_len))
			return ret;
		rec_len = dirent->rec_len - min_rec_len;
		dirent->rec_len = min_rec_len;
		next = (struct ocfs2_dir_entry *) (buf + offset +
                                                   dirent->rec_len);
		next->inode = 0;
		next->name_len = 0;
		next->rec_len = rec_len;
		return OCFS2_DIRENT_CHANGED;
	}

	/*
	 * If we get this far, then the directory entry is not used.
	 * See if we can fit the request entry in.  If so, do it.
	 */
	if (dirent->rec_len < rec_len)
		return ret;
	dirent->inode = ls->inode;
	dirent->name_len = ls->namelen;
	strncpy(dirent->name, ls->name, ls->namelen);
	dirent->file_type = ls->flags;

	ls->blkno = blocknr;
	ls->done++;
	return OCFS2_DIRENT_ABORT|OCFS2_DIRENT_CHANGED;
}

/*
 * Note: the low 3 bits of the flags field are used as the directory
 * entry filetype.
 */
errcode_t ocfs2_link(ocfs2_filesys *fs, uint64_t dir, const char *name, 
		     uint64_t ino, int flags)
{
	errcode_t		retval;
	struct link_struct	ls;
	char			*buf;
	struct ocfs2_dinode	*di;

	if (!(fs->fs_flags & OCFS2_FLAG_RW))
		return OCFS2_ET_RO_FILESYS;

	if ((ino < OCFS2_SUPER_BLOCK_BLKNO) ||
	    (ino > fs->fs_blocks))
		return OCFS2_ET_INVALID_ARGUMENT;

        retval = ocfs2_malloc_block(fs->fs_io, &buf);
        if (retval)
            return retval;

	retval = ocfs2_read_inode(fs, dir, buf);
	if (retval)
		goto out_free;

        di = (struct ocfs2_dinode *)buf;

	ls.name = name;
	ls.namelen = name ? strlen(name) : 0;
	ls.inode = ino;
	ls.flags = flags;
	ls.done = 0;
	ls.sb = fs->fs_super;
	if (ocfs2_dir_has_trailer(fs, di))
		ls.blockend = ocfs2_dir_trailer_blk_off(fs);
	else
		ls.blockend = fs->fs_blocksize;

	retval = ocfs2_dir_iterate(fs, dir,
                                   OCFS2_DIRENT_FLAG_INCLUDE_EMPTY,
                                   NULL, link_proc, &ls);
	if (retval)
		goto out_free;

	if (!ls.done) {
		retval = ocfs2_expand_dir(fs, dir);
		if (retval)
			goto out_free;

		/* Gotta refresh */
		retval = ocfs2_read_inode(fs, dir, buf);
		if (retval)
			goto out_free;

		if (ocfs2_dir_has_trailer(fs, di))
			ls.blockend = ocfs2_dir_trailer_blk_off(fs);
		else
			ls.blockend = fs->fs_blocksize;
		retval = ocfs2_dir_iterate(fs, dir,
					   OCFS2_DIRENT_FLAG_INCLUDE_EMPTY,
					   NULL, link_proc, &ls);
		if (!retval && !ls.done)
			retval = OCFS2_ET_INTERNAL_FAILURE;
	}

	if (ls.done) {
		if (ocfs2_supports_indexed_dirs(OCFS2_RAW_SB(fs->fs_super)) &&
		    (di->i_dyn_features & OCFS2_INDEXED_DIR_FL))
			retval = ocfs2_dx_dir_insert_entry(fs, dir, ls.name,
							ls.inode, ls.blkno);
	}
out_free:
	ocfs2_free(&buf);

	return retval;
}

