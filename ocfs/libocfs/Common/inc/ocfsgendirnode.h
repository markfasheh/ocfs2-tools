/*
 * ocfsgendirnode.h
 *
 * Function prototypes for related 'C' file.
 *
 * Copyright (C) 2002, 2003 Oracle.  All rights reserved.
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
 * Authors: Neeraj Goyal, Suchit Kaura, Kurt Hackel, Sunil Mushran,
 *          Manish Singh, Wim Coekaerts
 */

#ifndef _OCFSGENDIRNODE_H_
#define _OCFSGENDIRNODE_H_

void ocfs_print_file_entry (ocfs_file_entry * fe);

void ocfs_print_dir_node (ocfs_super * osb, ocfs_dir_node * DirNode);

int ocfs_alloc_node_block (ocfs_super * osb,
		__u64 FileSize,
		__u64 * DiskOffset,
		__u64 * file_off, __u64 * NumClusterAlloc, __u32 NodeNum, __u32 Type);

int ocfs_free_vol_block (ocfs_super * osb, ocfs_free_log * FreeLog, __u32 NodeNum, __u32 Type);

int ocfs_free_node_block (ocfs_super * osb,
	       __u64 file_off, __u64 Length, __u32 NodeNum, __u32 Type);

int ocfs_free_directory_block (ocfs_super * osb, ocfs_file_entry * fe, __s32 LogNodeNum);

int ocfs_recover_dir_node (ocfs_super * osb,
		__u64 OrigDirNodeOffset, __u64 SavedDirNodeOffset);

#define ocfs_read_dir_node(__osb, __dirn, __off)	\
	ocfs_read_disk(__osb, __dirn, (__osb)->vol_layout.dir_node_size, __off)

int ocfs_write_force_dir_node (ocfs_super * osb,
		       ocfs_dir_node * DirNode, __s32 IndexFileEntry);

int ocfs_write_dir_node (ocfs_super * osb,
		  ocfs_dir_node * DirNode, __s32 IndexFileEntry);

bool ocfs_walk_dir_node (ocfs_super * osb,
	       ocfs_dir_node * DirNode,
	       ocfs_file_entry * found_fe, ocfs_file * OFile);

bool ocfs_search_dir_node (ocfs_super * osb,
		 ocfs_dir_node * DirNode,
		 struct qstr * SearchName,
		 ocfs_file_entry * found_fe, ocfs_file * OFile);

bool ocfs_find_index (ocfs_super * osb,
	   ocfs_dir_node * DirNode, struct qstr * FileName, int *Index);

int ocfs_reindex_dir_node (ocfs_super * osb, __u64 DirNodeOffset, ocfs_dir_node * DirNode);

int ocfs_insert_dir_node (ocfs_super * osb,
	       ocfs_dir_node * DirNode,
	       ocfs_file_entry * InsertEntry,
	       ocfs_dir_node * LockNode, __s32 * IndexOffset);

int ocfs_del_file_entry (ocfs_super * osb,
	      ocfs_file_entry * EntryToDel, ocfs_dir_node * LockNode);

int ocfs_insert_file (ocfs_super * osb, ocfs_dir_node * DirNode,
		      ocfs_file_entry * InsertEntry, ocfs_dir_node * LockNode,
		      ocfs_lock_res * LockResource, bool invalid_dirnode);

int ocfs_validate_dir_index (ocfs_super *osb, ocfs_dir_node *dirnode);

int ocfs_validate_num_del (ocfs_super *osb, ocfs_dir_node *dirnode);

static inline int ocfs_validate_dirnode (ocfs_super *osb, ocfs_dir_node *dirn)
{
	int ret = 0;

	if (!IS_VALID_DIR_NODE (dirn))
		ret = -EFAIL;

	if (ret == 0)
		ret = ocfs_validate_dir_index (osb, dirn);
	if (ret == 0)
		ret = ocfs_validate_num_del (osb, dirn);

	return ret;
}

static inline void ocfs_update_hden (ocfs_dir_node *lockn, ocfs_dir_node *dirn,
				     __u64 off)
{
	if (lockn->node_disk_off != dirn->node_disk_off)
		lockn->head_del_ent_node = off;
	else
		dirn->head_del_ent_node = off;
}

#endif				/* _OCFSGENDIRNODE_H_ */
