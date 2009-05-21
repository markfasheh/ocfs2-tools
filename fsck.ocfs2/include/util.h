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
#endif /* __O2FSCK_UTIL_H__ */
