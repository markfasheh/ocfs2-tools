/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * openfs.c
 *
 * Open an OCFS2 filesystem.  Part of the OCFS2 userspace library.
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
 * Authors: Joel Becker
 *
 * Ideas taken from e2fsprogs/lib/ext2fs/openfs.c
 *   Copyright (C) 1993, 1994, 1995, 1996 Theodore Ts'o.
 */

#define _XOPEN_SOURCE 600  /* Triggers XOPEN2K in features.h */
#define _LARGEFILE64_SOURCE

#include <string.h>

/* I hate glibc and gcc */
#ifndef ULLONG_MAX
# define ULLONG_MAX 18446744073709551615ULL
#endif

#include "ocfs2.h"

#include "ocfs1_fs_compat.h"


static errcode_t ocfs2_validate_ocfs1_header(ocfs2_filesys *fs)
{
	errcode_t ret;
	char *blk;
	ocfs1_vol_disk_hdr *hdr;
	
	ret = ocfs2_malloc_block(fs->fs_io, &blk);
	if (ret)
		return ret;

	ret = io_read_block(fs->fs_io, 0, 1, blk);
	if (ret)
		goto out;
	hdr = (ocfs1_vol_disk_hdr *)blk;

	ret = OCFS2_ET_OCFS_REV;
	if (le32_to_cpu(hdr->major_version) == OCFS1_MAJOR_VERSION)
		goto out;
	if (!memcmp(hdr->signature, OCFS1_VOLUME_SIGNATURE,
		    strlen(OCFS1_VOLUME_SIGNATURE)))
		goto out;

	ret = 0;

out:
	ocfs2_free(&blk);

	return ret;
}

static errcode_t ocfs2_get_fs_blocks(ocfs2_filesys *fs)
{
	errcode_t ret;
	char *buf;
	ocfs2_dinode *inode;
	int c_to_b_bits =
		OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits -
		OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;

	ret = ocfs2_lookup_system_inode(fs, GLOBAL_BITMAP_SYSTEM_INODE,
					0, &fs->fs_bm_blkno);
	if (ret)
		return ret;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_read_inode(fs, fs->fs_bm_blkno, buf);
	if (ret)
		goto out_free;

	inode = (ocfs2_dinode *)buf;

	fs->fs_clusters = inode->id1.bitmap1.i_total;
	fs->fs_blocks = (uint64_t)fs->fs_clusters << c_to_b_bits;

out_free:
	ocfs2_free(&buf);

	return ret;
}

static errcode_t ocfs2_read_super(ocfs2_filesys *fs, int superblock)
{
	errcode_t ret;
	char *blk;
	ocfs2_dinode *di;

	ret = ocfs2_malloc_block(fs->fs_io, &blk);
	if (ret)
		return ret;

	ret = io_read_block(fs->fs_io, superblock, 1, blk);
	if (ret)
		goto out_blk;
	di = (ocfs2_dinode *)blk;

	ret = OCFS2_ET_BAD_MAGIC;
	if (memcmp(di->i_signature, OCFS2_SUPER_BLOCK_SIGNATURE,
		   strlen(OCFS2_SUPER_BLOCK_SIGNATURE)))
		goto out_blk;

	fs->fs_super = di;

	/* FIXME: Swap the sucker here
	 * ocfs2_swap_inode()
	 * ocfs2_swap_super()
	 */

	return 0;

out_blk:
	ocfs2_free(&blk);

	return ret;
}

errcode_t ocfs2_open(const char *name, int flags,
		     unsigned int superblock, unsigned int block_size,
		     ocfs2_filesys **ret_fs)
{
	ocfs2_filesys *fs;
	errcode_t ret;

	ret = ocfs2_malloc0(sizeof(ocfs2_filesys), &fs);
	if (ret)
		return ret;

	fs->fs_flags = flags;
	fs->fs_umask = 022;

	ret = io_open(name, (flags & (OCFS2_FLAG_RO | OCFS2_FLAG_RW)),
		      &fs->fs_io);
	if (ret)
		goto out;

	ret = ocfs2_malloc(strlen(name)+1, &fs->fs_devname);
	if (ret)
		goto out;
	strcpy(fs->fs_devname, name);

	ret = ocfs2_validate_ocfs1_header(fs);
	if (ret)
		goto out;

	if (superblock) {
		ret = OCFS2_ET_INVALID_ARGUMENT;
		if (!block_size)
			goto out;
		io_set_blksize(fs->fs_io, block_size);
		ret = ocfs2_read_super(fs, superblock);
	} else {
		superblock = OCFS2_SUPER_BLOCK_BLKNO;
		if (block_size) {
			io_set_blksize(fs->fs_io, block_size);
			ret = ocfs2_read_super(fs, superblock);
		} else {
			for (block_size = io_get_blksize(fs->fs_io);
			     block_size <= OCFS2_MAX_BLOCKSIZE;
			     block_size <<= 1) {
				io_set_blksize(fs->fs_io, block_size);
				ret = ocfs2_read_super(fs, superblock);
				if (ret == OCFS2_ET_BAD_MAGIC)
					continue;
				break;
			}
		}
	}
	if (ret)
		goto out;

	fs->fs_blocksize = block_size;
	if (superblock == OCFS2_SUPER_BLOCK_BLKNO) {
		ret = ocfs2_malloc_block(fs->fs_io, &fs->fs_orig_super);
		if (ret)
			goto out;
		memcpy((char *)fs->fs_orig_super,
		       (char *)fs->fs_super, fs->fs_blocksize);
	}

#if 0
	ret = OCFS2_ET_REV_TOO_HIGH;
	if (fs->fs_super->id2.i_super.s_major_rev_level >
	    OCFS2_LIB_CURRENT_REV)
		goto out;
#endif

	ret = OCFS2_ET_UNSUPP_FEATURE;
	if (OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat &
	    ~OCFS2_LIB_FEATURE_INCOMPAT_SUPP)
		goto out;

	ret = OCFS2_ET_RO_UNSUPP_FEATURE;
	if ((flags & OCFS2_FLAG_RW) &&
	    (OCFS2_RAW_SB(fs->fs_super)->s_feature_ro_compat &
	     ~OCFS2_LIB_FEATURE_RO_COMPAT_SUPP))
		goto out;

	ret = OCFS2_ET_CORRUPT_SUPERBLOCK;
	if (!OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits)
		goto out;
	if (fs->fs_super->i_blkno != superblock)
		goto out;
	if ((OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits < 12) ||
	    (OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits > 20))
		goto out;
	if (!OCFS2_RAW_SB(fs->fs_super)->s_root_blkno ||
	    !OCFS2_RAW_SB(fs->fs_super)->s_system_dir_blkno)
		goto out;
	if (OCFS2_RAW_SB(fs->fs_super)->s_max_nodes > OCFS2_MAX_NODES)
		goto out;

	ret = OCFS2_ET_UNEXPECTED_BLOCK_SIZE;
	if (block_size !=
	    (1U << OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits))
		goto out;

	fs->fs_clustersize =
		1 << OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits;

	/* FIXME: Read the system dir */
	
	fs->fs_root_blkno =
		OCFS2_RAW_SB(fs->fs_super)->s_root_blkno;
	fs->fs_sysdir_blkno =
		OCFS2_RAW_SB(fs->fs_super)->s_system_dir_blkno;

	fs->fs_blocks = ULLONG_MAX;
	ret = ocfs2_get_fs_blocks(fs);
	if (ret)
		goto out;

	*ret_fs = fs;
	return 0;

out:
	ocfs2_freefs(fs);

	return ret;
}

#ifdef DEBUG_EXE
#include <getopt.h>
#include <limits.h>
#include <stdlib.h>

static int64_t read_number(const char *num)
{
	int64_t val;
	char *ptr;

	val = strtoll(num, &ptr, 0);
	if (!ptr || *ptr)
		return 0;

	return val;
}

static void print_usage(void)
{
	fprintf(stderr,
		"Usage: openfs [-s <superblock>] [-B <blksize>]\n"
	       	"               <filename>\n");
}

extern int opterr, optind;
extern char *optarg;

int main(int argc, char *argv[])
{
	errcode_t ret;
	int c;
	int64_t blkno, blksize;
	char *filename;
	ocfs2_filesys *fs;

	/* These mean "autodetect" */
	blksize = 0;
	blkno = 0;

	initialize_ocfs_error_table();

	while((c = getopt(argc, argv, "s:B:")) != EOF) {
		switch (c) {
			case 's':
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

			default:
				print_usage();
				return 1;
				break;
		}
	}

	if (blksize % OCFS2_MIN_BLOCKSIZE) {
		fprintf(stderr, "Invalid blocksize: %lld\n", blksize);
		print_usage();
		return 1;
	}

	if (optind >= argc) {
		fprintf(stderr, "Missing filename\n");
		print_usage();
		return 1;
	}

	filename = argv[optind];

	ret = ocfs2_open(filename, OCFS2_FLAG_RO, blkno, blksize, &fs);
	if (ret) {
		com_err(argv[0], ret,
			"while opening file \"%s\"", filename);
		goto out;
	}

	fprintf(stdout, "OCFS2 filesystem on \"%s\":\n", filename);
	fprintf(stdout,
		"\tblocksize = %d\n"
 		"\tclustersize = %d\n"
		"\tclusters = %u\n"
		"\tblocks = %llu\n",
 		fs->fs_blocksize,
		fs->fs_clustersize,
		fs->fs_clusters,
		fs->fs_blocks);

	ret = ocfs2_close(fs);
	if (ret) {
		com_err(argv[0], ret,
			"while closing file \"%s\"", filename);
	}

out:
	return 0;
}
#endif  /* DEBUG_EXE */
