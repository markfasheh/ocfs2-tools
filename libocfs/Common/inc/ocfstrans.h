/*
 * ocfstrans.h
 *
 * Logging and recovery related structures
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

#ifndef _OCFSTRANS_H_
#define _OCFSTRANS_H_

#define  LOG_TYPE_DISK_ALLOC      1
#define  LOG_TYPE_DIR_NODE        2
#define  LOG_TYPE_RECOVERY        3
#define  LOG_CLEANUP_LOCK         4
#define  LOG_TYPE_TRANS_START     5
#define  LOG_TYPE_TRANS_END       6
#define  LOG_RELEASE_BDCAST_LOCK  7
#define  LOG_DELETE_ENTRY         8
#define  LOG_MARK_DELETE_ENTRY    9
#define  LOG_FREE_BITMAP          10
#define  LOG_UPDATE_EXTENT        11
#define  LOG_DELETE_NEW_ENTRY     12

typedef struct _ocfs_free_bitmap
{
	__u64 length;
	__u64 file_off;
	__u32 type;
	__u32 node_num;
}
ocfs_free_bitmap;

typedef struct _ocfs_free_extent_log
{
	__u32 index;
	__u8 pad[4];
	__u64 disk_off;
}
ocfs_free_extent_log;

#define  FREE_LOG_SIZE            150

typedef struct _ocfs_free_log
{
	__u32 num_free_upds;
	__u8 pad[4];
	ocfs_free_bitmap free_bitmap[FREE_LOG_SIZE];
}
ocfs_free_log;

typedef struct _ocfs_delete_log
{
	__u64 node_num;
	__u64 ent_del;
	__u64 parent_dirnode_off;
	__u32 flags;
	__u8 pad[4];
}
ocfs_delete_log;

typedef struct _ocfs_recovery_log
{
	__u64 node_num;
}
ocfs_recovery_log;

#define  DISK_ALLOC_DIR_NODE      1
#define  DISK_ALLOC_EXTENT_NODE   2
#define  DISK_ALLOC_VOLUME        3

typedef struct _ocfs_alloc_log
{
	__u64 length;
	__u64 file_off;
	__u32 type;
	__u32 node_num;
}
ocfs_alloc_log;

typedef struct _ocfs_dir_log
{
	__u64 orig_off;
	__u64 saved_off;
	__u64 length;
}
ocfs_dir_log;

typedef struct _ocfs_lock_update
{
	__u64 orig_off;
	__u64 new_off;
}
ocfs_lock_update;

#define  LOCK_UPDATE_LOG_SIZE     450

typedef struct _ocfs_lock_log
{
	__u32 num_lock_upds;
	__u8 pad[4];
	ocfs_lock_update lock_upd[LOCK_UPDATE_LOG_SIZE];
}
ocfs_lock_log;

typedef struct _ocfs_bcast_rel_log
{
	__u64 lock_id;
}
ocfs_bcast_rel_log;

typedef struct _ocfs_cleanup_record
{
	__u64 log_id;
	__u32 log_type;
	__u8 pad[4];
	union
	{
		ocfs_lock_log lock;
		ocfs_alloc_log alloc;
		ocfs_bcast_rel_log bcast;
		ocfs_delete_log del;
		ocfs_free_log free;
	}
	rec;
}
ocfs_cleanup_record;

typedef struct _ocfs_log_record
{
	__u64 log_id;
	__u32 log_type;
	__u8 pad[4];
	union
	{
		ocfs_dir_log dir;
		ocfs_alloc_log alloc;
		ocfs_recovery_log recovery;
		ocfs_bcast_rel_log bcast;
		ocfs_delete_log del;
		ocfs_free_extent_log extent;
	}
	rec;
}
ocfs_log_record;

#define  LOG_RECOVER              1
#define  LOG_CLEANUP              2

#endif				/*  _OCFSTRANS_H_ */
