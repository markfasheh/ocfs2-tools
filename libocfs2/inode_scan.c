/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * inode_scan.c
 *
 * Scan all inodes in an OCFS2 filesystem.  For the OCFS2 userspace
 * library.
 *
 * Copyright (C) 2004, 2011 Oracle.  All rights reserved.
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
 * Ideas taken from e2fsprogs/lib/ext2fs/inode_scan.c
 *   Copyright (C) 1993, 1994, 1995, 1996, 1997 Theodore Ts'o.
 */

#define _XOPEN_SOURCE 600  /* Triggers XOPEN2K in features.h */
#define _LARGEFILE64_SOURCE

#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "ocfs2/ocfs2.h"

#include "extent_map.h"

struct _ocfs2_inode_scan {
	ocfs2_filesys *fs;
	int num_inode_alloc;
	int next_inode_file;
	ocfs2_cached_inode *cur_inode_alloc;
	ocfs2_cached_inode **inode_alloc;
	struct ocfs2_chain_rec *cur_rec;
	int next_rec;
	struct ocfs2_group_desc *cur_desc;
	unsigned int count;
	uint64_t cur_blkno;
	char *group_buffer;
	char *cur_block;
	int buffer_blocks;
	int blocks_in_buffer;
	unsigned int blocks_left;
	uint64_t b_offset;		/* bit offset in the group bitmap. */
	uint16_t cur_discontig_rec;	/* Only valid in discontig group. */
};


/*
 * This function is called by fill_group_buffer when an alloc group has
 * been completely read.  It must not be called from the last group.
 * ocfs2_get_next_inode() should have detected that condition.
 */
static errcode_t get_next_group(ocfs2_inode_scan *scan)
{
	errcode_t ret;

	if (!scan->cur_desc) {
		if (scan->b_offset)
			abort();

		ret = ocfs2_malloc_block(scan->fs->fs_io,
					 &scan->cur_desc);
		if (ret)
			return ret;
	}

	if (scan->b_offset)
		scan->cur_blkno = scan->cur_desc->bg_next_group;

	/*
	 * scan->cur_blkno better be nonzero, either set by
	 * get_next_chain() or valid from bg_next_group
	 */ 
	if (!scan->cur_blkno)
		abort();

	ret = ocfs2_read_group_desc(scan->fs, scan->cur_blkno,
				    (char *)scan->cur_desc);
	if (ret)
		return (ret);

	if (scan->cur_desc->bg_blkno != scan->cur_blkno)
		return OCFS2_ET_CORRUPT_GROUP_DESC;

	/* Skip past group descriptor block */
	scan->cur_blkno++;
	scan->count++;
	scan->blocks_left--;
	scan->b_offset = 1;
	scan->cur_discontig_rec = 0;

	return 0;
}

/*
 * This function is called by fill_group_buffer when an alloc chain
 * has been completely read.  It must not be called  when the current
 * inode alloc file has been read in its entirety.  This condition
 * should have been detected by ocfs2_get_next_inode().
 */
static errcode_t get_next_chain(ocfs2_inode_scan *scan)
{
	struct ocfs2_dinode *di = scan->cur_inode_alloc->ci_inode;

	if (scan->next_rec == di->id2.i_chain.cl_next_free_rec) {
		if (!scan->next_rec) {
			/*
			 * The only way we can get here with next_rec
			 * == cl_next_free_rec == 0 is if
			 * bitmap1.i_total was non-zero.  But if i_total
			 * was non-zero and we have no chains, we're
			 * corrupt.
			 */
			return OCFS2_ET_CORRUPT_CHAIN;
		} else
			abort();
	}

	scan->cur_rec = &di->id2.i_chain.cl_recs[scan->next_rec];
	scan->next_rec++;
	scan->count = 0;
	scan->b_offset = 0;
	scan->cur_blkno = scan->cur_rec->c_blkno;

	return 0;
}

/*
 * Get the number of blocks we will read next.
 * In case of discontiguous group, we will set scan->cur_blkno in case
 * we need to read next extent record.
 */
static int get_next_read_blocks(ocfs2_inode_scan *scan)
{
	int num_blocks, rec_end;
	struct ocfs2_extent_rec *rec;

	if (!scan->cur_desc)
		abort();

	if (!scan->cur_desc->bg_list.l_next_free_rec) {
		/* Contiguous group. Just set num_blocks. */
		num_blocks = scan->cur_desc->bg_bits - scan->b_offset;
		goto out;
	}

	/* We shouldn't arrived here. */
	if (scan->cur_discontig_rec == scan->cur_desc->bg_list.l_next_free_rec)
		abort();

	rec = &scan->cur_desc->bg_list.l_recs[scan->cur_discontig_rec];
	rec_end = ocfs2_clusters_to_blocks(scan->fs,
					   rec->e_cpos + rec->e_leaf_clusters);
	if (rec_end < scan->b_offset)
		abort();
	else if (rec_end > scan->b_offset) {
		/* OK, we have more blocks to read in this rec. */
		num_blocks = rec_end - scan->b_offset;
	} else {
		/* We have to read the next rec now. */
		scan->cur_discontig_rec++;
		rec = &scan->cur_desc->bg_list.l_recs[scan->cur_discontig_rec];
		scan->cur_blkno = rec->e_blkno;
		num_blocks = ocfs2_clusters_to_blocks(scan->fs,
						      rec->e_leaf_clusters);
	}

out:
	if (num_blocks > scan->buffer_blocks)
		num_blocks = scan->buffer_blocks;

	return num_blocks;
}

/*
 * This function is called by ocfs2_get_next_inode when it needs
 * to read in more clusters from the current inode alloc file.  It
 * must not be called when the current inode alloc file has been read
 * in its entirety.  This condition is detected by
 * ocfs2_get_next_inode().
 */
static errcode_t fill_group_buffer(ocfs2_inode_scan *scan)
{
	errcode_t ret;
	int num_blocks;

	if (scan->cur_rec && (scan->count > scan->cur_rec->c_total))
		abort();

	if (scan->cur_rec && (scan->b_offset > scan->cur_desc->bg_bits))
		abort();

	if (!scan->cur_rec || (scan->count == scan->cur_rec->c_total)) {
		ret = get_next_chain(scan);
		if (ret)
			return ret;
	}
	
	if (!scan->b_offset || (scan->b_offset == scan->cur_desc->bg_bits)) {
		ret = get_next_group(scan);
		if (ret)
			return ret;
	}

	num_blocks = get_next_read_blocks(scan);

	ret = ocfs2_read_blocks(scan->fs, scan->cur_blkno, num_blocks,
				scan->group_buffer);
	if (ret)
		return ret;

	scan->b_offset += num_blocks;
	scan->blocks_in_buffer = num_blocks;
	scan->cur_block = scan->group_buffer;

	return 0;
}

/* This function sets the starting points for a given cached inode */
static int get_next_inode_alloc(ocfs2_inode_scan *scan)
{
	ocfs2_cached_inode *cinode = scan->cur_inode_alloc;

	if (cinode && scan->blocks_left)
		abort();

	do {
		if (scan->next_inode_file == scan->num_inode_alloc)
			return 1;  /* Out of files */

		scan->cur_inode_alloc =
			scan->inode_alloc[scan->next_inode_file];
		cinode = scan->cur_inode_alloc;

		scan->next_inode_file++;
	} while (!cinode->ci_inode->id1.bitmap1.i_total);

	scan->next_rec = 0;
	scan->count = 0;
	scan->cur_blkno = 0;
	scan->cur_rec = NULL;
	scan->blocks_left =
		cinode->ci_inode->id1.bitmap1.i_total;

	return 0;
}

uint64_t ocfs2_get_max_inode_count(ocfs2_inode_scan *scan)
{
	struct ocfs2_dinode *di = NULL;
	uint64_t count = 0;
	int i;

	if (!scan || !scan->num_inode_alloc)
		return 0;

	for (i = 0; i < scan->num_inode_alloc; i++) {
		if (scan->inode_alloc[i])
			di = scan->inode_alloc[i]->ci_inode;
		if (!di)
			continue;
		count += ocfs2_clusters_to_blocks(scan->fs, di->i_clusters);
		di = NULL;
	}

	return count;
}

errcode_t ocfs2_get_next_inode(ocfs2_inode_scan *scan,
			       uint64_t *blkno, char *inode)
{
	errcode_t ret;

	if (!scan->blocks_left) {
		if (scan->blocks_in_buffer)
			abort();

		if (get_next_inode_alloc(scan)) {
			*blkno = 0;
			return 0;
		}
	}

	if (!scan->blocks_in_buffer) {
		ret = fill_group_buffer(scan);
		if (ret)
			return ret;
	}
	
	/* the caller swap after verifying the inode's signature */
	memcpy(inode, scan->cur_block, scan->fs->fs_blocksize);

	scan->cur_block += scan->fs->fs_blocksize;
	scan->blocks_in_buffer--;
	scan->blocks_left--;
	*blkno = scan->cur_blkno;
	scan->cur_blkno++;
	scan->count++;

	return 0;
}

errcode_t ocfs2_open_inode_scan(ocfs2_filesys *fs,
				ocfs2_inode_scan **ret_scan)
{
	ocfs2_inode_scan *scan;
	uint64_t blkno;
	errcode_t ret;
	int i, slot_num;

	ret = ocfs2_malloc0(sizeof(struct _ocfs2_inode_scan), &scan);
	if (ret)
		return ret;

	scan->fs = fs;

	/* One inode alloc per slot, one global inode alloc */
	scan->num_inode_alloc =
		OCFS2_RAW_SB(fs->fs_super)->s_max_slots + 1;
	ret = ocfs2_malloc0(sizeof(ocfs2_cached_inode *) *
			    scan->num_inode_alloc,
			    &scan->inode_alloc);
	if (ret)
		goto out_scan;

	/*
	 * Ideally the buffer size should be one cpg. But finding that value
	 * is not worth the effort. Instead we default to 4MB, which is a
	 * typical value in most ocfs2 file systems.
	 */
#define OPEN_SCAN_BUFFER_SIZE	(4 * 1024 * 1024)
	scan->buffer_blocks = OPEN_SCAN_BUFFER_SIZE / fs->fs_blocksize;

	ret = ocfs2_malloc_blocks(fs->fs_io, scan->buffer_blocks,
				  &scan->group_buffer);
	if (ret)
		goto out_inode_files;

	ret = ocfs2_lookup_system_inode(fs,
					GLOBAL_INODE_ALLOC_SYSTEM_INODE,
					0, &blkno);
	if (ret)
		goto out_cleanup;

	ret = ocfs2_read_cached_inode(fs, blkno, &scan->inode_alloc[0]);
	if (ret)
		goto out_cleanup;

	for (i = 1; i < scan->num_inode_alloc; i++) {
		slot_num = i - 1;
		ret = ocfs2_lookup_system_inode(fs,
						INODE_ALLOC_SYSTEM_INODE,
						slot_num,
						&blkno);
		if (ret)
			goto out_cleanup;

		ret = ocfs2_read_cached_inode(fs, blkno,
					      &scan->inode_alloc[i]);
		if (ret)
			goto out_cleanup;
	}

	/*
	 * FIXME: Should this pre-read all the group descriptors like
	 * the old code read all the extent maps?
	 */

	*ret_scan = scan;

	return 0;

out_inode_files:
	ocfs2_free(&scan->inode_alloc);

out_scan:
	ocfs2_free(&scan);

out_cleanup:
	if (scan)
		ocfs2_close_inode_scan(scan);

	return ret;
}

void ocfs2_close_inode_scan(ocfs2_inode_scan *scan)
{
	int i;

	if (!scan)
		return;

	for (i = 0; i < scan->num_inode_alloc; i++) {
		if (scan->inode_alloc[i]) {
			ocfs2_free_cached_inode(scan->fs,
						scan->inode_alloc[i]);
		}
	}

	ocfs2_free(&scan->group_buffer);
	ocfs2_free(&scan->cur_desc);
	ocfs2_free(&scan->inode_alloc);
	ocfs2_free(&scan);

	return;
}



#ifdef DEBUG_EXE
#include <string.h>
#include <stdlib.h>

static void print_usage(void)
{
	fprintf(stderr,
		"Usage: debug_inode_scan <filename>\n");
}

extern int opterr, optind;
extern char *optarg;

int main(int argc, char *argv[])
{
	errcode_t ret;
	int done;
	uint64_t blkno;
	char *filename, *buf;
	ocfs2_filesys *fs;
	struct ocfs2_dinode *di;
	ocfs2_inode_scan *scan;

	initialize_ocfs_error_table();

	if (argc < 2) {
		fprintf(stderr, "Missing filename\n");
		print_usage();
		return 1;
	}
	filename = argv[1];

	ret = ocfs2_open(filename, OCFS2_FLAG_RO|OCFS2_FLAG_BUFFERED, 0, 0, &fs);
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

	di = (struct ocfs2_dinode *)buf;

	ret = ocfs2_open_inode_scan(fs, &scan);
	if (ret) {
		com_err(argv[0], ret,
			"while opening inode scan");
		goto out_free;
	}

	done = 0;
	while (!done) {
		ret = ocfs2_get_next_inode(scan, &blkno, buf);
		if (ret) {
			com_err(argv[0], ret,
				"while getting next inode");
			goto out_close_scan;
		}
		if (blkno) {
			if (memcmp(di->i_signature,
				   OCFS2_INODE_SIGNATURE,
				   strlen(OCFS2_INODE_SIGNATURE)))
				continue;

			if (!(di->i_flags & OCFS2_VALID_FL))
				continue;

			fprintf(stdout, 
				"%snode %"PRIu64" with size %"PRIu64"\n",
				(di->i_flags & OCFS2_SYSTEM_FL) ?
				"System i" : "I",
				blkno, di->i_size);
		}
		else
			done = 1;
	}

out_close_scan:
	ocfs2_close_inode_scan(scan);

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
