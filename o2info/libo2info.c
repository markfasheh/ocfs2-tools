/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * libo2info.c
 *
 * Shared routines for the ocfs2 o2info utility
 *
 * Copyright (C) 2010 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#define _XOPEN_SOURCE 600
#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE

#include <errno.h>
#include <inttypes.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

#include "ocfs2/ocfs2.h"
#include "ocfs2/bitops.h"
#include "ocfs2-kernel/fiemap.h"
#include "tools-internal/verbose.h"
#include "libo2info.h"

int o2info_get_fs_features(ocfs2_filesys *fs, struct o2info_fs_features *ofs)
{
	int rc = 0;
	struct ocfs2_super_block *sb = NULL;

	memset(ofs, 0, sizeof(*ofs));

	sb = OCFS2_RAW_SB(fs->fs_super);
	ofs->compat = sb->s_feature_compat;
	ofs->incompat = sb->s_feature_incompat;
	ofs->rocompat = sb->s_feature_ro_compat;

	return rc;
}

int o2info_get_volinfo(ocfs2_filesys *fs, struct o2info_volinfo *vf)
{
	int rc = 0;
	struct ocfs2_super_block *sb = NULL;

	memset(vf, 0, sizeof(*vf));

	sb = OCFS2_RAW_SB(fs->fs_super);
	vf->blocksize = fs->fs_blocksize;
	vf->clustersize = fs->fs_clustersize;
	vf->maxslots = sb->s_max_slots;
	memcpy(vf->label, sb->s_label, OCFS2_MAX_VOL_LABEL_LEN);
	memcpy(vf->uuid_str, fs->uuid_str, OCFS2_TEXT_UUID_LEN + 1);
	rc = o2info_get_fs_features(fs, &(vf->ofs));

	return rc;
}

int o2info_get_mkfs(ocfs2_filesys *fs, struct o2info_mkfs *oms)
{
	errcode_t err;
	uint64_t blkno;
	char *buf = NULL;
	struct ocfs2_dinode *di = NULL;

	memset(oms, 0, sizeof(*oms));

	err = ocfs2_malloc_block(fs->fs_io, &buf);
	if (err) {
		tcom_err(err, "while allocating buffer");
		goto out;
	}

	err = ocfs2_lookup_system_inode(fs, JOURNAL_SYSTEM_INODE, 0, &blkno);
	if (err) {
		tcom_err(err, "while looking up journal system inode");
		goto out;
	}

	err = ocfs2_read_inode(fs, blkno, buf);
	if (err) {
		tcom_err(err, "while reading journal system inode");
		goto out;
	}

	di = (struct ocfs2_dinode *)buf;
	oms->journal_size = di->i_size;
	err = o2info_get_volinfo(fs, &(oms->ovf));

out:
	if (buf)
		ocfs2_free(&buf);

	return err;
}

int o2info_get_freeinode(ocfs2_filesys *fs, struct o2info_freeinode *ofi)
{

	int ret = 0, i, j;
	char *block = NULL;
	uint64_t inode_alloc;

	struct ocfs2_dinode *dinode_alloc = NULL;
	struct ocfs2_chain_list *cl = NULL;
	struct ocfs2_chain_rec *rec = NULL;
	struct ocfs2_super_block *sb = OCFS2_RAW_SB(fs->fs_super);

	ofi->slotnum = sb->s_max_slots;

	ret = ocfs2_malloc_block(fs->fs_io, &block);
	if (ret) {
		tcom_err(ret, "while allocating block buffer");
		goto out;
	}

	dinode_alloc = (struct ocfs2_dinode *)block;

	for (i = 0; i < ofi->slotnum; i++) {

		ofi->fi[i].total = ofi->fi[i].free = 0;

		ret = ocfs2_lookup_system_inode(fs, INODE_ALLOC_SYSTEM_INODE,
						i, &inode_alloc);
		if (ret) {
			tcom_err(ret, "while looking up the global"
				 " bitmap inode");
			goto out;
		}

		ret = ocfs2_read_inode(fs, inode_alloc, (char *)dinode_alloc);
		if (ret) {
			tcom_err(ret, "reading global_bitmap inode "
				 "%"PRIu64" for stats", inode_alloc);
			goto out;
		}

		cl = &(dinode_alloc->id2.i_chain);

		for (j = 0; j < cl->cl_next_free_rec; j++) {
			rec = &(cl->cl_recs[j]);
			ofi->fi[i].total += rec->c_total;
			ofi->fi[i].free += rec->c_free;
		}
	}
out:
	if (block)
		ocfs2_free(&block);

	return ret;
}

static int ul_log2(unsigned long arg)
{
	unsigned int i = 0;

	arg >>= 1;
	while (arg) {
		i++;
		arg >>= 1;
	}

	return i;
}

static void o2info_update_freefrag_stats(struct o2info_freefrag *off,
					 unsigned int chunksize)
{
	int index;

	index = ul_log2(chunksize);
	if (index >= OCFS2_INFO_MAX_HIST)
		index = OCFS2_INFO_MAX_HIST - 1;

	off->histogram.fc_chunks[index]++;
	off->histogram.fc_clusters[index] += chunksize;

	if (chunksize > off->max)
		off->max = chunksize;

	if (chunksize < off->min)
		off->min = chunksize;

	off->avg += chunksize;
	off->free_chunks_real++;
}

static int o2info_scan_global_bitmap_chain(ocfs2_filesys *fs,
					   struct ocfs2_chain_rec *rec,
					   struct o2info_freefrag *off)
{
	int ret = 0, used;
	uint64_t blkno;

	char *block = NULL;
	struct ocfs2_group_desc *bg = NULL;

	unsigned int max_bits, num_clusters;
	unsigned int offset = 0, cluster, chunk;
	unsigned int chunk_free, last_chunksize = 0;

	if (!rec->c_free)
		goto out;

	ret = ocfs2_malloc_block(fs->fs_io, &block);
	if (ret) {
		tcom_err(ret, "while allocating block buffer");
		goto out;
	}

	do {
		if (!bg)
			blkno = rec->c_blkno;
		else
			blkno = bg->bg_next_group;

		ret = ocfs2_read_blocks(fs, blkno, 1, block);
		if (ret < 0) {
			tcom_err(ret, "while reading group descriptor "
				 "%"PRIu64" for stats", blkno);
			goto out;
		}

		bg = (struct ocfs2_group_desc *)block;

		if (!bg->bg_free_bits_count)
			continue;

		max_bits = bg->bg_bits;
		offset = 0;

		for (chunk = 0; chunk < off->chunks_in_group; chunk++) {

			/*
			 * last chunk may be not an entire one.
			 */
			if ((offset + off->clusters_in_chunk) > max_bits)
				num_clusters = max_bits - offset;
			else
				num_clusters = off->clusters_in_chunk;

			chunk_free = 0;

			for (cluster = 0; cluster < num_clusters; cluster++) {
				used = ocfs2_test_bit(offset,
						(unsigned long *)bg->bg_bitmap);
				if (!used) {
					last_chunksize++;
					chunk_free++;
				}

				if (used && (last_chunksize)) {
					o2info_update_freefrag_stats(off,
								last_chunksize);
					last_chunksize = 0;
				}

				offset++;
			}

			if (chunk_free == off->clusters_in_chunk)
				off->free_chunks++;
		}

		/*
		 * need to update the info of last free chunk.
		 */
		if (last_chunksize)
			o2info_update_freefrag_stats(off, last_chunksize);

	} while (bg->bg_next_group);

out:
	if (block)
		ocfs2_free(&block);

	return ret;
}

static int o2info_scan_global_bitmap(ocfs2_filesys *fs,
				     struct ocfs2_chain_list *cl,
				     struct o2info_freefrag *off)
{
	int ret = 0, i;
	struct ocfs2_chain_rec *rec = NULL;

	off->chunks_in_group = (cl->cl_cpg / off->clusters_in_chunk) + 1;

	for (i = 0; i < cl->cl_next_free_rec; i++) {
		rec = &(cl->cl_recs[i]);
		ret = o2info_scan_global_bitmap_chain(fs, rec, off);
		if (ret)
			return ret;
	}

	return ret;
}

int o2info_get_freefrag(ocfs2_filesys *fs, struct o2info_freefrag *off)
{
	int ret = 0;
	char *block = NULL;

	uint64_t gb_inode;
	struct ocfs2_dinode *gb_di = NULL;
	struct ocfs2_chain_list *cl = NULL;

	ret = ocfs2_malloc_block(fs->fs_io, &block);
	if (ret) {
		tcom_err(ret, "while allocating block buffer");
		goto out;
	}

	gb_di = (struct ocfs2_dinode *)block;

	ret = ocfs2_lookup_system_inode(fs, GLOBAL_BITMAP_SYSTEM_INODE,
					0, &gb_inode);
	if (ret) {
		tcom_err(ret, "while looking up the global bitmap inode");
		goto out;
	}

	ret = ocfs2_read_inode(fs, gb_inode, (char *)gb_di);
	if (ret) {
		tcom_err(ret, "reading global_bitmap inode "
			 "%"PRIu64" for stats", gb_inode);
		goto out;
	}

	off->clusters = gb_di->id1.bitmap1.i_total;
	off->free_clusters = gb_di->id1.bitmap1.i_total -
				gb_di->id1.bitmap1.i_used;

	off->total_chunks = (off->clusters + off->clusters_in_chunk) >>
				(off->chunkbits - off->clustersize_bits);
	cl = &(gb_di->id2.i_chain);

	ret = o2info_scan_global_bitmap(fs, cl, off);
	if (ret)
		goto out;

	if (off->free_chunks_real) {
		off->min <<= (off->clustersize_bits - 10);
		off->max <<= (off->clustersize_bits - 10);
		off->avg /= off->free_chunks_real;
		off->avg <<= (off->clustersize_bits - 10);
	}

out:
	if (block)
		ocfs2_free(&block);

	return ret;
}

static int figure_extents(int fd, uint32_t *num, int flags)
{
	int ret;
	static struct fiemap fiemap;

	fiemap.fm_start = 0ULL;
	fiemap.fm_length = FIEMAP_MAX_OFFSET;

	if (flags & FIEMAP_FLAG_XATTR)
		fiemap.fm_flags = FIEMAP_FLAG_XATTR;

	fiemap.fm_extent_count = 0;
	ret = ioctl(fd, FS_IOC_FIEMAP, &fiemap);
	if (ret < 0) {
		ret = errno;
		tcom_err(ret, "fiemap get count error");
		return -1;
	}

	*num = fiemap.fm_mapped_extents;

	return 0;
}

static uint32_t clusters_in_bytes(uint32_t clustersize, uint32_t bytes)
{
	uint64_t ret = bytes + clustersize - 1;

	if (ret < bytes)
		ret = UINT64_MAX;

	ret = ret >> ul_log2(clustersize);
	if (ret > UINT32_MAX)
		ret = UINT32_MAX;

	return (uint32_t)ret;
}

static int do_fiemap(int fd, int flags, struct o2info_fiemap *ofp)
{
	char buf[4096];

	int ret = 0, last = 0;
	int cluster_shift = 0, blk_shift = 0;
	int count = (sizeof(buf) - sizeof(struct fiemap)) /
		     sizeof(struct fiemap_extent);

	struct fiemap *fiemap = (struct fiemap *)buf;
	struct fiemap_extent *fm_ext = &fiemap->fm_extents[0];
	uint32_t num_extents = 0, extents_got = 0, i;

	uint32_t prev_start = 0, prev_len = 0;
	uint32_t start = 0, len = 0, phy_pos = 0;

	if (ofp->clustersize)
		cluster_shift = ul_log2(ofp->clustersize);

	if (ofp->blocksize)
		blk_shift = ul_log2(ofp->blocksize);

	memset(fiemap, 0, sizeof(*fiemap));

	ret = figure_extents(fd, &num_extents, 0);
	if (ret)
		return -1;

	if (flags & FIEMAP_FLAG_XATTR)
		fiemap->fm_flags = FIEMAP_FLAG_XATTR;
	else
		fiemap->fm_flags = flags;

	do {
		fiemap->fm_length = ~0ULL;
		fiemap->fm_extent_count = count;

		ret = ioctl(fd, FS_IOC_FIEMAP, (unsigned long)fiemap);
		if (ret < 0) {
			ret = errno;
			if (errno == EBADR) {
				fprintf(stderr, "fiemap failed with unsupported"
					" flags %x\n", fiemap->fm_flags);
			} else
				fprintf(stderr, "fiemap error: %d, %s\n",
					ret, strerror(ret));
			return -1;
		}

		if (!fiemap->fm_mapped_extents)
			break;

		for (i = 0; i < fiemap->fm_mapped_extents; i++) {

			start = fm_ext[i].fe_logical >> cluster_shift;
			len = fm_ext[i].fe_length >> cluster_shift;
			phy_pos = fm_ext[i].fe_physical >> blk_shift;

			if (fiemap->fm_flags & FIEMAP_FLAG_XATTR) {
				ofp->xattr += len;
			} else {
				if (fm_ext[i].fe_flags &
				    FIEMAP_EXTENT_UNWRITTEN)
					ofp->unwrittens += len;

				if (fm_ext[i].fe_flags & FIEMAP_EXTENT_SHARED)
					ofp->shared += len;

				if ((prev_start + prev_len) < start)
					ofp->holes += start - prev_start -
						      prev_len;
			}

			if (fm_ext[i].fe_flags & FIEMAP_EXTENT_LAST)
				last = 1;

			prev_start = start;
			prev_len = len;

			extents_got++;
			ofp->clusters += len;
		}

		fiemap->fm_start = (fm_ext[i-1].fe_logical +
				    fm_ext[i-1].fe_length);
	} while (!last);

	if (extents_got != num_extents) {
		fprintf(stderr, "Got wrong extents number, expected:%lu, "
			"got:%lu\n", num_extents, extents_got);
		return -1;
	}

	if (flags & FIEMAP_FLAG_XATTR)
		ofp->num_extents_xattr = num_extents;
	else
		ofp->num_extents = num_extents;

	return ret;
}

int o2info_get_fiemap(int fd, int flags, struct o2info_fiemap *ofp)
{
	int ret = 0;

	ret = do_fiemap(fd, flags, ofp);
	if (ret)
		return ret;

	if ((ofp->clusters > 1) && ofp->num_extents) {
		float e = ofp->num_extents, c = ofp->clusters;
		int clusters_per_mb = clusters_in_bytes(ofp->clustersize,
							OCFS2_MAX_CLUSTERSIZE);
		ofp->frag = 100 * (e / c);
		ofp->score = ofp->frag * clusters_per_mb;
	}

	return ret;
}
