/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * find_inode_paths.c
 *
 * Simple tool to take an inode block number and find all paths
 * leading to it.
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
 * Authors: Mark Fasheh, Joel Becker
 *
 *  This code is a port of e2fsprogs/lib/ext2fs/dir_iterate.c
 *  Copyright (C) 1993, 1994, 1994, 1995, 1996, 1997 Theodore Ts'o.
 */

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE
#define _DEFAULT_SOURCE

#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <inttypes.h>

#include "ocfs2/ocfs2.h"

static void print_usage(void)
{
	fprintf(stderr,
		"Usage: find_inode_paths <filename> <inode #>\n");
}

struct walk_path {
	char *argv0;
	ocfs2_filesys *fs;
	char *path;
	int quiet;
	uint64_t inode;
};

static int walk_tree_func(struct ocfs2_dir_entry *dentry,
			  uint64_t blocknr,
			  int offset,
			  int blocksize,
			  char *buf,
			  void *priv_data)
{
	errcode_t ret;
	int len;
	int reti = 0;
	char *old_path, *path;
	struct walk_path *wp = priv_data;

	if (!strncmp(dentry->name, ".", dentry->name_len) ||
	    !strncmp(dentry->name, "..", dentry->name_len))
		return 0;

	len = strlen(wp->path);

	if (len + dentry->name_len > 4095) {
		fprintf(stderr, "name is too long in %s\n", wp->path);
		return OCFS2_DIRENT_ABORT;
	}

	ret = ocfs2_malloc0(4096, &path);
	if (ret) {
		com_err(wp->argv0, ret,
			"while allocating path memory in %s\n",
			wp->path);
		return OCFS2_DIRENT_ABORT;
	}
	
	memcpy(path, wp->path, len);
	memcpy(path + len, dentry->name, dentry->name_len);
	if (dentry->file_type == OCFS2_FT_DIR)
		path[len + dentry->name_len] = '/';

	if (!wp->quiet)
		fprintf(stdout, "[trace] %13"PRIu64" %s\n",
			(uint64_t)dentry->inode, path);

	if (dentry->inode == wp->inode)
		fprintf(stdout, "[found] %13"PRIu64" %s\n",
			(uint64_t)dentry->inode, path);

	if (dentry->file_type == OCFS2_FT_DIR) {
		old_path = wp->path;
		wp->path = path;
		ret = ocfs2_dir_iterate(wp->fs, dentry->inode, 0, NULL,
					walk_tree_func, wp);
		if (ret) {
			com_err(wp->argv0, ret,
				"while walking %s", wp->path);
			reti = OCFS2_DIRENT_ABORT;
		}
		wp->path = old_path;
	}

	ocfs2_free(&path);

	return reti;
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

int main(int argc, char *argv[])
{
	errcode_t ret;
	uint64_t blkno;
	char *filename;
	ocfs2_filesys *fs;
	struct walk_path wp;

	blkno = OCFS2_SUPER_BLOCK_BLKNO;

	initialize_ocfs_error_table();

	wp.argv0 = argv[0];
	wp.quiet = 1;
	if (argc < 3) {
		fprintf(stderr, "Missing filename\n");
		print_usage();
		return 1;
	}
	filename = argv[1];

	wp.inode = read_number(argv[2]);
	if (!wp.inode) {
		fprintf(stderr, "invalid inode number\n");
		print_usage();
		return 1;
	}

	ret = ocfs2_open(filename, OCFS2_FLAG_RO, 0, 0, &fs);
	if (ret) {
		com_err(argv[0], ret,
			"while opening file \"%s\"", filename);
		goto out;
	}
	wp.fs = fs;
	fprintf(stdout, "Finding all paths leading to inode %"PRIu64"\n", 
		wp.inode);

	if (!wp.quiet)
		fprintf(stdout, "Walking system directory...\n");
	wp.path = "<system_dir>/";
	ret = ocfs2_dir_iterate(fs,
				OCFS2_RAW_SB(fs->fs_super)->s_system_dir_blkno,
				0, NULL, walk_tree_func, &wp);
	if (ret) {
		com_err(argv[0], ret,
			"while walking sysdm dir inode %"PRIu64" on \"%s\"\n",
			blkno, filename);
		goto out_close;
	}
	wp.path = "/";

	if (!wp.quiet)
		fprintf(stdout, "Walking root directory...\n");
	ret = ocfs2_dir_iterate(fs,
				OCFS2_RAW_SB(fs->fs_super)->s_root_blkno,
				0, NULL, walk_tree_func, &wp);
	if (ret) {
		com_err(argv[0], ret,
			"while walking root inode %"PRIu64" on \"%s\"\n",
			blkno, filename);
		goto out_close;
	}

out_close:
	ret = ocfs2_close(fs);
	if (ret) {
		com_err(argv[0], ret,
			"while closing file \"%s\"", filename);
	}

out:
	return 0;
}

