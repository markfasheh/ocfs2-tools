/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * find_inode_paths.c
 *
 * Takes an inode block number and find all paths leading to it.
 *
 * Copyright (C) 2004, 2005 Oracle.  All rights reserved.
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
 *  This code is a port of e2fsprogs/lib/ext2fs/dir_iterate.c
 *  Copyright (C) 1993, 1994, 1994, 1995, 1996, 1997 Theodore Ts'o.
 */

#include "main.h"

struct walk_path {
	char *argv0;
	FILE *out;
	ocfs2_filesys *fs;
	char *path;
	uint32_t found;
	uint32_t count;
	int findall;
	uint64_t *inode;
};

static int walk_tree_func(struct ocfs2_dir_entry *dentry, int offset,
			  int blocksize, char *buf, void *priv_data)
{
	errcode_t ret;
	int len, oldval;
	int reti = 0;
	int i = 0;
	int print = 0;
	char *old_path, *path;
	struct walk_path *wp = priv_data;

	if (!strncmp(dentry->name, ".", dentry->name_len) ||
	    !strncmp(dentry->name, "..", dentry->name_len))
		return 0;

	len = strlen(wp->path);

	if (len + dentry->name_len > 4095) {
		com_err(wp->argv0, OCFS2_ET_NO_SPACE,
			"name is too long in %s\n", wp->path);
		return OCFS2_DIRENT_ABORT;
	}

	ret = ocfs2_malloc0(4096, &path);
	if (ret) {
		com_err(wp->argv0, ret,
			"while allocating path memory in %s\n", wp->path);
		return OCFS2_DIRENT_ABORT;
	}
	
	memcpy(path, wp->path, len);
	memcpy(path + len, dentry->name, dentry->name_len);
	if (dentry->file_type == OCFS2_FT_DIR)
		path[len + dentry->name_len] = '/';

	oldval = 0;

	for (i = 0; i < wp->count; ++i) {
		if (dentry->inode == wp->inode[i]) {
			if (!print)
				dump_inode_path (wp->out, dentry->inode, path);
			++wp->found;
			++print;
		}
	}

	if (!wp->findall) {
		if (wp->found >= wp->count) {
			ocfs2_free(&path);	
			return OCFS2_DIRENT_ABORT;
		}
	}

	if (dentry->file_type == OCFS2_FT_DIR) {
		old_path = wp->path;
		wp->path = path;
		ret = ocfs2_dir_iterate(wp->fs, dentry->inode, 0, NULL,
					walk_tree_func, wp);
		if (ret) {
			com_err(wp->argv0, ret, "while walking %s", wp->path);
			reti = OCFS2_DIRENT_ABORT;
		}
		wp->path = old_path;
	}

	ocfs2_free(&path);

	return reti;
}

errcode_t find_inode_paths(ocfs2_filesys *fs, char **args, int findall,
			   uint32_t count, uint64_t *blknos, FILE *out)
{
	errcode_t ret = 0;
	struct walk_path wp;
	int i;
	int printroot = 0;
	int printsysd = 0;

	wp.argv0 = args[0];
	wp.out = out;
	wp.count = count;
	wp.inode = blknos;
	wp.findall = findall;
	wp.found = 0;
	wp.fs = fs;

	/* Compare with root and sysdir */
	for (i = 0; i < count; ++i) {
		if (blknos[i] == fs->fs_root_blkno) {
			if (!printroot)
				dump_inode_path (out, blknos[i], "/");
			++wp.found;
			++printroot;
		}
		if (blknos[i] == fs->fs_sysdir_blkno) {
			if (!printsysd)
				dump_inode_path (out, blknos[i], "//");
			++wp.found;
			++printsysd;
		}
	}

	if (!findall) {
		if (wp.found >= wp.count)
			goto bail;
	}


	/* Walk system dir */
	wp.path = "//";
	ret = ocfs2_dir_iterate(fs,
				OCFS2_RAW_SB(fs->fs_super)->s_system_dir_blkno,
				0, NULL, walk_tree_func, &wp);
	if (ret) {
		com_err(args[0], ret, "while walking system dir");
		goto bail;
	}

	/* Walk root dir */
	wp.path = "/";
	ret = ocfs2_dir_iterate(fs,
				OCFS2_RAW_SB(fs->fs_super)->s_root_blkno,
				0, NULL, walk_tree_func, &wp);
	if (ret) {
		com_err(args[0], ret, "while walking root dir");
		goto bail;
	}

	if (!wp.found)
		com_err(args[0], OCFS2_ET_FILE_NOT_FOUND, " ");

bail:
	return ret;
}
