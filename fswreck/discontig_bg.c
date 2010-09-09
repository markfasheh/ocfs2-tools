/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * discontig_bg.c
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

#include <assert.h>
#include "main.h"

extern char *progname;

static void create_discontig_bg_list(ocfs2_filesys *fs,
				     struct ocfs2_group_desc *gd,
				     uint64_t blkno,
				     uint32_t clusters)
{
	uint16_t recs = ocfs2_extent_recs_per_gd(fs->fs_blocksize) / 2;
	int i, clusters_per_rec, cpos = 0;
	struct ocfs2_extent_rec *rec;

	/* Calculate out how much clusters one rec have and how many
	 * recs we have.
	 */
	if (clusters > recs)
		clusters_per_rec = clusters / recs;
	else {
		recs = clusters;
		clusters_per_rec = 1;
	}

	for (i = 0; i < recs - 1; i++) {
		rec = &gd->bg_list.l_recs[i];

		rec->e_blkno = blkno;
		rec->e_cpos = cpos;
		rec->e_leaf_clusters = clusters_per_rec;
		blkno += ocfs2_clusters_to_blocks(fs, clusters_per_rec);
		cpos += clusters_per_rec;
	}

	/* Set the last rec according to all the clusters left. */
	rec = &gd->bg_list.l_recs[recs - 1];
	rec->e_blkno = blkno;
	rec->e_cpos = cpos;
	rec->e_leaf_clusters = clusters - (recs - 1) * clusters_per_rec;

	gd->bg_list.l_count = ocfs2_extent_recs_per_gd(fs->fs_blocksize);
	gd->bg_list.l_tree_depth = 0;
	gd->bg_list.l_next_free_rec = recs;
}

/*
 * Add a new discontig inode alloc group to the given slot.
 * the buf will contain the new created alloc group.
 */
static void create_discontig_bg(ocfs2_filesys *fs,
				uint16_t slotnum,
				char *gd_buf, uint16_t *cpg)
{
	char *buf = NULL;
	errcode_t ret;
	char sysfile[OCFS2_MAX_FILENAME_LEN];
	struct ocfs2_super_block *sb = OCFS2_RAW_SB(fs->fs_super);
	struct ocfs2_chain_list *cl;
	struct ocfs2_dinode *di;
	struct ocfs2_group_desc *gd;
	struct ocfs2_chain_rec *rec;
	uint32_t clusters;
	uint64_t gd_blkno, di_blkno, old_blkno;
	uint16_t chain;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	if (slotnum == UINT16_MAX)
		slotnum = 0;

	snprintf(sysfile, sizeof(sysfile),
		ocfs2_system_inodes[INODE_ALLOC_SYSTEM_INODE].si_name,
		slotnum);

	ret = ocfs2_lookup(fs, sb->s_system_dir_blkno, sysfile,
			   strlen(sysfile), NULL, &di_blkno);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_read_inode(fs, di_blkno, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	di = (struct ocfs2_dinode *)buf;
	cl = &di->id2.i_chain;

	chain = cl->cl_next_free_rec;
	if (chain == cl->cl_count)
		chain = 0;

	ret = ocfs2_new_clusters(fs, cl->cl_cpg, cl->cl_cpg,
				 &gd_blkno, &clusters);
	if (ret || (clusters != cl->cl_cpg))
		FSWRK_COM_FATAL(progname, ret);

	gd = (struct ocfs2_group_desc *)gd_buf;
	ocfs2_init_group_desc(fs, gd, gd_blkno, fs->fs_super->i_fs_generation,
			      di->i_blkno,
			      di->id2.i_chain.cl_cpg * di->id2.i_chain.cl_bpc,
			      chain, 1);

	create_discontig_bg_list(fs, gd, gd_blkno, clusters);

	rec = &di->id2.i_chain.cl_recs[chain];
	old_blkno = rec->c_blkno;
	gd->bg_next_group = old_blkno;

	ret = ocfs2_write_group_desc(fs, gd_blkno, gd_buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	rec->c_free += gd->bg_free_bits_count;
	rec->c_total += gd->bg_bits;
	rec->c_blkno = gd_blkno;

	di->i_clusters += di->id2.i_chain.cl_cpg;
	di->i_size = (uint64_t)di->i_clusters * fs->fs_clustersize;
	di->id1.bitmap1.i_total += gd->bg_bits;
	di->id1.bitmap1.i_used += gd->bg_bits -	gd->bg_free_bits_count;
	if (di->id2.i_chain.cl_next_free_rec == chain)
		di->id2.i_chain.cl_next_free_rec = chain + 1;

	*cpg = cl->cl_cpg;
	ret = ocfs2_write_inode(fs, di_blkno, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ocfs2_free(&buf);
}

void mess_up_discontig_bg(ocfs2_filesys *fs, enum fsck_type type,
			  uint16_t slotnum)
{
	errcode_t ret;
	char *buf = NULL;
	struct ocfs2_group_desc *gd;
	uint16_t old, cpg;
	uint32_t old_clusters, old_clusters1;
	uint64_t old_blkno;

	if (!ocfs2_supports_discontig_bg(OCFS2_RAW_SB(fs->fs_super)))
		FSWRK_FATAL("Should specify a discontig-bg supported "
			    "volume to do this corruption\n");

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	create_discontig_bg(fs, slotnum, buf, &cpg);

	gd = (struct ocfs2_group_desc *)buf;

	switch (type) {
	case DISCONTIG_BG_DEPTH:
		old = gd->bg_list.l_tree_depth;
		gd->bg_list.l_tree_depth++;
		fprintf(stdout, "DISCONTIG_BG_DEPTH: Corrupt discontig bg #"
			"%"PRIu64", change l_tree_depth from %u to %u\n",
			(uint64_t)gd->bg_blkno, old, gd->bg_list.l_tree_depth);
		break;
	case DISCONTIG_BG_COUNT:
		old = gd->bg_list.l_count;
		gd->bg_list.l_count += 10;;
		fprintf(stdout, "DISCONTIG_BG_COUNT: Corrupt discontig bg #"
			"%"PRIu64", change l_count from %u to %u\n",
			(uint64_t)gd->bg_blkno, old, gd->bg_list.l_count);
		break;
	case DISCONTIG_BG_REC_RANGE:
		old_blkno = gd->bg_list.l_recs[0].e_blkno;
		gd->bg_list.l_recs[0].e_blkno = fs->fs_blocks + 10;
		fprintf(stdout, "DISCONTIG_BG_REC_RANGE: Corrupt discontig bg #"
			"%"PRIu64", change recs[0].e_blkno from %"PRIu64
			" to %"PRIu64"\n",
			(uint64_t)gd->bg_blkno, old_blkno,
			(uint64_t)gd->bg_list.l_recs[0].e_blkno);
		break;
	case DISCONTIG_BG_CORRUPT_LEAVES:
		old_clusters = gd->bg_list.l_recs[0].e_leaf_clusters;
		old_clusters1 = gd->bg_list.l_recs[1].e_leaf_clusters;
		gd->bg_list.l_recs[0].e_leaf_clusters = cpg + 1;
		gd->bg_list.l_recs[1].e_leaf_clusters = cpg + 1;
		fprintf(stdout, "DISCONTIG_BG_CORRUPT_LEAVES: Corrupt discontig"
			" bg #%"PRIu64", change recs[0] clusters from %u to %u,"
			" change recs1[1] clusters from %u to %u\n",
			(uint64_t)gd->bg_blkno,
			old_clusters, gd->bg_list.l_recs[0].e_leaf_clusters,
			old_clusters1, gd->bg_list.l_recs[1].e_leaf_clusters);
		break;
	case DISCONTIG_BG_CLUSTERS:
		old = gd->bg_list.l_next_free_rec - 1;
		old_clusters = gd->bg_list.l_recs[old].e_leaf_clusters;
		gd->bg_list.l_recs[old].e_leaf_clusters += 1;
		fprintf(stdout, "DISCONTIG_BG_CLUSTERS: Corrupt "
			"discontig bg #%"PRIu64", change recs[%u] clusters "
			"from %u to %u\n", (uint64_t)gd->bg_blkno, old,
			old_clusters, gd->bg_list.l_recs[old].e_leaf_clusters);
		break;
	case DISCONTIG_BG_LESS_CLUSTERS:
		old = gd->bg_list.l_next_free_rec;
		gd->bg_list.l_next_free_rec -= 1;
		fprintf(stdout, "DISCONTIG_BG_LESS_CLUSTERS: Corrupt discontig"
			" bg #%"PRIu64", change l_next_free_rec from %u to "
			"%u\n",	(uint64_t)gd->bg_blkno,
			old, gd->bg_list.l_next_free_rec);
		break;
	case DISCONTIG_BG_NEXT_FREE_REC:
		old = gd->bg_list.l_next_free_rec;
		gd->bg_list.l_next_free_rec += 1;;
		fprintf(stdout, "DISCONTIG_BG_NEXT_FREE_REC: Corrupt discontig "
			"bg #%"PRIu64", change l_next_free_rec from %u to %u\n",
			(uint64_t)gd->bg_blkno,
			old, gd->bg_list.l_next_free_rec);
		break;
	case DISCONTIG_BG_LIST_CORRUPT:
		old = gd->bg_list.l_next_free_rec;
		old_clusters = gd->bg_list.l_recs[0].e_leaf_clusters;
		gd->bg_list.l_recs[0].e_leaf_clusters = cpg+1;
		old_clusters1 = gd->bg_list.l_recs[old-2].e_leaf_clusters;
		gd->bg_list.l_recs[old-2].e_leaf_clusters += 2;
		fprintf(stdout, "DISCONTIG_BG_LIST_CORRUPT: Corrupt discontig "
			"bg #%"PRIu64", change recs[0] clusters from %u to %u, "
			"change recs[%u] cluster from %u to %u\n",
			(uint64_t)gd->bg_blkno,
			old_clusters, gd->bg_list.l_recs[0].e_leaf_clusters,
			old - 2, old_clusters1,
			gd->bg_list.l_recs[old-2].e_leaf_clusters);
		break;
	case DISCONTIG_BG_REC_CORRUPT:
		old_clusters = gd->bg_list.l_recs[0].e_leaf_clusters;
		old_clusters1 = gd->bg_list.l_recs[1].e_leaf_clusters;
		gd->bg_list.l_recs[0].e_leaf_clusters = cpg + 1;
		gd->bg_list.l_recs[1].e_leaf_clusters += 1;
		fprintf(stdout, "DISCONTIG_BG_REC_CORRUPT: Corrupt discontig"
			" bg #%"PRIu64", change recs[0] clusters from %u to "
			"%u, recs[1] clusters from %u to %u\n",
			(uint64_t)gd->bg_blkno,
			old_clusters, gd->bg_list.l_recs[0].e_leaf_clusters,
			old_clusters1, gd->bg_list.l_recs[1].e_leaf_clusters);
		break;
	case DISCONTIG_BG_LEAF_CLUSTERS:
		old_clusters = gd->bg_list.l_recs[0].e_leaf_clusters;
		gd->bg_list.l_recs[0].e_leaf_clusters = cpg + 1;
		fprintf(stdout, "DISCONTIG_BG_LEAF_CLUSTERS: Corrupt discontig"
			" bg #%"PRIu64", change recs[0] clusters from %u to "
			"%u\n",	(uint64_t)gd->bg_blkno,
			old_clusters, gd->bg_list.l_recs[0].e_leaf_clusters);
		break;
	default:
		FSWRK_FATAL("Invalid type[%d]\n", type);
	}

	ret = ocfs2_write_group_desc(fs, gd->bg_blkno, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);
	ocfs2_free(&buf);

	return;
}
