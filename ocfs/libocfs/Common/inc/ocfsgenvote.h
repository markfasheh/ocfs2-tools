/*
 * ocfsgenvote.h
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

#ifndef _OCFSGENVOTE_H_
#define _OCFSGENVOTE_H_

int ocfs_send_vote_reply (ocfs_super * osb, ocfs_dlm_msg * dlm_msg,
			  __u32 vote_status, bool inode_open);

int ocfs_comm_vote_for_del_ren (ocfs_super * osb, ocfs_lock_res ** lockres,
				ocfs_dlm_msg * dlm_msg);

bool ocfs_check_ipc_msg (__u8 * msg, __u32 msg_len);

void ocfs_find_osb (__s8 * volume_id, ocfs_super ** osb);

int ocfs_find_create_lockres (ocfs_super * osb, __u64 lock_id,
			      ocfs_lock_res ** lockres);

int ocfs_comm_process_vote (ocfs_super * osb, ocfs_dlm_msg * dlm_msg);

int ocfs_comm_process_vote_reply (ocfs_super * osb, ocfs_dlm_msg * dlm_msg);

void ocfs_dlm_recv_msg (void *val);

int ocfs_comm_process_msg (__u8 * msg);

int ocfs_send_dismount_msg (ocfs_super * osb, __u64 vote_map);

void ocfs_comm_process_dismount (ocfs_super * osb, ocfs_dlm_msg * dlm_msg);

#endif				/* _OCFSGENVOTE_H_ */
