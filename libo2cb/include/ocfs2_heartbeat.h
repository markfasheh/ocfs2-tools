/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * ocfs2_heartbeat.h
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
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
 */

#ifndef _OCFS2_HEARTBEAT_H
#define _OCFS2_HEARTBEAT_H

#define CLUSTER_DISK_UUID_LEN      32      // 16 byte binary == 32 char hex string


#define HB_OP_MAGIC      0xf00d
enum {
	HB_OP_START_DISK_HEARTBEAT=371,
	HB_OP_GET_NODE_MAP
};

typedef struct _hb_op
{
	__u16 magic;
	__u16 opcode;
	__u32 fd;
	char disk_uuid[CLUSTER_DISK_UUID_LEN+1];
	char pad1[15];  /* Pad to the __u16 following it */
	__u16 group_num;
	__u32 bits;
	__u32 blocks;
	__u64 start;
} hb_op;

typedef struct _hb_disk_heartbeat_block
{
	__u64 time;
} hb_disk_heartbeat_block;

#endif /* _OCFS2_HEARTBEAT_H */
