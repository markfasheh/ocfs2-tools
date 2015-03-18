/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * freefs.c
 *
 * Free an OCFS2 filesystem.  Part of the OCFS2 userspace library.
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
 * Ideas taken from e2fsprogs/lib/ext2fs/freefs.c
 *   Copyright (C) 1993, 1994, 1995, 1996 Theodore Ts'o.
 */

#define _XOPEN_SOURCE 600  /* Triggers XOPEN2K in features.h */
#define _LARGEFILE64_SOURCE

#include <stdlib.h>

#include "ocfs2/ocfs2.h"

void ocfs2_free_fs_inode_allocs(ocfs2_filesys *fs)
{
	uint16_t i;
	uint16_t max_slots = 0;
	struct ocfs2_super_block *osb = OCFS2_RAW_SB(fs->fs_super);
	if (!osb)
		return;
	max_slots = osb->s_max_slots;
	ocfs2_free_cached_inode(fs, fs->fs_system_inode_alloc);

	if (fs->fs_inode_allocs) {
		for (i = 0; i < max_slots; i++) {
			ocfs2_free_cached_inode(fs, fs->fs_inode_allocs[i]);
		}
	}

	if (fs->fs_eb_allocs) {
		for (i = 0; i < max_slots; i++) {
			ocfs2_free_cached_inode(fs, fs->fs_eb_allocs[i]);
		}
	}

	return;
}

void ocfs2_freefs(ocfs2_filesys *fs)
{
	if (!fs)
		abort();

	ocfs2_free_fs_inode_allocs(fs);

	if (fs->fs_orig_super)
		ocfs2_free(&fs->fs_orig_super);
	if (fs->fs_super)
		ocfs2_free(&fs->fs_super);
	if (fs->fs_devname)
		ocfs2_free(&fs->fs_devname);
	if (fs->fs_inode_allocs)
		ocfs2_free(&fs->fs_inode_allocs);
	if (fs->fs_eb_allocs)
		ocfs2_free(&fs->fs_eb_allocs);
	if (fs->ost)
		ocfs2_free(&fs->ost);
	if (fs->fs_io)
		io_close(fs->fs_io);

	ocfs2_free(&fs);
}
