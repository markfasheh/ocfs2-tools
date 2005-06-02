/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * mark_journal_dirty.c
 *
 * Marks the journal for a given slot # as dirty.
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
 * Authors: Mark Fasheh
 */

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE

#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <inttypes.h>

#include "ocfs2.h"

static void print_usage(void)
{
	fprintf(stderr,	"Usage: mark_journal_dirty <device> <slot #>\n");
	fprintf(stderr,
		"Will mark the journal in <slot #> as needing recovery\n");
}

static errcode_t mark_journal(ocfs2_filesys *fs,
			      uint64_t blkno)
{
	errcode_t ret;
	char *buf = NULL;
	ocfs2_dinode *di;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret)
		goto out_free;

	di = (ocfs2_dinode *) buf;

	if (!(di->i_flags & OCFS2_JOURNAL_FL)) {
		fprintf(stderr, "Block %"PRIu64" is not a journal inode!\n",
			blkno);
		goto out_free;
	}

	di->id1.journal1.ij_flags |= OCFS2_JOURNAL_DIRTY_FL;

	ret = ocfs2_write_inode(fs, blkno, buf);
	if (ret)
		goto out_free;

out_free:
	ocfs2_free(&buf);

	return ret;
}

static unsigned int read_number(const char *num)
{
	unsigned long val;
	char *ptr;

	val = strtoul(num, &ptr, 0);
	if (!ptr || *ptr)
		return 0;

	return (unsigned int) val;
}

extern int opterr, optind;
extern char *optarg;

int main(int argc,
	 char *argv[])
{
	errcode_t ret;
	unsigned int slot;
	uint64_t journal_blkno;
	char *filename;
	ocfs2_filesys *fs;

	initialize_ocfs_error_table();

	if (argc < 3) {
		fprintf(stderr, "Missing parameters\n");
		print_usage();
		return 1;
	}
	filename = argv[1];

	slot = read_number(argv[2]);
	if (!slot) {
		fprintf(stderr, "invalid slot number\n");
		print_usage();
		return 1;
	}

	ret = ocfs2_open(filename, OCFS2_FLAG_RW, 0, 0, &fs);
	if (ret) {
		com_err(argv[0], ret,
			"while opening file \"%s\"", filename);
		goto out_close;
	}

	ret = ocfs2_lookup_system_inode(fs, JOURNAL_SYSTEM_INODE, slot,
					&journal_blkno);
	if (ret) {
		com_err(argv[0], ret,
			"while looking up journal in slot %u\n", slot);
		goto out_close;
	}

	fprintf(stdout, "Marking journal (block %"PRIu64") in slot %u\n",
		journal_blkno, slot);

	ret = mark_journal(fs, journal_blkno);
	if (ret) {
		com_err(argv[0], ret,
			"while marking journal dirty");
		goto out_close;
	}

out_close:
	ret = ocfs2_close(fs);
	if (ret) {
		com_err(argv[0], ret,
			"while closing file \"%s\"", filename);
	}

	return 0;
}

