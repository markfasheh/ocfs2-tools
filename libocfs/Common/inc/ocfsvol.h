/*
 * ocfsvol.h
 *
 * On-disk structures. See format.h for disk layout.
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

#ifndef _OCFSVOL_H_
#define _OCFSVOL_H_

#define  OCFS_MINOR_VERSION              (2)
#define  OCFS_MAJOR_VERSION              (1)
#define  OCFS_MINOR_VER_STRING           "2"
#define  OCFS_MAJOR_VER_STRING           "1"

#define  OCFS2_MINOR_VERSION             (0)
#define  OCFS2_MAJOR_VERSION             (2)
#define  OCFS2_MINOR_VER_STRING          "0"
#define  OCFS2_MAJOR_VER_STRING          "2"

#define  OCFS_VOLUME_SIGNATURE           "OracleCFS"
#define  MAX_VOL_SIGNATURE_LEN		128
#define  MAX_MOUNT_POINT_LEN		128

typedef struct _ocfs_vol_disk_hdr		// CLASS
{
	__u32 minor_version;			// NUMBER RANGE(0,UINT_MAX)
	__u32 major_version;			// NUMBER RANGE(0,UINT_MAX)
	__u8 signature[MAX_VOL_SIGNATURE_LEN];	// CHAR[MAX_VOL_SIGNATURE_LEN]
	__u8 mount_point[MAX_MOUNT_POINT_LEN];	// CHAR[MAX_MOUNT_POINT_LEN]
	__u64 serial_num;			// NUMBER RANGE(0,ULONG_LONG_MAX)
	/* Size of the device in bytes */           
	__u64 device_size;			// NUMBER RANGE(0,ULONG_LONG_MAX)
	/* Start of the volume... typically 0 */    
	__u64 start_off;			// NUMBER RANGE(0,ULONG_LONG_MAX)
	/* Offset to Volume Bitmap... */            
	__u64 bitmap_off;			// NUMBER RANGE(0,ULONG_LONG_MAX)
	/* Offset to the Publish Sector */          
	__u64 publ_off;				// NUMBER RANGE(0,ULONG_LONG_MAX)
	/* Offset to the Vote Sector */             
	__u64 vote_off;				// NUMBER RANGE(0,ULONG_LONG_MAX)
	__u64 root_bitmap_off;			// NUMBER RANGE(0,ULONG_LONG_MAX)
	__u64 data_start_off;			// NUMBER RANGE(0,ULONG_LONG_MAX)
	__u64 root_bitmap_size;			// NUMBER RANGE(0,ULONG_LONG_MAX)
	__u64 root_off;				// NUMBER RANGE(0,ULONG_LONG_MAX)
	__u64 root_size;			// NUMBER RANGE(0,ULONG_LONG_MAX)
	/* Cluster size as specified during format */        
	__u64 cluster_size;			// CLUSTERSIZE
	/* Max number of nodes.... OCFS_MAXIMUM_NODES */
	__u64 num_nodes;			// NUMBER RANGE(0,32)
	/* Number of free clusters at format */
	__u64 num_clusters;			// NUMBER RANGE(0,ULONG_LONG_MAX)
	/* OCFS_DEFAULT_DIR_NODE_SIZE */
	__u64 dir_node_size;			// NUMBER RANGE(0,ULONG_LONG_MAX)
	/* OCFS_DEFAULT_FILE_NODE_SIZE */
	__u64 file_node_size;			// NUMBER RANGE(0,ULONG_LONG_MAX)
	__u64 internal_off;			// NUMBER RANGE(0,ULONG_LONG_MAX)
	/* Offset to Node Config */
	__u64 node_cfg_off;			// NUMBER RANGE(0,ULONG_LONG_MAX)
	/* Size of Node Config */
	__u64 node_cfg_size;			// NUMBER RANGE(0,ULONG_LONG_MAX)
	/* Offset to Node Config Lock */
	__u64 new_cfg_off;			// NUMBER RANGE(0,ULONG_LONG_MAX)
	__u32 prot_bits;			// PERMS
	__u32 uid;				// UID
	__u32 gid;				// GID
	__s32 excl_mount;			// NODENUM
	/* disk heartbeat time in ms */
	__u32 disk_hb;				// NUMBER RANGE(0, ULONG_MAX)
	/* node timeout in ms */
	__u32 hb_timeo;				// NUMBER RANGE(0, ULONG_MAX)
}
ocfs_vol_disk_hdr;				// END CLASS

#define DLOCK_FLAG_OPEN_MAP    (0x1)
#define DLOCK_FLAG_LOCK        (0x2)
#define DLOCK_FLAG_SEQ_NUM     (0x4)
#define DLOCK_FLAG_MASTER      (0x8)
#define DLOCK_FLAG_LAST_UPDATE (0x10)
#define DLOCK_FLAG_ALL         (DLOCK_FLAG_OPEN_MAP | DLOCK_FLAG_LOCK | \
                                DLOCK_FLAG_SEQ_NUM | DLOCK_FLAG_MASTER | \
                                DLOCK_FLAG_LAST_UPDATE)

typedef struct _ocfs_disk_lock			// CLASS
{
	__u32 curr_master;			// NODENUM
	__u8 file_lock;				// LOCKLEVEL
	__u64 last_write_time;			// NUMBER RANGE(0,ULONG_LONG_MAX)
	__u64 last_read_time;			// NUMBER RANGE(0,ULONG_LONG_MAX)
	__u32 writer_node_num;			// NODENUM
	__u32 reader_node_num;			// NODENUM
	__u64 oin_node_map;			// NODEBITMAP
	__u64 dlock_seq_num;			// NUMBER RANGE(0,ULONG_LONG_MAX)
}
ocfs_disk_lock;					// END CLASS

#define DISK_LOCK_CURRENT_MASTER(x)   ( ((ocfs_disk_lock *)x)->curr_master )
#define DISK_LOCK_OIN_MAP(x)          ( ((ocfs_disk_lock *)x)->oin_node_map )
#define DISK_LOCK_FILE_LOCK(x)        ( ((ocfs_disk_lock *)x)->file_lock )
#define DISK_LOCK_LAST_READ(x)        ( ((ocfs_disk_lock *)x)->last_read_time )
#define DISK_LOCK_LAST_WRITE(x)       ( ((ocfs_disk_lock *)x)->last_write_time )
#define DISK_LOCK_READER_NODE(x)      ( ((ocfs_disk_lock *)x)->reader_node_num )
#define DISK_LOCK_SEQNUM(x)           ( ((ocfs_disk_lock *)x)->dlock_seq_num )
#define DISK_LOCK_WRITER_NODE(x)      ( ((ocfs_disk_lock *)x)->writer_node_num )

#define MAX_VOL_ID_LENGTH		16
#define MAX_VOL_LABEL_LEN		64
#define MAX_CLUSTER_NAME_LEN		64

typedef struct _ocfs_vol_label			// CLASS
{
	ocfs_disk_lock disk_lock;		// DISKLOCK
	__u8 label[MAX_VOL_LABEL_LEN];		// CHAR[MAX_VOL_LABEL_LEN]
	__u16 label_len;			// NUMBER RANGE(0,MAX_VOL_LABEL_LEN)
	__u8 vol_id[MAX_VOL_ID_LENGTH];		// HEX[MAX_VOL_ID_LENGTH]
	__u16 vol_id_len;			// NUMBER RANGE(0,MAX_VOL_ID_LENGTH)
	__u8 cluster_name[MAX_CLUSTER_NAME_LEN];// CHAR[MAX_CLUSTER_NAME_LEN]
	__u16 cluster_name_len;			// NUMBER RANGE(0,MAX_CLUSTER_NAME_LEN)
}
ocfs_vol_label;					// END CLASS

#define OCFS_IPC_DEFAULT_PORT   7000

typedef struct _ocfs_ipc_config_info		// CLASS
{
	__u8 type;				// NUMBER RANGE(0, 255)
	__u8 ip_addr[MAX_IP_ADDR_LEN+1];	// CHAR[MAX_IP_ADDR_LEN+1]
	__u32 ip_port;				// NUMBER RANGE(0,ULONG_MAX)
	__u8 ip_mask[MAX_IP_ADDR_LEN+1];	// CHAR[MAX_IP_ADDR_LEN+1]
}
ocfs_ipc_config_info;				// END CLASS

#define OCFS_IPC_DLM_VERSION    0x0201

#define GUID_LEN		32
#define HOSTID_LEN		20
#define MACID_LEN		12
/* TODO this structure will break in 64-bit.... need to pack */
typedef union _ocfs_guid			// CLASS
{
	struct
	{
		char host_id[HOSTID_LEN];
		char mac_id[MACID_LEN];
	} id;
	__u8 guid[GUID_LEN];			// CHAR[GUID_LEN]
}
ocfs_guid;					// END CLASS

#define MAX_NODE_NAME_LENGTH    32

typedef struct _ocfs_disk_node_config_info	// CLASS
{
	ocfs_disk_lock disk_lock;		// DISKLOCK
	__u8 node_name[MAX_NODE_NAME_LENGTH+1];	// CHAR[MAX_NODE_NAME_LENGTH+1]
	ocfs_guid guid;				// GUID
	ocfs_ipc_config_info ipc_config;	// IPCONFIG
}
ocfs_disk_node_config_info;			// END CLASS

#define NODE_CONFIG_HDR_SIGN        "NODECFG"
#define NODE_CONFIG_SIGN_LEN        8
#define NODE_CONFIG_VER             2
#define NODE_MIN_SUPPORTED_VER      2

typedef struct _ocfs_node_config_hdr		// CLASS
{
	ocfs_disk_lock disk_lock;		// DISKLOCK
	__u8 signature[NODE_CONFIG_SIGN_LEN];	// CHAR[NODE_CONFIG_SIGN_LEN]
	__u32 version;				// NUMBER RANGE(0,ULONG_MAX)
	__u32 num_nodes;			// NUMBER RANGE(0,32)
	__u32 last_node;			// NUMBER RANGE(0,32)
	__u64 cfg_seq_num;			// NUMBER RANGE(0,ULONG_LONG_MAX)
}
OCFS_GCC_ATTR_PACKALGN
ocfs_node_config_hdr;				// END CLASS

/*
** CDSL
*/
#define OCFS_CDSL_CREATE        (0x1)
#define OCFS_CDSL_DELETE        (0x2)
#define OCFS_CDSL_REVERT        (0x3)

#define OCFS_FLAG_CDSL_FILE     (0x1)
#define OCFS_FLAG_CDSL_DIR      (0x2)

typedef struct _ocfs_cdsl
{
	__u8 name[1024];
	__u32 flags;
	__u32 operation;
}
ocfs_cdsl;

#endif				/*  _OCFSVOL_H_ */
