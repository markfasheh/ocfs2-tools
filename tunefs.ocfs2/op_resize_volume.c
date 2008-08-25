/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * op_resize_volume.c
 *
 * ocfs2 tune utility to resize the volume.
 *
 * Copyright (C) 2004, 2008 Oracle.  All rights reserved.
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

#define _LARGEFILE64_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include <sys/ioctl.h>

#include "o2dlm/o2dlm.h"
#include "ocfs2/ocfs2.h"
#include "ocfs2/bitops.h"

#include "libocfs2ne.h"


/*
 * This lock name is specific and only used in online resize;
 */
static char lock_name[OCFS2_LOCK_ID_MAX_LEN] = "tunefs-online-resize-lock";


/*
 * We can handle a new size specified in bytes, blocks, or clusters.
 * However, we don't have an open filesystem at the time we parse the
 * new size.  Thus, we store off the choice in struct resize_specs until
 * we're ready to go.
 */
enum resize_units {
	RESIZE_BYTES	= 0,
	RESIZE_BLOCKS	= 1,
	RESIZE_CLUSTERS	= 2,
};
static char *resize_unit_strings[] = {
	[RESIZE_BYTES]		= "bytes:",
	[RESIZE_BLOCKS]		= "blocks:",
	[RESIZE_CLUSTERS]	= "clusters:",
};
struct resize_specs {
	enum resize_units	rs_unit;
	uint64_t		rs_size;
};

static errcode_t online_resize_lock(ocfs2_filesys *fs)
{
	return tunefs_dlm_lock(fs, lock_name, O2DLM_LEVEL_EXMODE,
			       O2DLM_TRYLOCK);
}

static errcode_t online_resize_unlock(ocfs2_filesys *fs)
{
	return tunefs_dlm_unlock(fs, lock_name);
}

static errcode_t reserve_cluster(ocfs2_filesys *fs,
				 uint16_t cl_cpg,
				 uint32_t cluster,
				 struct ocfs2_group_desc *gd)
{
	errcode_t ret = 0;
	unsigned char *bitmap = gd->bg_bitmap;

	ret = ocfs2_set_bit(cluster % cl_cpg, bitmap);
	if (ret != 0) {
		errorf("Unable to allocate the backup superblock"
			"in cluster %u\n",
			cluster);
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

	numsb = ocfs2_get_backup_super_offsets(fs, blocks, ARRAY_SIZE(blocks));
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

	ret = tunefs_online_ioctl(fs, OCFS2_IOC_GROUP_ADD, &input);
	if (ret)
		tcom_err(ret,
			 "while asking the kernel to link the group at "
			 "block %"PRIu64" to chain %u",
			 gd_blkno, chain);
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
		tcom_err(ret, "while allocating a group descriptor buffer");
		goto bail;
	}

	ret = ocfs2_malloc_blocks(fs->fs_io, ocfs2_clusters_to_blocks(fs, 1),
				  &zero_buf);
	if (ret) {
		tcom_err(ret, "while allocating a zeroing buffer");
		goto bail;
	}

	memset(zero_buf, 0, fs->fs_clustersize);
	gd = (struct ocfs2_group_desc *)gd_buf;

	while(num_new_clusters) {
		gd_blkno = ocfs2_which_cluster_group(fs, cl->cl_cpg,
						     first_new_cluster);
		cluster_chunk = ocfs2_min(num_new_clusters,
					  (uint32_t)cl->cl_cpg);
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
			tcom_err(ret,
				 "while initializing the cluster group "
				 "starting at block %"PRIu64,
				 gd_blkno);
			goto bail;
		}

		if (online) {
			ret = online_resize_group_add(fs, di, gd_blkno, gd_buf,
						      chain, cluster_chunk);
			if (ret) {
				tcom_err(ret,
					 "while trying to add the cluster "
					 "group at block %"PRIu64,
					 gd_blkno);
				goto bail;
			}
		} else {
			/* write a new group descriptor */
			ret = ocfs2_write_group_desc(fs, gd_blkno, gd_buf);
			if (ret) {
				tcom_err(ret,
					 "while writing the new group "
					 "descriptor at block %"PRIu64,
					 gd_blkno);
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

	tunefs_block_signals();
	/* Flush that last group descriptor we updated before the new ones */
	if (flush_lgd) {
		ret = ocfs2_write_group_desc(fs, lgd->bg_blkno, (char *)lgd);
		if (ret) {
			tcom_err(ret,
				 "while flushing the former tail group "
				 "descriptor to block %"PRIu64,
				 (uint64_t)lgd->bg_blkno);
			goto bail;
		}
	}

	/* write the global bitmap inode */
	ret = ocfs2_write_inode(fs, di->i_blkno, (char *)di);
	if (ret)
		tcom_err(ret,
			 "while writing the global bitmap inode to block "
			 "%"PRIu64,
			 (uint64_t)di->i_blkno);

bail:
	tunefs_unblock_signals();
	return ret;
}

static errcode_t run_resize(ocfs2_filesys *fs, uint32_t total_clusters,
			    int online)
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

	ret = ocfs2_malloc_block(fs->fs_io, &in_buf);
	if (ret) {
		tcom_err(ret, "while allocating an inode buffer during");
		goto bail;
	}

	ret = ocfs2_malloc_block(fs->fs_io, &lgd_buf);
	if (ret) {
		tcom_err(ret, "while allocating a group descriptor buffer");
		goto bail;
	}

	/* read global bitmap */
	ret = ocfs2_lookup_system_inode(fs, GLOBAL_BITMAP_SYSTEM_INODE, 0,
					&bm_blkno);
	if (ret) {
		tcom_err(ret, "while looking up the global bitmap inode");
		goto bail;
	}

	ret = ocfs2_read_inode(fs, bm_blkno, in_buf);
	if (ret) {
		tcom_err(ret,
			 "while reading the global bitmap inode from block "
			 "%"PRIu64,
			 bm_blkno);
		goto bail;
	}

	di = (struct ocfs2_dinode *)in_buf;
	cl = &(di->id2.i_chain);

	first_new_cluster = di->i_clusters;
	save_new_clusters = num_new_clusters =
		total_clusters - di->i_clusters;

	/* Find the blknum of the last cluster group */
	lgd_blkno = ocfs2_which_cluster_group(fs, cl->cl_cpg, first_new_cluster - 1);

	ret = ocfs2_read_group_desc(fs, lgd_blkno, lgd_buf);
	if (ret) {
		tcom_err(ret,
			 "while reading the tail group descriptor from "
			 "block %"PRIu64,
			 lgd_blkno);
		goto bail;
	}

	gd = (struct ocfs2_group_desc *)lgd_buf;

	/* If only one cluster group then see if we need to adjust up cl_cpg */
	if (cl->cl_next_free_rec == 1) {
		if (cl->cl_cpg < (8 * gd->bg_size))
			cl->cl_cpg = 8 * gd->bg_size;
	}

	chain = gd->bg_chain;

	/*
	 * If possible round off the last group to cpg.
	 *
	 * For online resize, we set it up like offline resize,
	 * but have the kernel do the update.
	 */
	cluster_chunk = ocfs2_min(num_new_clusters,
				  (uint32_t)(cl->cl_cpg -
					     (gd->bg_bits/cl->cl_bpc)));
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
		/* cluster groups are written to disk if we're doing
		 * an offline resize */
		flush_lgd = 1;

		if (online) {
			new_clusters = cluster_chunk;
			ret = tunefs_online_ioctl(fs,
						  OCFS2_IOC_GROUP_EXTEND,
						  &new_clusters);
			if (ret < 0) {
				tcom_err(ret,
					 "while asking the kernel to "
					 "extend the tail group descriptor "
					 "by %"PRIu32" clusters",
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
		/* Finish up the former tail group descriptor if we're
		 * an offline resize */
		di->id1.bitmap1.i_total += total_bits;
		di->id1.bitmap1.i_used += used_bits;

		di->i_clusters += save_new_clusters;
		di->i_size = (uint64_t) di->i_clusters * fs->fs_clustersize;

		fs->fs_super->i_clusters = di->i_clusters;

		ret = update_global_bitmap(fs, di, gd, flush_lgd);
	}

bail:
	if (in_buf)
		ocfs2_free(&in_buf);
	if (lgd_buf)
		ocfs2_free(&lgd_buf);

	return ret;
}

/*
 * This function does a lot of raw bit shifting because it has to handle
 * overflow of our 32bit cluster counts.  The ocfs2_%_in_%() functions
 * generally assume they are living inside a valid filesystem size.
 */
static errcode_t check_new_size(ocfs2_filesys *fs, uint64_t new_size,
				uint32_t *new_clusters)
{
	errcode_t ret;
	uint64_t max_bytes = ocfs2_clusters_to_bytes(fs, UINT32_MAX);
	uint64_t device_bytes;
	uint32_t try_clusters = ocfs2_clusters_in_bytes(fs, new_size);
	uint64_t try_blocks;
	uint64_t device_blocks;
	uint64_t device_clusters;  /* 64bits because devices can be larger
				      than ocfs2 supports */
	int b_to_c_bits =
		OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits -
		OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;

	if (new_size > max_bytes) {
		verbosef(VL_APP,
			 "Requested more than %"PRIu32" clusters "
			 "(a new size of %"PRIu64" bytes)\n",
			 UINT32_MAX, new_size);
		errorf("The ocfs2 filesystem on device \"%s\" cannot be "
		       "larger than %"PRIu32" clusters (%"PRIu64" bytes)\n",
		       fs->fs_devname, UINT32_MAX, max_bytes);
		return TUNEFS_ET_INVALID_NUMBER;
	}

	ret = ocfs2_get_device_size(fs->fs_devname, fs->fs_blocksize,
				    &device_blocks);
	if (ret) {
		tcom_err(ret, "while getting size of device \"%s\"",
			 fs->fs_devname);
		return ret;
	}

	device_clusters = device_blocks >> b_to_c_bits;
	if (device_clusters > UINT32_MAX)
		device_clusters = UINT32_MAX;
	if (!try_clusters)
		try_clusters = device_clusters;
	try_blocks = try_clusters << b_to_c_bits;

	/* Now we're guaranteed that try_clusters is within range */

	device_bytes = device_clusters <<
		OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits;
	if (try_clusters > device_clusters) {
		verbosef(VL_APP,
			 "Requested %"PRIu32" clusters (encompassing "
			 "%"PRIu64" bytes)\n",
			 try_clusters, new_size);
		errorf("The device \"%s\" cannot hold more than "
		       "%"PRIu64" clusters (%"PRIu64" bytes)\n",
		       fs->fs_devname, device_clusters, device_bytes);
		return TUNEFS_ET_INVALID_NUMBER;
	}

	if (try_clusters < fs->fs_clusters) {
		verbosef(VL_APP,
			 "Requested %"PRIu32" clusters < "
			 "current filesystem's %"PRIu32" clusters\n",
			 try_clusters, fs->fs_clusters);
		errorf("Shrinking ocfs2 filesystems is not supported\n");
		return TUNEFS_ET_INVALID_NUMBER;
	}

	if (try_blocks > UINT32_MAX) {
		verbosef(VL_APP,
			 "Requested %"PRIu32" clusters (%"PRIu64" "
			 "blocks)\n",
			 try_clusters, try_blocks);
		errorf("The Journaled Block Device (JBD) cannot "
		       "support more than %"PRIu32" blocks\n",
		       UINT32_MAX);
		return TUNEFS_ET_INVALID_NUMBER;
	}

	*new_clusters = (uint32_t)try_clusters;

	return 0;
}

static errcode_t update_volume_size_online(ocfs2_filesys *fs,
					   uint32_t new_clusters)
{
	errcode_t err, tmp;

	tunefs_block_signals();
	err = online_resize_lock(fs);
	tunefs_unblock_signals();
	if (err) {
		tcom_err(err,
			 "while locking the filesystem for online resize");
		goto out;
	}

	err = run_resize(fs, new_clusters, 1);

	tunefs_block_signals();
	tmp = online_resize_unlock(fs);
	tunefs_unblock_signals();
	if (!err) {
		err = tmp;
		if (err)
			tcom_err(err, "unlocking the filesystem");
	}

out:
	return err;
}

static errcode_t update_volume_size_offline(ocfs2_filesys *fs,
					   uint32_t new_clusters)
{
	errcode_t err;

	tunefs_block_signals();
	err = tunefs_set_in_progress(fs,
				     OCFS2_FEATURE_INCOMPAT_RESIZE_INPROG);
	tunefs_unblock_signals();
	if (err) {
		tcom_err(err,
			 "while marking the superblock for volume resize");
		goto out;
	}

	err = run_resize(fs, new_clusters, 0);
	if (err)
		goto out;

	tunefs_block_signals();
	err = tunefs_clear_in_progress(fs,
				       OCFS2_FEATURE_INCOMPAT_RESIZE_INPROG);
	if (!err) {
		err = ocfs2_write_super(fs);
		if (err)
			tcom_err(err,
				 "writing out the superblock");
	} else
		tcom_err(err,
			 "while clearing the resize operation from the "
			 "superblock");
	tunefs_unblock_signals();

out:
	return err;
}

static errcode_t update_volume_size(ocfs2_filesys *fs, uint64_t new_size,
				    int online)
{
	errcode_t err = 0;
	uint32_t new_clusters;

	err = check_new_size(fs, new_size, &new_clusters);
	if (err)
		goto out;

	if (new_clusters == fs->fs_clusters) {
		verbosef(VL_APP,
			 "Filesystem on device \"%s\" is already "
			 "%"PRIu32" clusters; nothing to do\n",
			 fs->fs_devname, new_clusters);
		goto out;
	}

	if (!tools_interact("Grow the filesystem on device \"%s\" from "
			    "%"PRIu32" to %"PRIu32" clusters? ",
			    fs->fs_devname, fs->fs_clusters, new_clusters))
		goto out;

	if (online)
		err = update_volume_size_online(fs, new_clusters);
	else
		err = update_volume_size_offline(fs, new_clusters);

out:
	return err;
}

static int resize_parse_units(struct resize_specs *specs, char *arg)
{
	int i;
	size_t len, arglen = strlen(arg);
	size_t sizecount =
		sizeof(resize_unit_strings) / sizeof(resize_unit_strings[0]);

	for (i = 0; i < sizecount; i++) {
		len = strlen(resize_unit_strings[i]);
		if (len > arglen)
			continue;
		if (strncmp(resize_unit_strings[i], arg, len))
			continue;

		specs->rs_unit = i;
		return len;
	}

	specs->rs_unit = RESIZE_BYTES;
	return 0;
}

static int resize_volume_parse_option(struct tunefs_operation *op, char *arg)
{
	int rc = 1;
	size_t len;
	errcode_t err;
	struct resize_specs *specs;

	err = ocfs2_malloc0(sizeof(struct resize_specs), &specs);
	if (err) {
		tcom_err(err, "while processing volume size options");
		goto out;
	}

	if (arg) {
		len = resize_parse_units(specs, arg);
		arg += len;
		err = tunefs_get_number(arg, &specs->rs_size);
		if (err) {
			tcom_err(err, "- new size is invalid: %s", arg);
			ocfs2_free(&specs);
			goto out;
		}
	}

	verbosef(VL_DEBUG, "Resize specifications: %"PRIu64" %s\n",
		 specs->rs_size, resize_unit_strings[specs->rs_unit]);
	op->to_private = specs;
	rc = 0;

out:
	return rc;
}

static int resize_volume_run(struct tunefs_operation *op,
			     ocfs2_filesys *fs, int flags)
{
	errcode_t err;
	uint64_t new_size;
	struct resize_specs *specs = op->to_private;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);

	new_size = specs->rs_size;
	switch (specs->rs_unit) {
		case RESIZE_CLUSTERS:
			new_size = new_size << super->s_clustersize_bits;
			break;

		case RESIZE_BLOCKS:
			new_size = new_size << super->s_blocksize_bits;

		case RESIZE_BYTES:
		default:
			break;
	}
	/* Handle wrapping */
	if (new_size < specs->rs_size)
		new_size = UINT64_MAX;

	err = update_volume_size(fs, new_size, flags & TUNEFS_FLAG_ONLINE);

	ocfs2_free(&specs);
	op->to_private = NULL;

	return err != 0;
}


DEFINE_TUNEFS_OP(resize_volume,
		 "Usage: op_resize_volume [opts] <device> [size]\n"
		 "If [size] is left out, the filesystem will be "
		 "resized to fill the volume\n",
		 TUNEFS_FLAG_RW | TUNEFS_FLAG_ALLOCATION |
		 TUNEFS_FLAG_ONLINE,
		 resize_volume_parse_option,
		 resize_volume_run);

#ifdef DEBUG_EXE
int main(int argc, char *argv[])
{
	return tunefs_op_main(argc, argv, &resize_volume_op);
}
#endif
