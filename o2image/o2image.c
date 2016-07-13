/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * o2image.c
 *
 * o2image utility to backup/restore OCFS2 metadata structures
 *
 * Copyright (C) 2008 Oracle.  All rights reserved.
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

#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include <errno.h>
#include <fcntl.h>
#include <ocfs2/bitops.h>
#include <libgen.h>
#include <sys/vfs.h>

#include "ocfs2/ocfs2.h"
#include "ocfs2/image.h"

static errcode_t traverse_inode(ocfs2_filesys *ofs, uint64_t inode);
char *program_name = NULL;

static void usage(void)
{
	fprintf(stderr, ("Usage: %s [-frI] device image_file\n"),
		program_name);
	exit(1);
}

static errcode_t mark_localalloc_bits(ocfs2_filesys *ofs,
				      struct ocfs2_local_alloc *loc)
{
	/* no need to dump space reserved in local alloc inode */
	return 0;
}

static errcode_t traverse_group_desc(ocfs2_filesys *ofs,
				     struct ocfs2_group_desc *grp,
				     int dump_type, int bpc)
{
	errcode_t ret = 0;
	uint64_t blkno;
	int i;

	blkno = grp->bg_blkno;
	for (i = 1; i < grp->bg_bits; i++) {
		blkno = ocfs2_get_block_from_group(ofs, grp, bpc, i);
		if ((dump_type == OCFS2_IMAGE_READ_INODE_YES) &&
		    ocfs2_test_bit(i, grp->bg_bitmap))
			ret = traverse_inode(ofs, blkno);
		else
			ocfs2_image_mark_bitmap(ofs, blkno);
	}
	return ret;
}

static errcode_t mark_dealloc_bits(ocfs2_filesys *ofs,
				   struct ocfs2_truncate_log *tl)
{
	/* no need to dump deleted space */
	return 0;
}

static errcode_t traverse_extents(ocfs2_filesys *ofs,
				  struct ocfs2_extent_list *el)
{
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_rec *rec;
	struct ocfs2_image_state *ost = ofs->ost;
	errcode_t ret = 0;
	char *buf = NULL;
	int i, j;

	ret = ocfs2_malloc_block(ofs->fs_io, &buf);
	if (ret)
		goto out;

	for (i = 0; i < el->l_next_free_rec; ++i) {
		rec = &(el->l_recs[i]);
		ocfs2_image_mark_bitmap(ofs, rec->e_blkno);
		if (el->l_tree_depth) {
			ret = ocfs2_read_extent_block(ofs, rec->e_blkno, buf);
			if (ret)
				goto out;
			eb = (struct ocfs2_extent_block *)buf;
			ret = traverse_extents(ofs, &(eb->h_list));
			if (ret)
				goto out;
		} else {
			for (j = 0; j < (rec->e_int_clusters*ost->ost_bpc); j++)
				ocfs2_image_mark_bitmap(ofs,
							(rec->e_blkno + j));
		}
	}
out:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

static errcode_t traverse_chains(ocfs2_filesys *ofs,
				 struct ocfs2_chain_list *cl, int dump_type)
{
	struct ocfs2_group_desc *grp;
	struct ocfs2_chain_rec *rec;
	errcode_t ret = 0;
	char *buf = NULL;
	uint64_t blkno;
	int i;

	ret = ocfs2_malloc_block(ofs->fs_io, &buf);
	if (ret) {
		com_err(program_name, ret, "while allocating block buffer "
			"to group descriptor");
		goto out;
	}

	for (i = 0; i < cl->cl_next_free_rec; i++) {
		rec = &(cl->cl_recs[i]);
		blkno = rec->c_blkno;
		while (blkno) {
			ocfs2_image_mark_bitmap(ofs, blkno);
			ret = ocfs2_read_group_desc(ofs, blkno, buf);
			if (ret)
				goto out;

			grp = (struct ocfs2_group_desc *)buf;
			if (dump_type) {
				ret = traverse_group_desc(ofs, grp,
						dump_type, cl->cl_bpc);
				if (ret)
					goto out;
			}
			blkno = grp->bg_next_group;
		}
	}

out:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

static errcode_t traverse_dx_root(ocfs2_filesys *ofs, uint64_t blkno)
{
	errcode_t ret;
	char *buf = NULL;
	struct ocfs2_dx_root_block *dx_root;

	ocfs2_image_mark_bitmap(ofs, blkno);

	ret = ocfs2_malloc_block(ofs->fs_io, &buf);
	if (ret)
		goto out;

	ret = ocfs2_read_dx_root(ofs, blkno, buf);
	if (ret)
		goto out;

	dx_root = (struct ocfs2_dx_root_block *) buf;
	if (!(dx_root->dr_flags & OCFS2_DX_FLAG_INLINE))
		traverse_extents(ofs, &dx_root->dr_list);

out:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

static errcode_t traverse_xb(ocfs2_filesys *ofs, uint64_t blkno)
{
	errcode_t ret = 0;
	char *buf = NULL;
	struct ocfs2_xattr_block *xb;

	ret = ocfs2_malloc_block(ofs->fs_io, &buf);
	if (ret)
		goto out;

	ret = ocfs2_read_xattr_block(ofs, blkno, buf);
	if (ret)
		goto out;

	xb = (struct ocfs2_xattr_block *)buf;

	if (xb->xb_flags & OCFS2_XATTR_INDEXED)
		traverse_extents(ofs, &(xb->xb_attrs.xb_root.xt_list));
	else
		/* Direct xattr block should be handled by
		 * extent_alloc scans */
		goto out;
out:
	if (buf)
		ocfs2_free(&buf);

	return ret;
}


static errcode_t traverse_inode(ocfs2_filesys *ofs, uint64_t inode)
{
	struct ocfs2_super_block *super;
	struct ocfs2_image_state *ost = ofs->ost;
	struct ocfs2_dinode *di;
	errcode_t ret = 0;
	int dump_type = 0;
	char *buf = NULL;
	int slot;

	super = OCFS2_RAW_SB(ofs->fs_super);
	ocfs2_image_mark_bitmap(ofs, inode);
	ret = ocfs2_malloc_block(ofs->fs_io, &buf);
	if (ret) {
		com_err(program_name, ret, "while allocating block buffer "
			"to bitmap inode");
		goto out;
	}

	di = (struct ocfs2_dinode *)buf;
	ret = ocfs2_read_inode(ofs, inode, (char *)di);
	if (ret) {
		com_err(program_name, ret, "while reading global bitmap inode"
			" %"PRIu64"", inode);
		goto out;
	}

	/*
	 * Do not scan inode if it's regular file. Extent blocks of regular
	 * files get backedup when scanning extent_alloc system files
	 *
	 * NOTE: we do need to handle its xattr btree if exists.
	 */
	if (!S_ISDIR(di->i_mode) && !(di->i_flags & OCFS2_SYSTEM_FL) &&
	    !(di->i_dyn_features & OCFS2_HAS_XATTR_FL))
		goto out;

	/* Read and traverse group descriptors */
	if (di->i_flags & OCFS2_SYSTEM_FL)
		dump_type = OCFS2_IMAGE_READ_INODE_NO;

	/* Do not traverse chains of a global bitmap inode */
	if (inode == ost->ost_glbl_bitmap_inode)
		dump_type = OCFS2_IMAGE_READ_CHAIN_NO;

	/*
	 * If inode is an alloc inode, read the inodes(files/directories) and
	 * traverse inode if it's a directory
	 */
	for (slot = 0; slot < super->s_max_slots; slot++)
		if (inode == ost->ost_inode_allocs[slot])
			dump_type = OCFS2_IMAGE_READ_INODE_YES;

	if (inode == ost->ost_glbl_inode_alloc) {
		if (ost->ost_glbl_inode_traversed) {
			goto out;
		} else {
			dump_type = OCFS2_IMAGE_READ_INODE_YES;
			ost->ost_glbl_inode_traversed = 1;
		}
	}

	if ((di->i_flags & OCFS2_LOCAL_ALLOC_FL))
		ret = mark_localalloc_bits(ofs, &(di->id2.i_lab));
	else if (di->i_flags & OCFS2_CHAIN_FL)
		ret = traverse_chains(ofs, &(di->id2.i_chain), dump_type);
	else if (di->i_flags & OCFS2_DEALLOC_FL)
		ret = mark_dealloc_bits(ofs, &(di->id2.i_dealloc));
	else if ((di->i_dyn_features & OCFS2_HAS_XATTR_FL) && di->i_xattr_loc)
		/* Do need to traverse xattr btree to map bucket leaves */
		ret = traverse_xb(ofs, di->i_xattr_loc);
	else {
		/*
		 * Don't check superblock flag for the dir indexing
		 * feature in case it (or the directory) is corrupted
		 * we want to try to pick up as much of the supposed
		 * index as possible.
		 *
		 * Error reporting is a bit different though. If the
		 * directory indexing feature is set on the super
		 * block, we should fail here to indicate an
		 * incomplete inode. Otherwise it is safe to ignore
		 * errors from traverse_dx_root.
		 */
		if (S_ISDIR(di->i_mode) &&
		    (di->i_dyn_features & OCFS2_INDEXED_DIR_FL)) {
			ret = traverse_dx_root(ofs, di->i_dx_root);
			if (ret && ocfs2_supports_indexed_dirs(super))
			    goto out_error;
		}
		/* traverse extents for system files */
		ret = traverse_extents(ofs, &(di->id2.i_list));
	}

out_error:
	if (ret) {
		com_err(program_name, ret, "while scanning inode %"PRIu64"",
			inode);
		goto out;
	}
out:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

static errcode_t o2image_initialize(ocfs2_filesys *ofs, int raw_flag,
				    int install_flag)
{
	uint64_t blocks[OCFS2_MAX_BACKUP_SUPERBLOCKS];
	struct ocfs2_super_block *super;
	struct ocfs2_image_state *ost = ofs->ost;
	errcode_t ret = 0;
	uint64_t blkno;
	int i, len;

	ost->ost_fsblkcnt = ofs->fs_blocks;
	ret = ocfs2_image_alloc_bitmap(ofs);
	if (ret) {
		com_err(program_name, ret, "while allocating bitmap");
		goto out;
	}

	super = OCFS2_RAW_SB(ofs->fs_super);
	ost->ost_bpc = ofs->fs_clustersize/ofs->fs_blocksize;

	/* traverse and mark super blocks for backup */
	if (super->s_feature_compat & OCFS2_FEATURE_COMPAT_BACKUP_SB) {
		len = ocfs2_get_backup_super_offsets(ofs, blocks,
						     ARRAY_SIZE(blocks));
		for (i = 0; i < len; i++)
			ocfs2_image_mark_bitmap(ofs, blocks[i]);
	}

	/* mark blocks before first cluster group for backup */
	blkno = 0;
	while(blkno <= super->s_first_cluster_group)
		ocfs2_image_mark_bitmap(ofs, blkno++);


	/* get global bitmap system inode number */
	ret = ocfs2_lookup_system_inode(ofs, GLOBAL_BITMAP_SYSTEM_INODE, 0,
					&ost->ost_glbl_bitmap_inode);
	if (ret) {
		com_err(program_name, ret, "while looking for bitmap inode");
		goto out;
	}

	/* track global_inode_alloc inode number */
	ret = ocfs2_lookup_system_inode(ofs, GLOBAL_INODE_ALLOC_SYSTEM_INODE,
					0, &ost->ost_glbl_inode_alloc);
	if (ret) {
		com_err(program_name, ret, "while looking for global inode");
		goto out;
	}
	ost->ost_glbl_inode_traversed = 0;

	/* track local alloc inode numbers for all slots */
	ret = ocfs2_malloc(((super->s_max_slots) * sizeof(blkno)),
			   &ost->ost_inode_allocs);
	if (ret) {
		com_err(program_name, ret, "while allocating mem for "
			"local_allocs");
		goto out;
	}

	for (i = 0; i < super->s_max_slots; i++) {
		ret = ocfs2_lookup_system_inode(ofs, INODE_ALLOC_SYSTEM_INODE,
						i, &ost->ost_inode_allocs[i]);
		if (ret) {
			com_err(program_name, ret, "while reading inode for "
				"slot %d", i);
			goto out;
		}
	}

out:
	return ret;
}

/*
 * This write function takes a file descriptor and pretends to be
 * pwrite64().  If the descriptor is seekable, it will just call
 * pwrite64().  Otherwise it will send zeros down to fill any holes.  The
 * caller can't go backwards in the file, because seeking may not be
 * possible.
 *
 * It returns -1 if it fails to finish up at offset+count.  It will
 * print its error, so the caller does not need to.
 */
#define ZERO_BUF_SIZE  (1<<20)
static ssize_t raw_write(ocfs2_filesys *fs, int fd, const void *buf,
			 size_t count, loff_t offset)
{
	errcode_t ret;
	ssize_t written;
	static char *zero_buf = NULL;
	static int can_seek = -1;
	static loff_t fpos = 0;
	loff_t to_write;

	if (can_seek == -1) {
		/* Test if we can seek/pwrite */
		fpos = lseek64(fd, 0, SEEK_CUR);
		if (fpos < 0) {
			fpos = 0;
			can_seek = 0;
		} else
			can_seek = 1;
	}

	if (!can_seek && !zero_buf) {
		ret = ocfs2_malloc_blocks(fs->fs_io,
					  ZERO_BUF_SIZE / fs->fs_blocksize,
					  &zero_buf);
		if (ret) {
			com_err(program_name, ret,
				"while allocating zero buffer");
			written = -1;
			goto out;
		}
		memset(zero_buf, 0, ZERO_BUF_SIZE);
	}

	if (can_seek) {
		written = pwrite64(fd, buf, count, offset);
		goto out;
	}

	/* Ok, let's fake pwrite64() for the caller */
	if (fpos > offset) {
		com_err(program_name, OCFS2_ET_INTERNAL_FAILURE,
			": file position went backwards while writing "
			"image file");
		written = -1;
		goto out;
	}

	while (fpos < offset) {
		to_write = ocfs2_min((loff_t)ZERO_BUF_SIZE, offset - fpos);
		written = write(fd, zero_buf, to_write);
		if (written < 0) {
			com_err(program_name, OCFS2_ET_IO,
				"while writing zero blocks: %s",
				strerror(errno));
			goto out;
		}
		fpos += written;
	}

	to_write = count;
	while (to_write) {
		written = write(fd, buf, to_write);
		if (written < 0) {
			com_err(program_name, OCFS2_ET_IO,
				"while writing data blocks: %s",
				strerror(errno));
			goto out;
		}
		fpos += written;
		to_write -= written;
	}
	/* Ok, we did it */
	written = count;

out:
	return written;
}

static errcode_t write_raw_image_file(ocfs2_filesys *ofs, int fd)
{
	char *blk_buf = NULL;
	uint64_t blk = -1;
	ssize_t count;
	errcode_t ret;

	ret = ocfs2_malloc_block(ofs->fs_io, &blk_buf);
	if (ret) {
		com_err(program_name, ret, "while allocating I/O buffer");
		goto out;
	}

	while (++blk < ofs->fs_blocks) {
		if (ocfs2_image_test_bit(ofs, blk)) {
			ret = ocfs2_read_blocks(ofs, blk, 1, blk_buf);
			if (ret) {
				com_err(program_name, ret,
					"while reading block %"PRIu64, blk);
				break;
			}

			count = raw_write(ofs, fd, blk_buf,
					  ofs->fs_blocksize,
					  (loff_t)(blk * ofs->fs_blocksize));
			if (count < 0) {
				ret = OCFS2_ET_IO;
				break;
			}
		}
	}

out:
	if (blk_buf)
		ocfs2_free(&blk_buf);
	return ret;
}

static errcode_t write_image_file(ocfs2_filesys *ofs, int fd)
{
	uint64_t supers[OCFS2_MAX_BACKUP_SUPERBLOCKS];
	struct ocfs2_image_state *ost = ofs->ost;
	struct ocfs2_image_hdr *hdr;
	uint64_t i, blk;
	errcode_t ret;
	int bytes;
	char *buf;

	ret = ocfs2_malloc_block(ofs->fs_io, &buf);
	if (ret) {
		com_err(program_name, ret, "allocating %lu bytes ",
			ofs->fs_blocksize);
		return ret;
	}
	hdr = (struct ocfs2_image_hdr *)buf;
	hdr->hdr_magic = OCFS2_IMAGE_MAGIC;
	memcpy(hdr->hdr_magic_desc, OCFS2_IMAGE_DESC,
	       sizeof(OCFS2_IMAGE_DESC));

	/* count metadata blocks that will be backedup */
	blk = 0;
	for (i = 0; i<ofs->fs_blocks; i++)
		if (ocfs2_image_test_bit(ofs, i))
			blk++;

	hdr->hdr_timestamp 	= time(0);
	hdr->hdr_version 	= OCFS2_IMAGE_VERSION;
	hdr->hdr_fsblkcnt 	= ofs->fs_blocks;
	hdr->hdr_fsblksz 	= ofs->fs_blocksize;
	hdr->hdr_imgblkcnt	= blk;
	hdr->hdr_bmpblksz	= ost->ost_bmpblksz;
	hdr->hdr_superblkcnt 	=
		ocfs2_get_backup_super_offsets(ofs, supers,
					       ARRAY_SIZE(supers));
	for (i = 0; i < hdr->hdr_superblkcnt; i++)
		hdr->hdr_superblocks[i] = ocfs2_image_get_blockno(ofs,
								  supers[i]);

	ocfs2_image_swap_header(hdr);
	/* o2image header size is smaller than ofs->fs_blocksize */
	bytes = write(fd, hdr, ofs->fs_blocksize);
	if (bytes < 0) {
		perror("writing header");
		ret = bytes;
		goto out;
	}
	if (bytes != ofs->fs_blocksize) {
		fprintf(stderr, "write_image: short write %d bytes", bytes);
		goto out;
	}

	/* copy metadata blocks to image files */
	for (blk = 0; blk < ofs->fs_blocks; blk++) {
		if (ocfs2_image_test_bit(ofs, blk)) {
			ret = ocfs2_read_blocks(ofs, blk, 1, buf);
			if (ret) {
				com_err(program_name, ret, "error occurred "
					"during read block %"PRIu64"", blk);
				goto out;
			}

			bytes = write(fd, buf, ofs->fs_blocksize);
			if ((bytes == -1) || (bytes < ofs->fs_blocksize)) {
				com_err(program_name, errno, "error writing "
					"blk %"PRIu64"", blk);
				goto out;
			}
		}
	}
	/* write bitmap blocks at the end */
	for(blk = 0; blk < ost->ost_bmpblks; blk++) {
		bytes = write(fd, ost->ost_bmparr[blk].arr_map,
			      ost->ost_bmpblksz);
		if ((bytes == -1) || (bytes < ost->ost_bmpblksz)) {
			com_err(program_name, errno, "error writing bitmap "
				"blk %"PRIu64" bytes %u", blk, bytes);
			goto out;
		}
	}
out:
	if (buf)
		ocfs2_free(&buf);
	if (bytes < 0)
		ret = bytes;

	return ret;
}

static errcode_t scan_raw_disk(ocfs2_filesys *ofs)
{
	struct ocfs2_image_state *ost = ofs->ost;
	uint64_t bits_set = 0;
	errcode_t ret;
	int i, j;

	/*
	 * global inode alloc has list of all metadata inodes blocks.
	 * traverse_inode recursively traverses each inode
	 */
	ret = traverse_inode(ofs, ofs->ost->ost_glbl_inode_alloc);
	if (ret)
		goto out;

	/* update set_bit_cnt for future use */
	for (i = 0; i < ost->ost_bmpblks; i++) {
		ost->ost_bmparr[i].arr_set_bit_cnt = bits_set;
		for (j = 0; j < (ost->ost_bmpblksz * 8); j++)
			if (ocfs2_test_bit(j, ost->ost_bmparr[i].arr_map))
				bits_set++;
	}

out:
	return ret;
}

static int prompt_image_creation(ocfs2_filesys *ofs, int rawflg, char *filename)
{
	int i, n;
	uint64_t free_spc;
	struct statfs stat;
	uint64_t img_size = 0;
	char *filepath;

	filepath = strdup(filename);
	statfs(dirname(filepath), &stat);
	free_spc = stat.f_bsize * stat.f_bavail;

	n = ofs->ost->ost_bmpblks - 1;
	if (!rawflg)
		img_size = ofs->ost->ost_bmpblks * ofs->ost->ost_bmpblksz;
	img_size += ofs->ost->ost_bmparr[n].arr_set_bit_cnt *
		ofs->fs_blocksize;

	for (i = 0; i < (ofs->ost->ost_bmpblksz * 8); i++)
		if (ocfs2_test_bit(i, ofs->ost->ost_bmparr[n].arr_map))
			img_size += ofs->fs_blocksize;

	fprintf(stdout, "Image file expected to be %luK, "
		"Available free space %luK. Continue ? (y/N): ",
		img_size/1024, free_spc/1024);
	if (toupper(getchar()) != 'Y') {
		fprintf(stdout, "Aborting image creation.\n");
		ocfs2_free(&filepath);
		return 0;
	}

	ocfs2_free(&filepath);
	return 1;
}

int main(int argc, char **argv)
{
	ocfs2_filesys *ofs;
	errcode_t ret;
	char *src_file	= NULL;
	char *dest_file	= NULL;
	int open_flags		= 0;
	int raw_flag      	= 0;
	int install_flag  	= 0;
	int interactive		= 0;
	int fd            	= STDOUT_FILENO;
	int c;

	if (argc && *argv)
		program_name = *argv;

	initialize_ocfs_error_table();

	optind = 0;
	while((c = getopt(argc, argv, "irI")) != EOF) {
		switch (c) {
		case 'r':
			raw_flag++;
			break;
		case 'I':
			install_flag++;
			break;
		case 'i':
			interactive = 1;
			break;
		default:
			usage();
		}
	}

	if (optind != argc -2)
		usage();

	/* We interchange src_file and image file if installing */
	if (install_flag) {
		dest_file    = argv[optind];
		src_file = argv[optind + 1];
		if ((strcmp(src_file, "-") == 0) ||
		    (strcmp(dest_file, "-") == 0)) {
			com_err(program_name, 1, "cannot install to/from "
				"file - ");
			exit(1);
		}

		fprintf(stdout, "Install %s image to %s. Continue? (y/N): ",
			src_file, dest_file);
		if (toupper(getchar()) != 'Y') {
			fprintf(stderr, "Aborting operation.\n");
			return 1;
		}
		/* if raw is not specified then we are opening image file */
		if (!raw_flag)
			open_flags = OCFS2_FLAG_IMAGE_FILE;
	} else {
		src_file  = argv[optind];
		dest_file = argv[optind + 1];
	}

	/*
	 * ocfs2_open is modified to be aware of OCFS2_FLAG_IMAGE_FILE.
	 * open routine allocates ocfs2_image_state and loads the bitmap if
	 * OCFS2_FLAG_IMAGE_FILE flag is passed in
	 */
	ret = ocfs2_open(src_file,
			 OCFS2_FLAG_RO|OCFS2_FLAG_NO_ECC_CHECKS|open_flags, 0,
			 0, &ofs);
	if (ret) {
		com_err(program_name, ret, "while trying to open \"%s\"",
			src_file);
		exit(1);
	}

	/*
	 * If src_file is opened with OCFS2_FLAG_IMAGE_FILE, then no need to
	 * allocate and initialize ocfs2_image_state. ocfs2_open would have
	 * allocated one already.
	 */
	if (!(open_flags & OCFS2_FLAG_IMAGE_FILE)) {
		ret = ocfs2_malloc0(sizeof(struct ocfs2_image_state),&ofs->ost);
		if (ret) {
			com_err(program_name, ret, "allocating memory for "
				"ocfs2_image_state");
			goto out;
		}

		ret = o2image_initialize(ofs, raw_flag, install_flag);
		if (ret) {
			com_err(program_name, ret, "during o2image initialize");
			goto out;
		}

		ret = scan_raw_disk(ofs);
		if (ret) {
			com_err(program_name, ret, "while scanning disk \"%s\"",
				src_file);
			goto out;
		}
	}

	if (strcmp(dest_file, "-") == 0)
		fd = STDOUT_FILENO;
	else {
		/* prompt user for image creation */
		if (interactive && !install_flag &&
		    !prompt_image_creation(ofs, raw_flag, dest_file))
			goto out;
		fd = open64(dest_file, O_CREAT|O_TRUNC|O_WRONLY, 0600);
		if (fd < 0) {
			com_err(program_name, errno,
				"while trying to open \"%s\"",
				argv[optind + 1]);
			goto out;
		}
	}

	/* Installs always are done in raw format */
	if (raw_flag || install_flag)
		ret = write_raw_image_file(ofs, fd);
	else
		ret = write_image_file(ofs, fd);

	if (ret) {
		com_err(program_name, ret, "while writing to image \"%s\"",
			dest_file);
		goto out;
	}

out:
	ocfs2_image_free_bitmap(ofs);

	if (ofs->ost->ost_inode_allocs)
		ocfs2_free(&ofs->ost->ost_inode_allocs);

	ret = ocfs2_close(ofs);
	if (ret)
		com_err(program_name, ret, "while closing file \"%s\"",
			src_file);

	if (fd && (fd != STDOUT_FILENO))
		close(fd);

	return ret;
}
