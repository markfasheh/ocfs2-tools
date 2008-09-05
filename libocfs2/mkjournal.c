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

#include "ocfs2/byteorder.h"
#include "ocfs2/ocfs2.h"

size_t ocfs2_journal_tag_bytes(journal_superblock_t *jsb)
{
	if (JBD2_HAS_INCOMPAT_FEATURE(jsb, JBD2_FEATURE_INCOMPAT_64BIT))
		return JBD2_TAG_SIZE64;
	else
		return JBD2_TAG_SIZE32;
}

uint64_t ocfs2_journal_tag_block(journal_block_tag_t *tag, size_t tag_bytes)
{
	uint64_t blockno = be32_to_cpu(tag->t_blocknr);
	if (tag_bytes > JBD2_TAG_SIZE32)
		blockno |= (uint64_t)be32_to_cpu(tag->t_blocknr_high) << 32;
	return blockno;
}

void ocfs2_swap_journal_superblock(journal_superblock_t *jsb)
{
	if (cpu_is_big_endian)
		return;

	jsb->s_header.h_magic     = bswap_32(jsb->s_header.h_magic);
	jsb->s_header.h_blocktype = bswap_32(jsb->s_header.h_blocktype);
	jsb->s_header.h_sequence  = bswap_32(jsb->s_header.h_sequence);

	jsb->s_blocksize         = bswap_32(jsb->s_blocksize);
	jsb->s_maxlen            = bswap_32(jsb->s_maxlen);
	jsb->s_first             = bswap_32(jsb->s_first);
	jsb->s_sequence          = bswap_32(jsb->s_sequence);
	jsb->s_start             = bswap_32(jsb->s_start);
	jsb->s_errno             = bswap_32(jsb->s_errno);
	jsb->s_feature_compat    = bswap_32(jsb->s_feature_compat);
	jsb->s_feature_incompat  = bswap_32(jsb->s_feature_incompat);
	jsb->s_feature_ro_compat = bswap_32(jsb->s_feature_ro_compat);
	jsb->s_nr_users          = bswap_32(jsb->s_nr_users);
	jsb->s_dynsuper          = bswap_32(jsb->s_dynsuper);
	jsb->s_max_transaction   = bswap_32(jsb->s_max_transaction);
	jsb->s_max_trans_data    = bswap_32(jsb->s_max_trans_data);
}

errcode_t ocfs2_init_journal_superblock(ocfs2_filesys *fs, char *buf,
					int buflen, uint32_t jrnl_size_in_blks)
{
	journal_superblock_t *jsb = (journal_superblock_t *)buf;

	if (buflen < fs->fs_blocksize)
		return OCFS2_ET_INTERNAL_FAILURE;

	if (jrnl_size_in_blks < 1024)
		return OCFS2_ET_JOURNAL_TOO_SMALL;

	memset(buf, 0, buflen);
	jsb->s_header.h_magic     = JBD2_MAGIC_NUMBER;
	jsb->s_header.h_blocktype = JBD2_SUPERBLOCK_V2;

	jsb->s_blocksize = fs->fs_blocksize;
	jsb->s_maxlen    = jrnl_size_in_blks;

	if (fs->fs_blocksize == 512)
		jsb->s_first = 2;
	else
		jsb->s_first = 1;

	jsb->s_start    = 1;
	jsb->s_sequence = 1;
	jsb->s_errno    = 0;
	jsb->s_nr_users = 1;

	memcpy(jsb->s_uuid, OCFS2_RAW_SB(fs->fs_super)->s_uuid,
	       sizeof(jsb->s_uuid));

	return 0;
}

/*
 * This function automatically sets up the journal superblock and
 * returns it as an allocated block.
 */
static errcode_t ocfs2_create_journal_superblock(ocfs2_filesys *fs,
						 uint32_t size,
						 char  **ret_jsb)
{
	errcode_t retval;
	char *buf = NULL;

	*ret_jsb = NULL;

	if ((retval = ocfs2_malloc_block(fs->fs_io, &buf)))
		goto bail;

	retval = ocfs2_init_journal_superblock(fs, buf, fs->fs_blocksize, size);
	if (retval)
		goto bail;

#if 0 /* Someday */
	/*
	 * If we're creating an external journal device, we need to
	 * adjust these fields.
	 */
	if (fs->super->s_feature_incompat &
	    EXT3_FEATURE_INCOMPAT_JOURNAL_DEV) {
		jsb->s_nr_users = 0;
		if (fs->blocksize == 1024)
			jsb->s_first = 3;
		else
			jsb->s_first = 2;
	}
#endif

	*ret_jsb = buf;

bail:
	if (retval && buf)
		ocfs2_free(&buf);
	return retval;
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

	ret = ocfs2_read_blocks(fs, blkno, 1, blk);
	if (ret)
		goto out;

	disk = (journal_superblock_t *)blk;
	jsb = (journal_superblock_t *)jsb_buf;

	if (disk->s_header.h_magic != htonl(JBD2_MAGIC_NUMBER)) {
		ret = OCFS2_ET_BAD_JOURNAL_SUPERBLOCK_MAGIC;
		goto out;
	}

	memcpy(jsb_buf, blk, fs->fs_blocksize);
	ocfs2_swap_journal_superblock(jsb);

	if (JBD2_HAS_INCOMPAT_FEATURE(jsb, ~JBD2_KNOWN_INCOMPAT_FEATURES)) {
		ret = OCFS2_ET_UNSUPP_FEATURE;
		goto out;
	}

	if (JBD2_HAS_RO_COMPAT_FEATURE(jsb, ~JBD2_KNOWN_ROCOMPAT_FEATURES)) {
		ret = OCFS2_ET_RO_UNSUPP_FEATURE;
		goto out;
	}

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
	ocfs2_swap_journal_superblock(disk);

	ret = io_write_block(fs->fs_io, blkno, 1, blk);
	if (ret)
		goto out;

	fs->fs_flags |= OCFS2_FLAG_CHANGED;
	ret = 0;
out:
	ocfs2_free(&blk);

	return ret;
}

static errcode_t ocfs2_format_journal(ocfs2_filesys *fs,
				      ocfs2_cached_inode *ci)
{
	errcode_t ret = 0;
	char *buf = NULL, *jsb_buf = NULL;
	int bs_bits = OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;
	uint64_t offset = 0;
	uint32_t wrote, count, jrnl_blocks;

#define BUFLEN	1048576
	ret = ocfs2_malloc_blocks(fs->fs_io, (BUFLEN >> bs_bits), &buf);
	if (ret)
		goto out;
	memset(buf, 0, BUFLEN);

	count = (uint32_t) ci->ci_inode->i_size;
	while (count) {
		ret = ocfs2_file_write(ci, buf, ocfs2_min((uint32_t) BUFLEN, count),
				       offset, &wrote);
		if (ret)
			goto out;
		offset += wrote;
		count -= wrote;
	}

	jrnl_blocks = ocfs2_clusters_to_blocks(fs, ci->ci_inode->i_clusters);
	ret = ocfs2_create_journal_superblock(fs, jrnl_blocks, &jsb_buf);
	if (ret)
		goto out;

	/* re-use offset here for 1st journal block. */
	ret = ocfs2_extent_map_get_blocks(ci, 0, 1, &offset, NULL, NULL);
	if (ret)
		goto out;

	ret = ocfs2_write_journal_superblock(fs, offset, jsb_buf);
out:
	if (buf)
		ocfs2_free(&buf);
	if (jsb_buf)
		ocfs2_free(&jsb_buf);

	return ret;
}

errcode_t ocfs2_make_journal(ocfs2_filesys *fs, uint64_t blkno,
			     uint32_t clusters)
{
	errcode_t ret = 0;
	ocfs2_cached_inode *ci = NULL;
	struct ocfs2_dinode *di;

	if ((clusters << OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits) <
	    OCFS2_MIN_JOURNAL_SIZE) {
		ret = OCFS2_ET_JOURNAL_TOO_SMALL;
		goto out;
	}

	ret = ocfs2_read_cached_inode(fs, blkno, &ci);
	if (ret)
		goto out;

	/* verify it is a journal file */
	if (!(ci->ci_inode->i_flags & OCFS2_VALID_FL) ||
	    !(ci->ci_inode->i_flags & OCFS2_SYSTEM_FL) ||
	    !(ci->ci_inode->i_flags & OCFS2_JOURNAL_FL)) {
		ret = OCFS2_ET_INTERNAL_FAILURE;
		goto out;
	}

	di = ci->ci_inode;
	if (clusters > di->i_clusters) {
		ret = ocfs2_extend_allocation(fs, blkno,
					      (clusters - di->i_clusters));
		if (ret)
			goto out;

		/* We don't cache in the library right now, so any
		 * work done in extend_allocation won't be reflected
		 * in our now stale copy. */
		ocfs2_free_cached_inode(fs, ci);
		ret = ocfs2_read_cached_inode(fs, blkno, &ci);
		if (ret) {
			ci = NULL;
			goto out;
		}
		di = ci->ci_inode;

		di->i_size = di->i_clusters <<
			OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits;
		di->i_mtime = time(NULL);

		ret = ocfs2_write_inode(fs, blkno, (char *)di);
		if (ret)
			goto out;
	} else if (clusters < di->i_clusters) {
		uint64_t new_size = clusters <<
			   OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits;
		ret = ocfs2_truncate(fs, blkno, new_size);
		if (ret)
			goto out;

		ocfs2_free_cached_inode(fs, ci);
		ret = ocfs2_read_cached_inode(fs, blkno, &ci);
		if (ret) {
			ci = NULL;
			goto out;
		}
	}

	ret = ocfs2_format_journal(fs, ci);
out:
	if (ci)
		ocfs2_free_cached_inode(fs, ci);

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
