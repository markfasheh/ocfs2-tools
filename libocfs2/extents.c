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

#include <string.h>

#include "ocfs2.h"


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

struct block_context {
	int (*func)(ocfs2_filesys *fs,
		    uint64_t blkno,
		    uint64_t bcount,
		    void *priv_data);
	int flags;
	ocfs2_dinode *inode;
	errcode_t errcode;
	void *priv_data;
};

static int block_iterate_func(ocfs2_filesys *fs,
			      ocfs2_extent_rec *rec,
			      int tree_depth,
			      uint32_t ccount,
			      uint64_t ref_blkno,
			      int ref_recno,
			      void *priv_data)
{
	struct block_context *ctxt = priv_data;
	uint64_t blkno, bcount, bend;
	int iret = 0;
	int c_to_b_bits =
		(OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits -
		 OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits);

	bcount = (uint64_t)ccount << c_to_b_bits;
	bend = bcount + ((uint64_t)rec->e_clusters << c_to_b_bits);

	for (blkno = rec->e_blkno; bcount < bend; blkno++, bcount++) {
		if (((bcount * fs->fs_blocksize) >= ctxt->inode->i_size) &&
		    !(ctxt->flags & OCFS2_BLOCK_FLAG_APPEND))
			break;

		iret = (*ctxt->func)(fs, blkno, bcount,
				     ctxt->priv_data);
		if (iret & OCFS2_BLOCK_ABORT)
			break;
	}

	return iret;
}

errcode_t ocfs2_block_iterate(ocfs2_filesys *fs,
			      uint64_t blkno,
			      int flags,
			      int (*func)(ocfs2_filesys *fs,
					  uint64_t blkno,
					  uint64_t bcount,
					  void *priv_data),
			      void *priv_data)
{
	errcode_t ret;
	char *buf;
	struct block_context ctxt;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret)
		goto out_buf;

	ctxt.inode = (ocfs2_dinode *)buf;
	ctxt.flags = flags;
	ctxt.func = func;
	ctxt.errcode = 0;
	ctxt.priv_data = priv_data;

	ret = ocfs2_extent_iterate(fs, blkno,
				   OCFS2_EXTENT_FLAG_DATA_ONLY,
				   NULL,
				   block_iterate_func, &ctxt);

out_buf:
	ocfs2_free(&buf);

	return ret;
}


#ifdef DEBUG_EXE
#include <stdlib.h>
#include <getopt.h>

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
		"Usage: extents -i <inode_blkno> [-e] [-b] <filename>\n");
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

	if (!ccount && !pad_amount)
		fprintf(stdout, "EXTENTS:\n");

	fprintf(stdout, "0x%08llX:%02u ", ref_blkno, ref_recno);
	for (i = 0; i < pad_amount; i++)
		fprintf(stdout, " ");
	fprintf(stdout, "(%08u, %08lu, %08llu) | + %08lu = %08lu / %08lu\n",
		rec->e_cpos, rec->e_clusters,
		rec->e_blkno, ccount, ccount + rec->e_clusters,
		wi->di->i_clusters);

	if (!tree_depth &&
	    ((ccount + rec->e_clusters) == wi->di->i_clusters))
		fprintf(stdout, "TOTAL: %u\n", wi->di->i_clusters);

	return 0;
}

struct walk_block {
	ocfs2_dinode *di;
	uint64_t last_block;
	uint64_t run_first_blkno;
	uint64_t run_first_bcount;
	uint64_t run_prev_blkno;
};

static int walk_blocks_func(ocfs2_filesys *fs,
			    uint64_t blkno,
			    uint64_t bcount,
			    void *priv_data)
{
	struct walk_block *wb = priv_data;

	/* Very first block */
	if (!wb->run_prev_blkno) {
		wb->run_prev_blkno = blkno;
		wb->run_first_blkno = blkno;
		fprintf(stdout, "BLOCKS:\n");
	} else if ((wb->run_prev_blkno + 1) != blkno) {
		if (wb->run_first_bcount)
			fprintf(stdout, ", ");

		if ((wb->run_first_bcount + 1) == bcount) {
			fprintf(stdout, "(%llu):%llu",
				wb->run_first_bcount,
				wb->run_first_blkno);
		} else {
			fprintf(stdout, "(%llu-%llu):%llu-%llu",
				wb->run_first_bcount,
				bcount - 1,
				wb->run_first_blkno,
				wb->run_prev_blkno);
		}
		wb->run_first_bcount = bcount;
		wb->run_first_blkno = blkno;
	}

	if ((bcount + 1) == wb->last_block) {
		if (wb->run_first_bcount)
			fprintf(stdout, ", ");

		if ((wb->run_prev_blkno + 1) != blkno) {
			fprintf(stdout, "(%llu):%llu\n",
				bcount, blkno);
		} else {
			fprintf(stdout, "(%llu-%llu):%llu-%llu\n",
				wb->run_first_bcount,
				bcount,
				wb->run_first_blkno,
				blkno);
		}

		fprintf(stdout, "TOTAL: %llu\n", bcount + 1);
	}

	wb->run_prev_blkno = blkno;

	return 0;
}


extern int opterr, optind;
extern char *optarg;

int main(int argc, char *argv[])
{
	errcode_t ret;
	uint64_t blkno;
	int c;
	int walk_blocks = 0, walk_extents = 0;
	char *filename, *buf, *eb_buf = NULL;
	ocfs2_filesys *fs;
	ocfs2_dinode *di;
	struct walk_it wi;
	struct walk_block wb;

	blkno = OCFS2_SUPER_BLOCK_BLKNO;

	initialize_ocfs_error_table();

	while ((c = getopt(argc, argv, "bei:")) != EOF) {
		switch (c) {
			case 'b':
				walk_blocks = 1;
				break;

			case 'e':
				walk_extents = 1;
				break;

			case 'i':
				blkno = read_number(optarg);
				if (blkno <= OCFS2_SUPER_BLOCK_BLKNO) {
					fprintf(stderr,
						"Invalid inode block: %s\n",
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

	if (optind >= argc) {
		fprintf(stderr, "Missing filename\n");
		print_usage();
		return 1;
	}
	filename = argv[optind];
	
	if (!(walk_blocks + walk_extents)) {
		fprintf(stderr,
			"No operation specified\n");
		print_usage();
		return 1;
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

	if (walk_extents) {
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
	}

	if (walk_blocks) {
		wb.di = di;
		wb.run_first_blkno = wb.run_first_bcount =
			wb.run_prev_blkno = 0;
		wb.last_block = (wb.di->i_size +
				 (fs->fs_blocksize - 1)) /
			fs->fs_blocksize;
		ret = ocfs2_block_iterate(fs, blkno, 0,
					  walk_blocks_func,
					  &wb);
		if (ret) {
			com_err(argv[0], ret,
				"while walking blocks");
			goto out_free;
		}
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


