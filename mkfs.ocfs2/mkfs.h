/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * mkfs2.h
 *
 * Definitions, function prototypes, etc.
 *
 * Copyright (C) 2004, 2005 Oracle.  All rights reserved.
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
 */

#define _LARGEFILE64_SOURCE

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <errno.h>
#include <malloc.h>
#include <time.h>
#include <libgen.h>
#include <netinet/in.h>
#include <inttypes.h>
#include <ctype.h>
#include <assert.h>

#include <uuid/uuid.h>

#include "ocfs2/ocfs2.h"
#include "ocfs2/bitops.h"
#include "ocfs2-kernel/ocfs1_fs_compat.h"

#include <signal.h>
#include <libgen.h>


#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#define MOUNT_LOCAL             1
#define MOUNT_CLUSTER           2
#define MOUNT_LOCAL_STR         "local"
#define MOUNT_CLUSTER_STR       "cluster"

#define MIN_RESERVED_TAIL_BLOCKS    8

#define SUPERBLOCK_BLOCKS       3
#define ROOTDIR_BLOCKS		1
#define SYSDIR_BLOCKS		1
#define LOSTDIR_BLOCKS		1

#define CLEAR_CHUNK		1048576

#define OCFS2_OS_LINUX           0
#define OCFS2_OS_HURD            1
#define OCFS2_OS_MASIX           2
#define OCFS2_OS_FREEBSD         3
#define OCFS2_OS_LITES           4

#define OCFS2_DFL_MAX_MNT_COUNT          20
#define OCFS2_DFL_CHECKINTERVAL          0

#define SYSTEM_FILE_NAME_MAX   40

#define ONE_MB_SHIFT           20
#define ONE_GB_SHIFT           30

#define BITMAP_AUTO_MAX        786432

#define AUTO_CLUSTERSIZE       65536

#define CLUSTERS_MAX           (UINT32_MAX - 1)

#define MAX_EXTALLOC_RESERVE_PERCENT	5

enum {
	SFI_JOURNAL,
	SFI_CLUSTER,
	SFI_LOCAL_ALLOC,
	SFI_HEARTBEAT,
	SFI_CHAIN,
	SFI_TRUNCATE_LOG,
	SFI_QUOTA,
	SFI_OTHER
};

typedef struct _SystemFileInfo SystemFileInfo;

struct _SystemFileInfo {
	char *name;
	int type;
	int global;
	int mode;
};

struct BitInfo {
	uint32_t used_bits;
	uint32_t total_bits;
};

typedef struct _AllocGroup AllocGroup;
typedef struct _SystemFileDiskRecord SystemFileDiskRecord;
typedef struct _DirData DirData;

struct _AllocGroup {
	char *name;
	struct ocfs2_group_desc *gd;
	SystemFileDiskRecord *alloc_inode;
	uint32_t chain_free;
	uint32_t chain_total;
	struct _AllocGroup *next;
};


struct _SystemFileDiskRecord {
	uint64_t fe_off;
        uint16_t suballoc_bit;
	uint64_t extent_off;
	uint64_t extent_len;
	uint64_t file_size;

	uint64_t chain_off;
	AllocGroup *group;

	struct BitInfo bi;
	struct _AllocBitmap *bitmap;

	int flags;
	int links;
	int mode;
	int cluster_bitmap;

	/* record the dir entry so that inline dir can be stored with file. */
	DirData *dir_data;
};

typedef struct _AllocBitmap AllocBitmap;

struct _AllocBitmap {
	AllocGroup **groups;

	uint32_t valid_bits;
	uint32_t unit;
	uint32_t unit_bits;

	char *name;

	uint64_t fe_disk_off;

	SystemFileDiskRecord *bm_record;
	SystemFileDiskRecord *alloc_record;
	int num_chains;
};

struct _DirData {
	void *buf;
	int last_off;

	SystemFileDiskRecord *record;
};

typedef struct _State State;

struct _State {
  	char *progname;

	int verbose;
	int quiet;
	int force;
	int prompt;
	int hb_dev;
	int mount;
	int no_backup_super;
	int inline_data;
	int dx_dirs;
	int dry_run;

	uint32_t blocksize;
	uint32_t blocksize_bits;

	uint32_t cluster_size;
	uint32_t cluster_size_bits;

	uint64_t specified_size_in_blocks;
	uint64_t volume_size_in_bytes;
	uint32_t volume_size_in_clusters;
	uint64_t volume_size_in_blocks;

	uint32_t pagesize_bits;

	uint64_t reserved_tail_size;

	unsigned int initial_slots;

	uint64_t journal_size_in_bytes;
	int journal64;

	uint32_t extent_alloc_size_in_clusters;

	char *vol_label;
	char *device_name;
	unsigned char *uuid;
	char *cluster_stack;
	char *cluster_name;
	uint8_t stack_flags;
	int global_heartbeat;
	uint32_t vol_generation;

	int fd;

	time_t format_time;

	AllocBitmap *global_bm;
	AllocGroup *system_group;
	uint32_t nr_cluster_groups;
	uint16_t global_cpg;
	uint16_t tail_group_bits;
	uint32_t first_cluster_group;
	uint64_t first_cluster_group_blkno;

	ocfs2_fs_options feature_flags;

	enum ocfs2_mkfs_types fs_type;
};

int is_classic_stack(char *stack_name);
void cluster_fill(char **stack_name, char **cluster_name, uint8_t *stack_flags);
int ocfs2_fill_cluster_information(State *s);
int ocfs2_check_volume(State *s);
