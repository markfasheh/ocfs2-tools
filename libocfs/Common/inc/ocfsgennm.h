/*
 * ocfsgennm.h
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

#ifndef _OCFSGENNM_H_
#define _OCFSGENNM_H_

static inline int ocfs_validate_lockid (ocfs_super *osb, __u64 lockid) {
	if (!lockid)
		return -1;
	if (osb->vol_layout.size < lockid)
		return -1;
	if (lockid % OCFS_SECTOR_SIZE)
		return -1;
	return 0;
}

int ocfs_flush_data (ocfs_inode * oin);

int ocfs_disk_update_resource (ocfs_super * osb, ocfs_lock_res * lockres,
			       ocfs_file_entry * file_ent, __u32 timeout);

int ocfs_check_for_stale_lock(ocfs_super *osb, ocfs_lock_res *lockres, 
			      ocfs_file_entry *fe, __u64 lock_id);

int ocfs_find_update_res (ocfs_super * osb, __u64 lock_id,
			  ocfs_lock_res ** lockres, ocfs_file_entry * fe,
			  __u32 * updated, __u32 timeout);

int ocfs_vote_for_del_ren (ocfs_super * osb, ocfs_publish * publish,
			   __u32 node_num, ocfs_vote * vote,
			   ocfs_lock_res ** lockres);

struct inode * ocfs_get_inode_from_offset(ocfs_super * osb, __u64 fileoff);

int ocfs_process_update_inode_request (ocfs_super * osb, __u64 lock_id,
				       ocfs_lock_res * lockres, __u32 node_num);

int ocfs_process_vote (ocfs_super * osb, ocfs_publish * publish, __u32 node_num);

int ocfs_common_del_ren (ocfs_super * osb, __u64 lock_id, __u32 flags,
			 __u32 node_num, __u64 seq_num, __u8 * vote,
			 ocfs_lock_res ** lockres);

#endif				/* _OCFSGENNM_H_ */
