/*
 * ocfsgencreate.h
 *
 * Function prototypes for related 'C' file.
 *
 * Copyright (C) 2002 Oracle Corporation.  All rights reserved.
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

#ifndef _OCFSGENCREATE_H_
#define _OCFSGENCREATE_H_

int ocfs_verify_update_oin (ocfs_super * osb, ocfs_inode * oin);

int ocfs_find_contiguous_space_from_bitmap (ocfs_super * osb,
			       __u64 file_size,
			       __u64 * cluster_off, __u64 * cluster_count, bool sysfile);

int ocfs_create_oin_from_entry (ocfs_super * osb,
		    ocfs_file_entry * fe,
		    ocfs_inode ** new_oin,
		    __u64 parent_dir_off, ocfs_inode * parent_oin);

int ocfs_find_files_on_disk (ocfs_super * osb,
		 __u64 parent_off,
		 struct qstr * file_name,
		 ocfs_file_entry * fe, ocfs_file * ofile);

void ocfs_initialize_dir_node (ocfs_super * osb,
		   ocfs_dir_node * dir_node,
		   __u64 bitmap_off, __u64 file_off, __u32 node);

int ocfs_delete_file_entry (ocfs_super * osb,
		 ocfs_file_entry * fe, __u64 parent_off, __s32 log_node_num);

int ocfs_rename_file (ocfs_super * osb,
		__u64 parent_off, struct qstr * file_name, __u64 file_off);

int ocfs_del_file (ocfs_super * osb, __u64 parent_off, __u32 flags, __u64 file_off);

int ocfs_extend_file (ocfs_super * osb, __u64 parent_off,
		ocfs_inode * oin, __u64 file_size, __u64 * file_off);

int ocfs_change_file_size (ocfs_super * osb,
		    __u64 parent_off,
		    ocfs_inode * oin,
		    __u64 file_size, __u64 * file_off, struct iattr *attr);

int ocfs_get_dirnode(ocfs_super *osb, ocfs_dir_node *lockn, __u64 lockn_off,
		     ocfs_dir_node *dirn, bool *invalid_dirnode);

int ocfs_create_directory (ocfs_super * osb, __u64 parent_off, ocfs_file_entry * fe);

int ocfs_create_file (ocfs_super * osb, __u64 parent_off, ocfs_file_entry * fe);

int ocfs_create_modify_file (ocfs_super * osb,
		  __u64 parent_off,
		  ocfs_inode * oin,
		  struct qstr * file_name,
		  __u64 file_size,
		  __u64 * file_off, __u32 flags, ocfs_file_entry * fe, struct iattr *attr);

int ocfs_initialize_oin (ocfs_inode * oin,
		   ocfs_super * osb,
		   __u32 flags, struct file *file_obj, __u64 file_off, __u64 lock_id);

int ocfs_create_delete_cdsl (struct inode *inode,
		  struct file *filp, ocfs_super * osb, ocfs_cdsl * cdsl);

int ocfs_find_create_cdsl (ocfs_super * osb, ocfs_file_entry * fe);

#ifdef UNUSED_CODE
int ocfs_update_file_entry_slot (ocfs_super * osb, ocfs_inode * oin, ocfs_rw_mode rw_mode);

void ocfs_check_lock_state (ocfs_super * osb, ocfs_inode * oin);
#endif

int ocfs_delete_cdsl (ocfs_super * osb, __u64 parent_off, ocfs_file_entry * fe);

int ocfs_create_cdsl (ocfs_super * osb, __u64 parent_off, ocfs_file_entry * fe);

int ocfs_change_to_cdsl (ocfs_super * osb, __u64 parent_off, ocfs_file_entry * fe);

int ocfs_truncate_file (ocfs_super * osb, __u64 file_off, __u64 file_size, ocfs_inode *oin);

#endif				/* _OCFSGENCREATE_H_ */
