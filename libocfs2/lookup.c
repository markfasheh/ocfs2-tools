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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 * Authors: Joel Becker
 *
 *  This code is a port of e2fsprogs/lib/ext2fs/lookup.c
 *  Copyright (C) 1993, 1994, 1994, 1995 Theodore Ts'o.
 */

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE

#include <string.h>

#include "ocfs2.h"


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


errcode_t ocfs2_lookup(ocfs2_filesys *fs, uint64_t dir,
                       const char *name, int namelen, char *buf,
                       uint64_t *inode)
{
	errcode_t	retval;
	struct lookup_struct ls;

	ls.name = name;
	ls.len = namelen;
	ls.inode = inode;
	ls.found = 0;

	retval = ocfs2_dir_iterate(fs, dir, 0, buf, lookup_proc, &ls);
	if (retval)
		return retval;

	return (ls.found) ? 0 : OCFS2_ET_FILE_NOT_FOUND;
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

	fprintf(stdout, "/ (%llu)\n", blkno);

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
				"while looking up \"%s\" in inode %llu on \"%s\"\n",
				lookup_name, blkno, filename);
			goto out_free;
		}

		indent += 4;
		for (c = 0; c < indent; c++)
			fprintf(stdout, " ");
		fprintf(stdout, "%s (%llu)\n", lookup_name,
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

