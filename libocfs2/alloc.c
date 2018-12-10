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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301 USA.
 */

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE
#define _DEFAULT_SOURCE

#include <string.h>
#include <inttypes.h>

#include "ocfs2/ocfs2.h"

static errcode_t ocfs2_chain_alloc_with_io(ocfs2_filesys *fs,
					   ocfs2_cached_inode *cinode,
					   uint64_t *gd_blkno,
					   uint16_t *suballoc_bit,
					   uint64_t *bitno)
{
	errcode_t ret;

	if (!cinode->ci_chains) {
		ret = ocfs2_load_chain_allocator(fs, cinode);
		if (ret)
			return ret;
	}

	ret = ocfs2_chain_alloc(fs, cinode, gd_blkno, suballoc_bit, bitno);
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
				      int type, int slot_num,
				      ocfs2_cached_inode **alloc_cinode)
{
	errcode_t ret;
	uint64_t blkno;

	if (!*alloc_cinode) {
		ret = ocfs2_lookup_system_inode(fs, type, slot_num, &blkno);
		if (ret)
			return ret;
		ret = ocfs2_read_cached_inode(fs, blkno, alloc_cinode);
		if (ret)
			return ret;
		/* Ignore error */
		ocfs2_cache_chain_allocator_blocks(fs,
						   (*alloc_cinode)->ci_inode);
	}

	if (!(*alloc_cinode)->ci_chains) {
		ret = ocfs2_load_chain_allocator(fs, *alloc_cinode);
		if (ret)
			return ret;
	}

	return 0;
}

/*
 * This function is duplicated in mkfs.ocfs2/mkfs.c.
 * Please keep them in sync.
 */
static int ocfs2_clusters_per_group(int block_size, int cluster_size_bits)
{
	int megabytes;

	switch (block_size) {
	case 4096:
	case 2048:
		megabytes = 4;
		break;
	case 1024:
		megabytes = 2;
		break;
	case 512:
	default:
		megabytes = 1;
		break;
	}
#define ONE_MB_SHIFT           20
	return (megabytes << ONE_MB_SHIFT) >> cluster_size_bits;
}

static void ocfs2_zero_dinode_id2_with_xattr(int blocksize,
					     struct ocfs2_dinode *di)
{
	unsigned int xattrsize = di->i_xattr_inline_size;

	if (di->i_dyn_features & OCFS2_INLINE_XATTR_FL)
		memset(&di->id2, 0, blocksize -
				    offsetof(struct ocfs2_dinode, id2) -
				    xattrsize);
	else
		memset(&di->id2, 0, blocksize -
				    offsetof(struct ocfs2_dinode, id2));
}

void ocfs2_dinode_new_extent_list(ocfs2_filesys *fs,
				  struct ocfs2_dinode *di)
{
	ocfs2_zero_dinode_id2_with_xattr(fs->fs_blocksize, di);

	di->id2.i_list.l_tree_depth = 0;
	di->id2.i_list.l_next_free_rec = 0;
	di->id2.i_list.l_count = ocfs2_extent_recs_per_inode(fs->fs_blocksize);
}

void ocfs2_set_inode_data_inline(ocfs2_filesys *fs, struct ocfs2_dinode *di)
{
	struct ocfs2_inline_data *idata = &di->id2.i_data;

	ocfs2_zero_dinode_id2_with_xattr(fs->fs_blocksize, di);

	idata->id_count =
		ocfs2_max_inline_data_with_xattr(fs->fs_blocksize, di);

	di->i_dyn_features |= OCFS2_INLINE_DATA_FL;
}

static void ocfs2_init_inode(ocfs2_filesys *fs, struct ocfs2_dinode *di,
			     int16_t slot, uint64_t gd_blkno,
			     uint16_t suballoc_bit,
			     uint64_t blkno, uint16_t mode,
			     uint32_t flags)
{
	int cs_bits = OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits;
	unsigned int tl_recs;

	di->i_generation = fs->fs_super->i_generation;
	di->i_fs_generation = fs->fs_super->i_fs_generation;
	di->i_blkno = blkno;
	di->i_suballoc_slot = slot;
	di->i_suballoc_loc = gd_blkno;
	di->i_suballoc_bit = suballoc_bit;
	di->i_uid = di->i_gid = 0;
	di->i_mode = mode;
	if (S_ISDIR(di->i_mode))
		di->i_links_count = 2;
	else
		di->i_links_count = 1;
	strcpy((char *)di->i_signature, OCFS2_INODE_SIGNATURE);
	di->i_atime = di->i_ctime = di->i_mtime = time(NULL);
	di->i_dtime = 0;

	di->i_flags = flags;

	if (flags & OCFS2_LOCAL_ALLOC_FL) {
		di->id2.i_lab.la_size =
			ocfs2_local_alloc_size(fs->fs_blocksize);
		return ;
	}

	if (flags & OCFS2_CHAIN_FL) {
		di->id2.i_chain.cl_count = 
			ocfs2_chain_recs_per_inode(fs->fs_blocksize);
		di->id2.i_chain.cl_cpg = 
			ocfs2_clusters_per_group(fs->fs_blocksize, cs_bits);
		di->id2.i_chain.cl_bpc = fs->fs_clustersize / fs->fs_blocksize;
		di->id2.i_chain.cl_next_free_rec = 0;
		return ;
	}

	if (flags & OCFS2_DEALLOC_FL) {
		tl_recs = ocfs2_truncate_recs_per_inode(fs->fs_blocksize);
		di->id2.i_dealloc.tl_count = tl_recs;
		return ;
	}

	if (flags & OCFS2_SUPER_BLOCK_FL)
		return ;

	if (ocfs2_support_inline_data(OCFS2_RAW_SB(fs->fs_super)) &&
	    S_ISDIR(di->i_mode))
		ocfs2_set_inode_data_inline(fs, di);
	else
		ocfs2_dinode_new_extent_list(fs, di);
}

static void ocfs2_init_eb(ocfs2_filesys *fs,
			  struct ocfs2_extent_block *eb,
			  uint64_t gd_blkno, uint16_t suballoc_bit,
			  uint64_t blkno)
{
	strcpy((char *)eb->h_signature, OCFS2_EXTENT_BLOCK_SIGNATURE);
	eb->h_fs_generation = fs->fs_super->i_fs_generation;
	eb->h_blkno = blkno;
	eb->h_suballoc_slot = 0;
	eb->h_suballoc_loc = gd_blkno;
	eb->h_suballoc_bit = suballoc_bit;
	eb->h_list.l_count = ocfs2_extent_recs_per_eb(fs->fs_blocksize);
}

errcode_t ocfs2_new_inode(ocfs2_filesys *fs, uint64_t *ino, int mode)
{
	errcode_t ret;
	char *buf;
	uint64_t gd_blkno;
	uint16_t suballoc_bit;
	struct ocfs2_dinode *di;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_load_allocator(fs, INODE_ALLOC_SYSTEM_INODE, 0,
				   &fs->fs_inode_allocs[0]);
	if (ret)
		goto out;

	ret = ocfs2_chain_alloc_with_io(fs, fs->fs_inode_allocs[0],
					&gd_blkno, &suballoc_bit, ino);
	if (ret == OCFS2_ET_BIT_NOT_FOUND) {
		ret = ocfs2_chain_add_group(fs, fs->fs_inode_allocs[0]);
		if (ret)
			goto out;
		ret = ocfs2_chain_alloc_with_io(fs, fs->fs_inode_allocs[0],
						&gd_blkno, &suballoc_bit, ino);
		if (ret)
			goto out;
	} else if (ret)
		goto out;

	memset(buf, 0, fs->fs_blocksize);
	di = (struct ocfs2_dinode *)buf;
	ocfs2_init_inode(fs, di, 0, gd_blkno, suballoc_bit,
			 *ino, mode, OCFS2_VALID_FL);

	ret = ocfs2_write_inode(fs, *ino, buf);
	if (ret)
		ocfs2_delete_inode(fs, *ino);

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
	uint16_t suballoc_bit;
	struct ocfs2_dinode *di;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_load_allocator(fs, GLOBAL_INODE_ALLOC_SYSTEM_INODE,
			   	   0, &fs->fs_system_inode_alloc);
	if (ret)
		goto out;

	ret = ocfs2_chain_alloc_with_io(fs, fs->fs_system_inode_alloc,
					&gd_blkno, &suballoc_bit, ino);
	if (ret == OCFS2_ET_BIT_NOT_FOUND) {
		ret = ocfs2_chain_add_group(fs, fs->fs_system_inode_alloc);
		if (ret)
			goto out;
		ret = ocfs2_chain_alloc_with_io(fs, fs->fs_system_inode_alloc,
						&gd_blkno, &suballoc_bit, ino);
		if (ret)
			goto out;
	} else if (ret)
		goto out;

	memset(buf, 0, fs->fs_blocksize);
	di = (struct ocfs2_dinode *)buf;
	ocfs2_init_inode(fs, di, -1, gd_blkno, suballoc_bit, *ino, mode,
			 (flags | OCFS2_VALID_FL | OCFS2_SYSTEM_FL));

	ret = ocfs2_write_inode(fs, *ino, buf);

out:
	ocfs2_free(&buf);

	return ret;
}

errcode_t ocfs2_delete_inode(ocfs2_filesys *fs, uint64_t ino)
{
	errcode_t ret;
	char *buf;
	struct ocfs2_dinode *di;
	int16_t slot;
	ocfs2_cached_inode **inode_alloc;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_read_inode(fs, ino, buf);
	if (ret)
		goto out;
	di = (struct ocfs2_dinode *)buf;
	slot = di->i_suballoc_slot;

	if (slot == OCFS2_INVALID_SLOT) {
		inode_alloc = &fs->fs_system_inode_alloc;
		ret = ocfs2_load_allocator(fs, GLOBAL_INODE_ALLOC_SYSTEM_INODE,
					   0, inode_alloc);
	}
	else {
		inode_alloc = &fs->fs_inode_allocs[slot];
		ret = ocfs2_load_allocator(fs, INODE_ALLOC_SYSTEM_INODE, slot,
					   inode_alloc);
	}

	if (ret)
		goto out;

	ret = ocfs2_chain_free_with_io(fs, *inode_alloc, ino);
	if (ret)
		goto out;

	di->i_flags &= ~(OCFS2_VALID_FL | OCFS2_ORPHANED_FL);
	di->i_dtime = time(NULL);
	ret = ocfs2_write_inode(fs, di->i_blkno, buf);

out:
	ocfs2_free(&buf);

	return ret;
}

errcode_t ocfs2_test_inode_allocated(ocfs2_filesys *fs, uint64_t blkno,
				     int *is_allocated)
{
	int16_t slot;
	uint16_t max_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;
	ocfs2_cached_inode **ci;
	int type;
	errcode_t ret = OCFS2_ET_INTERNAL_FAILURE;

	for (slot = OCFS2_INVALID_SLOT; slot != max_slots; slot++) {
		if (slot == OCFS2_INVALID_SLOT) {
			type = GLOBAL_INODE_ALLOC_SYSTEM_INODE;
			ci = &fs->fs_system_inode_alloc;
		} else {
			type = INODE_ALLOC_SYSTEM_INODE;
			ci = &fs->fs_inode_allocs[slot];
		}

		ret = ocfs2_load_allocator(fs, type, slot, ci);
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
	uint16_t suballoc_bit;
	struct ocfs2_extent_block *eb;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_load_allocator(fs, EXTENT_ALLOC_SYSTEM_INODE,
			   	   0, &fs->fs_eb_allocs[0]);
	if (ret)
		goto out;

	ret = ocfs2_chain_alloc_with_io(fs, fs->fs_eb_allocs[0],
					&gd_blkno, &suballoc_bit, blkno);
	if (ret == OCFS2_ET_BIT_NOT_FOUND) {
		ret = ocfs2_chain_add_group(fs, fs->fs_eb_allocs[0]);
		if (ret)
			goto out;
		ret = ocfs2_chain_alloc_with_io(fs, fs->fs_eb_allocs[0],
						&gd_blkno, &suballoc_bit,
						blkno);
		if (ret)
			goto out;
	} else if (ret)
		goto out;

	memset(buf, 0, fs->fs_blocksize);
	eb = (struct ocfs2_extent_block *)buf;
	ocfs2_init_eb(fs, eb, gd_blkno, suballoc_bit, *blkno);

	ret = ocfs2_write_extent_block(fs, *blkno, buf);

out:
	ocfs2_free(&buf);

	return ret;
}

errcode_t ocfs2_delete_xattr_block(ocfs2_filesys *fs, uint64_t blkno)
{
	errcode_t ret;
	char *buf;
	int slot;
	struct ocfs2_xattr_block *xb = NULL;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_read_xattr_block(fs, blkno, buf);
	if (ret)
		goto out;

	xb = (struct ocfs2_xattr_block *)buf;
	slot = xb->xb_suballoc_slot;

	ret = ocfs2_load_allocator(fs, EXTENT_ALLOC_SYSTEM_INODE, slot,
				   &fs->fs_eb_allocs[slot]);
	if (ret)
		goto out;

	ret = ocfs2_chain_free_with_io(fs, fs->fs_eb_allocs[slot], blkno);
	if (ret)
		goto out;

out:
	ocfs2_free(&buf);

	return ret;
}

errcode_t ocfs2_delete_extent_block(ocfs2_filesys *fs, uint64_t blkno)
{
	errcode_t ret;
	char *buf;
	struct ocfs2_extent_block *eb;
	int slot;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_read_extent_block(fs, blkno, buf);
	if (ret)
		goto out;
	eb = (struct ocfs2_extent_block *)buf;
	slot = eb->h_suballoc_slot;

	ret = ocfs2_load_allocator(fs, EXTENT_ALLOC_SYSTEM_INODE, slot,
				   &fs->fs_eb_allocs[slot]);
	if (ret)
		goto out;

	ret = ocfs2_chain_free_with_io(fs, fs->fs_eb_allocs[slot], blkno);
	if (ret)
		goto out;

	ret = ocfs2_write_extent_block(fs, eb->h_blkno, buf);

out:
	ocfs2_free(&buf);

	return ret;
}

errcode_t ocfs2_delete_refcount_block(ocfs2_filesys *fs, uint64_t blkno)
{
	errcode_t ret;
	char *buf;
	struct ocfs2_refcount_block *rb;
	int slot;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_read_refcount_block(fs, blkno, buf);
	if (ret)
		goto out;
	rb = (struct ocfs2_refcount_block *)buf;
	slot = rb->rf_suballoc_slot;

	ret = ocfs2_load_allocator(fs, EXTENT_ALLOC_SYSTEM_INODE, slot,
				   &fs->fs_eb_allocs[slot]);
	if (ret)
		goto out;

	ret = ocfs2_chain_free_with_io(fs, fs->fs_eb_allocs[slot], blkno);

out:
	ocfs2_free(&buf);

	return ret;
}

static void ocfs2_init_rb(ocfs2_filesys *fs,
			  struct ocfs2_refcount_block *rb,
			  uint64_t gd_blkno, uint16_t suballoc_bit,
			  uint64_t blkno,
			  uint64_t root_blkno, uint32_t rf_generation)
{
	strcpy((void *)rb, OCFS2_REFCOUNT_BLOCK_SIGNATURE);
	rb->rf_fs_generation = fs->fs_super->i_fs_generation;
	rb->rf_blkno = blkno;
	rb->rf_suballoc_slot = 0;
	rb->rf_suballoc_loc = gd_blkno;
	rb->rf_suballoc_bit = suballoc_bit;
	rb->rf_parent = root_blkno;
	if (root_blkno)
		rb->rf_flags = OCFS2_REFCOUNT_LEAF_FL;
	rb->rf_records.rl_count = ocfs2_refcount_recs_per_rb(fs->fs_blocksize);
	rb->rf_generation = rf_generation;
}

errcode_t ocfs2_new_refcount_block(ocfs2_filesys *fs, uint64_t *blkno,
				   uint64_t root_blkno, uint32_t rf_generation)
{
	errcode_t ret;
	char *buf;
	uint64_t gd_blkno;
	uint16_t suballoc_bit;
	struct ocfs2_refcount_block *rb;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_load_allocator(fs, EXTENT_ALLOC_SYSTEM_INODE,
				   0, &fs->fs_eb_allocs[0]);
	if (ret)
		goto out;

	ret = ocfs2_chain_alloc_with_io(fs, fs->fs_eb_allocs[0],
					&gd_blkno, &suballoc_bit, blkno);
	if (ret == OCFS2_ET_BIT_NOT_FOUND) {
		ret = ocfs2_chain_add_group(fs, fs->fs_eb_allocs[0]);
		if (ret)
			goto out;
		ret = ocfs2_chain_alloc_with_io(fs, fs->fs_eb_allocs[0],
						&gd_blkno, &suballoc_bit,
						blkno);
		if (ret)
			goto out;
	} else if (ret)
		goto out;

	memset(buf, 0, fs->fs_blocksize);
	rb = (struct ocfs2_refcount_block *)buf;

	ocfs2_init_rb(fs, rb, gd_blkno, suballoc_bit,
		      *blkno, root_blkno, rf_generation);

	ret = ocfs2_write_refcount_block(fs, *blkno, buf);

out:
	ocfs2_free(&buf);

	return ret;
}

errcode_t ocfs2_grow_chain_allocator(ocfs2_filesys *fs, int type,
				     int slot_num, uint32_t num_clusters)
{
	errcode_t ret = OCFS2_ET_INVALID_ARGUMENT;
	ocfs2_cached_inode *ci;
	int i, num_groups, cpg;

	switch (type) {
	case EXTENT_ALLOC_SYSTEM_INODE:
		ci = fs->fs_eb_allocs[slot_num];
		break;
	case INODE_ALLOC_SYSTEM_INODE:
		ci = fs->fs_inode_allocs[slot_num];
		break;
	case GLOBAL_INODE_ALLOC_SYSTEM_INODE:
		ci = fs->fs_system_inode_alloc;
		break;
	default:
		goto out;
	}

	ret = ocfs2_load_allocator(fs, type, slot_num, &ci);
	if (ret)
		goto out;

	cpg = ci->ci_inode->id2.i_chain.cl_cpg;

	num_groups = (num_clusters + cpg - 1) / cpg;

	for (i = 0; i < num_groups; ++i) {
		ret = ocfs2_chain_add_group(fs, ci);
		if (ret)
			goto out;
	}

out:
	return ret;
}

/* only initiate part of dx_root:
 *   dr_subllaoc_slot
 *   dr_sbualloc_bit
 *   dr_fs_generation
 *   dr_blkno
 *   dr_flags
 */
static void init_dx_root(ocfs2_filesys *fs,
			struct ocfs2_dx_root_block *dx_root,
			int slot, uint64_t gd_blkno,
			uint16_t suballoc_bit, uint64_t dr_blkno)
{

	memset(dx_root, 0, fs->fs_blocksize);
	strcpy((char *)dx_root->dr_signature, OCFS2_DX_ROOT_SIGNATURE);
	dx_root->dr_suballoc_slot = slot;
	dx_root->dr_suballoc_loc = gd_blkno;
	dx_root->dr_suballoc_bit = suballoc_bit;
	dx_root->dr_fs_generation = fs->fs_super->i_fs_generation;
	dx_root->dr_blkno = dr_blkno;
	dx_root->dr_flags |= OCFS2_DX_FLAG_INLINE;
}

errcode_t ocfs2_new_dx_root(ocfs2_filesys *fs,
				struct ocfs2_dinode *di,
				uint64_t *dr_blkno)
{
	errcode_t ret;
	char *buf = NULL;
	uint64_t gd_blkno;
	uint16_t suballoc_bit;
	struct ocfs2_dx_root_block *dx_root;
	int slot;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		goto out;

	slot = di->i_suballoc_slot;
	if (slot == (uint16_t)OCFS2_INVALID_SLOT)
		slot = 0;

	ret = ocfs2_load_allocator(fs, EXTENT_ALLOC_SYSTEM_INODE,
				slot, &fs->fs_eb_allocs[slot]);
	if (ret)
		goto out;

	ret = ocfs2_chain_alloc_with_io(fs, fs->fs_eb_allocs[slot],
					&gd_blkno, &suballoc_bit, dr_blkno);
	if (ret == OCFS2_ET_BIT_NOT_FOUND) {
		ret = ocfs2_chain_add_group(fs, fs->fs_eb_allocs[slot]);
		if (ret)
			goto out;
		ret = ocfs2_chain_alloc_with_io(fs, fs->fs_eb_allocs[slot],
						&gd_blkno, &suballoc_bit,
						dr_blkno);
		if (ret)
			goto out;
	} else if (ret)
		goto out;

	dx_root = (struct ocfs2_dx_root_block *)buf;
	init_dx_root(fs, dx_root, slot, gd_blkno, suballoc_bit, *dr_blkno);

	ret = ocfs2_write_dx_root(fs, *dr_blkno, (char *)dx_root);
out:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

errcode_t ocfs2_delete_dx_root(ocfs2_filesys *fs, uint64_t dr_blkno)
{
	errcode_t ret;
	char *buf = NULL;
	struct ocfs2_dx_root_block *dx_root;
	int slot;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		goto out;

	ret = ocfs2_read_dx_root(fs, dr_blkno, buf);
	if (ret)
		goto out;

	dx_root = (struct ocfs2_dx_root_block *)buf;
	slot = dx_root->dr_suballoc_slot;

	ret = ocfs2_load_allocator(fs, EXTENT_ALLOC_SYSTEM_INODE, slot,
				&fs->fs_eb_allocs[slot]);
	if (ret)
		goto out;

	ret = ocfs2_chain_free_with_io(fs, fs->fs_eb_allocs[slot], dr_blkno);

out:
	if (buf)
		ocfs2_free(&buf);

	return ret;
}

/* XXX what to do about local allocs?
 * XXX Well, we shouldn't use local allocs to allocate, as we are
 *     userspace and we have the entire bitmap in memory.  However, this
 *     doesn't solve the issue of "is space still in dirty local
 *     allocs?"
 */
errcode_t ocfs2_new_clusters(ocfs2_filesys *fs,
			     uint32_t min,
			     uint32_t requested,
			     uint64_t *start_blkno,
			     uint32_t *clusters_found)

{
	errcode_t ret;
	uint64_t start_bit;
	uint64_t found;

	ret = ocfs2_load_allocator(fs, GLOBAL_BITMAP_SYSTEM_INODE,
				   0, &fs->fs_cluster_alloc);
	if (ret)
		goto out;

	ret = ocfs2_chain_alloc_range(fs, fs->fs_cluster_alloc, min, requested,
				      &start_bit, &found);
	if (ret)
		goto out;

	*start_blkno = ocfs2_clusters_to_blocks(fs, start_bit);
	/* We never have enough bits that can be allocated
	 * contiguously to overflow this. The lower level API needs
	 * fixing. */
	*clusters_found = (uint32_t) found;

	ret = ocfs2_write_chain_allocator(fs, fs->fs_cluster_alloc);
	if (ret)
		ocfs2_free_clusters(fs, requested, *start_blkno);

out:
	return ret;
}

errcode_t ocfs2_test_cluster_allocated(ocfs2_filesys *fs, uint32_t cpos,
				       int *is_allocated)
{
	errcode_t ret;
	ret = ocfs2_load_allocator(fs, GLOBAL_BITMAP_SYSTEM_INODE,
				   0, &fs->fs_cluster_alloc);
	if (!ret) {
		ret = ocfs2_chain_test(fs, fs->fs_cluster_alloc, cpos,
				       is_allocated);
	}

	return ret;
}

errcode_t ocfs2_new_specific_cluster(ocfs2_filesys *fs, uint32_t cpos)
{
	errcode_t ret;
	int allocatedp = 0;

	/* Loads the allocator if we need it */
	ret = ocfs2_test_cluster_allocated(fs, cpos, &allocatedp);
	if (ret)
		goto out;

	if (allocatedp) {
		ret = OCFS2_ET_BIT_NOT_FOUND;
		goto out;
	}

	ocfs2_chain_force_val(fs, fs->fs_cluster_alloc, cpos, 1, NULL);
	ret = ocfs2_write_chain_allocator(fs, fs->fs_cluster_alloc);
	if (ret)
		ocfs2_free_clusters(fs, 1,
				    ocfs2_blocks_to_clusters(fs, cpos));

out:
	return ret;
}

errcode_t ocfs2_free_clusters(ocfs2_filesys *fs,
			      uint32_t len,
			      uint64_t start_blkno)
{
	errcode_t ret;

	ret = ocfs2_load_allocator(fs, GLOBAL_BITMAP_SYSTEM_INODE,
				   0, &fs->fs_cluster_alloc);
	if (ret)
		goto out;

	ret = ocfs2_chain_free_range(fs, fs->fs_cluster_alloc, len,
				     ocfs2_blocks_to_clusters(fs, start_blkno));
	if (ret)
		goto out;

	/* XXX OK, it's bad if we can't revert this after the io fails */
	ret = ocfs2_write_chain_allocator(fs, fs->fs_cluster_alloc);
out:
	return ret;
}

/*
 * Test whether clusters have the specified value in the bitmap.
 * test: expected value
 * matches: 1 if all bits match test, else 0
 */
errcode_t ocfs2_test_clusters(ocfs2_filesys *fs,
			      uint32_t len,
			      uint64_t start_blkno,
			      int test,
			      int *matches)
{
	errcode_t ret;
	uint32_t start_cluster;
	int set = 0;

	*matches = 0;

	if (!len)
		return 0;

	ret = ocfs2_load_allocator(fs, GLOBAL_BITMAP_SYSTEM_INODE,
				   0, &fs->fs_cluster_alloc);
	if (ret)
		goto out;

	start_cluster = ocfs2_blocks_to_clusters(fs, start_blkno);

	while (len) {
		ret = ocfs2_bitmap_test(fs->fs_cluster_alloc->ci_chains,
					start_cluster, &set);
		if (ret || set != test)
			goto out;

		len--;
		start_cluster++;
	}

	*matches = 1;
out:
	return ret;
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
	struct ocfs2_dinode *di;
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

	di = (struct ocfs2_dinode *)buf;

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
