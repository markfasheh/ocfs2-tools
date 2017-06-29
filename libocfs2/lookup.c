/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * lookup.c
 *
 * Directory lookup routines for the OCFS2 userspace library.
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
 *  This code is a port of e2fsprogs/lib/ext2fs/lookup.c
 *  Copyright (C) 1993, 1994, 1994, 1995 Theodore Ts'o.
 */

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE

#include <string.h>
#include <inttypes.h>
#include <assert.h>

#include "ocfs2/ocfs2.h"


struct lookup_struct  {
	const char	*name;
	int		len;
	uint64_t	*inode;
	int		found;
};	

#ifdef __TURBOC__
 #pragma argsused
#endif
static int lookup_proc(struct ocfs2_dir_entry *dirent,
		       uint64_t	blocknr,
		       int	offset,
		       int	blocksize,
		       char	*buf,
		       void	*priv_data)
{
	struct lookup_struct *ls = (struct lookup_struct *) priv_data;

	if (ls->len != (dirent->name_len & 0xFF))
		return 0;
	if (strncmp(ls->name, dirent->name, (dirent->name_len & 0xFF)))
		return 0;
	*ls->inode = dirent->inode;
	ls->found++;
	return OCFS2_DIRENT_ABORT;
}

static errcode_t ocfs2_find_entry_dx(ocfs2_filesys *fs,
				struct ocfs2_dinode *di,
				char *buf,
				struct lookup_struct *ls)
{
	char *dx_root_buf = NULL;
	struct ocfs2_dx_root_block *dx_root;
	struct ocfs2_dir_lookup_result lookup;
	errcode_t ret;

	ret = ocfs2_malloc_block(fs->fs_io, &dx_root_buf);
	if (ret)
		goto out;
	ret = ocfs2_read_dx_root(fs, di->i_dx_root, dx_root_buf);
	if (ret)
		goto out;
	dx_root = (struct ocfs2_dx_root_block *)dx_root_buf;

	memset(&lookup, 0, sizeof(struct ocfs2_dir_lookup_result));
	ocfs2_dx_dir_name_hash(fs, ls->name,
			ls->len, &lookup.dl_hinfo);

	ret = ocfs2_dx_dir_search(fs, ls->name, ls->len,
				dx_root, &lookup);
	if (ret)
		goto out;

	*ls->inode = lookup.dl_entry->inode;
	ls->found++;
	ret = 0;

out:
	release_lookup_res(&lookup);
	if (dx_root_buf)
		ocfs2_free(&dx_root_buf);
	return ret;
}

errcode_t ocfs2_lookup(ocfs2_filesys *fs, uint64_t dir,
                       const char *name, int namelen, char *buf,
                       uint64_t *inode)
{
	errcode_t ret;
	struct lookup_struct ls;
	char *di_buf = NULL;
	struct ocfs2_dinode *di;

	ls.name = name;
	ls.len = namelen;
	ls.inode = inode;
	ls.found = 0;

	ret = ocfs2_malloc_block(fs->fs_io, &di_buf);
	if (ret)
		goto out;
	ret = ocfs2_read_inode(fs, dir, di_buf);
	if (ret)
		goto out;
	di = (struct ocfs2_dinode *)di_buf;

	if (ocfs2_supports_indexed_dirs(OCFS2_RAW_SB(fs->fs_super)) &&
	    ocfs2_dir_indexed(di)) {
		ret = ocfs2_find_entry_dx(fs, di, buf, &ls);
	} else {
		ret = ocfs2_dir_iterate(fs, dir, 0, buf, lookup_proc, &ls);
	}
	if (ret)
		goto out;

	ret = (ls.found) ? 0 : OCFS2_ET_FILE_NOT_FOUND;

out:
	if(di_buf)
		ocfs2_free(&di_buf);
	return ret;
}



#ifdef DEBUG_EXE
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
		"Usage: lookup [-i <start_blkno>] <filename> <path_to_find>\n");
}


extern int opterr, optind;
extern char *optarg;

int main(int argc, char *argv[])
{
	errcode_t ret;
	uint64_t blkno, result_blkno;
	int c, indent;
	char *filename, *lookup_path, *buf;
	char *p;
	char lookup_name[256];
	ocfs2_filesys *fs;

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
	optind++;

	if (optind >= argc) {
		fprintf(stdout, "Missing path to lookup\n");
		print_usage();
		return 1;
	}
	lookup_path = argv[optind];

	ret = ocfs2_open(filename, OCFS2_FLAG_RO, 0, 0, &fs);
	if (ret) {
		com_err(argv[0], ret,
			"while opening file \"%s\"", filename);
		goto out;
	}

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret) {
		com_err(argv[0], ret,
			"while allocating inode buffer");
		goto out_close;
	}

	if (!blkno)
		blkno = OCFS2_RAW_SB(fs->fs_super)->s_root_blkno;

	for (p = lookup_path; *p == '/'; p++);

	lookup_path = p;

	fprintf(stdout, "/ (%"PRIu64")\n", blkno);

	indent = 0;
	for (p = lookup_path; ; p++) {
		if (*p && *p != '/')
			continue;

		memcpy(lookup_name, lookup_path, p - lookup_path);
		lookup_name[p - lookup_path] = '\0';
		ret = ocfs2_lookup(fs, blkno, lookup_name,
				   strlen(lookup_name), NULL,
				   &result_blkno);
		if (ret) {
			com_err(argv[0], ret,
				"while looking up \"%s\" in inode %"PRIu64" on"
			        " \"%s\"\n",
				lookup_name, blkno, filename);
			goto out_free;
		}

		indent += 4;
		for (c = 0; c < indent; c++)
			fprintf(stdout, " ");
		fprintf(stdout, "%s (%"PRIu64")\n", lookup_name,
			result_blkno);

		blkno = result_blkno;

		for (; *p == '/'; p++);

		lookup_path = p;

		if (!*p)
			break;
	}

out_free:
	ocfs2_free(&buf);

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

