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
 * Portions of the code from e2fsprogs/lib/ext2fs/mkjournal.c
 *   Copyright (C) 2000 Theodore Ts'o.
 */

#define _XOPEN_SOURCE 600  /* Triggers XOPEN2K in features.h */
#define _LARGEFILE64_SOURCE

#include <string.h>
#include <netinet/in.h>

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

errcode_t ocfs2_read_journal_superblock(ocfs2_filesys *fs, uint64_t blkno,
					char *jsb_buf)
{
	errcode_t ret;
	char *blk;
	journal_superblock_t *disk, *jsb;
	
	if ((blkno < OCFS2_SUPER_BLOCK_BLKNO) ||
	    (blkno > fs->fs_blocks))
		return OCFS2_ET_BAD_BLKNO;

	ret = ocfs2_malloc_block(fs->fs_io, &blk);
	if (ret)
		return ret;

	ret = io_read_block(fs->fs_io, blkno, 1, blk);
	if (ret)
		goto out;

	disk = (journal_superblock_t *)blk;
	jsb = (journal_superblock_t *)jsb_buf;

	if (disk->s_header.h_magic != htonl(JFS_MAGIC_NUMBER)) {
		ret = OCFS2_ET_BAD_JOURNAL_SUPERBLOCK_MAGIC;
		goto out;
	}

	memcpy(jsb_buf, blk, fs->fs_blocksize);

	/* XXX incomplete */
	jsb->s_header.h_magic = be32_to_cpu(disk->s_header.h_magic);
	jsb->s_header.h_blocktype = be32_to_cpu(disk->s_header.h_blocktype);

	jsb->s_blocksize = be32_to_cpu(disk->s_blocksize);
	jsb->s_maxlen = be32_to_cpu(disk->s_maxlen);
	jsb->s_first = be32_to_cpu(disk->s_first);
	jsb->s_start = be32_to_cpu(disk->s_start);
	jsb->s_sequence = be32_to_cpu(disk->s_sequence);
	jsb->s_errno = be32_to_cpu(disk->s_errno);

	ret = 0;
out:
	ocfs2_free(&blk);

	return ret;
}

errcode_t ocfs2_write_journal_superblock(ocfs2_filesys *fs, uint64_t blkno,
					 char *jsb_buf)
{
	errcode_t ret;
	char *blk;
	journal_superblock_t *disk, *jsb;
	
	if (!(fs->fs_flags & OCFS2_FLAG_RW))
		return OCFS2_ET_RO_FILESYS;

	if ((blkno < OCFS2_SUPER_BLOCK_BLKNO) ||
	    (blkno > fs->fs_blocks))
		return OCFS2_ET_BAD_BLKNO;

	ret = ocfs2_malloc_block(fs->fs_io, &blk);
	if (ret)
		return ret;

	disk = (journal_superblock_t *)blk;
	jsb = (journal_superblock_t *)jsb_buf;

	memcpy(blk, jsb_buf, fs->fs_blocksize);

	/* XXX incomplete */
	disk->s_header.h_magic = cpu_to_be32(jsb->s_header.h_magic);
	disk->s_header.h_blocktype = cpu_to_be32(jsb->s_header.h_blocktype);

	disk->s_blocksize = cpu_to_be32(jsb->s_blocksize);
	disk->s_maxlen = cpu_to_be32(jsb->s_maxlen);
	disk->s_first = cpu_to_be32(jsb->s_first);
	disk->s_start = cpu_to_be32(jsb->s_start);
	disk->s_sequence = cpu_to_be32(jsb->s_sequence);
	disk->s_errno = cpu_to_be32(jsb->s_errno);

	ret = io_write_block(fs->fs_io, blkno, 1, blk);
	if (ret)
		goto out;

	fs->fs_flags |= OCFS2_FLAG_CHANGED;
	ret = 0;
out:
	ocfs2_free(&blk);

	return ret;
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

#include <stdio.h>
int main(int argc, char *argv[])
{
	fprintf(stdout, "Does nothing for now\n");
	return 0;
}
#endif  /* DEBUG_EXE */
