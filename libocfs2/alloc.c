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
					   uint64_t *gd_blkno,
					   uint64_t *bitno)
{
	errcode_t ret;

	if (!cinode->ci_chains) {
		ret = ocfs2_load_chain_allocator(fs, cinode);
		if (ret)
			return ret;
	}

	ret = ocfs2_chain_alloc(fs, cinode, gd_blkno, bitno);
	if (ret)
		return ret;

	return ocfs2_write_chain_allocator(fs, cinode);
}

static errcode_t ocfs2_chain_free_with_io(ocfs2_filesys *fs,
					  ocfs2_cached_inode *cinode,
					  uint64_t bitno)
{
	errcode_t ret;

	if (!cinode->ci_chains) {
		ret = ocfs2_load_chain_allocator(fs, cinode);
		if (ret)
			return ret;
	}

	ret = ocfs2_chain_free(fs, cinode, bitno);
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

	if (!(*alloc_cinode)->ci_chains) {
		ret = ocfs2_load_chain_allocator(fs, *alloc_cinode);
		if (ret)
			return ret;
	}

	return 0;
}

static void ocfs2_init_inode(ocfs2_filesys *fs, ocfs2_dinode *di,
			     uint64_t gd_blkno, uint64_t blkno)
{
	ocfs2_extent_list *fel;

	di->i_generation = fs->fs_super->i_generation;
	di->i_blkno = blkno;
	di->i_suballoc_node = 0;
	di->i_suballoc_bit = (uint16_t)(blkno - gd_blkno);
	di->i_uid = di->i_gid = 0;
	if (S_ISDIR(di->i_mode))
		di->i_links_count = 2;
	else
		di->i_links_count = 1;
	strcpy(di->i_signature, OCFS2_INODE_SIGNATURE);
	di->i_flags |= OCFS2_VALID_FL;
	di->i_atime = di->i_ctime = di->i_mtime = time(NULL);
	di->i_dtime = 0;

	fel = &di->id2.i_list;
	fel->l_tree_depth = 0;
	fel->l_next_free_rec = 0;
	fel->l_count = ocfs2_extent_recs_per_inode(fs->fs_blocksize);
}

static void ocfs2_init_eb(ocfs2_filesys *fs, ocfs2_extent_block *eb,
			  uint64_t gd_blkno, uint64_t blkno)
{
	strcpy(eb->h_signature, OCFS2_EXTENT_BLOCK_SIGNATURE);
	eb->h_blkno = blkno;
	eb->h_suballoc_node = 0;
	eb->h_suballoc_bit = (uint16_t)(blkno - gd_blkno);
	eb->h_list.l_count = ocfs2_extent_recs_per_eb(fs->fs_blocksize);
}

errcode_t ocfs2_new_inode(ocfs2_filesys *fs, uint64_t *ino, int mode)
{
	errcode_t ret;
	char *buf;
	uint64_t gd_blkno;
	ocfs2_dinode *di;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_load_allocator(fs, INODE_ALLOC_SYSTEM_INODE,
			   	   0, &fs->fs_inode_allocs[0]);
	if (ret)
		goto out;

	ret = ocfs2_chain_alloc_with_io(fs, fs->fs_inode_allocs[0],
					&gd_blkno, ino);
	if (ret)
		goto out;

	memset(buf, 0, fs->fs_blocksize);
	di = (ocfs2_dinode *)buf;
	di->i_mode = mode;
	ocfs2_init_inode(fs, di, gd_blkno, *ino);

	ret = ocfs2_write_inode(fs, *ino, buf);

out:
	ocfs2_free(&buf);

	return ret;
}

errcode_t ocfs2_new_system_inode(ocfs2_filesys *fs, uint64_t *ino,
				 int mode, int flags)
{
	errcode_t ret;
	char *buf;
	uint64_t gd_blkno;
	ocfs2_dinode *di;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_load_allocator(fs, GLOBAL_INODE_ALLOC_SYSTEM_INODE,
			   	   0, &fs->fs_system_inode_alloc);
	if (ret)
		goto out;

	ret = ocfs2_chain_alloc_with_io(fs, fs->fs_system_inode_alloc,
					&gd_blkno, ino);
	if (ret)
		goto out;

	memset(buf, 0, fs->fs_blocksize);
	di = (ocfs2_dinode *)buf;
	di->i_mode = mode;
	di->i_flags = flags;
	ocfs2_init_inode(fs, di, gd_blkno, *ino);
	di->i_flags |= OCFS2_SYSTEM_FL;

	ret = ocfs2_write_inode(fs, *ino, buf);

out:
	ocfs2_free(&buf);

	return ret;
}

errcode_t ocfs2_delete_inode(ocfs2_filesys *fs, uint64_t ino)
{
	errcode_t ret;
	char *buf;
	ocfs2_dinode *di;
	int node;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_read_inode(fs, ino, buf);
	if (ret)
		goto out;
	di = (ocfs2_dinode *)buf;
	node = di->i_suballoc_node;

	ret = ocfs2_load_allocator(fs, INODE_ALLOC_SYSTEM_INODE,
				   node,
				   &fs->fs_inode_allocs[node]);
	if (ret)
		goto out;

	ret = ocfs2_chain_free_with_io(fs, fs->fs_inode_allocs[node],
				       ino);
	if (ret)
		goto out;

	di->i_flags &= ~OCFS2_VALID_FL;
	ret = ocfs2_write_inode(fs, di->i_blkno, buf);

out:
	ocfs2_free(&buf);

	return ret;
}

errcode_t ocfs2_test_inode_allocated(ocfs2_filesys *fs, uint64_t blkno,
				     int *is_allocated)
{
	uint16_t node, max_nodes = OCFS2_RAW_SB(fs->fs_super)->s_max_nodes;
	ocfs2_cached_inode **ci;
	int type;
	errcode_t ret = OCFS2_ET_INTERNAL_FAILURE;

	for (node = ~0; node != max_nodes; node++) {
		if (node == (uint16_t)~0) {
			type = GLOBAL_INODE_ALLOC_SYSTEM_INODE;
			ci = &fs->fs_system_inode_alloc;
		} else {
			type = INODE_ALLOC_SYSTEM_INODE;
			ci = &fs->fs_inode_allocs[node];
		}

		ret = ocfs2_load_allocator(fs, type, node, ci);
		if (ret)
			break;

		ret = ocfs2_chain_test(fs, *ci, blkno, is_allocated);
		if (ret != OCFS2_ET_INVALID_BIT)
			break;
	}
	return ret;
}

errcode_t ocfs2_new_extent_block(ocfs2_filesys *fs, uint64_t *blkno)
{
	errcode_t ret;
	char *buf;
	uint64_t gd_blkno;
	ocfs2_extent_block *eb;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_load_allocator(fs, EXTENT_ALLOC_SYSTEM_INODE,
			   	   0, &fs->fs_eb_allocs[0]);
	if (ret)
		goto out;

	ret = ocfs2_chain_alloc_with_io(fs, fs->fs_eb_allocs[0],
					&gd_blkno, blkno);
	if (ret)
		goto out;

	memset(buf, 0, fs->fs_blocksize);
	eb = (ocfs2_extent_block *)buf;
	ocfs2_init_eb(fs, eb, gd_blkno, *blkno);

	ret = ocfs2_write_extent_block(fs, *blkno, buf);

out:
	ocfs2_free(&buf);

	return ret;
}

errcode_t ocfs2_delete_extent_block(ocfs2_filesys *fs, uint64_t blkno)
{
	errcode_t ret;
	char *buf;
	ocfs2_extent_block *eb;
	int node;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_read_extent_block(fs, blkno, buf);
	if (ret)
		goto out;
	eb = (ocfs2_extent_block *)buf;
	node = eb->h_suballoc_node;

	ret = ocfs2_load_allocator(fs, EXTENT_ALLOC_SYSTEM_INODE,
				   node,
				   &fs->fs_eb_allocs[node]);
	if (ret)
		goto out;

	ret = ocfs2_chain_free_with_io(fs, fs->fs_eb_allocs[node],
				       blkno);
	if (ret)
		goto out;

	ret = ocfs2_write_extent_block(fs, eb->h_blkno, buf);

out:
	ocfs2_free(&buf);

	return ret;
}

#if 0
/* This function needs to be filled out.  Essentially, it should be
 * calling a function in chainalloc.c.  Something like
 * "ocfs2_chain_alloc_range()".  The difference between that and
 * ocfs2_chain_alloc() is that the range can return a number of bits.
 * That function should take a 'required' number of bits, and return
 * the biggest chunk available.  It will need some sort of
 * "find_clear_bit_range()" function for the bitmaps.
 */
errcode_t ocfs2_new_clusters()
{
	return 0;
}
#endif

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

	ret = ocfs2_new_inode(fs, &blkno, 0644 | S_IFREG);
	if (ret) {
		com_err(argv[0], ret,
			"while allocating a new inode");
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
