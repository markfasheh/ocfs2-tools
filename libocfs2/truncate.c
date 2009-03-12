/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * truncate.c
 *
 * Truncate an OCFS2 inode.  For the OCFS2 userspace library.
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

#include <string.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "ocfs2/ocfs2.h"

struct truncate_ctxt {
	uint64_t new_size_in_clusters;
	uint32_t new_i_clusters;
};

/*
 * Delete and free clusters if needed.  This only works with DEPTH_TRAVERSE.
 */
static int truncate_iterate(ocfs2_filesys *fs,
			    struct ocfs2_extent_rec *rec,
			    int tree_depth, uint32_t ccount,
			    uint64_t ref_blkno, int ref_recno,
			    void *priv_data)
{
	struct truncate_ctxt *ctxt = (struct truncate_ctxt *)priv_data;
	uint32_t len = 0, new_size_in_clusters = ctxt->new_size_in_clusters;
	uint64_t start = 0;
	errcode_t ret;
	int func_ret = OCFS2_EXTENT_ERROR;
	char *buf = NULL;
	struct ocfs2_extent_list *el = NULL;

	if ((rec->e_cpos + ocfs2_rec_clusters(tree_depth, rec)) <=
							new_size_in_clusters)
		return 0;

	if (rec->e_cpos >= new_size_in_clusters) {
		/* the rec is entirely outside the new size, free it */
		if (!tree_depth) {
			start = rec->e_blkno;
			len = ocfs2_rec_clusters(tree_depth, rec);
		} else {
			/* here we meet with a full empty extent block, delete
			 * it. The extent list it contains should already be
			 * iterated and all the clusters have been freed.
			 */
			ret = ocfs2_delete_extent_block(fs, rec->e_blkno);
			if (ret)
				goto bail;
		}

		memset(rec, 0, sizeof(struct ocfs2_extent_rec));
	} else {
		/* we're truncating into the middle of the rec */
		len = rec->e_cpos +
			 ocfs2_rec_clusters(tree_depth, rec);
		len -= new_size_in_clusters;
		if (!tree_depth) {
			ocfs2_set_rec_clusters(tree_depth, rec,
				 	new_size_in_clusters - rec->e_cpos);
			start = rec->e_blkno +
				ocfs2_clusters_to_blocks(fs,
						ocfs2_rec_clusters(tree_depth,
								   rec));
		} else {
			ocfs2_set_rec_clusters(tree_depth, rec,
					new_size_in_clusters - rec->e_cpos);
			/*
			 * For a sparse file, we may meet with another
			 * situation here:
			 * The start of the left most extent rec is greater
			 * than the new size we truncate the file to, but the
			 * start of the extent block is less than that size.
			 * In this case, actually all the extent records in
			 * this extent block have been removed. So we have
			 * to remove the extent block also.
			 * In this function, we have to reread the extent list
			 * to see whether the extent block is empty or not.
			 */
			ret = ocfs2_malloc_block(fs->fs_io, &buf);
			if (ret)
				goto bail;

			ret = ocfs2_read_extent_block(fs, rec->e_blkno, buf);
			if (ret)
				goto bail;

			el = &((struct ocfs2_extent_block *)buf)->h_list;
			if (el->l_next_free_rec == 0) {
				ret = ocfs2_delete_extent_block(fs, rec->e_blkno);
				if (ret)
					goto bail;
				memset(rec, 0, sizeof(struct ocfs2_extent_rec));
			}
		}
	}

	if (start) {
		ret = ocfs2_free_clusters(fs, len, start);
		if (ret)
			goto bail;
		ctxt->new_i_clusters -= len;
	}

	func_ret =  OCFS2_EXTENT_CHANGED;
bail:
	if (buf)
		ocfs2_free(&buf);
	return func_ret;
}

/*
 * Zero the area past i_size but still within an allocated
 * cluster. This avoids exposing nonzero data on subsequent file
 * extends.
 */
static errcode_t ocfs2_zero_tail_for_truncate(ocfs2_cached_inode *ci,
					      uint64_t new_size)
{
	errcode_t ret;
	char *buf = NULL;
	ocfs2_filesys *fs = ci->ci_fs;
	uint64_t start_blk, p_blkno, contig_blocks, start_off;
	int count, byte_counts, bpc = fs->fs_clustersize /fs->fs_blocksize;

	if (new_size == 0)
		return 0;

	start_blk = new_size / fs->fs_blocksize;

	ret = ocfs2_extent_map_get_blocks(ci, start_blk, 1,
					  &p_blkno, &contig_blocks, NULL);
	if (ret)
		goto out;

	/* Tail is a hole. */
	if (!p_blkno)
		goto out;

	/* calculate the total blocks we need to empty. */
	count = bpc - (p_blkno & (bpc - 1));
	ret = ocfs2_malloc_blocks(fs->fs_io, count, &buf);
	if (ret)
		goto out;

	ret = ocfs2_read_blocks(fs, p_blkno, count, buf);
	if (ret)
		goto out;

	/* empty the content after the new_size and within the same cluster. */
	start_off = new_size % fs->fs_blocksize;
	byte_counts = count * fs->fs_blocksize - start_off;
	memset(buf + start_off, 0, byte_counts);

	ret = io_write_block(fs->fs_io, p_blkno, count, buf);

out:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

/* XXX care about zeroing new clusters and final partially truncated 
 * clusters */
errcode_t ocfs2_truncate(ocfs2_filesys *fs, uint64_t ino, uint64_t new_i_size)
{
	errcode_t ret;
	uint32_t new_clusters;
	ocfs2_cached_inode *ci = NULL;

	ret = ocfs2_read_cached_inode(fs, ino, &ci);
	if (ret)
		goto out;

	if (ci->ci_inode->i_size == new_i_size)
		goto out;

	if (ci->ci_inode->i_size < new_i_size)
		ret = ocfs2_extend_file(fs, ino, new_i_size);
	else {
		ret = ocfs2_zero_tail_and_truncate(fs, ci, new_i_size,
						   &new_clusters);
		if (ret)
			goto out;

		ci->ci_inode->i_clusters = new_clusters;

		/* now all the clusters and extent blocks are freed.
		 * only when the file's content is empty, should the tree depth
		 * change.
		 */
		if (new_clusters == 0)
			ci->ci_inode->id2.i_list.l_tree_depth = 0;

		ci->ci_inode->i_size = new_i_size;
		ret = ocfs2_write_cached_inode(fs, ci);
	}
out:
	if (ci)
		ocfs2_free_cached_inode(fs, ci);
	return ret;
}

/*
 * This fucntion will truncate the file's cluster which exceeds
 * the cluster where new_size resides in and empty all the
 * bytes in the same cluster which exceeds new_size.
 */
errcode_t ocfs2_zero_tail_and_truncate(ocfs2_filesys *fs,
				       ocfs2_cached_inode *ci,
				       uint64_t new_i_size,
				       uint32_t *new_clusters)
{
	errcode_t ret;
	uint64_t new_size_in_blocks;
	struct truncate_ctxt ctxt;

	new_size_in_blocks = ocfs2_blocks_in_bytes(fs, new_i_size);
	ctxt.new_i_clusters = ci->ci_inode->i_clusters;
	ctxt.new_size_in_clusters =
			ocfs2_clusters_in_blocks(fs, new_size_in_blocks);

	ret = ocfs2_extent_iterate_inode(fs, ci->ci_inode,
					 OCFS2_EXTENT_FLAG_DEPTH_TRAVERSE,
					 NULL, truncate_iterate,
					 &ctxt);
	if (ret)
		goto out;

	ret = ocfs2_zero_tail_for_truncate(ci, new_i_size);
	if (ret)
		goto out;

	if (new_clusters)
		*new_clusters = ctxt.new_i_clusters;
out:
	return ret;
}

errcode_t ocfs2_xattr_value_truncate(ocfs2_filesys *fs,
				     struct ocfs2_xattr_value_root *xv)
{
	struct truncate_ctxt ctxt;
	int changed;
	struct ocfs2_extent_list *el = &xv->xr_list;

	ctxt.new_i_clusters = xv->xr_clusters;
	ctxt.new_size_in_clusters = 0;

	return ocfs2_extent_iterate_xattr(fs, el, xv->xr_last_eb_blk,
					OCFS2_EXTENT_FLAG_DEPTH_TRAVERSE,
					truncate_iterate,
					&ctxt, &changed);
}

errcode_t ocfs2_xattr_tree_truncate(ocfs2_filesys *fs,
				    struct ocfs2_xattr_tree_root *xt)
{
	struct truncate_ctxt ctxt;
	int changed;
	struct ocfs2_extent_list *el = &xt->xt_list;

	ctxt.new_i_clusters = xt->xt_clusters;
	ctxt.new_size_in_clusters = 0;

	return ocfs2_extent_iterate_xattr(fs, el, xt->xt_last_eb_blk,
					OCFS2_EXTENT_FLAG_DEPTH_TRAVERSE,
					truncate_iterate,
					&ctxt, &changed);
}

#ifdef DEBUG_EXE
#include <stdlib.h>
#include <getopt.h>
#include <inttypes.h>

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
		"Usage: debug_truncate -i <ino_blkno> -s <new_size> device\n");
}

extern int opterr, optind;
extern char *optarg;

int main(int argc, char *argv[])
{
	errcode_t ret;
	int c;
	uint64_t blkno = 0, new_size = 0;
	ocfs2_filesys *fs;
	char *device;

	initialize_ocfs_error_table();

	while ((c = getopt(argc, argv, "i:s:")) != EOF) {
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

			case 's':
				new_size = read_number(optarg);
				break;

			default:
				print_usage();
				return 1;
				break;
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "Missing device name\n");
		print_usage();
		return 1;
	}
	device = argv[optind];

	if (!blkno || !new_size) {
		print_usage();
		return 1;
	}

	ret = ocfs2_open(device, OCFS2_FLAG_RW, 0, 0, &fs);
	if (ret) {
		com_err(argv[0], ret,
			"while opening file \"%s\"", device);
		return ret;
	}

	ret = ocfs2_truncate(fs, blkno, new_size);
	if (ret)
		com_err(argv[0], ret, "while truncating inode %"PRIu64, blkno);

	ocfs2_close(fs);

	return ret;
}
#endif  /* DEBUG_EXE */
