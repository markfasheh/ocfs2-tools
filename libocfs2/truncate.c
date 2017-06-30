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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301 USA.
 */

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE

#include <string.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <assert.h>
#include <errno.h>
#include "ocfs2/ocfs2.h"
#include "ocfs2/byteorder.h"

struct truncate_ctxt {
	uint64_t ino;
	uint64_t new_size_in_clusters;
	uint32_t new_i_clusters;
	errcode_t (*free_clusters)(ocfs2_filesys *fs,
				   uint32_t len,
				   uint64_t start_blkno,
				   void *free_data);
	void *free_data;
};

static int ocfs2_truncate_clusters(ocfs2_filesys *fs,
				   struct ocfs2_extent_rec *rec,
				   uint64_t ino,
				   uint32_t len,
				   uint64_t start)
{
	if (!ocfs2_refcount_tree(OCFS2_RAW_SB(fs->fs_super)) ||
	    !(rec->e_flags & OCFS2_EXT_REFCOUNTED))
		return ocfs2_free_clusters(fs, len, start);

	assert(ino);

	return ocfs2_decrease_refcount(fs, ino,
				ocfs2_blocks_to_clusters(fs, start),
				len, 1);
}

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
	int cleanup_rec = 0;

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

		cleanup_rec = 1;
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
				cleanup_rec = 1;
			}
		}
	}

	if (start) {
		if (ctxt->free_clusters)
			ret = ctxt->free_clusters(fs, len, start,
						  ctxt->free_data);
		else
			ret = ocfs2_truncate_clusters(fs, rec, ctxt->ino,
						      len, start);
		if (ret)
			goto bail;
		ctxt->new_i_clusters -= len;
	}

	func_ret =  OCFS2_EXTENT_CHANGED;
bail:
	if (cleanup_rec)
		memset(rec, 0, sizeof(struct ocfs2_extent_rec));
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
	uint16_t ext_flags;

	if (new_size == 0)
		return 0;

	start_blk = new_size / fs->fs_blocksize;

	ret = ocfs2_extent_map_get_blocks(ci, start_blk, 1,
					  &p_blkno, &contig_blocks, &ext_flags);
	if (ret)
		goto out;

	/* Tail is a hole. */
	if (!p_blkno)
		goto out;

	if (ext_flags & OCFS2_EXT_REFCOUNTED) {
		uint32_t cpos = ocfs2_blocks_to_clusters(fs, start_blk);
		ret = ocfs2_refcount_cow(ci, cpos, 1, cpos + 1);
		if (ret)
			goto out;

		ret = ocfs2_extent_map_get_blocks(ci, start_blk, 1,
						  &p_blkno, &contig_blocks,
						  &ext_flags);
		if (ret)
			goto out;

		assert(!(ext_flags & OCFS2_EXT_REFCOUNTED) && p_blkno);

	}

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

/*
 * This function will truncate the file's cluster which exceeds
 * the cluster where new_size resides in and empty all the
 * bytes in the same cluster which exceeds new_size.
 */
static errcode_t ocfs2_zero_tail_and_truncate_full(ocfs2_filesys *fs,
						   ocfs2_cached_inode *ci,
						   uint64_t new_i_size,
						   uint32_t *new_clusters,
			      errcode_t (*free_clusters)(ocfs2_filesys *fs,
							 uint32_t len,
							 uint64_t start,
							 void *free_data),
						   void *free_data)
{
	errcode_t ret;
	uint64_t new_size_in_blocks;
	struct truncate_ctxt ctxt;

	new_size_in_blocks = ocfs2_blocks_in_bytes(fs, new_i_size);
	ctxt.ino = ci->ci_blkno;
	ctxt.new_i_clusters = ci->ci_inode->i_clusters;
	ctxt.new_size_in_clusters =
			ocfs2_clusters_in_blocks(fs, new_size_in_blocks);
	ctxt.free_clusters = free_clusters;
	ctxt.free_data = free_data;

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

errcode_t ocfs2_zero_tail_and_truncate(ocfs2_filesys *fs,
				       ocfs2_cached_inode *ci,
				       uint64_t new_i_size,
				       uint32_t *new_clusters)
{
	return ocfs2_zero_tail_and_truncate_full(fs, ci, new_i_size,
						 new_clusters, NULL, NULL);
}

/*
 * NOTE: ocfs2_truncate_inline() also handles fast symlink,
 * since truncating for inline file and fasy symlink are
 * almost the same thing per se.
 */
errcode_t ocfs2_truncate_inline(ocfs2_filesys *fs, uint64_t ino,
				uint64_t new_i_size)
{
	errcode_t ret = 0;
	char *buf = NULL;
	struct ocfs2_dinode *di = NULL;
	struct ocfs2_inline_data *idata = NULL;

	if (!(fs->fs_flags & OCFS2_FLAG_RW))
		return OCFS2_ET_RO_FILESYS;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_read_inode(fs, ino, buf);
	if (ret)
		goto out_free_buf;

	di = (struct ocfs2_dinode *)buf;
	if (di->i_size < new_i_size) {
		ret = EINVAL;
		goto out_free_buf;
	}

	idata = &di->id2.i_data;

	if (!(di->i_dyn_features & OCFS2_INLINE_DATA_FL) &&
	    !(S_ISLNK(di->i_mode) && !di->i_clusters)) {
		ret = EINVAL;
		goto out_free_buf;
	}

	if (di->i_dyn_features & OCFS2_INLINE_DATA_FL)
		memset(idata->id_data + new_i_size, 0, di->i_size - new_i_size);
	else
		memset(di->id2.i_symlink + new_i_size, 0,
		       di->i_size - new_i_size);

	di->i_size = new_i_size;

	ret = ocfs2_write_inode(fs, ino, buf);

out_free_buf:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

/* XXX care about zeroing new clusters and final partially truncated 
 * clusters */
errcode_t ocfs2_truncate_full(ocfs2_filesys *fs, uint64_t ino,
			      uint64_t new_i_size,
			      errcode_t (*free_clusters)(ocfs2_filesys *fs,
							 uint32_t len,
							 uint64_t start,
							 void *free_data),
			      void *free_data)
{
	errcode_t ret;
	uint32_t new_clusters;
	ocfs2_cached_inode *ci = NULL;

	ret = ocfs2_read_cached_inode(fs, ino, &ci);
	if (ret)
		goto out;

	/* in case of dio crashed, force do trucate since blocks may already
	 * be allocated
	 */
	if (ci->ci_inode->i_flags & cpu_to_le32(OCFS2_DIO_ORPHANED_FL)) {
		ci->ci_inode->i_flags &= ~cpu_to_le32(OCFS2_DIO_ORPHANED_FL);
		ci->ci_inode->i_dio_orphaned_slot = 0;
		new_i_size = ci->ci_inode->i_size;
		goto truncate;
	}

	if (ci->ci_inode->i_size == new_i_size)
		goto out;

	if (ci->ci_inode->i_size < new_i_size) {
		ret = ocfs2_extend_file(fs, ino, new_i_size);
		goto out;
	}

truncate:
	if ((S_ISLNK(ci->ci_inode->i_mode) && !ci->ci_inode->i_clusters) ||
	    (ci->ci_inode->i_dyn_features & OCFS2_INLINE_DATA_FL))
		ret = ocfs2_truncate_inline(fs, ino, new_i_size);
	else {
		ret = ocfs2_zero_tail_and_truncate_full(fs, ci, new_i_size,
							&new_clusters,
							free_clusters,
							free_data);
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

	if (!ret && !new_i_size && ci->ci_inode->i_refcount_loc &&
		(ci->ci_inode->i_dyn_features & OCFS2_HAS_REFCOUNT_FL))
		ret = ocfs2_detach_refcount_tree(fs, ino, ci->ci_inode->i_refcount_loc);
out:
	if (ci)
		ocfs2_free_cached_inode(fs, ci);
	return ret;
}

errcode_t ocfs2_truncate(ocfs2_filesys *fs, uint64_t ino, uint64_t new_i_size)
{
	return ocfs2_truncate_full(fs, ino, new_i_size, NULL, NULL);
}

errcode_t ocfs2_xattr_value_truncate(ocfs2_filesys *fs, uint64_t ino,
				     struct ocfs2_xattr_value_root *xv)
{
	struct truncate_ctxt ctxt;
	int changed;
	struct ocfs2_extent_list *el = &xv->xr_list;

	ctxt.ino = ino;
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

	/*
	 * ino is used to find refcount tree, as we never use refcount
	 * in xattr tree, so set it to 0.
	 */
	ctxt.ino = 0;
	ctxt.new_i_clusters = xt->xt_clusters;
	ctxt.new_size_in_clusters = 0;

	return ocfs2_extent_iterate_xattr(fs, el, xt->xt_last_eb_blk,
					OCFS2_EXTENT_FLAG_DEPTH_TRAVERSE,
					truncate_iterate,
					&ctxt, &changed);
}


errcode_t ocfs2_dir_indexed_tree_truncate(ocfs2_filesys *fs,
					struct ocfs2_dx_root_block *dx_root)
{
	struct truncate_ctxt ctxt;

	memset(&ctxt, 0, sizeof (struct truncate_ctxt));
	ctxt.new_i_clusters = dx_root->dr_clusters;
	ctxt.new_size_in_clusters = 0;

	return ocfs2_extent_iterate_dx_root(fs, dx_root,
					OCFS2_EXTENT_FLAG_DEPTH_TRAVERSE,
					NULL, truncate_iterate,	&ctxt);
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
