/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * fileio.c
 *
 * I/O to files.  Part of the OCFS2 userspace library.
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
 * Ideas taken from e2fsprogs/lib/ext2fs/fileio.c
 *   Copyright (C) 1997 Theodore Ts'o.
 */

#define _XOPEN_SOURCE 600  /* Triggers XOPEN2K in features.h */
#define _LARGEFILE64_SOURCE

#include <string.h>
#include <limits.h>
#include <inttypes.h>

#include "ocfs2.h"

struct read_whole_context {
	char		*buf;
	char		*ptr;
	int		size;
	int		offset;
	errcode_t	errcode;
};

static int read_whole_func(ocfs2_filesys *fs,
			   uint64_t blkno,
			   uint64_t bcount,
			   void *priv_data)
{
	struct read_whole_context *ctx = priv_data;

	ctx->errcode = io_read_block(fs->fs_io, blkno,
				     1, ctx->ptr);
	if (ctx->errcode)
		return OCFS2_BLOCK_ABORT;

	ctx->ptr += fs->fs_blocksize;
	ctx->offset += fs->fs_blocksize;

	return 0;
}

errcode_t ocfs2_read_whole_file(ocfs2_filesys *fs,
				uint64_t blkno,
				char **buf,
				int *len)
{
	struct read_whole_context	ctx;
	errcode_t			retval;
	char *inode_buf;
	ocfs2_dinode *di;
	int c_to_b_bits = 
		OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits -
		OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;

	/* So the caller can see nothing was read */
	*len = 0;
	*buf = NULL;

	retval = ocfs2_malloc_block(fs->fs_io, &inode_buf);
	if (retval)
		return retval;

	retval = ocfs2_read_inode(fs, blkno, inode_buf);
	if (retval)
		goto out_free;

	di = (ocfs2_dinode *)inode_buf;

	/* Arbitrary limit for our malloc */
	retval = OCFS2_ET_INVALID_ARGUMENT;
	if (di->i_size > INT_MAX) 
		goto out_free;

	retval = ocfs2_malloc_blocks(fs->fs_io,
				     di->i_clusters << c_to_b_bits,
				     buf);
	if (retval)
		goto out_free;

	ctx.buf = *buf;
	ctx.ptr = *buf;
	ctx.size = di->i_size;
	ctx.offset = 0;
	ctx.errcode = 0;
	retval = ocfs2_block_iterate(fs, blkno, 0,
				     read_whole_func, &ctx);

	*len = ctx.size;
	if (ctx.offset < ctx.size)
		*len = ctx.offset;

out_free:
	ocfs2_free(&inode_buf);

	if (!(*len)) {
		ocfs2_free(buf);
		*buf = NULL;
	}

	if (retval)
		return retval;
	return ctx.errcode;
}

/*
 * FIXME: port the reset of e2fsprogs/lib/ext2fs/fileio.c
 */


#ifdef DEBUG_EXE
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <errno.h>

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
		"Usage: debug_fileio [-i <start_blkno>] <filename> <path_to_find>\n");
}


static void dump_filebuf(const char *buf, int len)
{
	int rc, offset;

	offset = 0;
	while (offset < len) {
		rc = write(STDOUT_FILENO, buf + offset, len - offset);
		if (rc < 0) {
			fprintf(stderr, "Write error: %s\n",
				strerror(errno));
			return;
		} else if (rc) {
			offset += rc;
		} else {
			fprintf(stderr, "Wha?  Unexpected EOF\n");
			return;
		}
	}
	return;
}

extern int opterr, optind;
extern char *optarg;

int main(int argc, char *argv[])
{
	errcode_t ret;
	uint64_t blkno, result_blkno;
	int c, len;
	char *filename, *lookup_path, *buf;
	char *filebuf;
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
				"while looking up \"%s\" in inode %"PRIu64
			       	" on \"%s\"\n",
				lookup_name, blkno, filename);
			goto out_free;
		}

		blkno = result_blkno;

		for (; *p == '/'; p++);

		lookup_path = p;

		if (!*p)
			break;
	}

	if (ocfs2_check_directory(fs, blkno) != OCFS2_ET_NO_DIRECTORY) {
		com_err(argv[0], ret, "\"%s\" is not a file", filename);
		goto out_free;
	}

	ret = ocfs2_read_whole_file(fs, blkno, &filebuf, &len);
	if (ret) {
		com_err(argv[0], ret,
			"while reading file \"%s\" -- read %d bytes",
			filename, len);
		goto out_free_filebuf;
	}
	if (!len)
		fprintf(stderr, "boo!\n");

	dump_filebuf(filebuf, len);

out_free_filebuf:
	if (len)
		ocfs2_free(&filebuf);

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

