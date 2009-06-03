/*
 * inode.h
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

#ifndef __INODE_H
#define __INODE_H

void mess_up_inode_field(ocfs2_filesys *fs, uint64_t blkno);
void mess_up_inode_not_connected(ocfs2_filesys *fs, uint64_t blkno);
void mess_up_inode_orphaned(ocfs2_filesys *fs, uint16_t slotnum);
void mess_up_inode_alloc(ocfs2_filesys *fs, uint16_t slotnum);
void mess_up_inline_flag(ocfs2_filesys *fs, uint64_t blkno);
void mess_up_inline_count(ocfs2_filesys *fs, uint64_t blkno);
void mess_up_dup_clusters(ocfs2_filesys *fs, uint64_t blkno);

#endif		/* __INODE_H */
