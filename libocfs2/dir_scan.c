/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dir_scan.c
 *
 * Read all the entries in a directory.  For the OCFS2 userspace
 * library.
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
 * Authors: Manish Singh
 */

#include <string.h>
#include <inttypes.h>

#include "ocfs2.h"

#include "dir_util.h"


struct _ocfs2_dir_scan {
	ocfs2_filesys *fs;
	int flags;
	char *buf;
	unsigned int bufsize;
	unsigned int total_bufsize;
	ocfs2_cached_inode *inode;
	uint64_t total_blocks;
	uint64_t blocks_read;
	unsigned int offset;
};


static errcode_t get_more_dir_blocks(ocfs2_dir_scan *scan)
{
	errcode_t ret;
	uint64_t blkno;
	uint64_t cblocks;

	if (scan->blocks_read == scan->total_blocks)
		return OCFS2_ET_ITERATION_COMPLETE;

	ret = ocfs2_extent_map_get_blocks(scan->inode,
					  scan->blocks_read, 1,
					  &blkno, &cblocks, NULL);
	if (ret)
		return ret;

	ret = ocfs2_read_dir_block(scan->fs, blkno, scan->buf);
	if (ret)
		return ret;

	scan->blocks_read++;

	scan->bufsize = scan->total_bufsize;
	scan->offset = 0;

	return 0;
}

static inline int valid_dirent(ocfs2_dir_scan *scan,
			       struct ocfs2_dir_entry *dirent)
{
	if (dirent->inode) {
		if ((scan->flags & OCFS2_DIR_SCAN_FLAG_EXCLUDE_DOTS) &&
		    is_dots(dirent->name, dirent->name_len))
			return 0;
		else
			return 1;
	}

	return 0;
}

errcode_t ocfs2_get_next_dir_entry(ocfs2_dir_scan *scan,
				   struct ocfs2_dir_entry *out_dirent)
{
	errcode_t ret;
	struct ocfs2_dir_entry *dirent;

	do {
		if (scan->offset == scan->bufsize) {
			ret = get_more_dir_blocks(scan);
			if (ret == OCFS2_ET_ITERATION_COMPLETE) {
				memset(out_dirent, 0,
				       sizeof(struct ocfs2_dir_entry));
				return 0;
			}
			if (ret)
				return ret;
		}

		dirent = (struct ocfs2_dir_entry *) (scan->buf + scan->offset);

		if (((scan->offset + dirent->rec_len) > scan->fs->fs_blocksize) ||
		    (dirent->rec_len < 8) ||
		    ((dirent->rec_len % 4) != 0) ||
		    (((dirent->name_len & 0xFF)+8) > dirent->rec_len))
			return OCFS2_ET_DIR_CORRUPTED;

		scan->offset += dirent->rec_len;
	} while (!valid_dirent(scan, dirent));

	memcpy(out_dirent, dirent, sizeof(struct ocfs2_dir_entry));

	return 0;
}

errcode_t ocfs2_open_dir_scan(ocfs2_filesys *fs, uint64_t dir, int flags,
			      ocfs2_dir_scan **ret_scan)
{
	ocfs2_dir_scan *scan;
	errcode_t ret;

	ret = ocfs2_check_directory(fs, dir);
	if (ret)
		return ret;

	ret = ocfs2_malloc0(sizeof(struct _ocfs2_dir_scan), &scan);
	if (ret)
		return ret;

	scan->fs = fs;
	scan->flags = flags;

	ret = ocfs2_malloc_block(fs->fs_io, &scan->buf);
	if (ret)
		goto bail_scan;

	ret = ocfs2_read_cached_inode(fs, dir, &scan->inode);
	if (ret)
		goto bail_dir_block;

	scan->total_blocks = scan->inode->ci_inode->i_size /
		fs->fs_blocksize;
	/*
	 * Should we check i_size % blocksize?
	 * total_blocks <= i_clusters?
	 */

	scan->total_bufsize = fs->fs_blocksize;

	*ret_scan = scan;

	return 0;

bail_dir_block:
	ocfs2_free(&scan->buf);

bail_scan:
	ocfs2_free(&scan);
	return ret;
}

void ocfs2_close_dir_scan(ocfs2_dir_scan *scan)
{
	if (!scan)
		return;

	ocfs2_free_cached_inode(scan->fs, scan->inode);
	ocfs2_free(&scan->buf);
	ocfs2_free(&scan);

	return;
}



#ifdef DEBUG_EXE
#include <string.h>
#include <stdlib.h>
#include <getopt.h>

static uint64_t read_number(const char *num)
{
	uint64_t val;
	char *ptr;

	val = strtoull(num, &ptr, 0);
	if (!ptr || *ptr)
		return 0;

	return val;
}

static void print_usage(void)
{
	fprintf(stderr,
		"Usage: dir_scan -i <inode_blkno> <filename>\n");
}


extern int opterr, optind;
extern char *optarg;

int main(int argc, char *argv[])
{
	errcode_t ret;
	char *filename;
	uint64_t blkno;
	int c;
	ocfs2_filesys *fs;
	ocfs2_dir_scan *scan;
	int done;
	struct ocfs2_dir_entry *dirent;

	blkno = 0;

	initialize_ocfs_error_table();

	while ((c = getopt(argc, argv, "i:")) != EOF) {
		switch (c) {
			case 'i':
				blkno = read_number(optarg);
				if (blkno <= OCFS2_SUPER_BLOCK_BLKNO) {
					fprintf(stderr,
						"Invalid inode block: %s\n",
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

	if (optind >= argc) {
		fprintf(stderr, "Missing filename\n");
		print_usage();
		return 1;
	}
	filename = argv[optind];
	
	ret = ocfs2_open(filename, OCFS2_FLAG_RO, 0, 0, &fs);
	if (ret) {
		com_err(argv[0], ret,
			"while opening file \"%s\"", filename);
		goto out;
	}

	ret = ocfs2_malloc0(sizeof(struct ocfs2_dir_entry), &dirent);
	if (ret) {
		com_err(argv[0], ret,
			"while allocating dirent buffer");
		goto out_close;
	}

	if (blkno == 0)
		blkno = fs->fs_root_blkno;

	ret = ocfs2_open_dir_scan(fs, blkno, 0, &scan);
	if (ret) {
		com_err(argv[0], ret,
			"while opening dir scan");
		goto out_free;
	}

	done = 0;
	while (!done) {
		ret = ocfs2_get_next_dir_entry(scan, dirent);
		if (ret) {
			com_err(argv[0], ret,
				"while getting next dirent");
			goto out_close_scan;
		}
		if (dirent->rec_len) {
			dirent->name[dirent->name_len] = '\0';
			fprintf(stdout, "%s\n", dirent->name);
		}
		else
			done = 1;
	}

out_close_scan:
	ocfs2_close_dir_scan(scan);

out_free:
	ocfs2_free(&dirent);

out_close:
	ret = ocfs2_close(fs);
	if (ret) {
		com_err(argv[0], ret,
			"while closing file \"%s\"", filename);
	}

out:
	return 0;
}
#endif  /* DEBUG_EXE */
