/*
 * ocfsdisk.h
 *
 * Defines disk-based structures
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

#ifndef  _OCFSDISK_H_
#define  _OCFSDISK_H_

typedef struct _ocfs_alloc_ext		// CLASS
{
	/* Starting offset within the file */
	__u64 file_off;			// DISKPTR
	/* Number of bytes used by this alloc */
	__u64 num_bytes;		// NUMBER RANGE(0,ULONG_LONG_MAX)
	/* Physical Disk Offset */
	__u64 disk_off;			// DISKPTR
}
ocfs_alloc_ext;				// END CLASS

typedef struct _ocfs_publish		// CLASS
{
	__u64 time;			// NUMBER RANGE(0,ULONG_LONG_MAX)
	__s32 vote;			// BOOL
	bool dirty;			// BOOL
	__u32 vote_type;		// FILEFLAG
	__u64 vote_map;			// NODEBITMAP
	__u64 publ_seq_num;		// NUMBER RANGE(0,ULONG_LONG_MAX)
	__u64 dir_ent;			// NUMBER RANGE(0,ULONG_LONG_MAX)
	__u8 hbm[OCFS_MAXIMUM_NODES];	// UNUSED
	/* last seq num used in comm voting */
	__u64 comm_seq_num;		// NUMBER RANGE(0,ULONG_LONG_MAX)
}
OCFS_GCC_ATTR_PACKALGN
ocfs_publish;				// END CLASS

typedef struct _ocfs_vote		// CLASS
{
	__u8 vote[OCFS_MAXIMUM_NODES];	// VOTEFLAG[OCFS_MAXIMUM_NODES]
	__u64 vote_seq_num;		// NUMBER RANGE(0,ULONG_LONG_MAX)
	__u64 dir_ent;			// NUMBER RANGE(0,ULONG_LONG_MAX)
	__u8 open_handle;		// BOOL
}
OCFS_GCC_ATTR_PACKALGN
ocfs_vote;				// END CLASS

typedef struct _ocfs_file_entry		// CLASS
{
	ocfs_disk_lock disk_lock;	// DISKLOCK
	__u8 signature[8];		// CHAR[8]
	bool local_ext;			// BOOL
	__u8 next_free_ext;		// NUMBER RANGE(0,OCFS_MAX_FILE_ENTRY_EXTENTS)
	__s8 next_del;			// DIRNODEINDEX
	__s32 granularity;		// NUMBER RANGE(-1,3)
	__u8 filename[OCFS_MAX_FILENAME_LENGTH];	// CHAR[OCFS_MAX_FILENAME_LENGTH]
	__u16 filename_len;		// NUMBER RANGE(0,OCFS_MAX_FILENAME_LENGTH)
	__u64 file_size;		// NUMBER RANGE(0,ULONG_LONG_MAX)
	__u64 alloc_size;		// NUMBER RANGE(0,ULONG_LONG_MAX)
	__u64 create_time;		// DATE
	__u64 modify_time;		// DATE
	ocfs_alloc_ext extents[OCFS_MAX_FILE_ENTRY_EXTENTS];	// EXTENT[OCFS_MAX_FILE_ENTRY_EXTENTS]
	__u64 dir_node_ptr;		// NUMBER RANGE(0,ULONG_LONG_MAX)
	__u64 this_sector;		// NUMBER RANGE(0,ULONG_LONG_MAX)
	__u64 last_ext_ptr;		// NUMBER RANGE(0,ULONG_LONG_MAX)
	__u32 sync_flags;		// SYNCFLAG
	__u32 link_cnt;			// NUMBER RANGE(0,UINT_MAX)
	__u32 attribs;			// ATTRIBS
	__u32 prot_bits;		// PERMS
	__u32 uid;			// UID
	__u32 gid;			// GID
	__u16 dev_major;		// NUMBER RANGE(0,65535)   
	__u16 dev_minor;		// NUMBER RANGE(0,65535)
	/* 32-bit: sizeof(fe) = 484 bytes */
	/* 64-bit: sizeof(fe) = 488 bytes */
	/* Need to account for that fact when the struct is extended. */
}
ocfs_file_entry;			// END CLASS

/* not sizeof-safe across platforms */
typedef struct _ocfs_index_node
{
	__u64 down_ptr;
	__u64 file_ent_ptr;
	__u8 name_len;
	__u8 name[1];
}
OCFS_GCC_ATTR_PACKALGN
ocfs_index_node;

typedef struct _ocfs_index_hdr
{
	ocfs_disk_lock disk_lock;
	__u64 signature;
	__s64 up_tree_ptr;	/* Pointer to parent of this dnode */
	__u64 node_disk_off;
	__u8 state;		/* In recovery, needs recovery etc */
	__u64 down_ptr	OCFS_GCC_ATTR_ALIGNED;
	__u8 num_ents;;		/* Number of extents in this Node */
	__u8 depth;		/* Depth of this Node from root of the btree */
	__u8 num_ent_used;	/* Num of entries in the dir blk used up. */
	__u8 dir_node_flags;	/* Flags */
	__u8 sync_flags;		/* Flags */
	__u8 index[256];
	__u8 reserved[161];
	__u8 file_ent[1];	/* 63 entries here with 32K DIR_NODE size */
}
OCFS_GCC_ATTR_PACKED
ocfs_index_hdr;

/* not sizeof-safe across platforms */
typedef struct _ocfs_dir_node		// CLASS
{
	ocfs_disk_lock disk_lock;	// DISKLOCK
	__u8 signature[8];		// CHAR[8]
	__u64 alloc_file_off;		// NUMBER RANGE(0,ULONG_LONG_MAX) 
	__u32 alloc_node;		// NUMBER RANGE(0,31)
	__u64 free_node_ptr;		// DISKPTR
	__u64 node_disk_off;		// DISKPTR
	__s64 next_node_ptr;		// DISKPTR 
	__s64 indx_node_ptr;		// DISKPTR
	__s64 next_del_ent_node;	// DISKPTR
	__s64 head_del_ent_node;	// DISKPTR
	__u8 first_del;			// DIRNODEINDEX
	__u8 num_del;			// NUMBER RANGE(0,254)
	__u8 num_ents;			// NUMBER RANGE(0,254)
	__u8 depth;			// UNUSED
	__u8 num_ent_used;		// NUMBER RANGE(0,254)
	__u8 dir_node_flags;		// DIRFLAG
	__u8 sync_flags;		// NUMBER RANGE(0,0)
	__u8 index[256];		// DIRINDEX
	__u8 index_dirty;		// NUMBER RANGE(0,1)
	__u8 bad_off;			// NUMBER RANGE(0,254)
	__u8 reserved[127];		// UNUSED
	__u8 file_ent[1];		// UNUSED
}
OCFS_GCC_ATTR_PACKALGN
ocfs_dir_node;				// END CLASS

typedef struct _ocfs_vol_node_map
{
	__u64 time[OCFS_MAXIMUM_NODES];
	__u64 scan_time[OCFS_MAXIMUM_NODES];
	__u8 scan_rate[OCFS_MAXIMUM_NODES];
#ifdef UNUSED
	__u8 exp_scan_rate[OCFS_MAXIMUM_NODES];
	__u64 exp_rate_chng_time[OCFS_MAXIMUM_NODES];
#endif
	__u32 miss_cnt[OCFS_MAXIMUM_NODES];
	atomic_t dismount[OCFS_MAXIMUM_NODES];
	__u64 largest_seq_num;
}
ocfs_vol_node_map;

typedef struct _ocfs_vol_layout
{
	__u64 start_off;
	__u32 num_nodes;
	__u32 cluster_size;
	__u8 mount_point[MAX_MOUNT_POINT_LEN];
	__u8 vol_id[MAX_VOL_ID_LENGTH];
	__u8 label[MAX_VOL_LABEL_LEN];
	__u32 label_len;
	__u64 size;
	__u64 root_start_off;
	__u64 serial_num;
	__u64 root_size;
	__u64 publ_sect_off;
	__u64 vote_sect_off;
	__u64 root_bitmap_off;
	__u64 root_bitmap_size;
	__u64 data_start_off;
	__u64 num_clusters;
	__u64 root_int_off;
	__u64 dir_node_size;
	__u64 file_node_size;
	__u64 bitmap_off;
	__u64 node_cfg_off;
	__u64 node_cfg_size;
	__u64 new_cfg_off;
	__u32 prot_bits;
	__u32 uid;
	__u32 gid;
	__u32 disk_hb;
	__u32 hb_timeo;
}
ocfs_vol_layout;

typedef struct _ocfs_extent_group	// CLASS
{
	__u8 signature[8];		// CHAR ARRAY[8]
	/* 0 when init, -1 when full */
	__s32 next_free_ext;		// NUMBER RANGE(-1,INT_MAX)
	/* Currently available sector for use */
	__u32 curr_sect;		// NUMBER RANGE(0,UINT_MAX)
	/* Maximum Number of Sectors */
	__u32 max_sects;		// NUMBER RANGE(0,UINT_MAX)
	/* Type of this sector... either */
	/*  Actual Data or a Ptr to another location */
	__u32 type;			// EXTENTTYPE
	/* Number of leaf levels */
	__s32 granularity;		// NUMBER RANGE(-1,INT_MAX)
	__u32 alloc_node;		// NODENUM
	__u64 this_ext;			// DISKPTR
	__u64 next_data_ext;		// DISKPTR
	__u64 alloc_file_off;		// DISKPTR
	__u64 last_ext_ptr;		// DISKPTR
	__u64 up_hdr_node_ptr;		// DISKPTR
	ocfs_alloc_ext extents[OCFS_MAX_DATA_EXTENTS];	// EXTENT[OCFS_MAX_DATA_EXTENTS]
}
ocfs_extent_group;			// END CLASS

typedef struct _ocfs_bitmap_lock
{
    ocfs_disk_lock disk_lock;
    __u32 used_bits;
}
OCFS_GCC_ATTR_PACKALGN
ocfs_bitmap_lock;
#endif /*_OCFSDISK_H_ */
