/*
 * special.c
 *
 * root, lost+found corruptions
 *
 * Copyright (C) 2006 Oracle.  All rights reserved.
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

#include "main.h"

extern char *progname;

/* This file will corrupt inode root.
 * And as a sequence, lost+found will also disappear.
 * 
 * Special files error: ROOT_NOTDIR, ROOT_DIR_MISSING, LOSTFOUND_MISSING
 * 
 */
void mess_up_root(ocfs2_filesys *fs, uint64_t blkno)
{
	errcode_t ret;
	char *inobuf = NULL;
	struct ocfs2_dinode *di;
	struct ocfs2_super_block *sb = OCFS2_RAW_SB(fs->fs_super);
	blkno = sb->s_root_blkno;

	ret = ocfs2_malloc_block(fs->fs_io, &inobuf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_read_inode(fs, blkno, inobuf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	di = (struct ocfs2_dinode *)inobuf;

	if (!(di->i_flags & OCFS2_VALID_FL))
		FSWRK_FATAL("not a file");

	di->i_mode = 0;

	ret = ocfs2_write_inode(fs, blkno, inobuf);
	if (ret) 
		FSWRK_COM_FATAL(progname, ret);

	if (inobuf)
		ocfs2_free(&inobuf);

	fprintf(stdout, "ROOT_NOTDIR: "
		"Corrupt root inode#%"PRIu64"\n", blkno);
	return;
}
