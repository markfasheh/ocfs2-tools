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


static errcode_t insert_extent_eb(ocfs2_filesys *fs, uint64_t eb_blkno,
				  ocfs2_extent_rec *new_rec);

static errcode_t insert_extent_el(ocfs2_filesys *fs,
			  	  ocfs2_extent_list *el,
				  ocfs2_extent_rec *new_rec)
{
	errcode_t ret;
	ocfs2_extent_rec *rec;

	if (!el->l_tree_depth) {
		if (el->l_next_free_rec) {
			rec = &el->l_recs[el->l_next_free_rec - 1];

			if ((rec->e_blkno +
			     ocfs2_clusters_to_blocks(fs, rec->e_clusters)) ==
			    new_rec->e_blkno) {
				rec->e_clusters += new_rec->e_clusters;
				return 0;
			}

			if (!rec->e_clusters) {
				*rec = *new_rec;
				return 0;
			}

			if (el->l_next_free_rec == el->l_count)
				return OCFS2_ET_NO_SPACE;
		}

		rec = &el->l_recs[el->l_next_free_rec];
		*rec = *new_rec;
		el->l_next_free_rec++;
		return 0;
	}

	/* We're a branch node */
	rec = &el->l_recs[el->l_next_free_rec - 1];
	ret = insert_extent_eb(fs, rec->e_blkno, new_rec);
	if (ret) {
		if (ret != OCFS2_ET_NO_SPACE)
			return ret;
		
		if (el->l_next_free_rec == el->l_count)
			return OCFS2_ET_NO_SPACE;

		/* FIXME: Alloc a metadata block */
	}

	rec->e_clusters += new_rec->e_clusters;
	return 0;
}

static errcode_t insert_extent_eb(ocfs2_filesys *fs, uint64_t eb_blkno,
				  ocfs2_extent_rec *new_rec)
{
	errcode_t ret;
	char *buf;
	ocfs2_extent_block *eb;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_read_extent_block(fs, eb_blkno, buf);
	if (!ret) {
		eb = (ocfs2_extent_block *)buf;
		ret = insert_extent_el(fs, &eb->h_list, new_rec);
	}

	ocfs2_free(&buf);
	return ret;
}

static errcode_t shift_tree_depth(ocfs2_filesys *fs, ocfs2_dinode *di)
{
	return 0;
}

errcode_t ocfs2_insert_extent(ocfs2_filesys *fs, uint64_t ino,
			      uint64_t c_blkno, uint32_t clusters)
{
	errcode_t ret;
	ocfs2_extent_rec rec;
	ocfs2_dinode *di;
	char *buf;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	di = (ocfs2_dinode *)buf;
	ret = ocfs2_read_inode(fs, ino, buf);
	if (ret)
		goto out_free_buf;

	rec.e_cpos = di->i_clusters;
	rec.e_blkno = c_blkno;
	rec.e_clusters = clusters;
	ret = insert_extent_el(fs, &di->id2.i_list, &rec);
	if (ret == OCFS2_ET_NO_SPACE) {
		ret = shift_tree_depth(fs, di);
		if (!ret)
			ret = insert_extent_el(fs, &di->id2.i_list,
					       &rec);
	}

out_free_buf:
	ocfs2_free(&buf);

	return ret;
}

errcode_t ocfs2_extend_allocation(ocfs2_filesys *fs, uint64_t ino,
				  uint64_t new_clusters)
{
	return 0;
}
