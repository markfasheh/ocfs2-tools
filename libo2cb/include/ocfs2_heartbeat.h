/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dlmhb.h
 *
 * Function prototypes
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
 *
 * Authors: Kurt Hackel, Mark Fasheh, Sunil Mushran, Wim Coekaerts,
 *	    Manish Singh, Neeraj Goyal, Suchit Kaura
 */

#ifndef DLMHB_H
#define DLMHB_H

#define CLUSTER_DISK_UUID_LEN      32      // 16 byte binary == 32 char hex string

enum {
	HB_NODE_STATE_INIT = 0,
	HB_NODE_STATE_DOWN,
	HB_NODE_STATE_UP
};


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

enum {
	HB_TYPE_DISK = 0,
	HB_TYPE_NET
};


/* callback stuff */

enum {
	HB_NODE_DOWN_CB = 0,
	HB_NODE_UP_CB,
	HB_NODE_RESPONDED_CB,    // this one is very chatty
	HB_NUM_CB
};

enum {
	HB_Root = 1,
	HB_Disk,
	HB_WriteOpArraySize
};

typedef struct _hb_disk_heartbeat_block
{
	__u64 time;
} hb_disk_heartbeat_block;


// number of initial allowed misses 
#define HB_INITIAL_DISK_MARGIN     60
#define HB_INITIAL_NET_MARGIN      60

// number of allowed misses in steady state
#define HB_DISK_MARGIN             30
#define HB_NET_MARGIN              30

#endif /* DLMHB_H */
