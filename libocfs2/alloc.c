/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * alloc.c
 *
 * Allocate inodes, extent_blocks, and actual data space.  Part of the
 * OCFS2 userspace library.
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
#include <inttypes.h>

#include "ocfs2.h"

static errcode_t ocfs2_chain_alloc_with_io(ocfs2_filesys *fs,
					   ocfs2_cached_inode *cinode,
					   uint64_t *blkno)
{
	errcode_t ret;

	if (!cinode->ci_chains) {
		ret = ocfs2_load_chain_allocator(fs, cinode);
		if (ret)
			return ret;
	}

	ret = ocfs2_chain_alloc(fs, cinode, blkno);
	if (ret)
		return ret;

	return ocfs2_write_chain_allocator(fs, cinode);
}

static errcode_t ocfs2_chain_free_with_io(ocfs2_filesys *fs,
					  ocfs2_cached_inode *cinode,
					  uint64_t blkno)
{
	errcode_t ret;

	if (!cinode->ci_chains) {
		ret = ocfs2_load_chain_allocator(fs, cinode);
		if (ret)
			return ret;
	}

	ret = ocfs2_chain_free(fs, cinode, blkno);
	if (ret)
		return ret;

	return ocfs2_write_chain_allocator(fs, cinode);
}

static errcode_t ocfs2_load_allocator(ocfs2_filesys *fs,
				      int type, int node_num,
				      ocfs2_cached_inode **alloc_cinode)
{
	errcode_t ret;
	uint64_t blkno;

	if (!*alloc_cinode) {
		ret = ocfs2_lookup_system_inode(fs, type, node_num,
						&blkno);
		if (ret)
			return ret;
		ret = ocfs2_read_cached_inode(fs, blkno, alloc_cinode);
		if (ret)
			return ret;
	}

	return 0;
}

errcode_t ocfs2_new_inode(ocfs2_filesys *fs, uint64_t *blkno)
{
	errcode_t ret;

	ret = ocfs2_load_allocator(fs, INODE_ALLOC_SYSTEM_INODE,
			   	   0, &fs->fs_inode_alloc);
	if (ret)
		return ret;

	return ocfs2_chain_alloc_with_io(fs, fs->fs_inode_alloc, blkno);
}

errcode_t ocfs2_new_system_inode(ocfs2_filesys *fs, uint64_t *blkno)
{
	errcode_t ret;

	ret = ocfs2_load_allocator(fs, GLOBAL_INODE_ALLOC_SYSTEM_INODE,
			   	   0, &fs->fs_system_inode_alloc);
	if (ret)
		return ret;

	return ocfs2_chain_alloc_with_io(fs, fs->fs_system_inode_alloc,
					 blkno);
}

errcode_t ocfs2_delete_inode(ocfs2_filesys *fs, uint64_t blkno)
{
	errcode_t ret;

	ret = ocfs2_load_allocator(fs, INODE_ALLOC_SYSTEM_INODE,
			   	   0, &fs->fs_inode_alloc);
	if (ret)
		return ret;

	return ocfs2_chain_free_with_io(fs, fs->fs_inode_alloc, blkno);
}

void ocfs2_init_inode(ocfs2_filesys *fs, ocfs2_dinode *di,
		      uint64_t blkno)
{
	ocfs2_extent_list *fel;

	di->i_generation = 0; /* FIXME */
	di->i_blkno = blkno;
	di->i_suballoc_node = 0;
	di->i_suballoc_bit = 0; /* FIXME */
	di->i_uid = di->i_gid = 0;
	if (S_ISDIR(di->i_mode))
		di->i_links_count = 2;
	else
		di->i_links_count = 1;
	strcpy(di->i_signature, OCFS2_INODE_SIGNATURE);
	di->i_flags |= OCFS2_VALID_FL;
	di->i_atime = di->i_ctime = di->i_mtime = 0;  /* FIXME */
	di->i_dtime = 0;

	fel = &di->id2.i_list;
	fel->l_tree_depth = 0;
	fel->l_next_free_rec = 0;
	fel->l_count = ocfs2_extent_recs_per_inode(fs->fs_blocksize);
}

#ifdef DEBUG_EXE
#include <stdio.h>

static void print_usage(void)
{
	fprintf(stdout, "debug_alloc <newfile> <device>\n");
}

int main(int argc, char *argv[])
{
	errcode_t ret;
	ocfs2_filesys *fs;
	char *buf;
	ocfs2_dinode *di;
	uint64_t blkno;

	if (argc < 3) {
		print_usage();
		return 1;
	}

	initialize_ocfs_error_table();

	ret = ocfs2_open(argv[2], OCFS2_FLAG_RW, 0, 0, &fs);
	if (ret) {
		com_err(argv[0], ret,
			"while opening \"%s\"", argv[2]);
		goto out;
	}

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret) {
		com_err(argv[0], ret,
			"while allocating buffer");
		goto out_close;
	}

	di = (ocfs2_dinode *)buf;

	ret = ocfs2_new_inode(fs, &blkno);
	if (ret) {
		com_err(argv[0], ret,
			"while allocating a new inode");
		goto out_free;
	}

	ocfs2_init_inode(fs, di, blkno);

	ret = ocfs2_write_inode(fs, blkno, buf);
	if (ret) {
		com_err(argv[0], ret,
			"while writing new inode %"PRIu64, blkno);
		goto out_free;
	}

	ret = ocfs2_link(fs, fs->fs_root_blkno, argv[1],
			 blkno, OCFS2_FT_REG_FILE);
	if (ret) {
		com_err(argv[0], ret,
			"while linking inode %"PRIu64, blkno);
	}

out_free:
	ocfs2_free(&buf);

out_close:
	ret = ocfs2_close(fs);
	if (ret) {
		com_err(argv[0], ret,
			"while closing \"%s\"", argv[2]);
	}

out:
	return ret;
}

#endif  /* DEBUG_EXE */
