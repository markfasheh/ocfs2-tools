/* -*- mode: c; c-basic-offset: 9; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * ocfs1_fs_compat.h
 *
 * OCFS1 volume header definitions.  OCFS2 creates valid but unmountable
 * OCFS1 volume headers on the first two sectors of an OCFS2 volume.
 * This allows an OCFS1 volume to see the partition and cleanly fail to
 * mount it.
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
 *	    Manish Singh, Neeraj Goyal, Suchit Kaura, Joel Becker
 */

#ifndef _OCFS1_FS_COMPAT_H
#define _OCFS1_FS_COMPAT_H

#define MAX_VOL_SIGNATURE_LEN_V1          128
#define MAX_MOUNT_POINT_LEN_V1            128
#define MAX_VOL_ID_LENGTH_V1               16
#define MAX_VOL_LABEL_LEN_V1               64
#define MAX_CLUSTER_NAME_LEN_V1            64
#define MAX_IP_ADDR_LEN                    32
#define MAX_NODE_NAME_LENGTH               32

#define GUID_LEN                           32
#define HOSTID_LEN                         20
#define MACID_LEN                          12

#define OCFS1_MAJOR_VERSION              (2)
#define OCFS1_MINOR_VERSION              (0)
#define OCFS1_VOLUME_SIGNATURE		 "OracleCFS"

/*
 * OCFS1 superblock.  Lives at sector 0.
 */
typedef struct _ocfs1_vol_disk_hdr
{
/*00*/	__u32 minor_version;
	__u32 major_version;
/*08*/	__u8 signature[MAX_VOL_SIGNATURE_LEN_V1];
/*88*/	__u8 mount_point[MAX_MOUNT_POINT_LEN_V1];
/*108*/	__u64 serial_num;
/*110*/	__u64 device_size;
	__u64 start_off;
/*120*/	__u64 bitmap_off;
	__u64 publ_off;
/*130*/	__u64 vote_off;
	__u64 root_bitmap_off;
/*140*/	__u64 data_start_off;
	__u64 root_bitmap_size;
/*150*/	__u64 root_off;
	__u64 root_size;
/*160*/	__u64 cluster_size;
	__u64 num_nodes;
/*170*/	__u64 num_clusters;
	__u64 dir_node_size;
/*180*/	__u64 file_node_size;
	__u64 internal_off;
/*190*/	__u64 node_cfg_off;
	__u64 node_cfg_size;
/*1A0*/	__u64 new_cfg_off;
	__u32 prot_bits;
	__s32 excl_mount;
/*1B0*/
} ocfs1_vol_disk_hdr;


typedef struct _ocfs1_disk_lock
{
/*00*/	__u32 curr_master;
	__u8 file_lock;
	__u8 compat_pad[3];  /* Not in orignal definition.  Used to
				make the already existing alignment
				explicit */
	__u64 last_write_time;
/*10*/	__u64 last_read_time;
	__u32 writer_node_num;
	__u32 reader_node_num;
/*20*/	__u64 oin_node_map;
	__u64 dlock_seq_num;
/*30*/
} ocfs1_disk_lock;

/*
 * OCFS1 volume label.  Lives at sector 1.
 */
typedef struct _ocfs1_vol_label
{
/*00*/	ocfs1_disk_lock disk_lock;
/*30*/	__u8 label[MAX_VOL_LABEL_LEN_V1];
/*70*/	__u16 label_len;
/*72*/	__u8 vol_id[MAX_VOL_ID_LENGTH_V1];
/*82*/	__u16 vol_id_len;
/*84*/	__u8 cluster_name[MAX_CLUSTER_NAME_LEN_V1];
/*A4*/	__u16 cluster_name_len;
/*A6*/
} ocfs1_vol_label;


typedef struct _ocfs1_ipc_config_info
{
	__u8 type;
	__u8 ip_addr[MAX_IP_ADDR_LEN+1];
	__u32 ip_port;
	__u8 ip_mask[MAX_IP_ADDR_LEN+1];
} ocfs1_ipc_config_info;

typedef union _ocfs1_guid
{
	struct
	{
		char host_id[HOSTID_LEN];
		char mac_id[MACID_LEN];
	} id;
	__u8 guid[GUID_LEN];
} ocfs1_guid;

typedef struct _ocfs1_disk_node_config_info
{
	ocfs1_disk_lock disk_lock;
	__u8 node_name[MAX_NODE_NAME_LENGTH+1];
	ocfs1_guid guid;
	ocfs1_ipc_config_info ipc_config;
} ocfs1_disk_node_config_info;

#endif /* _OCFS1_FS_COMPAT_H */

