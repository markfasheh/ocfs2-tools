/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * util.c
 *
 * file system checker for OCFS2
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
 * Authors: Zach Brown
 */
#include <inttypes.h>
#include "ocfs2.h"

#include "util.h"

void o2fsck_write_inode(ocfs2_filesys *fs, uint64_t blkno, ocfs2_dinode *di)
{
	errcode_t ret;

	if (blkno != di->i_blkno)
		fatal_error(0, "Asked to write inode with i_blkno %"PRIu64
				" to different block %"PRIu64".\n", 
				di->i_blkno, blkno);

	ret = ocfs2_write_inode(fs, blkno, (char *)di);
	if (ret)
		fatal_error(ret, "while writing inode %"PRIu64, di->i_blkno);
}
