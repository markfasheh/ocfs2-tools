/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * truncate.c
 *
 * Truncate an OCFS2 inode.  For the OCFS2 userspace library.
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
 */

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE

#include <string.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "ocfs2.h"

/*
 * Delete and free clusters if needed.  This only works with DEPTH_TRAVERSE.
 */
static int truncate_iterate(ocfs2_filesys *fs,
			    struct ocfs2_extent_rec *rec,
			    int tree_depth, uint32_t ccount,
			    uint64_t ref_blkno, int ref_recno,
			    void *priv_data)
{
	uint32_t len, new_i_clusters = *(uint32_t *)priv_data;
	uint64_t start = 0;
	errcode_t ret;

	if ((rec->e_cpos + rec->e_clusters) <= new_i_clusters)
		return 0;

	if (rec->e_cpos >= new_i_clusters) {
		/* the rec is entirely outside the new size, free it */
		if (!tree_depth) {
			start = rec->e_blkno;
			len = rec->e_clusters;
		} else {
			/* here we meet with a full empty extent block, delete
			 * it. The extent list it contains should already be
			 * iterated and all the clusters have been freed.
			 */
			ret = ocfs2_delete_extent_block(fs, rec->e_blkno);
			if (ret)
				goto bail;
		}

		rec->e_blkno = 0;
		rec->e_clusters = 0;
		rec->e_cpos = 0;
	} else {
		/* we're truncating into the middle of the rec */
		len = rec->e_cpos + rec->e_clusters;
		len -= new_i_clusters;
		rec->e_clusters = new_i_clusters - rec->e_cpos;
		if (!tree_depth)
			start = rec->e_blkno +
				ocfs2_clusters_to_blocks(fs, rec->e_clusters);
	}

	if (start) {
		ret = ocfs2_free_clusters(fs, len, start);
		if (ret)
			goto bail;
	}

	return OCFS2_EXTENT_CHANGED;
bail:
	return OCFS2_EXTENT_ERROR;
}

/* XXX care about zeroing new clusters and final partially truncated 
 * clusters */
errcode_t ocfs2_truncate(ocfs2_filesys *fs, uint64_t ino, uint64_t new_i_size)
{
	errcode_t ret;
	char *buf;
	struct ocfs2_dinode *di;
	uint32_t new_i_clusters;
	uint64_t new_i_blocks;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_read_inode(fs, ino, buf);
	if (ret)
		goto out;
	di = (struct ocfs2_dinode *)buf;

	if (di->i_size == new_i_size)
		goto out;

	new_i_blocks = ocfs2_blocks_in_bytes(fs, new_i_size);
	new_i_clusters = ocfs2_clusters_in_blocks(fs, new_i_blocks);

	if (di->i_clusters < new_i_clusters) {
		ret = ocfs2_extend_allocation(fs, ino,
					new_i_clusters - di->i_clusters);
		if (ret)
			goto out;

		/* the information of dinode has been changed, and we need to
		 * read it again.
		 */
		ret = ocfs2_read_inode(fs, ino, buf);
		if (ret)
			goto out;
	} else {
		ret = ocfs2_extent_iterate_inode(fs, di,
						 OCFS2_EXTENT_FLAG_DEPTH_TRAVERSE,
						 NULL, truncate_iterate,
						 &new_i_clusters);
		if (ret)
			goto out;

		/* now all the clusters and extent blocks are freed.
		 * only when the file's content is empty, should the tree depth
		 * change.
		 */
		if (new_i_clusters == 0)
			di->id2.i_list.l_tree_depth = 0;

	}

	di->i_clusters = new_i_clusters;
	di->i_size = new_i_size;
	ret = ocfs2_write_inode(fs, ino, buf);

out:
	ocfs2_free(&buf);

	return ret;
}

#ifdef DEBUG_EXE
#include <stdlib.h>
#include <getopt.h>
#include <inttypes.h>

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
		"Usage: debug_truncate -i <ino_blkno> -s <new_size> device\n");
}

extern int opterr, optind;
extern char *optarg;

int main(int argc, char *argv[])
{
	errcode_t ret;
	int c;
	uint64_t blkno = 0, new_size = 0;
	ocfs2_filesys *fs;
	char *device;

	initialize_ocfs_error_table();

	while ((c = getopt(argc, argv, "i:s:")) != EOF) {
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

			case 's':
				new_size = read_number(optarg);
				break;

			default:
				print_usage();
				return 1;
				break;
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "Missing device name\n");
		print_usage();
		return 1;
	}
	device = argv[optind];

	if (!blkno || !new_size) {
		print_usage();
		return 1;
	}

	ret = ocfs2_open(device, OCFS2_FLAG_RW, 0, 0, &fs);
	if (ret) {
		com_err(argv[0], ret,
			"while opening file \"%s\"", device);
		return ret;
	}

	ret = ocfs2_truncate(fs, blkno, new_size);
	if (ret)
		com_err(argv[0], ret, "while truncating inode %"PRIu64, blkno);

	ocfs2_close(fs);

	return ret;
}
#endif  /* DEBUG_EXE */
