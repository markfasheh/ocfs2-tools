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

typedef struct _rdump_opts {
	ocfs2_filesys *fs;
	char *fullname;
	char *buf;
	int verbose;
} rdump_opts;

void get_incompat_flag(uint32_t flag, GString *str);
void get_tunefs_flag(uint32_t incompat_flag, uint16_t flag, GString *str);
void get_compat_flag(uint32_t flag, GString *str);
void get_rocompat_flag(uint32_t flag, GString *str);
void get_vote_flag (uint32_t flag, GString *str);
void get_publish_flag (uint32_t flag, GString *str);
void get_journal_block_type (uint32_t jtype, GString *str);
void get_tag_flag (uint32_t flags, GString *str);
FILE *open_pager(int interactive);
void close_pager(FILE *stream);
int inodestr_to_inode(char *str, uint64_t *blkno);
errcode_t string_to_inode(ocfs2_filesys *fs, uint64_t root_blkno,
			  uint64_t cwd_blkno, char *str, uint64_t *blkno);
errcode_t dump_file(ocfs2_filesys *fs, uint64_t ino, int fd, char *out_file,
		    int preserve);
errcode_t read_whole_file(ocfs2_filesys *fs, uint64_t ino, char **buf,
			  uint32_t *buflen);
void inode_perms_to_str(uint16_t mode, char *str, int len);
void inode_time_to_str(uint64_t mtime, char *str, int len);
errcode_t rdump_inode(ocfs2_filesys *fs, uint64_t blkno, const char *name,
		      const char *dumproot, int verbose);
void crunch_strsplit(char **args);
void find_max_contig_free_bits(struct ocfs2_group_desc *gd, int *max_contig_free_bits);

#endif		/* __UTILS_H__ */
