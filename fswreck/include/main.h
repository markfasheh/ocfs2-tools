/*
 * main.h
 *
 * Function prototypes, macros, etc. for related 'C' files
 *
 * Copyright (C) 2006 Oracle.  All rights reserved.
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

#include "ocfs2/byteorder.h"
#include "ocfs2/ocfs2.h"

#define FSWRK_FATAL(fmt, arg...)	({ fprintf(stderr, "ERROR at %s, %d: " fmt ".  EXITING!!!\n", \
						   __FILE__, __LINE__, ##arg);  \
					   raise (SIGTERM);	\
					   exit(1); \
					 })

#define FSWRK_COM_FATAL(__p, __r)	do { com_err(__p, __r, "(%s,%d)", __FILE__, __LINE__); raise (SIGTERM); exit(1); } while(0)

#define FSWRK_FATAL_STR(str)		FSWRK_FATAL(str, "")

#define FSWRK_WARN(fmt, arg...)		fprintf(stderr, "WARNING at %s, %d: " fmt ".\n", \
						__FILE__, __LINE__, ##arg)

#define FSWRK_WARN_STR(str)		FSWRK_WARN(str, "")

#undef max
#define max(a,b)	((a) > (b) ? (a) : (b))
#undef min
#define min(a,b)	((a) < (b) ? (a) : (b))

enum{
	CORRUPT_EXTENT_BLOCK = 13,
	CORRUPT_EXTENT_LIST,
	CORRUPT_EXTENT_REC,
	CORRUPT_CHAIN_LIST,
	CORRUPT_CHAIN_REC,
	CORRUPT_CHAIN_INODE,
	CORRUPT_CHAIN_GROUP,
	CORRUPT_CHAIN_GROUP_MAGIC,
	CORRUPT_CHAIN_CPG,
	CORRUPT_SUPERBLOCK_CLUSTERS_EXCESS,
	CORRUPT_SUPERBLOCK_CLUSTERS_LACK,
	CORRUPT_GROUP_MINOR,
	CORRUPT_GROUP_GENERATION,
	CORRUPT_GROUP_LIST,
	CORRUPT_INODE_FIELD,
	CORRUPT_INODE_NOT_CONNECTED,
	CORRUPT_INODE_ORPHANED,
	CORRUPT_INODE_ALLOC_REPAIR,
	CORRUPT_LOCAL_ALLOC_EMPTY,
	CORRUPT_LOCAL_ALLOC_BITMAP,
	CORRUPT_LOCAL_ALLOC_USED,
	CORRUPT_TRUNCATE_LOG_LIST,
	CORRUPT_TRUNCATE_LOG_REC,
	CORRUPT_SYMLINK,
	CORRUPT_SPECIAL_FILE,
	CORRUPT_DIR_INODE,
	CORRUPT_DIR_DOT,
	CORRUPT_DIR_ENT,
	CORRUPT_DIR_PARENT_DUP,
	CORRUPT_DIR_NOT_CONNECTED,
	CORRUPT_CLUSTER_AND_GROUP_DESC,
	MAX_CORRUPT
	
};

#define ARRAY_ELEMENTS(arr) (sizeof(arr) / sizeof(arr[0]))

/* remaining headers */
#include <corrupt.h>
#include <chain.h>
#include <extent.h>
#include <group.h>
#include <inode.h>
#include <local_alloc.h>
#include <truncate_log.h>
#include <symlink.h>
#include <special.h>
#include <dir.h>
#include <fsck_type.h>

#endif		/* __MAIN_H__ */
