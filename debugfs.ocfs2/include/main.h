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
#include <utime.h>
#include <getopt.h>
#include <glib.h>

#include <readline/readline.h>
#include <readline/history.h>

#include <linux/types.h>

#include "ocfs2.h"
#include "ocfs2_fs.h"
#include "ocfs1_fs_compat.h"

enum {
	CONFIG,
	PUBLISH,
	VOTE
};

typedef struct _dbgfs_glbs {
	char *progname;
	int allow_write;
	int interactive;
	char *device;
	ocfs2_filesys *fs;
	char *cwd;
	uint64_t cwd_blkno;
	char *blockbuf;
	uint64_t max_clusters;
	uint64_t max_blocks;
	uint64_t root_blkno;
	uint64_t sysdir_blkno;
	uint64_t hb_blkno;
	uint64_t slotmap_blkno;
	uint64_t jrnl_blkno[256];
} dbgfs_gbls;

typedef struct _dbgfs_opts {
	int allow_write;
	char *cmd_file;
	char *device;
} dbgfs_opts;

#define DBGFS_FATAL(fmt, arg...)	({ fprintf(stderr, "ERROR at %s, %d: " fmt ".  EXITING!!!\n", \
						   __FILE__, __LINE__, ##arg);  \
					   raise (SIGTERM);	\
					   exit(1); \
					 })

#define DBGFS_FATAL_STR(str)		DBGFS_FATAL(str, "")

#define DBGFS_WARN(fmt, arg...)		fprintf(stderr, "WARNING at %s, %d: " fmt ".\n", \
						__FILE__, __LINE__, ##arg)

#define DBGFS_WARN_STR(str)		DBGFS_WARN(str, "")

#undef max
#define max(a,b)	((a) > (b) ? (a) : (b))
#undef min
#define min(a,b)	((a) < (b) ? (a) : (b))

/* Publish flags */
#define  FLAG_FILE_CREATE         0x00000001
#define  FLAG_FILE_EXTEND         0x00000002
#define  FLAG_FILE_DELETE         0x00000004
#define  FLAG_FILE_RENAME         0x00000008
#define  FLAG_FILE_UPDATE         0x00000010
#define  FLAG_FILE_RECOVERY       0x00000020
#define  FLAG_FILE_CREATE_DIR     0x00000040
#define  FLAG_FILE_UPDATE_OIN     0x00000080
#define  FLAG_FILE_RELEASE_MASTER 0x00000100
#define  FLAG_RELEASE_DENTRY      0x00000200
#define  FLAG_CHANGE_MASTER       0x00000400
#define  FLAG_ADD_OIN_MAP         0x00000800
#define  FLAG_DIR                 0x00001000
#define  FLAG_REMASTER            0x00002000
#define  FLAG_FAST_PATH_LOCK      0x00004000
#define  FLAG_FILE_UNUSED5        0x00008000
#define  FLAG_FILE_UNUSED6        0x00010000
//#define  FLAG_DEL_NAME            0x00020000
//#define  FLAG_DEL_INODE           0x00040000
#define  FLAG_FILE_UNUSED7        0x00080000
#define  FLAG_FILE_UNUSED8        0x00100000
#define  FLAG_FILE_UNUSED9        0x00200000
#define  FLAG_FILE_RELEASE_CACHE  0x00400000
#define  FLAG_FILE_UNUSED10       0x00800000
#define  FLAG_FILE_UNUSED11       0x01000000
#define  FLAG_FILE_UNUSED12       0x02000000
#define  FLAG_FILE_UNUSED13       0x04000000
#define  FLAG_FILE_TRUNCATE       0x08000000
#define  FLAG_DROP_READONLY       0x10000000 
#define  FLAG_READDIR             0x20000000 
#define  FLAG_ACQUIRE_LOCK        0x40000000 
#define  FLAG_RELEASE_LOCK        0x80000000 

/* Vote flags */
#define  FLAG_VOTE_NODE               0x1
#define  FLAG_VOTE_OIN_UPDATED        0x2
#define  FLAG_VOTE_OIN_ALREADY_INUSE  0x4
#define  FLAG_VOTE_UPDATE_RETRY       0x8
#define  FLAG_VOTE_FILE_DEL           0x10


/* remaining headers */
#include <commands.h>
#include <kernel-list.h>
#include <utils.h>
#include <journal.h>
#include <dump.h>

#endif		/* __MAIN_H__ */
