/*
 * journal.c
 *
 * reads the journal file
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
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

#include <main.h>
#include <commands.h>
#include <readfs.h>
#include <utils.h>
#include <journal.h>
#include <dump.h>
#include <bindraw.h>

extern dbgfs_gbls gbls;

/*
 * read_journal()
 *
 */
void read_journal (FILE *out, char *buf, __u64 buflen)
{
	char *block;
	__u64 blocknum;
	journal_header_t *header;
	__u32 blksize = 1 << gbls.blksz_bits;
	__u64 len;
	char *p;
	__u64 last_unknown = 0;
	int type;

	dump_jbd_superblock (out, (journal_superblock_t *) buf);

	blocknum = 1;
	p = buf + blksize;
	len = buflen - blksize;

	while (len) {
		block = p;
		header = (journal_header_t *) block;
		if (header->h_magic == ntohl(JFS_MAGIC_NUMBER)) {
			if (last_unknown) {
				dump_jbd_unknown (out, last_unknown, blocknum);
				last_unknown = 0;
			}
			dump_jbd_block (out, header, blocknum);
		} else {
			type = detect_block (block);
			if (type < 0) {
				if (last_unknown == 0)
					last_unknown = blocknum;
			} else {
				if (last_unknown) {
					dump_jbd_unknown (out, last_unknown, blocknum);
					last_unknown = 0;
				}
				dump_jbd_metadata (out, type, block, blocknum);
			}
		}
		blocknum++;
		p += blksize;
		len -= blksize;
	}

	if (last_unknown) {
		dump_jbd_unknown (out, last_unknown, blocknum);
		last_unknown = 0;
	}

	return ;
}				/* read_journal */

/*
 * detect_block()
 *
 */
int detect_block (char *buf)
{
	ocfs2_dinode *inode;
	ocfs2_extent_block *extent;
	int ret = -1;

	inode = (ocfs2_dinode *)buf;
	if (!memcmp(inode->i_signature, OCFS2_INODE_SIGNATURE,
		    sizeof(OCFS2_INODE_SIGNATURE))) {
		ret = 1;
		goto bail;
	}

	extent = (ocfs2_extent_block *)buf;
	if (!memcmp(extent->h_signature, OCFS2_EXTENT_BLOCK_SIGNATURE,
		    sizeof(OCFS2_EXTENT_BLOCK_SIGNATURE))) {
		ret = 2;
		goto bail;
	}

bail:
	return ret;
}				/* detect_block */
