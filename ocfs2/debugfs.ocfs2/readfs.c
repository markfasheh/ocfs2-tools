/*
 * readfs.c
 *
 * reads ocfs2 structures
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
 * Authors: Sunil Mushran
 */

#include <main.h>
#include <commands.h>
#include <dump.h>
#include <readfs.h>
#include <utils.h>

extern __u32 blksz_bits;
extern __u32 clstrsz_bits;

/*
 * read_super_block()
 *
 */
int read_super_block (int fd, char **buf)
{
	int ret = -1;
	__u64 off;
	ocfs1_vol_disk_hdr *hdr;
	ocfs2_dinode *di;
	__u32 bits = 9;
	__u32 buflen;

	buflen = 1 << bits;
	if (!(*buf = malloc(buflen)))
		DBGFS_FATAL("%s", strerror(errno));

	if ((ret = pread64(fd, *buf, buflen, 0)) == -1)
		DBGFS_FATAL("%s", strerror(errno));

	hdr = (ocfs1_vol_disk_hdr *)*buf;
	if (memcmp(hdr->signature, OCFS1_VOLUME_SIGNATURE,
		   strlen (OCFS1_VOLUME_SIGNATURE)) == 0) {
		printf("OCFS1 detected. Use debugocfs.\n");
		safefree (*buf);
		goto bail;
	}

	/*
	 * Now check at magic offset for 512, 1024, 2048, 4096
	 * blocksizes.  4096 is the maximum blocksize because it is
	 * the minimum clustersize.
	 */
	for (bits = 9; bits < 13; bits++) {
		if (!*buf) {
			buflen = 1 << bits;
			if (!(*buf = malloc(buflen)))
				DBGFS_FATAL("%s", strerror(errno));
		}

		off = OCFS2_SUPER_BLOCK_BLKNO << bits;
		if ((ret = pread64(fd, *buf, buflen, off)) == -1)
			DBGFS_FATAL("%s", strerror(errno));

		di = (ocfs2_dinode *) *buf;
		if (!memcmp(di->i_signature,
                            OCFS2_SUPER_BLOCK_SIGNATURE,
			   strlen(OCFS2_SUPER_BLOCK_SIGNATURE))) {
			ret = 0;
			break;
		}
		safefree (*buf);
	}

        if (bits >= 13)
            printf("Not an OCFS2 volume");

bail:
	return ret;
}				/* read_super_block */

/*
 * read_inode()
 *
 */
int read_inode (int fd, __u32 blknum, char *buf, int buflen)
{
	__u64 off;
	ocfs2_dinode *inode;
	int ret = 0;

	off = (__u64)(blknum << blksz_bits);

	if ((pread64(fd, buf, buflen, off)) == -1)
		DBGFS_FATAL("%s", strerror(errno));

	inode = (ocfs2_dinode *)buf;

	if (memcmp(inode->i_signature, OCFS2_FILE_ENTRY_SIGNATURE,
		   sizeof(OCFS2_FILE_ENTRY_SIGNATURE)))
		ret = -1;

	return ret;
}				/* read_inode */

/*
 * traverse_extents()
 *
 */
int traverse_extents (int fd, ocfs2_extent_list *ext, GArray *arr, int dump)
{
	ocfs2_extent_block *blk;
	ocfs2_extent_rec *rec;
	int ret = 0;
	__u64 off;
	char *buf = NULL;
	__u32 buflen;
	int i;

	if (dump)
		dump_extent_list (ext);

	for (i = 0; i < ext->l_next_free_rec; ++i) {
		rec = &(ext->l_recs[i]);
		if (ext->l_tree_depth == -1)
			add_extent_rec (arr, rec);
		else {
			buflen = 1 << blksz_bits;
			if (!(buf = malloc(buflen)))
				DBGFS_FATAL("%s", strerror(errno));

			off = (__u64)rec->e_blkno << blksz_bits;
			if ((pread64 (fd, buf, buflen, off)) == -1)
				DBGFS_FATAL("%s", strerror(errno));

			blk = (ocfs2_extent_block *)buf;

			if (dump)
				dump_extent_block (blk);

			traverse_extents (fd, &(blk->h_list), arr, dump);
		}
	}

	safefree (buf);
	return ret;
}				/* traverse_extents */

/*
 * read_dir()
 *
 */
void read_dir (struct ocfs2_dir_entry *dir, int len, GArray *arr)
{
	char *p;
	struct ocfs2_dir_entry *rec;

	p = (char *) dir;

	while (p < (((char *)dir) + len)) {
		rec = (struct ocfs2_dir_entry *)p;
		if (rec->inode)
			add_dir_rec (arr, rec);
		p += rec->rec_len;
	}

	return ;
}				/* read_dir */
