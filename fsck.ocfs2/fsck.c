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
 * pass5.c: load global quota file, merge node-local quota files to global
 *          quota file, recompute quota usage and recreate quota files
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
#include <signal.h>
#include <libgen.h>

#include "ocfs2/ocfs2.h"

#include "fsck.h"
#include "icount.h"
#include "journal.h"
#include "pass0.h"
#include "pass1.h"
#include "pass2.h"
#include "pass3.h"
#include "pass4.h"
#include "pass5.h"
#include "problem.h"
#include "util.h"
#include "slot_recovery.h"

int verbose = 0;

static char *whoami = "fsck.ocfs2";
static o2fsck_state _ost;
static int cluster_locked = 0;

static void mark_magical_clusters(o2fsck_state *ost);

static void handle_signal(int sig)
{
	switch (sig) {
	case SIGTERM:
	case SIGINT:
		printf("\nProcess Interrupted.\n");

		if (cluster_locked && _ost.ost_fs->fs_dlm_ctxt) {
			ocfs2_release_cluster(_ost.ost_fs);
			cluster_locked = 0;
		}

		if (_ost.ost_fs->fs_dlm_ctxt)
			ocfs2_shutdown_dlm(_ost.ost_fs, whoami);

		if (_ost.ost_fs)
			ocfs2_close(_ost.ost_fs);

		exit(1);
	}

	return ;
}

/* Call this with SIG_BLOCK to block and SIG_UNBLOCK to unblock */
static void block_signals(int how)
{
     sigset_t sigs;

     sigfillset(&sigs);
     sigdelset(&sigs, SIGTRAP);
     sigdelset(&sigs, SIGSEGV);
     sigprocmask(how, &sigs, NULL);
}

static void print_usage(void)
{
	fprintf(stderr,
		"Usage: fsck.ocfs2 {-y|-n|-p} [ -fGnuvVy ] [ -b superblock block ]\n"
		"		    [ -B block size ] [-r num] device\n"
		"\n"
		"Critical flags for emergency repair:\n" 
		" -n		Check but don't change the file system\n"
		" -y		Answer 'yes' to all repair questions\n"
		" -p		Automatic repair (no questions, only safe repairs)\n"
		" -f		Force checking even if file system is clean\n"
		" -F		Ignore cluster locking (dangerous!)\n"
		" -r		restore backup superblock(dangerous!)\n"
		"\n"
		"Less critical flags:\n"
		" -b superblock	Treat given block as the super block\n"
		" -B blocksize	Force the given block size\n"
		" -G		Ask to fix mismatched inode generations\n"
		" -P		Show progress\n"
		" -t		Show I/O statistics\n"
		" -tt		Show I/O statistics per pass\n"
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

	ret = ocfs2_cluster_bitmap_new(fs, "allocated clusters",
				       &ost->ost_allocated_clusters);
	if (ret) {
		com_err(whoami, ret, "while allocating a bitmap to track "
			"allocated clusters");
		return ret;
	}

	return 0;
}

errcode_t o2fsck_state_reinit(ocfs2_filesys *fs, o2fsck_state *ost)
{
	errcode_t ret;

	ocfs2_bitmap_free(&ost->ost_dir_inodes);
	ost->ost_dir_inodes = NULL;

	ocfs2_bitmap_free(&ost->ost_reg_inodes);
	ost->ost_reg_inodes = NULL;

	ocfs2_bitmap_free(&ost->ost_allocated_clusters);
	ost->ost_allocated_clusters = NULL;

	if (ost->ost_duplicate_clusters) {
		ocfs2_bitmap_free(&ost->ost_duplicate_clusters);
		ost->ost_duplicate_clusters = NULL;
	}

	o2fsck_icount_free(ost->ost_icount_in_inodes);
	ost->ost_icount_in_inodes = NULL;

	o2fsck_icount_free(ost->ost_icount_refs);
	ost->ost_icount_refs = NULL;

	ret = o2fsck_state_init(fs, ost);
	if (ret) {
		com_err(whoami, ret, "while intializing o2fsck_state.");
		return ret;
	}

	mark_magical_clusters(ost);
	return 0;
}

static errcode_t check_superblock(o2fsck_state *ost)
{
	struct ocfs2_dinode *di = ost->ost_fs->fs_super;
	struct ocfs2_super_block *sb = OCFS2_RAW_SB(di);
	errcode_t ret = 0;

	if (sb->s_max_slots == 0) {
		printf("The superblock max_slots field is set to 0.\n");
		ret = OCFS2_ET_CORRUPT_SUPERBLOCK;
	}

	ost->ost_fs_generation = di->i_fs_generation;

	/* XXX do we want checking for different revisions of ocfs2? */

	return ret;
}

static errcode_t write_out_superblock(o2fsck_state *ost)
{
	struct ocfs2_dinode *di = ost->ost_fs->fs_super;
	struct ocfs2_super_block *sb = OCFS2_RAW_SB(di);

	if (sb->s_feature_incompat & OCFS2_FEATURE_INCOMPAT_RESIZE_INPROG)
		sb->s_feature_incompat &= ~OCFS2_FEATURE_INCOMPAT_RESIZE_INPROG;

	if (sb->s_feature_incompat & OCFS2_FEATURE_INCOMPAT_TUNEFS_INPROG) {
		sb->s_feature_incompat &=
				 ~OCFS2_FEATURE_INCOMPAT_TUNEFS_INPROG;
		sb->s_tunefs_flag = 0;
	}

	if (ost->ost_num_clusters)
		di->i_clusters = ost->ost_num_clusters;

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
	struct ocfs2_super_block *sb = OCFS2_RAW_SB(ost->ost_fs->fs_super);
	time_t now = time(NULL);
	time_t next = sb->s_lastcheck + sb->s_checkinterval;
	static char reason[4096] = {'\0', };
	struct tm local;

	if (ost->ost_force)
		strcpy(reason, "was run with -f");
	else if ((OCFS2_RAW_SB(ost->ost_fs->fs_super)->s_feature_incompat &
		  OCFS2_FEATURE_INCOMPAT_RESIZE_INPROG))
		strcpy(reason, "incomplete volume resize detected");
	else if ((OCFS2_RAW_SB(ost->ost_fs->fs_super)->s_feature_incompat &
		  OCFS2_FEATURE_INCOMPAT_TUNEFS_INPROG))
		strcpy(reason, "incomplete tunefs operation detected");
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
	unsigned char *label = OCFS2_RAW_SB(ost->ost_fs->fs_super)->s_label;
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
		printf("%02X", uuid[i]);

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

static void print_version(void)
{
	fprintf(stderr, "%s %s\n", whoami, VERSION);
}

#define P_(singular, plural, n) ((n) == 1 ? (singular) : (plural))

static void show_stats(o2fsck_state *ost)
{
	uint32_t num_links;

	if (!ost->ost_show_stats)
		return;

	num_links = ost->ost_links_count - ost->ost_dir_count;

	printf("\n  # of inodes with depth 0/1/2/3/4/5: %u/%u/%u/%u/%u/%u\n",
	       ost->ost_tree_depth_count[0], ost->ost_tree_depth_count[1],
	       ost->ost_tree_depth_count[2], ost->ost_tree_depth_count[3],
	       ost->ost_tree_depth_count[4], ost->ost_tree_depth_count[5]);
	printf("  # of orphaned inodes found/deleted: %u/%u\n",
	       ost->ost_orphan_count, ost->ost_orphan_deleted_count);

	printf(P_("\n%12u regular file", "\n%12u regular files",
		  ost->ost_file_count), ost->ost_file_count);
	printf(P_(" (%u inline,", " (%u inlines,",
		  ost->ost_inline_file_count), ost->ost_inline_file_count);
	printf(P_(" %u reflink)\n", " %u reflinks)\n",
		  ost->ost_reflinks_count), ost->ost_reflinks_count);
	printf(P_("%12u directory", "%12u directories",
		  ost->ost_dir_count), ost->ost_dir_count);
	printf(P_(" (%u inline)\n", " (%u inlines)\n",
		  ost->ost_inline_dir_count), ost->ost_inline_dir_count);
	printf(P_("%12u character device file\n",
		  "%12u character device files\n", ost->ost_chardev_count),
	       ost->ost_chardev_count);
	printf(P_("%12u block device file\n", "%12u block device files\n",
		  ost->ost_blockdev_count), ost->ost_blockdev_count);
	printf(P_("%12u fifo\n", "%12u fifos\n", ost->ost_fifo_count),
	       ost->ost_fifo_count);
	printf(P_("%12u link\n", "%12u links\n", num_links), num_links);
	printf(P_("%12u symbolic link", "%12u symbolic links",
		  ost->ost_symlinks_count), ost->ost_symlinks_count);
	printf(P_(" (%u fast symbolic link)\n", " (%u fast symbolic links)\n",
		  ost->ost_fast_symlinks_count), ost->ost_fast_symlinks_count);
	printf(P_("%12u socket\n", "%12u sockets\n", ost->ost_sockets_count),
	       ost->ost_sockets_count);
	printf("\n");
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
	int replayed = 0, should = 0, has_dirty = 0;
	errcode_t ret = 0;

	ret = o2fsck_should_replay_journals(ost->ost_fs, &should, &has_dirty);
	if (ret)
		goto out;

	ost->ost_has_journal_dirty = has_dirty ? 1 : 0;

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

/*
 * Do the slot recovery, replay truncate log, local aloc and orphan dir.
 * If there is any error, a force check is enabled.
 */
static errcode_t o2fsck_slot_recovery(o2fsck_state *ost)
{
	errcode_t ret = 0;

	if (!(ost->ost_fs->fs_flags & OCFS2_FLAG_RW)) {
		printf("** Skipping slot recovery because -n was "
		       "given. **\n");
		goto out;
	}

	ret = o2fsck_replay_local_allocs(ost->ost_fs);
	if (ret)
		goto out;

	ret = o2fsck_replay_truncate_logs(ost->ost_fs);
	if (ret)
		goto out;

	/*
	 * If the user want a force-check, orphan_dir will be
	 * replayed after the full check.
	 */
	if (!ost->ost_force) {
		ret = o2fsck_replay_orphan_dirs(ost);
		if (ret)
			com_err(whoami, ret, "while trying to replay the orphan"
				" directory");
	}

out:
	return ret;
}

static errcode_t recover_backup_super(o2fsck_state *ost,
				      char* device, int sb_num)
{
	errcode_t ret;
	uint64_t offsets[OCFS2_MAX_BACKUP_SUPERBLOCKS], blksize, sb;
	ocfs2_filesys *fs = NULL;

	if (sb_num < 1 || sb_num > OCFS2_MAX_BACKUP_SUPERBLOCKS)
		return -1;

	ocfs2_get_backup_super_offsets(NULL, offsets, ARRAY_SIZE(offsets));

	/* iterate all the blocksize to get the right one. */
	for (blksize = OCFS2_MIN_BLOCKSIZE;
		blksize <= OCFS2_MAX_BLOCKSIZE;	blksize <<= 1) {
		sb = offsets[sb_num - 1] / blksize;
		/* Here we just give the possible value of block num and
		 * block size to ocfs2_open and this function will check
		 * them and return '0' if they meet the right one.
		 */
		ret = ocfs2_open(device, OCFS2_FLAG_RW, sb, blksize, &fs);
		if (!ret)
			break;
	}

	if (ret)
		goto bail;

	/* recover the backup information to superblock. */
	if (prompt(ost, PN, PR_RECOVER_BACKUP_SUPERBLOCK,
	    	   "Recover superblock information from backup block"
		   "#%"PRIu64"?", sb)) {
		fs->fs_super->i_blkno = OCFS2_SUPER_BLOCK_BLKNO;
		ret = ocfs2_write_primary_super(fs);
		if (ret)
			goto bail;
	}

	/* no matter whether the user recover the superblock or not above,
	 * we should return 0 in case the superblock can be opened
	 * without the recovery.
	 */
	ret = 0;

bail:
	if (fs)
		ocfs2_close(fs);
	return ret;
}

static errcode_t recover_cluster_info(o2fsck_state *ost)
{
	errcode_t ret;
	struct o2cb_cluster_desc disk = {NULL, NULL}, running = {NULL, NULL};

	ret = o2cb_running_cluster_desc(&running);
	if (ret)
		goto bail;

	ret = ocfs2_fill_cluster_desc(ost->ost_fs, &disk);
	if (ret)
		goto bail;

	/*
	 * If the disk matches the running cluster, there is nothing we
	 * can fix.
	 */
	if ((!running.c_stack && !disk.c_stack) ||
	    (running.c_stack && running.c_cluster &&
	     disk.c_stack && disk.c_cluster &&
	     !strcmp(running.c_stack, disk.c_stack) &&
	     !strcmp(running.c_cluster, disk.c_cluster)))
		goto bail;

#define RECOVER_CLUSTER_WARNING						\
	"The running cluster is using the %s stack\n"			\
	"%s%s, but the filesystem is configured for\n"			\
	"the %s stack%s%s. Thus, %s cannot\n"				\
	"determine whether the filesystem is in use or not. This utility can\n"\
	"reconfigure the filesystem to use the currently running cluster configuration.\n"\
	"DANGER: YOU MUST BE ABSOLUTELY SURE THAT NO OTHER NODE IS USING THIS\n"\
	"FILESYSTEM BEFORE MODIFYING ITS CLUSTER CONFIGURATION.\n"	\
	"Recover cluster configuration information the running cluster?"

	/* recover the backup information to superblock. */
	if (prompt(ost, PN, PR_RECOVER_CLUSTER_INFO, RECOVER_CLUSTER_WARNING,
		   running.c_stack ? running.c_stack : "classic o2cb",
		   running.c_stack ? "with the cluster name " : "",
		   running.c_stack ? running.c_cluster : "",
		   disk.c_stack ? disk.c_stack : "classic o2cb",
		   disk.c_stack ? " with the cluster name " : "",
		   disk.c_stack ? disk.c_cluster : "", whoami)) {
		ret = ocfs2_set_cluster_desc(ost->ost_fs, &running);
		if (ret)
			goto bail;
	}

	/* no matter whether the user recover the superblock or not above,
	 * we should return 0 in case the superblock can be opened
	 * without the recovery.
	 */
	ret = 0;

bail:
	o2cb_free_cluster_desc(&running);
	o2cb_free_cluster_desc(&disk);

	return ret;
}

int main(int argc, char **argv)
{
	char *filename;
	int64_t blkno, blksize;
	o2fsck_state *ost = &_ost;
	int c, open_flags = OCFS2_FLAG_RW | OCFS2_FLAG_STRICT_COMPAT_CHECK;
	int sb_num = 0;
	int fsck_mask = FSCK_OK;
	int slot_recover_err = 0;
	errcode_t ret;
	int mount_flags;
	int proceed = 1;

	memset(ost, 0, sizeof(o2fsck_state));
	ost->ost_ask = 1;
	ost->ost_dirblocks.db_root = RB_ROOT;
	ost->ost_dir_parents = RB_ROOT;
	ost->ost_refcount_trees = RB_ROOT;

	/* These mean "autodetect" */
	blksize = 0;
	blkno = 0;

	initialize_ocfs_error_table();
	initialize_o2dl_error_table();
	initialize_o2cb_error_table();
	setlinebuf(stderr);
	setlinebuf(stdout);

	tools_progress_disable();

	while ((c = getopt(argc, argv, "b:B:DfFGnupavVytPr:")) != EOF) {
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
			case 'D':
				ost->ost_compress_dirs = 1;
				break;

			case 'F':
				ost->ost_skip_o2cb = 1;
				break;

			case 'f':
				ost->ost_force = 1;
				break;

			case 'G':
				ost->ost_fix_fs_gen = 1;
				break;

			case 'n':
				open_flags &= ~OCFS2_FLAG_RW;
				open_flags |= OCFS2_FLAG_RO;
				/* Fall through */

			case 'a':
			case 'p':
				/*
				 * Like extN, -a maps to -p, which is
				 * 'preen'.  This means only fix things
				 * that don't require human interaction.
				 * Unlike extN, this is only journal
				 * replay for now.  To make it smarter,
				 * ost->ost_answer needs to learn a
				 * new mode.
				 */
				ost->ost_ask = 0;
				ost->ost_answer = 0;
				break;

			case 'P':
				tools_progress_enable();
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
				print_version();
				exit(FSCK_USAGE);
				break;

			case 'r':
				sb_num = read_number(optarg);
				break;

			case 't':
				if (ost->ost_show_stats)
					ost->ost_show_extended_stats = 1;
				ost->ost_show_stats = 1;
				break;

			default:
				fsck_mask |= FSCK_USAGE;
				print_usage();
				goto out;
				break;
		}
	}

	if (!(open_flags & OCFS2_FLAG_RW) && ost->ost_compress_dirs) {
		fprintf(stderr, "Compress directories (-D) incompatible with read-only mode\n");
		fsck_mask |= FSCK_USAGE;
		print_usage();
		goto out;
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

	print_version();

	ret = ocfs2_check_if_mounted(filename, &mount_flags);
	if (ret) {
		com_err(whoami, ret, "while determining whether %s is mounted.",
			filename);
		fsck_mask |= FSCK_ERROR;
		goto out;
	}

	if (mount_flags & (OCFS2_MF_MOUNTED | OCFS2_MF_BUSY)) {
		if (!(open_flags & OCFS2_FLAG_RW))
			fprintf(stdout, "\nWARNING!!! Running fsck.ocfs2 (read-"
				"only) on a mounted filesystem may detect "
				"invalid errors.\n\n");
		else {
			fprintf(stdout, "\nRunning fsck.ocfs2 on a "
				"mounted filesystem may cause SEVERE "
				"filesystem damage, abort.\n\n");
			fsck_mask |= FSCK_CANCELED;
			goto out;
		}
		proceed = 0;
	}

	if (proceed && ost->ost_skip_o2cb) {
		fprintf(stdout, "\nWARNING!!! You have disabled the cluster check. "
			"Continue only if you\nare absolutely sure that NO "
			"node has this filesystem mounted or is\notherwise "
			"accessing it. If unsure, do NOT continue.\n\n");
		proceed = 0;
	}

	if (!proceed) {
		fprintf(stdout, "Do you really want to continue (y/N): ");
		if (toupper(getchar()) != 'Y') {
			printf("Aborting operation.\n");
			fsck_mask |= FSCK_CANCELED;
			goto out;
		}
	}

	if (signal(SIGTERM, handle_signal) == SIG_ERR) {
		com_err(whoami, 0, "Could not set SIGTERM");
		exit(1);
	}

	if (signal(SIGINT, handle_signal) == SIG_ERR) {
		com_err(whoami, 0, "Could not set SIGINT");
		exit(1);
	}

	/* recover superblock should be called at first. */
	if (sb_num) {
		ret = recover_backup_super(ost, filename, sb_num);
		if (ret) {
			com_err(whoami, ret, "recover superblock failed.\n");
			fsck_mask |= FSCK_ERROR;
			goto out;
		}

	}

	ret = open_and_check(ost, filename, open_flags, blkno, blksize);
	if (ret) {
		fsck_mask |= FSCK_ERROR;
		goto out;
	}

	if (open_flags & OCFS2_FLAG_RW && !ost->ost_skip_o2cb &&
	    !ocfs2_mount_local(ost->ost_fs)) {
		ret = o2cb_init();
		if (ret) {
			com_err(whoami, ret, "while initializing the cluster");
			goto close;
		}

		block_signals(SIG_BLOCK);
		ret = ocfs2_initialize_dlm(ost->ost_fs, whoami);
		if (ret == O2CB_ET_INVALID_STACK_NAME ||
		    ret == O2CB_ET_INVALID_CLUSTER_NAME ||
		    ret == O2CB_ET_INVALID_HEARTBEAT_MODE) {
			block_signals(SIG_UNBLOCK);
			ret = recover_cluster_info(ost);
			if (ret) {
				com_err(whoami, ret,
					"while recovering cluster information");
				goto close;
			}
			block_signals(SIG_BLOCK);
			ret = ocfs2_initialize_dlm(ost->ost_fs, whoami);
		}
		if (ret) {
			block_signals(SIG_UNBLOCK);
			com_err(whoami, ret, "while initializing the DLM");
			goto close;
		}

		ret = ocfs2_lock_down_cluster(ost->ost_fs);
		if (ret) {
			block_signals(SIG_UNBLOCK);
			com_err(whoami, ret, "while locking down the cluster");
			goto close;
		}
		cluster_locked = 1;
		block_signals(SIG_UNBLOCK);
	}

	printf("Checking OCFS2 filesystem in %s:\n", filename);
	printf("  Label:              ");
	print_label(ost);
	printf("  UUID:               ");
	print_uuid(ost);
	printf("  Number of blocks:   %"PRIu64"\n", ost->ost_fs->fs_blocks);
	printf("  Block size:         %u\n", ost->ost_fs->fs_blocksize);
	printf("  Number of clusters: %"PRIu32"\n", ost->ost_fs->fs_clusters);
	printf("  Cluster size:       %u\n", ost->ost_fs->fs_clustersize);
	printf("  Number of slots:    %u\n\n", 
	       OCFS2_RAW_SB(ost->ost_fs->fs_super)->s_max_slots);

	/* Let's get enough of a cache to replay the journals */
	o2fsck_init_cache(ost, O2FSCK_CACHE_MODE_JOURNAL);

	if (open_flags & OCFS2_FLAG_RW) {
		ret = o2fsck_check_journals(ost);
		if (ret) {
			printf("fsck saw unrecoverable errors in the journal "
				"files and will not continue.\n");
			goto unlock;
		}
	}

	ret = maybe_replay_journals(ost, filename, open_flags, blkno, blksize);
	if (ret) {
		printf("fsck encountered unrecoverable errors while "
		       "replaying the journals and will not continue\n");
		fsck_mask |= FSCK_ERROR;
		goto unlock;
	}

	/* Grow the cache */
	o2fsck_init_cache(ost, O2FSCK_CACHE_MODE_FULL);

	/* allocate all this junk after we've replayed the journal and the
	 * sb should be stable */
	if (o2fsck_state_init(ost->ost_fs, ost)) {
		fprintf(stderr, "error allocating run-time state, exiting..\n");
		fsck_mask |= FSCK_ERROR;
		goto unlock;
	}

	ret = o2fsck_slot_recovery(ost);
	if (ret) {
		printf("fsck encountered errors while recovering slot "
		       "information, check forced.\n");
		slot_recover_err = 1;
		ost->ost_force = 1;
	}

	if (fs_is_clean(ost, filename)) {
		fsck_mask = FSCK_OK;
		goto clear_dirty_flag;
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

	ret = o2fsck_pass5(ost);
	if (ret) {
		com_err(whoami, ret, "while performing pass 5");
		goto done;
	}

done:
	if (ret)
		fsck_mask |= FSCK_ERROR;
	else {
		fsck_mask = FSCK_OK;
		ost->ost_saw_error = 0;
		printf("All passes succeeded.\n\n");
		o2fsck_print_resource_track(NULL, ost, &ost->ost_rt,
					    ost->ost_fs->fs_io);
		show_stats(ost);
	}

clear_dirty_flag:
	if (ost->ost_fs->fs_flags & OCFS2_FLAG_RW) {
		ret = write_out_superblock(ost);
		if (ret)
			com_err(whoami, ret, "while writing back the "
				"superblock(s)");
		if (fsck_mask == FSCK_OK) {
			if (slot_recover_err) {
				ret = o2fsck_slot_recovery(ost);
				if (ret) {
					com_err(whoami, ret, "while doing slot "
						"recovery.");
					goto unlock;
				}
			}

			ret = o2fsck_clear_journal_flags(ost);
			if (ret) {
				com_err(whoami, ret, "while clear dirty "
					"journal flag.");
				goto unlock;
			}

			ret = ocfs2_format_slot_map(ost->ost_fs);
			if (ret)
				com_err(whoami, ret, "while format slot "
					"map.");
		}
	}

unlock:
	block_signals(SIG_BLOCK);
	if (ost->ost_fs->fs_dlm_ctxt)
		ocfs2_release_cluster(ost->ost_fs);
	cluster_locked = 0;
	block_signals(SIG_UNBLOCK);

close:
	block_signals(SIG_BLOCK);
	if (ost->ost_fs->fs_dlm_ctxt)
		ocfs2_shutdown_dlm(ost->ost_fs, whoami);
	block_signals(SIG_UNBLOCK);

	ret = ocfs2_close(ost->ost_fs);
	if (ret) {
		com_err(whoami, ret, "while closing file \"%s\"", filename);
		/* XXX I wonder about this error.. */
		fsck_mask |= FSCK_ERROR;
	} 

out:
	return fsck_mask;
}
