/*
 * utils.h
 *
 * Function prototypes, macros, etc. for related 'C' files
 *
 * Copyright (C) 2004, 2008 Oracle.  All rights reserved.
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

#ifndef __UTILS_H__
#define __UTILS_H__

struct rdump_opts {
	ocfs2_filesys *fs;
	char *fullname;
	char *buf;
	int verbose;
};

struct strings {
	char *s_str;
	struct list_head s_list;
};

void get_incompat_flag(struct ocfs2_super_block *sb, char *buf, size_t count);
void get_tunefs_flag(struct ocfs2_super_block *sb, char *buf, size_t count);
void get_compat_flag(struct ocfs2_super_block *sb, char *buf, size_t count);
void get_rocompat_flag(struct ocfs2_super_block *sb, char *buf, size_t count);
void get_cluster_info_flag(struct ocfs2_super_block *sb, char *buf,
			   size_t count);
void get_journal_block_type (uint32_t jtype, GString *str);
void get_tag_flag (uint32_t flags, GString *str);
void get_journal_compat_flag(uint32_t flags, char *buf, size_t count);
void get_journal_incompat_flag(uint32_t flags, char *buf, size_t count);
void get_journal_rocompat_flag(uint32_t flags, char *buf, size_t count);
void ctime_nano(struct timespec *t, char *buf, int buflen);
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

errcode_t get_debugfs_path(char *debugfs_path, int len);
errcode_t open_debugfs_file(const char *debugfs_path, const char *dirname,
			    const char *uuid, const char *filename, FILE **fd);

void init_stringlist(struct list_head *strlist);
void free_stringlist(struct list_head *strlist);
errcode_t add_to_stringlist(char *str, struct list_head *strlist);
int del_from_stringlist(char *str, struct list_head *strlist);

errcode_t traverse_extents(ocfs2_filesys *fs, struct ocfs2_extent_list *el,
			   FILE *out);
errcode_t traverse_chains(ocfs2_filesys *fs, struct ocfs2_chain_list *cl,
			  FILE *out);

enum dump_block_type detect_block (char *buf);

#endif		/* __UTILS_H__ */
