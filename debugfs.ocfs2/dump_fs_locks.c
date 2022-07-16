/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dump_fs_locks.c
 *
 * Interface with the kernel and dump current fs locking state
 *
 * Copyright (C) 2005, 2008 Oracle.  All rights reserved.
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301 USA.
 *
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
#include "ocfs2/byteorder.h"
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
static int dump_version_two_or_more(FILE *file, FILE *out, int version)
{
	unsigned long long num_prmode, num_exmode;
	unsigned int num_prmode_failed, num_exmode_failed;
	unsigned long long  total_prmode, total_exmode;
	unsigned long long  avg_prmode = 0, avg_exmode = 0;
	unsigned int max_prmode, max_exmode, num_refresh;
	unsigned long long  last_prmode, last_exmode, wait;
	int ret;

#define NSEC_PER_USEC   1000

	ret = fscanf(file, "%llu\t"
		     "%llu\t"
		     "%u\t"
		     "%u\t"
		     "%llu\t"
		     "%llu\t"
		     "%u\t"
		     "%u\t"
		     "%u\t"
		     "%llu\t"
		     "%llu\t"
		     "%llu",
		     &num_prmode,
		     &num_exmode,
		     &num_prmode_failed,
		     &num_exmode_failed,
		     &total_prmode,
		     &total_exmode,
		     &max_prmode,
		     &max_exmode,
		     &num_refresh,
		     &last_prmode,
		     &last_exmode,
		     &wait);
	if (ret != 12) {
		ret = -EINVAL;
		goto out;
	}

	if (version < 3) {
		max_prmode /= NSEC_PER_USEC;
		max_exmode /= NSEC_PER_USEC;
	}

	if (num_prmode)
		avg_prmode = total_prmode/num_prmode;

	if (num_exmode)
		avg_exmode = total_exmode/num_exmode;

	fprintf(out, "PR > Gets: %llu  Fails: %u    Waits Total: %lluus  "
		"Max: %uus  Avg: %lluns",
		num_prmode, num_prmode_failed, total_prmode/NSEC_PER_USEC,
		max_prmode, avg_prmode);
	if (version > 3) {
		fprintf(out, " Last: %lluus", last_prmode);
	}
	fprintf(out, "\n");
	fprintf(out, "EX > Gets: %llu  Fails: %u    Waits Total: %lluus  "
		"Max: %uus  Avg: %lluns",
		num_exmode, num_exmode_failed, total_exmode/NSEC_PER_USEC,
		max_exmode, avg_exmode);
	if (version > 3) {
		fprintf(out, " Last: %lluus", last_exmode);
	}
	fprintf(out, "\n");
	fprintf(out, "Disk Refreshes: %u", num_refresh);
	if (version > 3) {
		fprintf(out, " First Wait: %lluus", wait);
	}
	fprintf(out, "\n");

	ret = 1;
out:
	return ret;
}

/* 0 = eof, > 0 = success, < 0 = error */
static int dump_version_one(FILE *file, FILE *out, int lvbs, int only_busy,
			    struct list_head *locklist, int *skipped,
			    unsigned int version)
{
	char id[OCFS2_LOCK_ID_MAX_LEN + 1];	
	char lvb[DLM_LVB_LEN];
	int ret, i, level, requested, blocking;
	unsigned long flags;
	unsigned int action, unlock_action, ro, ex, dummy;
	const char *format;

	*skipped = 1;

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
		if ((i == (DLM_LVB_LEN - 1)) && (version < 2))
			format = "0x%x";

		ret = fscanf(file, format, &dummy);
		if (ret != 1) {
			ret = -EINVAL;
			goto out;
		}

		lvb[i] = (char) dummy;
	}

	if (!list_empty(locklist)) {
		if (!del_from_stringlist(id, locklist)) {
			ret = 1;
			goto out;
		}
	}

	if (only_busy) {
		if (!(flags & OCFS2_LOCK_BUSY)) {
			ret = 1;
			goto out;
		}
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

	*skipped = 0;

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

#define CURRENT_PROTO 4
/* returns 0 on error or end of file */
static int dump_one_lockres(FILE *file, FILE *out, int lvbs, int only_busy,
			    struct list_head *locklist)
{
	unsigned int version;
	int ret;
	int skipped = 0;

	ret = fscanf(file, "%x\t", &version);
	if (ret != 1)
		return 0;

	if (version > CURRENT_PROTO) {
		fprintf(stdout, "Debug string proto %u found, but %u is the "
			"highest I understand.\n", version, CURRENT_PROTO);
		return 0;
	}

	ret = dump_version_one(file, out, lvbs, only_busy, locklist, &skipped, version);
	if (ret <= 0)
		return 0;

	if (!skipped) {
		if (version > 1) {
			ret = dump_version_two_or_more(file, out, version);
			if (ret <= 0)
				return 0;
		}

		fprintf(out, "\n");
	}

	/* Read to the end of the record here. Any new fields tagged
	 * onto the current format will be silently ignored. */
	ret = !end_line(file);

	return ret;
}

void dump_fs_locks(char *uuid_str, FILE *out, char *path, int dump_lvbs,
		   int only_busy, struct list_head *locklist)
{
	errcode_t ret;
	char debugfs_path[PATH_MAX];
	FILE *file;
	int show_select;

	if (path == NULL) {
		ret = get_debugfs_path(debugfs_path, sizeof(debugfs_path));
		if (ret) {
			fprintf(stderr, "Could not locate debugfs file system. "
				"Perhaps it is not mounted?\n");
			return;
		}

		ret = open_debugfs_file(debugfs_path, "ocfs2", uuid_str,
					"locking_state", &file);
		if (ret) {
			fprintf(stderr, "Could not open debug state for "
				"\"%s\".\nPerhaps that OCFS2 file system is "
				"not mounted?\n", uuid_str);
			return;
		}
	} else {
		file = fopen(path, "r");
		if (!file) {
			fprintf(stderr, "Could not open file at \"%s\"\n",
				path);
			return;
		}
	}

	show_select = !list_empty(locklist);

	while (dump_one_lockres(file, out, dump_lvbs, only_busy, locklist)) {
		if (show_select && list_empty(locklist))
			break;
	}

	fclose(file);
}
