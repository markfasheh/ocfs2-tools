/*
 * resize.c
 *
 * tunefs utility for online and offline resize.
 *
 * Copyright (C) 2007 Oracle.  All rights reserved.
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
 */

#include <tunefs.h>

extern ocfs2_tune_opts opts;

void get_vol_size(ocfs2_filesys *fs)
{
	errcode_t ret = 0;
	uint64_t num_blocks;

	ret = ocfs2_get_device_size(opts.device, fs->fs_blocksize,
				    &num_blocks);
	if (ret) {
		com_err(opts.progname, ret, "while getting size of device %s",
			opts.device);
		exit(1);
	}

	if (!opts.num_blocks)
		opts.num_blocks = num_blocks;

	if (opts.num_blocks > num_blocks) {
		com_err(opts.progname, 0, "The containing partition (or device) "
			"is only %"PRIu64" blocks", num_blocks);
		exit(1);
	}

	return ;
}

int validate_vol_size(ocfs2_filesys *fs)
{
	uint64_t num_blocks;

	if (opts.num_blocks == fs->fs_blocks) {
		com_err(opts.progname, 0, "The filesystem is already "
			"%"PRIu64" blocks", fs->fs_blocks);
		return -1;
	}

	if (opts.num_blocks < fs->fs_blocks) {
		com_err(opts.progname, 0, "Cannot shrink volume size from "
		       "%"PRIu64" blocks to %"PRIu64" blocks",
		       fs->fs_blocks, opts.num_blocks);
		return -1;
	}

	num_blocks = ocfs2_clusters_to_blocks(fs, 1);
	if (num_blocks > (opts.num_blocks - fs->fs_blocks)) {
		com_err(opts.progname, 0, "Cannot grow volume size less than "
		       "%d blocks", num_blocks);
		return -1;
	}

	if (opts.num_blocks > UINT32_MAX) {
		com_err(opts.progname, 0, "As JBD can only store block numbers "
			"in 32 bits, %s cannot be grown to more than %"PRIu64" "
			"blocks.", opts.device, UINT32_MAX);
		return -1;
	}

	return 0;
}

errcode_t update_volume_size(ocfs2_filesys *fs, int *changed)
{
	errcode_t ret = 0;
	struct ocfs2_dinode *di;
	uint64_t bm_blkno = 0;
	uint64_t gd_blkno = 0;
	uint64_t lgd_blkno = 0;
	char *in_buf = NULL;
	char *gd_buf = NULL;
	char *lgd_buf = NULL;
	struct ocfs2_chain_list *cl;
	struct ocfs2_chain_rec *cr;
	struct ocfs2_group_desc *gd;
	uint32_t cluster_chunk;
	uint32_t num_new_clusters, save_new_clusters;
	uint32_t first_new_cluster;
	uint16_t chain;
	uint32_t used_bits;
	uint32_t total_bits;
	uint32_t num_bits;
	int flush_lgd = 0;
	char *zero_buf = NULL;

	ret = ocfs2_malloc_block(fs->fs_io, &in_buf);
	if (ret) {
		com_err(opts.progname, ret, "while allocating a block during "
			"volume resize");
		goto bail;
	}

	ret = ocfs2_malloc_block(fs->fs_io, &gd_buf);
	if (ret) {
		com_err(opts.progname, ret, "while allocating a block during "
			"volume resize");
		goto bail;
	}

	ret = ocfs2_malloc_block(fs->fs_io, &lgd_buf);
	if (ret) {
		com_err(opts.progname, ret, "while allocating a block during "
			"volume resize");
		goto bail;
	}

	ret = ocfs2_malloc_blocks(fs->fs_io, ocfs2_clusters_to_blocks(fs, 1),
				  &zero_buf);
	if (ret) {
		com_err(opts.progname, ret, "while allocating a cluster during "
			"volume resize");
		goto bail;
	}

	memset(zero_buf, 0, fs->fs_clustersize);

	/* read global bitmap */
	ret = ocfs2_lookup_system_inode(fs, GLOBAL_BITMAP_SYSTEM_INODE, 0,
					&bm_blkno);
	if (ret) {
		com_err(opts.progname, ret, "while looking up global bitmap "
			"inode during volume resize");
		goto bail;
	}

	ret = ocfs2_read_inode(fs, bm_blkno, in_buf);
	if (ret) {
		com_err(opts.progname, ret, "while reading inode at block "
			"%"PRIu64" during volume resize", bm_blkno);
		goto bail;
	}

	di = (struct ocfs2_dinode *)in_buf;
	cl = &(di->id2.i_chain);

	total_bits = di->id1.bitmap1.i_total;
	used_bits = di->id1.bitmap1.i_used;

	first_new_cluster = di->i_clusters;
	save_new_clusters = num_new_clusters =
		ocfs2_blocks_to_clusters(fs, opts.num_blocks) - di->i_clusters;

	/* Find the blknum of the last cluster group */
	lgd_blkno = ocfs2_which_cluster_group(fs, cl->cl_cpg, first_new_cluster - 1);

	ret = ocfs2_read_group_desc(fs, lgd_blkno, lgd_buf);
	if (ret) {
		com_err(opts.progname, ret, "while reading group descriptor "
			"at block %"PRIu64" during volume resize", lgd_blkno);
		goto bail;
	}

	gd = (struct ocfs2_group_desc *)lgd_buf;

	/* If only one cluster group then see if we need to adjust up cl_cpg */
	if (cl->cl_next_free_rec == 1) {
		if (cl->cl_cpg < 8 * gd->bg_size)
			cl->cl_cpg = 8 * gd->bg_size;
	}

	chain = gd->bg_chain;

	/* If possible round off the last group to cpg */
	cluster_chunk = MIN(num_new_clusters,
			    (cl->cl_cpg - (gd->bg_bits/cl->cl_bpc)));
	if (cluster_chunk) {
		num_new_clusters -= cluster_chunk;
		first_new_cluster += cluster_chunk;

		num_bits = cluster_chunk * cl->cl_bpc;

		gd->bg_bits += num_bits;
		gd->bg_free_bits_count += num_bits;

		cr = &(cl->cl_recs[chain]);
		cr->c_total += num_bits;
		cr->c_free += num_bits;

		total_bits += num_bits;

		fs->fs_clusters += cluster_chunk;
		fs->fs_blocks += ocfs2_clusters_to_blocks(fs, cluster_chunk);

		/* This cluster group block is written after the new */
		/* cluster groups are written to disk */
		flush_lgd = 1;
	}

	/* Init the new groups and write to disk */
	/* Add these groups one by one starting from the first chain after */
	/* the one containing the last group */

	gd = (struct ocfs2_group_desc *)gd_buf;

	while(num_new_clusters) {
		gd_blkno = ocfs2_which_cluster_group(fs, cl->cl_cpg,
						     first_new_cluster);
		cluster_chunk = MIN(num_new_clusters, cl->cl_cpg);
		num_new_clusters -= cluster_chunk;
		first_new_cluster += cluster_chunk;

		if (++chain >= cl->cl_count)
			chain = 0;

		ocfs2_init_group_desc(fs, gd, gd_blkno,
				      fs->fs_super->i_fs_generation, di->i_blkno,
				      (cluster_chunk *cl->cl_bpc), chain);

		/* Add group to chain */
		cr = &(cl->cl_recs[chain]);
		if (chain >= cl->cl_next_free_rec) {
			cl->cl_next_free_rec++;
			cr->c_free = 0;
			cr->c_total = 0;
			cr->c_blkno = 0;
		}

		gd->bg_next_group = cr->c_blkno;
		cr->c_blkno = gd_blkno;
		cr->c_free += gd->bg_free_bits_count;
		cr->c_total += gd->bg_bits;

		used_bits += (gd->bg_bits - gd->bg_free_bits_count);
		total_bits += gd->bg_bits;

		fs->fs_clusters += cluster_chunk;
		fs->fs_blocks += ocfs2_clusters_to_blocks(fs, cluster_chunk);

		/* Initialize the first cluster in the group */
		ret = io_write_block(fs->fs_io, gd_blkno,
				     ocfs2_clusters_to_blocks(fs, 1), zero_buf);
		if (ret) {
			com_err(opts.progname, ret, "while initializing the "
				"cluster starting at block %"PRIu64" during "
				"volume resize", gd_blkno);
			goto bail;
		}

		/* write a new group descriptor */
		ret = ocfs2_write_group_desc(fs, gd_blkno, gd_buf);
		if (ret) {
			com_err(opts.progname, ret, "while writing group "
				"descriptor at block %"PRIu64" during "
				"volume resize", gd_blkno);
			goto bail;
		}
	}

	di->id1.bitmap1.i_total = total_bits;
	di->id1.bitmap1.i_used = used_bits;

	di->i_clusters += save_new_clusters;
	di->i_size = (uint64_t) di->i_clusters * fs->fs_clustersize;

	fs->fs_super->i_clusters = di->i_clusters;

	block_signals(SIG_BLOCK);
	/* Flush that last group descriptor we updated before the new ones */
	if (flush_lgd) {
		ret = ocfs2_write_group_desc(fs, lgd_blkno, lgd_buf);
		if (ret) {
			block_signals(SIG_UNBLOCK);
			com_err(opts.progname, ret, "while flushing group "
				"descriptor at block %"PRIu64" during "
				"volume resize", lgd_blkno);
			goto bail;
		}
	}

	/* write the global bitmap inode */
	ret = ocfs2_write_inode(fs, bm_blkno, in_buf);
	if (ret) {
		block_signals(SIG_UNBLOCK);
		com_err(opts.progname, ret, "while writing global bitmap "
			"inode at block %"PRIu64" during volume resize",
			bm_blkno);
		goto bail;
	}

	block_signals(SIG_UNBLOCK);

	*changed = 1;

bail:
	if (zero_buf)
		ocfs2_free(&zero_buf);
	if (in_buf)
		ocfs2_free(&in_buf);
	if (gd_buf)
		ocfs2_free(&gd_buf);
	if (lgd_buf)
		ocfs2_free(&lgd_buf);

	return ret;
}
