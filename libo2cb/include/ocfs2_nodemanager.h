/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dlmnm.h
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

#ifndef DLMNM_H
#define DLMNM_H

#include "ocfs2_heartbeat.h"

#define NM_MAX_IFACES            2
#define NM_MAX_NODES             255
#define NM_INVALID_SLOT_NUM      255

/* host name, group name, cluster name all 64 bytes */
#define NM_MAX_NAME_LEN          64    // __NEW_UTS_LEN


#define NM_GROUP_INODE_START    200000
#define NM_NODE_INODE_START     100000

enum {
	NM_CLUSTER_DOWN=0,
	NM_CLUSTER_UP
};

enum {
	NM_GROUP_NOT_READY=0,
	NM_GROUP_READY
};

enum {
	NM_Root = 1,
	NM_Cluster,
	NM_Node,
	NM_Group,
};




typedef struct _nm_network_iface
{
	__u16 ip_port;			/* for simplicity, just define exactly one port for this if */
	__u16 ip_version;
	union {
		__u32 ip_addr4;		/* IPv4 address in NBO */
		__u32 ip_addr6[4];	/* IPv6 address in NBO */
	} addr_u;
} nm_network_iface;

typedef struct _nm_node_info 
{
	__u16 node_num;
	__u16 pad1;
	__u32 pad2;
	char node_name[NM_MAX_NAME_LEN+1];
	char pad3[63];
	nm_network_iface ifaces[NM_MAX_IFACES];
} nm_node_info;

/* transaction file nm_op stuff */

#define NM_OP_MAGIC      0xbeaf
enum {
	NM_OP_CREATE_CLUSTER=123,
	NM_OP_DESTROY_CLUSTER,
	NM_OP_NAME_CLUSTER,
	NM_OP_ADD_CLUSTER_NODE,
	NM_OP_GET_CLUSTER_NUM_NODES,
	NM_OP_GET_NODE_INFO,
	NM_OP_CREATE_GROUP,
	NM_OP_GET_GROUP_INFO,
	NM_OP_ADD_GROUP_NODE,
	NM_OP_GET_GLOBAL_NODE_NUM
};

typedef struct _nm_group_change
{
	__u16 group_num;
	__u16 node_num;
	__u16 slot_num;
	char disk_uuid[CLUSTER_DISK_UUID_LEN+1];
	char name[NM_MAX_NAME_LEN+1];
} nm_group_change;

typedef struct _nm_op
{
	__u16 magic;
	__u16 opcode;
	__u32 pad1;
	union {
		__u16 index;
		char name[NM_MAX_NAME_LEN+1];
		nm_node_info node;
		nm_group_change gc;
	} arg_u;
} nm_op;

#endif /* DLMNM_H */
