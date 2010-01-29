/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * ocfs2.h
 *
 * Filesystem object routines for the OCFS2 userspace library.
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
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
 * Authors: Joel Becker
 */

#ifndef _FILESYS_H
#define _FILESYS_H

#ifndef _XOPEN_SOURCE
# define _XOPEN_SOURCE 600
#endif
#ifndef _LARGEFILE64_SOURCE
# define _LARGEFILE64_SOURCE
#endif

#include <stdio.h>
#include <stdint.h>
#include <sys/stat.h>
#include <time.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>

#include <limits.h>

#include <linux/types.h>

#include <et/com_err.h>

#include <ocfs2-kernel/kernel-list.h>
#include <ocfs2-kernel/sparse_endian_types.h>
#include <ocfs2-kernel/ocfs2_fs.h>
#include <ocfs2-kernel/quota_tree.h>
#include <o2dlm/o2dlm.h>
#include <o2cb/o2cb.h>
#include <ocfs2/ocfs2_err.h>
#include <ocfs2/jbd2.h>
#include <ocfs2-kernel/ocfs2_lockid.h>

#define OCFS2_LIB_FEATURE_INCOMPAT_SUPP		(OCFS2_FEATURE_INCOMPAT_SUPP | \
						 OCFS2_FEATURE_INCOMPAT_HEARTBEAT_DEV | \
						 OCFS2_FEATURE_INCOMPAT_RESIZE_INPROG | \
						 OCFS2_FEATURE_INCOMPAT_LOCAL_MOUNT   | \
						 OCFS2_FEATURE_INCOMPAT_INLINE_DATA   | \
						 OCFS2_FEATURE_INCOMPAT_TUNEFS_INPROG)

#define OCFS2_LIB_FEATURE_RO_COMPAT_SUPP	OCFS2_FEATURE_RO_COMPAT_SUPP

#define OCFS2_LIB_FEATURE_COMPAT_SUPP		OCFS2_FEATURE_COMPAT_SUPP

#define OCFS2_LIB_ABORTED_TUNEFS_SUPP		OCFS2_TUNEFS_INPROG_REMOVE_SLOT


/* define OCFS2_SB for ocfs2-tools */
#define OCFS2_SB(sb)	(sb)

/* Flags for the ocfs2_filesys structure */
#define OCFS2_FLAG_RO			0x00
#define OCFS2_FLAG_RW			0x01
#define OCFS2_FLAG_CHANGED		0x02
#define OCFS2_FLAG_DIRTY		0x04
#define OCFS2_FLAG_SWAP_BYTES		0x08
#define OCFS2_FLAG_BUFFERED		0x10
#define OCFS2_FLAG_NO_REV_CHECK		0x20	/* Do not check the OCFS
						   vol_header structure
						   for revision info */
#define OCFS2_FLAG_HEARTBEAT_DEV_OK	0x40
#define OCFS2_FLAG_STRICT_COMPAT_CHECK	0x80
#define OCFS2_FLAG_IMAGE_FILE	      0x0100

/* Return flags for the directory iterator functions */
#define OCFS2_DIRENT_CHANGED	0x01
#define OCFS2_DIRENT_ABORT	0x02
#define OCFS2_DIRENT_ERROR	0x04

/* Directory iterator flags */
#define OCFS2_DIRENT_FLAG_INCLUDE_EMPTY		0x01
#define OCFS2_DIRENT_FLAG_INCLUDE_REMOVED	0x02
#define OCFS2_DIRENT_FLAG_EXCLUDE_DOTS		0x04
#define OCFS2_DIRENT_FLAG_INCLUDE_TRAILER	0x08

/* Return flags for the chain iterator functions */
#define OCFS2_CHAIN_CHANGED	0x01
#define OCFS2_CHAIN_ABORT	0x02
#define OCFS2_CHAIN_ERROR	0x04

/* Directory constants */
#define OCFS2_DIRENT_DOT_FILE		1
#define OCFS2_DIRENT_DOT_DOT_FILE	2
#define OCFS2_DIRENT_OTHER_FILE		3
#define OCFS2_DIRENT_DELETED_FILE	4

/* Directory scan flags */
#define OCFS2_DIR_SCAN_FLAG_EXCLUDE_DOTS	0x01

/* Check if mounted flags */
#define OCFS2_MF_MOUNTED		1
#define OCFS2_MF_ISROOT			2
#define OCFS2_MF_READONLY		4
#define OCFS2_MF_SWAP			8
#define OCFS2_MF_BUSY			16
#define OCFS2_MF_MOUNTED_CLUSTER	32

/* check_heartbeats progress states */
#define OCFS2_CHB_START		1
#define OCFS2_CHB_WAITING	2
#define OCFS2_CHB_COMPLETE	3

/* Flags for global quotafile info */
#define OCFS2_QF_INFO_DIRTY 1
#define OCFS2_QF_INFO_LOADED 2

typedef void (*ocfs2_chb_notify)(int state, char *progress, void *data);

typedef struct _ocfs2_filesys ocfs2_filesys;
typedef struct _ocfs2_cached_inode ocfs2_cached_inode;
typedef struct _ocfs2_cached_dquot ocfs2_cached_dquot;
typedef struct _io_channel io_channel;
typedef struct _ocfs2_inode_scan ocfs2_inode_scan;
typedef struct _ocfs2_dir_scan ocfs2_dir_scan;
typedef struct _ocfs2_bitmap ocfs2_bitmap;
typedef struct _ocfs2_devices ocfs2_devices;

#define MAXQUOTAS 2
#define USRQUOTA 0
#define GRPQUOTA 1

#define OCFS2_DEF_BLOCK_GRACE 604800 /* 1 week */
#define OCFS2_DEF_INODE_GRACE 604800 /* 1 week */
#define OCFS2_DEF_QUOTA_SYNC 10000   /* 10 seconds */

struct _ocfs2_quota_info {
	ocfs2_cached_inode *qi_inode;
	int flags;
	struct ocfs2_global_disk_dqinfo qi_info;
};

typedef struct _ocfs2_quota_info ocfs2_quota_info;

struct _ocfs2_filesys {
	char *fs_devname;
	uint32_t fs_flags;
	io_channel *fs_io;
	struct ocfs2_dinode *fs_super;
	struct ocfs2_dinode *fs_orig_super;
	unsigned int fs_blocksize;
	unsigned int fs_clustersize;
	uint32_t fs_clusters;
	uint64_t fs_blocks;
	uint32_t fs_umask;
	uint64_t fs_root_blkno;
	uint64_t fs_sysdir_blkno;
	uint64_t fs_first_cg_blkno;
	char uuid_str[OCFS2_VOL_UUID_LEN * 2 + 1];

	/* Allocators */
	ocfs2_cached_inode *fs_cluster_alloc;
	ocfs2_cached_inode **fs_inode_allocs;
	ocfs2_cached_inode *fs_system_inode_alloc;
	ocfs2_cached_inode **fs_eb_allocs;
	ocfs2_cached_inode *fs_system_eb_alloc;

	struct o2dlm_ctxt *fs_dlm_ctxt;
	struct ocfs2_image_state *ost;

	ocfs2_quota_info qinfo[MAXQUOTAS];

	/* Reserved for the use of the calling application. */
	void *fs_private;
};

struct _ocfs2_cached_inode {
	struct _ocfs2_filesys *ci_fs;
	uint64_t ci_blkno;
	struct ocfs2_dinode *ci_inode;
	ocfs2_bitmap *ci_chains;
};

typedef unsigned int qid_t;

struct _ocfs2_cached_dquot {
	loff_t d_off;	/* Offset of structure in the file */
	struct _ocfs2_cached_dquot *d_next;	/* Next entry in hashchain */
	struct _ocfs2_cached_dquot **d_pprev;	/* Previous pointer in hashchain */
	struct ocfs2_global_disk_dqblk d_ddquot;	/* Quota entry */
};

struct ocfs2_slot_data {
	int		sd_valid;
	unsigned int	sd_node_num;
};

struct ocfs2_slot_map_data {
	int			md_num_slots;
	struct ocfs2_slot_data	*md_slots;
};

struct _ocfs2_devices {
	struct list_head list;
	char dev_name[PATH_MAX];
	uint8_t label[64];
	uint8_t uuid[16];
	int mount_flags;
	int fs_type;			/* 0=unknown, 1=ocfs, 2=ocfs2 */
	int hb_dev;
	uint32_t maj_num;		/* major number of the device */
	uint32_t min_num;		/* minor number of the device */
	errcode_t errcode;		/* error encountered reading device */
	void *private;
	struct ocfs2_slot_map_data *map; /* Mounted nodes, must be freed */
};

typedef struct _ocfs2_fs_options ocfs2_fs_options;

struct _ocfs2_fs_options {
	uint32_t opt_compat;
	uint32_t opt_incompat;
	uint32_t opt_ro_compat;
};

struct _ocfs2_quota_hash {
	int alloc_entries;
	int used_entries;
	ocfs2_cached_dquot **hash;
};

typedef struct _ocfs2_quota_hash ocfs2_quota_hash;

errcode_t ocfs2_malloc(unsigned long size, void *ptr);
errcode_t ocfs2_malloc0(unsigned long size, void *ptr);
errcode_t ocfs2_free(void *ptr);
errcode_t ocfs2_realloc(unsigned long size, void *ptr);
errcode_t ocfs2_realloc0(unsigned long size, void *ptr,
			 unsigned long old_size);
errcode_t ocfs2_malloc_blocks(io_channel *channel, int num_blocks,
			      void *ptr);
errcode_t ocfs2_malloc_block(io_channel *channel, void *ptr);

errcode_t io_open(const char *name, int flags, io_channel **channel);
errcode_t io_close(io_channel *channel);
int io_get_error(io_channel *channel);
errcode_t io_set_blksize(io_channel *channel, int blksize);
int io_get_blksize(io_channel *channel);
int io_get_fd(io_channel *channel);

/*
 * Raw I/O functions.  They will use the I/O cache if available.  The
 * _nocache version will not add a block to the cache, but if the block is
 * already in the cache it will be moved to the end of the LRU and kept
 * in a good state.
 *
 * Use ocfs2_read_blocks() if your application might handle o2image file.
 *
 * If a channel is set to 'nocache' via io_set_nocache(), it will use
 * the _nocache() functions even if called via the regular functions.
 * This allows control of naive code that we don't want to have to carry
 * nocache parameters around.  Smarter code can ignore this function and
 * use the _nocache() functions directly.
 */
errcode_t io_read_block(io_channel *channel, int64_t blkno, int count,
			char *data);
errcode_t io_read_block_nocache(io_channel *channel, int64_t blkno, int count,
			char *data);
errcode_t io_write_block(io_channel *channel, int64_t blkno, int count,
			 const char *data);
errcode_t io_write_block_nocache(io_channel *channel, int64_t blkno, int count,
			 const char *data);
errcode_t io_init_cache(io_channel *channel, size_t nr_blocks);
void io_set_nocache(io_channel *channel, bool nocache);
errcode_t io_init_cache_size(io_channel *channel, size_t bytes);
errcode_t io_share_cache(io_channel *from, io_channel *to);
errcode_t io_mlock_cache(io_channel *channel);
void io_destroy_cache(io_channel *channel);

errcode_t ocfs2_read_super(ocfs2_filesys *fs, uint64_t superblock, char *sb);
/* Writes the main superblock at OCFS2_SUPER_BLOCK_BLKNO */
errcode_t ocfs2_write_primary_super(ocfs2_filesys *fs);
/* Writes the primary and backups if enabled */
errcode_t ocfs2_write_super(ocfs2_filesys *fs);

/*
 * ocfs2_read_blocks() is a wraper around io_read_block. If device is an
 * image file it translates disk offset to image offset.
 * ocfs2_read_blocks_nocache() calls io_read_block_nocache().
 */
errcode_t ocfs2_read_blocks(ocfs2_filesys *fs, int64_t blkno, int count,
			    char *data);
errcode_t ocfs2_read_blocks_nocache(ocfs2_filesys *fs, int64_t blkno, int count,
				    char *data);
int ocfs2_mount_local(ocfs2_filesys *fs);
errcode_t ocfs2_open(const char *name, int flags,
		     unsigned int superblock, unsigned int blksize,
		     ocfs2_filesys **ret_fs);
errcode_t ocfs2_flush(ocfs2_filesys *fs);
errcode_t ocfs2_close(ocfs2_filesys *fs);
void ocfs2_freefs(ocfs2_filesys *fs);

void ocfs2_swap_inode_from_cpu(ocfs2_filesys *fs, struct ocfs2_dinode *di);
void ocfs2_swap_inode_to_cpu(ocfs2_filesys *fs, struct ocfs2_dinode *di);
errcode_t ocfs2_read_inode(ocfs2_filesys *fs, uint64_t blkno,
			   char *inode_buf);
errcode_t ocfs2_write_inode(ocfs2_filesys *fs, uint64_t blkno,
			    char *inode_buf);
errcode_t ocfs2_check_directory(ocfs2_filesys *fs, uint64_t dir);

errcode_t ocfs2_read_cached_inode(ocfs2_filesys *fs, uint64_t blkno,
				  ocfs2_cached_inode **ret_ci);
errcode_t ocfs2_write_cached_inode(ocfs2_filesys *fs,
				   ocfs2_cached_inode *cinode);
errcode_t ocfs2_free_cached_inode(ocfs2_filesys *fs,
				  ocfs2_cached_inode *cinode);
errcode_t ocfs2_refresh_cached_inode(ocfs2_filesys *fs,
				     ocfs2_cached_inode *cinode);

/*
 * obj is the object containing the extent list.  eg, if you are swapping
 * an inode's extent list, you're passing 'di' for the obj and
 * '&di->id2.i_list' for the el.  obj is needed to make sure the
 * byte swapping code doesn't walk off the end of the buffer in the
 * presence of corruption.
 */
void ocfs2_swap_extent_list_from_cpu(ocfs2_filesys *fs, void *obj,
				     struct ocfs2_extent_list *el);
void ocfs2_swap_extent_list_to_cpu(ocfs2_filesys *fs, void *obj,
				   struct ocfs2_extent_list *el);
errcode_t ocfs2_extent_map_get_blocks(ocfs2_cached_inode *cinode,
				      uint64_t v_blkno, int count,
				      uint64_t *p_blkno,
				      uint64_t *ret_count,
				      uint16_t *extent_flags);
errcode_t ocfs2_get_clusters(ocfs2_cached_inode *cinode,
			     uint32_t v_cluster,
			     uint32_t *p_cluster,
			     uint32_t *num_clusters,
			     uint16_t *extent_flags);
errcode_t ocfs2_xattr_get_clusters(ocfs2_filesys *fs,
				   struct ocfs2_extent_list *el,
				   uint64_t el_blkno,
				   char *el_blk,
				   uint32_t v_cluster,
				   uint32_t *p_cluster,
				   uint32_t *num_clusters,
				   uint16_t *extent_flags);
int ocfs2_find_leaf(ocfs2_filesys *fs, struct ocfs2_dinode *di,
		    uint32_t cpos, char **leaf_buf);
int ocfs2_search_extent_list(struct ocfs2_extent_list *el, uint32_t v_cluster);
void ocfs2_swap_journal_superblock(journal_superblock_t *jsb);
errcode_t ocfs2_init_journal_superblock(ocfs2_filesys *fs, char *buf,
					int buflen, uint32_t jrnl_size);
errcode_t ocfs2_read_journal_superblock(ocfs2_filesys *fs, uint64_t blkno,
					char *jsb_buf);
errcode_t ocfs2_write_journal_superblock(ocfs2_filesys *fs, uint64_t blkno,
					 char *jsb_buf);
errcode_t ocfs2_make_journal(ocfs2_filesys *fs, uint64_t blkno,
			     uint32_t clusters, ocfs2_fs_options *features);
errcode_t ocfs2_journal_clear_features(journal_superblock_t *jsb,
				       ocfs2_fs_options *features);
errcode_t ocfs2_journal_set_features(journal_superblock_t *jsb,
				     ocfs2_fs_options *features);
extern size_t ocfs2_journal_tag_bytes(journal_superblock_t *jsb);
extern uint64_t ocfs2_journal_tag_block(journal_block_tag_t *tag,
					size_t tag_bytes);

void ocfs2_swap_extent_block_to_cpu(ocfs2_filesys *fs,
				    struct ocfs2_extent_block *eb);
void ocfs2_swap_extent_block_from_cpu(ocfs2_filesys *fs,
				      struct ocfs2_extent_block *eb);
errcode_t ocfs2_read_extent_block(ocfs2_filesys *fs, uint64_t blkno,
       				  char *eb_buf);
errcode_t ocfs2_read_extent_block_nocheck(ocfs2_filesys *fs, uint64_t blkno,
					  char *eb_buf);
errcode_t ocfs2_write_extent_block(ocfs2_filesys *fs, uint64_t blkno,
       				   char *eb_buf);
void ocfs2_swap_refcount_list_to_cpu(ocfs2_filesys *fs, void *obj,
				     struct ocfs2_refcount_list *rl);
void ocfs2_swap_refcount_list_from_cpu(ocfs2_filesys *fs, void *obj,
				       struct ocfs2_refcount_list *rl);
void ocfs2_swap_refcount_block_to_cpu(ocfs2_filesys *fs,
				      struct ocfs2_refcount_block *rb);
void ocfs2_swap_refcount_block_from_cpu(ocfs2_filesys *fs,
					struct ocfs2_refcount_block *rb);
errcode_t ocfs2_read_refcount_block(ocfs2_filesys *fs, uint64_t blkno,
				    char *eb_buf);
errcode_t ocfs2_read_refcount_block_nocheck(ocfs2_filesys *fs, uint64_t blkno,
					    char *eb_buf);
errcode_t ocfs2_write_refcount_block(ocfs2_filesys *fs, uint64_t blkno,
				     char *rb_buf);
errcode_t ocfs2_delete_refcount_block(ocfs2_filesys *fs, uint64_t blkno);
errcode_t ocfs2_new_refcount_block(ocfs2_filesys *fs, uint64_t *blkno,
				   uint64_t root_blkno, uint32_t rf_generation);
errcode_t ocfs2_increase_refcount(ocfs2_filesys *fs, uint64_t ino,
				  uint64_t cpos, uint32_t len);
errcode_t ocfs2_decrease_refcount(ocfs2_filesys *fs,
				  uint64_t ino, uint32_t cpos,
				  uint32_t len, int delete);
errcode_t ocfs2_refcount_cow(ocfs2_cached_inode *cinode,
			     uint32_t cpos, uint32_t write_len,
			     uint32_t max_cpos);
errcode_t ocfs2_refcount_cow_xattr(ocfs2_cached_inode *ci,
				   char *xe_buf,
				   uint64_t xe_blkno,
				   char *value_buf,
				   uint64_t value_blkno,
				   struct ocfs2_xattr_value_root *xv,
				   uint32_t cpos, uint32_t write_len);
errcode_t ocfs2_change_refcount_flag(ocfs2_filesys *fs, uint64_t i_blkno,
				     uint32_t v_cpos, uint32_t clusters,
				     uint64_t p_cpos,
				     int new_flags, int clear_flags);
errcode_t ocfs2_refcount_tree_get_rec(ocfs2_filesys *fs,
				      struct ocfs2_refcount_block *rb,
				      uint32_t phys_cpos,
				      uint64_t *p_blkno,
				      uint32_t *e_cpos,
				      uint32_t *num_clusters);
errcode_t ocfs2_refcount_punch_hole(ocfs2_filesys *fs, uint64_t rf_blkno,
				    uint64_t p_start, uint32_t len);
errcode_t ocfs2_change_refcount(ocfs2_filesys *fs, uint64_t rf_blkno,
				uint64_t p_start, uint32_t len,
				uint32_t refcount);
int ocfs2_get_refcount_rec(ocfs2_filesys *fs,
			   char *ref_root_buf,
			   uint64_t cpos, unsigned int len,
			   struct ocfs2_refcount_rec *ret_rec,
			   int *index,
			   char *ret_buf);
errcode_t ocfs2_create_refcount_tree(ocfs2_filesys *fs, uint64_t *refcount_loc);
errcode_t ocfs2_attach_refcount_tree(ocfs2_filesys *fs,
				     uint64_t ino, uint64_t refcount_loc);
errcode_t ocfs2_swap_dir_entries_from_cpu(void *buf, uint64_t bytes);
errcode_t ocfs2_swap_dir_entries_to_cpu(void *buf, uint64_t bytes);
void ocfs2_swap_dir_trailer(struct ocfs2_dir_block_trailer *trailer);
errcode_t ocfs2_read_dir_block(ocfs2_filesys *fs, struct ocfs2_dinode *di,
			       uint64_t block, void *buf);
errcode_t ocfs2_write_dir_block(ocfs2_filesys *fs, struct ocfs2_dinode *di,
				uint64_t block, void *buf);
unsigned int ocfs2_dir_trailer_blk_off(ocfs2_filesys *fs);
struct ocfs2_dir_block_trailer *ocfs2_dir_trailer_from_block(ocfs2_filesys *fs,
							     void *data);
int ocfs2_supports_dir_trailer(ocfs2_filesys *fs);
int ocfs2_dir_has_trailer(ocfs2_filesys *fs, struct ocfs2_dinode *di);
int ocfs2_skip_dir_trailer(ocfs2_filesys *fs, struct ocfs2_dinode *di,
			   struct ocfs2_dir_entry *de, unsigned long offset);
void ocfs2_init_dir_trailer(ocfs2_filesys *fs, struct ocfs2_dinode *di,
			    uint64_t blkno, void *buf);

errcode_t ocfs2_dir_iterate2(ocfs2_filesys *fs,
			     uint64_t dir,
			     int flags,
			     char *block_buf,
			     int (*func)(uint64_t	dir,
					 int		entry,
					 struct ocfs2_dir_entry *dirent,
					 int	offset,
					 int	blocksize,
					 char	*buf,
					 void	*priv_data),
			     void *priv_data);
extern errcode_t ocfs2_dir_iterate(ocfs2_filesys *fs, 
				   uint64_t dir,
				   int flags,
				   char *block_buf,
				   int (*func)(struct ocfs2_dir_entry *dirent,
					       int	offset,
					       int	blocksize,
					       char	*buf,
					       void	*priv_data),
				   void *priv_data);

errcode_t ocfs2_lookup(ocfs2_filesys *fs, uint64_t dir,
		       const char *name, int namelen, char *buf,
		       uint64_t *inode);

errcode_t ocfs2_lookup_system_inode(ocfs2_filesys *fs, int type,
				    int slot_num, uint64_t *blkno);

errcode_t ocfs2_link(ocfs2_filesys *fs, uint64_t dir, const char *name,
		     uint64_t ino, int flags);

errcode_t ocfs2_unlink(ocfs2_filesys *fs, uint64_t dir,
		       const char *name, uint64_t ino, int flags);

errcode_t ocfs2_open_inode_scan(ocfs2_filesys *fs,
				ocfs2_inode_scan **ret_scan);
void ocfs2_close_inode_scan(ocfs2_inode_scan *scan);
errcode_t ocfs2_get_next_inode(ocfs2_inode_scan *scan,
			       uint64_t *blkno, char *inode);

errcode_t ocfs2_open_dir_scan(ocfs2_filesys *fs, uint64_t dir, int flags,
			      ocfs2_dir_scan **ret_scan);
void ocfs2_close_dir_scan(ocfs2_dir_scan *scan);
errcode_t ocfs2_get_next_dir_entry(ocfs2_dir_scan *scan,
				   struct ocfs2_dir_entry *dirent);

errcode_t ocfs2_cluster_bitmap_new(ocfs2_filesys *fs,
				   const char *description,
				   ocfs2_bitmap **ret_bitmap);
errcode_t ocfs2_block_bitmap_new(ocfs2_filesys *fs,
				 const char *description,
				 ocfs2_bitmap **ret_bitmap);
void ocfs2_bitmap_free(ocfs2_bitmap *bitmap);
errcode_t ocfs2_bitmap_set(ocfs2_bitmap *bitmap, uint64_t bitno,
			   int *oldval);
errcode_t ocfs2_bitmap_clear(ocfs2_bitmap *bitmap, uint64_t bitno,
			     int *oldval);
errcode_t ocfs2_bitmap_test(ocfs2_bitmap *bitmap, uint64_t bitno,
			    int *val);
errcode_t ocfs2_bitmap_find_next_set(ocfs2_bitmap *bitmap,
				     uint64_t start, uint64_t *found);
errcode_t ocfs2_bitmap_find_next_clear(ocfs2_bitmap *bitmap,
				       uint64_t start, uint64_t *found);
errcode_t ocfs2_bitmap_read(ocfs2_bitmap *bitmap);
errcode_t ocfs2_bitmap_write(ocfs2_bitmap *bitmap);
uint64_t ocfs2_bitmap_get_set_bits(ocfs2_bitmap *bitmap);
errcode_t ocfs2_bitmap_alloc_range(ocfs2_bitmap *bitmap, uint64_t min,
				   uint64_t len, uint64_t *first_bit,
				   uint64_t *bits_found);
errcode_t ocfs2_bitmap_clear_range(ocfs2_bitmap *bitmap, uint64_t len, 
				   uint64_t first_bit);

errcode_t ocfs2_get_device_size(const char *file, int blocksize,
				uint64_t *retblocks);

errcode_t ocfs2_get_device_sectsize(const char *file, int *sectsize);

errcode_t ocfs2_check_if_mounted(const char *file, int *mount_flags);
errcode_t ocfs2_check_mount_point(const char *device, int *mount_flags,
		                  char *mtpt, int mtlen);

errcode_t ocfs2_read_whole_file(ocfs2_filesys *fs, uint64_t blkno,
				char **buf, int *len);

void ocfs2_swap_disk_heartbeat_block(struct o2hb_disk_heartbeat_block *hb);
errcode_t ocfs2_check_heartbeat(char *device, int *mount_flags,
				struct list_head *nodes_list);

errcode_t ocfs2_check_heartbeats(struct list_head *dev_list, int ignore_local);

errcode_t ocfs2_get_ocfs1_label(char *device, uint8_t *label, uint16_t label_len,
				uint8_t *uuid, uint16_t uuid_len);

void ocfs2_swap_group_desc(struct ocfs2_group_desc *gd);
errcode_t ocfs2_read_group_desc(ocfs2_filesys *fs, uint64_t blkno,
				char *gd_buf);

errcode_t ocfs2_write_group_desc(ocfs2_filesys *fs, uint64_t blkno,
				 char *gd_buf);

errcode_t ocfs2_chain_iterate(ocfs2_filesys *fs,
			      uint64_t blkno,
			      int (*func)(ocfs2_filesys *fs,
					  uint64_t gd_blkno,
					  int chain_num,
					  void *priv_data),
			      void *priv_data);

errcode_t ocfs2_load_chain_allocator(ocfs2_filesys *fs,
				     ocfs2_cached_inode *cinode);
errcode_t ocfs2_write_chain_allocator(ocfs2_filesys *fs,
				      ocfs2_cached_inode *cinode);
errcode_t ocfs2_chain_alloc(ocfs2_filesys *fs,
			    ocfs2_cached_inode *cinode,
			    uint64_t *gd_blkno,
			    uint64_t *bitno);
errcode_t ocfs2_chain_free(ocfs2_filesys *fs,
			   ocfs2_cached_inode *cinode,
			   uint64_t bitno);
errcode_t ocfs2_chain_alloc_range(ocfs2_filesys *fs,
				  ocfs2_cached_inode *cinode,
				  uint64_t min,
				  uint64_t requested,
				  uint64_t *start_bit,
				  uint64_t *bits_found);
errcode_t ocfs2_chain_free_range(ocfs2_filesys *fs,
				 ocfs2_cached_inode *cinode,
				 uint64_t len,
				 uint64_t start_bit);
errcode_t ocfs2_chain_test(ocfs2_filesys *fs,
			   ocfs2_cached_inode *cinode,
			   uint64_t bitno,
			   int *oldval);
errcode_t ocfs2_chain_force_val(ocfs2_filesys *fs,
				ocfs2_cached_inode *cinode,
				uint64_t blkno, 
				int newval,
				int *oldval);
errcode_t ocfs2_chain_add_group(ocfs2_filesys *fs,
				ocfs2_cached_inode *cinode);

errcode_t ocfs2_init_dir(ocfs2_filesys *fs,
			 uint64_t dir,
			 uint64_t parent_dir);

errcode_t ocfs2_expand_dir(ocfs2_filesys *fs,
			   uint64_t dir);

errcode_t ocfs2_test_inode_allocated(ocfs2_filesys *fs, uint64_t blkno,
				     int *is_allocated);
void ocfs2_init_group_desc(ocfs2_filesys *fs,
			   struct ocfs2_group_desc *gd,
			   uint64_t blkno, uint32_t generation,
			   uint64_t parent_inode, uint16_t bits,
			   uint16_t chain);

errcode_t ocfs2_new_dir_block(ocfs2_filesys *fs, uint64_t dir_ino,
			      uint64_t parent_ino, char **block);

errcode_t ocfs2_inode_insert_extent(ocfs2_filesys *fs, uint64_t ino,
				    uint32_t cpos, uint64_t c_blkno,
				    uint32_t clusters, uint16_t flag);
errcode_t ocfs2_cached_inode_insert_extent(ocfs2_cached_inode *ci,
					   uint32_t cpos, uint64_t c_blkno,
					   uint32_t clusters, uint16_t flag);

void ocfs2_dinode_new_extent_list(ocfs2_filesys *fs, struct ocfs2_dinode *di);
void ocfs2_set_inode_data_inline(ocfs2_filesys *fs, struct ocfs2_dinode *di);
errcode_t ocfs2_convert_inline_data_to_extents(ocfs2_cached_inode *ci);
errcode_t ocfs2_new_inode(ocfs2_filesys *fs, uint64_t *ino, int mode);
errcode_t ocfs2_new_system_inode(ocfs2_filesys *fs, uint64_t *ino, int mode, int flags);
errcode_t ocfs2_delete_inode(ocfs2_filesys *fs, uint64_t ino);
errcode_t ocfs2_new_extent_block(ocfs2_filesys *fs, uint64_t *blkno);
errcode_t ocfs2_delete_extent_block(ocfs2_filesys *fs, uint64_t blkno);
/*
 * Allocate the blocks and insert them to the file.
 * only i_clusters of dinode will be updated accordingly, i_size not changed.
 */
errcode_t ocfs2_extend_allocation(ocfs2_filesys *fs, uint64_t ino,
				  uint32_t new_clusters);
/* Ditto for cached inode */
errcode_t ocfs2_cached_inode_extend_allocation(ocfs2_cached_inode *ci,
					       uint32_t new_clusters);
/* Extend the file to the new size. No clusters will be allocated. */
errcode_t ocfs2_extend_file(ocfs2_filesys *fs, uint64_t ino, uint64_t new_size);

int ocfs2_mark_extent_written(ocfs2_filesys *fs, struct ocfs2_dinode *di,
			      uint32_t cpos, uint32_t len, uint64_t p_blkno);
/* Reserve spaces at "offset" with a "len" in the files. */
errcode_t ocfs2_allocate_unwritten_extents(ocfs2_filesys *fs, uint64_t ino,
					   uint64_t offset, uint64_t len);

errcode_t ocfs2_truncate(ocfs2_filesys *fs, uint64_t ino, uint64_t new_i_size);
errcode_t ocfs2_truncate_full(ocfs2_filesys *fs, uint64_t ino,
			      uint64_t new_i_size,
			      errcode_t (*free_clusters)(ocfs2_filesys *fs,
							 uint32_t len,
							 uint64_t start,
							 void *free_data),
			      void *free_data);
errcode_t ocfs2_zero_tail_and_truncate(ocfs2_filesys *fs,
				       ocfs2_cached_inode *ci,
				       uint64_t new_size,
				       uint32_t *new_clusters);
errcode_t ocfs2_new_clusters(ocfs2_filesys *fs,
			     uint32_t min,
			     uint32_t requested,
			     uint64_t *start_blkno,
			     uint32_t *clusters_found);
errcode_t ocfs2_test_cluster_allocated(ocfs2_filesys *fs, uint32_t cpos,
				       int *is_allocated);
errcode_t ocfs2_new_specific_cluster(ocfs2_filesys *fs, uint32_t cpos);
errcode_t ocfs2_free_clusters(ocfs2_filesys *fs,
			      uint32_t len,
			      uint64_t start_blkno);
errcode_t ocfs2_test_clusters(ocfs2_filesys *fs,
			      uint32_t len,
			      uint64_t start_blkno,
			      int test,
			      int *matches);

errcode_t ocfs2_lookup(ocfs2_filesys *fs, uint64_t dir, const char *name,
		       int namelen, char *buf, uint64_t *inode);

errcode_t ocfs2_namei(ocfs2_filesys *fs, uint64_t root, uint64_t cwd,
		      const char *name, uint64_t *inode);

errcode_t ocfs2_namei_follow(ocfs2_filesys *fs, uint64_t root, uint64_t cwd,
			     const char *name, uint64_t *inode);

errcode_t ocfs2_follow_link(ocfs2_filesys *fs, uint64_t root, uint64_t cwd,
			    uint64_t inode, uint64_t *res_inode);

errcode_t ocfs2_file_read(ocfs2_cached_inode *ci, void *buf, uint32_t count,
			  uint64_t offset, uint32_t *got);

errcode_t ocfs2_file_write(ocfs2_cached_inode *ci, void *buf, uint32_t count,
			   uint64_t offset, uint32_t *wrote);

errcode_t ocfs2_fill_cluster_desc(ocfs2_filesys *fs,
				  struct o2cb_cluster_desc *desc);
errcode_t ocfs2_set_cluster_desc(ocfs2_filesys *fs,
				 struct o2cb_cluster_desc *desc);
errcode_t ocfs2_fill_heartbeat_desc(ocfs2_filesys *fs,
				    struct o2cb_region_desc *desc);

errcode_t ocfs2_lock_down_cluster(ocfs2_filesys *fs);

errcode_t ocfs2_release_cluster(ocfs2_filesys *fs);

errcode_t ocfs2_initialize_dlm(ocfs2_filesys *fs, const char *service);

errcode_t ocfs2_shutdown_dlm(ocfs2_filesys *fs, const char *service);

errcode_t ocfs2_super_lock(ocfs2_filesys *fs);

errcode_t ocfs2_super_unlock(ocfs2_filesys *fs);

errcode_t ocfs2_meta_lock(ocfs2_filesys *fs, ocfs2_cached_inode *inode,
			  enum o2dlm_lock_level level, int flags);

errcode_t ocfs2_meta_unlock(ocfs2_filesys *fs, ocfs2_cached_inode *ci);

/* Quota operations */
static inline int ocfs2_global_dqstr_in_blk(int blocksize)
{
	return (blocksize - OCFS2_QBLK_RESERVED_SPACE -
		sizeof(struct qt_disk_dqdbheader)) /
		sizeof(struct ocfs2_global_disk_dqblk);
}
void ocfs2_swap_quota_header(struct ocfs2_disk_dqheader *header);
void ocfs2_swap_quota_local_info(struct ocfs2_local_disk_dqinfo *info);
void ocfs2_swap_quota_chunk_header(struct ocfs2_local_disk_chunk *chunk);
void ocfs2_swap_quota_global_info(struct ocfs2_global_disk_dqinfo *info);
void ocfs2_swap_quota_global_dqblk(struct ocfs2_global_disk_dqblk *dqblk);
void ocfs2_swap_quota_leaf_block_header(struct qt_disk_dqdbheader *bheader);
errcode_t ocfs2_init_local_quota_file(ocfs2_filesys *fs, int type,
				      uint64_t blkno);
errcode_t ocfs2_init_local_quota_files(ocfs2_filesys *fs, int type);
int ocfs2_qtree_depth(int blocksize);
int ocfs2_qtree_entry_unused(struct ocfs2_global_disk_dqblk *ddquot);
errcode_t ocfs2_init_global_quota_file(ocfs2_filesys *fs, int type);
errcode_t ocfs2_init_fs_quota_info(ocfs2_filesys *fs, int type);
errcode_t ocfs2_read_global_quota_info(ocfs2_filesys *fs, int type);
errcode_t ocfs2_load_fs_quota_info(ocfs2_filesys *fs);
errcode_t ocfs2_write_global_quota_info(ocfs2_filesys *fs, int type);
errcode_t ocfs2_write_dquot(ocfs2_filesys *fs, int type,
			    ocfs2_cached_dquot *dquot);
errcode_t ocfs2_delete_dquot(ocfs2_filesys *fs, int type,
			     ocfs2_cached_dquot *dquot);
errcode_t ocfs2_read_dquot(ocfs2_filesys *fs, int type, qid_t id,
			   ocfs2_cached_dquot **ret_dquot);
errcode_t ocfs2_new_quota_hash(ocfs2_quota_hash **hashp);
errcode_t ocfs2_free_quota_hash(ocfs2_quota_hash *hash);
errcode_t ocfs2_insert_quota_hash(ocfs2_quota_hash *hash,
				  ocfs2_cached_dquot *dquot);
errcode_t ocfs2_remove_quota_hash(ocfs2_quota_hash *hash,
				  ocfs2_cached_dquot *dquot);
errcode_t ocfs2_find_quota_hash(ocfs2_quota_hash *hash, qid_t id,
				ocfs2_cached_dquot **dquotp);
errcode_t ocfs2_find_create_quota_hash(ocfs2_quota_hash *hash, qid_t id,
				       ocfs2_cached_dquot **dquotp);
errcode_t ocfs2_find_read_quota_hash(ocfs2_filesys *fs, ocfs2_quota_hash *hash,
				     int type, qid_t id,
				     ocfs2_cached_dquot **dquotp);
errcode_t ocfs2_compute_quota_usage(ocfs2_filesys *fs,
				    ocfs2_quota_hash *usr_hash,
				    ocfs2_quota_hash *grp_hash);
errcode_t ocfs2_init_quota_change(ocfs2_filesys *fs,
				  ocfs2_quota_hash **usrhash,
				  ocfs2_quota_hash **grphash);
errcode_t ocfs2_finish_quota_change(ocfs2_filesys *fs,
				    ocfs2_quota_hash *usrhash,
				    ocfs2_quota_hash *grphash);
errcode_t ocfs2_apply_quota_change(ocfs2_filesys *fs,
				   ocfs2_quota_hash *usrhash,
				   ocfs2_quota_hash *grphash,
				   uid_t uid, gid_t gid,
				   int64_t space_change,
				   int64_t inode_change);
errcode_t ocfs2_iterate_quota_hash(ocfs2_quota_hash *hash,
				   errcode_t (*f)(ocfs2_cached_dquot *, void *),
				   void *data);
errcode_t ocfs2_write_release_dquots(ocfs2_filesys *fs, int type,
				     ocfs2_quota_hash *hash);

/* Low level */
void ocfs2_swap_slot_map(struct ocfs2_slot_map *sm, int num_slots);
void ocfs2_swap_slot_map_extended(struct ocfs2_slot_map_extended *se,
				  int num_slots);
errcode_t ocfs2_read_slot_map(ocfs2_filesys *fs,
			      int num_slots,
			      struct ocfs2_slot_map **map_ret);
errcode_t ocfs2_read_slot_map_extended(ocfs2_filesys *fs,
				       int num_slots,
				       struct ocfs2_slot_map_extended **map_ret);
errcode_t ocfs2_write_slot_map(ocfs2_filesys *fs,
			       int num_slots,
			       struct ocfs2_slot_map *sm);
errcode_t ocfs2_write_slot_map_extended(ocfs2_filesys *fs,
					int num_slots,
					struct ocfs2_slot_map_extended *se);

/* High level functions for metadata ecc */
void ocfs2_compute_meta_ecc(ocfs2_filesys *fs, void *data,
			    struct ocfs2_block_check *bc);
errcode_t ocfs2_validate_meta_ecc(ocfs2_filesys *fs, void *data,
				  struct ocfs2_block_check *bc);
/* Low level checksum compute functions.  Use the high-level ones. */
extern void ocfs2_block_check_compute(void *data, size_t blocksize,
				      struct ocfs2_block_check *bc);
extern errcode_t ocfs2_block_check_validate(void *data, size_t blocksize,
					    struct ocfs2_block_check *bc);

/* High level */
errcode_t ocfs2_format_slot_map(ocfs2_filesys *fs);
errcode_t ocfs2_load_slot_map(ocfs2_filesys *fs,
			      struct ocfs2_slot_map_data **data_ret);
errcode_t ocfs2_store_slot_map(ocfs2_filesys *fs,
			       struct ocfs2_slot_map_data *md);

enum ocfs2_lock_type ocfs2_get_lock_type(char c);

char *ocfs2_get_lock_type_string(enum ocfs2_lock_type type);

errcode_t ocfs2_encode_lockres(enum ocfs2_lock_type type, uint64_t blkno,
			       uint32_t generation, uint64_t parent,
			       char *lockres);

errcode_t ocfs2_decode_lockres(char *lockres, enum ocfs2_lock_type *type,
			       uint64_t *blkno, uint32_t *generation,
			       uint64_t *parent);

errcode_t ocfs2_printable_lockres(char *lockres, char *name, int len);

/* write the superblock at the specific block. */
errcode_t ocfs2_write_backup_super(ocfs2_filesys *fs, uint64_t blkno);

/* Get the blkno according to the file system info.
 * The unused ones, depending on the volume size, are zeroed.
 * Return the length of the block array.
 */
int ocfs2_get_backup_super_offsets(ocfs2_filesys *fs,
				   uint64_t *blocks, size_t len);

/* This function will get the superblock pointed to by fs and copy it to
 * the blocks. But first it will ensure all the appropriate clusters are free.
 * If not, it will error out with ENOSPC. If free, it will set bits for all
 * the clusters, zero the clusters and write the backup sb.
 * In case of updating, it will override the backup blocks with the newest
 * superblock information.
 */
errcode_t ocfs2_set_backup_super_list(ocfs2_filesys *fs,
				      uint64_t *blocks, size_t len);
/* Conversely, this clears all the allocator bits associated with the
 * specified backup superblocks */
errcode_t ocfs2_clear_backup_super_list(ocfs2_filesys *fs,
					uint64_t *blocks, size_t len);

/* Refresh the backup superblock information */
errcode_t ocfs2_refresh_backup_supers(ocfs2_filesys *fs);
/* Refresh a specific list of backup superblocks */
errcode_t ocfs2_refresh_backup_super_list(ocfs2_filesys *fs,
					  uint64_t *blocks, size_t len);

errcode_t ocfs2_read_backup_super(ocfs2_filesys *fs, int backup, char *sbbuf);

/* get the virtual offset of the last allocated cluster. */
errcode_t ocfs2_get_last_cluster_offset(ocfs2_filesys *fs,
					struct ocfs2_dinode *di,
					uint32_t *v_cluster);

/* Filesystem features */
enum ocfs2_feature_levels {
	OCFS2_FEATURE_LEVEL_DEFAULT = 0,
	OCFS2_FEATURE_LEVEL_MAX_COMPAT,
	OCFS2_FEATURE_LEVEL_MAX_FEATURES,
};

errcode_t ocfs2_snprint_feature_flags(char *str, size_t size,
				      ocfs2_fs_options *flags);
errcode_t ocfs2_snprint_tunefs_flags(char *str, size_t size, uint16_t flags);
errcode_t ocfs2_snprint_extent_flags(char *str, size_t size, uint8_t flags);
errcode_t ocfs2_snprint_refcount_flags(char *str, size_t size, uint8_t flags);
errcode_t ocfs2_parse_feature(const char *opts,
			      ocfs2_fs_options *feature_flags,
			      ocfs2_fs_options *reverse_flags);

errcode_t ocfs2_parse_feature_level(const char *typestr,
				    enum ocfs2_feature_levels *level);

errcode_t ocfs2_merge_feature_flags_with_level(ocfs2_fs_options *dest,
					       int level,
					       ocfs2_fs_options *feature_set,
					       ocfs2_fs_options *reverse_set);

/*
 * Get a callback with each feature in feature_set in order.  This will
 * calculate the dependencies of each feature in feature_set, then call func
 * once per feature, with only that feature passed to func.
 */
void ocfs2_feature_foreach(ocfs2_fs_options *feature_set,
			   int (*func)(ocfs2_fs_options *feature,
				       void *user_data),
			   void *user_data);
/* The reverse function.  It will call the features in reverse order. */
void ocfs2_feature_reverse_foreach(ocfs2_fs_options *reverse_set,
				   int (*func)(ocfs2_fs_options *feature,
					       void *user_data),
				   void *user_data);


/* These are deprecated names - don't use them */
int ocfs2_get_backup_super_offset(ocfs2_filesys *fs,
				  uint64_t *blocks, size_t len);
errcode_t ocfs2_refresh_backup_super(ocfs2_filesys *fs,
				     uint64_t *blocks, size_t len);
errcode_t ocfs2_set_backup_super(ocfs2_filesys *fs,
				 uint64_t *blocks, size_t len);



/* 
 * ${foo}_to_${bar} is a floor function.  blocks_to_clusters will
 * returns the cluster that contains a block, not the number of clusters
 * that hold a given number of blocks.
 *
 * ${foo}_in_${bar} is a ceiling function.  clusters_in_blocks will give
 * the number of clusters needed to hold a given number of blocks.
 *
 * These functions return UINTxx_MAX when they overflow, but UINTxx_MAX
 * cannot be used to check overflow.  UINTxx_MAX is a valid value in much
 * of ocfs2.  The caller is responsible for preventing overflow before
 * using these functions.
 */

static inline uint64_t ocfs2_clusters_to_blocks(ocfs2_filesys *fs,
						uint32_t clusters)
{
	int c_to_b_bits =
		OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits -
		OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;

	return (uint64_t)clusters << c_to_b_bits;
}

static inline uint32_t ocfs2_blocks_to_clusters(ocfs2_filesys *fs,
						uint64_t blocks)
{
	int b_to_c_bits =
		OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits -
		OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;
	uint64_t ret = blocks >> b_to_c_bits;

	if (ret > UINT32_MAX)
		ret = UINT32_MAX;

	return (uint32_t)ret;
}

static inline uint64_t ocfs2_clusters_to_bytes(ocfs2_filesys *fs,
					       uint32_t clusters)
{
	uint64_t ret = clusters;

	ret = ret << OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits;
	if (ret < clusters)
		ret = UINT64_MAX;

	return ret;
}

static inline uint32_t ocfs2_bytes_to_clusters(ocfs2_filesys *fs,
					       uint64_t bytes)
{
	uint64_t ret =
		bytes >> OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits;

	if (ret > UINT32_MAX)
		ret = UINT32_MAX;

	return (uint32_t)ret;
}

static inline uint64_t ocfs2_blocks_to_bytes(ocfs2_filesys *fs,
					     uint64_t blocks)
{
	uint64_t ret =
		blocks << OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;

	if (ret < blocks)
		ret = UINT64_MAX;

	return ret;
}

static inline uint64_t ocfs2_bytes_to_blocks(ocfs2_filesys *fs,
					     uint64_t bytes)
{
	return bytes >> OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;
}

static inline uint32_t ocfs2_clusters_in_blocks(ocfs2_filesys *fs, 
						uint64_t blocks)
{
	int c_to_b_bits = OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits -
		          OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;
	uint64_t ret = blocks + ((1 << c_to_b_bits) - 1); 

	if (ret < blocks) /* deal with wrapping */
		ret = UINT64_MAX;

	ret = ret >> c_to_b_bits;
	if (ret > UINT32_MAX)
		ret = UINT32_MAX;

	return (uint32_t)ret;
}

static inline uint32_t ocfs2_clusters_in_bytes(ocfs2_filesys *fs,
					       uint64_t bytes)
{
	uint64_t ret = bytes + fs->fs_clustersize - 1;

	if (ret < bytes) /* deal with wrapping */
		ret = UINT64_MAX;

	ret = ret >> OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits;
	if (ret > UINT32_MAX)
		ret = UINT32_MAX;

	return (uint32_t)ret;
}

static inline uint64_t ocfs2_blocks_in_bytes(ocfs2_filesys *fs,
					     uint64_t bytes)
{
	uint64_t ret = bytes + fs->fs_blocksize - 1;

	if (ret < bytes) /* deal with wrapping */
		return UINT64_MAX;

	return ret >> OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;
}

static inline uint64_t ocfs2_align_bytes_to_clusters(ocfs2_filesys *fs,
						     uint64_t bytes)
{
	uint32_t clusters;

	clusters = ocfs2_clusters_in_bytes(fs, bytes);
	return (uint64_t)clusters <<
			OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits;
}

static inline uint64_t ocfs2_align_bytes_to_blocks(ocfs2_filesys *fs,
						   uint64_t bytes)
{
	uint64_t blocks;

	blocks = ocfs2_blocks_in_bytes(fs, bytes);
	return blocks << OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;
}

/* given a cluster offset, calculate which block group it belongs to
 * and return that block offset. */
static inline uint64_t ocfs2_which_cluster_group(ocfs2_filesys *fs,
						 uint16_t cpg,
						 uint32_t cluster)
{
	struct ocfs2_super_block *sb = OCFS2_RAW_SB(fs->fs_super);
	uint32_t group_no;

	group_no = cluster / cpg;
	if (!group_no)
		return sb->s_first_cluster_group;
	return ocfs2_clusters_to_blocks(fs, group_no * cpg);
}

static inline int ocfs2_block_out_of_range(ocfs2_filesys *fs, uint64_t block)
{
	return (block < OCFS2_SUPER_BLOCK_BLKNO) || (block > fs->fs_blocks);
}

struct ocfs2_cluster_group_sizes {
	uint16_t	cgs_cpg;
	uint16_t	cgs_tail_group_bits;
	uint32_t	cgs_cluster_groups;
};
static inline void ocfs2_calc_cluster_groups(uint64_t clusters, 
					     uint64_t blocksize,
				     struct ocfs2_cluster_group_sizes *cgs)
{
	uint16_t max_bits = 8 * ocfs2_group_bitmap_size(blocksize);

	cgs->cgs_cpg = max_bits;
	if (max_bits > clusters)
		cgs->cgs_cpg = clusters;

	cgs->cgs_cluster_groups = (clusters + cgs->cgs_cpg - 1) / 
				  cgs->cgs_cpg;

	cgs->cgs_tail_group_bits = clusters % cgs->cgs_cpg;
	if (cgs->cgs_tail_group_bits == 0)
		cgs->cgs_tail_group_bits = cgs->cgs_cpg;
}

/*
 * This is only valid for leaf nodes, which are the only ones that can
 * have empty extents anyway.
 */
static inline int ocfs2_is_empty_extent(struct ocfs2_extent_rec *rec)
{
	return !rec->e_leaf_clusters;
}

/*
 * Helper function to look at the # of clusters in an extent record.
 */
static inline uint32_t ocfs2_rec_clusters(uint16_t tree_depth,
					  struct ocfs2_extent_rec *rec)
{
	/*
	 * Cluster count in extent records is slightly different
	 * between interior nodes and leaf nodes. This is to support
	 * unwritten extents which need a flags field in leaf node
	 * records, thus shrinking the available space for a clusters
	 * field.
	 */
	if (tree_depth)
		return rec->e_int_clusters;
	else
		return rec->e_leaf_clusters;
}

static inline void ocfs2_set_rec_clusters(uint16_t tree_depth,
					  struct ocfs2_extent_rec *rec,
					  uint32_t clusters)
{
	if (tree_depth)
		rec->e_int_clusters = clusters;
	else
		rec->e_leaf_clusters = clusters;
}

static inline int ocfs2_sparse_alloc(struct ocfs2_super_block *osb)
{
	if (osb->s_feature_incompat & OCFS2_FEATURE_INCOMPAT_SPARSE_ALLOC)
		return 1;
	return 0;
}

static inline int ocfs2_userspace_stack(struct ocfs2_super_block *osb)
{
	if (osb->s_feature_incompat & OCFS2_FEATURE_INCOMPAT_USERSPACE_STACK)
		return 1;
	return 0;
}

static inline int ocfs2_writes_unwritten_extents(struct ocfs2_super_block *osb)
{
	/*
	 * Support for sparse files is a pre-requisite
	 */
	if (!ocfs2_sparse_alloc(osb))
		return 0;

	if (osb->s_feature_ro_compat & OCFS2_FEATURE_RO_COMPAT_UNWRITTEN)
		return 1;
	return 0;
}

static inline int ocfs2_uses_extended_slot_map(struct ocfs2_super_block *osb)
{
	if (osb->s_feature_incompat & OCFS2_FEATURE_INCOMPAT_EXTENDED_SLOT_MAP)
		return 1;
	return 0;
}

static inline int ocfs2_support_inline_data(struct ocfs2_super_block *osb)
{
	if (osb->s_feature_incompat & OCFS2_FEATURE_INCOMPAT_INLINE_DATA)
		return 1;
	return 0;
}

static inline int ocfs2_meta_ecc(struct ocfs2_super_block *osb)
{
	if (OCFS2_HAS_INCOMPAT_FEATURE(osb,
				       OCFS2_FEATURE_INCOMPAT_META_ECC))
		return 1;
	return 0;
}

static inline int ocfs2_support_xattr(struct ocfs2_super_block *osb)
{
	if (osb->s_feature_incompat & OCFS2_FEATURE_INCOMPAT_XATTR)
		return 1;
	return 0;
}

/*
 * When we're swapping some of our disk structures, a garbage count
 * can send us past the edge of a block buffer.  This function guards
 * against that.  It returns true if the element would walk off then end
 * of the block buffer.
 */
static inline int ocfs2_swap_barrier(ocfs2_filesys *fs, void *block_buffer,
				     void *element, size_t element_size)
{
	char *limit, *end;

	limit = block_buffer;
	limit += fs->fs_blocksize;

	end = element;
	end += element_size;

	return end > limit;
}


static inline int ocfs2_refcount_tree(struct ocfs2_super_block *osb)
{
	if (OCFS2_HAS_INCOMPAT_FEATURE(osb,
				       OCFS2_FEATURE_INCOMPAT_REFCOUNT_TREE))
		return 1;
	return 0;
}

/*
 * shamelessly lifted from the kernel
 *
 * min()/max() macros that also do
 * strict type-checking.. See the
 * "unnecessary" pointer comparison.
 */
#define ocfs2_min(x,y) ({ \
	const typeof(x) _x = (x);       \
	const typeof(y) _y = (y);       \
	(void) (&_x == &_y);            \
	_x < _y ? _x : _y; })
                                                                                
#define ocfs2_max(x,y) ({ \
	const typeof(x) _x = (x);       \
	const typeof(y) _y = (y);       \
	(void) (&_x == &_y);            \
	_x > _y ? _x : _y; })

/* lifted from the kernel. include/linux/kernel.h */
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/*
 * DEPRECATED: Extent/block iterate functions.
 *
 * Do not use these for reading/writing regular files - they don't properly
 * handle holes or inline data.
 */

/* Return flags for the extent iterator functions */
#define OCFS2_EXTENT_CHANGED	0x01
#define OCFS2_EXTENT_ABORT	0x02
#define OCFS2_EXTENT_ERROR	0x04

/*
 * Extent iterate flags
 *
 * OCFS2_EXTENT_FLAG_APPEND indicates that the iterator function should
 * be called on extents past the leaf next_free_rec.  This is used by
 * ocfs2_expand_dir() to add a new extent to a directory (via
 * OCFS2_BLOCK_FLAG_APPEND and the block iteration functions).
 *
 * OCFS2_EXTENT_FLAG_DEPTH_TRAVERSE indicates that the iterator
 * function for tree_depth > 0 records (ocfs2_extent_blocks, iow)
 * should be called after all of the extents contained in the
 * extent_block are processed.  This is useful if you are going to be
 * deallocating extents.
 *
 * OCFS2_EXTENT_FLAG_DATA_ONLY indicates that the iterator function
 * should be called for data extents (depth == 0) only.
 */
#define OCFS2_EXTENT_FLAG_APPEND		0x01
#define OCFS2_EXTENT_FLAG_DEPTH_TRAVERSE	0x02
#define OCFS2_EXTENT_FLAG_DATA_ONLY		0x04


/* Return flags for the block iterator functions */
#define OCFS2_BLOCK_CHANGED	0x01
#define OCFS2_BLOCK_ABORT	0x02
#define OCFS2_BLOCK_ERROR	0x04

/*
 * Block iterate flags
 *
 * In OCFS2, block iteration runs through the blocks contained in an
 * inode's data extents.  As such, "DATA_ONLY" and "DEPTH_TRAVERSE"
 * can't really apply.
 *
 * OCFS2_BLOCK_FLAG_APPEND is as OCFS2_EXTENT_FLAG_APPEND, except on a
 * blocksize basis.  This may mean that the underlying extent already
 * contains the space for a new block, and i_size is updated
 * accordingly.
 */
#define OCFS2_BLOCK_FLAG_APPEND		0x01

errcode_t ocfs2_extent_iterate(ocfs2_filesys *fs,
			       uint64_t blkno,
			       int flags,
			       char *block_buf,
			       int (*func)(ocfs2_filesys *fs,
					   struct ocfs2_extent_rec *rec,
					   int tree_depth,
					   uint32_t ccount,
					   uint64_t ref_blkno,
					   int ref_recno,
					   void *priv_data),
			       void *priv_data);
errcode_t ocfs2_extent_iterate_inode(ocfs2_filesys *fs,
				     struct ocfs2_dinode *inode,
				     int flags,
				     char *block_buf,
				     int (*func)(ocfs2_filesys *fs,
					         struct ocfs2_extent_rec *rec,
					         int tree_depth,
					         uint32_t ccount,
					         uint64_t ref_blkno,
					         int ref_recno,
					         void *priv_data),
					         void *priv_data);
errcode_t ocfs2_block_iterate(ocfs2_filesys *fs,
			      uint64_t blkno,
			      int flags,
			      int (*func)(ocfs2_filesys *fs,
					  uint64_t blkno,
					  uint64_t bcount,
					  uint16_t ext_flags,
					  void *priv_data),
			      void *priv_data);
errcode_t ocfs2_block_iterate_inode(ocfs2_filesys *fs,
				    struct ocfs2_dinode *inode,
				    int flags,
				    int (*func)(ocfs2_filesys *fs,
						uint64_t blkno,
						uint64_t bcount,
						uint16_t ext_flags,
						void *priv_data),
				    void *priv_data);

#define OCFS2_XATTR_ABORT	0x01
#define OCFS2_XATTR_ERROR	0x02
errcode_t ocfs2_xattr_iterate(ocfs2_cached_inode *ci,
			      int (*func)(ocfs2_cached_inode *ci,
					  char *xe_buf,
					  uint64_t xe_blkno,
					  struct ocfs2_xattr_entry *xe,
					  char *value_buf,
					  uint64_t value_blkno,
					  void *value,
					  int in_bucket,
					  void *priv_data),
			      void *priv_data);

uint32_t ocfs2_xattr_uuid_hash(unsigned char *uuid);
uint32_t ocfs2_xattr_name_hash(uint32_t uuid_hash, const char *name,
			       int name_len);
int ocfs2_tree_find_leaf(ocfs2_filesys *fs, struct ocfs2_extent_list *el,
			 uint64_t el_blkno, char *el_blk,
			 uint32_t cpos, char **leaf_buf);
uint16_t ocfs2_xattr_buckets_per_cluster(ocfs2_filesys *fs);
uint16_t ocfs2_blocks_per_xattr_bucket(ocfs2_filesys *fs);
/* See ocfs2_swap_extent_list() for a discussion of obj */
void ocfs2_swap_xattrs_to_cpu(ocfs2_filesys *fs, void *obj,
			      struct ocfs2_xattr_header *xh);
void ocfs2_swap_xattrs_from_cpu(ocfs2_filesys *fs, void *obj,
				struct ocfs2_xattr_header *xh);
void ocfs2_swap_xattr_block_to_cpu(ocfs2_filesys *fs,
				   struct ocfs2_xattr_block *xb);
void ocfs2_swap_xattr_block_from_cpu(ocfs2_filesys *fs,
				     struct ocfs2_xattr_block *xb);
errcode_t ocfs2_read_xattr_block(ocfs2_filesys *fs,
				 uint64_t blkno,
				 char *xb_buf);
errcode_t ocfs2_write_xattr_block(ocfs2_filesys *fs,
				  uint64_t blkno,
				  char *xb_buf);
errcode_t ocfs2_xattr_get_rec(ocfs2_filesys *fs,
			      struct ocfs2_xattr_block *xb,
			      uint32_t name_hash,
			      uint64_t *p_blkno,
			      uint32_t *e_cpos,
			      uint32_t *num_clusters);
uint16_t ocfs2_xattr_value_real_size(uint16_t name_len, uint16_t value_len);
uint16_t ocfs2_xattr_min_offset(struct ocfs2_xattr_header *xh, uint16_t size);
uint16_t ocfs2_xattr_name_value_len(struct ocfs2_xattr_header *xh);
errcode_t ocfs2_read_xattr_bucket(ocfs2_filesys *fs,
				  uint64_t blkno,
				  char *bucket_buf);
errcode_t ocfs2_write_xattr_bucket(ocfs2_filesys *fs,
				   uint64_t blkno,
				   char *bucket_buf);
errcode_t ocfs2_xattr_value_truncate(ocfs2_filesys *fs, uint64_t ino,
				     struct ocfs2_xattr_value_root *xv);
errcode_t ocfs2_xattr_tree_truncate(ocfs2_filesys *fs,
				    struct ocfs2_xattr_tree_root *xt);
errcode_t ocfs2_extent_iterate_xattr(ocfs2_filesys *fs,
				     struct ocfs2_extent_list *el,
				     uint64_t last_eb_blk,
				     int flags,
				     int (*func)(ocfs2_filesys *fs,
						struct ocfs2_extent_rec *rec,
						int tree_depth,
						uint32_t ccount,
						uint64_t ref_blkno,
						int ref_recno,
						void *priv_data),
				     void *priv_data,
				     int *changed);
errcode_t ocfs2_delete_xattr_block(ocfs2_filesys *fs, uint64_t blkno);

#endif  /* _FILESYS_H */
