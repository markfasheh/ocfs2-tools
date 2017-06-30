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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301 USA.
 *
 * Authors: Mark Fasheh
 */

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE

#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <inttypes.h>

#include "ocfs2/byteorder.h"
#include "ocfs2/ocfs2.h"

static int debug = 0;

static void print_usage(void)
{
	fprintf(stderr,
		"Usage: mark_journal_dirty <device> <node #> <slot #>\n");
	fprintf(stderr, "Will insert node <node #> into slot <slot #> and "
		"mark the journal in <slot #> as needing recovery.\n");
}

static errcode_t mark_journal(ocfs2_filesys *fs,
			      uint64_t blkno)
{
	errcode_t ret;
	char *buf = NULL;
	struct ocfs2_dinode *di;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret)
		goto out_free;

	di = (struct ocfs2_dinode *) buf;

	if (!(di->i_flags & OCFS2_JOURNAL_FL)) {
		ret = OCFS2_ET_INVALID_ARGUMENT;
		fprintf(stderr, "Block %"PRIu64" is not a journal inode!\n",
			blkno);
		goto out_free;
	}

	di->id1.journal1.ij_flags |= OCFS2_JOURNAL_DIRTY_FL;

	ret = ocfs2_write_inode(fs, blkno, buf);

out_free:
	ocfs2_free(&buf);

	return ret;
}

static errcode_t write_back_slot_map(ocfs2_filesys *fs,
				     uint64_t slot_map_blkno,
				     char *slots_buf)
{
	errcode_t ret;
	char *di_buf = NULL;
	struct ocfs2_dinode *di;
	uint64_t block;
	struct ocfs2_extent_list *el;

	ret = ocfs2_malloc_block(fs->fs_io, &di_buf);
	if (ret)
		return ret;

	ret = ocfs2_read_inode(fs, slot_map_blkno, di_buf);
	if (ret)
		goto out_free;

	di = (struct ocfs2_dinode *) di_buf;
	el = &di->id2.i_list;
	block = el->l_recs[0].e_blkno;
	if (el->l_tree_depth || !block) {
		ret = OCFS2_ET_INVALID_EXTENT_LOOKUP;
		goto out_free;
	}

	if (debug)
		fprintf(stdout, "Write back slot data at block %"PRIu64"\n",
			block);

	ret = io_write_block(fs->fs_io, block, 1, slots_buf);

out_free:
	ocfs2_free(&di_buf);

	return ret;
}

static errcode_t insert_node_into_slot(ocfs2_filesys *fs,
				       int node,
				       int slot)
{
	errcode_t ret;
	int i, num_slots;
	char *buf = NULL;
	uint64_t slot_map_blkno;
	int len = fs->fs_blocksize;
	int16_t *slots;

	ret = ocfs2_lookup_system_inode(fs, SLOT_MAP_SYSTEM_INODE, -1,
					&slot_map_blkno);
	if (ret)
		goto out;

	ret = ocfs2_read_whole_file(fs, slot_map_blkno, &buf, &len);
	if (ret)
		goto out;

	num_slots = fs->fs_super->id2.i_super.s_max_slots;

	if (debug)
		fprintf(stdout, "%d slots on this device\n", num_slots);

	if (len < fs->fs_blocksize) {
		ret = OCFS2_ET_SHORT_READ;
		goto out;
	}

	slots = (int16_t *) buf;

	/* We'll allow the caller to put a different node in a
	 * currently filled slot, but we must watch out for duplicate
	 * nodes in slots. */
	for(i = 0; i < num_slots; i++) {
		if (le16_to_cpu(slots[i]) == node) {
			ret = OCFS2_ET_INTERNAL_FAILURE;
			fprintf(stdout, "node %d already found in slot_map "
				"slot %d\n", node, i);
			goto out;
		}
	}

	slots[slot] = cpu_to_le16(node);

	ret = write_back_slot_map(fs, slot_map_blkno, buf);

out:
	return ret;
}

static unsigned int read_number(const char *num)
{
	unsigned long val;
	char *ptr;

	val = strtoul(num, &ptr, 0);
	if (!ptr || *ptr)
		return -1;

	return (unsigned int) val;
}

extern int opterr, optind;
extern char *optarg;

int main(int argc,
	 char *argv[])
{
	errcode_t ret;
	int slot, node;
	uint64_t journal_blkno;
	char *filename;
	ocfs2_filesys *fs;

	initialize_ocfs_error_table();

	if (argc < 4) {
		fprintf(stderr, "Missing parameters\n");
		print_usage();
		return 1;
	}
	filename = argv[1];

	node = read_number(argv[2]);
	if (node == -1) {
		fprintf(stderr, "invalid node number\n");
		print_usage();
		return 1;
	}

	slot = read_number(argv[3]);
	if (slot == -1) {
		fprintf(stderr, "invalid slot number\n");
		print_usage();
		return 1;
	}

	ret = ocfs2_open(filename, OCFS2_FLAG_RW, 0, 0, &fs);
	if (ret) {
		com_err(argv[0], ret, "while opening file \"%s\"", filename);
		goto out_close;
	}

	if (debug)
		fprintf(stdout, "Inserting node %d into slot %d\n", node,
			slot);

	ret = insert_node_into_slot(fs, node, slot);
	if (ret) {
		com_err(argv[0], ret, "while inserting node\n");
		goto out_close;
	}

	ret = ocfs2_lookup_system_inode(fs, JOURNAL_SYSTEM_INODE, slot,
					&journal_blkno);
	if (ret) {
		com_err(argv[0], ret,
			"while looking up journal in slot %d\n", slot);
		goto out_close;
	}

	if (debug)
		fprintf(stdout, "Marking journal (block %"PRIu64") in slot "
			"%d\n",	journal_blkno, slot);

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
