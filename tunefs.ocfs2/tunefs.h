/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * tune.h
 *
 * ocfs2 tune utility
 *
 * Copyright (C) 2004, 2007 Oracle.  All rights reserved.
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

#ifndef _TUNEFS_H
#define _TUNEFS_H

#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE /* Because libc really doesn't want us using O_DIRECT? */

#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fd.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <malloc.h>
#include <time.h>
#include <libgen.h>
#include <netinet/in.h>
#include <inttypes.h>
#include <ctype.h>
#include <signal.h>
#include <uuid/uuid.h>

#include "ocfs2/ocfs2.h"
#include "ocfs2/bitops.h"

#define SYSTEM_FILE_NAME_MAX   40

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#define MOUNT_LOCAL             1
#define MOUNT_CLUSTER           2
#define MOUNT_LOCAL_STR         "local"
#define MOUNT_CLUSTER_STR       "cluster"

enum {
	BACKUP_SUPER_OPTION = CHAR_MAX + 1,
	LIST_SPARSE_FILES,
	FEATURES_OPTION,
	UPDATE_CLUSTER_OPTION,
};

typedef struct _ocfs2_tune_opts {
	uint16_t num_slots;
	uint64_t num_blocks;
	uint64_t jrnl_size;
	char *vol_label;
	char *progname;
	char *device;
	unsigned char *vol_uuid;
	char *queryfmt;
	int mount;
	int verbose;
	int quiet;
	int prompt;
	int backup_super;
	int update_cluster;
	int list_sparse;
	fs_options set_feature;
	fs_options clear_feature;
	char *feature_string;
	time_t tune_time;
} ocfs2_tune_opts;

void block_signals(int how);

void print_query(char *queryfmt);

errcode_t remove_slots(ocfs2_filesys *fs);
errcode_t remove_slot_check(ocfs2_filesys *fs);

errcode_t list_sparse(ocfs2_filesys *fs);
errcode_t set_sparse_file_flag(ocfs2_filesys *fs, char *progname);
errcode_t clear_sparse_file_check(ocfs2_filesys *fs, char *progname,
				  int unwritten_only);
errcode_t clear_sparse_file_flag(ocfs2_filesys *fs, char *progname);
void set_unwritten_extents_flag(ocfs2_filesys *fs);
void free_clear_ctxt(void);

errcode_t reformat_slot_map(ocfs2_filesys *fs);

errcode_t feature_check(ocfs2_filesys *fs);
errcode_t update_feature(ocfs2_filesys *fs);

void get_vol_size(ocfs2_filesys *fs);
errcode_t update_volume_size(ocfs2_filesys *fs, int *changed, int online);
int validate_vol_size(ocfs2_filesys *fs);

errcode_t online_resize_check(ocfs2_filesys *fs);
errcode_t online_resize_lock(ocfs2_filesys *fs);
errcode_t online_resize_unlock(ocfs2_filesys *fs);
#endif /* _TUNEFS_H */
