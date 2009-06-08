/*
 * dir.h
 *
 * Function prototypes, macros, etc. for related 'C' files
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

#ifndef __DIR_H
#define __DIR_H

void mess_up_dir_inode(ocfs2_filesys *fs, enum fsck_type type, uint64_t blkno);
void mess_up_dir_dot(ocfs2_filesys *fs, enum fsck_type type, uint64_t blkno);
void mess_up_dir_ent(ocfs2_filesys *fs, enum fsck_type type, uint64_t blkno);
void mess_up_dir_parent_dup(ocfs2_filesys *fs, enum fsck_type type,
			    uint64_t blkno);
void mess_up_dir_not_connected(ocfs2_filesys *fs, enum fsck_type type,
			       uint64_t blkno);

void create_directory(ocfs2_filesys *fs, uint64_t parentblk, uint64_t *blkno);

#endif		/* __DIR_H */
