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
#include <journal.h>

extern dbgfs_gbls gbls;

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

	if ((pread64(fd, *buf, buflen, 0)) == -1)
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
		if ((pread64(fd, *buf, buflen, off)) == -1)
			DBGFS_FATAL("%s", strerror(errno));

		di = (ocfs2_dinode *) *buf;
		if (!memcmp(di->i_signature, OCFS2_SUPER_BLOCK_SIGNATURE,
			    strlen(OCFS2_SUPER_BLOCK_SIGNATURE))) {
			ret = 0;
			break;
		}
		safefree (*buf);
	}

        if (bits >= 13)
            printf("Not an OCFS2 volume\n");

bail:
	return ret;
}				/* read_super_block */

/*
 * read_inode()
 *
 */
int read_inode (int fd, __u64 blknum, char *buf, int buflen)
{
	__u64 off;
	ocfs2_dinode *inode;
	int ret = 0;

	off = blknum << gbls.blksz_bits;

	if ((pread64(fd, buf, buflen, off)) == -1)
		DBGFS_FATAL("%s", strerror(errno));

	inode = (ocfs2_dinode *)buf;

	if (memcmp(inode->i_signature, OCFS2_INODE_SIGNATURE,
		   sizeof(OCFS2_INODE_SIGNATURE)))
		ret = -1;

	return ret;
}				/* read_inode */

/*
 * traverse_extents()
 *
 */
int traverse_extents (int fd, ocfs2_extent_list *ext, GArray *arr, int dump, FILE *out)
{
	ocfs2_extent_block *blk;
	ocfs2_extent_rec *rec;
	int ret = 0;
	__u64 off;
	char *buf = NULL;
	__u32 buflen;
	int i;

	if (dump)
		dump_extent_list (out, ext);

	for (i = 0; i < ext->l_next_free_rec; ++i) {
		rec = &(ext->l_recs[i]);
		if (ext->l_tree_depth == 0)
			add_extent_rec (arr, rec);
		else {
			buflen = 1 << gbls.blksz_bits;
			if (!(buf = malloc(buflen)))
				DBGFS_FATAL("%s", strerror(errno));

			off = (__u64)rec->e_blkno << gbls.blksz_bits;
			if ((pread64 (fd, buf, buflen, off)) == -1)
				DBGFS_FATAL("%s", strerror(errno));

			blk = (ocfs2_extent_block *)buf;

			if (dump)
				dump_extent_block (out, blk);

			traverse_extents (fd, &(blk->h_list), arr, dump, out);
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
	unsigned int i = 0;
	char *buf = NULL;
	__u32 len;
	__u64 off;
	__u64 foff;

	arr = g_array_new(0, 1, sizeof(ocfs2_extent_rec));

	traverse_extents (fd, ext, arr, 0, stdout);

	for (i = 0; i < arr->len; ++i) {
		rec = &(g_array_index(arr, ocfs2_extent_rec, i));

		off = rec->e_blkno << gbls.blksz_bits;
                foff = rec->e_cpos << gbls.clstrsz_bits;
		len = rec->e_clusters << gbls.clstrsz_bits;
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
	unsigned int i, j;
	char *journal[256];
	ocfs2_super_block *sb = &((gbls.superblk)->id2.i_super);
	char tmpstr[40];

	inode = (ocfs2_dinode *)sysdir;

	if (!S_ISDIR(inode->i_mode)) {
		printf("No system directory on thei volume\n");
		goto bail;
	}

	dirarr = g_array_new(0, 1, sizeof(struct ocfs2_dir_entry));

	read_dir (fd, &(inode->id2.i_list), inode->i_size, dirarr);

	/* generate journal sysfile names */
	for (i = 0; i < sb->s_max_nodes; ++i) {
		snprintf (tmpstr, sizeof(tmpstr),
			  ocfs2_system_inode_names[JOURNAL_SYSTEM_INODE], i);
		journal[i] = strdup (tmpstr);
		gbls.journal_blkno[i] = 0;
	}

	for (i = 0; i < dirarr->len; ++i) {
		rec = &(g_array_index(dirarr, struct ocfs2_dir_entry, i));
		if (!strncmp (rec->name, dlm, strlen(dlm))) {
			gbls.dlm_blkno = rec->inode;
			continue;
		}
		for (j = 0; j < sb->s_max_nodes; ++j) {
			if (!strncmp (rec->name, journal[j], strlen(journal[j]))) {
				gbls.journal_blkno[j] = rec->inode;
				break;
			}
		}
	}

bail:
	if (dirarr)
		g_array_free (dirarr, 1);

	for (i = 0; i < sb->s_max_nodes; ++i)
		safefree (journal[i]);

	return ;
}				/* read_sysdir */

/*
 * read_file()
 *
 */
int read_file (int fd, __u64 blknum, int fdo, char **buf)
{
	ocfs2_dinode *inode = NULL;
	GArray *arr = NULL;
	ocfs2_extent_rec *rec;
	char *p = NULL;
	__u64 off, foff, len;
	unsigned int i;
	char *newbuf = NULL;
	__u64 newlen = 0;
	char *inode_buf = NULL;
	__u64 buflen = 0;
	int ret = -1;

	arr = g_array_new(0, 1, sizeof(ocfs2_extent_rec));

	buflen = 1 << gbls.blksz_bits;
	if (!(inode_buf = malloc(buflen)))
		DBGFS_FATAL("%s", strerror(errno));

	if ((read_inode (fd, blknum, inode_buf, buflen)) == -1) {
		printf("Not an inode\n");
		goto bail;
	}
	inode = (ocfs2_dinode *)inode_buf;

	traverse_extents (fd, &(inode->id2.i_list), arr, 0, stdout);

	if (fdo == -1) {
		newlen = inode->i_size;
	} else {
		newlen = 1024 * 1024;
		if (fdo > 2) {
			fchmod (fdo, inode->i_mode);
			fchown (fdo, inode->i_uid, inode->i_gid);
		}
	}

	if (!(newbuf = malloc (newlen)))
		DBGFS_FATAL("%s", strerror(errno));

	p = newbuf;

	for (i = 0; i < arr->len; ++i) {
		rec = &(g_array_index(arr, ocfs2_extent_rec, i));
		off = rec->e_blkno << gbls.blksz_bits;
		foff = rec->e_cpos << gbls.clstrsz_bits;
		len = rec->e_clusters << gbls.clstrsz_bits;
		if ((foff + len) > inode->i_size)
			len = inode->i_size - foff;

		while (len) {
			buflen = min (newlen, len);

			if ((pread64(fd, p, buflen, off)) == -1)
				DBGFS_FATAL("%s", strerror(errno));

			if (fdo != -1) {
				if (!(write (fdo, p, buflen)))
					DBGFS_FATAL("%s", strerror(errno));
			} else
				p += buflen;
			len -= buflen;
			off += buflen;
		}
	}

	ret = 0;
	if (buf) {
		*buf = newbuf;
		ret = newlen;
	}

bail:
	safefree (inode_buf);
	if (ret == -1 || !buf)
		safefree (newbuf);

	if (arr)
		g_array_free (arr, 1);

	return ret;
}				/* read_file */
