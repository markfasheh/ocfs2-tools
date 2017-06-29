/*
 * main.h
 *
 * Function prototypes, macros, etc. for related 'C' files
 *
 * Copyright (C) 2004, 2011 Oracle.  All rights reserved.
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301 USA.
 *
 */

#ifndef __MAIN_H__
#define __MAIN_H__

#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statfs.h>
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

#include "ocfs2/ocfs2.h"
#include "ocfs2-kernel/ocfs1_fs_compat.h"

struct dbgfs_gbls {
	char *progname;
	int allow_write;
	int imagefile;
	int interactive;
	char *device;
	ocfs2_filesys *fs;
	char *cwd;
	char *cmd;
	uint64_t cwd_blkno;
	char *blockbuf;
	uint64_t max_clusters;
	uint64_t max_blocks;
	uint64_t root_blkno;
	uint64_t sysdir_blkno;
	uint64_t hb_blkno;
	uint64_t slotmap_blkno;
	uint64_t jrnl_blkno[OCFS2_MAX_SLOTS];
};

struct dbgfs_opts {
	int allow_write;
	int imagefile;
	int no_prompt;
	uint32_t sb_num;
	char *cmd_file;
	char *one_cmd;
	char *device;
};

#define DBGFS_FATAL(fmt, arg...)					\
	({ fprintf(stderr, "ERROR at %s, %d: " fmt ".  EXITING!!!\n",	\
		   __FILE__, __LINE__, ##arg);				\
	 raise(SIGTERM);						\
	 exit(1);							\
	 })

#define DBGFS_FATAL_STR(str)		DBGFS_FATAL(str, "")

#define DBGFS_WARN(fmt, arg...)						\
	fprintf(stderr, "WARNING at %s, %d: " fmt ".\n",		\
		__FILE__, __LINE__, ##arg)

#define DBGFS_WARN_STR(str)		DBGFS_WARN(str, "")

#undef max
#define max(a,b)	((a) > (b) ? (a) : (b))
#undef min
#define min(a,b)	((a) < (b) ? (a) : (b))

#define dbfs_swap(a, b)			\
	do {				\
		typeof(a) c = (a);	\
		(a) = (b);		\
		(b) = c;		\
	} while (0);

/* remaining headers */
#include <commands.h>
#include "ocfs2-kernel/kernel-list.h"
#include <utils.h>
#include <journal.h>
#include <find_block_inode.h>
#include <find_inode_paths.h>
#include <dump_fs_locks.h>
#include <dump_dlm_locks.h>
#include <dump.h>
#include <stat_sysdir.h>
#include <dump_net_stats.h>

#endif		/* __MAIN_H__ */
