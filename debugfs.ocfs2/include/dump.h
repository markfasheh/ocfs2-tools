/*
 * dump.h
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

#ifndef __DUMP_H__
#define __DUMP_H__

void dump_super_block (FILE *out, ocfs2_super_block *sb);
void dump_local_alloc (FILE *out, ocfs2_local_alloc *loc);
void dump_inode (FILE *out, ocfs2_dinode *in);
void dump_disk_lock (FILE *out, ocfs2_disk_lock *dl);
void dump_extent_list (FILE *out, ocfs2_extent_list *ext);
void dump_chain_list (FILE *out, ocfs2_chain_list *cl);
void dump_extent_block (FILE *out, ocfs2_extent_block *blk);
void dump_group_descriptor (FILE *out, ocfs2_group_desc *grp, int index);
int  dump_dir_entry (struct ocfs2_dir_entry *rec, int offset, int blocksize,
		     char *buf, void *priv_data);
void dump_config (FILE *out, char *buf);
void dump_publish (FILE *out, char *buf);
void dump_vote (FILE *out, char *buf);
void dump_jbd_header (FILE *out, journal_header_t *header);
void dump_jbd_superblock (FILE *out, journal_superblock_t *jsb);
void dump_jbd_block (FILE *out, journal_header_t *header, __u64 blknum);
void dump_jbd_metadata (FILE *out, int type, char *buf, __u64 blknum);
void dump_jbd_unknown (FILE *out, __u64 start, __u64 end);
void traverse_chain(FILE *out, __u64 blknum);
#endif		/* __DUMP_H__ */
