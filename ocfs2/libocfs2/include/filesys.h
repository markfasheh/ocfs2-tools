/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * filesys.h
 *
 * Filesystem object routines for the OCFS2 userspace library.
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
 */

#ifndef _FILESYS_H
#define _FILESYS_H

#define OCFS2_LIB_FEATURE_INCOMPAT_SUPP		OCFS2_FEATURE_INCOMPAT_SUPP
#define OCFS2_LIB_FEATURE_RO_COMPAT_SUPP	OCFS2_FEATURE_RO_COMPAT_SUPP

/* Flags for the ocfs2_filesys structure */
#define OCFS2_FLAG_RO		0x00
#define OCFS2_FLAG_RW		0x01
#define OCFS2_FLAG_CHANGED	0x02
#define OCFS2_FLAG_DIRTY	0x04
#define OCFS2_FLAG_SWAP_BYTES	0x08

typedef struct _ocfs2_filesys ocfs2_filesys;

struct _ocfs2_filesys {
	char *fs_devname;
	uint32_t fs_flags;
	io_channel *fs_io;
	ocfs2_dinode *fs_super;
	ocfs2_dinode *fs_orig_super;
	unsigned int fs_blocksize;
	unsigned int fs_clustersize;
	uint32_t fs_umask;

	/* Reserved for the use of the calling application. */
	void *fs_private;
};


errcode_t ocfs2_open(const char *name, int flags, int superblock,
		     unsigned int blksize, ocfs2_filesys **ret_fs);
errcode_t ocfs2_flush(ocfs2_filesys *fs);
errcode_t ocfs2_close(ocfs2_filesys *fs);
void ocfs2_freefs(ocfs2_filesys *fs);

errcode_t ocfs2_read_inode(ocfs2_filesys *fs, uint64_t blkno,
			   ocfs2_dinode *di);
errcode_t ocfs2_write_inode(ocfs2_filesys *fs, uint64_t blkno,
			    ocfs2_dinode *di);

#endif  /* _FILESYS_H */

