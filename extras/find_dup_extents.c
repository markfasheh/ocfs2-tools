/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * find_dup_extents.c
 *
 * Simple tool to iterate the inodes and find duplicate extents.
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301 USA.
 *
 * Authors: Joel Becker
 *
 *  This code is a port of e2fsprogs/lib/ext2fs/dir_iterate.c
 *  Copyright (C) 1993, 1994, 1994, 1995, 1996, 1997 Theodore Ts'o.
 */

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "ocfs2/ocfs2.h"

static void print_usage(void)
{
	fprintf(stderr,
		"Usage: find_dup_extents <filename>\n");
}

struct walk_extents {
	char *argv0;
	ocfs2_filesys *fs;
	uint64_t blkno;
	int has_dups;
	ocfs2_bitmap *extent_map;
	ocfs2_bitmap *dup_map;
};

static int extent_set_func(ocfs2_filesys *fs,
			   struct ocfs2_extent_rec *rec,
			   int tree_depth,
			   uint32_t ccount,
			   uint64_t ref_blkno,
			   int ref_recno,
			   void *priv_data)
{
	errcode_t ret;
	struct walk_extents *we = priv_data;
	uint32_t cluster, i;
	int oldval;
	int b_to_c_bits =
		OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits -
		OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;

	cluster = (uint32_t)(rec->e_blkno >> b_to_c_bits);
	for (i = 0; i < ocfs2_rec_clusters(tree_depth, rec); i++) {
		ret = ocfs2_bitmap_set(we->extent_map,
				       cluster + i,
				       &oldval);
		if (ret) {
			com_err(we->argv0, ret,
				"while setting bit for cluster %u",
				cluster + i);
			return OCFS2_EXTENT_ABORT;
		}
		if (oldval) {
			we->has_dups = 1;
			ret = ocfs2_bitmap_set(we->dup_map,
					       cluster + i,
					       NULL);
			if (ret) {
				com_err(we->argv0, ret,
					"while setting bit for cluster %u",
					cluster + i);
				return OCFS2_EXTENT_ABORT;
			}
		}
	}

	return 0;
}

static int extent_test_func(ocfs2_filesys *fs,
			    struct ocfs2_extent_rec *rec,
			    int tree_depth,
			    uint32_t ccount,
			    uint64_t ref_blkno,
			    int ref_recno,
			    void *priv_data)
{
	errcode_t ret;
	struct walk_extents *we = priv_data;
	uint32_t cluster, i;
	int oldval;
	int b_to_c_bits =
		OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits -
		OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;

	cluster = (uint32_t)(rec->e_blkno >> b_to_c_bits);
	for (i = 0; i < ocfs2_rec_clusters(tree_depth, rec); i++) {
		ret = ocfs2_bitmap_test(we->dup_map,
					cluster + i,
					&oldval);
		if (ret) {
			com_err(we->argv0, ret,
				"while checking bit for cluster %u",
				cluster + i);
			return OCFS2_EXTENT_ABORT;
		}
		if (oldval) {
			fprintf(stdout,
				"Dup! %20"PRIu64" : %u\n",
				we->blkno, cluster + i);
		}
	}

	return 0;
}


static errcode_t run_scan(struct walk_extents *we, int test)
{
	errcode_t ret;
	uint64_t blkno;
	char *buf;
	int done;
	struct ocfs2_dinode *di;
	ocfs2_inode_scan *scan;
	int (*extent_func)(ocfs2_filesys *fs,
			   struct ocfs2_extent_rec *rec,
			   int tree_depth,
			   uint32_t ccount,
			   uint64_t ref_blkno,
			   int ref_recno,
			   void *priv_data);

	if (test)
		extent_func = extent_test_func;
	else
		extent_func = extent_set_func;

	ret = ocfs2_malloc_block(we->fs->fs_io, &buf);
	if (ret) {
		com_err(we->argv0, ret,
			"while allocating inode buffer");
		return ret;
	}

	di = (struct ocfs2_dinode *)buf;

	ret = ocfs2_open_inode_scan(we->fs, &scan);
	if (ret) {
		com_err(we->argv0, ret,
			"while opening inode scan");
		goto out_free;
	}

	done = 0;
	while (!done) {
		ret = ocfs2_get_next_inode(scan, &blkno, buf);
		if (ret) {
			com_err(we->argv0, ret,
				"while getting next inode");
			goto out_close_scan;
		}
		if (blkno) {
			if (memcmp(di->i_signature,
				   OCFS2_INODE_SIGNATURE,
				   strlen(OCFS2_INODE_SIGNATURE)))
				continue;

			ocfs2_swap_inode_to_cpu(we->fs, di);

			if (!(di->i_flags & OCFS2_VALID_FL))
				continue;

			if ((di->i_flags & OCFS2_SYSTEM_FL) &&
			    (di->i_flags & (OCFS2_SUPER_BLOCK_FL |
					    OCFS2_LOCAL_ALLOC_FL |
					    OCFS2_CHAIN_FL)))
				continue;

			if (!di->i_clusters && S_ISLNK(di->i_mode))
				continue;

			we->blkno = blkno;
			ret = ocfs2_extent_iterate(we->fs, blkno,
						   OCFS2_EXTENT_FLAG_DATA_ONLY,
						   NULL,
						   extent_func,
						   we);
			if (ret) {
				com_err(we->argv0, ret,
					"while walking inode %"PRIu64, blkno);
				goto out_close_scan;
			}
		}
		else
			done = 1;
	}

out_close_scan:
	ocfs2_close_inode_scan(scan);

out_free:
	ocfs2_free(&buf);

	return ret;
}


int main(int argc, char *argv[])
{
	errcode_t ret;
	char *filename;
	ocfs2_filesys *fs;
	struct walk_extents we;

	initialize_ocfs_error_table();

	if (argc < 2) {
		fprintf(stderr, "Missing filename\n");
		print_usage();
		return 1;
	}
	filename = argv[1];

	we.argv0 = argv[0];
	we.has_dups = 0;

	ret = ocfs2_open(filename, OCFS2_FLAG_RO, 0, 0, &fs);
	if (ret) {
		com_err(argv[0], ret,
			"while opening file \"%s\"", filename);
		goto out;
	}
	we.fs = fs;

        ret = ocfs2_cluster_bitmap_new(fs, "Used extent map",
                                       &we.extent_map);
        if (ret) {
		com_err(argv[0], ret, 
			"while creating the extent map");
		goto out_close;
	}
        ret = ocfs2_cluster_bitmap_new(fs, "Dup extent map",
                                       &we.dup_map);
        if (ret) {
		ocfs2_bitmap_free(&we.extent_map);
		com_err(argv[0], ret, 
			"while creating the dup map");
		goto out_close;
	}

	ret = run_scan(&we, 0);
	if (!ret && we.has_dups)
		ret = run_scan(&we, 1);

	ocfs2_bitmap_free(&we.extent_map);
	ocfs2_bitmap_free(&we.dup_map);

out_close:
	ret = ocfs2_close(fs);
	if (ret) {
		com_err(argv[0], ret,
			"while closing file \"%s\"", filename);
	}

out:
	return 0;
}

