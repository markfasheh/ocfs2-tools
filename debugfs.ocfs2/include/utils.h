/*
 * utils.h
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

#ifndef __UTILS_H__
#define __UTILS_H__

void get_vote_flag (__u32 flag, GString *str);
void get_publish_flag (__u32 flag, GString *str);
void get_journal_blktyp (__u32 jtype, GString *str);
void get_tag_flag (__u32 flags, GString *str);
FILE *open_pager(void);
void close_pager(FILE *stream);
int inodestr_to_inode(char *str, uint64_t *blkno);
errcode_t string_to_inode(ocfs2_filesys *fs, uint64_t root_blkno,
			  uint64_t cwd_blkno, char *str, uint64_t *blkno);
errcode_t dump_file(ocfs2_filesys *fs, uint64_t ino, char *out_file,
		    int preserve);

#endif		/* __UTILS_H__ */
