/*
 * util.h
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

#ifndef __O2FSCK_UTIL_H__
#define __O2FSCK_UTIL_H__

#include <stdlib.h>
#include "fsck.h"

/* we duplicate e2fsck's error codes to make everyone's life easy */
#define FSCK_OK          0      /* No errors */
#define FSCK_NONDESTRUCT 1      /* File system errors corrected */
#define FSCK_REBOOT      2      /* System should be rebooted */
#define FSCK_UNCORRECTED 4      /* File system errors left uncorrected */
#define FSCK_ERROR       8      /* Operational error */
#define FSCK_USAGE       16     /* Usage or syntax error */
#define FSCK_CANCELED    32     /* Aborted with a signal or ^C */
#define FSCK_LIBRARY     128    /* Shared library error */

/* Managing the I/O cache */
enum o2fsck_cache_hint {
	O2FSCK_CACHE_MODE_NONE = 0,
	O2FSCK_CACHE_MODE_JOURNAL,	/* Enough of a cache to replay a
					   journal */
	O2FSCK_CACHE_MODE_FULL,		/* Enough of a cache to recover the
					   filesystem */
};
void o2fsck_init_cache(o2fsck_state *ost, enum o2fsck_cache_hint hint);
int o2fsck_worth_caching(int blocks_to_read);
void o2fsck_reset_blocks_cached(void);

void o2fsck_write_inode(o2fsck_state *ost, uint64_t blkno,
                        struct ocfs2_dinode *di);
void o2fsck_mark_cluster_allocated(o2fsck_state *ost, uint32_t cluster);
void o2fsck_mark_clusters_allocated(o2fsck_state *ost, uint32_t cluster,
				    uint32_t num);
void o2fsck_mark_cluster_unallocated(o2fsck_state *ost, uint32_t cluster);
errcode_t o2fsck_type_from_dinode(o2fsck_state *ost, uint64_t ino,
				  uint8_t *type);
errcode_t o2fsck_read_publish(o2fsck_state *ost);
size_t o2fsck_bitcount(unsigned char *bytes, size_t len);

errcode_t handle_slots_system_file(ocfs2_filesys *fs,
				   int type,
				   errcode_t (*func)(ocfs2_filesys *fs,
						     struct ocfs2_dinode *di,
						     int slot));

/* How to abort but clean up the cluster state */
void o2fsck_abort(void);

/*
 * Wrap the ocfs2 bitmap functions to abort when errors are found.  They're
 * not supposed to fail, so we want to handle it.
 */
void __o2fsck_bitmap_set(ocfs2_bitmap *bitmap, uint64_t bitno, int *oldval,
			 const char *where);
void __o2fsck_bitmap_clear(ocfs2_bitmap *bitmap, uint64_t bitno, int *oldval,
			   const char *where);

/* These wrappers pass the caller into __o2fsck_bitmap_*() */
#define o2fsck_bitmap_set(_map, _bit, _old)     			\
	__o2fsck_bitmap_set((_map), (_bit), (_old), __FUNCTION__)
#define o2fsck_bitmap_clear(_map, _bit, _old)				\
	__o2fsck_bitmap_clear((_map), (_bit), (_old), __FUNCTION__);

void o2fsck_init_resource_track(struct o2fsck_resource_track *rt,
				io_channel *channel);
void o2fsck_compute_resource_track(struct o2fsck_resource_track *rt,
				   io_channel *channel);
void o2fsck_print_resource_track(char *pass, o2fsck_state *ost,
				 struct o2fsck_resource_track *rt,
				 io_channel *channel);
void o2fsck_add_resource_track(struct o2fsck_resource_track *rt1,
			       struct o2fsck_resource_track *rt2);

#endif /* __O2FSCK_UTIL_H__ */
