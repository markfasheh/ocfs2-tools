/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
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
 * - to make life simpler, memory allocation is a fatal error.  We should
 *   have reasonable memory demands in relation to the size of the fs.
 * - I'm still of mixed opinions about IO errors.  thoughts?
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

static void print_usage(void)
{
	fprintf(stderr,
		"Usage: fsck.ocfs2 [-s <superblock>] [-B <blksize>]\n"
	       	"               <filename>\n");
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

static errcode_t o2fsck_state_init(ocfs2_filesys *fs, char *whoami, 
				    o2fsck_state *ost)
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

	ret = ocfs2_block_bitmap_new(fs, "directory inodes to rebuild",
				     &ost->ost_rebuild_dirs);
	if (ret) {
		com_err(whoami, ret, "while allocating rebuild dirs bitmap");
		return ret;
	}

	return 0;
}

static errcode_t check_superblock(char *whoami, o2fsck_state *ost)
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

static void exit_if_skipping(o2fsck_state *ost)
{
	if (ost->ost_force)
		return;

	/* XXX do something with s_state, _mnt_count, checkinterval,
	 * etc. */
	return;
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

int main(int argc, char **argv)
{
	char *filename;
	int64_t blkno, blksize;
	o2fsck_state _ost, *ost = &_ost;
	int c, ret, open_flags = OCFS2_FLAG_RW;
	int fsck_mask = FSCK_OK;

	memset(ost, 0, sizeof(o2fsck_state));
	ost->ost_ask = 1;
	ost->ost_dirblocks.db_root = RB_ROOT;
	ost->ost_dir_parents = RB_ROOT;

	/* These mean "autodetect" */
	blksize = 0;
	blkno = 0;

	initialize_ocfs_error_table();
	setlinebuf(stderr);
	setlinebuf(stdout);

	while((c = getopt(argc, argv, "b:B:npuvVy")) != EOF) {
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

			case 'n':
				ost->ost_ask = 0;
				ost->ost_answer = 0;
				open_flags &= ~OCFS2_FLAG_RW;
				open_flags |= OCFS2_FLAG_RO;
				break;

			/* "preen" don't ask and force fixing */
			case 'p':
				ost->ost_ask = 0;
				ost->ost_answer = 1;
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
				printf("$URL$ $Rev$\n");
				exit(FSCK_USAGE);

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

	ret = ocfs2_open(filename, open_flags, blkno, blksize, &ost->ost_fs);
	if (ret) {
		com_err(argv[0], ret,
			"while opening file \"%s\"", filename);
		fsck_mask |= FSCK_ERROR;
		goto out;
	}

	if (o2fsck_state_init(ost->ost_fs, argv[0], ost)) {
		fprintf(stderr, "error allocating run-time state, exiting..\n");
		fsck_mask |= FSCK_ERROR;
		goto out;
	}

	ret = check_superblock(argv[0], ost);
	if (ret) {
		printf("fsck saw unrecoverable errors in the super block and "
		       "will not continue.\n");
		fsck_mask |= FSCK_ERROR;
		goto out;
	}

	exit_if_skipping(ost);

#if 0
	o2fsck_mark_block_used(ost, 0);
	o2fsck_mark_block_used(ost, 1);
	o2fsck_mark_block_used(ost, OCFS2_SUPER_BLOCK_BLKNO);
#endif
	mark_magical_clusters(ost);

	/* XXX we don't use the bad blocks inode, do we? */

	printf("Checking OCFS2 filesystem in %s:\n", filename);
	printf("  label:              ");
	print_label(ost);
	printf("  uuid:               ");
	print_uuid(ost);
	printf("  number of blocks:   %"PRIu64"\n", ost->ost_fs->fs_blocks);
	printf("  bytes per block:    %u\n", ost->ost_fs->fs_blocksize);
	printf("  number of clusters: %"PRIu32"\n", ost->ost_fs->fs_clusters);
	printf("  bytes per cluster:  %u\n", ost->ost_fs->fs_clustersize);
	printf("  max nodes:          %u\n", 
	       OCFS2_RAW_SB(ost->ost_fs->fs_super)->s_max_nodes);

	ret = o2fsck_replay_journals(ost);
	if (ret) {
		printf("fsck encountered unrecoverable errors while replaying "
		       "the journals and will not continue\n");
		fsck_mask |= FSCK_ERROR;
		goto out;
	}

	/* XXX think harder about these error cases. */
	ret = o2fsck_pass0(ost);
	if (ret) {
		printf("fsck encountered unrecoverable errors in pass 0 and "
		       "will not continue\n");
		fsck_mask |= FSCK_ERROR;
		goto out;
	}

	ret = o2fsck_pass1(ost);
	if (ret)
		com_err(argv[0], ret, "pass1 failed");

	ret = o2fsck_pass2(ost);
	if (ret)
		com_err(argv[0], ret, "pass2 failed");

	ret = o2fsck_pass3(ost);
	if (ret)
		com_err(argv[0], ret, "pass3 failed");

	ret = o2fsck_pass4(ost);
	if (ret)
		com_err(argv[0], ret, "pass4 failed");

	ret = ocfs2_close(ost->ost_fs);
	if (ret) {
		com_err(argv[0], ret,
			"while closing file \"%s\"", filename);
	}

	/* XXX check if the fs is modified and yell something. */
	printf("fsck completed successfully.\n");

out:
	return fsck_mask;
}
