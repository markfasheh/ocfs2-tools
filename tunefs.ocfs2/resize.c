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

#include <limits.h>		/* for PATH_MAX */
#ifndef PATH_MAX
#define PATH_MAX 8192
#endif

#include <sys/ioctl.h>
#include <errno.h>
#include <tunefs.h>

extern ocfs2_tune_opts opts;

/*
 * This lock name is specific and only used in online resize;
 */
static char lock_name[OCFS2_LOCK_ID_MAX_LEN] = "tunefs-online-resize-lock";
static char mnt_dir[PATH_MAX];
static int fd = -1;

errcode_t online_resize_lock(ocfs2_filesys *fs)
{
	return o2dlm_lock(fs->fs_dlm_ctxt, lock_name,
			  O2DLM_LEVEL_EXMODE, O2DLM_TRYLOCK);
}

errcode_t online_resize_unlock(ocfs2_filesys *fs)
{
	return o2dlm_unlock(fs->fs_dlm_ctxt, lock_name);
}

static errcode_t find_mount_point(char *device)
{
	int mount_flags = 0;
	errcode_t ret;

	memset(mnt_dir, 0, sizeof(mnt_dir));

	ret = ocfs2_check_mount_point(device, &mount_flags,
				      mnt_dir, sizeof(mnt_dir));
	if (ret)
		goto out;

	if (!(mount_flags & OCFS2_MF_MOUNTED) ||
	    (mount_flags & OCFS2_MF_READONLY) ||
	    (mount_flags & OCFS2_MF_SWAP)) {
		ret = OCFS2_ET_BAD_DEVICE_NAME;
		goto out;
	}

	ret = 0;
out:
	return ret;
}

errcode_t online_resize_check(ocfs2_filesys *fs)
{
	/*
	 * we don't allow online resize to be coexist with other tunefs
	 * options to keep things simple.
	 */
	if (opts.backup_super || opts.vol_label || opts.num_slots ||
	     opts.mount || opts.jrnl_size) {
		com_err(opts.progname, 0, "Cannot do online-resize"
			" along with other tasks");
		exit(1);
	}

	return find_mount_point(opts.device);
}

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
		       "%"PRIu64" blocks", num_blocks);
		return -1;
	}

	if (opts.num_blocks > UINT32_MAX) {
		com_err(opts.progname, 0, "As JBD can only store block numbers "
			"in 32 bits, %s cannot be grown to more than %u "
			"blocks.", opts.device, UINT32_MAX);
		return -1;
	}

	return 0;
}

static inline errcode_t online_last_group_extend(int *new_clusters)
{
	return ioctl(fd, OCFS2_IOC_GROUP_EXTEND, new_clusters);
}

static inline errcode_t online_add_new_group(struct ocfs2_new_group_input *input)
{
	return ioctl(fd, OCFS2_IOC_GROUP_ADD, input);
}

static inline errcode_t reserve_cluster(ocfs2_filesys *fs,
					uint16_t cl_cpg,
					uint32_t cluster,
					struct ocfs2_group_desc *gd)
{
	errcode_t ret = 0;
	unsigned char *bitmap = gd->bg_bitmap;

	ret = ocfs2_set_bit(cluster % cl_cpg, bitmap);
	if (ret != 0) {
		com_err(opts.progname, 0, "while allocating backup superblock"
			"in cluster %u during volume resize", cluster);
		goto out;
	}

	gd->bg_free_bits_count--;
out:
	return ret;
}

/* Reserve the backup superblocks which exist in the new added groups. */
static errcode_t reserve_backup_in_group(ocfs2_filesys *fs,
					 struct ocfs2_dinode *di,
					 struct ocfs2_group_desc *gd,
					 uint16_t *backups)
{
	errcode_t ret = 0;
	int numsb, i;
	uint64_t blkno, gd_blkno = gd->bg_blkno;
	uint64_t blocks[OCFS2_MAX_BACKUP_SUPERBLOCKS];
	uint16_t cl_cpg = di->id2.i_chain.cl_cpg;
	uint32_t cluster;

	*backups = 0;

	if (!OCFS2_HAS_COMPAT_FEATURE(OCFS2_RAW_SB(fs->fs_super),
				      OCFS2_FEATURE_COMPAT_BACKUP_SB))
		goto out;

	numsb = ocfs2_get_backup_super_offset(fs, blocks, ARRAY_SIZE(blocks));
	if (numsb <= 0)
		goto out;

	for (i = 0; i < numsb; i++) {
		cluster = ocfs2_blocks_to_clusters(fs, blocks[i]);
		blkno = ocfs2_which_cluster_group(fs, cl_cpg, cluster);
		if (blkno < gd_blkno)
			continue;
		else if (blkno > gd_blkno)
			break;

		ret = reserve_cluster(fs, cl_cpg, cluster, gd);
		if (ret)
			goto out;
		(*backups)++;
	}

out:
	return ret;
}

static errcode_t online_resize_group_add(ocfs2_filesys *fs,
					 struct ocfs2_dinode *di,
					 uint64_t gd_blkno,
					 char *gd_buf,
					 uint16_t chain,
					 uint32_t new_clusters)
{
	errcode_t ret;
	uint16_t backups = 0, cl_bpc = di->id2.i_chain.cl_bpc;
	struct ocfs2_group_desc *gd = (struct ocfs2_group_desc *)gd_buf;
	struct ocfs2_new_group_input input;

	ret = reserve_backup_in_group(fs, di, gd, &backups);
	if (ret)
		goto out;

	ret = ocfs2_write_group_desc(fs, gd_blkno, gd_buf);
	if (ret)
		goto out;

	/*
	 * Initialize the input data and call online resize procedure.
	 * free clusters is calculated accordingly and checked in the kernel.
	 */
	memset(&input, 0, sizeof(input));

	input.group = gd_blkno;
	input.clusters = new_clusters;
	input.chain = chain;
	input.frees = gd->bg_bits/cl_bpc - 1 - backups;

	ret = online_add_new_group(&input);
	if (ret)
		com_err(opts.progname, ret,
			"while linking a new group %"PRIu64" with "
			"%u clusters to chain %u",
			gd_blkno, new_clusters, chain);
out:
	return ret;
}

/*
 * Initalize the group descriptors in the new added cluster range.
 *
 * di: global_bitmap's inode info.
 * first_new_cluster: the start cluster offset.
 * num_new_cluster: cluster range length.
 * chain: the chain position of the last group descriptor. the new
 *        group will be added to the chain after this one.
 * total_bits and used_bits will be added according to the new groups.
 */
static errcode_t init_new_gd(ocfs2_filesys *fs,
			     struct ocfs2_dinode *di,
			     uint32_t first_new_cluster,
			     uint32_t num_new_clusters,
			     uint16_t chain,
			     uint32_t *total_bits,
			     uint32_t *used_bits,
			     int online)
{
	errcode_t ret = 0;
	uint32_t cluster_chunk;
	uint64_t gd_blkno = 0;
	struct ocfs2_chain_list *cl = &di->id2.i_chain;
	struct ocfs2_chain_rec *cr = NULL;
	struct ocfs2_group_desc *gd = NULL;
	char *zero_buf = NULL;
	char *gd_buf = NULL;

	ret = ocfs2_malloc_block(fs->fs_io, &gd_buf);
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

		*used_bits += (gd->bg_bits - gd->bg_free_bits_count);
		*total_bits += gd->bg_bits;

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

		if (online) {
			ret = online_resize_group_add(fs, di, gd_blkno, gd_buf,
						      chain, cluster_chunk);
			if (ret) {
				com_err(opts.progname, ret,
					"while add a new group at "
					"block %"PRIu64" during "
					"volume online resize", gd_blkno);
				goto bail;
			}
		} else {
			/* write a new group descriptor */
			ret = ocfs2_write_group_desc(fs, gd_blkno, gd_buf);
			if (ret) {
				com_err(opts.progname, ret,
					"while writing group descriptor at "
					"block %"PRIu64" during "
					"volume resize", gd_blkno);
				goto bail;
			}
		}
	}

bail:
	if (zero_buf)
		ocfs2_free(&zero_buf);
	if (gd_buf)
		ocfs2_free(&gd_buf);
	return ret;
}

static errcode_t update_global_bitmap(ocfs2_filesys *fs,
				      struct ocfs2_dinode *di,
				      struct ocfs2_group_desc *lgd,
				      int flush_lgd)
{
	errcode_t ret = 0;

	block_signals(SIG_BLOCK);
	/* Flush that last group descriptor we updated before the new ones */
	if (flush_lgd) {
		ret = ocfs2_write_group_desc(fs, lgd->bg_blkno, (char *)lgd);
		if (ret) {
			com_err(opts.progname, ret, "while flushing group "
				"descriptor at block %"PRIu64" during "
				"volume resize", lgd->bg_blkno);
			goto bail;
		}
	}

	/* write the global bitmap inode */
	ret = ocfs2_write_inode(fs, di->i_blkno, (char *)di);
	if (ret) {
		com_err(opts.progname, ret, "while writing global bitmap "
			"inode at block %"PRIu64" during volume resize",
			di->i_blkno);
	}

bail:
	block_signals(SIG_UNBLOCK);
	return ret;
}

errcode_t update_volume_size(ocfs2_filesys *fs, int *changed, int online)
{
	errcode_t ret = 0;
	struct ocfs2_dinode *di;
	uint64_t bm_blkno = 0;
	uint64_t lgd_blkno = 0;
	char *in_buf = NULL;
	char *lgd_buf = NULL;
	struct ocfs2_chain_list *cl;
	struct ocfs2_chain_rec *cr;
	struct ocfs2_group_desc *gd;
	uint32_t cluster_chunk;
	uint32_t num_new_clusters, save_new_clusters;
	uint32_t first_new_cluster;
	uint16_t chain;
	uint32_t used_bits = 0;
	uint32_t total_bits = 0;
	uint32_t num_bits;
	int flush_lgd = 0, new_clusters;

	if (online) {
		fd = open(mnt_dir, O_RDONLY);
		if (fd < 0) {
			com_err(opts.progname, errno,
				"while opening mounted dir %s.\n", mnt_dir);
			return errno;
		}
	}

	ret = ocfs2_malloc_block(fs->fs_io, &in_buf);
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

	/*
	 * If possible round off the last group to cpg.
	 *
	 * For online resize, it is proceeded as offline resize,
	 * but the update of the group will be done by kernel.
	 */
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

		if (online) {
			new_clusters = cluster_chunk;
			ret = online_last_group_extend(&new_clusters);
			if (ret < 0) {
				com_err(opts.progname, errno, "while adding %u "
					"more clusters in the last group",
					cluster_chunk);
				goto bail;
			}
		}
	}

	/*
	 * Init the new groups and write to disk
	 * Add these groups one by one starting from the first chain after
	 * the one containing the last group.
	 */
	if (num_new_clusters) {
		ret = init_new_gd(fs, di, first_new_cluster,
				  num_new_clusters, chain,
				  &total_bits, &used_bits, online);
		if (ret)
			goto bail;
	}

	if (!online) {
		di->id1.bitmap1.i_total = total_bits;
		di->id1.bitmap1.i_used = used_bits;

		di->i_clusters += save_new_clusters;
		di->i_size = (uint64_t) di->i_clusters * fs->fs_clustersize;

		fs->fs_super->i_clusters = di->i_clusters;

		ret = update_global_bitmap(fs, di, gd, flush_lgd);
		if (ret)
			goto bail;
	}

	*changed = 1;

bail:
	if (in_buf)
		ocfs2_free(&in_buf);
	if (lgd_buf)
		ocfs2_free(&lgd_buf);

	return ret;
}
