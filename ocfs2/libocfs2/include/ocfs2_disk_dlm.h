/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * ocfs2_disk_dlm.h
 *
 * On-disk structures involved in disk publish/vote for OCFS2.
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License, version 2,  as published by the Free Software Foundation.
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
 *	    Manish Singh, Joel Becker
 */

#ifndef _OCFS2_DISK_DLM_H
#define _OCFS2_DISK_DLM_H

/*
 * On-disk IPC configuration for an OCFS2 node.
 */
typedef struct _ocfs_ipc_config_info
{
/*00*/	__u16 ip_version;		/* IP version in NBO */
	__u16 ip_port;			/* IP port in NBO */
	__u32 ip_reserved1;
	__u64 ip_reserved2;
/*10*/	union {
		__u32 ip_addr4;		/* IPv4 address in NBO */
		__u32 ip_addr6[4];	/* IPv6 address in NBO */
	} addr_u;
/*20*/
} ocfs_ipc_config_info;

/*
 * On-disk structure representing a Global Unique ID for an OCFS2 node.
 *
 * The GUID has two parts.  The host_id is a generally-randomly-unique
 * hex-as-ascii string of 20 characters (10 bytes).  The mad_id field
 * is, unsurprisingly, the MAC address of the network card that the
 * IPC mechanism will be using (the address in
 * ocfs_ipc_config_info.addr_u).  This should (ha-ha) provide a unique
 * identifier for a node in the OCFS2 cluster.  It has the added
 * benefit of detecting when a node has changed network cards
 * (host_id is the same, mac_id has changed) or when an identical
 * mac address is on a different mode (the converse).
 */
typedef union _ocfs_guid
{
/*00*/	struct
	{
		char host_id[OCFS2_GUID_HOSTID_LEN];
		char mac_id[OCFS2_GUID_MACID_LEN];
	} id;
	__u8 guid[OCFS2_GUID_LEN];
/*20*/
} ocfs_guid;

/*
 * On-disk configuration information for an OCFS2 node.  A node
 * populates its own info for other nodes to read and use.
 */
typedef struct _ocfs_node_config_info
{
/*00*/	ocfs2_disk_lock disk_lock;		/* Lock on the info */
/*30*/	ocfs_guid guid;				/* GUID */
/*50*/	ocfs_ipc_config_info ipc_config;	/* IPC info */
/*70*/	__u8 node_name[MAX_NODE_NAME_LENGTH+1]; /* Name */
/*91*/	__u8 name_pad[7];			/* Pad to align (UGH) */
/*98*/
} ocfs_node_config_info;

/*
 * On-disk ... for OCFS2.  FIXME this description.
 */
typedef struct _ocfs_node_config_hdr
{
/*00*/	ocfs2_disk_lock disk_lock;
/*30*/	__u8 signature[OCFS2_NODE_CONFIG_SIGN_LEN];
	__u32 version;
	__u16 num_nodes;
	__u16 reserved1;
/*40*/	__u32 last_node;
	__u32 onch_pad;
	__u64 cfg_seq_num;
/*50*/	
} ocfs_node_config_hdr;

/*
 * On-disk lock / state change request for OCFS2.
 */
typedef struct _ocfs_publish
{
/*00*/	__u64 time;		/* Time of publish */
	__s32 vote;
	__u32 dirty;		/* Is the node in a clean state */
/*10*/	__u32 vote_type;	/* Type required */
	__u32 mounted;		/* Does the publisher have it mounted */
/*18*/	__u32 vote_map[8];	/* Who needs to vote */
/*38*/	__u64 reserved1;
/*50*/	__u64 publ_seq_num;	/* Sequence for vote */
	__u64 lock_id;		/* Lock vote is requested for */
	/* last seq num used in comm voting */
/*60*/	__u64 comm_seq_num;
/*68*/
} ocfs_publish;

typedef struct _ocfs_vote
{
/*00*/	__u8 type;		/* Vote type */
	__u8 node;		/* Node voting */
	__u8 reserved1[30];	/* used to be vote[32] */
/*20*/	__u64 vote_seq_num;	/* Vote sequence */
	__u64 lock_id;		/* Lock being voted on */
/*30*/	__u8 open_handle;	/* Does the voter have it open */
	__u8 ov_pad[7];
/*38*/	
} ocfs_vote;

#endif  /* _OCFS2_DISK_DLM_H */
