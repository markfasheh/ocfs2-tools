/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * extent_map.c
 *
 * In-memory extent map for the OCFS2 userspace library.
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
#define _DEFAULT_SOURCE
#define _LARGEFILE64_SOURCE

#include <string.h>
#include <inttypes.h>
#include <assert.h>

#include "ocfs2/ocfs2.h"

#include "extent_map.h"

/*
 * Return the 1st index within el which contains an extent start
 * larger than v_cluster.
 */
static int ocfs2_search_for_hole_index(struct ocfs2_extent_list *el,
				       uint32_t v_cluster)
{
	int i;
	struct ocfs2_extent_rec *rec;

	for(i = 0; i < el->l_next_free_rec; i++) {
		rec = &el->l_recs[i];

		if (v_cluster < rec->e_cpos)
			break;
	}

	return i;
}

/*
 * Figure out the size of a hole which starts at v_cluster within the given
 * extent list.
 *
 * If there is no more allocation past v_cluster, we return the maximum
 * cluster size minus v_cluster.
 *
 * If we have in-inode extents, then el points to the dinode list and
 * eb_buf is NULL. Otherwise, eb_buf should point to the extent block
 * containing el.
 */
static int ocfs2_figure_hole_clusters(ocfs2_cached_inode *cinode,
				      struct ocfs2_extent_list *el,
				      char *eb_buf,
				      uint32_t v_cluster,
				      uint32_t *num_clusters)
{
	int ret, i;
	char *next_eb_buf = NULL;
	struct ocfs2_extent_block *eb, *next_eb;

	i = ocfs2_search_for_hole_index(el, v_cluster);

	if (i == el->l_next_free_rec && eb_buf) {
		eb = (struct ocfs2_extent_block *)eb_buf;

		/*
		 * Check the next leaf for any extents.
		 */
		if (eb->h_next_leaf_blk == 0)
			goto no_more_extents;

		ret = ocfs2_malloc_block(cinode->ci_fs->fs_io, &next_eb_buf);
		if (ret)
			goto out;

		ret = ocfs2_read_extent_block(cinode->ci_fs,
					      eb->h_next_leaf_blk, next_eb_buf);
		if (ret)
			goto out;

		next_eb = (struct ocfs2_extent_block *)next_eb_buf;

		el = &next_eb->h_list;

		i = ocfs2_search_for_hole_index(el, v_cluster);
		if (i > 0) {
			if ((i > 1) || ocfs2_rec_clusters(el->l_tree_depth,
							  &el->l_recs[0])) {
				ret = OCFS2_ET_CORRUPT_EXTENT_BLOCK;
			goto out;
			}
		}
	}

no_more_extents:
	if (i == el->l_next_free_rec) {
		/*
		 * We're at the end of our existing allocation. Just
		 * return the maximum number of clusters we could
		 * possibly allocate.
		 */
		*num_clusters = UINT32_MAX - v_cluster;
	} else
		*num_clusters = el->l_recs[i].e_cpos - v_cluster;

	ret = 0;
out:
	if (next_eb_buf)
		ocfs2_free(&next_eb_buf);
	return ret;
}

errcode_t ocfs2_get_clusters(ocfs2_cached_inode *cinode,
			     uint32_t v_cluster,
			     uint32_t *p_cluster,
			     uint32_t *num_clusters,
			     uint16_t *extent_flags)
{
	int i;
	uint16_t flags = 0;
	errcode_t ret =  0;
	ocfs2_filesys *fs = cinode->ci_fs;
	struct ocfs2_dinode *di;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_list *el;
	struct ocfs2_extent_rec *rec;
	char *eb_buf = NULL;
	uint32_t coff;

	di = cinode->ci_inode;
	el = &di->id2.i_list;

	if (el->l_tree_depth) {
		ret = ocfs2_find_leaf(fs, di, v_cluster, &eb_buf);
		if (ret)
			goto out;

		eb = (struct ocfs2_extent_block *) eb_buf;
		el = &eb->h_list;

		if (el->l_tree_depth) {
			ret = OCFS2_ET_CORRUPT_EXTENT_BLOCK;
			goto out;
		}
	}

	i = ocfs2_search_extent_list(el, v_cluster);
	if (i == -1) {
		/*
		 * A hole was found. Return some canned values that
		 * callers can key on. If asked for, num_clusters will
		 * be populated with the size of the hole.

		 */
		*p_cluster = 0;
		if (num_clusters) {
			ret = ocfs2_figure_hole_clusters(cinode, el, eb_buf,
							 v_cluster,
							 num_clusters);
			if (ret)
				goto out;
		}
	} else {
		rec = &el->l_recs[i];

		assert(v_cluster >= rec->e_cpos);

		if (!rec->e_blkno) {
			ret = OCFS2_ET_BAD_BLKNO;
			goto out;
		}

		coff = v_cluster - rec->e_cpos;

		*p_cluster = ocfs2_blocks_to_clusters(fs, rec->e_blkno);
		*p_cluster = *p_cluster + coff;

		if (num_clusters)
			*num_clusters = ocfs2_rec_clusters(el->l_tree_depth,
							   rec) - coff;

		flags = rec->e_flags;
	}

	if (extent_flags)
		*extent_flags = flags;

out:
	if (eb_buf)
		ocfs2_free(&eb_buf);
	return ret;
}

errcode_t ocfs2_xattr_get_clusters(ocfs2_filesys *fs,
				   struct ocfs2_extent_list *el,
				   uint64_t el_blkno,
				   char *el_blk,
				   uint32_t v_cluster,
				   uint32_t *p_cluster,
				   uint32_t *num_clusters,
				   uint16_t *extent_flags)
{
	int i;
	errcode_t ret =  0;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_rec *rec;
	char *eb_buf = NULL;
	uint32_t coff;

	if (el->l_tree_depth) {
		ret = ocfs2_tree_find_leaf(fs, el, el_blkno, el_blk,
					   v_cluster, &eb_buf);
		if (ret)
			goto out;

		eb = (struct ocfs2_extent_block *)eb_buf;
		el = &eb->h_list;

		if (el->l_tree_depth) {
			ret = OCFS2_ET_CORRUPT_EXTENT_BLOCK;
			goto out;
		}
	}

	i = ocfs2_search_extent_list(el, v_cluster);
	if (i == -1) {
		ret = -1;
		goto out;
	} else {
		rec = &el->l_recs[i];

		assert(v_cluster >= rec->e_cpos);

		if (!rec->e_blkno) {
			ret = OCFS2_ET_BAD_BLKNO;
			goto out;
		}

		coff = v_cluster - rec->e_cpos;

		*p_cluster = ocfs2_blocks_to_clusters(fs, rec->e_blkno);
		*p_cluster = *p_cluster + coff;

		if (num_clusters)
			*num_clusters = ocfs2_rec_clusters(el->l_tree_depth,
							   rec) - coff;
		if (extent_flags)
			*extent_flags = rec->e_flags;
	}
out:
	if (eb_buf)
		ocfs2_free(&eb_buf);
	return ret;
}

errcode_t ocfs2_extent_map_get_blocks(ocfs2_cached_inode *cinode,
				      uint64_t v_blkno, int count,
				      uint64_t *p_blkno, uint64_t *ret_count,
				      uint16_t *extent_flags)
{
	errcode_t ret;
	int bpc;
	uint32_t cpos, num_clusters = -1, p_cluster = -1;
	uint64_t boff = 0;
	ocfs2_filesys *fs = cinode->ci_fs;

	bpc = ocfs2_clusters_to_blocks(fs, 1);
	cpos = ocfs2_blocks_to_clusters(fs, v_blkno);

	ret = ocfs2_get_clusters(cinode, cpos, &p_cluster,
				 &num_clusters, extent_flags);
	if (ret)
		goto out;

	/*
	 * p_cluster == 0 indicates a hole.
	 */
	if (p_cluster) {
		boff = ocfs2_clusters_to_blocks(fs, p_cluster);
		boff += (v_blkno & (uint64_t)(bpc - 1));
	}

	*p_blkno = boff;

	if (ret_count) {
		*ret_count = ocfs2_clusters_to_blocks(fs, num_clusters);
		*ret_count -= v_blkno & (uint64_t)(bpc - 1);
	}

out:
	return ret;
}

errcode_t ocfs2_get_last_cluster_offset(ocfs2_filesys *fs,
					struct ocfs2_dinode *di,
					uint32_t *v_cluster)
{
	errcode_t ret = 0;
	char *buf = NULL;
	struct ocfs2_extent_list *el = NULL;
	struct ocfs2_extent_rec *er = NULL;

	el = &di->id2.i_list;

	*v_cluster = 0;
	if (!el->l_next_free_rec)
		return 0;

	if (el->l_tree_depth) {
		ret = ocfs2_malloc_block(fs->fs_io, &buf);
		if (ret)
			goto bail;

		ret = ocfs2_read_extent_block(fs, di->i_last_eb_blk, buf);
		if (ret)
			goto bail;

		el = &((struct ocfs2_extent_block *)buf)->h_list;

		if (!el->l_next_free_rec ||
		    (el->l_next_free_rec == 1 &&
		     ocfs2_is_empty_extent(&el->l_recs[0]))) {
			ret = OCFS2_ET_CORRUPT_EXTENT_BLOCK;
			goto bail;
		}
	}

	er = &el->l_recs[el->l_next_free_rec - 1];

	*v_cluster = er->e_cpos + er->e_leaf_clusters - 1;

bail:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

#ifdef DEBUG_EXE
#include <stdlib.h>
#include <getopt.h>
#include <limits.h>

enum debug_op {
	OP_NONE = 0,
	OP_LOOKUP_BLOCK,
};

static uint64_t read_number(const char *num)
{
	uint64_t val;
	char *ptr;

	val = strtoull(num, &ptr, 0);
	if (!ptr || *ptr)
		return 0;

	return val;
}

static int read_b_numbers(const char *num, uint64_t *blkno, int *count)
{
	uint64_t val;
	char *ptr;

	val = strtoull(num, &ptr, 0);
	if (!ptr)
		return 1;
	if (*ptr != ':')
		return 1;
	*blkno = val;

	ptr++;

	val = strtoull(ptr, &ptr, 0);
	if (!ptr || *ptr)
		return 1;
	if (val > INT_MAX)
		return 1;
	*count = (int)val;

	return 0;
}

static void print_usage(void)
{
	fprintf(stderr,
		"Usage: extent_map -i <inode_blkno> -b <blkno>:<blocks> <filename>\n");
}

extern int opterr, optind;
extern char *optarg;

int main(int argc, char *argv[])
{
	errcode_t ret;
	uint64_t blkno, contig, blkoff = 0;
	uint16_t ext_flags;
	int count = 0;
	int c, op = 0;
	char *filename;
	ocfs2_filesys *fs;
	ocfs2_cached_inode *cinode;

	blkno = OCFS2_SUPER_BLOCK_BLKNO;

	initialize_ocfs_error_table();

	while ((c = getopt(argc, argv, "i:b:")) != EOF) {
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

			case 'b':
				if (op) {
					fprintf(stderr, "Cannot specify more than one operation\n");
					print_usage();
					return 1;
				}
				if (read_b_numbers(optarg,
						   &blkoff, &count)) {
					fprintf(stderr, "Invalid block range: %s\n", optarg);
					print_usage();
					return 1;
				}
				op = OP_LOOKUP_BLOCK;
				break;

			default:
				print_usage();
				return 1;
				break;
		}
	}

	if (!op) {
		fprintf(stderr, "Missing operation\n");
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

	ret = ocfs2_read_cached_inode(fs, blkno, &cinode);
	if (ret) {
		com_err(argv[0], ret, "while reading inode %"PRIu64, blkno);
		goto out_close;
	}

	fprintf(stdout, "OCFS2 inode %"PRIu64" on \"%s\" has depth %"PRId16"\n",
		blkno, filename,
		cinode->ci_inode->id2.i_list.l_tree_depth);

	ret = ocfs2_extent_map_get_blocks(cinode,
					  blkoff,
					  count,
					  &blkno,
					  &contig,
					  &ext_flags);
	if (ret) {
		com_err(argv[0], ret,
			"looking up block range %"PRIu64":%d", blkoff, count);
		goto out_free;
	}
	fprintf(stdout, "Lookup of block range %"PRIu64":%d returned %"PRIu64":%"PRIu64"\n",
		blkoff, count, blkno, contig);

out_free:
	ocfs2_free_cached_inode(fs, cinode);

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


