/*
 * readfs.h
 *
 * Function prototypes, macros, etc. for related 'C' files
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
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
 * Authors: Sunil Mushran
 */

#ifndef __READFS_H__
#define __READFS_H__

int read_super_block (int fd, char **buf);
int read_inode (int fd, __u32 blknum, char *buf, int buflen);
int traverse_extents (int fd, ocfs2_extent_list *ext, GArray *arr, int dump);
void read_dir_block (struct ocfs2_dir_entry *dir, int len, GArray *arr);
void read_dir (int fd, ocfs2_extent_list *ext, __u64 size, GArray *dirarr);
void read_sysdir (int fd, char *sysdir);
void read_file (int fd, ocfs2_extent_list *ext, __u64 size, char *buf, int fdo);
void process_dlm (int fd, int type);

#endif		/* __READFS_H__ */
