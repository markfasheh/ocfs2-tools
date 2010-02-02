/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * refcount.c
 *
 * Copyright (C) 2009 Oracle.  All rights reserved.
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

#include <assert.h>
#include "main.h"

extern char *progname;

/*
 * Create a refcount tree.
 *
 * If tree = 0, the root is just a refcount block with refcount recs.
 * Otherwise, we will create a refcount extent tree.
 *
 */
static void create_refcount_tree(ocfs2_filesys *fs, uint64_t blkno,
				 uint64_t *rf_blkno, int tree_depth)
{
	errcode_t ret;
	uint64_t file1, file2, file3, root_blkno, new_clusters, tmpblk;
	int i, recs_num = ocfs2_refcount_recs_per_rb(fs->fs_blocksize);
	int bpc = ocfs2_clusters_to_blocks(fs, 1), offset = 0;
	uint32_t n_clusters;
	uint64_t file_size;

	/*
	 * Create 3 files.
	 * file1 and file2 are used to sharing a refcount tree.
	 * file2 is used to waste some clusters so that the refcount
	 * tree won't be increased easily.
	 */
	create_file(fs, blkno, &file1);
	create_file(fs, blkno, &file2);
	create_file(fs, blkno, &file3);

	ret = ocfs2_new_refcount_block(fs, &root_blkno, 0, 0);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	/* attach the create refcount tree in these 2 files. */
	ret = ocfs2_attach_refcount_tree(fs, file1, root_blkno);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);
	ret = ocfs2_attach_refcount_tree(fs, file2, root_blkno);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	/*
	 * Calculate how much clusters we need in order to create
	 * the required extent tree.
	 */
	new_clusters = 1;
	while (tree_depth-- > 0)
		new_clusters *= recs_num;
	new_clusters += recs_num / 2;

	/*
	 * We double the new_clusters so that half of them will be inserted
	 * into tree, while another half is inserted into file3.
	 */
	new_clusters *= 2;
	while (new_clusters) {
		ret = ocfs2_new_clusters(fs, 1, new_clusters, &blkno,
					 &n_clusters);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);
		if (!n_clusters)
			FSWRK_FATAL("ENOSPC");

		/*
		 * In order to ensure the extent records are not coalesced,
		 * we insert each cluster in reverse.
		 */
		for (i = n_clusters; i > 1; i -= 2, offset++) {
			tmpblk = blkno + ocfs2_clusters_to_blocks(fs, i - 2);
			ret = ocfs2_inode_insert_extent(fs, file1, offset,
							tmpblk, 1,
							OCFS2_EXT_REFCOUNTED);
			if (ret)
				FSWRK_COM_FATAL(progname, ret);
			ret = ocfs2_inode_insert_extent(fs, file2, offset,
							tmpblk, 1,
							OCFS2_EXT_REFCOUNTED);
			if (ret)
				FSWRK_COM_FATAL(progname, ret);

			ret = ocfs2_change_refcount(fs, root_blkno,
					ocfs2_blocks_to_clusters(fs, tmpblk),
					1, 2);
			if (ret)
				FSWRK_COM_FATAL(progname, ret);

			ret = ocfs2_inode_insert_extent(fs, file3, offset,
							tmpblk + bpc, 1, 0);
			if (ret)
				FSWRK_COM_FATAL(progname, ret);

		}
		new_clusters -= n_clusters;
	}

	file_size = (offset + 1) * fs->fs_clustersize;
	ret = ocfs2_extend_file(fs, file1, file_size);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);
	ret = ocfs2_extend_file(fs, file2, file_size);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);
	ret = ocfs2_extend_file(fs, file3, file_size);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);


	*rf_blkno = root_blkno;

	return;
}

static void damage_refcount_block(ocfs2_filesys *fs, enum fsck_type type,
				  struct ocfs2_refcount_block *rb)
{
	uint32_t oldno;
	uint64_t oldblkno;

	switch (type) {
	case RB_BLKNO:
		oldblkno = rb->rf_blkno;
		rb->rf_blkno += 1;
		fprintf(stdout, "RB_BLKNO: "
			"change refcount block's number from %"PRIu64" to "
			"%"PRIu64"\n", oldblkno, (uint64_t)rb->rf_blkno);
		break;
	case RB_GEN:
	case RB_GEN_FIX:
		oldno = rb->rf_fs_generation;
		rb->rf_fs_generation = 0x1234;
		if (type == RB_GEN)
			fprintf(stdout, "RB_GEN: ");
		else if (type == RB_GEN_FIX)
			fprintf(stdout, "RB_GEN_FIX: ");
		fprintf(stdout, "change refcount block %"PRIu64
			" generation number from 0x%x to 0x%x\n",
			(uint64_t)rb->rf_blkno, oldno, rb->rf_fs_generation);
		break;
	case RB_PARENT:
		oldblkno = rb->rf_parent;
		rb->rf_parent += 1;
		fprintf(stdout, "RB_PARENT: "
			"change refcount block's parent from %"PRIu64" to "
			"%"PRIu64"\n", oldblkno, (uint64_t)rb->rf_parent);
		break;
	case REFCOUNT_BLOCK_INVALID:
	case REFCOUNT_ROOT_BLOCK_INVALID:
		memset(rb->rf_signature, 'a', sizeof(rb->rf_signature));
		fprintf(stdout, "Corrupt the signature of refcount block "
			"%"PRIu64"\n", (uint64_t)rb->rf_blkno);
		break;
	default:
		FSWRK_FATAL("Invalid type=%d", type);
	}
}

static void damage_refcount_list(ocfs2_filesys *fs, enum fsck_type type,
				 struct ocfs2_refcount_block *rb)
{
	uint32_t oldno;
	uint64_t oldblkno;

	switch (type) {
	case REFCOUNT_LIST_COUNT:
		oldno = rb->rf_records.rl_count;
		rb->rf_records.rl_count *= 2;
		fprintf(stdout, "REFCOUNT_LIST_COUNT: Corrupt refcount block #"
			"%"PRIu64", change rl_count from %u to %u\n",
			(uint64_t)rb->rf_blkno, oldno, rb->rf_records.rl_count);
		break;
	case REFCOUNT_LIST_USED:
		oldno = rb->rf_records.rl_used;
		rb->rf_records.rl_used = 2 * rb->rf_records.rl_count;
		fprintf(stdout, "REFCOUNT_LIST_USED: Corrupt refcount block #"
			"%"PRIu64", change rl_used from %u to %u\n",
			(uint64_t)rb->rf_blkno, oldno, rb->rf_records.rl_used);
		break;
	case REFCOUNT_CLUSTER_RANGE:
		oldblkno = rb->rf_records.rl_recs[0].r_cpos;
		rb->rf_records.rl_recs[0].r_cpos = fs->fs_clusters + 1;
		fprintf(stdout, "REFCOUNT_CLUSTER_RANGE, Corrupt refcount "
			"block #%"PRIu64", change recs[0] from %"PRIu64
			" to %"PRIu64"\n", (uint64_t)rb->rf_blkno, oldblkno,
			(uint64_t)rb->rf_records.rl_recs[0].r_cpos);
		break;
	case REFCOUNT_CLUSTER_COLLISION:
		oldblkno = rb->rf_records.rl_recs[0].r_cpos;
		rb->rf_records.rl_recs[0].r_cpos = fs->fs_clusters - 1;
		fprintf(stdout, "REFCOUNT_CLUSTER_COLLISION, Corrupt refcount "
			"block #%"PRIu64", change recs[0] from %"PRIu64
			" to %"PRIu64"\n", (uint64_t)rb->rf_blkno, oldblkno,
			(uint64_t)rb->rf_records.rl_recs[0].r_cpos);
		break;
	case REFCOUNT_LIST_EMPTY:
		oldno = rb->rf_records.rl_used;
		rb->rf_records.rl_used = 0;
		fprintf(stdout, "REFCOUNT_LIST_EMPTY: Corrupt refcount block #"
			"%"PRIu64", change rl_used from %u to 0\n",
			(uint64_t)rb->rf_blkno, oldno);
		break;
	default:
		FSWRK_FATAL("Invalid type=%d", type);
	}
}

static void damage_refcount_record(ocfs2_filesys *fs, enum fsck_type type,
				   struct ocfs2_refcount_block *rb)
{
	uint32_t oldno;
	uint64_t oldblkno;

	switch (type) {
	case REFCOUNT_REC_REDUNDANT:
		oldblkno = rb->rf_records.rl_recs[0].r_cpos;
		rb->rf_records.rl_recs[0].r_cpos = 1;
		rb->rf_records.rl_recs[1].r_clusters += 1;
		rb->rf_records.rl_recs[3].r_cpos -= 1;
		rb->rf_records.rl_recs[3].r_clusters += 10;
		fprintf(stdout, "REFCOUNT_REC_REDUNDANT: Corrupt refcount "
			"record in block %"PRIu64", change recs[0].r_cpos "
			"from %"PRIu64" to 1, add recs[1].r_clusters by 1,"
			"decrease recs[3].r_cpos by 1 and "
			"increase r_clusters by 100\n",
			(uint64_t)rb->rf_blkno, oldblkno);
		break;
	case REFCOUNT_COUNT_INVALID:
		oldno = rb->rf_records.rl_recs[0].r_refcount;
		rb->rf_records.rl_recs[0].r_refcount = 100;
		fprintf(stdout, "REFCOUNT_COUNT_INVALID: Corrupt refcount "
			"record in block %"PRIu64", change recs[0].r_count "
			"from %u to 100\n",
			(uint64_t)rb->rf_blkno, oldno);
		break;
	default:
		FSWRK_FATAL("Invalid type=%d", type);
	}
}

void mess_up_refcount_tree_block(ocfs2_filesys *fs, enum fsck_type type,
				 uint64_t blkno)
{
	errcode_t ret;
	char *buf1 = NULL, *buf2 = NULL, *buf2_leaf = NULL;
	uint64_t rf_blkno1, rf_blkno2, rf_leaf_blkno;
	struct ocfs2_refcount_block *rb1, *rb2, *rb2_leaf;

	if (!ocfs2_refcount_tree(OCFS2_RAW_SB(fs->fs_super)))
		FSWRK_FATAL("Should specify a refcount supported "
			    "volume to do this corruption\n");

	ret = ocfs2_malloc_block(fs->fs_io, &buf1);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);
	ret = ocfs2_malloc_block(fs->fs_io, &buf2);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);
	ret = ocfs2_malloc_block(fs->fs_io, &buf2_leaf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	/*
	 * We create 2 refcount trees. One only has a root refcount block,
	 * and the other has a tree with depth = 1. So we can corrupt both
	 * of them and verify whether fsck works for different block types.
	 */
	create_refcount_tree(fs, blkno, &rf_blkno1, 0);
	create_refcount_tree(fs, blkno, &rf_blkno2, 1);

	ret = ocfs2_read_refcount_block(fs, rf_blkno1, buf1);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);
	rb1 = (struct ocfs2_refcount_block *)buf1;

	/* tree 2 is an extent tree, so find the 1st leaf refcount block. */
	ret = ocfs2_read_refcount_block(fs, rf_blkno2, buf2);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);
	rb2 = (struct ocfs2_refcount_block *)buf2;
	assert(rb2->rf_flags & OCFS2_REFCOUNT_TREE_FL);
	rf_leaf_blkno = rb2->rf_list.l_recs[0].e_blkno;
	ret = ocfs2_read_refcount_block(fs, rf_leaf_blkno, buf2_leaf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);
	rb2_leaf = (struct ocfs2_refcount_block *)buf2_leaf;

	switch (type) {
	case RB_BLKNO:
	case RB_GEN:
	case RB_GEN_FIX:
		damage_refcount_block(fs, type, rb1);
		damage_refcount_block(fs, type, rb2_leaf);
		break;
	case RB_PARENT:
		damage_refcount_block(fs, type, rb2_leaf);
		break;
	case REFCOUNT_BLOCK_INVALID:
		damage_refcount_block(fs, type, rb2_leaf);
		break;
	case REFCOUNT_ROOT_BLOCK_INVALID:
		damage_refcount_block(fs, type, rb1);
		damage_refcount_block(fs, type, rb2);
		break;
	case REFCOUNT_LIST_COUNT:
	case REFCOUNT_LIST_USED:
	case REFCOUNT_CLUSTER_RANGE:
	case REFCOUNT_CLUSTER_COLLISION:
	case REFCOUNT_LIST_EMPTY:
		damage_refcount_list(fs, type, rb1);
		damage_refcount_list(fs, type, rb2_leaf);
		break;
	case REFCOUNT_REC_REDUNDANT:
	case REFCOUNT_COUNT_INVALID:
		damage_refcount_record(fs, type, rb1);
		damage_refcount_record(fs, type, rb2_leaf);
		break;
	default:
		FSWRK_FATAL("Invalid type[%d]\n", type);
	}

	ret = ocfs2_write_refcount_block(fs, rf_blkno1, buf1);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);
	ret = ocfs2_write_refcount_block(fs, rf_blkno2, buf2);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);
	ret = ocfs2_write_refcount_block(fs, rf_leaf_blkno, buf2_leaf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);
	ocfs2_free(&buf1);
	ocfs2_free(&buf2);
	ocfs2_free(&buf2_leaf);

	return;
}

void mess_up_refcount_tree(ocfs2_filesys *fs, enum fsck_type type,
			   uint64_t blkno)
{
	errcode_t ret;
	char *buf = NULL;
	uint64_t rf_blkno;
	uint32_t oldno;
	struct ocfs2_refcount_block *rb;

	if (!ocfs2_refcount_tree(OCFS2_RAW_SB(fs->fs_super)))
		FSWRK_FATAL("Should specify a refcount supported "
			    "volume to do this corruption\n");

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	create_refcount_tree(fs, blkno, &rf_blkno, 2);

	ret = ocfs2_read_refcount_block(fs, rf_blkno, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);
	rb = (struct ocfs2_refcount_block *)buf;

	switch (type) {
	case REFCOUNT_CLUSTERS:
		oldno = rb->rf_clusters;
		rb->rf_clusters = 1;
		fprintf(stdout, "REFCOUNT_CLUSTERS: Corrupt refcount block #"
			"%"PRIu64", change rf_clusters from %u to %u\n",
			(uint64_t)rb->rf_blkno, oldno, rb->rf_clusters);
		break;
	case REFCOUNT_COUNT:
		oldno = rb->rf_count;
		rb->rf_count = 0;
		fprintf(stdout, "REFCOUNT_COUNT: Corrupt refcount block #"
			"%"PRIu64", change rf_count from %u to %u\n",
			(uint64_t)rb->rf_blkno, oldno, rb->rf_count);
		break;
	default:
		FSWRK_FATAL("Invalid type[%d]\n", type);
	}

	ret = ocfs2_write_refcount_block(fs, rf_blkno, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);
	ocfs2_free(&buf);

	return;
}
