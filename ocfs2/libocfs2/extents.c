/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * extents.c
 *
 * Iterate over the extents in an inode.  Part of the OCFS2 userspace
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
 * Ideas taken from e2fsprogs/lib/ext2fs/block.c
 *   Copyright (C) 1993, 1994, 1995, 1996 Theodore Ts'o.
 */

#define _XOPEN_SOURCE 600  /* Triggers XOPEN2K in features.h */
#define _LARGEFILE64_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <errno.h>

#include <linux/types.h>

#include <et/com_err.h>
#include "ocfs2_err.h"

#include "unix_io.h"
#include "memory.h"
#include "byteorder.h"

#include "ocfs2_fs.h"
#include "ocfs1_fs_compat.h"

#include "filesys.h"


errcode_t ocfs2_read_extent_block(ocfs2_filesys *fs, uint64_t blkno,
				  char *eb_buf)
{
	errcode_t ret;
	char *blk;
	ocfs2_extent_block *eb;

	if ((blkno < OCFS2_SUPER_BLOCK_BLKNO) ||
	    (blkno > fs->fs_blocks))
		return OCFS2_ET_BAD_BLKNO;

	ret = ocfs2_malloc_block(fs->fs_io, &blk);
	if (ret)
		return ret;

	ret = io_read_block(fs->fs_io, blkno, 1, blk);
	if (ret)
		goto out;

	eb = (ocfs2_extent_block *)blk;

	ret = OCFS2_ET_BAD_EXTENT_BLOCK_MAGIC;
	if (memcmp(eb->h_signature, OCFS2_EXTENT_BLOCK_SIGNATURE,
		   strlen(OCFS2_EXTENT_BLOCK_SIGNATURE)))
		goto out;

	/* FIXME swap block */

	memcpy(eb_buf, blk, fs->fs_blocksize);

	ret = 0;
out:
	ocfs2_free(&blk);

	return ret;
}

errcode_t ocfs2_write_extent_block(ocfs2_filesys *fs, uint64_t blkno,
				   char *eb_buf)
{
	errcode_t ret;
	char *blk;

	if (!(fs->fs_flags & OCFS2_FLAG_RW))
		return OCFS2_ET_RO_FILESYS;

	if ((blkno < OCFS2_SUPER_BLOCK_BLKNO) ||
	    (blkno > fs->fs_blocks))
		return OCFS2_ET_BAD_BLKNO;

	ret = ocfs2_malloc_block(fs->fs_io, &blk);
	if (ret)
		return ret;

	/* FIXME Swap block */

	memcpy(blk, eb_buf, fs->fs_blocksize);

	ret = io_write_block(fs->fs_io, blkno, 1, blk);
	if (ret)
		goto out;

	fs->fs_flags |= OCFS2_FLAG_CHANGED;
	ret = 0;

out:
	ocfs2_free(&blk);

	return ret;
}



struct extent_context {
	ocfs2_filesys *fs;
	int (*func)(ocfs2_filesys *fs,
		    ocfs2_extent_rec *rec,
		    int tree_depth,
		    uint32_t ccount,
		    uint64_t ref_blkno,
		    int ref_recno,
		    void *priv_data);
	uint32_t ccount;
	int flags;
	errcode_t errcode;
	char **eb_bufs;
	void *priv_data;
};

static int extent_iterate_eb(ocfs2_extent_rec *eb_rec,
			     int tree_depth, uint64_t ref_blkno,
			     int ref_recno,
			     struct extent_context *ctxt);


static int extent_iterate_el(ocfs2_extent_list *el, uint64_t ref_blkno,
			     struct extent_context *ctxt)
{
	int iret = 0;
	int i;

	for (i = 0; i < el->l_next_free_rec; i++) {
		if (el->l_tree_depth) {
			iret |= extent_iterate_eb(&el->l_recs[i],
						  el->l_tree_depth,
						  ref_blkno, i, ctxt);
		} else {
			iret |= (*ctxt->func)(ctxt->fs, &el->l_recs[i],
					      el->l_tree_depth,
					      ctxt->ccount, ref_blkno,
					      i, ctxt->priv_data);
			ctxt->ccount += el->l_recs[i].e_clusters;
		}
		if (iret & (OCFS2_EXTENT_ABORT | OCFS2_EXTENT_ERROR))
			goto out_abort;
	}

out_abort:
	if (iret & OCFS2_EXTENT_CHANGED) {
		/* Something here ? */
	}

	return iret;
}

static int extent_iterate_eb(ocfs2_extent_rec *eb_rec,
			     int ref_tree_depth, uint64_t ref_blkno,
			     int ref_recno, struct extent_context *ctxt)
{
	int iret = 0, changed = 0, flags;
	int tree_depth = ref_tree_depth - 1;
	ocfs2_extent_block *eb;
	ocfs2_extent_list *el;

	if (!(ctxt->flags & OCFS2_EXTENT_FLAG_DEPTH_TRAVERSE) &&
	    !(ctxt->flags & OCFS2_EXTENT_FLAG_DATA_ONLY))
		iret = (*ctxt->func)(ctxt->fs, eb_rec,
				     ref_tree_depth,
				     ctxt->ccount, ref_blkno,
				     ref_recno, ctxt->priv_data);
	if (!eb_rec->e_blkno || (iret & OCFS2_EXTENT_ABORT))
		return iret;
	if ((eb_rec->e_blkno < OCFS2_SUPER_BLOCK_BLKNO) ||
	    (eb_rec->e_blkno > ctxt->fs->fs_blocks)) {
		ctxt->errcode = OCFS2_ET_BAD_BLKNO;
		iret |= OCFS2_EXTENT_ERROR;
		return iret;
	}

	ctxt->errcode =
		ocfs2_read_extent_block(ctxt->fs,
					eb_rec->e_blkno,
					ctxt->eb_bufs[tree_depth]);
	if (ctxt->errcode) {
		iret |= OCFS2_EXTENT_ERROR;
		return iret;
	}

	eb = (ocfs2_extent_block *)ctxt->eb_bufs[tree_depth];
	el = &eb->h_list;

	if ((el->l_tree_depth != tree_depth) ||
	    (eb->h_blkno != eb_rec->e_blkno)) {
		ctxt->errcode = OCFS2_ET_CORRUPT_EXTENT_BLOCK;
		iret |= OCFS2_EXTENT_ERROR;
		return iret;
	}

	flags = extent_iterate_el(el, eb_rec->e_blkno, ctxt);
	changed |= flags;
	if (flags & OCFS2_EXTENT_ABORT)
		iret |= OCFS2_EXTENT_ABORT;

	if (changed & OCFS2_EXTENT_CHANGED) {
		/* Do something */
	}

	if ((ctxt->flags & OCFS2_EXTENT_FLAG_DEPTH_TRAVERSE) &&
	    !(ctxt->flags & OCFS2_EXTENT_FLAG_DATA_ONLY) &&
	    !(iret & OCFS2_EXTENT_ABORT))
		iret = (*ctxt->func)(ctxt->fs, eb_rec,
				     ref_tree_depth,
				     ctxt->ccount, ref_blkno,
				     ref_recno, ctxt->priv_data);
	return iret;
}


errcode_t ocfs2_extent_iterate(ocfs2_filesys *fs,
			       uint64_t blkno,
			       int flags,
			       char *block_buf,
			       int (*func)(ocfs2_filesys *fs,
					   ocfs2_extent_rec *rec,
					   int tree_depth,
					   uint32_t ccount,
					   uint64_t ref_blkno,
					   int ref_recno,
					   void *priv_data),
			       void *priv_data)
{
	int i;
	int iret = 0;
	char *buf;
	ocfs2_dinode *inode;
	ocfs2_extent_list *el;
	errcode_t ret;
	struct extent_context ctxt;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret)
		goto out_buf;

	inode = (ocfs2_dinode *)buf;

	ret = OCFS2_ET_INODE_NOT_VALID;
	if (!(inode->i_flags & OCFS2_VALID_FL))
		goto out_buf;

	ret = OCFS2_ET_INODE_CANNOT_BE_ITERATED;
	if (inode->i_flags &
	    (OCFS2_SUPER_BLOCK_FL | OCFS2_LOCAL_ALLOC_FL))
		goto out_buf;

	el = &inode->id2.i_list;
	if (el->l_tree_depth) {
		ret = ocfs2_malloc0(sizeof(char *) * el->l_tree_depth,
				    &ctxt.eb_bufs);
		if (ret)
			goto out_buf;

		if (block_buf) {
			ctxt.eb_bufs[0] = block_buf;
		} else {
			ret = ocfs2_malloc0(fs->fs_blocksize *
					    el->l_tree_depth,
					    &ctxt.eb_bufs[0]);
			if (ret)
				goto out_eb_bufs;
		}

		for (i = 1; i < el->l_tree_depth; i++) {
			ctxt.eb_bufs[i] = ctxt.eb_bufs[0] +
				fs->fs_blocksize;
		}
	}
	else
		ctxt.eb_bufs = NULL;

	ctxt.fs = fs;
	ctxt.func = func;
	ctxt.priv_data = priv_data;
	ctxt.flags = flags;
	ctxt.ccount = 0;

	ret = 0;
	iret |= extent_iterate_el(el, 0, &ctxt);
	if (iret & OCFS2_EXTENT_ERROR)
		ret = ctxt.errcode;

	if (iret & OCFS2_EXTENT_ABORT)
		goto out_abort;

out_abort:
	if (iret & OCFS2_EXTENT_CHANGED) {
		/* Do something */
	}

out_eb_bufs:
	if (ctxt.eb_bufs) {
		if (!block_buf && ctxt.eb_bufs[0])
			ocfs2_free(&ctxt.eb_bufs[0]);
		ocfs2_free(&ctxt.eb_bufs);
	}

out_buf:
	ocfs2_free(&buf);

	return ret;
}


#ifdef DEBUG_EXE
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
		"Usage: extents <filename> <inode_num>\n");
}

struct walk_it {
	ocfs2_dinode *di;
};

static int walk_extents_func(ocfs2_filesys *fs,
			     ocfs2_extent_rec *rec,
			     int tree_depth,
			     uint32_t ccount,
			     uint64_t ref_blkno,
			     int ref_recno,
			     void *priv_data)
{
	struct walk_it *wi = priv_data;
	int pad_amount = wi->di->id2.i_list.l_tree_depth - tree_depth;
	int i;

	fprintf(stdout, "0x%08llX:%02u ", ref_blkno, ref_recno);
	for (i = 0; i < pad_amount; i++)
		fprintf(stdout, " ");
	fprintf(stdout, "(%08u, %08lu, %08llu) | + %08lu = %08lu / %08lu\n",
		rec->e_cpos, rec->e_clusters,
		rec->e_blkno, ccount, ccount + rec->e_clusters,
		wi->di->i_clusters);

	return 0;
}

extern int opterr, optind;
extern char *optarg;

int main(int argc, char *argv[])
{
	errcode_t ret;
	uint64_t blkno;
	char *filename, *buf, *eb_buf = NULL;
	ocfs2_filesys *fs;
	ocfs2_dinode *di;
	struct walk_it wi;

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


	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret) {
		com_err(argv[0], ret,
			"while reading inode %llu", blkno);
		goto out_free;
	}

	di = (ocfs2_dinode *)buf;

	fprintf(stdout, "OCFS2 inode %llu on \"%s\" has depth %d\n",
		blkno, filename, di->id2.i_list.l_tree_depth);

	if (di->id2.i_list.l_tree_depth) {
		ret = ocfs2_malloc_blocks(fs->fs_io,
					  di->id2.i_list.l_tree_depth,
					  &eb_buf);
		if (ret) {
			com_err(argv[0], ret,
				"while allocating eb buffer");
			goto out_free;
		}
	}

	wi.di = di;
	ret = ocfs2_extent_iterate(fs, blkno, 0,
				   eb_buf,
				   walk_extents_func,
				   &wi);
	if (ret) {
		com_err(argv[0], ret,
			"while walking extents");
		goto out_free;
	}

out_free:
	if (eb_buf)
		ocfs2_free(&eb_buf);

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


