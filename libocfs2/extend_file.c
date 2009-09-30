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

#include <string.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <inttypes.h>
#include <errno.h>
#include <assert.h>
#include "ocfs2/ocfs2.h"
#include "extent_tree.h"

/*
 * Insert an extent into an inode btree.
 */
errcode_t ocfs2_inode_insert_extent(ocfs2_filesys *fs, uint64_t ino,
				    uint32_t cpos, uint64_t c_blkno,
				    uint32_t clusters, uint16_t flag)
{
	errcode_t ret;
	ocfs2_cached_inode *ci = NULL;

	ret = ocfs2_read_cached_inode(fs, ino, &ci);
	if (ret)
		goto bail;

	ret = ocfs2_cached_inode_insert_extent(ci, cpos, c_blkno,
					       clusters, flag);
	if (ret)
		goto bail;

	ret = ocfs2_write_cached_inode(fs, ci);

bail:
	if (ci)
		ocfs2_free_cached_inode(fs, ci);

	return ret;
}

errcode_t ocfs2_cached_inode_insert_extent(ocfs2_cached_inode *ci,
					   uint32_t cpos, uint64_t c_blkno,
					   uint32_t clusters, uint16_t flag)
{
	struct ocfs2_extent_tree et;

	ocfs2_init_dinode_extent_tree(&et, ci->ci_fs, (char *)ci->ci_inode,
				      ci->ci_inode->i_blkno);

	return ocfs2_tree_insert_extent(ci->ci_fs, &et, cpos, c_blkno,
					clusters, flag);
}

errcode_t ocfs2_cached_inode_extend_allocation(ocfs2_cached_inode *ci,
					       uint32_t new_clusters)
{
	errcode_t ret = 0;
	uint32_t n_clusters = 0, cpos;
	uint64_t blkno, file_size;
	ocfs2_filesys *fs = ci->ci_fs;

	file_size = ci->ci_inode->i_size;
	cpos = (file_size + fs->fs_clustersize - 1) / fs->fs_clustersize;
	while (new_clusters) {
		n_clusters = 1;
		ret = ocfs2_new_clusters(fs, 1, new_clusters, &blkno,
					 &n_clusters);
		if (ret)
			break;

		ret = ocfs2_cached_inode_insert_extent(ci, cpos, blkno,
						       n_clusters, 0);
		if (ret) {
			/* XXX: We don't wan't to overwrite the error
			 * from insert_extent().  But we probably need
			 * to BE LOUDLY UPSET. */
			ocfs2_free_clusters(fs, n_clusters, blkno);
			break;
		}

	 	new_clusters -= n_clusters;
		cpos += n_clusters;
	}
	return ret;
}

errcode_t ocfs2_extend_allocation(ocfs2_filesys *fs, uint64_t ino,
				  uint32_t new_clusters)
{
	errcode_t ret;
	ocfs2_cached_inode *ci = NULL;

	ret = ocfs2_read_cached_inode(fs, ino, &ci);
	if (ret)
		goto bail;

	ret = ocfs2_cached_inode_extend_allocation(ci, new_clusters);
	if (ret)
		goto bail;

	ret = ocfs2_write_cached_inode(fs, ci);
bail:
	if (ci)
		ocfs2_free_cached_inode(fs, ci);

	return ret;
}

errcode_t ocfs2_extend_file(ocfs2_filesys *fs, uint64_t ino, uint64_t new_size)
{
	errcode_t ret = 0;
	char *buf = NULL;
	struct ocfs2_dinode* di = NULL;

	if (!(fs->fs_flags & OCFS2_FLAG_RW))
		return OCFS2_ET_RO_FILESYS;
	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_read_inode(fs, ino, buf);
	if (ret)
		goto out_free_buf;

	di = (struct ocfs2_dinode *)buf;
	if (di->i_size >= new_size) {
		ret = EINVAL;
		goto out_free_buf;
	}

	di->i_size = new_size;

	ret = ocfs2_write_inode(fs, ino, buf);

out_free_buf:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

errcode_t ocfs2_allocate_unwritten_extents(ocfs2_filesys *fs, uint64_t ino,
					   uint64_t offset, uint64_t len)
{
	errcode_t ret = 0;
	uint32_t n_clusters = 0, cpos;
	uint64_t p_blkno, v_blkno, v_end, contig_blocks, wanted_blocks;
	ocfs2_cached_inode *ci = NULL;

	if (!(fs->fs_flags & OCFS2_FLAG_RW))
		return OCFS2_ET_RO_FILESYS;

	if (!ocfs2_writes_unwritten_extents(OCFS2_RAW_SB(fs->fs_super)))
		return OCFS2_ET_RO_UNSUPP_FEATURE;

	ret = ocfs2_read_cached_inode(fs, ino, &ci);
	if (ret)
		goto out;

	if (!(ci->ci_inode->i_flags & OCFS2_VALID_FL))
		return OCFS2_ET_INODE_NOT_VALID;

	if (ci->ci_inode->i_flags & OCFS2_SYSTEM_FL)
		return OCFS2_ET_INVALID_ARGUMENT;

	if (!S_ISREG(ci->ci_inode->i_mode))
		return OCFS2_ET_INVALID_ARGUMENT;

	v_blkno = offset / fs->fs_blocksize;
	v_end = (offset + len - 1) / fs->fs_blocksize;

	while (v_blkno <= v_end) {
		ret = ocfs2_extent_map_get_blocks(ci, v_blkno, 1,
						  &p_blkno, &contig_blocks,
						  NULL);
		if (p_blkno) {
			v_blkno += contig_blocks;
			continue;
		}

		/*
		 * There is a hole, so we have to allocate the space and
		 * insert the unwritten extents.
		 */
		wanted_blocks = ocfs2_min(contig_blocks, v_end - v_blkno + 1);
		n_clusters = ocfs2_clusters_in_blocks(fs, wanted_blocks);
		ret = ocfs2_new_clusters(fs, 1, n_clusters, &p_blkno,
					 &n_clusters);
		if (ret || n_clusters == 0)
			break;

		cpos = ocfs2_blocks_to_clusters(fs, v_blkno);
		ret = ocfs2_cached_inode_insert_extent(ci, cpos,
						       p_blkno, n_clusters,
						       OCFS2_EXT_UNWRITTEN);
		if (ret) {
			/*
			 * XXX: We don't wan't to overwrite the error
			 * from insert_extent().  But we probably need
			 * to BE LOUDLY UPSET.
			 */
			ocfs2_free_clusters(fs, n_clusters, p_blkno);
			goto out;
		}

		/* save up what we have done. */
		ret = ocfs2_write_cached_inode(fs, ci);
		if (ret)
			goto out;

		v_blkno = ocfs2_clusters_to_blocks(fs, cpos + n_clusters);
	}

	if (ci->ci_inode->i_size <= offset + len) {
		ci->ci_inode->i_size = offset + len;
		ret = ocfs2_write_cached_inode(fs, ci);
	}

out:
	if (ci)
		ocfs2_free_cached_inode(fs, ci);

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
