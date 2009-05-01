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
 * Ideas taken from e2fsprogs/lib/ext2fs/block.c
 *   Copyright (C) 1993, 1994, 1995, 1996 Theodore Ts'o.
 */

#define _XOPEN_SOURCE 600  /* Triggers XOPEN2K in features.h */
#define _LARGEFILE64_SOURCE

#include <string.h>
#include <inttypes.h>

#include "ocfs2/byteorder.h"
#include "ocfs2/ocfs2.h"

static void ocfs2_swap_extent_list_primary(struct ocfs2_extent_list *el)
{
	el->l_tree_depth = bswap_16(el->l_tree_depth);
	el->l_count = bswap_16(el->l_count);
	el->l_next_free_rec = bswap_16(el->l_next_free_rec);
}

static void ocfs2_swap_extent_list_secondary(struct ocfs2_extent_list *el)
{
	uint16_t i;

	for(i = 0; i < el->l_next_free_rec; i++) {
		struct ocfs2_extent_rec *rec = &el->l_recs[i];

		rec->e_cpos = bswap_32(rec->e_cpos);
		if (el->l_tree_depth)
			rec->e_int_clusters = bswap_32(rec->e_int_clusters);
		else
			rec->e_leaf_clusters = bswap_16(rec->e_leaf_clusters);
		rec->e_blkno = bswap_64(rec->e_blkno);
	}
}

void ocfs2_swap_extent_list_from_cpu(struct ocfs2_extent_list *el)
{
	if (cpu_is_little_endian)
		return;

	ocfs2_swap_extent_list_secondary(el);
	ocfs2_swap_extent_list_primary(el);
}
void ocfs2_swap_extent_list_to_cpu(struct ocfs2_extent_list *el)
{
	if (cpu_is_little_endian)
		return;

	ocfs2_swap_extent_list_primary(el);
	ocfs2_swap_extent_list_secondary(el);
}

static void ocfs2_swap_extent_block_header(struct ocfs2_extent_block *eb)
{

	eb->h_suballoc_slot = bswap_16(eb->h_suballoc_slot);
	eb->h_suballoc_bit  = bswap_16(eb->h_suballoc_bit);
	eb->h_fs_generation = bswap_32(eb->h_fs_generation);
	eb->h_blkno         = bswap_64(eb->h_blkno);
	eb->h_next_leaf_blk = bswap_64(eb->h_next_leaf_blk);
}

void ocfs2_swap_extent_block_from_cpu(struct ocfs2_extent_block *eb)
{
	if (cpu_is_little_endian)
		return;

	ocfs2_swap_extent_block_header(eb);
	ocfs2_swap_extent_list_from_cpu(&eb->h_list);
}

void ocfs2_swap_extent_block_to_cpu(struct ocfs2_extent_block *eb)
{
	if (cpu_is_little_endian)
		return;

	ocfs2_swap_extent_block_header(eb);
	ocfs2_swap_extent_list_to_cpu(&eb->h_list);
}

errcode_t ocfs2_read_extent_block_nocheck(ocfs2_filesys *fs,
					  uint64_t blkno,
					  char *eb_buf)
{
	errcode_t ret;
	char *blk;
	struct ocfs2_extent_block *eb;

	if ((blkno < OCFS2_SUPER_BLOCK_BLKNO) ||
	    (blkno > fs->fs_blocks))
		return OCFS2_ET_BAD_BLKNO;

	ret = ocfs2_malloc_block(fs->fs_io, &blk);
	if (ret)
		return ret;

	ret = ocfs2_read_blocks(fs, blkno, 1, blk);
	if (ret)
		goto out;

	eb = (struct ocfs2_extent_block *)blk;

	ret = ocfs2_validate_meta_ecc(fs, blk, &eb->h_check);
	if (ret)
		goto out;

	if (memcmp(eb->h_signature, OCFS2_EXTENT_BLOCK_SIGNATURE,
		   strlen(OCFS2_EXTENT_BLOCK_SIGNATURE))) {
		ret = OCFS2_ET_BAD_EXTENT_BLOCK_MAGIC;
		goto out;
	}

	memcpy(eb_buf, blk, fs->fs_blocksize);

	eb = (struct ocfs2_extent_block *) eb_buf;
	ocfs2_swap_extent_block_to_cpu(eb);

out:
	ocfs2_free(&blk);

	return ret;
}

errcode_t ocfs2_read_extent_block(ocfs2_filesys *fs, uint64_t blkno,
				  char *eb_buf)
{
	errcode_t ret;
	struct ocfs2_extent_block *eb =
		(struct ocfs2_extent_block *)eb_buf;

	ret = ocfs2_read_extent_block_nocheck(fs, blkno, eb_buf);

	if (ret == 0 && eb->h_list.l_next_free_rec > eb->h_list.l_count)
		ret = OCFS2_ET_CORRUPT_EXTENT_BLOCK;

	return ret;
}

errcode_t ocfs2_write_extent_block(ocfs2_filesys *fs, uint64_t blkno,
				   char *eb_buf)
{
	errcode_t ret;
	char *blk;
	struct ocfs2_extent_block *eb;

	if (!(fs->fs_flags & OCFS2_FLAG_RW))
		return OCFS2_ET_RO_FILESYS;

	if ((blkno < OCFS2_SUPER_BLOCK_BLKNO) ||
	    (blkno > fs->fs_blocks))
		return OCFS2_ET_BAD_BLKNO;

	ret = ocfs2_malloc_block(fs->fs_io, &blk);
	if (ret)
		return ret;

	memcpy(blk, eb_buf, fs->fs_blocksize);

	eb = (struct ocfs2_extent_block *) blk;
	ocfs2_swap_extent_block_from_cpu(eb);

	ocfs2_compute_meta_ecc(fs, blk, &eb->h_check);
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
		    struct ocfs2_extent_rec *rec,
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
	uint64_t last_eb_blkno;
	uint64_t last_eb_cpos;
};

static int extent_iterate_eb(struct ocfs2_extent_rec *eb_rec,
			     int tree_depth, uint64_t ref_blkno,
			     int ref_recno,
			     struct extent_context *ctxt);

static int update_leaf_rec(struct extent_context *ctxt,
			   struct ocfs2_extent_rec *before,
			   struct ocfs2_extent_rec *current)
{
	return 0;
}

static int update_eb_rec(struct extent_context *ctxt,
			 struct ocfs2_extent_rec *before,
			 struct ocfs2_extent_rec *current)
{
	return 0;
}

static int extent_iterate_el(struct ocfs2_extent_list *el,
			     uint64_t ref_blkno,
			     struct extent_context *ctxt)
{
	struct ocfs2_extent_rec before;
	int iret = 0;
	int i;

	for (i = 0; i < el->l_next_free_rec; i++) {
		/* XXX we could put some constraints on how the rec
		 * is allowed to change.. */
		before = el->l_recs[i];

		if (el->l_tree_depth) {
			iret |= extent_iterate_eb(&el->l_recs[i],
						  el->l_tree_depth,
						  ref_blkno, i, ctxt);
			if (iret & OCFS2_EXTENT_CHANGED)
				iret |= update_eb_rec(ctxt, &before,
						      &el->l_recs[i]);

			if (el->l_recs[i].e_int_clusters &&
			   (el->l_recs[i].e_cpos >= ctxt->last_eb_cpos)) {
				/*
				 * Only set last_eb_blkno if current extent
				 * list	point to leaf blocks.
				 */
				if (el->l_tree_depth == 1)
					ctxt->last_eb_blkno =
							el->l_recs[i].e_blkno;
				ctxt->last_eb_cpos = el->l_recs[i].e_cpos;
			}

		} else {
			/*
			 * For a sparse file, we may find an empty record
			 * in the left most record. Just skip it.
			 */
			if (!i && !el->l_recs[i].e_leaf_clusters)
				continue;
			iret |= (*ctxt->func)(ctxt->fs, &el->l_recs[i],
					      el->l_tree_depth,
					      ctxt->ccount, ref_blkno,
					      i, ctxt->priv_data);
			if (iret & OCFS2_EXTENT_CHANGED)
				iret |= update_leaf_rec(ctxt, &before,
							&el->l_recs[i]);
			ctxt->ccount += ocfs2_rec_clusters(el->l_tree_depth,
							   &el->l_recs[i]);
		}
		if (iret & (OCFS2_EXTENT_ABORT | OCFS2_EXTENT_ERROR))
			break;
	}

	if (iret & OCFS2_EXTENT_CHANGED) {
		for (i = 0; i < el->l_count; i++) {
			if (ocfs2_rec_clusters(el->l_tree_depth,
					       &el->l_recs[i]))
				continue;
			el->l_next_free_rec = i;
			break;
		}
	}

	return iret;
}

static int extent_iterate_eb(struct ocfs2_extent_rec *eb_rec,
			     int ref_tree_depth, uint64_t ref_blkno,
			     int ref_recno, struct extent_context *ctxt)
{
	int iret = 0, changed = 0, flags;
	int tree_depth = ref_tree_depth - 1;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_list *el;

	if (!(ctxt->flags & OCFS2_EXTENT_FLAG_DEPTH_TRAVERSE) &&
	    !(ctxt->flags & OCFS2_EXTENT_FLAG_DATA_ONLY))
		iret = (*ctxt->func)(ctxt->fs, eb_rec,
				     ref_tree_depth,
				     ctxt->ccount, ref_blkno,
				     ref_recno, ctxt->priv_data);
	if (!eb_rec->e_blkno || (iret & OCFS2_EXTENT_ABORT))
		goto out;
	if ((eb_rec->e_blkno < OCFS2_SUPER_BLOCK_BLKNO) ||
	    (eb_rec->e_blkno > ctxt->fs->fs_blocks)) {
		ctxt->errcode = OCFS2_ET_BAD_BLKNO;
		iret |= OCFS2_EXTENT_ERROR;
		goto out;
	}

	ctxt->errcode =
		ocfs2_read_extent_block(ctxt->fs,
					eb_rec->e_blkno,
					ctxt->eb_bufs[tree_depth]);
	if (ctxt->errcode) {
		iret |= OCFS2_EXTENT_ERROR;
		goto out;
	}

	eb = (struct ocfs2_extent_block *)ctxt->eb_bufs[tree_depth];
	el = &eb->h_list;

	if ((el->l_tree_depth != tree_depth) ||
	    (eb->h_blkno != eb_rec->e_blkno)) {
		ctxt->errcode = OCFS2_ET_CORRUPT_EXTENT_BLOCK;
		iret |= OCFS2_EXTENT_ERROR;
		goto out;
	}

	flags = extent_iterate_el(el, eb_rec->e_blkno, ctxt);
	changed |= flags;
	if (flags & (OCFS2_EXTENT_ABORT | OCFS2_EXTENT_ERROR))
		iret |= flags & (OCFS2_EXTENT_ABORT | OCFS2_EXTENT_ERROR);

	/*
	 * If the list was changed, we should write the changes to disk.
	 * Note:
	 * For a sparse file, we may have an empty extent block.
	 */
	if (changed & OCFS2_EXTENT_CHANGED) {
		ctxt->errcode = ocfs2_write_extent_block(ctxt->fs,
							 eb_rec->e_blkno,
						ctxt->eb_bufs[tree_depth]);
		if (ctxt->errcode) {
			iret |= OCFS2_EXTENT_ERROR;
			goto out;
		}
	}

	if ((ctxt->flags & OCFS2_EXTENT_FLAG_DEPTH_TRAVERSE) &&
	    !(ctxt->flags & OCFS2_EXTENT_FLAG_DATA_ONLY) &&
	    !(iret & (OCFS2_EXTENT_ABORT|OCFS2_EXTENT_ERROR)))
		iret = (*ctxt->func)(ctxt->fs, eb_rec,
				     ref_tree_depth,
				     ctxt->ccount, ref_blkno,
				     ref_recno, ctxt->priv_data);
out:
	return iret;
}

errcode_t ocfs2_extent_iterate_xattr(ocfs2_filesys *fs,
				     struct ocfs2_extent_list *el,
				     uint64_t last_eb_blk,
				     int flags,
				     int (*func)(ocfs2_filesys *fs,
						struct ocfs2_extent_rec *rec,
						int tree_depth,
						uint32_t ccount,
						uint64_t ref_blkno,
						int ref_recno,
						void *priv_data),
				     void *priv_data,
				     int *changed)
{
	int i;
	int iret = 0;
	errcode_t ret;
	struct extent_context ctxt;

	if (el->l_tree_depth) {
		ret = ocfs2_malloc0(sizeof(char *) * el->l_tree_depth,
				    &ctxt.eb_bufs);
		if (ret)
			goto out;

		ret = ocfs2_malloc0(fs->fs_blocksize *
				    el->l_tree_depth,
				    &ctxt.eb_bufs[0]);
		if (ret)
			goto out_eb_bufs;

		for (i = 1; i < el->l_tree_depth; i++) {
			ctxt.eb_bufs[i] = ctxt.eb_bufs[0] +
				i * fs->fs_blocksize;
		}
	} else
		ctxt.eb_bufs = NULL;

	ctxt.fs = fs;
	ctxt.func = func;
	ctxt.priv_data = priv_data;
	ctxt.flags = flags;
	ctxt.ccount = 0;
	ctxt.last_eb_blkno = 0;
	ctxt.last_eb_cpos = 0;

	ret = 0;
	iret |= extent_iterate_el(el, 0, &ctxt);
	if (iret & OCFS2_EXTENT_ERROR)
		ret = ctxt.errcode;

	if (iret & OCFS2_EXTENT_ABORT)
		goto out_abort;

	if (last_eb_blk != ctxt.last_eb_blkno) {
		last_eb_blk = ctxt.last_eb_blkno;
		iret |= OCFS2_EXTENT_CHANGED;
	}

out_abort:
	if (!ret && (iret & OCFS2_EXTENT_CHANGED))
		*changed = 1;
out_eb_bufs:
	if (ctxt.eb_bufs) {
		if (ctxt.eb_bufs[0])
			ocfs2_free(&ctxt.eb_bufs[0]);
		ocfs2_free(&ctxt.eb_bufs);
	}
out:
	return ret;
}

errcode_t ocfs2_extent_iterate_inode(ocfs2_filesys *fs,
				     struct ocfs2_dinode *inode,
				     int flags,
				     char *block_buf,
				     int (*func)(ocfs2_filesys *fs,
					         struct ocfs2_extent_rec *rec,
					         int tree_depth,
					         uint32_t ccount,
					         uint64_t ref_blkno,
					         int ref_recno,
					         void *priv_data),
					         void *priv_data)
{
	int i;
	int iret = 0;
	struct ocfs2_extent_list *el;
	errcode_t ret;
	struct extent_context ctxt;

	ret = OCFS2_ET_INODE_NOT_VALID;
	if (!(inode->i_flags & OCFS2_VALID_FL))
		goto out;

	ret = OCFS2_ET_INODE_CANNOT_BE_ITERATED;
	if (inode->i_flags & (OCFS2_SUPER_BLOCK_FL |
			      OCFS2_LOCAL_ALLOC_FL |
			      OCFS2_CHAIN_FL))
		goto out;

	el = &inode->id2.i_list;
	if (el->l_tree_depth) {
		ret = ocfs2_malloc0(sizeof(char *) * el->l_tree_depth,
				    &ctxt.eb_bufs);
		if (ret)
			goto out;

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
				i * fs->fs_blocksize;
		}
	}
	else
		ctxt.eb_bufs = NULL;

	ctxt.fs = fs;
	ctxt.func = func;
	ctxt.priv_data = priv_data;
	ctxt.flags = flags;
	ctxt.ccount = 0;
	ctxt.last_eb_blkno = 0;
	ctxt.last_eb_cpos = 0;

	ret = 0;
	iret |= extent_iterate_el(el, 0, &ctxt);
	if (iret & OCFS2_EXTENT_ERROR)
		ret = ctxt.errcode;

	if (iret & OCFS2_EXTENT_ABORT)
		goto out_abort;

	/* we can only trust ctxt.last_eb_blkno if we walked the whole tree */
	if (inode->i_last_eb_blk != ctxt.last_eb_blkno) {
		inode->i_last_eb_blk = ctxt.last_eb_blkno;
		iret |= OCFS2_EXTENT_CHANGED;
	}

out_abort:
	if (!ret && (iret & OCFS2_EXTENT_CHANGED))
		ret = ocfs2_write_inode(fs, inode->i_blkno, (char *)inode);

out_eb_bufs:
	if (ctxt.eb_bufs) {
		if (!block_buf && ctxt.eb_bufs[0])
			ocfs2_free(&ctxt.eb_bufs[0]);
		ocfs2_free(&ctxt.eb_bufs);
	}

out:
	return ret;
}

errcode_t ocfs2_extent_iterate(ocfs2_filesys *fs,
			       uint64_t blkno,
			       int flags,
			       char *block_buf,
			       int (*func)(ocfs2_filesys *fs,
					   struct ocfs2_extent_rec *rec,
					   int tree_depth,
					   uint32_t ccount,
					   uint64_t ref_blkno,
					   int ref_recno,
					   void *priv_data),
			       void *priv_data)
{
	char *buf = NULL;
	struct ocfs2_dinode *inode;
	errcode_t ret;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret)
		goto out_buf;

	inode = (struct ocfs2_dinode *)buf;

	ret = ocfs2_extent_iterate_inode(fs, inode, flags, block_buf,
					 func, priv_data);

out_buf:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

struct block_context {
	int (*func)(ocfs2_filesys *fs,
		    uint64_t blkno,
		    uint64_t bcount,
		    uint16_t ext_flags,
		    void *priv_data);
	int flags;
	struct ocfs2_dinode *inode;
	errcode_t errcode;
	void *priv_data;
};

static int block_iterate_func(ocfs2_filesys *fs,
			      struct ocfs2_extent_rec *rec,
			      int tree_depth,
			      uint32_t ccount,
			      uint64_t ref_blkno,
			      int ref_recno,
			      void *priv_data)
{
	struct block_context *ctxt = priv_data;
	uint64_t blkno, bcount, bend;
	int iret = 0;

	bcount = ocfs2_clusters_to_blocks(fs, rec->e_cpos);
	bend = bcount + ocfs2_clusters_to_blocks(fs,
					ocfs2_rec_clusters(tree_depth, rec));

	for (blkno = rec->e_blkno; bcount < bend; blkno++, bcount++) {
		if (((bcount * fs->fs_blocksize) >= ctxt->inode->i_size) &&
		    !(ctxt->flags & OCFS2_BLOCK_FLAG_APPEND))
			break;

		iret = (*ctxt->func)(fs, blkno, bcount, rec->e_flags,
				     ctxt->priv_data);
		if (iret & OCFS2_BLOCK_ABORT)
			break;
	}

	return iret;
}

errcode_t ocfs2_block_iterate_inode(ocfs2_filesys *fs,
				    struct ocfs2_dinode *inode,
				    int flags,
				    int (*func)(ocfs2_filesys *fs,
						uint64_t blkno,
						uint64_t bcount,
						uint16_t ext_flags,
						void *priv_data),
				    void *priv_data)
{
	errcode_t ret;
	struct block_context ctxt;

	ctxt.inode = inode;
	ctxt.flags = flags;
	ctxt.func = func;
	ctxt.errcode = 0;
	ctxt.priv_data = priv_data;

	ret = ocfs2_extent_iterate_inode(fs, inode,
					 OCFS2_EXTENT_FLAG_DATA_ONLY,
					 NULL,
					 block_iterate_func, &ctxt);
	return ret;
}

errcode_t ocfs2_block_iterate(ocfs2_filesys *fs,
			      uint64_t blkno,
			      int flags,
			      int (*func)(ocfs2_filesys *fs,
					  uint64_t blkno,
					  uint64_t bcount,
					  uint16_t ext_flags,
					  void *priv_data),
			      void *priv_data)
{
	struct ocfs2_dinode *inode;
	errcode_t ret;
	char *buf;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret)
		goto out_buf;

	inode = (struct ocfs2_dinode *)buf;

	ret = ocfs2_block_iterate_inode(fs, inode, flags, func, priv_data);

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
	struct ocfs2_dinode *di;
};

static int walk_extents_func(ocfs2_filesys *fs,
			     struct ocfs2_extent_rec *rec,
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

	fprintf(stdout, "0x%08"PRIX64":%02u ", ref_blkno, ref_recno);
	for (i = 0; i < pad_amount; i++)
		fprintf(stdout, " ");
	fprintf(stdout, "(%08"PRIu32", %08"PRIu32", %08"PRIu64") |"
			" + %08"PRIu32" = %08"PRIu32" / %08"PRIu32"\n",
		rec->e_cpos, ocfs2_rec_clusters(tree_depth, rec),
		rec->e_blkno, ccount,
		ccount + ocfs2_rec_clusters(tree_depth, rec),
		wi->di->i_clusters);

	if (!tree_depth &&
	    ((ccount + ocfs2_rec_clusters(tree_depth, rec)) ==
							 wi->di->i_clusters))
		fprintf(stdout, "TOTAL: %u\n", wi->di->i_clusters);

	return 0;
}

struct walk_block {
	struct ocfs2_dinode *di;
	uint64_t last_block;
	uint64_t run_first_blkno;
	uint64_t run_first_bcount;
	uint64_t run_prev_blkno;
};

static int walk_blocks_func(ocfs2_filesys *fs,
			    uint64_t blkno,
			    uint64_t bcount,
			    uint16_t ext_flags,
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
			fprintf(stdout, "(%"PRIu64"):%"PRIu64"",
				wb->run_first_bcount,
				wb->run_first_blkno);
		} else {
			fprintf(stdout, 
				"(%"PRIu64"-%"PRIu64"):%"PRIu64"-%"PRIu64"",
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
			fprintf(stdout, "(%"PRIu64"):%"PRIu64"\n",
				bcount, blkno);
		} else {
			fprintf(stdout, 
				"(%"PRIu64"-%"PRIu64"):%"PRIu64"-%"PRIu64"\n",
				wb->run_first_bcount,
				bcount,
				wb->run_first_blkno,
				blkno);
		}

		fprintf(stdout, "TOTAL: %"PRIu64"\n", bcount + 1);
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
	struct ocfs2_dinode *di;
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
		com_err(argv[0], ret, "while reading inode %"PRIu64, blkno);
		goto out_free;
	}

	di = (struct ocfs2_dinode *)buf;

	fprintf(stdout, "OCFS2 inode %"PRIu64" on \"%s\" has depth %"PRId16"\n",
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


