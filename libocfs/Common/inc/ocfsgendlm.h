/*
 * ocfsgendlm.h
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

#ifndef _OCFSGENDLM_H_
#define _OCFSGENDLM_H_

typedef struct _ocfs_offset_map
{
	__u32 length;
	__u64 log_disk_off;
	__u64 actual_disk_off;
}
OCFS_GCC_ATTR_PACKALGN
ocfs_offset_map;

int ocfs_insert_cache_link (ocfs_super * osb, ocfs_lock_res * lockres);

int ocfs_update_lock_state (ocfs_super * osb, ocfs_lock_res * lockres,
			    __u32 flags, bool *disk_vote);

int ocfs_disk_request_vote (ocfs_super * osb, __u64 lock_id, __u32 lock_type,
			    __u32 flags, __u64 vote_map, __u64 * lock_seq_num);

int ocfs_wait_for_disk_lock_release (ocfs_super * osb, __u64 offset,
				     __u32 time_to_wait, __u32 lock_type);

int ocfs_wait_for_lock_release (ocfs_super * osb, __u64 offset, __u32 time_to_wait,
				ocfs_lock_res * lockres, __u32 lock_type);

int ocfs_get_vote_on_disk (ocfs_super * osb, __u64 lock_id, __u32 lock_type,
			   __u32 flags, __u64 * got_vote_map, __u64 vote_map,
			   __u64 lock_seq_num, __u64 * oin_open_map);

int ocfs_disk_reset_voting (ocfs_super * osb, __u64 lock_id, __u32 lock_type);

int ocfs_wait_for_vote (ocfs_super * osb, __u64 lock_id, __u32 lock_type, __u32 flags,
			__u64 vote_map, __u32 time_to_wait, __u64 lock_seq_num,
			ocfs_lock_res * lockres);

int ocfs_reset_voting (ocfs_super * osb, __u64 lock_id, __u32 lock_type, __u64 vote_map);

int ocfs_request_vote (ocfs_super * osb, __u64 lock_id, __u32 lock_type, __u32 flags,
		       __u64 vote_map, __u64 * lock_seq_num);

int ocfs_comm_request_vote (ocfs_super * osb, __u64 lock_id, __u32 lock_type,
			    __u32 flags, ocfs_file_entry * fe, __u32 attempts);

void ocfs_init_dlm_msg (ocfs_super * osb, ocfs_dlm_msg * dlm_msg, __u32 msg_len);

int ocfs_send_dlm_request_msg (ocfs_super * osb, __u64 lock_id, __u32 lock_type,
			       __u32 flags, ocfs_lock_res * lockres,
			       __u64 vote_map);

int ocfs_comm_make_lock_master (ocfs_super * osb, __u64 lock_id, __u32 lock_type,
				__u32 flags, ocfs_lock_res * lockres,
				ocfs_file_entry * fe, __u64 vote_map,
				__u32 attempts);

int ocfs_make_lock_master (ocfs_super * osb, __u64 lock_id, __u32 lock_type,
			   __u32 flags, ocfs_lock_res * lockres,
			   ocfs_file_entry * fe, bool *disk_vote);

int ocfs_acquire_lockres_ex (ocfs_lock_res * lockres, __u32 timeout);
#define ocfs_acquire_lockres(a)		ocfs_acquire_lockres_ex(a, 0)

void ocfs_release_lockres (ocfs_lock_res * lockres);

int ocfs_update_disk_lock (ocfs_super * osb, ocfs_lock_res * lockres,
			   __u32 flags, ocfs_file_entry * fe);

int ocfs_update_master_on_open (ocfs_super * osb, ocfs_lock_res * lockres);

void ocfs_init_lockres (ocfs_super * osb, ocfs_lock_res * lockres,
			__u64 lock_id);

int ocfs_create_update_lock (ocfs_super * osb, ocfs_inode * oin, __u64 lock_id,
			     __u32 flags);

int ocfs_get_x_for_del (ocfs_super * osb, __u64 lock_id, __u32 lock_type, __u32 flags,
			ocfs_lock_res * lockres, ocfs_file_entry * fe);

int ocfs_try_exclusive_lock (ocfs_super *osb, ocfs_lock_res *lockres, __u32 flags,
			     __u32 updated, ocfs_file_entry *fe, __u64 lock_id,
			     __u32 lock_type);

int ocfs_acquire_lock (ocfs_super * osb, __u64 lock_id, __u32 lock_type, __u32 flags,
		       ocfs_lock_res ** lockres, ocfs_file_entry * lock_fe);

int ocfs_disk_release_lock (ocfs_super * osb, __u64 lock_id, __u32 lock_type,
			    __u32 flags, ocfs_lock_res * lockres, ocfs_file_entry *fe);

int ocfs_release_lock (ocfs_super * osb, __u64 lock_id, __u32 lock_type, __u32 flags,
		       ocfs_lock_res * lockres, ocfs_file_entry *fe);

int ocfs_init_dlm (void);

int ocfs_add_lock_to_recovery (void);

int ocfs_create_log_extent_map (ocfs_super * osb, ocfs_io_runs ** PTransRuns,
		__u32 * PNumTransRuns, __u64 diskOffset, __u64 ByteCount);

int ocfs_lookup_cache_link (ocfs_super * osb, __u8 * buf, __u64 actual_disk_off,
			    __u64 length);

int ocfs_process_log_file (ocfs_super * osb, bool flag);

int ocfs_break_cache_lock (ocfs_super * osb, ocfs_lock_res * lockres,
			   ocfs_file_entry *fe);

#endif				/* _OCFSGENDLM_H_ */
