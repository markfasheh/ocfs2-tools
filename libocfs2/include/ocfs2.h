/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * filesys.h
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

#include <linux/types.h>

#include <et/com_err.h>

#include "byteorder.h"

#include <kernel-list.h>
#include <kernel-rbtree.h>

#if OCFS2_FLAT_INCLUDES
#include "ocfs2_err.h"

#include "ocfs2_fs.h"
#else
#include <ocfs2/ocfs2_err.h>

#include <ocfs2/ocfs2_fs.h>
#endif

#define OCFS2_LIB_FEATURE_INCOMPAT_SUPP		OCFS2_FEATURE_INCOMPAT_SUPP
#define OCFS2_LIB_FEATURE_RO_COMPAT_SUPP	OCFS2_FEATURE_RO_COMPAT_SUPP

/* Flags for the ocfs2_filesys structure */
#define OCFS2_FLAG_RO		0x00
#define OCFS2_FLAG_RW		0x01
#define OCFS2_FLAG_CHANGED	0x02
#define OCFS2_FLAG_DIRTY	0x04
#define OCFS2_FLAG_SWAP_BYTES	0x08
#define OCFS2_FLAG_BUFFERED	0x10

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


/* Return flags for the directory iterator functions */
#define OCFS2_DIRENT_CHANGED	0x01
#define OCFS2_DIRENT_ABORT	0x02
#define OCFS2_DIRENT_ERROR	0x04

/* Directory iterator flags */
#define OCFS2_DIRENT_FLAG_INCLUDE_EMPTY		0x01
#define OCFS2_DIRENT_FLAG_INCLUDE_REMOVED	0x02

/* Return flags for the chain iterator functions */
#define OCFS2_CHAIN_CHANGED	0x01
#define OCFS2_CHAIN_ABORT	0x02
#define OCFS2_CHAIN_ERROR	0x04


/* Directory constants */
#define OCFS2_DIRENT_DOT_FILE		1
#define OCFS2_DIRENT_DOT_DOT_FILE	2
#define OCFS2_DIRENT_OTHER_FILE		3
#define OCFS2_DIRENT_DELETED_FILE	4

/* Check if mounted flags */
#define OCFS2_MF_MOUNTED         0x01
#define OCFS2_MF_ISROOT          0x02
#define OCFS2_MF_READONLY        0x04
#define OCFS2_MF_SWAP            0x08
#define OCFS2_MF_MOUNTED_CLUSTER 0x16

/* Some constants used in heartbeat */
#define OCFS2_NODE_MAP_MAX_NODES	256
#define OCFS2_HBT_WAIT			10

/* check_heartbeats progress states */
#define OCFS2_CHB_START		1
#define OCFS2_CHB_WAITING	2
#define OCFS2_CHB_COMPLETE	3

typedef void (*ocfs2_chb_notify)(int state, char *progress, void *data);

typedef struct _ocfs2_filesys ocfs2_filesys;
typedef struct _ocfs2_cached_inode ocfs2_cached_inode;
typedef struct _io_channel io_channel;
typedef struct _ocfs2_extent_map ocfs2_extent_map;
typedef struct _ocfs2_inode_scan ocfs2_inode_scan;
typedef struct _ocfs2_bitmap ocfs2_bitmap;
typedef struct _ocfs2_nodes ocfs2_nodes;
typedef struct _ocfs2_devices ocfs2_devices;

struct _ocfs2_filesys {
	char *fs_devname;
	uint32_t fs_flags;
	io_channel *fs_io;
	ocfs2_dinode *fs_super;
	ocfs2_dinode *fs_orig_super;
	unsigned int fs_blocksize;
	unsigned int fs_clustersize;
	uint32_t fs_clusters;
	uint64_t fs_blocks;
	uint32_t fs_umask;
	uint64_t fs_root_blkno;
	uint64_t fs_sysdir_blkno;
	uint64_t fs_bm_blkno;

	/* Reserved for the use of the calling application. */
	void *fs_private;
};

struct _ocfs2_cached_inode {
	struct _ocfs2_filesys *ci_fs;
	uint64_t ci_blkno;
	ocfs2_dinode *ci_inode;
	ocfs2_extent_map *ci_map;
};

struct _ocfs2_nodes {
	struct list_head list;
	char node_name[MAX_NODE_NAME_LENGTH+1];
	uint16_t node_num;
};

struct _ocfs2_devices {
	struct list_head list;
	char dev_name[100];
	uint8_t label[64];
	uint8_t uuid[16];
	int mount_flags;
	int fs_type;		/* 0=unknown, 1=ocfs, 2=ocfs2 */
	void *private;
	struct list_head node_list;
};

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
errcode_t io_read_block(io_channel *channel, int64_t blkno, int count,
			char *data);
errcode_t io_write_block(io_channel *channel, int64_t blkno, int count,
			 const char *data);

errcode_t ocfs2_open(const char *name, int flags,
		     unsigned int superblock, unsigned int blksize,
		     ocfs2_filesys **ret_fs);
errcode_t ocfs2_flush(ocfs2_filesys *fs);
errcode_t ocfs2_close(ocfs2_filesys *fs);
void ocfs2_freefs(ocfs2_filesys *fs);

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

errcode_t ocfs2_load_extent_map(ocfs2_filesys *fs,
				ocfs2_cached_inode *cinode);
errcode_t ocfs2_free_extent_map(ocfs2_filesys *fs,
				ocfs2_cached_inode *cinode);
errcode_t ocfs2_extent_map_add(ocfs2_cached_inode *cinode,
			       ocfs2_extent_rec *rec);
errcode_t ocfs2_extent_map_clear(ocfs2_cached_inode *cinode,
				 uint32_t cpos, uint32_t clusters);

errcode_t ocfs2_create_journal_superblock(ocfs2_filesys *fs,
					  uint32_t size, int flags,
					  char **ret_jsb);

errcode_t ocfs2_read_extent_block(ocfs2_filesys *fs, uint64_t blkno,
       				  char *eb_buf);
errcode_t ocfs2_write_extent_block(ocfs2_filesys *fs, uint64_t blkno,
       				   char *eb_buf);
errcode_t ocfs2_extent_iterate(ocfs2_filesys *fs,
			       uint64_t blkno,
			       int flags,
			       char *block_buf,
			       int (*func)(ocfs2_filesys *fs,
					   ocfs2_extent_rec *rec,
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
					  void *priv_data),
			      void *priv_data);

errcode_t ocfs2_read_dir_block(ocfs2_filesys *fs, uint64_t block,
			       void *buf);
errcode_t ocfs2_write_dir_block(ocfs2_filesys *fs, uint64_t block,
				void *buf);

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
				    int node_num, uint64_t *blkno);

errcode_t ocfs2_link(ocfs2_filesys *fs, uint64_t dir, const char *name,
		     uint64_t ino, int flags);

errcode_t ocfs2_unlink(ocfs2_filesys *fs, uint64_t dir,
		       const char *name, uint64_t ino, int flags);

errcode_t ocfs2_open_inode_scan(ocfs2_filesys *fs,
				ocfs2_inode_scan **ret_scan);
void ocfs2_close_inode_scan(ocfs2_inode_scan *scan);
errcode_t ocfs2_get_next_inode(ocfs2_inode_scan *scan,
			       uint64_t *blkno, char *inode);

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
uint64_t ocfs2_bitmap_get_set_bits(ocfs2_bitmap *bitmap);

errcode_t ocfs2_get_device_size(const char *file, int blocksize,
				uint32_t *retblocks);

errcode_t ocfs2_check_if_mounted(const char *file, int *mount_flags);
errcode_t ocfs2_check_mount_point(const char *device, int *mount_flags,
		                  char *mtpt, int mtlen);

errcode_t ocfs2_read_whole_file(ocfs2_filesys *fs, uint64_t blkno,
				char **buf, int *len);

errcode_t ocfs2_check_heartbeat(char *device, int quick_detect,
			       	int *mount_flags, struct list_head *nodes_list,
                                ocfs2_chb_notify notify, void *user_data);

errcode_t ocfs2_check_heartbeats(struct list_head *dev_list, int quick_detect,
				 ocfs2_chb_notify notify, void *user_data);

errcode_t ocfs2_get_ocfs1_label(char *device, uint8_t *label, uint16_t label_len,
				uint8_t *uuid, uint16_t uuid_len);

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

#endif  /* _FILESYS_H */
