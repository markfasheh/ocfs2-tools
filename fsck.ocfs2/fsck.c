/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * Copyright (C) 1993-2004 Theodore Ts'o.
 * Copyright (C) 2004 Oracle.  All rights reserved.
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
 * --
 * Roughly o2fsck performs the following operations.  Each pass' file has
 * more details.
 *
 * journal.c: try and replay the journal for each node
 * pass0.c: make sure all the chain allocators are consistent
 * pass1.c: walk allocated inodes and verify them, including their extents
 *          reflect valid inodes in the inode chain allocators
 *          reflect allocated clusters in the cluster chain allocator
 * pass2.c: verify directory entries, record some linkage metadata
 * pass3.c: make sure all dirs are reachable
 * pass4.c: resolve inode's link counts, move disconnected inodes to lost+found
 *
 * When hacking on this keep the following in mind:
 *
 * - fsck -n is a good read-only on-site diagnostic tool.  This means that fsck
 *   _should not_ write to the file system unless it has asked prompt() to do
 *   so.  It should also not exit if prompt() returns 0.  prompt() should give
 *   as much detail as possible as it becomes an error log.
 * - to make life simpler, memory allocation is a fatal error.  It would be
 *   very exciting to have allocation failure trick fsck -y into tearing
 *   apart the fs because it didn't have memorty to track what was in use.  
 *   We should have reasonable memory demands in relation to the size of 
 *   the fs.
 * - I'm still of mixed opinions about IO errors.  For now they're fatal.
 *   One needs to dd a volume off a busted device before fixing it.
 *   thoughts?
 */
#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>

#include "ocfs2.h"

#include "fsck.h"
#include "icount.h"
#include "journal.h"
#include "pass0.h"
#include "pass1.h"
#include "pass2.h"
#include "pass3.h"
#include "pass4.h"
#include "problem.h"
#include "util.h"

int verbose = 0;

static char *whoami = "fsck.ocfs2";

static void print_usage(void)
{
	fprintf(stderr,
		"Usage: fsck.ocfs2 [ -fGnuvVy ] [ -b superblock block ]\n"
		"		    [ -B block size ] device\n"
		"\n"
		"Critical flags for emergency repair:\n" 
		" -n		Check but don't change the file system\n"
		" -y		Answer 'yes' to all repair questions\n"
		" -f		Force checking even if file system is clean\n"
		"\n"
		"Less critical flags:\n"
		" -b superblock	Treat given block as the super block\n"
		" -B blocksize	Force the given block size\n"
		" -G		Ask to fix mismatched inode generations\n"
		" -u		Access the device with buffering\n"
		" -V		Output fsck.ocfs2's version\n"
		" -v		Provide verbose debugging output\n"
		);
}

static uint64_t read_number(const char *num)
{
	uint64_t val;
	char *ptr;

	val = strtoull(num, &ptr, 0);
	if (!ptr || *ptr)
		return 0;

	return val;
}

extern int opterr, optind;
extern char *optarg;

static errcode_t o2fsck_state_init(ocfs2_filesys *fs, o2fsck_state *ost)
{
	errcode_t ret;

	ret = o2fsck_icount_new(fs, &ost->ost_icount_in_inodes);
	if (ret) {
		com_err(whoami, ret, "while allocating inode icount");
		return ret;
	}

	ret = o2fsck_icount_new(fs, &ost->ost_icount_refs);
	if (ret) {
		com_err(whoami, ret, "while allocating reference icount");
		return ret;
	}

	ret = ocfs2_block_bitmap_new(fs, "inodes with bad fields", 
				     &ost->ost_bad_inodes);
	if (ret) {
		com_err(whoami, ret, "while allocating bad inodes bitmap");
		return ret;
	}

	ret = ocfs2_block_bitmap_new(fs, "directory inodes", 
				     &ost->ost_dir_inodes);
	if (ret) {
		com_err(whoami, ret, "while allocating dir inodes bitmap");
		return ret;
	}

	ret = ocfs2_block_bitmap_new(fs, "regular file inodes", 
				     &ost->ost_reg_inodes);
	if (ret) {
		com_err(whoami, ret, "while allocating reg inodes bitmap");
		return ret;
	}

	ret = ocfs2_block_bitmap_new(fs, "allocated clusters",
				     &ost->ost_allocated_clusters);
	if (ret) {
		com_err(whoami, ret, "while allocating a bitmap to track "
			"allocated clusters");
		return ret;
	}

	return 0;
}

static errcode_t check_superblock(o2fsck_state *ost)
{
	ocfs2_dinode *di = ost->ost_fs->fs_super;
	ocfs2_super_block *sb = OCFS2_RAW_SB(di);
	errcode_t ret = 0;

	if (sb->s_max_nodes == 0) {
		printf("The superblock max_nodes field is set to 0.\n");
		ret = OCFS2_ET_CORRUPT_SUPERBLOCK;
	}

	/* ocfs2_open() already checked _incompat and _ro_compat */
	if (sb->s_feature_compat & ~OCFS2_FEATURE_COMPAT_SUPP) {
		if (ret == 0)
			ret = OCFS2_ET_UNSUPP_FEATURE;
		com_err(whoami, ret, "while checking the super block's compat "
			"flags");
	}

	ost->ost_fs_generation = di->i_fs_generation;

	/* XXX do we want checking for different revisions of ocfs2? */

	return ret;
}

static errcode_t write_out_superblock(o2fsck_state *ost)
{
	ocfs2_dinode *di = ost->ost_fs->fs_super;
	ocfs2_super_block *sb = OCFS2_RAW_SB(di);

	sb->s_errors = ost->ost_saw_error;
	sb->s_lastcheck = time(NULL);
	sb->s_mnt_count = 0;

	return ocfs2_write_super(ost->ost_fs);
}

static void scale_time(time_t secs, unsigned *scaled, char **units)
{
	if (secs < 60) {
		*units = "seconds";
		goto done;
	}
	secs /= 60;

	if (secs < 60) {
		*units = "minutes";
		goto done;
	}
	secs /= 60;

	if (secs < 24) {
		*units = "hours";
		goto done;
	}
	secs /= 24;
	*units = "days";

done:
	*scaled = secs;
}

/* avoid "warning: `%c' yields only last 2 digits of year in some locales" */
static size_t ftso_strftime(char *s, size_t max, const char *fmt,
			    const struct tm *tm) {
	return strftime(s, max, fmt, tm);
}

static int fs_is_clean(o2fsck_state *ost, char *filename)
{
	ocfs2_super_block *sb = OCFS2_RAW_SB(ost->ost_fs->fs_super);
	time_t now = time(NULL);
	time_t next = sb->s_lastcheck + sb->s_checkinterval;
	static char reason[4096] = {'\0', };
	struct tm local;

	if (ost->ost_force)
		strcpy(reason, "was run with -f");
	else if (sb->s_state & OCFS2_ERROR_FS)
		strcpy(reason, "contains a file system with errors");
	else if (sb->s_max_mnt_count > 0 &&
		 sb->s_mnt_count > sb->s_max_mnt_count) {
		sprintf(reason, "has been mounted %u times without being "
			"checked", sb->s_mnt_count);
	} else if (sb->s_checkinterval > 0 && now >= next) {
		unsigned scaled_time;
		char *scaled_units;

		scale_time(now - sb->s_lastcheck, &scaled_time, &scaled_units);
		sprintf(reason, "has gone %u %s without being checked",
			scaled_time, scaled_units);
	}

	if (reason[0]) {
		printf("%s %s, check forced.\n", filename, reason);
		return 0;
	}

	reason[0] = '\0';

	if (sb->s_max_mnt_count > 0)
		sprintf(reason, "after %u additional mounts", 
			sb->s_max_mnt_count - sb->s_mnt_count);

	if (sb->s_checkinterval > 0) {
		localtime_r(&next, &local);

		if (reason[0])
			ftso_strftime(reason + strlen(reason),
				 sizeof(reason) - strlen(reason),
			 	 " or by %c, whichever comes first", &local);
		else
			ftso_strftime(reason, sizeof(reason), "by %c", &local);
	}

	printf("%s is clean.", filename);

	if (reason[0])
		printf("  It will be checked %s.\n", reason);

	return 1;
}

static void print_label(o2fsck_state *ost)
{
	char *label = OCFS2_RAW_SB(ost->ost_fs->fs_super)->s_label;
	size_t i, max = sizeof(OCFS2_RAW_SB(ost->ost_fs->fs_super)->s_label);

	for(i = 0; i < max && label[i]; i++) {
		if (isprint(label[i]))
			printf("%c", label[i]);
		else
			printf(".");
	}
	if (i == 0)
		printf("<NONE>");

	printf("\n");
}

static void print_uuid(o2fsck_state *ost)
{
	unsigned char *uuid = OCFS2_RAW_SB(ost->ost_fs->fs_super)->s_uuid;
	size_t i, max = sizeof(OCFS2_RAW_SB(ost->ost_fs->fs_super)->s_uuid);

	for(i = 0; i < max; i++)
		printf("%02x ", uuid[i]);

	printf("\n");
}

static void mark_magical_clusters(o2fsck_state *ost)
{
	uint32_t cluster;

	cluster = ocfs2_blocks_to_clusters(ost->ost_fs, 
					   ost->ost_fs->fs_first_cg_blkno);

	if (cluster != 0) 
		o2fsck_mark_clusters_allocated(ost, 0, cluster);
}

static void version(void)
{
	char url[] = "$URL$";
       	char rev[] = "$Rev$";
	char noise[] = "fsck.ocfs2/fsck.c";
	char *found;

	/* url =~ s/noise// :P */
	found = strstr(url, noise);
	if (found) {
		char *rest = found + strlen(noise);
		memcpy(found, rest, sizeof(url) - (found - url));
	}

	printf("fsck.ocfs2 version information from Subversion:\n"
	       " %s\n"
	       " %s\n", url, rev);

	exit(FSCK_USAGE);
}

static errcode_t open_and_check(o2fsck_state *ost, char *filename,
				int open_flags, uint64_t blkno,
				uint64_t blksize)
{
	errcode_t ret;

	ret = ocfs2_open(filename, open_flags, blkno, blksize, &ost->ost_fs);
	if (ret) {
		com_err(whoami, ret, "while opening \"%s\"", filename);
		goto out;
	}

	ret = check_superblock(ost);
	if (ret) {
		printf("fsck saw unrecoverable errors in the super block and "
		       "will not continue.\n");
		goto out;
	}

out:
	return ret;
}

static errcode_t maybe_replay_journals(o2fsck_state *ost, char *filename,
				       int open_flags, uint64_t blkno,
				       uint64_t blksize)
{	
	int replayed = 0, should = 0;
	errcode_t ret = 0;

	ret = o2fsck_should_replay_journals(ost->ost_fs, &should);
	if (ret)
		goto out;
	if (!should)
		goto out;

	if (!(ost->ost_fs->fs_flags & OCFS2_FLAG_RW)) {
		printf("** Skipping journal replay because -n was "
		       "given.  There may be spurious errors that "
		       "journal replay would fix. **\n");
		goto out;
	}

	printf("%s wasn't cleanly unmounted by all nodes.  Attempting to "
	       "replay the journals for nodes that didn't unmount cleanly\n",
	       filename);

	/* journal replay is careful not to use ost as we only really
	 * build it up after spraying the journal all over the disk
	 * and reopening */
	ret = o2fsck_replay_journals(ost->ost_fs, &replayed);
	if (ret)
		goto out;

	/* if the journals were replayed we close the fs and start
	 * over */
	if (!replayed)
		goto out;

	ret = ocfs2_close(ost->ost_fs);
	if (ret) {
		com_err(whoami, ret, "while closing \"%s\"", filename);
		goto out;
	}

	ret = open_and_check(ost, filename, open_flags, blkno, blksize);
out:
	return ret;
}

int main(int argc, char **argv)
{
	char *filename;
	int64_t blkno, blksize;
	o2fsck_state _ost, *ost = &_ost;
	int c, open_flags = OCFS2_FLAG_RW;
	int fsck_mask = FSCK_OK;
	errcode_t ret;

	memset(ost, 0, sizeof(o2fsck_state));
	ost->ost_ask = 1;
	ost->ost_dirblocks.db_root = RB_ROOT;
	ost->ost_dir_parents = RB_ROOT;

	/* These mean "autodetect" */
	blksize = 0;
	blkno = 0;

	initialize_ocfs_error_table();
	initialize_o2dl_error_table();
	setlinebuf(stderr);
	setlinebuf(stdout);

	while((c = getopt(argc, argv, "b:B:fGnuvVy")) != EOF) {
		switch (c) {
			case 'b':
				blkno = read_number(optarg);
				if (blkno < OCFS2_SUPER_BLOCK_BLKNO) {
					fprintf(stderr,
						"Invalid blkno: %s\n",
						optarg);
					fsck_mask |= FSCK_USAGE;
					print_usage();
					goto out;
				}
				break;

			case 'B':
				blksize = read_number(optarg);
				if (blksize < OCFS2_MIN_BLOCKSIZE) {
					fprintf(stderr, 
						"Invalid blksize: %s\n",
						optarg);
					fsck_mask |= FSCK_USAGE;
					print_usage();
					goto out;
				}
				break;

			case 'f':
				ost->ost_force = 1;
				break;

			case 'G':
				ost->ost_fix_fs_gen = 1;
				break;

			case 'n':
				ost->ost_ask = 0;
				ost->ost_answer = 0;
				open_flags &= ~OCFS2_FLAG_RW;
				open_flags |= OCFS2_FLAG_RO;
				break;

			case 'y':
				ost->ost_ask = 0;
				ost->ost_answer = 1;
				break;

			case 'u':
				open_flags |= OCFS2_FLAG_BUFFERED;
				break;

			case 'v':
				verbose = 1;
				break;

			case 'V':
				version();
				break;

			default:
				fsck_mask |= FSCK_USAGE;
				print_usage();
				goto out;
				break;
		}
	}


	if (blksize % OCFS2_MIN_BLOCKSIZE) {
		fprintf(stderr, "Invalid blocksize: %"PRId64"\n", blksize);
		fsck_mask |= FSCK_USAGE;
		print_usage();
		goto out;
	}

	if (optind >= argc) {
		fprintf(stderr, "Missing filename\n");
		fsck_mask |= FSCK_USAGE;
		print_usage();
		goto out;
	}

	filename = argv[optind];

	ret = open_and_check(ost, filename, open_flags, blkno, blksize);
	if (ret) {
		fsck_mask |= FSCK_ERROR;
		goto out;
	}

	if (open_flags & OCFS2_FLAG_RW) {
		ret = ocfs2_initialize_dlm(ost->ost_fs);
		if (ret) {
			com_err(whoami, ret, " ");
			goto close;
		}

		ret = ocfs2_lock_down_cluster(ost->ost_fs);
		if (ret) {
			com_err(whoami, ret, " ");
			goto close;
		}
	}

	printf("Checking OCFS2 filesystem in %s:\n", filename);
	printf("  label:              ");
	print_label(ost);
	printf("  uuid:               ");
	print_uuid(ost);
	printf("  number of blocks:   %"PRIu64"\n", ost->ost_fs->fs_blocks);
	printf("  bytes per block:    %u\n", ost->ost_fs->fs_blocksize);
	printf("  number of clusters: %"PRIu32"\n", ost->ost_fs->fs_clusters);
	printf("  bytes per cluster:  %u\n", ost->ost_fs->fs_clustersize);
	printf("  max nodes:          %u\n\n", 
	       OCFS2_RAW_SB(ost->ost_fs->fs_super)->s_max_nodes);

	ret = maybe_replay_journals(ost, filename, open_flags, blkno, blksize);
	if (ret) {
		printf("fsck encountered unrecoverable errors while "
		       "replaying the journals and will not continue\n");
		fsck_mask |= FSCK_ERROR;
		goto unlock;
	}

	/* allocate all this junk after we've replayed the journal and the
	 * sb should be stable */
	if (o2fsck_state_init(ost->ost_fs, ost)) {
		fprintf(stderr, "error allocating run-time state, exiting..\n");
		fsck_mask |= FSCK_ERROR;
		goto unlock;
	}

	if (fs_is_clean(ost, filename)) {
		fsck_mask = FSCK_OK;
		goto unlock;
	}

#if 0
	o2fsck_mark_block_used(ost, 0);
	o2fsck_mark_block_used(ost, 1);
	o2fsck_mark_block_used(ost, OCFS2_SUPER_BLOCK_BLKNO);
#endif
	mark_magical_clusters(ost);

	/* XXX we don't use the bad blocks inode, do we? */


	/* XXX for now it is assumed that errors returned from a pass
	 * are fatal.  these can be fixed over time. */
	ret = o2fsck_pass0(ost);
	if (ret) {
		com_err(whoami, ret, "while performing pass 0");
		goto done;
	}

	ret = o2fsck_pass1(ost);
	if (ret) {
		com_err(whoami, ret, "while performing pass 1");
		goto done;
	}

	ret = o2fsck_pass2(ost);
	if (ret) {
		com_err(whoami, ret, "while performing pass 2");
		goto done;
	}

	ret = o2fsck_pass3(ost);
	if (ret) {
		com_err(whoami, ret, "while performing pass 3");
		goto done;
	}

	ret = o2fsck_pass4(ost);
	if (ret) {
		com_err(whoami, ret, "while performing pass 4");
		goto done;
	}

done:
	if (ret)
		fsck_mask |= FSCK_ERROR;
	else {
		ost->ost_saw_error = 0;
		printf("All passes succeeded.\n");
	}

	if (ost->ost_fs->fs_flags & OCFS2_FLAG_RW) {
		ret = write_out_superblock(ost);
		if (ret)
			com_err(whoami, ret, "while writing back the "
				"superblock");
	}

unlock:
	if (ost->ost_fs->fs_dlm_ctxt)
		ocfs2_release_cluster(ost->ost_fs);

close:
	if (ost->ost_fs->fs_dlm_ctxt)
		ocfs2_shutdown_dlm(ost->ost_fs);

	ret = ocfs2_close(ost->ost_fs);
	if (ret) {
		com_err(whoami, ret, "while closing file \"%s\"", filename);
		/* XXX I wonder about this error.. */
		fsck_mask |= FSCK_ERROR;
	} 

out:
	return fsck_mask;
}
