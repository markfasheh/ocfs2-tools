/*
 * ocfsgentrans.h
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

#ifndef _OCFSGENTRANS_H_
#define _OCFSGENTRANS_H_

int ocfs_free_disk_bitmap (ocfs_super * osb, ocfs_cleanup_record * log_rec);

int ocfs_process_record (ocfs_super * osb, void *buffer);

int ocfs_process_log (ocfs_super * osb, __u64 trans_id, __u32 node_num, __u32 * type);

int ocfs_start_trans (ocfs_super * osb);

int ocfs_commit_trans (ocfs_super * osb, __u64 trans_id);

int ocfs_abort_trans (ocfs_super * osb, __u64 trans_id);

int ocfs_reset_publish (ocfs_super * osb, __u64 node_num);

int ocfs_recover_vol (ocfs_super * osb, __u64 node_num);

int ocfs_write_log (ocfs_super * osb, ocfs_log_record * log_rec, __u32 type);

int ocfs_write_node_log (ocfs_super * osb,
		  ocfs_log_record * log_rec, __u32 node_num, __u32 type);

#endif				/* _OCFSGENTRANS_H_ */
