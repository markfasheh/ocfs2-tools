/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * chain.c
 *
 * Iterate over allocation chains.  Part of the OCFS2 userspace library.
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
 */

#define _XOPEN_SOURCE 600  /* Triggers XOPEN2K in features.h */
#define _LARGEFILE64_SOURCE

#include <string.h>
#include <inttypes.h>

#include "ocfs2.h"


errcode_t ocfs2_read_group_desc(ocfs2_filesys *fs, uint64_t blkno,
				char *gd_buf)
{
	errcode_t ret;
	char *blk;
	ocfs2_group_desc *gd;

	if ((blkno < OCFS2_SUPER_BLOCK_BLKNO) ||
	    (blkno > fs->fs_blocks))
		return OCFS2_ET_BAD_BLKNO;

	ret = ocfs2_malloc_block(fs->fs_io, &blk);
	if (ret)
		return ret;

	ret = io_read_block(fs->fs_io, blkno, 1, blk);
	if (ret)
		goto out;

	gd = (ocfs2_group_desc *)blk;

	ret = OCFS2_ET_BAD_GROUP_DESC_MAGIC;
	if (memcmp(gd->bg_signature, OCFS2_GROUP_DESC_SIGNATURE,
		   strlen(OCFS2_GROUP_DESC_SIGNATURE)))
		goto out;

	/* FIXME swap block */

	memcpy(gd_buf, blk, fs->fs_blocksize);

	ret = 0;
out:
	ocfs2_free(&blk);

	return ret;
}

errcode_t ocfs2_write_group_desc(ocfs2_filesys *fs, uint64_t blkno,
				 char *gd_buf)
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

	memcpy(blk, gd_buf, fs->fs_blocksize);

	ret = io_write_block(fs->fs_io, blkno, 1, blk);
	if (ret)
		goto out;

	fs->fs_flags |= OCFS2_FLAG_CHANGED;
	ret = 0;

out:
	ocfs2_free(&blk);

	return ret;
}



struct chain_context {
	ocfs2_filesys *fs;
	int (*func)(ocfs2_filesys *fs,
		    uint64_t gd_blkno,
		    int chain_num,
		    void *priv_data);
	errcode_t errcode;
	char *gd_buf;
	void *priv_data;
};


static int chain_iterate_gd(ocfs2_chain_rec *c_rec, int chain_num,
			    struct chain_context *ctxt)
{
	int iret = 0;
	uint64_t blkno;
	ocfs2_group_desc *gd;

	blkno = c_rec->c_blkno;

	while (blkno) {
		iret = (*ctxt->func)(ctxt->fs, blkno, chain_num,
				     ctxt->priv_data);
		if (iret & OCFS2_CHAIN_ABORT)
			break;

		ctxt->errcode = ocfs2_read_group_desc(ctxt->fs, blkno,
						      ctxt->gd_buf);
		if (ctxt->errcode) {
			iret |= OCFS2_EXTENT_ERROR;
			break;
		}
		gd = (ocfs2_group_desc *)ctxt->gd_buf;

		if ((gd->bg_blkno != blkno) ||
		    (gd->bg_chain != chain_num)) {
			ctxt->errcode = OCFS2_ET_CORRUPT_GROUP_DESC;
			iret |= OCFS2_CHAIN_ERROR;
			break;
		}

		blkno = gd->bg_next_group;
	}

	return iret;
}

static int chain_iterate_cl(ocfs2_chain_list *cl,
			    struct chain_context *ctxt)
{
	int iret = 0;
	int i;

	for (i = 0; i < cl->cl_next_free_rec; i++) {
		iret |= chain_iterate_gd(&cl->cl_recs[i], i, ctxt);
		if (iret & (OCFS2_CHAIN_ABORT | OCFS2_CHAIN_ERROR))
			break;
	}

	if (iret & OCFS2_CHAIN_CHANGED) {
		/* Something here ? */
	}

	return iret;
}

errcode_t ocfs2_chain_iterate(ocfs2_filesys *fs,
			      uint64_t blkno,
			      int (*func)(ocfs2_filesys *fs,
					  uint64_t gd_blkno,
					  int chain_num,
					  void *priv_data),
			       void *priv_data)
{
	int iret = 0;
	char *buf;
	ocfs2_dinode *inode;
	errcode_t ret;
	struct chain_context ctxt;

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
	if (!(inode->i_flags & OCFS2_CHAIN_FL))
		goto out_buf;

	ret = ocfs2_malloc0(fs->fs_blocksize, &ctxt.gd_buf);
	if (ret)
		goto out_gd_buf;

	ctxt.fs = fs;
	ctxt.func = func;
	ctxt.priv_data = priv_data;

	ret = 0;
	iret |= chain_iterate_cl(&inode->id2.i_chain, &ctxt);
	if (iret & OCFS2_EXTENT_ERROR)
		ret = ctxt.errcode;

	if (iret & OCFS2_EXTENT_CHANGED) {
		/* Do something */
	}

out_gd_buf:
	if (ctxt.gd_buf)
		ocfs2_free(&ctxt.gd_buf);

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
		"Usage: debug_chain -i <inode_blkno> <filename>\n");
}

struct walk_it {
	ocfs2_dinode *di;
	char *gd_buf;
	int last_chain;
	int count_free;
	int count_total;
};

static int walk_chain_func(ocfs2_filesys *fs,
			   uint64_t gd_blkno,
			   int chain_num,
			   void *priv_data)
{
	struct walk_it *wi = priv_data;
	ocfs2_group_desc *gd;
	errcode_t ret;

	if (wi->last_chain != chain_num) {
		fprintf(stdout, "CHAIN[%02d]: %d/%d\n", chain_num,
			wi->di->id2.i_chain.cl_recs[chain_num].c_free,
			wi->di->id2.i_chain.cl_recs[chain_num].c_total);
		wi->last_chain = chain_num;
		wi->count_free = wi->count_total = 0;
	}

	ret = ocfs2_read_group_desc(fs, gd_blkno, wi->gd_buf);
	if (ret)
		return OCFS2_CHAIN_ERROR;

	gd = (ocfs2_group_desc *)wi->gd_buf;
	wi->count_free += gd->bg_free_bits_count;
	wi->count_total += gd->bg_bits;
	fprintf(stdout, "     %16"PRIu64": %05d/%05d = %05d/%05d\n",
		gd->bg_blkno,
		gd->bg_free_bits_count, gd->bg_bits,
		wi->count_free, wi->count_total);

	return 0;
}


extern int opterr, optind;
extern char *optarg;

int main(int argc, char *argv[])
{
	errcode_t ret;
	uint64_t blkno;
	int c;
	char *filename, *buf;
	ocfs2_filesys *fs;
	ocfs2_dinode *di;
	struct walk_it wi;

	blkno = OCFS2_SUPER_BLOCK_BLKNO;

	initialize_ocfs_error_table();

	while ((c = getopt(argc, argv, "bei:")) != EOF) {
		switch (c) {
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
	
	if (blkno == OCFS2_SUPER_BLOCK_BLKNO) {
		fprintf(stderr, "You must specify an inode block\n");
		print_usage();
		return 1;
	}

	if (optind >= argc) {
		fprintf(stderr, "Missing filename\n");
		print_usage();
		return 1;
	}
	filename = argv[optind];
	
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

	memset(&wi, 0, sizeof(wi));

	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret) {
		com_err(argv[0], ret, "while reading inode %"PRIu64, blkno);
		goto out_free;
	}

	di = (ocfs2_dinode *)buf;

	fprintf(stdout, "OCFS2 inode %"PRIu64" on \"%s\"\n",
		blkno, filename);

	ret = ocfs2_malloc_block(fs->fs_io, &wi.gd_buf);
	if (ret) {
		com_err(argv[0], ret,
			"while allocating gd buffer");
		goto out_free;
	}

	wi.di = di;
	wi.last_chain = -1;
	ret = ocfs2_chain_iterate(fs, blkno,
				  walk_chain_func,
				  &wi);
	if (ret) {
		com_err(argv[0], ret,
			"while walking extents");
	}

out_free:
	if (wi.gd_buf)
		ocfs2_free(&wi.gd_buf);

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


