/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * inode_scan.c
 *
 * Scan all inodes in an OCFS2 filesystem.  For the OCFS2 userspace
 * library.
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
 * Ideas taken from e2fsprogs/lib/ext2fs/inode_scan.c
 *   Copyright (C) 1993, 1994, 1995, 1996, 1997 Theodore Ts'o.
 */

#define _XOPEN_SOURCE 600  /* Triggers XOPEN2K in features.h */
#define _LARGEFILE64_SOURCE

#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "ocfs2.h"

#include "extent_map.h"

struct _ocfs2_inode_scan {
	ocfs2_filesys *fs;
	int num_inode_alloc;
	int next_inode_file;
	ocfs2_cached_inode *cur_inode_alloc;
	ocfs2_cached_inode **inode_alloc;
	ocfs2_extent_map_entry *cur_entry;
	uint64_t cur_blkno;
	char *extent_buffer;
	char *cur_block;
	uint32_t buffer_clusters;
	uint64_t blocks_in_buffer;
	uint64_t blocks_left;
	uint32_t cpos;
	int c_to_b_bits;
};


/*
 * This function is called by fill_extent_buffer when an extent has
 * been completely read.  It must not be called from the last extent.
 * ocfs2_get_next_inode() should have detected that condition.
 */
static errcode_t get_next_extent(ocfs2_inode_scan *scan)
{
	struct list_head *pos;
	ocfs2_extent_map *em;

	em = scan->cur_inode_alloc->ci_map;
	pos = scan->cur_entry->e_list.next;
	if (pos == &em->em_extents)
		abort();
	scan->cur_entry = list_entry(pos, ocfs2_extent_map_entry,
				     e_list);
	if (scan->cur_entry->e_rec.e_cpos != scan->cpos)
		return OCFS2_ET_CORRUPT_EXTENT_BLOCK;

	scan->cur_blkno = scan->cur_entry->e_rec.e_blkno;

	return 0;
}

/*
 * This function is called by ocfs2_get_next_inode when it needs
 * to read in more clusters from the current inode alloc file.  It
 * must not be called when the current inode alloc file has been read
 * in its entirety.  This condition is detected by
 * ocfs2_get_next_inode().
 */
static errcode_t fill_extent_buffer(ocfs2_inode_scan *scan)
{
	errcode_t ret;
	uint64_t num_blocks;
	uint32_t num_clusters;

	if (scan->cpos > (scan->cur_entry->e_rec.e_cpos +
			  scan->cur_entry->e_rec.e_clusters))
		abort();

	if (scan->cpos == (scan->cur_entry->e_rec.e_cpos +
			   scan->cur_entry->e_rec.e_clusters)) {
		ret = get_next_extent(scan);
		if (ret)
			return ret;
	}

	num_clusters = scan->cur_entry->e_rec.e_clusters -
		(scan->cpos - scan->cur_entry->e_rec.e_cpos);

	if (num_clusters > scan->buffer_clusters)
		num_clusters = scan->buffer_clusters;

	num_blocks = (uint64_t)num_clusters << scan->c_to_b_bits;
	ret = io_read_block(scan->fs->fs_io,
			    scan->cur_blkno,
			    num_blocks,
			    scan->extent_buffer);
	if (ret)
		return ret;

	scan->cpos += num_clusters;
	scan->blocks_in_buffer = num_blocks;
	scan->cur_block = scan->extent_buffer;

	if (!scan->cur_blkno) {
		/*
		 * inode_alloc[0] is the global inode allocator.  It
		 * contains the first extent of the filesystem,
		 * including block zero.  If this is that allocator
		 * skip it past the superblock inode.
		 *
		 * (next_inode_file is one because we just incremented
		 *  it.)
		 */
		if (scan->next_inode_file == 1) {
			scan->cur_blkno = OCFS2_SUPER_BLOCK_BLKNO + 1;
			scan->cur_block += (scan->cur_blkno *
					    scan->fs->fs_blocksize);
			scan->blocks_left -= scan->cur_blkno;
			scan->blocks_in_buffer -= scan->cur_blkno;
		}
		/* FIXME: error otherwise */
	}

	return 0;
}

/* This function sets the starting points for a given cached inode */
static int get_next_inode_alloc(ocfs2_inode_scan *scan)
{
	ocfs2_cached_inode *cinode = scan->cur_inode_alloc;

	if (cinode && (scan->cpos != cinode->ci_inode->i_clusters))
		abort();

	do {
		if (scan->next_inode_file == scan->num_inode_alloc)
			return 1;  /* Out of files */

		scan->cur_inode_alloc =
			scan->inode_alloc[scan->next_inode_file];
		cinode = scan->cur_inode_alloc;

		scan->next_inode_file++;
	} while (list_empty(&cinode->ci_map->em_extents));

	scan->cur_entry = list_entry(cinode->ci_map->em_extents.next,
				     ocfs2_extent_map_entry, e_list);
	scan->cpos = scan->cur_entry->e_rec.e_cpos;
	/* could check cpos == 0 */
	scan->cur_blkno = scan->cur_entry->e_rec.e_blkno;
	scan->blocks_left =
		cinode->ci_inode->i_clusters << scan->c_to_b_bits;

	return 0;
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
		ret = fill_extent_buffer(scan);
		if (ret)
			return ret;
	}
	
	/* Should swap the inode */
	memcpy(inode, scan->cur_block, scan->fs->fs_blocksize);

	scan->cur_block += scan->fs->fs_blocksize;
	scan->blocks_in_buffer--;
	scan->blocks_left--;
	*blkno = scan->cur_blkno;
	scan->cur_blkno++;

	return 0;
}

errcode_t ocfs2_open_inode_scan(ocfs2_filesys *fs,
				ocfs2_inode_scan **ret_scan)
{
	ocfs2_inode_scan *scan;
	uint64_t blkno;
	errcode_t ret;
	int i, node_num;

	ret = ocfs2_malloc0(sizeof(struct _ocfs2_inode_scan), &scan);
	if (ret)
		return ret;

	scan->fs = fs;
	scan->c_to_b_bits =
		OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits -
		OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;

	/* One inode alloc per node, one global inode alloc */
	scan->num_inode_alloc =
		OCFS2_RAW_SB(fs->fs_super)->s_max_nodes + 1;
	ret = ocfs2_malloc0(sizeof(ocfs2_cached_inode *) *
			    scan->num_inode_alloc,
			    &scan->inode_alloc);
	if (ret)
		goto out_scan;

	/* Minimum 8 inodes in the buffer */
	if ((fs->fs_clustersize / fs->fs_blocksize) < 8) {
		scan->buffer_clusters =
			((8 * fs->fs_blocksize) +
			 (fs->fs_clustersize - 1)) /
			fs->fs_clustersize;
	} else {
		scan->buffer_clusters = 1;
	}
	ret = ocfs2_malloc(sizeof(char) * scan->buffer_clusters *
			   fs->fs_clustersize, &scan->extent_buffer);
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
		node_num = i - 1;
		ret = ocfs2_lookup_system_inode(fs,
						INODE_ALLOC_SYSTEM_INODE,
						node_num,
						&blkno);
		if (ret)
			goto out_cleanup;

		ret = ocfs2_read_cached_inode(fs, blkno,
					      &scan->inode_alloc[i]);
		if (ret)
			goto out_cleanup;
	}

	for (i = 0; i < scan->num_inode_alloc; i++) {
		ret = ocfs2_load_extent_map(fs, scan->inode_alloc[i]);
		if (ret)
			goto out_cleanup;
	}

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

	ocfs2_free(&scan->extent_buffer);
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
	ocfs2_dinode *di;
	ocfs2_inode_scan *scan;

	initialize_ocfs_error_table();

	if (argc < 2) {
		fprintf(stderr, "Missing filename\n");
		print_usage();
		return 1;
	}
	filename = argv[1];

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
