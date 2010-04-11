/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * expanddir.c
 *
 * Expands an OCFS2 directory.  For the OCFS2 userspace library.
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
 *  This code is a port of e2fsprogs/lib/ext2fs/expanddir.c
 *  Copyright (C) 1993, 1999 Theodore Ts'o.
 */

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "ocfs2/ocfs2.h"

/*
 * ocfs2_expand_dir()
 *
 */
errcode_t ocfs2_expand_dir(ocfs2_filesys *fs,
			   uint64_t dir)
{
	errcode_t ret = 0;
	ocfs2_cached_inode *cinode = NULL;
	uint64_t used_blks;
	uint64_t totl_blks;
	uint64_t new_blk;
	uint64_t contig;
	char *buf = NULL;
	struct ocfs2_dir_entry *de;

	if (!(fs->fs_flags & OCFS2_FLAG_RW))
		return OCFS2_ET_RO_FILESYS;

	/* ensure it is a dir */
	ret = ocfs2_check_directory(fs, dir);
	if (ret)
		goto bail;

	/* read inode */
	ret = ocfs2_read_cached_inode(fs, dir, &cinode);
	if (ret)
		goto bail;

	if (ocfs2_support_inline_data(OCFS2_RAW_SB(fs->fs_super)) &&
	    cinode->ci_inode->i_dyn_features & OCFS2_INLINE_DATA_FL) {
		ret = ocfs2_convert_inline_data_to_extents(cinode);
		if ((ret == 0) &&
		     ocfs2_supports_indexed_dirs(OCFS2_RAW_SB(fs->fs_super))) {
			ret = ocfs2_dx_dir_build(fs, dir);
		}
		goto bail;
	}

	/* This relies on the fact that i_size of a directory is a
	 * multiple of blocksize */
	used_blks = cinode->ci_inode->i_size >>
	       			OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;
	totl_blks = ocfs2_clusters_to_blocks(fs, cinode->ci_inode->i_clusters);

	if (used_blks >= totl_blks) {
		ocfs2_free_cached_inode(fs, cinode);
		cinode = NULL;

		/* extend the directory */
		ret = ocfs2_extend_allocation(fs, dir, 1);
		if (ret)
			goto bail;

		ret = ocfs2_read_cached_inode(fs, dir, &cinode);
		if (ret)
			goto bail;
	}

	/* get the next free block */
	ret = ocfs2_extent_map_get_blocks(cinode, used_blks, 1,
					  &new_blk, &contig, NULL);
	if (ret) 
		goto bail;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		goto bail;

	memset(buf, 0, fs->fs_blocksize);
	de = (struct ocfs2_dir_entry *)buf;
	if (ocfs2_dir_has_trailer(fs, cinode->ci_inode)) {
		de->rec_len = ocfs2_dir_trailer_blk_off(fs);
		ocfs2_init_dir_trailer(fs, cinode->ci_inode, new_blk, buf);
	} else
		de->rec_len = fs->fs_blocksize;

	/* write new dir block */
	ret = ocfs2_write_dir_block(fs, cinode->ci_inode, new_blk, buf);
	if (ret)
		goto bail;

	/* increase the size */
	cinode->ci_inode->i_size += fs->fs_blocksize;

	/* update the size of the inode */
	ret = ocfs2_write_cached_inode(fs, cinode);
	if (ret)
		goto bail;

bail:
	if (buf)
		ocfs2_free(&buf);

	if (cinode)
		ocfs2_free_cached_inode(fs, cinode);

	return ret;
}

static void ocfs2_fill_initial_dirents(uint64_t dir, uint64_t parent,
				       char *start, uint16_t size)
{
	struct ocfs2_dir_entry *de = (struct ocfs2_dir_entry *)start;

	de->inode = dir;
	de->name_len = 1;
	de->rec_len = OCFS2_DIR_REC_LEN(de->name_len);
	de->name[0] = '.';
	de->file_type = OCFS2_FT_DIR;

	de = (struct ocfs2_dir_entry *) ((char *)de + de->rec_len);
	de->inode = parent;
	de->rec_len = size - OCFS2_DIR_REC_LEN(1);
	de->name_len = 2;
	strcpy(de->name, "..");
	de->file_type = OCFS2_FT_DIR;
}

errcode_t ocfs2_init_dir(ocfs2_filesys *fs,
			 uint64_t dir,
			 uint64_t parent_dir)
{
	errcode_t ret = 0;
	ocfs2_cached_inode *cinode = NULL;
	struct ocfs2_dinode *parent;
	uint16_t size;
	uint64_t blkno, contig;
	char *buf = NULL, *data = NULL;
	struct ocfs2_inline_data *inline_data;

	if (!(fs->fs_flags & OCFS2_FLAG_RW))
		return OCFS2_ET_RO_FILESYS;

	/* ensure it is a dir */
	ret = ocfs2_check_directory(fs, dir);
	if (ret)
		goto bail;

	/* read inode */
	ret = ocfs2_read_cached_inode(fs, dir, &cinode);
	if (ret)
		goto bail;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		goto bail;

	if (ocfs2_support_inline_data(OCFS2_RAW_SB(fs->fs_super))) {
		if (!(cinode->ci_inode->i_dyn_features & OCFS2_INLINE_DATA_FL))
			return OCFS2_ET_DIR_CORRUPTED;

		inline_data = &cinode->ci_inode->id2.i_data;
		data = (char *)inline_data->id_data;
		size = inline_data->id_count;
	} else {
		if (cinode->ci_inode->i_dyn_features & OCFS2_INLINE_DATA_FL)
			return OCFS2_ET_DIR_CORRUPTED;

		ret = ocfs2_expand_dir(fs, dir);
		if (ret)
			goto bail;

		ocfs2_free_cached_inode(fs, cinode);
		cinode = NULL;

		ret = ocfs2_read_cached_inode(fs, dir, &cinode);
		if (ret)
			goto bail;

		ret = ocfs2_extent_map_get_blocks(cinode, 0, 1,
						  &blkno, &contig, NULL);
		if (ret)
			goto bail;

		data = buf;
		size = fs->fs_blocksize;
		if (ocfs2_supports_dir_trailer(fs))
			size = ocfs2_dir_trailer_blk_off(fs);
	}

	/* set '..' and '.' in dir. */
	ocfs2_fill_initial_dirents(dir, parent_dir, data, size);

	/* And set the trailer if necessary */
	if (!(cinode->ci_inode->i_dyn_features & OCFS2_INLINE_DATA_FL) &&
	    ocfs2_supports_dir_trailer(fs))
		ocfs2_init_dir_trailer(fs, cinode->ci_inode, blkno, data);

	if (!(cinode->ci_inode->i_dyn_features & OCFS2_INLINE_DATA_FL)) {
		ret = ocfs2_write_dir_block(fs, cinode->ci_inode, blkno, buf);
		if (ret)
			goto bail;
	}

	/*
	 * Only build indexed tree if the directory is initiated as non-inline.
	 * Otherwise, the indexed tree will be build when convert the inlined
	 * directory to extent in ocfs2_expand_dir()
	 */
	if (ocfs2_supports_indexed_dirs(OCFS2_RAW_SB(fs->fs_super)) &&
	    (!cinode->ci_inode->i_dyn_features & OCFS2_INLINE_DATA_FL)) {
		ret = ocfs2_dx_dir_build(fs, dir);
		if (ret)
			goto bail;
	}

	/* set link count of the parent */
	ret = ocfs2_read_inode(fs, parent_dir, buf);
	if (ret)
		goto bail;
	parent = (struct ocfs2_dinode *)buf;
	parent->i_links_count++;
	ret = ocfs2_write_inode(fs, parent_dir, buf);
	if (ret)
		goto bail;

	/* increase the size */
	cinode->ci_inode->i_size = size;

	/* update the inode */
	ret = ocfs2_write_cached_inode(fs, cinode);

bail:
	if (buf)
		ocfs2_free(&buf);

	if (cinode)
		ocfs2_free_cached_inode(fs, cinode);

	return ret;
}
