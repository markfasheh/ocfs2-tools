/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * extend_file.c
 *
 * Adds extents to an OCFS2 inode.  For the OCFS2 userspace library.
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
 */

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE

#include <stdio.h>
#include <string.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "ocfs2.h"


struct insert_ctxt {
	ocfs2_filesys *fs;
	ocfs2_dinode *di;
	ocfs2_extent_rec rec;
};

static errcode_t insert_extent_eb(struct insert_ctxt *ctxt,
				  uint64_t eb_blkno);

/*
 * Update the leaf pointer from the previous last_eb_blk to the new
 * last_eb_blk.  Also updates the dinode's ->last_eb_blk.
 */
static errcode_t update_last_eb_blk(struct insert_ctxt *ctxt,
				    ocfs2_extent_block *eb)
{
	errcode_t ret;
	char *buf;
	ocfs2_extent_block *last_eb;

	if (!ctxt->di->i_last_eb_blk)
		return OCFS2_ET_INTERNAL_FAILURE;

	ret = ocfs2_malloc_block(ctxt->fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_read_extent_block(ctxt->fs, ctxt->di->i_last_eb_blk,
				      buf);
	if (ret)
		goto out;

	last_eb = (ocfs2_extent_block *)buf;
	last_eb->h_next_leaf_blk = eb->h_blkno;

	ret = ocfs2_write_extent_block(ctxt->fs, last_eb->h_blkno,
				       buf);
	if (ret)
		goto out;

	/* This is written at the end by insert_extent() */
	ctxt->di->i_last_eb_blk = eb->h_blkno;

out:
	ocfs2_free(&buf);

	return ret;
}

/*
 * Add a child extent_block to a non-leaf extent list.
 */
static errcode_t append_eb(struct insert_ctxt *ctxt,
			   ocfs2_extent_list *el)
{
	errcode_t ret;
	char *buf;
	uint64_t blkno;
	ocfs2_extent_block *eb;
	ocfs2_extent_rec *rec;

	ret = ocfs2_malloc_block(ctxt->fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_new_extent_block(ctxt->fs, &blkno);
	if (ret)
		goto out;

	ret = ocfs2_read_extent_block(ctxt->fs, blkno, buf);
	if (ret)
		goto out;

	eb = (ocfs2_extent_block *)buf;
	eb->h_list.l_tree_depth = el->l_tree_depth - 1;

	if (!eb->h_list.l_tree_depth) {
		ret = update_last_eb_blk(ctxt, eb);
		if (ret)
			goto out;
	}

	if (el->l_next_free_rec) {
		rec = &el->l_recs[el->l_next_free_rec - 1];
		if (!rec->e_blkno) {
			rec->e_blkno = blkno;
			goto out;
		}
	}
	rec = &el->l_recs[el->l_next_free_rec];
	rec->e_blkno = blkno;
	rec->e_cpos = ctxt->rec.e_cpos;
	el->l_next_free_rec++;

out:
	ocfs2_free(&buf);

	return ret;
}

/*
 * Insert a new extent into an extent list.  If this list is a leaf,
 * add it where appropriate.  Otherwise, recurse down the appropriate
 * branch, updating this list on the way back up.
 */
static errcode_t insert_extent_el(struct insert_ctxt *ctxt,
			  	  ocfs2_extent_list *el)
{
	errcode_t ret;
	ocfs2_extent_rec *rec;

	if (!el->l_tree_depth) {
		/* A leaf extent_list can do one of three things: */
		if (el->l_next_free_rec) {
			/* It has at least one valid entry and... */
			rec = &el->l_recs[el->l_next_free_rec - 1];

			/* (1) That entry is contiguous with the new
			 *     one, so just enlarge the entry. */
			if ((rec->e_blkno +
			     ocfs2_clusters_to_blocks(ctxt->fs, rec->e_clusters)) ==
			    ctxt->rec.e_blkno) {
				rec->e_clusters += ctxt->rec.e_clusters;
				return 0;
			}

			/* (2) That entry is zero length, so just fill
			 *     it in with the new one. */
			if (!rec->e_clusters) {
				*rec = ctxt->rec;
				return 0;
			}

			if (el->l_next_free_rec == el->l_count)
				return OCFS2_ET_NO_SPACE;
		}

		/* (3) The new entry can't use an existing slot, so
		 *     put it in a new slot. */
		rec = &el->l_recs[el->l_next_free_rec];
		*rec = ctxt->rec;
		el->l_next_free_rec++;
		return 0;
	}

	/* We're a branch node */
	ret = OCFS2_ET_NO_SPACE;
	if (el->l_next_free_rec) {
		/* If there exists a valid record, and it is not an
		 * empty record (e_blkno points to a valid child),
		 * try to fill along that branch. */
		rec = &el->l_recs[el->l_next_free_rec - 1];
		if (rec->e_blkno)
			ret = insert_extent_eb(ctxt, rec->e_blkno);
	}
	if (ret) {
		if (ret != OCFS2_ET_NO_SPACE)
			return ret;
		
		if ((el->l_next_free_rec == el->l_count) &&
		    (el->l_recs[el->l_next_free_rec - 1].e_blkno))
			return OCFS2_ET_NO_SPACE;

		/* If there wasn't an existing child we insert to and
		 * there are free slots, add a new child. */
		ret = append_eb(ctxt, el);
		if (ret)
			return ret;

		/* append_eb() put a new record here, insert on it.
		 * If the new child isn't a leaf, this recursion
		 * will do the append_eb() again, all the way down to
		 * the leaf. */
		rec = &el->l_recs[el->l_next_free_rec - 1];
		ret = insert_extent_eb(ctxt, rec->e_blkno);
		if (ret)
			return ret;
	}

	/* insert_extent_eb() doesn't update e_clusters so that
	 * all updates are on the path up, not the path down.  Do the
	 * update now. */
	rec->e_clusters += ctxt->rec.e_clusters;
	return 0;
}

/*
 * Insert a new extent into this extent_block.  That means
 * reading the block, calling insert_extent_el() on the contained
 * extent list, and then writing out the updated block.
 */
static errcode_t insert_extent_eb(struct insert_ctxt *ctxt,
				  uint64_t eb_blkno)
{
	errcode_t ret;
	char *buf;
	ocfs2_extent_block *eb;

	ret = ocfs2_malloc_block(ctxt->fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_read_extent_block(ctxt->fs, eb_blkno, buf);
	if (!ret) {
		eb = (ocfs2_extent_block *)buf;
		ret = insert_extent_el(ctxt, &eb->h_list);
	}

	if (!ret)
		ret = ocfs2_write_extent_block(ctxt->fs, eb_blkno, buf);

	ocfs2_free(&buf);
	return ret;
}

/*
 * Change the depth of the tree. That means allocating an extent block,
 * copying all extent records from the dinode into the extent block,
 * and then pointing the dinode to the new extent_block.
 */
static errcode_t shift_tree_depth(struct insert_ctxt *ctxt)
{
	errcode_t ret;
	char *buf;
	uint64_t blkno;
	ocfs2_extent_block *eb;
	ocfs2_extent_list *el;

	el = &ctxt->di->id2.i_list;
	if (el->l_next_free_rec != el->l_count)
		return OCFS2_ET_INTERNAL_FAILURE;

	ret = ocfs2_malloc_block(ctxt->fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_new_extent_block(ctxt->fs, &blkno);
	if (ret)
		goto out;

	ret = ocfs2_read_extent_block(ctxt->fs, blkno, buf);
	if (ret)
		goto out;

	eb = (ocfs2_extent_block *)buf;
	eb->h_list.l_tree_depth = el->l_tree_depth;
	eb->h_list.l_next_free_rec = el->l_next_free_rec;
	memcpy(eb->h_list.l_recs, el->l_recs,
	       sizeof(ocfs2_extent_rec) * el->l_count);

	el->l_tree_depth++;
	memset(el->l_recs, 0, sizeof(ocfs2_extent_rec) * el->l_count);
	el->l_recs[0].e_cpos = 0;
	el->l_recs[0].e_blkno = blkno;
	el->l_recs[0].e_clusters = ctxt->di->i_clusters;
	el->l_next_free_rec = 1;

	if (el->l_tree_depth == 1)
		ctxt->di->i_last_eb_blk = blkno;

out:
	ocfs2_free(&buf);

	return 0;
}

/*
 * Takes a new contiguous extend, defined by (blkno, clusters), and
 * inserts it into the tree of dinode ino.  This follows the driver's
 * allocation pattern.  It tries to insert on the existing tree, and
 * if that tree is completely full, then shifts the tree depth.
 */
errcode_t ocfs2_insert_extent(ocfs2_filesys *fs, uint64_t ino,
			      uint64_t c_blkno, uint32_t clusters)
{
	errcode_t ret;
	struct insert_ctxt ctxt;
	char *buf;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ctxt.fs = fs;
	ctxt.di = (ocfs2_dinode *)buf;

	ret = ocfs2_read_inode(fs, ino, buf);
	if (ret)
		goto out_free_buf;

	ctxt.rec.e_cpos = ctxt.di->i_clusters;
	ctxt.rec.e_blkno = c_blkno;
	ctxt.rec.e_clusters = clusters;
	ret = insert_extent_el(&ctxt, &ctxt.di->id2.i_list);
	if (ret == OCFS2_ET_NO_SPACE) {
		ret = shift_tree_depth(&ctxt);
		if (!ret)
			ret = insert_extent_el(&ctxt,
					       &ctxt.di->id2.i_list);
	}
	if (!ret) {
		ctxt.di->i_clusters += clusters;
		ret = ocfs2_write_inode(fs, ino, buf);
	}

out_free_buf:
	ocfs2_free(&buf);

	return ret;
}

errcode_t ocfs2_extend_allocation(ocfs2_filesys *fs, uint64_t ino,
				  uint32_t new_clusters)
{
	errcode_t ret = 0;
	uint32_t n_clusters = 0;
	uint64_t blkno;

	if (!(fs->fs_flags & OCFS2_FLAG_RW))
		return OCFS2_ET_RO_FILESYS;

	while (new_clusters) {
		n_clusters = 1;
		ret = ocfs2_new_clusters(fs, n_clusters, &blkno);
		if (ret)
			break;

	 	ret = ocfs2_insert_extent(fs, ino, blkno, n_clusters);
		if (ret) {
			/* XXX: We don't wan't to overwrite the error
			 * from insert_extent().  But we probably need
			 * to BE LOUDLY UPSET. */
			ocfs2_free_clusters(fs, n_clusters, blkno);
			break;
		}

	 	new_clusters -= n_clusters;
	}

	return ret;
}

#ifdef DEBUG_EXE
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <inttypes.h>

static void print_usage(void)
{
	fprintf(stdout, "debug_extend_file -i <ino> -c <clusters> <device>\n");
}

static uint64_t read_number(const char *num)
{
	uint64_t val;
	char *ptr;

	val = strtoull(num, &ptr, 0);
	if (!ptr || *ptr)
		return 0;

	return val;
}

extern int opterr, optind;
extern char *optarg;

int main(int argc, char *argv[])
{
	errcode_t ret;
	char *filename;
	ocfs2_filesys *fs;
	uint64_t ino = 0;
	uint32_t new_clusters = 0;
	int c;

	initialize_ocfs_error_table();

	while ((c = getopt(argc, argv, "i:c:")) != EOF) {
		switch (c) {
			case 'i':
				ino = read_number(optarg);
				if (ino <= OCFS2_SUPER_BLOCK_BLKNO) {
					fprintf(stderr,
						"Invalid inode block: %s\n",
						optarg);
					print_usage();
					return 1;
				}
				break;

			case 'c':
				new_clusters = read_number(optarg);
				if (!new_clusters) {
					fprintf(stderr,
						"Invalid cluster count: %s\n",
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

	if (!ino) {
		fprintf(stderr, "You must specify an inode block\n");
		print_usage();
		return 1;
	}

	if (!new_clusters) {
		fprintf(stderr, "You must specify how many clusters to extend\n");
		print_usage();
		return 1;
	}

	if (optind >= argc) {
		fprintf(stderr, "Missing filename\n");
		print_usage();
		return 1;
	}
	filename = argv[optind];

	ret = ocfs2_open(filename, OCFS2_FLAG_RW, 0, 0, &fs);
	if (ret) {
		com_err(argv[0], ret,
			"while opening file \"%s\"", filename);
		goto out;
	}

	ret = ocfs2_extend_allocation(fs, ino, new_clusters);
	if (ret) {
		com_err(argv[0], ret,
			"while extending inode %"PRIu64, ino);
	}

	ret = ocfs2_close(fs);
	if (ret) {
		com_err(argv[0], ret,
			"while closing file \"%s\"", filename);
	}
out:
	return !!ret;
}
#endif  /* DEBUG_EXE */
