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

/*
 * read_super_block()
 *
 */
static int read_super_block (fswrk_ctxt *ctxt)
{
	int ret = -1;
	uint64_t off;
	ocfs1_vol_disk_hdr *hdr;
	ocfs2_dinode *di;
	uint32_t bits = 9;
	uint32_t buflen;
	char *buf = NULL;

	for (bits = 9; bits < 13; bits++) {
		buflen = 1 << bits;
		if (!(buf = memalign(buflen, buflen)))
			FSWRK_FATAL("%s", strerror(errno));

		if ((ret = pread64(ctxt->fd, buf, buflen, 0)) == -1) {
			safefree (buf);
			continue;
		} else
			break;
	}

	if (ret == -1)
		FSWRK_FATAL ("unable to read the first block");

	hdr = (ocfs1_vol_disk_hdr *)buf;
	if (memcmp(hdr->signature, OCFS1_VOLUME_SIGNATURE,
		   strlen (OCFS1_VOLUME_SIGNATURE)) == 0) {
		printf("OCFS1 detected\n");
		ret = -1;
		goto bail;
	}

	/*
	 * Now check at magic offset for 512, 1024, 2048, 4096
	 * blocksizes.  4096 is the maximum blocksize because it is
	 * the minimum clustersize.
	 */
	for (; bits < 13; bits++) {
		if (!buf) {
			buflen = 1 << bits;
			if (!(buf = memalign(buflen, buflen)))
				FSWRK_FATAL("%s", strerror(errno));
		}

		off = OCFS2_SUPER_BLOCK_BLKNO << bits;
		if ((pread64(ctxt->fd, buf, buflen, off)) == -1)
			FSWRK_FATAL("%s", strerror(errno));

		di = (ocfs2_dinode *)buf;
		if (!memcmp(di->i_signature, OCFS2_SUPER_BLOCK_SIGNATURE,
			    strlen(OCFS2_SUPER_BLOCK_SIGNATURE))) {
			ret = 0;
			break;
		}
		safefree (buf);
	}

	if (bits >= 13) {
		printf("Not an OCFS2 volume\n");
		ret = -1;
	}

bail:
	if (!ret)
		ctxt->super_block = (ocfs2_dinode *)buf;
	else
		safefree (buf);
	
	return ret;
}

/*
 * read_sysdir()
 *
 */
static void read_sysdir (fswrk_ctxt *ctxt)
{
	ocfs2_dinode *di;
	struct ocfs2_dir_entry *rec;
	GArray *dirarr = NULL;
	char *global_inode_file = sysfile_info[GLOBAL_INODE_ALLOC_SYSTEM_INODE].name;
	char *dlm_file = sysfile_info[DLM_SYSTEM_INODE].name;
	char *global_bitmap_file = sysfile_info[GLOBAL_BITMAP_SYSTEM_INODE].name;
	char *orphan_file = sysfile_info[ORPHAN_DIR_SYSTEM_INODE].name;
	char *extent_file[256];
	char *inode_file[256];
	char *journal_file[256];
	char *local_file[256];
	ocfs2_super_block *sb = &(ctxt->super_block->id2.i_super);
	char tmpstr[40];
	unsigned int i, j;

	di = (ocfs2_dinode *)ctxt->system_dir;

	if (!S_ISDIR(di->i_mode)) {
		printf("No system directory on the volume\n");
		goto bail;
	}

	dirarr = g_array_new(0, 1, sizeof(struct ocfs2_dir_entry));

	read_dir (ctxt, &(di->id2.i_list), di->i_size, dirarr);

	/* generate node specific sysfile names */
	for (i = 0; i < sb->s_max_nodes; ++i) {
		/* extent */
		snprintf (tmpstr, sizeof(tmpstr),
			  sysfile_info[EXTENT_ALLOC_SYSTEM_INODE].name, i);
		extent_file[i] = strdup (tmpstr);
		ctxt->sys_extent[i] = 0;

		/* inode */
		snprintf (tmpstr, sizeof(tmpstr),
			  sysfile_info[INODE_ALLOC_SYSTEM_INODE].name, i);
		inode_file[i] = strdup (tmpstr);
		ctxt->sys_inode[i] = 0;

		/* journal */
		snprintf (tmpstr, sizeof(tmpstr),
			  sysfile_info[JOURNAL_SYSTEM_INODE].name, i);
		journal_file[i] = strdup (tmpstr);
		ctxt->sys_journal[i] = 0;

		/* local */
		snprintf (tmpstr, sizeof(tmpstr),
			  sysfile_info[LOCAL_ALLOC_SYSTEM_INODE].name, i);
		local_file[i] = strdup (tmpstr);
		ctxt->sys_local[i] = 0;
	}

	/* fill blknos for system files */
	for (i = 0; i < dirarr->len; ++i) {
		rec = &(g_array_index(dirarr, struct ocfs2_dir_entry, i));

		/* global inode */
		if (!ctxt->sys_global_inode &&
		    !strncmp (rec->name, global_inode_file, strlen(global_inode_file))) {
			ctxt->sys_global_inode = rec->inode;
			continue;
		}

		/* dlm */
		if (!ctxt->sys_dlm &&
		    !strncmp (rec->name, dlm_file, strlen(dlm_file))) {
			ctxt->sys_dlm = rec->inode;
			continue;
		}

		/* global bitmap */
		if (!ctxt->sys_global_bitmap &&
		    !strncmp (rec->name, global_bitmap_file, strlen(global_bitmap_file))) {
			ctxt->sys_global_bitmap = rec->inode;
			continue;
		}

		/* orphan */
		if (!ctxt->sys_orphan &&
		    !strncmp (rec->name, orphan_file, strlen(orphan_file))) {
			ctxt->sys_orphan = rec->inode;
			continue;
		}

		for (j = 0; j < sb->s_max_nodes; ++j) {
			/* extent alloc */
			if (!ctxt->sys_extent[j] &&
			    !strncmp (rec->name, extent_file[j], strlen(extent_file[j]))) {
				ctxt->sys_extent[j] = rec->inode;
				break;
			}

			/* inode alloc */
			if (!ctxt->sys_inode[j] &&
			    !strncmp (rec->name, inode_file[j], strlen(inode_file[j]))) {
				ctxt->sys_inode[j] = rec->inode;
				break;
			}

			/* journal */
			if (!ctxt->sys_journal[j] &&
			    !strncmp (rec->name, journal_file[j], strlen(journal_file[j]))) {
				ctxt->sys_journal[j] = rec->inode;
				break;
			}

			/* local alloc */
			if (!ctxt->sys_local[j] &&
			    !strncmp (rec->name, local_file[j], strlen(local_file[j]))) {
				ctxt->sys_local[j] = rec->inode;
				break;
			}
		}
	}

bail:
	if (dirarr)
		g_array_free (dirarr, 1);

	for (i = 0; i < sb->s_max_nodes; ++i) {
		safefree (extent_file[i]);
		safefree (inode_file[i]);
		safefree (journal_file[i]);
		safefree (local_file[i]);
	}

	return ;
}


/*
 * open_fs()
 *
 */
fswrk_ctxt *open_fs (char *dev)
{
	ocfs2_super_block *sb;
	fswrk_ctxt *ctxt = NULL;

	if (!dev)
		return NULL;

	if (!(ctxt = malloc(sizeof(fswrk_ctxt))))
		FSWRK_FATAL("%s", strerror(errno));

	memset(ctxt, 0, sizeof(fswrk_ctxt));

	ctxt->fd = open (dev, O_DIRECT | O_RDWR);
	if (ctxt->fd == -1) {
		printf ("could not open device %s\n", dev);
		goto bail;
	}

	ctxt->device = g_strdup (dev);

	if (read_super_block (ctxt) == -1) {
		close (ctxt->fd);
		goto bail;
	}

	sb = &(ctxt->super_block->id2.i_super);

	/* read root inode */
	if (read_block(ctxt, sb->s_root_blkno, (char **)&ctxt->root_dir))
		FSWRK_FATAL("%s", strerror(errno));

	/* read sysdir inode */
	if (read_block(ctxt, sb->s_system_dir_blkno, (char **)&ctxt->system_dir))
		FSWRK_FATAL("%s", strerror(errno));

	/* load sysfiles blknums */
	read_sysdir(ctxt);

	/* get the max clusters/blocks */
	ctxt->max_clusters = ctxt->super_block->i_clusters;
	ctxt->max_blocks = ctxt->max_clusters << (sb->s_clustersize_bits -
						  sb->s_blocksize_bits);

	return ctxt;

bail:
	safefree(ctxt);
	return NULL;
}

/*
 * close_fs()
 *
 */
void close_fs (fswrk_ctxt *ctxt)
{
	if (ctxt->device) {
		safefree (ctxt->device);

		close (ctxt->fd);
		ctxt->fd = -1;

		safefree (ctxt->super_block);
		safefree (ctxt->root_dir);
		safefree (ctxt->system_dir);
	} else
		printf ("device not open\n");

	return ;
}

/*
 * read_inode()
 *
 */
int read_inode (fswrk_ctxt *ctxt, uint64_t blkno, char *buf)
{
	ocfs2_dinode *di;

	if (read_block(ctxt, blkno, (char **)&buf))
		FSWRK_FATAL("%s blkno=%"PRIu64, strerror(errno), blkno);

	di = (ocfs2_dinode *)buf;

	if (memcmp(di->i_signature, OCFS2_INODE_SIGNATURE,
		   sizeof(OCFS2_INODE_SIGNATURE)))
		return -1;

	return 0;
}

/*
 * read_group()
 *
 */
int read_group (fswrk_ctxt *ctxt, uint64_t blkno, char *buf)
{
	ocfs2_group_desc *bg;

	if (read_block(ctxt, blkno, (char **)&buf))
		FSWRK_FATAL("%s blkno=%"PRIu64, strerror(errno), blkno);

	bg = (ocfs2_group_desc *)buf;

	if (memcmp(bg->bg_signature, OCFS2_GROUP_DESC_SIGNATURE,
		   sizeof(OCFS2_GROUP_DESC_SIGNATURE)))
		return -1;

	return 0;
}

/*
 * traverse_extents()
 *
 */
static int traverse_extents (fswrk_ctxt *ctxt, ocfs2_extent_list *ext, GArray *arr)
{
	ocfs2_extent_block *blk;
	ocfs2_extent_rec *rec;
	int ret = 0;
	char *buf = NULL;
	int i;

	for (i = 0; i < ext->l_next_free_rec; ++i) {
		rec = &(ext->l_recs[i]);
		if (ext->l_tree_depth == 0)
			add_extent_rec (arr, rec);
		else {
			if (read_block(ctxt, rec->e_blkno, (char **)buf))
				FSWRK_FATAL("%s", strerror(errno));

			blk = (ocfs2_extent_block *)buf;

			traverse_extents (ctxt, &(blk->h_list), arr);
		}
	}

	safefree (buf);
	return ret;
}

/*
 * read_dir_block()
 *
 */
static void read_dir_block (struct ocfs2_dir_entry *dir, int len, GArray *arr)
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
}

/*
 * read_dir()
 *
 */
void read_dir (fswrk_ctxt *ctxt, ocfs2_extent_list *ext, uint64_t size, GArray *dirarr)
{
	ocfs2_extent_rec *rec;
	GArray *arr = NULL;
	unsigned int i = 0;
	char *buf = NULL;
	ocfs2_super_block *sb = &(ctxt->super_block->id2.i_super);
	uint32_t len;
	uint64_t foff;

	arr = g_array_new(0, 1, sizeof(ocfs2_extent_rec));

	traverse_extents (ctxt, ext, arr);

	for (i = 0; i < arr->len; ++i) {
		rec = &(g_array_index(arr, ocfs2_extent_rec, i));

                foff = rec->e_cpos << sb->s_clustersize_bits;
		len = rec->e_clusters << sb->s_clustersize_bits;
		if ((foff + len) > size)
			len = size - foff;

		if (read_block(ctxt, rec->e_blkno, (char **)&buf))
			FSWRK_FATAL("%s", strerror(errno));

		read_dir_block ((struct ocfs2_dir_entry *)buf, len, dirarr);

		safefree (buf);
	}

	if (arr)
		g_array_free (arr, 1);

	return ;
}

#if 0
/*
 * read_file()
 *
 */
int read_file (int fd, uint64_t blknum, int fdo, char **buf)
{
	ocfs2_dinode *inode = NULL;
	GArray *arr = NULL;
	ocfs2_extent_rec *rec;
	char *p = NULL;
	uint64_t off, foff, len;
	unsigned int i;
	char *newbuf = NULL;
	uint64_t newlen = 0;
	char *inode_buf = NULL;
	uint64_t buflen = 0;
	uint64_t rndup = 0;
	int ret = -1;

	arr = g_array_new(0, 1, sizeof(ocfs2_extent_rec));

	buflen = 1 << gbls.blocksize_bits;
	if (!(inode_buf = memalign(buflen, buflen)))
		FSWRK_FATAL("%s", strerror(errno));

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

	if (!(newbuf = memalign((1 << gbls.blocksize_bits), newlen)))
		FSWRK_FATAL("%s", strerror(errno));

	p = newbuf;

	for (i = 0; i < arr->len; ++i) {
		rec = &(g_array_index(arr, ocfs2_extent_rec, i));
		off = rec->e_blkno << gbls.blocksize_bits;
		foff = rec->e_cpos << gbls.clustersize_bits;
		len = rec->e_clusters << gbls.clustersize_bits;
		if ((foff + len) > inode->i_size)
			len = inode->i_size - foff;

		while (len) {
			buflen = min (newlen, len);
			/* rndup is reqd because source is read o_direct */
			rndup = buflen % (1 << gbls.blocksize_bits);
			rndup = (rndup ? (1 << gbls.blocksize_bits) - rndup : 0);
			buflen += rndup;

			if ((pread64(fd, p, buflen, off)) == -1)
				FSWRK_FATAL("%s", strerror(errno));

			buflen -= rndup;

			if (fdo != -1) {
				if (!(write (fdo, p, buflen)))
					FSWRK_FATAL("%s", strerror(errno));
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
#endif
