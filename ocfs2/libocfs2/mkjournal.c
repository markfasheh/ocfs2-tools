/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * mkjournal.c
 *
 * Journal creation for the OCFS2 userspace library.
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
 * Portions of the code from e2fsprogs/lib/ext2fs/mkjournal.c
 *   Copyright (C) 2000 Theodore Ts'o.
 */

#define _XOPEN_SOURCE 600  /* Triggers XOPEN2K in features.h */
#define _LARGEFILE64_SOURCE

#include <string.h>

#include "ocfs2.h"

/* jfs_compat.h defines these */
#undef cpu_to_be32
#undef be32_to_cpu
#include "jfs_user.h"


/*
 * This function automatically sets up the journal superblock and
 * returns it as an allocated block.
 */
errcode_t ocfs2_create_journal_superblock(ocfs2_filesys *fs,
					  uint32_t size, int flags,
					  char  **ret_jsb)
{
	errcode_t		retval;
	journal_superblock_t	*jsb;

	if (size < 1024)
		return OCFS2_ET_JOURNAL_TOO_SMALL;

	if ((retval = ocfs2_malloc_block(fs->fs_io, &jsb)))
		return retval;

	memset(jsb, 0, fs->fs_blocksize);

	jsb->s_header.h_magic = htonl(JFS_MAGIC_NUMBER);
	jsb->s_header.h_blocktype = htonl(JFS_SUPERBLOCK_V2);
	jsb->s_blocksize = htonl(fs->fs_blocksize);
	jsb->s_maxlen = htonl(size);
	jsb->s_nr_users = htonl(1);
	if (fs->fs_blocksize == 512)
		jsb->s_first = htonl(2);
	else
		jsb->s_first = htonl(1);
	jsb->s_sequence = htonl(1);
	memcpy(jsb->s_uuid, OCFS2_RAW_SB(fs->fs_super)->s_uuid,
	       sizeof(OCFS2_RAW_SB(fs->fs_super)->s_uuid));

#if 0 /* Someday */
	/*
	 * If we're creating an external journal device, we need to
	 * adjust these fields.
	 */
	if (fs->super->s_feature_incompat &
	    EXT3_FEATURE_INCOMPAT_JOURNAL_DEV) {
		jsb->s_nr_users = 0;
		if (fs->blocksize == 1024)
			jsb->s_first = htonl(3);
		else
			jsb->s_first = htonl(2);
	}
#endif

	*ret_jsb = (char *) jsb;
	return 0;
}

#ifdef DEBUG_EXE
#if 0
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
		"Usage: mkjournal <filename> <inode_num>\n");
}

extern int opterr, optind;
extern char *optarg;

int main(int argc, char *argv[])
{
	errcode_t ret;
	uint64_t blkno;
	char *filename, *buf;
	ocfs2_filesys *fs;
	ocfs2_dinode *di;

	blkno = OCFS2_SUPER_BLOCK_BLKNO;

	initialize_ocfs_error_table();

	if (argc < 2) {
		fprintf(stderr, "Missing filename\n");
		print_usage();
		return 1;
	}
	filename = argv[1];

	if (argc > 2) {
		blkno = read_number(argv[2]);
		if (blkno < OCFS2_SUPER_BLOCK_BLKNO) {
			fprintf(stderr, "Invalid blockno: %s\n",
				blkno);
			print_usage();
			return 1;
		}
	}

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

	di = (ocfs2_dinode *)buf;

	ret = ocfs2_read_inode(fs, blkno, di);
	if (ret) {
		com_err(argv[0], ret,
			"while reading inode %llu", blkno);
		goto out_free;
	}

	fprintf(stdout, "OCFS2 inode %llu on \"%s\"\n", blkno,
		filename);


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
#endif
#endif  /* DEBUG_EXE */
