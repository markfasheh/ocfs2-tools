/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * fsck.h
 *
 * Copyright (C) 2002 Oracle Corporation.  All rights reserved.
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
 * Author: Zach Brown
 */

#ifndef __O2FSCK_FSCK_H__
#define __O2FSCK_FSCK_H__

#include "icount.h"
#include "dirblocks.h"
#include "tools-internal/progress.h"

struct refcount_file;

/*
 * This structure is used for keeping track of how much resources have
 * been used for a particular pass of fsck.ocfs2.
 */
struct o2fsck_resource_track {
	struct timeval		rt_real_time;
	struct timeval		rt_user_time;
	struct timeval		rt_sys_time;
	struct ocfs2_io_stats	rt_io_stats;
};

typedef struct _o2fsck_state {
	ocfs2_filesys 	*ost_fs;

	ocfs2_cached_inode	*ost_global_inode_alloc;
	ocfs2_cached_inode	**ost_inode_allocs;

	ocfs2_bitmap	*ost_dir_inodes;
	ocfs2_bitmap	*ost_reg_inodes;

	ocfs2_bitmap	*ost_allocated_clusters;
	ocfs2_bitmap    *ost_duplicate_clusters;

	/* This is no more than a cache of what we know the i_link_count
	 * in each inode to currently be.  If an inode is marked in used_inodes
	 * this had better be up to date. */
	o2fsck_icount	*ost_icount_in_inodes;
	/* this records references to each inode from other directory 
	 * entries, including '.' and '..'. */
	o2fsck_icount	*ost_icount_refs;

	o2fsck_dirblocks	ost_dirblocks;

	uint32_t	ost_fs_generation;
	uint64_t	ost_lostfound_ino;

	uint32_t	ost_num_clusters;

	struct rb_root	ost_dir_parents;

	struct rb_root	ost_refcount_trees;
	struct refcount_file *ost_latest_file;

	unsigned	ost_ask:1,	/* confirm with the user */
			ost_answer:1,	/* answer if we don't ask the user */
			ost_force:1,	/* -f supplied; force check */
			ost_skip_o2cb:1,/* -F: ignore cluster services */
			ost_write_inode_alloc_asked:1,
			ost_write_inode_alloc:1,
			ost_write_error:1,
			ost_write_cluster_alloc_asked:1,
 			ost_write_cluster_alloc:1,
 			ost_saw_error:1, /* if we think there are still errors
 					  * on disk we'll mark the sb as having
 					  * errors as we exit */
 			ost_stale_mounts:1, /* set when reading publish blocks
 					     * that still indicated mounted */
			ost_fix_fs_gen:1,
			ost_has_journal_dirty:1,
			ost_compress_dirs:1,
			ost_show_stats:1,
			ost_show_extended_stats:1;
	errcode_t ost_err;

	struct o2fsck_resource_track	ost_rt;
	struct tools_progress		*ost_prog;

	/* counters */
	uint32_t	ost_file_count;
	uint32_t	ost_inline_file_count;
	uint32_t	ost_dir_count;
	uint32_t	ost_inline_dir_count;
	uint32_t	ost_reflinks_count;
	uint32_t	ost_links_count;
	uint32_t	ost_chardev_count;
	uint32_t	ost_sockets_count;
	uint32_t	ost_fifo_count;
	uint32_t	ost_blockdev_count;
	uint32_t	ost_symlinks_count;
	uint32_t	ost_fast_symlinks_count;
	uint32_t	ost_orphan_count;
	uint32_t	ost_orphan_deleted_count;
#define OCFS2_MAX_PATH_DEPTH	5
	uint32_t	ost_tree_depth_count[OCFS2_MAX_PATH_DEPTH + 1];
} o2fsck_state;

errcode_t o2fsck_state_reinit(ocfs2_filesys *fs, o2fsck_state *ost);

#define kbytes(x)		(((x) + 1023) / 1024)
#define mbytes(x)		(((x) + 1048575) / 1048576)
#define gbytes(x)		(((x) + 1073741823) / 1073741824)

/* The idea is to let someone off-site run fsck and have it give us 
 * enough information to diagnose problems with */
extern int verbose;
#define verbosef(fmt, args...) do {					\
	if (verbose)							\
		printf("%s:%d | " fmt, __FUNCTION__, __LINE__, args);\
} while (0)

#endif /* __O2FSCK_FSCK_H__ */

