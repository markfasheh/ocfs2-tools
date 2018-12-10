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
 *  This code is a port of e2fsprogs/lib/ext2fs/dir_iterate.c
 *  Copyright (C) 1993, 1994, 1994, 1995, 1996, 1997 Theodore Ts'o.
 */

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE
#define _DEFAULT_SOURCE

#include <inttypes.h>

#include "ocfs2/ocfs2.h"

#include "dir_iterate.h"
#include "dir_util.h"

static int ocfs2_inline_dir_iterate(ocfs2_filesys *fs,
				    struct ocfs2_dinode *di,
				    struct dir_context *ctx);
/*
 * This function checks to see whether or not a potential deleted
 * directory entry looks valid.  What we do is check the deleted entry
 * and each successive entry to make sure that they all look valid and
 * that the last deleted entry ends at the beginning of the next
 * undeleted entry.  Returns 1 if the deleted entry looks valid, zero
 * if not valid.
 */
static int ocfs2_validate_entry(char *buf, int offset, int final_offset)
{
	struct ocfs2_dir_entry *dirent;
	
	while (offset < final_offset) {
		dirent = (struct ocfs2_dir_entry *)(buf + offset);
		offset += dirent->rec_len;
		if ((dirent->rec_len < 8) ||
		    ((dirent->rec_len % 4) != 0) ||
		    (((dirent->name_len & 0xFF)+8) > dirent->rec_len))
			return 0;
	}
	return (offset == final_offset);
}

errcode_t ocfs2_dir_iterate2(ocfs2_filesys *fs,
			     uint64_t dir,
			     int flags,
			     char *block_buf,
			     int (*func)(uint64_t	dir,
					 int		entry,
					 struct ocfs2_dir_entry *dirent,
					 uint64_t blocknr,
					 int	offset,
					 int	blocksize,
					 char	*buf,
					 void	*priv_data),
			     void *priv_data)
{
	struct ocfs2_dinode *di;
	struct		dir_context	ctx;
	errcode_t	retval;
	
	retval = ocfs2_check_directory(fs, dir);
	if (retval)
		return retval;
	
	ctx.dir = dir;
	ctx.flags = flags;
	if (block_buf)
		ctx.buf = block_buf;
	else {
		retval = ocfs2_malloc_block(fs->fs_io, &ctx.buf);
		if (retval)
			return retval;
	}
	ctx.func = func;
	ctx.priv_data = priv_data;
	ctx.errcode = 0;

	retval = ocfs2_malloc_block(fs->fs_io, &ctx.di);
	if (retval)
		goto out;

	retval = ocfs2_read_inode(fs, dir, ctx.buf);
	if (retval)
		goto out;

	/*
	 * Save off the inode - some paths use the buffer for dirent
	 * data.
	 */
	memcpy(ctx.di, ctx.buf, fs->fs_blocksize);

	di = (struct ocfs2_dinode *)ctx.buf;

	if (ocfs2_support_inline_data(OCFS2_RAW_SB(fs->fs_super)) &&
	    di->i_dyn_features & OCFS2_INLINE_DATA_FL)
		retval = ocfs2_inline_dir_iterate(fs, di, &ctx);
	else
		retval = ocfs2_block_iterate(fs, dir, 0,
					     ocfs2_process_dir_block,
					     &ctx);

out:
	if (!block_buf)
		ocfs2_free(&ctx.buf);
	if (ctx.di)
		ocfs2_free(&ctx.di);
	if (retval)
		return retval;
	return ctx.errcode;
}

struct xlate {
	int (*func)(struct ocfs2_dir_entry *dirent,
		    uint64_t	blocknr,
		    int		offset,
		    int		blocksize,
		    char	*buf,
		    void	*priv_data);
	void *real_private;
};

static int xlate_func(uint64_t dir,
		      int entry,
		      struct ocfs2_dir_entry *dirent, uint64_t blocknr,
		      int offset, int blocksize, char *buf, void *priv_data)
{
	struct xlate *xl = (struct xlate *) priv_data;

	return (*xl->func)(dirent, blocknr, offset, blocksize, buf, xl->real_private);
}

extern errcode_t ocfs2_dir_iterate(ocfs2_filesys *fs, 
				   uint64_t dir,
				   int flags,
				   char *block_buf,
				   int (*func)(struct ocfs2_dir_entry *dirent,
					       uint64_t blocknr,
					       int	offset,
					       int	blocksize,
					       char	*buf,
					       void	*priv_data),
				   void *priv_data)
{
	struct xlate xl;
	
	xl.real_private = priv_data;
	xl.func = func;

	return ocfs2_dir_iterate2(fs, dir, flags, block_buf,
				  xlate_func, &xl);
}

static int ocfs2_process_dir_entry(ocfs2_filesys *fs,
				   uint64_t blocknr,
				   unsigned int offset,
				   int entry,
				   int *changed,
				   int *do_abort,
				   struct dir_context *ctx)
{
	errcode_t ret;
	struct ocfs2_dir_entry *dirent;
	unsigned int next_real_entry = 0;
	int size;

	while (offset < fs->fs_blocksize) {
		dirent = (struct ocfs2_dir_entry *) (ctx->buf + offset);
		if (((offset + dirent->rec_len) > fs->fs_blocksize) ||
		    (dirent->rec_len < 8) ||
		    ((dirent->rec_len % 4) != 0) ||
		    (((dirent->name_len & 0xFF)+8) > dirent->rec_len)) {
			ctx->errcode = OCFS2_ET_DIR_CORRUPTED;
			return OCFS2_BLOCK_ABORT;
		}
		if (ocfs2_skip_dir_trailer(fs, ctx->di, dirent, offset)) {
			if (!(ctx->flags & OCFS2_DIRENT_FLAG_INCLUDE_TRAILER))
				goto next;
		} else if (!dirent->inode &&
			   !(ctx->flags & OCFS2_DIRENT_FLAG_INCLUDE_EMPTY)) {
			goto next;
		} else if ((ctx->flags & OCFS2_DIRENT_FLAG_EXCLUDE_DOTS) &&
			   is_dots(dirent->name, dirent->name_len)) {
			goto next;
		}

		ret = (ctx->func)(ctx->dir,
				  (next_real_entry > offset) ?
				  OCFS2_DIRENT_DELETED_FILE : entry,
				  dirent, blocknr, offset,
				  fs->fs_blocksize, ctx->buf,
				  ctx->priv_data);
		if (entry < OCFS2_DIRENT_OTHER_FILE)
			entry++;
			
		if (ret & OCFS2_DIRENT_CHANGED)
			*changed += 1;
		if (ret & OCFS2_DIRENT_ABORT) {
			*do_abort += 1;
			break;
		}
next:		
 		if (next_real_entry == offset)
			next_real_entry += dirent->rec_len;
 
 		if (ctx->flags & OCFS2_DIRENT_FLAG_INCLUDE_REMOVED) {
			size = ((dirent->name_len & 0xFF) + 11) & ~3;

			if (dirent->rec_len != size)  {
				unsigned int final_offset;

				final_offset = offset + dirent->rec_len;
				offset += size;
				while (offset < final_offset &&
				       !ocfs2_validate_entry(ctx->buf,
							     offset,
							     final_offset))
					offset += 4;
				continue;
			}
		}
		offset += dirent->rec_len;
	}

	return 0;
}

static int ocfs2_inline_dir_iterate(ocfs2_filesys *fs,
				    struct ocfs2_dinode *di,
				    struct dir_context *ctx)
{
	unsigned int offset = offsetof(struct ocfs2_dinode, id2.i_data.id_data);
	int ret = 0, changed = 0, do_abort = 0, entry;

	entry = OCFS2_DIRENT_DOT_FILE;

	ret = ocfs2_process_dir_entry(fs, di->i_blkno, offset, entry, &changed,
				      &do_abort, ctx);
	if (ret)
		return ret;

	if (changed) {
		ctx->errcode = ocfs2_write_inode(fs, di->i_blkno, ctx->buf);
		if (ctx->errcode)
			return OCFS2_BLOCK_ABORT;
	}

	return 0;
}

/*
 * Helper function which is private to this module.  Used by
 * ocfs2_dir_iterate() and ocfs2_dblist_dir_iterate()
 */
int ocfs2_process_dir_block(ocfs2_filesys *fs,
			    uint64_t	blocknr,
			    uint64_t	blockcnt,
			    uint16_t	ext_flags,
			    void	*priv_data)
{
	struct dir_context *ctx = (struct dir_context *) priv_data;
	unsigned int	offset = 0;
	int		ret = 0;
	int		changed = 0;
	int		do_abort = 0;
	int		entry;

	if (blockcnt < 0)
		return 0;

	entry = blockcnt ? OCFS2_DIRENT_OTHER_FILE :
		OCFS2_DIRENT_DOT_FILE;

	ctx->errcode = ocfs2_read_dir_block(fs, ctx->di, blocknr, ctx->buf);
	if (ctx->errcode)
		return OCFS2_BLOCK_ABORT;

	ret = ocfs2_process_dir_entry(fs, blocknr, offset, entry, &changed,
				      &do_abort, ctx);
	if (ret)
		return ret;

	if (changed) {
		ctx->errcode = ocfs2_write_dir_block(fs, ctx->di, blocknr,
						     ctx->buf);
		if (ctx->errcode)
			return OCFS2_BLOCK_ABORT;
	}
	if (do_abort)
		return OCFS2_BLOCK_ABORT;
	return 0;
}

struct dx_iterator_data {
	int (*dx_func)(ocfs2_filesys *fs,
		       struct ocfs2_dx_entry_list *entry_list,
		       struct ocfs2_dx_root_block *dx_root,
		       struct ocfs2_dx_leaf *dx_leaf,
		       void *priv_data);
	void *dx_priv_data;
	char *leaf_buf;
	struct ocfs2_dx_root_block *dx_root;
	errcode_t err;
};

static int dx_iterator(ocfs2_filesys *fs,
		       struct ocfs2_extent_rec *rec,
		       int tree_depth,
		       uint32_t ccount,
		       uint64_t ref_blkno,
		       int ref_recno,
		       void *priv_data)
{
	errcode_t err;
	int i;
	struct ocfs2_dx_leaf *dx_leaf;
	struct dx_iterator_data *iter = priv_data;
	uint64_t blkno, count;

	count = ocfs2_clusters_to_blocks(fs, rec->e_leaf_clusters);

	blkno = rec->e_blkno;
	for (i = 0; i < count; i++) {
		err = ocfs2_read_dx_leaf(fs, blkno, iter->leaf_buf);
		if (err) {
			iter->err = err;
			return OCFS2_EXTENT_ERROR;
		}

		dx_leaf = (struct ocfs2_dx_leaf *)iter->leaf_buf;
		err = iter->dx_func(fs, &dx_leaf->dl_list, iter->dx_root, dx_leaf,
			      iter->dx_priv_data);
		/* callback dx_func() is defined by users, the return value does not
		 * follow libocfs2 error codes. Don't touch iter->err and just stop
		 * the iteration here.*/
		if (err)
			return OCFS2_EXTENT_ERROR;

		blkno++;
	}

	return 0;
}

extern errcode_t ocfs2_dx_entries_iterate(ocfs2_filesys *fs,
			struct ocfs2_dinode *dir,
			int flags,
			int (*func)(ocfs2_filesys *fs,
				    struct ocfs2_dx_entry_list *entry_list,
				    struct ocfs2_dx_root_block *dx_root,
				    struct ocfs2_dx_leaf *dx_leaf,
				    void *priv_data),
			void *priv_data)
{
	errcode_t ret = 0;
	struct ocfs2_dx_root_block *dx_root;
	uint64_t dx_blkno;
	char *buf = NULL, *eb_buf = NULL, *leaf_buf = NULL;
	struct dx_iterator_data data;

	if (!S_ISDIR(dir->i_mode) && !ocfs2_dir_indexed(dir)) {
		goto out;
	}

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		goto out;

	dx_blkno = (uint64_t) dir->i_dx_root;

	ret = ocfs2_read_dx_root(fs, dx_blkno, buf);
	if (ret)
		goto out;

	dx_root = (struct ocfs2_dx_root_block *)buf;

	if (dx_root->dr_flags & OCFS2_DX_FLAG_INLINE) {
		ret = func(fs, &dx_root->dr_entries, dx_root, NULL, priv_data);
		goto out;
	}

	ret = ocfs2_malloc_block(fs->fs_io, &eb_buf);
	if (ret)
		goto out;

	ret = ocfs2_malloc_block(fs->fs_io, &leaf_buf);
	if (ret)
		goto out;

	data.dx_func = func;
	data.dx_priv_data = priv_data;
	data.leaf_buf = leaf_buf;
	data.dx_root = dx_root;
	data.err = 0;
	ret = ocfs2_extent_iterate_dx_root(fs, dx_root,
					   OCFS2_EXTENT_FLAG_DATA_ONLY, eb_buf,
					   dx_iterator, &data);
	/* dx_iterator may set the error code for non-extents-related
	 * errors. If the error code is set by dx_iterator, no matter
	 * what ocfs2_extent_iterate_dx_root() returns, we should take
	 * data.err as retured error code. */
	if (data.err)
		ret = data.err;
out:
	if (buf)
		ocfs2_free(&buf);
	if (eb_buf)
		ocfs2_free(&eb_buf);
	if (leaf_buf)
		ocfs2_free(&leaf_buf);
	return ret;
}

extern errcode_t ocfs2_dx_frees_iterate(ocfs2_filesys *fs,
			struct ocfs2_dinode *dir,
			struct ocfs2_dx_root_block *dx_root,
			int flags,
			int (*func)(ocfs2_filesys *fs,
				    uint64_t blkno,
				    struct ocfs2_dir_block_trailer *trailer,
				    char *dirblock,
				    void *priv_data),
			void *priv_data)
{
	errcode_t ret = 0;
	uint64_t blkno;
	char *buf = NULL;
	struct ocfs2_dir_block_trailer *trailer;

	if (!S_ISDIR(dir->i_mode) || !(ocfs2_dir_indexed(dir))) {
		goto out;
	}

	if (dx_root->dr_flags & OCFS2_DX_FLAG_INLINE) {
		goto out;
	}

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		goto out;

	blkno = dx_root->dr_free_blk;
	while (blkno) {
		ret = ocfs2_read_dir_block(fs, dir, blkno, buf);
		if (ret)
			goto out;

		trailer = ocfs2_dir_trailer_from_block(fs, buf);

		func(fs, blkno, trailer, buf, priv_data);

		blkno = trailer->db_free_next;
	}

out:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

#ifdef DEBUG_EXE
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

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
		"Usage: dir_iterate -i <inode_blkno> <filename>\n");
}

static int walk_names_func(struct ocfs2_dir_entry *dentry,
			   int offset,
			   int blocksize,
			   char *buf,
			   void *priv_data)
{
	char name[256];

	memcpy(name, dentry->name, dentry->name_len);
	name[dentry->name_len] = '\0';

	fprintf(stdout, "%20"PRIu64" %s\n", dentry->inode, name);

	return 0;
}



extern int opterr, optind;
extern char *optarg;

int main(int argc, char *argv[])
{
	errcode_t ret;
	uint64_t blkno;
	int c;
	char *filename, *buf;
	ocfs2_filesys *fs;
	struct ocfs2_dinode *di;

	blkno = 0;

	initialize_ocfs_error_table();

	while ((c = getopt(argc, argv, "i:")) != EOF) {
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

			default:
				print_usage();
				return 1;
				break;
		}
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

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret) {
		com_err(argv[0], ret,
			"while allocating inode buffer");
		goto out_close;
	}

	if (blkno == 0)
		blkno = fs->fs_root_blkno;

	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret) {
		com_err(argv[0], ret, "while reading inode %"PRIu64, blkno);
		goto out_free;
	}

	di = (struct ocfs2_dinode *)buf;

	fprintf(stdout, "OCFS2 inode %"PRIu64" on \"%s\"\n",
		blkno, filename);

	ret = ocfs2_dir_iterate(fs, blkno, 0, NULL,
				walk_names_func, NULL);
	if (ret) {
		com_err(argv[0], ret,
			"while listing inode %"PRIu64" on \"%s\"\n",
			blkno, filename);
		goto out_free;
	}

out_free:
	ocfs2_free(&buf);

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


