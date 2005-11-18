/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dump_fs_locks.c
 *
 * Interface with the kernel and dump current fs locking state
 *
 * Copyright (C) 2005 Oracle.  All rights reserved.
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
 *  This code is a port of e2fsprogs/lib/ext2fs/dir_iterate.c
 *  Copyright (C) 1993, 1994, 1994, 1995, 1996, 1997 Theodore Ts'o.
 */
#define _XOPEN_SOURCE 600  /* Triggers XOPEN2K in features.h */
#define _LARGEFILE64_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <unistd.h>
#include <limits.h>
#include <linux/types.h>

#include <inttypes.h>
#include <stdio.h>

#include "main.h"
#include "byteorder.h"
#include "ocfs2_internals.h"

static char *level_str(int level)
{
	char *s;

	switch (level) {
	case LKM_IVMODE:
		s = "Invalid";
		break;
	case LKM_NLMODE:
		s = "No Lock";
		break;
	case LKM_CRMODE:
		s = "Concurrent Read";
		break;
	case LKM_CWMODE:
		s = "Concurrent Write";
		break;
	case LKM_PRMODE:
		s = "Protected Read";
		break;
	case LKM_PWMODE:
		s = "Protected Write";
		break;
	case LKM_EXMODE:
		s = "Exclusive";
		break;
	default:
		s = "Unknown";
	}

	return s;
}

static void print_flags(unsigned long flags, FILE *out)
{
	if (flags & OCFS2_LOCK_INITIALIZED )
		fprintf(out, " Initialized");

	if (flags & OCFS2_LOCK_ATTACHED)
		fprintf(out, " Attached");

	if (flags & OCFS2_LOCK_BUSY)
		fprintf(out, " Busy");

	if (flags & OCFS2_LOCK_BLOCKED)
		fprintf(out, " Blocked");

	if (flags & OCFS2_LOCK_LOCAL)
		fprintf(out, " Local");

	if (flags & OCFS2_LOCK_NEEDS_REFRESH)
		fprintf(out, " Needs Refresh");

	if (flags & OCFS2_LOCK_REFRESHING)
		fprintf(out, " Refreshing");

	if (flags & OCFS2_LOCK_FREEING)
		fprintf(out, " Freeing");

	if (flags & OCFS2_LOCK_QUEUED)
		fprintf(out, " Queued");
}

static char *action_str(unsigned int action)
{
	char *s;

	switch (action) {
	case OCFS2_AST_INVALID:
		s = "None";
		break;
	case OCFS2_AST_ATTACH:
		s = "Attach";
		break;
	case OCFS2_AST_CONVERT:
		s = "Convert";
		break;
	case OCFS2_AST_DOWNCONVERT:
		s = "Downconvert";
		break;
	default:
		s = "Unknown";
	}

	return s;
}

static char *unlock_action_str(unsigned int unlock_action)
{
	char *s;
	switch (unlock_action) {
	case OCFS2_UNLOCK_INVALID:
		s = "None";
		break;
	case OCFS2_UNLOCK_CANCEL_CONVERT:
		s = "Cancel Convert";
		break;
	case OCFS2_UNLOCK_DROP_LOCK:
		s = "Drop Lock";
		break;
	default:
		s = "Unknown";
	}

	return s;
}

static void dump_raw_lvb(const char *lvb, FILE *out)
{
	int i;

	fprintf(out, "Raw LVB:\t");

	for(i = 0; i < DLM_LVB_LEN; i++) {
		fprintf(out, "%02hhx ", lvb[i]);
		if (!((i+1) % 16) && i != (DLM_LVB_LEN-1))
			fprintf(out, "\n\t\t");
	}
	fprintf(out, "\n");
}

static void dump_meta_lvb_v1(struct ocfs2_meta_lvb_v1 *lvb, FILE *out)
{
	fprintf(out, "Decoded LVB:\t");

	fprintf(out, "Version: %u  "
		"Clusters: %u  "
		"Size: %"PRIu64"\n",
		be32_to_cpu(lvb->lvb_version),
		be32_to_cpu(lvb->lvb_iclusters),
		be64_to_cpu(lvb->lvb_isize));
	fprintf(out, "\t\tMode: 0%o  "
		"UID: %u  "
		"GID: %u  "
		"Nlink: %u\n",
		be16_to_cpu(lvb->lvb_imode),
		be32_to_cpu(lvb->lvb_iuid),
		be32_to_cpu(lvb->lvb_igid),
		be16_to_cpu(lvb->lvb_inlink));
	fprintf(out, "\t\tAtime_packed: 0x%"PRIx64"\n"
		"\t\tCtime_packed: 0x%"PRIx64"\n"
		"\t\tMtime_packed: 0x%"PRIx64"\n",
		be64_to_cpu(lvb->lvb_iatime_packed),
		be64_to_cpu(lvb->lvb_ictime_packed),
		be64_to_cpu(lvb->lvb_imtime_packed));
}

static void dump_meta_lvb_v2(struct ocfs2_meta_lvb_v2 *lvb, FILE *out)
{
	fprintf(out, "Decoded LVB:\t");

	fprintf(out, "Version: %u  "
		"Clusters: %u  "
		"Size: %"PRIu64"\n",
		be32_to_cpu(lvb->lvb_version),
		be32_to_cpu(lvb->lvb_iclusters),
		be64_to_cpu(lvb->lvb_isize));
	fprintf(out, "\t\tMode: 0%o  "
		"UID: %u  "
		"GID: %u  "
		"Nlink: %u\n",
		be16_to_cpu(lvb->lvb_imode),
		be32_to_cpu(lvb->lvb_iuid),
		be32_to_cpu(lvb->lvb_igid),
		be16_to_cpu(lvb->lvb_inlink));
	fprintf(out, "\t\tAtime_packed: 0x%"PRIx64"\n"
		"\t\tCtime_packed: 0x%"PRIx64"\n"
		"\t\tMtime_packed: 0x%"PRIx64"\n",
		be64_to_cpu(lvb->lvb_iatime_packed),
		be64_to_cpu(lvb->lvb_ictime_packed),
		be64_to_cpu(lvb->lvb_imtime_packed));
}

static void dump_meta_lvb(const char *raw_lvb, FILE *out)
{
	struct ocfs2_meta_lvb_v1 *lvb1 = (struct ocfs2_meta_lvb_v1 *) raw_lvb;
	struct ocfs2_meta_lvb_v2 *lvb2 = (struct ocfs2_meta_lvb_v2 *) raw_lvb;

	if (!lvb1->lvb_old_seq && be32_to_cpu(lvb1->lvb_version) == 1)
		dump_meta_lvb_v1(lvb1, out);
	else if (be32_to_cpu(lvb2->lvb_version) == 2)
		dump_meta_lvb_v2(lvb2, out);
}

/* 0 = eof, > 0 = success, < 0 = error */
static int dump_version_one(FILE *file, FILE *out, int lvbs)
{
	char id[OCFS2_LOCK_ID_MAX_LEN + 1];	
	char lvb[DLM_LVB_LEN];
	int ret, i, level, requested, blocking;
	unsigned long flags;
	unsigned int action, unlock_action, ro, ex, dummy;
	const char *format;

	ret = fscanf(file, "%s\t"
		     "%d\t"
		     "0x%lx\t"
		     "0x%x\t"
		     "0x%x\t"
		     "%u\t"
		     "%u\t"
		     "%d\t"
		     "%d\t",
		     id,
		     &level,
		     &flags,
		     &action,
		     &unlock_action,
		     &ro,
		     &ex,
		     &requested,
		     &blocking);
	if (ret != 9) {
		ret = -EINVAL;
		goto out;
	}

	format = "0x%x\t";
	for (i = 0; i < DLM_LVB_LEN; i++) {
		/* This is the known last part of the record. If we
		 * include the field delimiting '\t' then fscanf will
		 * also catch the record delimiting '\n' character,
		 * which we want to save for the caller to find. */
		if (i == (DLM_LVB_LEN - 1))
			format = "0x%x";

		ret = fscanf(file, format, &dummy);
		if (ret != 1) {
			ret = -EINVAL;
			goto out;
		}

		lvb[i] = (char) dummy;
	}

	fprintf(out, "Lockres: %s  Mode: %s\nFlags:", id, level_str(level));
	print_flags(flags, out);
	fprintf(out, "\nRO Holders: %u  EX Holders: %u\n", ro, ex);
	fprintf(out, "Pending Action: %s  Pending Unlock Action: %s\n",
		action_str(action), unlock_action_str(unlock_action));
	fprintf(out, "Requested Mode: %s  Blocking Mode: %s\n",
		level_str(requested), level_str(blocking));

	if (lvbs) {
		dump_raw_lvb(lvb, out);
		if (id[0] == 'M')
			dump_meta_lvb(lvb, out);
	}
	fprintf(out, "\n");

	ret = 1;
out:
	return ret;
}

static int end_line(FILE *f)
{
	int ret;

	do {
		ret = fgetc(f);
		if (ret == EOF)
			return 1;
	} while (ret != '\n');

	return 0;
}

#define CURRENT_PROTO 1
/* returns 0 on error or end of file */
static int dump_one_lockres(FILE *file, FILE *out, int lvbs)
{
	unsigned int version;
	int ret;

	ret = fscanf(file, "%x\t", &version);
	if (ret != 1)
		return 0;

	if (version > CURRENT_PROTO) {
		fprintf(stdout, "Debug string proto %u found, but %u is the "
			"highest I understand.\n", version, CURRENT_PROTO);
		return 0;
	}

	ret = dump_version_one(file, out, lvbs);
	if (ret <= 0)
		return 0;

	/* Read to the end of the record here. Any new fields tagged
	 * onto the current format will be silently ignored. */
	ret = !end_line(file);

	return ret;
}

#define DEBUGFS_MAGIC   0x64626720
static errcode_t try_debugfs_path(const char *path)
{
	errcode_t ret;
	struct stat64 stat_buf;
	struct statfs64 statfs_buf;

	ret = stat64(path, &stat_buf);
	if (ret || !S_ISDIR(stat_buf.st_mode))
		return O2CB_ET_SERVICE_UNAVAILABLE;
	ret = statfs64(path, &statfs_buf);
	if (ret || (statfs_buf.f_type != DEBUGFS_MAGIC))
		return O2CB_ET_SERVICE_UNAVAILABLE;

	return 0;
}

#define LOCKING_STATE_FORMAT_PATH "%s/ocfs2/%s/locking_state"
static errcode_t open_locking_state(const char *debugfs_path,
				    const char *uuid_str,
				    FILE **state_file)
{
	errcode_t ret = 0;
	char path[PATH_MAX];

	ret = snprintf(path, PATH_MAX - 1, LOCKING_STATE_FORMAT_PATH,
		       debugfs_path, uuid_str);
	if ((ret <= 0) || (ret == (PATH_MAX - 1)))
		return O2CB_ET_INTERNAL_FAILURE;

	*state_file = fopen(path, "r");
	if (!*state_file) {
		switch (errno) {
			default:
				ret = O2CB_ET_INTERNAL_FAILURE;
				break;

			case ENOTDIR:
			case ENOENT:
			case EISDIR:
				ret = O2CB_ET_SERVICE_UNAVAILABLE;
				break;

			case EACCES:
			case EPERM:
			case EROFS:
				ret = O2CB_ET_PERMISSION_DENIED;
				break;
		}
		goto out;
	}

	ret = 0;
out:
	return ret;
}

#define SYSFS_BASE		"/sys/kernel/"
#define DEBUGFS_PATH		SYSFS_BASE "debug"
#define DEBUGFS_ALTERNATE_PATH	"/debug"

void dump_fs_locks(char *uuid_str, FILE *out, int dump_lvbs)
{
	errcode_t ret;
	int err;
	const char *debugfs_path = DEBUGFS_PATH;
	struct stat64 stat_buf;
	FILE *file;

	err = stat64(SYSFS_BASE, &stat_buf);
	if (err)
		debugfs_path = DEBUGFS_ALTERNATE_PATH;

	ret = try_debugfs_path(debugfs_path);
	if (ret) {
		fprintf(stderr, "Could not locate debugfs file system. "
			"Perhaps it is not mounted?\n");
		return;
	}

	ret = open_locking_state(debugfs_path, uuid_str, &file);
	if (ret) {
		fprintf(stderr, "Could not open debug state for \"%s\".\n"
			"Perhaps that OCFS2 file system is not mounted?\n",
			uuid_str);
		return;
	}

	while (dump_one_lockres(file, out, dump_lvbs))
		;

	fclose(file);
}
