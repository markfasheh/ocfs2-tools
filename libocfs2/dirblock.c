/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dirblock.c
 *
 * Directory block routines for the OCFS2 userspace library.
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
 *
 *  This code is a port of e2fsprogs/lib/ext2fs/dirblock.c
 *  Copyright (C) 1995, 1996 Theodore Ts'o.
 */

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE

#include <string.h>

#include "ocfs2/byteorder.h"
#include "ocfs2/ocfs2.h"

unsigned int ocfs2_dir_trailer_blk_off(ocfs2_filesys *fs)
{
	return fs->fs_blocksize - sizeof(struct ocfs2_dir_block_trailer);
}

struct ocfs2_dir_block_trailer *ocfs2_dir_trailer_from_block(ocfs2_filesys *fs,
							     void *data)
{
	char *p = data;

	p += ocfs2_dir_trailer_blk_off(fs);
	return (struct ocfs2_dir_block_trailer *)p;
}

int ocfs2_dir_has_trailer(ocfs2_filesys *fs, struct ocfs2_dinode *di)
{
	if (ocfs2_support_inline_data(OCFS2_RAW_SB(fs->fs_super)) &&
	    (di->i_dyn_features & OCFS2_INLINE_DATA_FL))
		return 0;

	if (ocfs2_supports_indexed_dirs(OCFS2_RAW_SB(fs->fs_super)) &&
	    di->i_dyn_features & OCFS2_INDEXED_DIR_FL)
		return 1;

	return ocfs2_meta_ecc(OCFS2_RAW_SB(fs->fs_super));
}

int ocfs2_supports_dir_trailer(ocfs2_filesys *fs)
{
	return ocfs2_meta_ecc(OCFS2_RAW_SB(fs->fs_super)) ||
		ocfs2_supports_indexed_dirs(OCFS2_RAW_SB(fs->fs_super));
}

int ocfs2_skip_dir_trailer(ocfs2_filesys *fs, struct ocfs2_dinode *di,
			   struct ocfs2_dir_entry *de, unsigned long offset)
{
	if (!ocfs2_dir_has_trailer(fs, di))
		return 0;

	if (offset != ocfs2_dir_trailer_blk_off(fs))
		return 0;

	return 1;
}

/*
 * We are sure there is prepared space for the trailer, no directory
 * entry will overlap with the trailer:
 * - if we rebuild the indexed tree for a directory, no dir entry
 *   will overwrite the trailer's space.
 * - if we build the indexed tree by tunefs.ocfs2, it will enable
 *   meta ecc feature before enable indexed dirs feature. Which
 *   means space for each trailer is well prepared already.
 */
void ocfs2_init_dir_trailer(ocfs2_filesys *fs, struct ocfs2_dinode *di,
			    uint64_t blkno, void *buf)
{
	struct ocfs2_dir_block_trailer *trailer =
		ocfs2_dir_trailer_from_block(fs, buf);

	memset(trailer, 0, sizeof(struct ocfs2_dir_block_trailer));
	memcpy(trailer->db_signature, OCFS2_DIR_TRAILER_SIGNATURE,
	       strlen(OCFS2_DIR_TRAILER_SIGNATURE));
	trailer->db_compat_rec_len = sizeof(struct ocfs2_dir_block_trailer);
	trailer->db_blkno = blkno;
	trailer->db_parent_dinode = di->i_blkno;
}

static void ocfs2_swap_dir_entry(struct ocfs2_dir_entry *dirent)
{
	if (cpu_is_little_endian)
		return;

	dirent->inode = bswap_64(dirent->inode);
	dirent->rec_len = bswap_16(dirent->rec_len);
}

static errcode_t ocfs2_swap_dir_entries_direction(void *buf, uint64_t bytes,
						  int to_cpu)
{
	char *p, *end;
	struct ocfs2_dir_entry *dirent;
	unsigned int name_len, rec_len;
	errcode_t retval = 0;

	p = (char *) buf;
	end = (char *) buf + bytes;
	while (p < end-12) {
		dirent = (struct ocfs2_dir_entry *) p;

		if (to_cpu)
			ocfs2_swap_dir_entry(dirent);
		name_len = dirent->name_len;
		rec_len = dirent->rec_len;
		if (!to_cpu)
			ocfs2_swap_dir_entry(dirent);

		if ((rec_len < 12) || (rec_len % 4)) {
			rec_len = 12;
			retval = OCFS2_ET_DIR_CORRUPTED;
		}

		if (((name_len & 0xFF) + 12) > rec_len)
			retval = OCFS2_ET_DIR_CORRUPTED;
		p += rec_len;
	}
	return retval;
}

errcode_t ocfs2_swap_dir_entries_from_cpu(void *buf, uint64_t bytes)
{
	return ocfs2_swap_dir_entries_direction(buf, bytes, 0);
}
errcode_t ocfs2_swap_dir_entries_to_cpu(void *buf, uint64_t bytes)
{
	return ocfs2_swap_dir_entries_direction(buf, bytes, 1);
}

void ocfs2_swap_dir_trailer(struct ocfs2_dir_block_trailer *trailer)
{
	if (cpu_is_little_endian)
		return;

	trailer->db_compat_inode = bswap_64(trailer->db_compat_inode);
	trailer->db_compat_rec_len = bswap_16(trailer->db_compat_rec_len);
	trailer->db_blkno = bswap_64(trailer->db_blkno);
	trailer->db_parent_dinode = bswap_64(trailer->db_parent_dinode);
	trailer->db_free_rec_len = bswap_16(trailer->db_free_rec_len);
	trailer->db_free_next = bswap_64(trailer->db_free_next);
}

errcode_t ocfs2_read_dir_block(ocfs2_filesys *fs, struct ocfs2_dinode *di,
			       uint64_t block, void *buf)
{
	errcode_t retval;
	int end = fs->fs_blocksize;
	struct ocfs2_dir_block_trailer *trailer = NULL;

	retval = ocfs2_read_blocks(fs, block, 1, buf);
	if (retval)
		goto out;

	if (ocfs2_dir_has_trailer(fs, di)) {
		end = ocfs2_dir_trailer_blk_off(fs);
		trailer = ocfs2_dir_trailer_from_block(fs, buf);

		retval = ocfs2_validate_meta_ecc(fs, buf, &trailer->db_check);
		if (retval)
			goto out;

		if (memcmp(trailer->db_signature, OCFS2_DIR_TRAILER_SIGNATURE,
			   strlen(OCFS2_DIR_TRAILER_SIGNATURE))) {
			retval = OCFS2_ET_BAD_DIR_BLOCK_MAGIC;
			goto out;
		}
	}

	retval = ocfs2_swap_dir_entries_to_cpu(buf, end);
	if (retval)
		goto out;

	if (trailer)
		ocfs2_swap_dir_trailer(trailer);

out:
	return retval;
}

errcode_t ocfs2_write_dir_block(ocfs2_filesys *fs, struct ocfs2_dinode *di,
				uint64_t block, void *inbuf)
{
	errcode_t retval;
	char *buf = NULL;
	int end = fs->fs_blocksize;
	struct ocfs2_dir_block_trailer *trailer = NULL;

	retval = ocfs2_malloc_block(fs->fs_io, &buf);
	if (retval)
		return retval;

	memcpy(buf, inbuf, fs->fs_blocksize);

	if (ocfs2_dir_has_trailer(fs, di))
		end = ocfs2_dir_trailer_blk_off(fs);

	retval = ocfs2_swap_dir_entries_from_cpu(buf, end);
	if (retval)
		goto out;
	
	/*
	 * We can always set trailer - ocfs2_compute_meta_ecc() does
	 * nothing if the filesystem doesn't have the feature turned on
	 */
	trailer = ocfs2_dir_trailer_from_block(fs, buf);
	if (ocfs2_dir_has_trailer(fs, di))
		ocfs2_swap_dir_trailer(trailer);

	ocfs2_compute_meta_ecc(fs, buf, &trailer->db_check);
 	retval = io_write_block(fs->fs_io, block, 1, buf);
out:
	ocfs2_free(&buf);
	return retval;
}

static void ocfs2_swap_dx_entry(struct ocfs2_dx_entry *dx_entry)
{
	dx_entry->dx_major_hash		= bswap_32(dx_entry->dx_major_hash);
	dx_entry->dx_minor_hash		= bswap_32(dx_entry->dx_minor_hash);
	dx_entry->dx_dirent_blk		= bswap_64(dx_entry->dx_dirent_blk);
}

static void ocfs2_swap_dx_entry_list_to_cpu(struct ocfs2_dx_entry_list *dl_list)
{
	int i;

	if (cpu_is_little_endian)
		return;

	dl_list->de_count = bswap_16(dl_list->de_count);
	dl_list->de_num_used = bswap_16(dl_list->de_num_used);

	for (i = 0; i < dl_list->de_count; i++)
		ocfs2_swap_dx_entry(&dl_list->de_entries[i]);
}

static void ocfs2_swap_dx_entry_list_from_cpu(struct ocfs2_dx_entry_list *dl_list)
{
	int i;

	if (cpu_is_little_endian)
		return;

	for (i = 0; i < dl_list->de_count; i++)
		ocfs2_swap_dx_entry(&dl_list->de_entries[i]);

	dl_list->de_count = bswap_16(dl_list->de_count);
	dl_list->de_num_used = bswap_16(dl_list->de_num_used);
}

void ocfs2_swap_dx_root_to_cpu(ocfs2_filesys *fs,
			       struct ocfs2_dx_root_block *dx_root)
{
	if (cpu_is_little_endian)
		return;

	dx_root->dr_suballoc_slot	= bswap_16(dx_root->dr_suballoc_slot);
	dx_root->dr_suballoc_bit	= bswap_16(dx_root->dr_suballoc_bit);
	dx_root->dr_fs_generation	= bswap_32(dx_root->dr_fs_generation);
	dx_root->dr_blkno		= bswap_64(dx_root->dr_blkno);
	dx_root->dr_last_eb_blk		= bswap_64(dx_root->dr_last_eb_blk);
	dx_root->dr_clusters		= bswap_32(dx_root->dr_clusters);
	dx_root->dr_dir_blkno		= bswap_64(dx_root->dr_dir_blkno);
	dx_root->dr_num_entries		= bswap_32(dx_root->dr_num_entries);
	dx_root->dr_free_blk		= bswap_64(dx_root->dr_free_blk);

	if (dx_root->dr_flags & OCFS2_DX_FLAG_INLINE)
		ocfs2_swap_dx_entry_list_to_cpu(&dx_root->dr_entries);
	else
		ocfs2_swap_extent_list_to_cpu(fs, dx_root, &dx_root->dr_list);
}

void ocfs2_swap_dx_root_from_cpu(ocfs2_filesys *fs,
				struct ocfs2_dx_root_block *dx_root)
{
	if (cpu_is_little_endian)
		return;

	dx_root->dr_suballoc_slot	= bswap_16(dx_root->dr_suballoc_slot);
	dx_root->dr_suballoc_bit	= bswap_16(dx_root->dr_suballoc_bit);
	dx_root->dr_fs_generation	= bswap_32(dx_root->dr_fs_generation);
	dx_root->dr_blkno		= bswap_64(dx_root->dr_blkno);
	dx_root->dr_last_eb_blk		= bswap_64(dx_root->dr_last_eb_blk);
	dx_root->dr_clusters		= bswap_32(dx_root->dr_clusters);
	dx_root->dr_dir_blkno		= bswap_64(dx_root->dr_dir_blkno);
	dx_root->dr_num_entries		= bswap_32(dx_root->dr_num_entries);
	dx_root->dr_free_blk		= bswap_64(dx_root->dr_free_blk);

	if (dx_root->dr_flags & OCFS2_DX_FLAG_INLINE)
		ocfs2_swap_dx_entry_list_from_cpu(&dx_root->dr_entries);
	else
		ocfs2_swap_extent_list_from_cpu(fs, dx_root, &dx_root->dr_list);
}

/* XXX: should use the errcode_t return value */
errcode_t ocfs2_read_dx_root(ocfs2_filesys *fs, uint64_t block,
			     void *buf)
{
	errcode_t ret;
	struct ocfs2_dx_root_block *dx_root;
	char *dx_root_buf = NULL;

	ret = ocfs2_malloc_block(fs->fs_io, &dx_root_buf);
	if (ret)
		goto out;

	ret = ocfs2_read_blocks(fs, block, 1, dx_root_buf);
	if (ret)
		goto out;

	dx_root = (struct ocfs2_dx_root_block *)dx_root_buf;
	ret = ocfs2_validate_meta_ecc(fs, dx_root_buf, &dx_root->dr_check);
	if (ret)
		goto out;

	if (memcmp(dx_root->dr_signature, OCFS2_DX_ROOT_SIGNATURE,
		   strlen(OCFS2_DX_ROOT_SIGNATURE))) {
		ret = OCFS2_ET_DIR_CORRUPTED;
		goto out;
	}

	ocfs2_swap_dx_root_to_cpu(fs, dx_root);
	memcpy(buf, dx_root_buf, fs->fs_blocksize);
	ret = 0;
out:
	if (dx_root_buf)
		ocfs2_free(&dx_root_buf);
	return ret;
}

errcode_t ocfs2_write_dx_root(ocfs2_filesys *fs, uint64_t block,
				char *buf)
{
	errcode_t ret;
	char *dx_root_buf = NULL;
	struct ocfs2_dx_root_block *dx_root;

	if (!(fs->fs_flags & OCFS2_FLAG_RW))
		return OCFS2_ET_RO_FILESYS;

	if ((block < OCFS2_SUPER_BLOCK_BLKNO) ||
	    (block > fs->fs_blocks))
		return OCFS2_ET_BAD_BLKNO;

	ret = ocfs2_malloc_block(fs->fs_io, &dx_root_buf);
	if (ret)
		goto out;

	memcpy(dx_root_buf, buf, fs->fs_blocksize);

	dx_root = (struct ocfs2_dx_root_block *)dx_root_buf;
	ocfs2_swap_dx_root_from_cpu(fs, dx_root);

	ocfs2_compute_meta_ecc(fs, dx_root_buf, &dx_root->dr_check);
	ret = io_write_block(fs->fs_io, block, 1, dx_root_buf);
	if (!ret)
		fs->fs_flags |= OCFS2_FLAG_CHANGED;

out:
	if (dx_root_buf)
		ocfs2_free(&dx_root_buf);
	return ret;
}

void ocfs2_swap_dx_leaf_to_cpu(struct ocfs2_dx_leaf *dx_leaf)
{
	if (cpu_is_little_endian)
		return;
	dx_leaf->dl_blkno = bswap_64(dx_leaf->dl_blkno);
	dx_leaf->dl_fs_generation = bswap_32(dx_leaf->dl_fs_generation);
	ocfs2_swap_dx_entry_list_to_cpu(&dx_leaf->dl_list);
}

void ocfs2_swap_dx_leaf_from_cpu(struct ocfs2_dx_leaf *dx_leaf)
{
	if (cpu_is_little_endian)
		return;
	dx_leaf->dl_blkno = bswap_64(dx_leaf->dl_blkno);
	dx_leaf->dl_fs_generation = bswap_32(dx_leaf->dl_fs_generation);
	ocfs2_swap_dx_entry_list_from_cpu(&dx_leaf->dl_list);
}

errcode_t ocfs2_read_dx_leaf(ocfs2_filesys *fs, uint64_t block,
			     void *buf)
{
	errcode_t ret;
	struct ocfs2_dx_leaf *dx_leaf;

	ret = ocfs2_read_blocks(fs, block, 1, buf);
	if (ret)
		return ret;

	dx_leaf = (struct ocfs2_dx_leaf *)buf;
	ret = ocfs2_validate_meta_ecc(fs, buf, &dx_leaf->dl_check);
	if (ret)
		return ret;

	if (memcmp(dx_leaf->dl_signature, OCFS2_DX_LEAF_SIGNATURE,
		   strlen(OCFS2_DX_LEAF_SIGNATURE)))
		return OCFS2_ET_DIR_CORRUPTED;

	ocfs2_swap_dx_leaf_to_cpu(dx_leaf);

	return 0;
}

errcode_t ocfs2_write_dx_leaf(ocfs2_filesys *fs, uint64_t block,
				void *buf)
{
	errcode_t ret;
	char *dx_leaf_buf = NULL;
	struct ocfs2_dx_leaf *dx_leaf;

	if (!(fs->fs_flags & OCFS2_FLAG_RW))
		return OCFS2_ET_RO_FILESYS;

	if ((block < OCFS2_SUPER_BLOCK_BLKNO) ||
	    (block > fs->fs_blocks))
		return OCFS2_ET_BAD_BLKNO;

	ret = ocfs2_malloc_block(fs->fs_io, &dx_leaf_buf);
	if (ret)
		goto out;

	memcpy(dx_leaf_buf, buf, fs->fs_blocksize);
	dx_leaf = (struct ocfs2_dx_leaf *)dx_leaf_buf;
	ocfs2_swap_dx_leaf_from_cpu(dx_leaf);

	ocfs2_compute_meta_ecc(fs, dx_leaf_buf, &dx_leaf->dl_check);
	ret = io_write_block(fs->fs_io, block, 1, dx_leaf_buf);

	if (ret)
		goto out;

	fs->fs_flags |= OCFS2_FLAG_CHANGED;

out:
	if (dx_leaf_buf)
		ocfs2_free(&dx_leaf_buf);
	return ret;
}

int ocfs2_dir_indexed(struct ocfs2_dinode *di)
{
	if (di->i_dyn_features & OCFS2_INDEXED_DIR_FL)
		return 1;
	return 0;
}

/*
 * Only use this when we already know the directory is indexed.
 */
static int __ocfs2_is_dir_trailer(ocfs2_filesys *fs, unsigned long de_off)
{
	if (de_off == ocfs2_dir_trailer_blk_off(fs))
		return 1;

	return 0;
}

int ocfs2_is_dir_trailer(ocfs2_filesys *fs, struct ocfs2_dinode *di,
			 unsigned long de_off)
{
	if (ocfs2_dir_has_trailer(fs, di)) {
		return __ocfs2_is_dir_trailer(fs, de_off);
	}

	return 0;
}
