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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 *  This code is a port of e2fsprogs/lib/ext2fs/dir_iterate.c
 *  Copyright (C) 1993, 1994, 1994, 1995, 1996, 1997 Theodore Ts'o.
 */

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE

#include <inttypes.h>

#include "ocfs2/ocfs2.h"

#include "dir_iterate.h"
#include "dir_util.h"

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
					 int	offset,
					 int	blocksize,
					 char	*buf,
					 void	*priv_data),
			     void *priv_data)
{
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
	retval = ocfs2_block_iterate(fs, dir, 0,
				     ocfs2_process_dir_block, &ctx);
	if (!block_buf)
		ocfs2_free(&ctx.buf);
	if (retval)
		return retval;
	return ctx.errcode;
}

struct xlate {
	int (*func)(struct ocfs2_dir_entry *dirent,
		    int		offset,
		    int		blocksize,
		    char	*buf,
		    void	*priv_data);
	void *real_private;
};

static int xlate_func(uint64_t dir,
		      int entry,
		      struct ocfs2_dir_entry *dirent, int offset,
		      int blocksize, char *buf, void *priv_data)
{
	struct xlate *xl = (struct xlate *) priv_data;

	return (*xl->func)(dirent, offset, blocksize, buf, xl->real_private);
}

extern errcode_t ocfs2_dir_iterate(ocfs2_filesys *fs, 
				   uint64_t dir,
				   int flags,
				   char *block_buf,
				   int (*func)(struct ocfs2_dir_entry *dirent,
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
	unsigned int	next_real_entry = 0;
	int		ret = 0;
	int		changed = 0;
	int		do_abort = 0;
	int		entry, size;
	struct ocfs2_dir_entry *dirent;

	if (blockcnt < 0)
		return 0;

	entry = blockcnt ? OCFS2_DIRENT_OTHER_FILE :
		OCFS2_DIRENT_DOT_FILE;
	
	ctx->errcode = ocfs2_read_dir_block(fs, blocknr, ctx->buf);
	if (ctx->errcode)
		return OCFS2_BLOCK_ABORT;

	while (offset < fs->fs_blocksize) {
		dirent = (struct ocfs2_dir_entry *) (ctx->buf + offset);
		if (((offset + dirent->rec_len) > fs->fs_blocksize) ||
		    (dirent->rec_len < 8) ||
		    ((dirent->rec_len % 4) != 0) ||
		    (((dirent->name_len & 0xFF)+8) > dirent->rec_len)) {
			ctx->errcode = OCFS2_ET_DIR_CORRUPTED;
			return OCFS2_BLOCK_ABORT;
		}
		if (!dirent->inode &&
		    !(ctx->flags & OCFS2_DIRENT_FLAG_INCLUDE_EMPTY))
			goto next;
		if ((ctx->flags & OCFS2_DIRENT_FLAG_EXCLUDE_DOTS) &&
		    is_dots(dirent->name, dirent->name_len))
			goto next;

		ret = (ctx->func)(ctx->dir,
				  (next_real_entry > offset) ?
				  OCFS2_DIRENT_DELETED_FILE : entry,
				  dirent, offset,
				  fs->fs_blocksize, ctx->buf,
				  ctx->priv_data);
		if (entry < OCFS2_DIRENT_OTHER_FILE)
			entry++;
			
		if (ret & OCFS2_DIRENT_CHANGED)
			changed++;
		if (ret & OCFS2_DIRENT_ABORT) {
			do_abort++;
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

	if (changed) {
		ctx->errcode = ocfs2_write_dir_block(fs, blocknr,
						     ctx->buf);
		if (ctx->errcode)
			return OCFS2_BLOCK_ABORT;
	}
	if (do_abort)
		return OCFS2_BLOCK_ABORT;
	return 0;
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


