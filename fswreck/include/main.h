/*
 * main.h
 *
 * Function prototypes, macros, etc. for related 'C' files
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
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
 * Authors: Sunil Mushran
 */

#ifndef __MAIN_H__
#define __MAIN_H__

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/raw.h>
#include <linux/kdev_t.h>
#include <inttypes.h>

#include <glib.h>

#include <linux/types.h>

#include "ocfs2_fs.h"
#include "ocfs2_disk_dlm.h"
#include "ocfs1_fs_compat.h"

typedef struct _fswrk_ctxt {
	char *device;
	int fd;
	uint64_t sys_global_inode;
	uint64_t sys_dlm;
	uint64_t sys_global_bitmap;
	uint64_t sys_orphan;
	uint64_t sys_extent[256];
	uint64_t sys_inode[256];
	uint64_t sys_journal[256];
	uint64_t sys_local[256];
	uint64_t max_clusters;
	uint64_t max_blocks;
	ocfs2_dinode *super_block;
	ocfs2_dinode *root_dir;
	ocfs2_dinode *system_dir;
} fswrk_ctxt;

void *memalign(size_t boundary, size_t size);

#define safefree(_p)	do {if (_p) { free(_p); (_p) = NULL; } } while (0)

#define FSWRK_FATAL(fmt, arg...)	({ fprintf(stderr, "ERROR at %s, %d: " fmt ".  EXITING!!!\n", \
						   __FILE__, __LINE__, ##arg);  \
					   raise (SIGTERM);	\
					   exit(1); \
					 })

#define FSWRK_FATAL_STR(str)		DBGFS_FATAL(str, "")

#define FSWRK_WARN(fmt, arg...)		fprintf(stderr, "WARNING at %s, %d: " fmt ".\n", \
						__FILE__, __LINE__, ##arg)

#define FSWRK_WARN_STR(str)		DBGFS_WARN(str, "")

#undef max
#define max(a,b)	((a) > (b) ? (a) : (b))
#undef min
#define min(a,b)	((a) < (b) ? (a) : (b))

/* remaining headers */
#include <readfs.h>
#include <utils.h>

#endif		/* __MAIN_H__ */
