/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * fsck.c
 *
 * file system checker for OCFS2
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
 * Authors: Zach Brown
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
#include "pass1.h"
#include "pass2.h"
#include "pass3.h"
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

	ret = ocfs2_block_bitmap_new(fs, "inodes in use", 
				     &ost->ost_used_inodes);
	if (ret) {
		com_err(whoami, ret, "while allocating used inodes bitmap");
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

	ret = ocfs2_block_bitmap_new(fs, "blocks off inodes",
				     &ost->ost_found_blocks);
	if (ret) {
		com_err(whoami, ret, "while allocating found blocks bitmap");
		return ret;
	}

	ret = ocfs2_block_bitmap_new(fs, "duplicate blocks",
				     &ost->ost_dup_blocks);
	if (ret) {
		com_err(whoami, ret, "while allocating duplicate block bitmap");
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

static void exit_if_skipping(o2fsck_state *ost)
{
	if (ost->ost_force)
		return;

	/* XXX do something with s_state, _mnt_count, checkinterval,
	 * etc. */
	return;
}

int main(int argc, char **argv)
{
	char *filename;
	int64_t blkno, blksize;
	o2fsck_state _ost, *ost = &_ost;
	int c, ret, rw = OCFS2_FLAG_RW;

	memset(ost, 0, sizeof(o2fsck_state));
	ost->ost_ask = 1;
	ost->ost_dirblocks.db_root = RB_ROOT;
	ost->ost_dir_parents = RB_ROOT;

	/* These mean "autodetect" */
	blksize = 0;
	blkno = 0;

	initialize_ocfs_error_table();

	while((c = getopt(argc, argv, "b:B:npv")) != EOF) {
		switch (c) {
			case 'b':
				blkno = read_number(optarg);
				if (blkno < OCFS2_SUPER_BLOCK_BLKNO) {
					fprintf(stderr,
						"Invalid blkno: %s\n",
						optarg);
					print_usage();
					return 1;
				}
				break;

			case 'B':
				blksize = read_number(optarg);
				if (blksize < OCFS2_MIN_BLOCKSIZE) {
					fprintf(stderr, 
						"Invalid blksize: %s\n",
						optarg);
					print_usage();
					return 1;
				}
				break;

			case 'f':
				ost->ost_force = 1;
				break;

			case 'n':
				ost->ost_ask = 0;
				ost->ost_answer = 0;
				rw = OCFS2_FLAG_RO;
				break;

			/* "preen" don't ask and force fixing */
			case 'p':
				ost->ost_ask = 0;
				ost->ost_answer = 1;
				break;

			case 'v':
				verbose = 1;
				break;

			default:
				print_usage();
				return 1;
				break;
		}
	}


	if (blksize % OCFS2_MIN_BLOCKSIZE) {
		fprintf(stderr, "Invalid blocksize: %"PRId64"\n", blksize);
		print_usage();
		return 1;
	}

	if (optind >= argc) {
		fprintf(stderr, "Missing filename\n");
		print_usage();
		return 1;
	}

	filename = argv[optind];

	/* XXX we'll decide on a policy for using o_direct in the future.
	 * for now we want to test against loopback files in ext3, say. */
	ret = ocfs2_open(filename, rw | OCFS2_FLAG_BUFFERED, blkno,
			 blksize, &ost->ost_fs);
	if (ret) {
		com_err(argv[0], ret,
			"while opening file \"%s\"", filename);
		goto out;
	}

	if (o2fsck_state_init(ost->ost_fs, argv[0], ost)) {
		fprintf(stderr, "error allocating run-time state, exiting..\n");
		return 1;
	}

	/* XXX do we want checking for different revisions of ocfs2? */

	/* XXX worry about the journal */

	/* XXX should be verifying super-block bits here. */

	/* ocfs2_open() already checked _incompat and _ro_compat */
	if (OCFS2_RAW_SB(ost->ost_fs->fs_super)->s_feature_compat &
	    ~OCFS2_FEATURE_COMPAT_SUPP) {
		com_err(argv[0], OCFS2_ET_UNSUPP_FEATURE,
		        "while checking _compat flags");
		exit(FSCK_ERROR);
	}

	exit_if_skipping(ost);

	/* XXX we don't use the bad blocks inode, do we? */

	ret = o2fsck_pass1(ost);
	if (ret)
		com_err(argv[0], ret, "pass1 failed");

	ret = o2fsck_pass2(ost);
	if (ret)
		com_err(argv[0], ret, "pass2 failed");

	ret = o2fsck_pass3(ost);
	if (ret)
		com_err(argv[0], ret, "pass3 failed");


	ret = ocfs2_close(ost->ost_fs);
	if (ret) {
		com_err(argv[0], ret,
			"while closing file \"%s\"", filename);
	}

out:
	return 0;
}
