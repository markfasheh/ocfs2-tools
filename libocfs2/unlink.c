/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * unlink.c
 *
 * Remove an entry from an OCFS2 directory.  For the OCFS2 userspace
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301 USA.
 *
 *  This code is a port of e2fsprogs/lib/ext2fs/unlink.c
 *  Copyright (C) 1993, 1994, 1997 Theodore Ts'o.
 */

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE

#include <string.h>
#include <assert.h>

#include "ocfs2/ocfs2.h"


struct link_struct  {
	const char	*name;
	int		namelen;
	uint64_t	inode;
	int		flags;
	int		done;
};	

#ifdef __TURBOC__
 #pragma argsused
#endif
static int unlink_proc(struct ocfs2_dir_entry *dirent,
		     uint64_t	blocknr,
		     int	offset,
		     int	blocksize,
		     char	*buf,
		     void	*priv_data)
{
	struct link_struct *ls = (struct link_struct *) priv_data;

	if (ls->name && ((dirent->name_len & 0xFF) != ls->namelen))
		return 0;
	if (ls->name && strncmp(ls->name, dirent->name,
				dirent->name_len & 0xFF))
		return 0;
	if (ls->inode && (dirent->inode != ls->inode))
		return 0;

	dirent->inode = 0;
	ls->done++;
	return OCFS2_DIRENT_ABORT|OCFS2_DIRENT_CHANGED;
}

static errcode_t ocfs2_unlink_el(ocfs2_filesys *fs,
				uint64_t dir,
				const char *name,
				uint64_t ino,
				int flags)
{
	errcode_t ret;
	struct link_struct ls;

	ls.name = name;
	ls.namelen = name ? strlen(name) : 0;
	ls.inode = ino;
	ls.flags = 0;
	ls.done = 0;

	ret = ocfs2_dir_iterate(fs, dir, 0, 0, unlink_proc, &ls);
	if (ret)
		goto out;

	if (!ls.done)
		ret = OCFS2_ET_DIR_NO_SPACE;
out:
	return ret;
}

static errcode_t __ocfs2_delete_entry(ocfs2_filesys *fs,
				struct ocfs2_dir_entry *de_del,
				char *dir_buf)
{
	struct ocfs2_dir_entry *de, *pde;
	int offset = 0;
	errcode_t ret = 0;

	pde = NULL;
	de = (struct ocfs2_dir_entry *)dir_buf;

	while( offset < fs->fs_blocksize) {
		if (!ocfs2_check_dir_entry(fs, de, dir_buf, offset)) {
			ret = OCFS2_ET_DIR_CORRUPTED;
			goto out;
		}

		if (de == de_del) {
			if (pde)
				pde->rec_len += de->rec_len;
			else
				de->inode = 0;

			goto out;
		}
		if (de->rec_len <= 0) {
			ret = OCFS2_ET_DIR_CORRUPTED;
			goto out;
		}
		pde = de;
		offset += de->rec_len;
		de = (struct ocfs2_dir_entry *)((char *)de + de->rec_len);
	}

out:
	return ret;
}

static errcode_t ocfs2_unlink_dx(ocfs2_filesys *fs,
				uint64_t dir,
				const char *name,
				uint64_t ino,
				int flags)
{
	char *di_buf = NULL, *dx_root_buf = NULL;
	struct ocfs2_dinode *di;
	struct ocfs2_dx_root_block *dx_root;
	struct ocfs2_dx_entry_list *entry_list;
	struct ocfs2_dir_block_trailer *trailer;
	int write_dx_leaf = 0;
	int add_to_free_list = 0;
	int max_rec_len = 0;
	struct ocfs2_dir_lookup_result lookup;
	errcode_t ret;

	assert(name);

	ret = ocfs2_malloc_block(fs->fs_io, &di_buf);
	if (ret)
		goto out;
	ret = ocfs2_read_inode(fs, dir, di_buf);
	if (ret)
		goto out;
	di = (struct ocfs2_dinode *)di_buf;

	ret = ocfs2_malloc_block(fs->fs_io, &dx_root_buf);
	if (ret)
		goto out;
	ret = ocfs2_read_dx_root(fs, di->i_dx_root, dx_root_buf);
	if (ret)
		goto out;
	dx_root = (struct ocfs2_dx_root_block *)dx_root_buf;

	memset(&lookup, 0, sizeof(struct ocfs2_dir_lookup_result));
	ret= ocfs2_dx_dir_search(fs, name, strlen(name), dx_root, &lookup);
	if (ret)
		goto out;

	trailer = ocfs2_dir_trailer_from_block(fs, lookup.dl_leaf);
	if (trailer->db_free_rec_len == 0)
		add_to_free_list = 1;

	ret = __ocfs2_delete_entry(fs, lookup.dl_entry, lookup.dl_leaf);
	if (ret)
		goto out;

	max_rec_len = ocfs2_find_max_rec_len(fs, lookup.dl_leaf);
	trailer->db_free_rec_len = max_rec_len;
	if (add_to_free_list) {
		trailer->db_free_next = dx_root->dr_free_blk;
		dx_root->dr_free_blk = lookup.dl_leaf_blkno;
	}

	ret = ocfs2_write_dir_block(fs, di,
			lookup.dl_leaf_blkno, lookup.dl_leaf);
	if (ret)
		goto out;

	if (dx_root->dr_flags & OCFS2_DX_FLAG_INLINE)
		entry_list = &dx_root->dr_entries;
	else {
		entry_list = &(lookup.dl_dx_leaf->dl_list);
		write_dx_leaf = 1;
	}

	ocfs2_dx_list_remove_entry(entry_list,
				   lookup.dl_dx_entry_idx);

	if (write_dx_leaf) {
		ret = ocfs2_write_dx_leaf(fs, lookup.dl_dx_leaf_blkno, lookup.dl_dx_leaf);
		if (ret)
			goto out;
	}

	dx_root->dr_num_entries --;
	ret = ocfs2_write_dx_root(fs, di->i_dx_root, dx_root_buf);
	if (ret)
		goto out;
	ret = ocfs2_write_inode(fs, di->i_blkno, di_buf);

out:
	release_lookup_res(&lookup);
	if (dx_root_buf)
		ocfs2_free(&dx_root_buf);
	if (di_buf)
		ocfs2_free(&di_buf);

	return ret;
}

#ifdef __TURBOC__
 #pragma argsused
#endif
errcode_t ocfs2_unlink(ocfs2_filesys *fs, uint64_t dir,
			const char *name, uint64_t ino,
			int flags)
{
	errcode_t ret;
	char *di_buf = NULL;
	struct ocfs2_dinode *di;

	if (!(fs->fs_flags & OCFS2_FLAG_RW))
		return OCFS2_ET_RO_FILESYS;

	ret = ocfs2_malloc_block(fs->fs_io, &di_buf);
	if (ret)
		goto out;
	ret = ocfs2_read_inode(fs, dir, di_buf);
	if (ret)
		goto out;
	di = (struct ocfs2_dinode *)di_buf;

	if (ocfs2_supports_indexed_dirs(OCFS2_RAW_SB(fs->fs_super)) &&
	    (ocfs2_dir_indexed(di)))
		ret = ocfs2_unlink_dx(fs, dir, name, ino, flags);
	else
		ret = ocfs2_unlink_el(fs, dir, name, ino, flags);

out:
	if (di_buf)
		ocfs2_free(&di_buf);
	return ret;
}

