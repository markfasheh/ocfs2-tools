/*
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

static void scan_journal(FILE *out, char *buf, int len,
			 uint64_t *blocknum, uint64_t *last_unknown)
{
	char *block;
	char *p;
	int type;
	journal_header_t *header;

	p = buf;

	while (len) {
		block = p;
		header = (journal_header_t *)block;
		if (header->h_magic == ntohl(JFS_MAGIC_NUMBER)) {
			if (*last_unknown) {
				dump_jbd_unknown(out, *last_unknown, *blocknum);
				*last_unknown = 0;
			}
			dump_jbd_block(out, header, *blocknum);
		} else {
			type = detect_block(block);
			if (type < 0) {
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

	ret = ocfs2_read_cached_inode(fs, blkno, &ci);
	if (ret) {
		com_err(gbls.cmd, ret, "while reading inode %"PRIu64, blkno);
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
			dump_jbd_superblock(out, (journal_superblock_t *)buf);
			blocknum++;
			p += fs->fs_blocksize;
			len -= fs->fs_blocksize;
		}

		scan_journal(out, p, len, &blocknum, &last_unknown);

		if (got < buflen)
			break;
		offset += got;
	}

	if (last_unknown) {
		dump_jbd_unknown(out, last_unknown, blocknum);
		last_unknown = 0;
	}

bail:
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
int detect_block (char *buf)
{
	struct ocfs2_dinode *inode;
	struct ocfs2_extent_block *extent;
	struct ocfs2_group_desc *group;
	int ret = -1;

	inode = (struct ocfs2_dinode *)buf;
	if (!memcmp(inode->i_signature, OCFS2_INODE_SIGNATURE,
		    sizeof(OCFS2_INODE_SIGNATURE))) {
		ret = 1;
		goto bail;
	}

	extent = (struct ocfs2_extent_block *)buf;
	if (!memcmp(extent->h_signature, OCFS2_EXTENT_BLOCK_SIGNATURE,
		    sizeof(OCFS2_EXTENT_BLOCK_SIGNATURE))) {
		ret = 2;
		goto bail;
	}

	group = (struct ocfs2_group_desc *)buf;
	if (!memcmp(group->bg_signature, OCFS2_GROUP_DESC_SIGNATURE,
		    sizeof(OCFS2_GROUP_DESC_SIGNATURE))) {
		ret = 3;
		goto bail;
	}

bail:
	return ret;
}				/* detect_block */
