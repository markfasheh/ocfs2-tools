/*
 * ocfsdlm.h
 *
 * ipcdlm related structures
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

#ifndef  _OCFSDLM_H_
#define  _OCFSDLM_H_

#define  OCFS_MAX_DLM_PKT_SIZE			256
#define  OCFS_DLM_MAX_MSG_SIZE			256

#define  OCFS_DLM_MSG_MAGIC			0x79677083

typedef struct _ocfs_dlm_msg_hdr
{
	__u64 lock_id;
	__u32 flags;
	__u64 lock_seq_num;
	__u8 open_handle;
}
OCFS_GCC_ATTR_PACKALGN
ocfs_dlm_msg_hdr;

typedef ocfs_dlm_msg_hdr ocfs_dlm_req_master;
typedef ocfs_dlm_msg_hdr ocfs_dlm_disk_vote_req;

typedef struct _ocfs_dlm_reply_master
{
	ocfs_dlm_msg_hdr h;
	__u32 status;
}
ocfs_dlm_reply_master;

typedef struct _ocfs_dlm_disk_vote_reply
{
	ocfs_dlm_msg_hdr h;
	__u32 status;
}
ocfs_dlm_disk_vote_reply;

typedef struct _ocfs_dlm_msg
{
	__u32 magic;
	__u32 msg_len;
	__u8 vol_id[MAX_VOL_ID_LENGTH];
	__u32 src_node;
	__u32 dst_node;
	__u32 msg_type;
	__u32 check_sum;
	__u8 msg_buf[1];
}
ocfs_dlm_msg;

typedef struct _ocfs_recv_ctxt
{
	__s32 msg_len;
	__u8 msg[OCFS_MAX_DLM_PKT_SIZE];
	int status;
	struct tq_struct ipc_tq;
}
ocfs_recv_ctxt;

enum
{
	OCFS_VOTE_REQUEST = 1,
	OCFS_VOTE_REPLY,
	OCFS_INFO_DISMOUNT
};

#define ocfs_compute_dlm_stats(__status, __vote_status, __stats)	\
do {									\
	atomic_inc (&((__stats)->total));				\
	if (__status == -ETIMEDOUT)					\
		atomic_inc (&((__stats)->etimedout));			\
	else {								\
		switch (__vote_status) {				\
		case -EAGAIN:						\
		case FLAG_VOTE_UPDATE_RETRY:				\
			atomic_inc (&((__stats)->eagain));		\
			break;						\
		case -ENOENT:						\
		case FLAG_VOTE_FILE_DEL:				\
			atomic_inc (&((__stats)->enoent));		\
			break;						\
		case -EBUSY:						\
		case -EFAIL:						\
		case FLAG_VOTE_OIN_ALREADY_INUSE:			\
			atomic_inc (&((__stats)->efail));		\
			break;						\
		case 0:							\
		case FLAG_VOTE_NODE:					\
		case FLAG_VOTE_OIN_UPDATED:				\
			atomic_inc (&((__stats)->okay));		\
			break;						\
		default:						\
			atomic_inc (&((__stats)->def));			\
			break;						\
		}							\
	}								\
} while (0)

#define ocfs_compute_lock_type_stats(__stats, __type)			\
do {\
	switch (__type) {\
	case OCFS_UPDATE_LOCK_STATE:\
		atomic_inc (&((__stats)->update_lock_state));\
		break;\
	case OCFS_MAKE_LOCK_MASTER:\
		atomic_inc (&((__stats)->make_lock_master));\
		break;\
	case OCFS_DISK_RELEASE_LOCK:\
		atomic_inc (&((__stats)->disk_release_lock));\
		break;\
	case OCFS_BREAK_CACHE_LOCK:\
		atomic_inc (&((__stats)->break_cache_lock));\
		break;\
	default:\
		atomic_inc (&((__stats)->others));\
		break;\
	}\
} while (0)

#endif				/* _OCFSDLM_H_ */
