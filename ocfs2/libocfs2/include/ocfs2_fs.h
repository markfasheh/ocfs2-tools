/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * ocfs2_fs.h
 *
 * On-disk structures for OCFS2.
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

#ifndef _OCFS2_FS_H
#define _OCFS2_FS_H

/* Version */
#define OCFS2_MAJOR_REV_LEVEL		2
#define OCFS2_MINOR_REV_LEVEL          	0

/*
 * An OCFS2 volume starts this way:
 * Sector 0: Valid ocfs1_vol_disk_hdr that cleanly fails to mount OCFS.
 * Sector 1: Valid ocfs1_vol_label that cleanly fails to mount OCFS.
 * Block OCFS2_SUPER_BLOCK_BLKNO: OCFS2 superblock.
 *
 * All other structures are found from the superblock information.
 *
 * OCFS2_SUPER_BLOCK_BLKNO is in blocks, not sectors.  eg, for a
 * blocksize of 2K, it is 4096 bytes into disk.
 */
#define OCFS2_SUPER_BLOCK_BLKNO		2

/*
 * As OCFS2 has a minimum clustersize of 4K, it has a maximum blocksize
 * of 4K
 */
#define OCFS2_MAX_BLOCKSIZE		4096

/* Object signatures */
#define OCFS2_SUPER_BLOCK_SIGNATURE	"OCFSV2"
#define OCFS2_FILE_ENTRY_SIGNATURE	"INODE01"
#define OCFS2_EXTENT_BLOCK_SIGNATURE	"EXBLK01"

/* Compatibility flags */
#define OCFS2_HAS_COMPAT_FEATURE(sb,mask)			\
	( OCFS2_SB(sb)->s_feature_compat & (mask) )
#define OCFS2_HAS_RO_COMPAT_FEATURE(sb,mask)			\
	( OCFS2_SB(sb)->s_feature_ro_compat & (mask) )
#define OCFS2_HAS_INCOMPAT_FEATURE(sb,mask)			\
	( OCFS2_SB(sb)->s_feature_incompat & (mask) )
#define OCFS2_SET_COMPAT_FEATURE(sb,mask)			\
	OCFS2_SB(sb)->s_feature_compat |= (mask)
#define OCFS2_SET_RO_COMPAT_FEATURE(sb,mask)			\
	OCFS2_SB(sb)->s_feature_ro_compat |= (mask)
#define OCFS2_SET_INCOMPAT_FEATURE(sb,mask)			\
	OCFS2_SB(sb)->s_feature_incompat |= (mask)
#define OCFS2_CLEAR_COMPAT_FEATURE(sb,mask)			\
	OCFS2_SB(sb)->s_feature_compat &= ~(mask)
#define OCFS2_CLEAR_RO_COMPAT_FEATURE(sb,mask)			\
	OCFS2_SB(sb)->s_feature_ro_compat &= ~(mask)
#define OCFS2_CLEAR_INCOMPAT_FEATURE(sb,mask)			\
	OCFS2_SB(sb)->s_feature_incompat &= ~(mask)

#define OCFS2_FEATURE_COMPAT_SUPP	0
#define OCFS2_FEATURE_INCOMPAT_SUPP	0
#define OCFS2_FEATURE_RO_COMPAT_SUPP	0


/*
 * Flags on ocfs2_dinode.i_flags
 */
#define OCFS2_VALID_FL		(0x00000001)	/* Inode is valid */
#define OCFS2_UNUSED2_FL	(0x00000002)
#define OCFS2_ORPHANED_FL	(0x00000004)	/* On the orphan list */
#define OCFS2_UNUSED3_FL	(0x00000008)
/* System inode flags */
#define OCFS2_SYSTEM_FL		(0x00000010)	/* System inode */
#define OCFS2_SUPER_BLOCK_FL	(0x00000020)	/* Super block */
#define OCFS2_LOCAL_ALLOC_FL	(0x00000040)	/* Node local alloc bitmap */
#define OCFS2_BITMAP_FL		(0x00000080)	/* Allocation bitmap */
#define OCFS2_JOURNAL_FL	(0x00000100)	/* Node journal */
#define OCFS2_DLM_FL		(0x00000200)	/* DLM area */
	

/* Limit of space in ocfs2_dir_entry */
#define OCFS2_MAX_FILENAME_LENGTH       255

/* Limit of node map bits in ocfs2_disk_lock */
#define OCFS2_MAX_NODES			256

#define MAX_VOL_ID_LENGTH               16
#define MAX_VOL_LABEL_LEN               64
#define MAX_CLUSTER_NAME_LEN            64


#define ONE_MEGA_BYTE           	(1 * 1024 * 1024)   /* in bytes */
#define OCFS2_DEFAULT_JOURNAL_SIZE	(8 * ONE_MEGA_BYTE)


/* System file index */
enum {
	BAD_BLOCK_SYSTEM_INODE = 0,
	GLOBAL_INODE_ALLOC_SYSTEM_INODE,
	GLOBAL_INODE_ALLOC_BITMAP_SYSTEM_INODE,
	DLM_SYSTEM_INODE,
#define OCFS2_FIRST_ONLINE_SYSTEM_INODE DLM_SYSTEM_INODE
	GLOBAL_BITMAP_SYSTEM_INODE,
	ORPHAN_DIR_SYSTEM_INODE,
#define OCFS2_LAST_GLOBAL_SYSTEM_INODE ORPHAN_DIR_SYSTEM_INODE
	EXTENT_ALLOC_SYSTEM_INODE,
	EXTENT_ALLOC_BITMAP_SYSTEM_INODE,
	INODE_ALLOC_SYSTEM_INODE,
	INODE_ALLOC_BITMAP_SYSTEM_INODE,
	JOURNAL_SYSTEM_INODE,
	LOCAL_ALLOC_SYSTEM_INODE,
	NUM_SYSTEM_INODES
};

static char *ocfs2_system_inode_names[NUM_SYSTEM_INODES] = {
	/* Global system inodes (single copy) */
	/* The first three are only used from userspace mfks/tunefs */
	[BAD_BLOCK_SYSTEM_INODE]		"bad_blocks",
	[GLOBAL_INODE_ALLOC_SYSTEM_INODE] 	"global_inode_alloc",
	[GLOBAL_INODE_ALLOC_BITMAP_SYSTEM_INODE]	"global_inode_alloc_bitmap",

	/* These are used by the running filesystem */
	[DLM_SYSTEM_INODE]			"dlm",
	[GLOBAL_BITMAP_SYSTEM_INODE]		"global_bitmap",
	[ORPHAN_DIR_SYSTEM_INODE]		"orphan_dir",

	/* Node-specific system inodes (one copy per node) */
	[EXTENT_ALLOC_SYSTEM_INODE]		"extent_alloc:%04d",
	[EXTENT_ALLOC_BITMAP_SYSTEM_INODE]	"extent_alloc_bitmap:%04d",
	[INODE_ALLOC_SYSTEM_INODE]		"inode_alloc:%04d",
	[INODE_ALLOC_BITMAP_SYSTEM_INODE]	"inode_alloc_bitmap:%04d",
	[JOURNAL_SYSTEM_INODE]			"journal:%04d",
	[LOCAL_ALLOC_SYSTEM_INODE]		"local_alloc:%04d"
};


/* Default size for the local alloc bitmap */
#define OCFS2_LOCAL_BITMAP_DEFAULT_SIZE		256

/*
 * OCFS2 directory file types.  Only the low 3 bits are used.  The
 * other bits are reserved for now.
 */
#define OCFS2_FT_UNKNOWN	0
#define OCFS2_FT_REG_FILE	1
#define OCFS2_FT_DIR		2
#define OCFS2_FT_CHRDEV		3
#define OCFS2_FT_BLKDEV		4
#define OCFS2_FT_FIFO		5
#define OCFS2_FT_SOCK		6
#define OCFS2_FT_SYMLINK	7

#define OCFS2_FT_MAX		8

/*
 * OCFS2_DIR_PAD defines the directory entries boundaries
 *
 * NOTE: It must be a multiple of 4
 */
#define OCFS2_DIR_PAD			4
#define OCFS2_DIR_ROUND			(OCFS2_DIR_PAD - 1)
#define OCFS2_DIR_REC_LEN(name_len)	(((name_len) + 12 + \
                                          OCFS2_DIR_ROUND) & \
					 ~OCFS2_DIR_ROUND)
#define OCFS2_LINK_MAX		32000

#define S_SHIFT			12
static unsigned char ocfs_type_by_mode[S_IFMT >> S_SHIFT] = {
	[S_IFREG >> S_SHIFT]    OCFS2_FT_REG_FILE,
	[S_IFDIR >> S_SHIFT]    OCFS2_FT_DIR,
	[S_IFCHR >> S_SHIFT]    OCFS2_FT_CHRDEV,
	[S_IFBLK >> S_SHIFT]    OCFS2_FT_BLKDEV,
	[S_IFIFO >> S_SHIFT]    OCFS2_FT_FIFO,
	[S_IFSOCK >> S_SHIFT]   OCFS2_FT_SOCK,
	[S_IFLNK >> S_SHIFT]    OCFS2_FT_SYMLINK,
};


/*
 * Convenience casts
 */
#define OCFS2_RAW_SB(dinode)	(&((dinode)->id2.i_super))
#define DISK_LOCK(dinode)	(&((dinode)->i_disk_lock))
#define LOCAL_ALLOC(dinode)	(&((dinode)->id2.i_lab))

/* TODO: change these?  */
#define OCFS2_NODE_CONFIG_HDR_SIGN	"NODECFG"
#define OCFS2_NODE_CONFIG_SIGN_LEN	8
#define OCFS2_NODE_CONFIG_VER		2
#define OCFS2_NODE_MIN_SUPPORTED_VER	2

#define MAX_NODE_NAME_LENGTH	32

#define OCFS2_GUID_HOSTID_LEN	20
#define OCFS2_GUID_MACID_LEN	12
#define OCFS2_GUID_LEN		(OCFS2_GUID_HOSTID_LEN + OCFS2_GUID_MACID_LEN)



/*
 * On disk extent record for OCFS2
 * It describes a range of clusters on disk.
 */
typedef struct _ocfs2_extent_rec {
/*00*/	__u32 e_cpos;		/* Offset into the file, in clusters */
	__u32 e_clusters;	/* Clusters covered by this extent */
	__u64 e_blkno;		/* Physical disk offset, in blocks */
/*10*/
} ocfs2_extent_rec;	

/*
 * On disk extent list for OCFS2 (node in the tree).  Note that this
 * is contained inside ocfs2_dinode or ocfs2_extent_block, so the
 * offsets are relative to ocfs2_dinode.id2.i_list or
 * ocfs2_extent_block.h_list, respectively.
 */
typedef struct _ocfs2_extent_list {
/*00*/	__s16 l_tree_depth;		/* Extent tree depth from this
					   point.  -1 means data extents
					   hang directly off this
					   header (a leaf) */
	__u16 l_count;			/* Number of extent records */
	__u16 l_next_free_rec;		/* Next unused extent slot */
	__u16 l_reserved1;
	__u64 l_reserved2;		/* Pad to
					   sizeof(ocfs2_extent_rec) */
/*10*/	ocfs2_extent_rec l_recs[0];	/* Extent records */
} ocfs2_extent_list;

/*
 * On disk extent block (indirect block) for OCFS2
 */
typedef struct _ocfs2_extent_block
{
/*00*/	__u8 h_signature[8];		/* Signature for verification */
	__u64 h_suballoc_blkno;		/* Node suballocator offset,
					   in blocks */
/*10*/	__u16 h_suballoc_node;		/* Node suballocator this
					   extent_header belongs to */
	__u16 h_reserved1;
	__u32 h_reserved2;
	__u64 h_blkno;			/* Offset on disk, in blocks */
/*20*/	__u64 h_parent_blk;		/* Offset on disk, in blocks,
					   of this block's parent in the
					   tree */
	__u64 h_next_leaf_blk;		/* Offset on disk, in blocks,
					   of next leaf header pointing
					   to data */
/*30*/	ocfs2_extent_list h_list;	/* Extent record list */
/* Actual on-disk size is one block */
} ocfs2_extent_block;

/*
 * On disk lock structure for OCFS2
 */
typedef struct _ocfs2_disk_lock
{
/*00*/	__u32 dl_master;	/* Node number of current master */
	__u8 dl_level;		/* Lock level */
	__u8 dl_reserved1[3];	/* Pad to u64 */
	__u64 dl_seq_num;	/* Lock transaction seqnum */
/*10*/	__u32 dl_node_map[8];	/* Bitmap of interested nodes,
				   was __u32 */ 
/*30*/
} ocfs2_disk_lock;

/*
 * On disk superblock for OCFS2
 * Note that it is contained inside an ocfs2_dinode, so all offsets
 * are relative to the start of ocfs2_dinode.id2.
 */
typedef struct _ocfs2_super_block {
/*00*/	__u16 s_major_rev_level;
	__u16 s_minor_rev_level;
	__u16 s_mnt_count;
	__s16 s_max_mnt_count;
	__u16 s_state;			/* File system state */
	__u16 s_errors;			/* Behaviour when detecting errors */
	__u32 s_checkinterval;		/* Max time between checks */
/*10*/	__u64 s_lastcheck;		/* Time of last check */
	__u32 s_creator_os;		/* OS */
	__u32 s_feature_compat;		/* Compatible feature set */
/*20*/	__u32 s_feature_incompat;	/* Incompatible feature set */
	__u32 s_feature_ro_compat;	/* Readonly-compatible feature set */
	__u64 s_root_blkno;		/* Offset, in blocks, of root directory
					   dinode */
/*30*/	__u64 s_system_dir_blkno;	/* Offset, in blocks, of system
					   directory dinode */
	__u32 s_blocksize_bits;		/* Blocksize for this fs */
	__u32 s_clustersize_bits;	/* Clustersize for this fs */
/*40*/	__u32 s_max_nodes;		/* Max nodes in this cluster before
					   tunefs required */
	__u32 s_reserved1;
	__u64 s_reserved2;
/*50*/	__u8  s_label[64];		/* Label for mounting, etc. */
/*90*/	__u8  s_uuid[16];		/* Was vol_id */
/*A0*/
} ocfs2_super_block;

/*
 * Local allocation bitmap for OCFS2 nodes
 * Node that it exists inside an ocfs2_dinode, so all offsets are
 * relative to the start of ocfs2_dinode.id2.
 */
typedef struct _ocfs2_local_alloc
{
/*00*/	__u32 la_bm_off;	/* Starting bit offset in main bitmap */
	/* Do we want to use id1.bitmap1? */
	__u16 la_bm_bits;	/* Number of bits from main bitmap */
	__u16 la_bits_set;	/* Number of set bits */
	__u16 la_size;		/* Size of included bitmap, in bytes */
	__u16 la_reserved1;
	__u32 la_reserved2;
/*10*/	__u8 la_bitmap[0];
} ocfs2_local_alloc;

/*
 * On disk inode for OCFS2
 */
typedef struct _ocfs2_dinode {
/*00*/	__u8 i_signature[8];		/* Signature for validation */
	__u32 i_generation;		/* Generation number */
	__u16 i_reserved1;
	__u16 i_suballoc_node;		/* Node suballocater this inode
					   belongs to */
/*10*/	__u64 i_suballoc_blkno;		/* Node suballocator offset,
       					   in blocks */
/*18*/	ocfs2_disk_lock i_disk_lock;	/* Lock structure */
/*48*/	__u32 i_uid;			/* Owner UID */
	__u32 i_gid;			/* Owning GID */
/*50*/	__u64 i_size;			/* Size in bytes */
	__u16 i_mode;			/* File mode */
	__u16 i_links_count;		/* Links count */
	__u32 i_flags;			/* File flags */
/*60*/	__u64 i_atime;			/* Access time */
	__u64 i_ctime;			/* Creation time */
/*70*/	__u64 i_mtime;			/* Modification time */
	__u64 i_dtime;			/* Deletion time */
/*80*/	__u64 i_blkno;			/* Offset on disk, in blocks */
	__u32 i_clusters;		/* Cluster count */
	__u32 i_reserved2;
/*90*/	__u64 i_last_eb_blk;		/* Pointer to last extent
					   block */
	__u64 i_reserved3;
/*A0*/	__u64 i_reserved4;
	__u64 i_reserved5;
/*B0*/	__u64 i_reserved6;
	union {
		__u64 i_pad1;		/* Generic way to refer to this 64bit
					   union */
		struct {
			__u64 i_rdev;	/* Device number */
		} dev1;
		struct {		/* Info for bitmap system inodes */
			__u32 i_used;	/* Bits (ie, clusters) used  */
			__u32 i_total;	/* Total bits (clusters) available */
		} bitmap1;
	} id1;				/* Inode type dependant 1 */
/*C0*/	union {
		ocfs2_super_block i_super;
                ocfs2_local_alloc i_lab;
		ocfs2_extent_list i_list;
	} id2;
/* Actual on-disk size is one block */
} ocfs2_dinode;

/*
 * On-disk directory entry structure for OCFS2
 */
struct ocfs2_dir_entry {
/*00*/	__u64   inode;                  /* Inode number */
	__u16   rec_len;                /* Directory entry length */
	__u8    name_len;               /* Name length */
	__u8    file_type;
/*0C*/	char    name[OCFS2_MAX_FILENAME_LENGTH];    /* File name */
/* Actual on-disk length specified by rec_len */
};

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
	__u32 num_nodes;
/*40*/	__u32 last_node;
	__u32 onch_pad;
	__u64 cfg_seq_num;
/*50*/	
} ocfs_node_config_hdr;


#ifdef __KERNEL__
static inline int ocfs2_extent_recs_per_inode(struct super_block *sb)
{
	int size;

	size = sb->s_blocksize -
		offsetof(struct _ocfs2_dinode, id2.i_list.l_recs);

	return size / sizeof(struct _ocfs2_extent_rec);
}

static inline int ocfs2_extent_recs_per_eb(struct super_block *sb)
{
	int size;

	size = sb->s_blocksize -
		offsetof(struct _ocfs2_extent_block, h_list.l_recs);

	return size / sizeof(struct _ocfs2_extent_rec);
}

static inline int ocfs2_local_alloc_size(struct super_block *sb)
{
	/*
	 * Perhaps change one day when we want to be dynamic
	 * based on sb->s_blocksize.
	 */
	return OCFS2_LOCAL_BITMAP_DEFAULT_SIZE;
}
#else
static inline int ocfs2_extent_recs_per_inode(int blocksize)
{
	int size;

	size = blocksize -
		offsetof(struct _ocfs2_dinode, id2.i_list.l_recs);

	return size / sizeof(struct _ocfs2_extent_rec);
}

static inline int ocfs2_extent_recs_per_eb(int blocksize)
{
	int size;

	size = blocksize -
		offsetof(struct _ocfs2_extent_block, h_list.l_recs);

	return size / sizeof(struct _ocfs2_extent_rec);
}

static inline int ocfs2_local_alloc_size(int blocksize)
{
	return OCFS2_LOCAL_BITMAP_DEFAULT_SIZE;
}
#endif  /* __KERNEL__ */


static inline int ocfs2_system_inode_is_global(int type)
{
	return ((type >= 0) &&
		(type <= OCFS2_LAST_GLOBAL_SYSTEM_INODE));
}

static inline int ocfs2_sprintf_system_inode_name(char *buf, int len,
						  int type, int node)
{
	int chars;

        /*
         * Global system inodes can only have one copy.  Everything
         * after OCFS_LAST_GLOBAL_SYSTEM_INODE in the system inode
         * list has a copy per node.
         */
	if (type <= OCFS2_LAST_GLOBAL_SYSTEM_INODE)
		chars = snprintf(buf, len,
				 ocfs2_system_inode_names[type]);
	else
		chars = snprintf(buf, len,
				 ocfs2_system_inode_names[type], node);

	return chars;
}

static inline void ocfs_set_de_type(struct ocfs2_dir_entry *de,
				    umode_t mode)
{
	de->file_type = ocfs_type_by_mode[(mode & S_IFMT)>>S_SHIFT];
}

#endif  /* _OCFS2_FS_H */

