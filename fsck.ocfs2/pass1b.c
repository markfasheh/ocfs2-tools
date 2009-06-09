/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * Copyright (C) 1993, 1994, 1995, 1996, 1997 Theodore Ts'o.
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
 *
 * --
 *
 * Pass 1B-D are extra passes invoked only if Pass 1 discovered clusters
 * in use by more than one inode.  They are very expensive, as they cannot
 * make easy use of the I/O cache.
 *
 * Pass 1 has already built us a bitmap of duplicated clusters.  For
 * efficiency, it doesn't try to track who owns them.  Now we need to know.
 * Because Pass 1 has already repaired and verified the allocators, we can
 * trust them to be consistent.
 *
 * Pass 1B rescans the inodes and builds two rbtrees.  The first rbtree
 * maps a duplicate cluster to the inodes that share it.  The second rbtree
 * keeps track of all inodes with duplicates.  If an inode has more than
 * one duplicate cluster, it will get cloned or deleted when the first one
 * is evaluated in Pass 1D.  The second rbtree prevents us from re-examining
 * this inode for each addition cluster it used to share.
 *
 * Pass 1C walks the directory tree and gives names to each inode.  This
 * is so the user can see the name of the file they are fixing.
 *
 * Pass 1D does the actual fixing.  Each inode with duplicate clusters can
 * cloned to an entirely new file or deleted.  Regardless of the choice,
 * and inode that is fixed no longer has duplicate clusters.
 *
 * Once Pass1D is complete, the ost_duplicate_clusters bitmap can be
 * freed.
 */
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <assert.h>

#include "ocfs2/ocfs2.h"
#include "ocfs2/bitops.h"

#include "dirblocks.h"
#include "dirparents.h"
#include "extent.h"
#include "icount.h"
#include "fsck.h"
#include "pass1.h"
#include "pass1b.h"
#include "problem.h"
#include "util.h"
#include "xattr.h"

static const char *whoami = "UNSET!";

/*
 * Keep track of an inode that claims clusters shared by other objects.
 */
struct dup_inode {
	struct rb_node	di_node;

	/* The block number of this inode */
	uint64_t	di_ino;

	/*
	 * i_flags from the inode.  We need to refuse deletion of
	 * system files, and chain allocators are even worse.
	 */
	uint32_t	di_flags;
};

/*
 * Keep track of clusters that are claimed by multiple objects.
 */
struct dup_cluster_owner {
	struct list_head	dco_list;

	/*
	 * The block number of the owning inode.  This is the lookup key
	 * for the dup inode rbtree.
	 */
	uint64_t		dco_ino;
};

struct dup_cluster {
	struct rb_node		dc_node;

	/* The physical cluster that is multiply-claimed */
	uint32_t		dc_cluster;

	/* List of owning inodes */
	struct list_head	dc_owners;
};


/*
 * Context for Passes 1B-D.
 */
struct dup_context {
	/* Tree of multiply-claimed clusters */
	struct rb_root	dup_clusters;

	/* Inodes that own them */
	struct rb_root	dup_inodes;
};

/* See if the cluster rbtree has the given cluster.  */
static struct dup_cluster *dup_cluster_lookup(struct dup_context *dct,
					      uint32_t cluster)
{
	struct rb_node *p = dct->dup_clusters.rb_node;
	struct dup_cluster *dc;

	while (p) {
		dc = rb_entry(p, struct dup_cluster, dc_node);
		if (cluster < dc->dc_cluster) {
			p = p->rb_left;
		} else if (cluster > dc->dc_cluster) {
			p = p->rb_right;
		} else
			return dc;
	}

	return NULL;
}

static void dup_cluster_insert(struct dup_context *dct,
			       struct dup_cluster *insert_dc)
{
	struct rb_node **p = &dct->dup_clusters.rb_node;
	struct rb_node *parent = NULL;
	struct dup_cluster *dc = NULL;

	while (*p) {
		parent = *p;
		dc = rb_entry(parent, struct dup_cluster, dc_node);
		if (insert_dc->dc_cluster < dc->dc_cluster) {
			p = &(*p)->rb_left;
			dc = NULL;
		} else if (insert_dc->dc_cluster > dc->dc_cluster) {
			p = &(*p)->rb_right;
			dc = NULL;
		} else
			assert(0);  /* Caller checked */
	}

	rb_link_node(&insert_dc->dc_node, parent, p);
	rb_insert_color(&insert_dc->dc_node, &dct->dup_clusters);
}


/* See if the inode rbtree has the given cluster.  */
static struct dup_inode *dup_inode_lookup(struct dup_context *dct,
					  uint64_t ino)
{
	struct rb_node *p = dct->dup_inodes.rb_node;
	struct dup_inode *di;

	while (p) {
		di = rb_entry(p, struct dup_inode, di_node);
		if (ino < di->di_ino) {
			p = p->rb_left;
		} else if (ino > di->di_ino) {
			p = p->rb_right;
		} else
			return di;
	}

	return NULL;
}

static void dup_inode_insert(struct dup_context *dct,
			     struct dup_inode *insert_di)
{
	struct rb_node **p = &dct->dup_inodes.rb_node;
	struct rb_node *parent = NULL;
	struct dup_inode *di = NULL;

	while (*p) {
		parent = *p;
		di = rb_entry(parent, struct dup_inode, di_node);
		if (insert_di->di_ino < di->di_ino) {
			p = &(*p)->rb_left;
			di = NULL;
		} else if (insert_di->di_ino > di->di_ino) {
			p = &(*p)->rb_right;
			di = NULL;
		} else
			assert(0);  /* Caller checked */
	}

	rb_link_node(&insert_di->di_node, parent, p);
	rb_insert_color(&insert_di->di_node, &dct->dup_inodes);
}

/*
 * Given a (cluster,inode) tuple, insert the appropriate metadata
 * into the context.
 */
static errcode_t dup_insert(struct dup_context *dct, uint32_t cluster,
			    uint64_t ino, uint32_t i_flags)
{
	errcode_t ret;
	struct list_head *p;
	struct dup_cluster *dc, *new_dc = NULL;
	struct dup_inode *di, *new_di = NULL;
	struct dup_cluster_owner *dco, *new_dco = NULL;

	ret = ocfs2_malloc0(sizeof(struct dup_cluster), &new_dc);
	if (ret) {
		com_err(whoami, ret,
			"while allocating duplicate cluster tracking "
			"structures");
		goto out;
	}
	INIT_LIST_HEAD(&new_dc->dc_owners);
	new_dc->dc_cluster = cluster;

	ret = ocfs2_malloc0(sizeof(struct dup_inode), &new_di);
	if (ret) {
		com_err(whoami, ret,
			"while allocating duplicate cluster tracking "
			"structures");
		goto out;
	}
	new_di->di_ino = ino;
	new_di->di_flags = i_flags;

	ret = ocfs2_malloc0(sizeof(struct dup_cluster_owner), &new_dco);
	if (ret) {
		com_err(whoami, ret,
			"while allocating duplicate cluster tracking "
			"structures");
		goto out;
	}
	new_dco->dco_ino = ino;

	dc = dup_cluster_lookup(dct, cluster);
	if (!dc) {
		dup_cluster_insert(dct, new_dc);
		dc = new_dc;
		new_dc = NULL;
	}

	di = dup_inode_lookup(dct, ino);
	if (!di) {
		dup_inode_insert(dct, new_di);
		di = new_di;
		new_di = NULL;
	}

	dco = NULL;
	list_for_each(p, &dc->dc_owners) {
		dco = list_entry(p, struct dup_cluster_owner, dco_list);
		if (dco->dco_ino == ino)
			break;
		dco = NULL;
	}
	if (!dco) {
		list_add_tail(&new_dco->dco_list, &dc->dc_owners);
		dco = new_dco;
		new_dco = NULL;
	}

out:
	if (new_dc)
		ocfs2_free(&new_dc);
	if (new_di)
		ocfs2_free(&new_di);
	if (new_dco)
		ocfs2_free(&new_dco);

	return ret;
}

struct process_extents_context {
	o2fsck_state *ost;
	struct dup_context *dct;
	struct ocfs2_dinode *di;
	errcode_t ret;
	uint64_t global_bitmap_blkno;
};

static errcode_t process_dup_clusters(struct process_extents_context *pc,
				      uint32_t p_cpos, uint32_t clusters)
{
	int was_set;
	errcode_t ret = 0;

	while (clusters) {
		ret = ocfs2_bitmap_test(pc->ost->ost_duplicate_clusters,
					p_cpos, &was_set);
		if (ret) {
			com_err(whoami, ret,
				"while testing cluster %"PRIu32" of inode "
				"%"PRIu64" in the duplicate cluster map",
				p_cpos, pc->di->i_blkno);
			break;
		}

		if (was_set) {
			verbosef("Marking multiply-claimed cluster %"PRIu32
				 " as claimed by inode %"PRIu64"\n",
				 p_cpos, pc->di->i_blkno);
			ret = dup_insert(pc->dct, p_cpos, pc->di->i_blkno,
					 pc->di->i_flags);
			if (ret) {
				com_err(whoami, ret,
					"while marking duplicate cluster "
					"%"PRIu32" as owned by inode "
					"%"PRIu64,
					p_cpos, pc->di->i_blkno);
				break;
			}
		}

		p_cpos++;
		clusters--;
	}

	return ret;
}

static int process_inode_chains(ocfs2_filesys *fs, uint64_t gd_blkno,
				int chain_num, void *priv_data)
{
	struct process_extents_context *pc = priv_data;
	uint32_t clusters = pc->di->id2.i_chain.cl_cpg;

	if (pc->di->i_blkno == pc->global_bitmap_blkno)
		clusters = 1;

	pc->ret = process_dup_clusters(pc,
				       ocfs2_blocks_to_clusters(fs, gd_blkno),
				       clusters);

	return pc->ret ? OCFS2_CHAIN_ERROR | OCFS2_CHAIN_ABORT : 0;
}

static int process_inode_extents(ocfs2_filesys *fs,
				 struct ocfs2_extent_rec *rec,
				 int tree_depth,
				 uint32_t ccount,
				 uint64_t ref_blkno,
				 int ref_recno,
				 void *priv_data)
{
	struct process_extents_context *pc = priv_data;

	assert(!tree_depth);
	pc->ret = process_dup_clusters(pc,
				       ocfs2_blocks_to_clusters(fs,
								rec->e_blkno),
				       rec->e_leaf_clusters);

	return pc->ret ? OCFS2_EXTENT_ERROR | OCFS2_EXTENT_ABORT : 0;
}

static errcode_t process_xattr_header(struct process_extents_context *pc,
				      struct ocfs2_xattr_header *xh)
{
	int i;
	errcode_t ret = 0;
	struct ocfs2_xattr_entry *xe;
	struct ocfs2_xattr_value_root *xv;

	for (i = 0 ; i < xh->xh_count; i++) {
		xe = &xh->xh_entries[i];

		if (ocfs2_xattr_is_local(xe))
			continue;

		xv = (struct ocfs2_xattr_value_root *)
			((void *)xh + xe->xe_name_offset +
			 OCFS2_XATTR_SIZE(xe->xe_name_len));
		ret = ocfs2_extent_iterate_xattr(pc->ost->ost_fs,
						 &xv->xr_list,
						 xv->xr_last_eb_blk,
						 OCFS2_EXTENT_FLAG_DATA_ONLY,
						 process_inode_extents,
						 pc, NULL);
		if (ret)
			com_err(whoami, ret,
				"while processing xattrs on inode %"PRIu64,
				pc->di->i_blkno);

		if (!ret)
			ret = pc->ret;
		if (ret)
			break;
	}

	return ret;
}

static errcode_t process_one_bucket_list(struct process_extents_context *pc,
					 struct ocfs2_extent_rec *rec)
{
	int i;
	errcode_t ret;
	char *bucket;
	struct ocfs2_xattr_header *xh;
	uint64_t blkno = ocfs2_clusters_to_blocks(pc->ost->ost_fs,
						  rec->e_cpos);
	int bucket_count = rec->e_leaf_clusters *
		ocfs2_xattr_buckets_per_cluster(pc->ost->ost_fs);

	ret = ocfs2_malloc_blocks(pc->ost->ost_fs->fs_io,
				  ocfs2_blocks_per_xattr_bucket(pc->ost->ost_fs),
				  &bucket);
	if (ret) {
		com_err(whoami, ret,
			"while allocating an xattr bucket buffer");
		return ret;
	}

	xh = (struct ocfs2_xattr_header *)bucket;
	for (i = 0; i < bucket_count; i++) {
		ret = ocfs2_read_xattr_bucket(pc->ost->ost_fs,
					      blkno, bucket);
		if (ret) {
			com_err(whoami, ret,
				"while reading the xattr bucket at "
				"%"PRIu64" on inode %"PRIu64,
				blkno, pc->di->i_blkno);
			break;
		}

		if (!i)
			bucket_count = xh->xh_num_buckets;

		ret = process_xattr_header(pc, xh);
		if (ret)
			break;

		blkno += ocfs2_blocks_per_xattr_bucket(pc->ost->ost_fs);
	}

	ocfs2_free(&bucket);
	return ret;
}

static int process_xattr_buckets(ocfs2_filesys *fs,
				 struct ocfs2_extent_rec *rec,
				 int tree_depth,
				 uint32_t ccount,
				 uint64_t ref_blkno,
				 int ref_recno,
				 void *priv_data)
{
	errcode_t ret;
	struct process_extents_context *pc = priv_data;

	assert(!tree_depth);

	pc->ret = process_dup_clusters(pc,
				       ocfs2_blocks_to_clusters(fs,
								rec->e_blkno),
				       rec->e_leaf_clusters);
	if (pc->ret)
		goto out;

	ret = process_one_bucket_list(pc, rec);

out:
	if (!ret)
		ret = pc->ret;
	return pc->ret ? OCFS2_EXTENT_ERROR | OCFS2_EXTENT_ABORT : 0;
}

static errcode_t process_xattr_tree(struct process_extents_context *pc,
				    struct ocfs2_xattr_block *xb)
{
	errcode_t ret;

	ret = ocfs2_extent_iterate_xattr(pc->ost->ost_fs,
					 &xb->xb_attrs.xb_root.xt_list,
					 xb->xb_attrs.xb_root.xt_last_eb_blk,
					 OCFS2_EXTENT_FLAG_DATA_ONLY,
					 process_xattr_buckets,
					 pc, NULL);

	if (ret)
		com_err(whoami, ret,
			"while processing xattrs on inode %"PRIu64,
			pc->di->i_blkno);

	if (!ret)
		ret = pc->ret;

	return ret;
}

static errcode_t process_xattr_block(struct process_extents_context *pc)
{
	errcode_t ret;
	char *blk = NULL;
	struct ocfs2_xattr_block *xb;

	ret = ocfs2_malloc_block(pc->ost->ost_fs->fs_io, &blk);
	if (ret) {
		com_err(whoami, ret,
			"while allocating a buffer to read the xattr block "
			"on inode %"PRIu64,
			pc->di->i_xattr_loc);
		goto out;
	}

	ret = ocfs2_read_xattr_block(pc->ost->ost_fs, pc->di->i_xattr_loc,
				     blk);
	if (ret) {
		com_err(whoami, ret, "while reading externel block of"
			" extended attributes ");
		goto out;
	}

	xb = (struct ocfs2_xattr_block *)blk;

	if (!(xb->xb_flags & OCFS2_XATTR_INDEXED))
		ret = process_xattr_header(pc, &xb->xb_attrs.xb_header);
	else
		ret = process_xattr_tree(pc, xb);

out:
	if (blk)
		ocfs2_free(&blk);
	return ret;
}

static errcode_t process_inode_xattrs(struct process_extents_context *pc)
{
	errcode_t ret = 0;
	struct ocfs2_xattr_header *xh;

	if (pc->di->i_dyn_features & OCFS2_INLINE_XATTR_FL) {
		xh = (struct ocfs2_xattr_header *)
			((void *)pc->di + pc->ost->ost_fs->fs_blocksize -
			 pc->di->i_xattr_inline_size);

		ret = process_xattr_header(pc, xh);
		if (ret)
			goto out;
	}

	if (pc->di->i_xattr_loc)
		ret = process_xattr_block(pc);

out:
	return ret;
}

static errcode_t pass1b_process_inode(o2fsck_state *ost,
				      struct dup_context *dct,
				      uint64_t ino, struct ocfs2_dinode *di)
{
	errcode_t ret = 0;
	static uint64_t global_bitmap_blkno = 0;
	struct process_extents_context pc = {
		.ost = ost,
		.dct = dct,
		.di = di,
	};

	/*
	 * The global bitmap needs magic in process_inode_chains.  Let's
	 * look it up here.  It's static, so this only happens once.
	 */

	if (!global_bitmap_blkno) {
		ret = ocfs2_lookup_system_inode(ost->ost_fs,
						GLOBAL_BITMAP_SYSTEM_INODE,
						0, &global_bitmap_blkno);
		if (ret) {
			com_err(whoami, ret,
				"while looking up the cluster bitmap "
				"allocator inode");
			goto out;
		}
	}
	pc.global_bitmap_blkno = global_bitmap_blkno;

	/*
	 * Skip extent processing for for inodes that don't have i_list or
	 * i_chains.  Like Pass 1, we have to trust i_mode/i_clusters to
	 * tell us that a symlink has put target data in the union instead
	 * of i_list
	 */
	if ((di->i_flags & (OCFS2_SUPER_BLOCK_FL | OCFS2_LOCAL_ALLOC_FL |
			    OCFS2_DEALLOC_FL)) ||
	    (S_ISLNK(di->i_mode) && di->i_clusters == 0))
		goto xattrs;

	if (di->i_flags & OCFS2_CHAIN_FL)
		ret = ocfs2_chain_iterate(ost->ost_fs, pc.di->i_blkno,
					  process_inode_chains, &pc);
	else
		ret = ocfs2_extent_iterate_inode(ost->ost_fs, di,
						 OCFS2_EXTENT_FLAG_DATA_ONLY,
						 NULL,
						 process_inode_extents,
						 &pc);

	if (ret)
		com_err(whoami, ret, "while processing inode %"PRIu64,
			ino);
	else
		ret = pc.ret;

	if (ret)
		goto out;

xattrs:
	if (di->i_dyn_features & OCFS2_HAS_XATTR_FL)
		ret = process_inode_xattrs(&pc);

out:
	return ret;
}


static errcode_t o2fsck_pass1b(o2fsck_state *ost, struct dup_context *dct)
{
	errcode_t ret;
	uint64_t blkno;
	char *buf;
	struct ocfs2_dinode *di;
	ocfs2_inode_scan *scan;
	ocfs2_filesys *fs = ost->ost_fs;

	whoami = "pass1b";
	printf("Running additional passes to resolve clusters claimed by "
	       "more than one inode...\n"
	       "Pass 1b: Determining ownership of multiply-claimed clusters\n");

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret) {
		com_err(whoami, ret, "while allocating inode buffer");
		goto out;
	}

	di = (struct ocfs2_dinode *)buf;

	ret = ocfs2_open_inode_scan(fs, &scan);
	if (ret) {
		com_err(whoami, ret, "while opening inode scan");
		goto out_free;
	}

	/*
	 * The inode allocators should be good after Pass 1.
	 * Valid inodes should really be valid.  Errors are real errors.
	 */
	for(;;) {
		ret = ocfs2_get_next_inode(scan, &blkno, buf);
		if (ret) {
			com_err(whoami, ret, "while getting next inode");
			break;
		}
		if (blkno == 0)
			break;

		if (memcmp(di->i_signature, OCFS2_INODE_SIGNATURE,
			   strlen(OCFS2_INODE_SIGNATURE)))
			continue;

		ocfs2_swap_inode_to_cpu(di, fs->fs_blocksize);

		if (di->i_fs_generation != ost->ost_fs_generation)
			continue;

		if (!(di->i_flags & OCFS2_VALID_FL))
			continue;

		ret = pass1b_process_inode(ost, dct, blkno, di);
		if (ret)
			break;
	}

	ocfs2_close_inode_scan(scan);

out_free:
	ocfs2_free(&buf);

out:
	return ret;
}

static void o2fsck_empty_dup_context(struct dup_context *dct)
{
	struct dup_cluster *dc;
	struct dup_inode *di;
	struct dup_cluster_owner *dco;
	struct rb_node *node;
	struct list_head *p, *next;

	while ((node = rb_first(&dct->dup_clusters)) != NULL) {
		dc = rb_entry(node, struct dup_cluster, dc_node);

		list_for_each_safe(p, next, &dc->dc_owners) {
			dco = list_entry(p, struct dup_cluster_owner,
					 dco_list);
			list_del(&dco->dco_list);
			ocfs2_free(&dco);
		}

		rb_erase(&dc->dc_node, &dct->dup_clusters);
		ocfs2_free(&dc);
	}

	while ((node = rb_first(&dct->dup_inodes)) != NULL) {
		di = rb_entry(node, struct dup_inode, di_node);
		rb_erase(&di->di_node, &dct->dup_inodes);
		ocfs2_free(&di);
	}
}

errcode_t ocfs2_pass1_dups(o2fsck_state *ost)
{
	errcode_t ret;
	struct dup_context dct = {
		.dup_clusters = RB_ROOT,
		.dup_inodes = RB_ROOT,
	};

	ret = o2fsck_pass1b(ost, &dct);
#if 0
	if (!ret)
		ret = o2fsck_pass1c(ost, &dct);
	if (!ret)
		ret = o2fsck_pass1d(ost, &dct);
#endif

	o2fsck_empty_dup_context(&dct);
	return ret;
}
