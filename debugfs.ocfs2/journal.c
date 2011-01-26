/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * journal.c
 *
 * reads the journal file
 *
 * Copyright (C) 2004, 2007 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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
 * Authors: Sunil Mushran, Mark Fasheh
 */

#include "main.h"

extern dbgfs_gbls gbls;

static enum dump_block_type detect_block (char *buf);

static void scan_journal(FILE *out, journal_superblock_t *jsb, char *buf,
			 int len, uint64_t *blocknum, uint64_t *last_unknown)
{
	char *block;
	char *p;
	enum dump_block_type type;
	journal_header_t *header;

	p = buf;

	while (len) {
		block = p;
		header = (journal_header_t *)block;
		if (header->h_magic == ntohl(JBD2_MAGIC_NUMBER)) {
			if (*last_unknown) {
				dump_jbd_unknown(out, *last_unknown, *blocknum);
				*last_unknown = 0;
			}
			dump_jbd_block(out, jsb, header, *blocknum);
		} else {
			type = detect_block(block);
			if (type == DUMP_BLOCK_UNKNOWN) {
				if (*last_unknown == 0)
					*last_unknown = *blocknum;
			} else {
				if (*last_unknown) {
					dump_jbd_unknown(out, *last_unknown,
							 *blocknum);
					*last_unknown = 0;
				}
				dump_jbd_metadata(out, type, block, *blocknum);
			}
		}
		(*blocknum)++;
		p += gbls.fs->fs_blocksize;
		len -= gbls.fs->fs_blocksize;
	}

	return;
}

errcode_t read_journal(ocfs2_filesys *fs, uint64_t blkno, FILE *out)
{
	char *buf = NULL;
	char *jsb_buf = NULL;
	char *p;
	uint64_t blocknum;
	uint64_t len;
	uint64_t offset;
	uint32_t got;
	uint64_t last_unknown = 0;
	uint32_t buflen = 1024 * 1024;
	int buflenbits;
	ocfs2_cached_inode *ci = NULL;
	errcode_t ret;
	journal_superblock_t *jsb;

	ret = ocfs2_read_cached_inode(fs, blkno, &ci);
	if (ret) {
		com_err(gbls.cmd, ret, "while reading inode %"PRIu64, blkno);
		goto bail;
	}

	ret = ocfs2_malloc_block(fs->fs_io, &jsb_buf);
	if (ret) {
		com_err(gbls.cmd, ret,
			"while allocating journal superblock buffer");
		goto bail;
	}

	buflenbits = buflen >>
			OCFS2_RAW_SB(gbls.fs->fs_super)->s_blocksize_bits;
	ret = ocfs2_malloc_blocks(fs->fs_io, buflenbits, &buf);
	if (ret) {
		com_err(gbls.cmd, ret, "while allocating %u bytes", buflen);
		goto bail;
	}

	offset = 0;
	blocknum = 0;
	jsb = (journal_superblock_t *)jsb_buf;
	while (1) {
		ret = ocfs2_file_read(ci, buf, buflen, offset, &got);
		if (ret) {
			com_err(gbls.cmd, ret, "while reading journal");
			goto bail;
		};

		if (got == 0)
			break;

		p = buf;
		len = got;

		if (offset == 0) {
			memcpy(jsb_buf, buf, fs->fs_blocksize);
			dump_jbd_superblock(out, jsb);
			ocfs2_swap_journal_superblock(jsb);
			blocknum++;
			p += fs->fs_blocksize;
			len -= fs->fs_blocksize;
		}

		scan_journal(out, jsb, p, len, &blocknum, &last_unknown);

		if (got < buflen)
			break;
		offset += got;
	}

	if (last_unknown) {
		dump_jbd_unknown(out, last_unknown, blocknum);
		last_unknown = 0;
	}

bail:
	if (jsb_buf)
		ocfs2_free(&jsb_buf);
	if (buf)
		ocfs2_free(&buf);
	if (ci)
		ocfs2_free_cached_inode(fs, ci);

	return ret;
}

/*
 * detect_block()
 *
 */
static enum dump_block_type detect_block (char *buf)
{
	struct ocfs2_dinode *inode;
	struct ocfs2_extent_block *extent;
	struct ocfs2_group_desc *group;
	struct ocfs2_dir_block_trailer *trailer;
	struct ocfs2_xattr_block *xb;
	struct ocfs2_refcount_block *rb;
	struct ocfs2_dx_root_block *dx_root;
	struct ocfs2_dx_leaf *dx_leaf;
	enum dump_block_type ret = DUMP_BLOCK_UNKNOWN;

	inode = (struct ocfs2_dinode *)buf;
	if (!strncmp((char *)inode->i_signature, OCFS2_INODE_SIGNATURE,
		     sizeof(inode->i_signature))) {
		ret = DUMP_BLOCK_INODE;
		goto bail;
	}

	extent = (struct ocfs2_extent_block *)buf;
	if (!strncmp((char *)extent->h_signature, OCFS2_EXTENT_BLOCK_SIGNATURE,
		     sizeof(extent->h_signature))) {
		ret = DUMP_BLOCK_EXTENT_BLOCK;
		goto bail;
	}

	group = (struct ocfs2_group_desc *)buf;
	if (!strncmp((char *)group->bg_signature, OCFS2_GROUP_DESC_SIGNATURE,
		     sizeof(group->bg_signature))) {
		ret = DUMP_BLOCK_GROUP_DESCRIPTOR;
		goto bail;
	}

	trailer = ocfs2_dir_trailer_from_block(gbls.fs, buf);
	if (!strncmp((char *)trailer->db_signature, OCFS2_DIR_TRAILER_SIGNATURE,
		     sizeof(trailer->db_signature))) {
		ret = DUMP_BLOCK_DIR_BLOCK;
		goto bail;
	}

	xb = (struct ocfs2_xattr_block *)buf;
	if (!strncmp((char *)xb->xb_signature, OCFS2_XATTR_BLOCK_SIGNATURE,
		     sizeof(xb->xb_signature))) {
		ret = DUMP_BLOCK_XATTR;
		goto bail;
	}

	rb = (struct ocfs2_refcount_block *)buf;
	if (!strncmp((char *)rb->rf_signature, OCFS2_REFCOUNT_BLOCK_SIGNATURE,
		     sizeof(rb->rf_signature))) {
		ret = DUMP_BLOCK_REFCOUNT;
		goto bail;
	}

	dx_root = (struct ocfs2_dx_root_block *)buf;
	if (!strncmp((char *)dx_root->dr_signature, OCFS2_DX_ROOT_SIGNATURE,
		     strlen(OCFS2_DX_ROOT_SIGNATURE))) {
		ret = DUMP_BLOCK_DXROOT;
		goto bail;
	}

	dx_leaf = (struct ocfs2_dx_leaf *)buf;
	if (!strncmp((char *)dx_leaf->dl_signature, OCFS2_DX_LEAF_SIGNATURE,
		     strlen(OCFS2_DX_LEAF_SIGNATURE))) {
		ret = DUMP_BLOCK_DXLEAF;
		goto bail;
	}

bail:
	return ret;
}				/* detect_block */
