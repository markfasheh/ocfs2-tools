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
extern __u64 dlm_blkno;
extern char *superblk;

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
		if (ext->l_tree_depth == 0)
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
 * read_dir_block()
 *
 */
void read_dir_block (struct ocfs2_dir_entry *dir, int len, GArray *arr)
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
}				/* read_dir_block */

/*
 * read_dir()
 *
 */
void read_dir (int fd, ocfs2_extent_list *ext, __u64 size, GArray *dirarr)
{
	ocfs2_extent_rec *rec;
	GArray *arr = NULL;
	int i = 0;
	char *buf = NULL;
	__u32 len;
	__u64 off;
	__u64 foff;

	arr = g_array_new(0, 1, sizeof(ocfs2_extent_rec));

	traverse_extents (fd, ext, arr, 0);

	for (i = 0; i < arr->len; ++i) {
		rec = &(g_array_index(arr, ocfs2_extent_rec, i));

		off = rec->e_blkno << blksz_bits;
                foff = rec->e_cpos << clstrsz_bits;
		len = rec->e_clusters << clstrsz_bits;
                if ((foff + len) > size)
                    len = size - foff;

		if (!(buf = malloc (len)))
			DBGFS_FATAL("%s", strerror(errno));

		if ((pread64(fd, buf, len, off)) == -1)
			DBGFS_FATAL("%s", strerror(errno));

		read_dir_block ((struct ocfs2_dir_entry *)buf, len, dirarr);

		safefree (buf);
	}

	if (arr)
		g_array_free (arr, 1);

	return ;
}				/* read_dir */

/*
 * read_sysdir()
 *
 */
void read_sysdir (int fd, char *sysdir)
{
	ocfs2_dinode *inode;
	struct ocfs2_dir_entry *rec;
	GArray *dirarr = NULL;
	char *dlm = ocfs2_system_inode_names[DLM_SYSTEM_INODE];
	int i;

	inode = (ocfs2_dinode *)sysdir;

	if (!S_ISDIR(inode->i_mode)) {
		printf("No system directory on thei volume\n");
		goto bail;
	}

	dirarr = g_array_new(0, 1, sizeof(struct ocfs2_dir_entry));

	read_dir (fd, &(inode->id2.i_list), inode->i_size, dirarr);

	for (i = 0; i < dirarr->len; ++i) {
		rec = &(g_array_index(dirarr, struct ocfs2_dir_entry, i));
		if (!strncmp (rec->name, dlm, strlen(dlm)))
			dlm_blkno = rec->inode;
	}

bail:
	if (dirarr)
		g_array_free (dirarr, 1);


	return ;
}				/* read_sysdir */

/*
 * read_file()
 *
 */
void read_file (int fd, ocfs2_extent_list *ext, __u64 size, char *buf, int fdo)
{
	GArray *arr = NULL;
	ocfs2_extent_rec *rec;
	char *p;
	__u64 off, foff, len;
	int i;
	char *newbuf = NULL;
	__u32 newlen = 0;

	arr = g_array_new(0, 1, sizeof(ocfs2_extent_rec));

	traverse_extents (fd, ext, arr, 0);

	p = buf;

	for (i = 0; i < arr->len; ++i) {
		rec = &(g_array_index(arr, ocfs2_extent_rec, i));
		off = rec->e_blkno << blksz_bits;
		foff = rec->e_cpos << clstrsz_bits;
		len = rec->e_clusters << clstrsz_bits;
		if ((foff + len) > size)
			len = size - foff;

		if (fd != -1) {
			if (newlen <= len) {
				safefree (newbuf);
				if (!(newbuf = malloc (len)))
					DBGFS_FATAL("%s", strerror(errno));
				newlen = len;
				p = newbuf;
			}
		}

		if ((pread64(fd, p, len, off)) == -1)
			DBGFS_FATAL("%s", strerror(errno));

		if (fd != -1) {
			if (len)
				if (!(write (fdo, p, len)))
					DBGFS_FATAL("%s", strerror(errno));
		} else
			p += len;
	}

	safefree (newbuf);

	if (arr)
		g_array_free (arr, 1);

	return ;
}				/* read_file */

/*
 * process_dlm()
 *
 */
void process_dlm (int fd, int type)
{
	char *buf = NULL;
	__u32 buflen;
	char *dlmbuf = NULL;
	ocfs2_dinode *inode;
	ocfs2_super_block *sb = &(((ocfs2_dinode *)superblk)->id2.i_super);

	/* get the dlm inode */
	buflen = 1 << blksz_bits;
	if (!(buf = malloc(buflen)))
		DBGFS_FATAL("%s", strerror(errno));

	if ((read_inode (fd, dlm_blkno, buf, buflen)) == -1) {
		printf("Invalid dlm system file\n");
		goto bail;
	}
	inode = (ocfs2_dinode *)buf;

	/* length of file to read */
	buflen = 2 + 4 + (3 * sb->s_max_nodes);
	buflen <<= blksz_bits;

	/* alloc the buffer */
	if (!(dlmbuf = malloc (buflen)))
		DBGFS_FATAL("%s", strerror(errno));

	read_file (fd, &(inode->id2.i_list), buflen, dlmbuf, -1);

	switch (type) {
	case CONFIG:
		dump_config (dlmbuf);
		break;
	case PUBLISH:
		dump_publish (dlmbuf);
		break;
	case VOTE:
		dump_vote (dlmbuf);
		break;
	default:
		break;
	}

bail:
	safefree (buf);
	safefree (dlmbuf);

	return ;
}				/* process_dlm */
