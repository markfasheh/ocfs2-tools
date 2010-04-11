/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
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
 *
 *   The scheme of the passes is based on e2fsck pass1b.c,
 *   Copyright (C) 1993, 1994, 1995, 1996, 1997 Theodore Ts'o.
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
 * is so the user can see the name of the file they are fixing.  The pass
 * does a depth-first traversal of the tree.  For every inode in the
 * rbtree of duplicates it finds, it stores the path for it.  It will
 * ignore errors in the directory tree, because we haven't fixed it yet.
 * When reporting to the user, inodes without names will just get their
 * inode number printed.
 *
 * Pass 1D does the actual fixing.  Each inode with duplicate clusters can
 * cloned to an entirely new file or deleted.  Regardless of the choice,
 * an inode that is fixed no longer has duplicate clusters.  Cloning is
 * done by creating a new inode and copying the data to it.  Then the
 * extent trees are swapped between the original and clone inode.  This
 * leaves the original inode with a good extent tree.  Finally, the clone
 * inode is removed and its extent tree released.  If deletion is chosen
 * instead of cloning, the original inode is removed.  Either way, we end
 * up over-freeing the clusters in the main bitmap.  At the end, we run
 * the list of multi-claimed clusters again.  If the cluster still has
 * claimers, it is forced on in the bitmap.  If it does not, it is forced
 * clear in the bitmap.  If we crash in the middle, we're still safe.  A
 * re-run of fsck will determine whether the over-freed clusters are
 * actually in use.
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


/* states for dup_inode.di_state */
#define	DUP_INODE_CLONED	0x01
#define DUP_INODE_REMOVED	0x02

/* A simple test to see if we should care about this dup inode anymore */
#define DUP_INODE_HANDLED	(DUP_INODE_CLONED | DUP_INODE_REMOVED)

/*
 * Keep track of an inode that claims clusters shared by other objects.
 */
struct dup_inode {
	struct rb_node	di_node;

	/* The block number of this inode */
	uint64_t	di_ino;

	/* The path to this inode */
	char		*di_path;

	/*
	 * i_flags from the inode.  We need to refuse deletion of
	 * system files, and chain allocators are even worse.
	 */
	uint32_t	di_flags;

	/* What we've done to it. */
	unsigned int	di_state;

	/* the refcount tree it has. */
	uint64_t	di_refcount_loc;
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
	/*
	 * virtual offset in the extent tree.
	 * Only valid for an extent tree, 0 for a chain file.
	 */
	uint32_t		dco_cpos;
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
	/* How many there are */
	uint64_t	dup_inode_count;
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
	dct->dup_inode_count++;
}

/*
 * Given a (cluster,inode) tuple, insert the appropriate metadata
 * into the context.
 */
static errcode_t dup_insert(struct dup_context *dct, uint32_t cluster,
			    struct ocfs2_dinode *dinode, uint32_t v_cpos)
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
	new_di->di_ino = dinode->i_blkno;
	new_di->di_flags = dinode->i_flags;
	new_di->di_refcount_loc = dinode->i_refcount_loc;

	ret = ocfs2_malloc0(sizeof(struct dup_cluster_owner), &new_dco);
	if (ret) {
		com_err(whoami, ret,
			"while allocating duplicate cluster tracking "
			"structures");
		goto out;
	}
	new_dco->dco_ino = dinode->i_blkno;
	new_dco->dco_cpos = v_cpos;

	dc = dup_cluster_lookup(dct, cluster);
	if (!dc) {
		dup_cluster_insert(dct, new_dc);
		dc = new_dc;
		new_dc = NULL;
	}

	di = dup_inode_lookup(dct, dinode->i_blkno);
	if (!di) {
		dup_inode_insert(dct, new_di);
		di = new_di;
		new_di = NULL;
	}

	dco = NULL;
	list_for_each(p, &dc->dc_owners) {
		dco = list_entry(p, struct dup_cluster_owner, dco_list);
		if (dco->dco_ino == dinode->i_blkno)
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


/*
 * Pass 1B
 */

struct process_extents_context {
	o2fsck_state *ost;
	struct dup_context *dct;
	struct ocfs2_dinode *di;
	errcode_t ret;
	uint64_t global_bitmap_blkno;
};

static errcode_t process_dup_clusters(struct process_extents_context *pc,
				      uint32_t p_cpos, uint32_t clusters,
				      uint32_t v_cpos)
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
			ret = dup_insert(pc->dct, p_cpos, pc->di, v_cpos);
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
		v_cpos++;
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
				       clusters, 0);

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
				       rec->e_leaf_clusters,
				       rec->e_cpos);

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
				       rec->e_leaf_clusters,
				       rec->e_cpos);
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

		ocfs2_swap_inode_to_cpu(fs, di);

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


/*
 * Pass 1C
 */

struct dir_to_scan {
	struct list_head ts_list;
	uint64_t ts_ino;
	char *ts_path;
};

struct dir_scan_context {
	o2fsck_state *ds_ost;
	struct dup_context *ds_dct;

	/* Inodes we still have to find */
	int64_t ds_inodes_left;

	/* Subdirs that are pending */
	struct list_head ds_paths;

	/* The cwd's path and ino */
	uint64_t ds_ino;
	char *ds_cwd;
	int ds_cwdlen;
};

static void pass1c_warn(errcode_t ret)
{
	static int warned = 0;

	if (warned)
		return;

	warned = 1;
	com_err(whoami, ret,
		"while finding path names in Pass 1c.  The pass will "
		"continue, but some inodes may be described by "
		"inode number instead of name.");
}

static char *de_to_path(struct dir_scan_context *scan,
			struct ocfs2_dir_entry *de)
{
	/* The 2 is for the path separator and the null */
	int copied, pathlen = scan->ds_cwdlen + de->name_len + 2;
	char *path = NULL;
	/* We start with an empty cwd as we add '/' or '//' */
	const char *cwdstr = scan->ds_cwdlen ? scan->ds_cwd : "";
	const char *sep = "/";

	/* Don't repeat '/' */
	if (scan->ds_cwdlen &&
	    (scan->ds_cwd[scan->ds_cwdlen - 1] == '/'))
		sep = "";
	if (de->name_len && (de->name[0] == '/'))
		sep = "";

	if (!ocfs2_malloc0(sizeof(char) * pathlen, &path)) {
		copied = snprintf(path, pathlen, "%s%s%.*s",
				  cwdstr, sep, de->name_len, de->name);
		assert(copied < pathlen);
	}

	return path;
}

static void push_dir(struct dir_scan_context *scan,
		     struct ocfs2_dir_entry *de)
{
	errcode_t ret;
	struct dir_to_scan *ts = NULL;

	ret = ocfs2_malloc0(sizeof(struct dir_to_scan), &ts);
	if (ret)
		goto warn;

	ts->ts_ino = de->inode;
	ts->ts_path = de_to_path(scan, de);
	if (!ts->ts_path) {
		ret = OCFS2_ET_NO_MEMORY;
		goto warn;
	}

	list_add(&ts->ts_list, &scan->ds_paths);
	return;

warn:
	if (ts)
		ocfs2_free(&ts);
	pass1c_warn(ret);
}

static void set_next_cwd(struct dir_scan_context *scan)
{
	struct dir_to_scan *ts;

	if (scan->ds_cwd)
		ocfs2_free(&scan->ds_cwd);

	ts = list_entry(scan->ds_paths.next, struct dir_to_scan, ts_list);
	list_del(&ts->ts_list);

	/* Steal the string from ts */
	scan->ds_cwd = ts->ts_path;
	scan->ds_cwdlen = strlen(scan->ds_cwd);
	scan->ds_ino = ts->ts_ino;

	ocfs2_free(&ts);
}

static void name_inode(struct dir_scan_context *scan,
		       struct ocfs2_dir_entry *de)
{
	struct dup_inode *di = dup_inode_lookup(scan->ds_dct, de->inode);

	if (!di || di->di_path)
		return;

	scan->ds_inodes_left--;

	di->di_path = de_to_path(scan, de);
	if (!di->di_path)
		pass1c_warn(OCFS2_ET_NO_MEMORY);
}

static int walk_iterate(struct ocfs2_dir_entry *de, uint64_t blocknr,
			int offset, int blocksize, char *buf, void *priv_data)
{
	struct dir_scan_context *scan = priv_data;

	/* Directories are checked when they're traversed */
	if (de->file_type == OCFS2_FT_DIR)
		push_dir(scan, de);
	else
		name_inode(scan, de);

	return scan->ds_inodes_left ? 0 : OCFS2_DIRENT_ABORT;
}

static void walk_cwd(struct dir_scan_context *scan)
{
	errcode_t ret;
	struct ocfs2_dir_entry de;

	memcpy(de.name, scan->ds_cwd, scan->ds_cwdlen);
	de.name_len = scan->ds_cwdlen;
	name_inode(scan, &de);

	ret = ocfs2_dir_iterate(scan->ds_ost->ost_fs, scan->ds_ino,
				OCFS2_DIRENT_FLAG_EXCLUDE_DOTS, NULL,
				walk_iterate, scan);
	if (ret)
		pass1c_warn(ret);
}

static void o2fsck_pass1c(o2fsck_state *ost, struct dup_context *dct)
{
	struct dir_scan_context scan = {
		.ds_ost = ost,
		.ds_dct = dct,
		.ds_inodes_left = dct->dup_inode_count,
	};

	whoami = "pass1c";
	printf("Pass 1c: Determining the names of inodes owning "
	       "multiply-claimed clusters\n");

	INIT_LIST_HEAD(&scan.ds_paths);
	push_dir(&scan, &(struct ocfs2_dir_entry){
		 .name = "/",
		 .name_len = 1,
		 .file_type = OCFS2_FT_DIR,
		 .inode = ost->ost_fs->fs_root_blkno,
		 });
	push_dir(&scan, &(struct ocfs2_dir_entry){
		 .name = "//",
		 .name_len = 2,
		 .file_type = OCFS2_FT_DIR,
		 .inode = ost->ost_fs->fs_sysdir_blkno,
		 });

	while (scan.ds_inodes_left && !list_empty(&scan.ds_paths)) {
		set_next_cwd(&scan);
		walk_cwd(&scan);
	}
}


/*
 * Pass 1D
 */

static void print_inode_path(struct dup_inode *di)
{
	if (di->di_path)
		fprintf(stdout, "%s\n", di->di_path);
	else
		fprintf(stdout, "<%"PRIu64">\n", di->di_ino);
}

/*
 * Walk the owning inodes of a dup_cluster, calling func().  func() may
 * return non-zero to abort the walk.
 */
static void for_each_owner(struct dup_context *dct, struct dup_cluster *dc,
			   int (*func)(struct dup_cluster *dc,
				       struct dup_inode *di,
				       struct dup_cluster_owner *dco,
				       void *priv_data),
			   void *priv_data)
{
	struct list_head *p, *next;
	struct dup_cluster_owner *dco;
	struct dup_inode *di;

	assert(!list_empty(&dc->dc_owners));
	list_for_each_safe(p, next, &dc->dc_owners) {
		dco = list_entry(p, struct dup_cluster_owner, dco_list);
		di = dup_inode_lookup(dct, dco->dco_ino);
		assert(di);
		if (func(dc, di, dco, priv_data))
			break;
	}
}

static int count_func(struct dup_cluster *dc, struct dup_inode *di,
		      struct dup_cluster_owner *dco, void *priv_data)
{
	uint64_t *count = priv_data;

	if (!(di->di_state & DUP_INODE_HANDLED))
		(*count)++;

	return 0;
}

static int print_func(struct dup_cluster *dc, struct dup_inode *di,
		      struct dup_cluster_owner *dco, void *priv_data)
{
	printf("  ");
	print_inode_path(di);

	return 0;
}

/*
 * Check whether we can create refcount for the file.
 * So a file is valid only if:
 * 1. It has no refcount tree.
 * 2. It has the same tree as others.
 * Store refcount_loc if we find one.
 *
 * if there is other file that does't have the same tree, set refcount_loc
 * to UINT64_MAX and stop the search.
 */
static int find_refcount_func(struct dup_cluster *dc, struct dup_inode *di,
			      struct dup_cluster_owner *dco, void *priv_data)
{
	uint64_t *refcount_loc = priv_data;

	if (!di->di_refcount_loc)
		return 0;

	if (!*refcount_loc) {
		*refcount_loc = di->di_refcount_loc;
		return 0;
	}

	if (di->di_refcount_loc != *refcount_loc) {
		*refcount_loc = UINT64_MAX;
		return 1;
	}

	return 0;
}

/* Context for fix_dups_func() */
struct fix_dup_context {
	o2fsck_state *fd_ost;
	struct dup_context *fd_dct;
	errcode_t fd_err;
};

static void print_chain_warning(void)
{
	static int chain_warning = 0;

	if (chain_warning)
		return;

	printf("The filesystem is safe to read.  You may wish to mount "
	       "it read-only and copy data to a new filesystem.\n");
	chain_warning = 1;
}

static errcode_t new_clone(ocfs2_filesys *fs, ocfs2_cached_inode *orig_ci,
			   ocfs2_cached_inode **clone_ci)
{
	errcode_t ret, ret2;
	uint64_t clone_blkno = 0;
	uint64_t bytes = orig_ci->ci_inode->i_size;
	uint32_t clusters = ocfs2_clusters_in_bytes(fs, bytes);

	ret = ocfs2_new_inode(fs, &clone_blkno, orig_ci->ci_inode->i_mode);
	if (ret) {
		com_err(whoami, ret, "while allocating a clone inode");
		return ret;
	}

	/*
	 * Let's get the clusters in the best way we can.  We make sure
	 * i_size is updated so that ocfs2_file_write() is happy.
	 */
	if (ocfs2_writes_unwritten_extents(OCFS2_RAW_SB(fs->fs_super)) &&
	    !(orig_ci->ci_inode->i_flags & OCFS2_SYSTEM_FL))
		ret = ocfs2_allocate_unwritten_extents(fs, clone_blkno, 0,
						       bytes);
	else {
		ret = ocfs2_extend_allocation(fs, clone_blkno, clusters);
		if (!ret)
			ret = ocfs2_extend_file(fs, clone_blkno, bytes);
	}
	if (ret) {
		com_err(whoami, ret,
			"while allocating data clusters for a clone inode");
		goto out;
	}

	ret = ocfs2_read_cached_inode(fs, clone_blkno, clone_ci);
	if (ret)
		com_err(whoami, ret, "while reading temporary clone inode");

	/*
	 * It is so tempting to link the temporary clone inode into
	 * the orphan directory here.  But we can't, because later in
	 * the clone process it will point to multiply-claimed clusters.
	 * Orphan cleanup would free them, which is even worse than
	 * leaving the temporary clone inode around.
	 */

out:
	if (ret && clone_blkno) {
		ret2 = ocfs2_delete_inode(fs, clone_blkno);
		if (ret2)
			com_err(whoami, ret2,
				"while removing temporary clone inode");
	}
	return ret;
}

static errcode_t copy_clone(ocfs2_filesys *fs, ocfs2_cached_inode *orig_ci,
			    ocfs2_cached_inode *clone_ci)
{
	char *buf;
	errcode_t ret;
	uint64_t offset = 0;
	uint64_t filesize = orig_ci->ci_inode->i_size;
	unsigned int iosize = 1024 * 1024;  /* Let's read in 1MB hunks */
	unsigned int got, wrote, write_len;

	ret = ocfs2_malloc_blocks(fs->fs_io, iosize / fs->fs_blocksize,
				  &buf);
	if (ret) {
		com_err(whoami, ret, "while allocating clone buffer");
		return ret;
	}

	while (offset < filesize) {
		ret = ocfs2_file_read(orig_ci, buf, iosize, offset, &got);
		if (ret) {
			com_err(whoami, ret, "while reading inode to clone");
			break;
		}

		write_len = ocfs2_align_bytes_to_blocks(fs, got);
		ret = ocfs2_file_write(clone_ci, buf, write_len,
				       offset, &wrote);
		if (ret) {
			com_err(whoami, ret, "while writing clone data");
			break;
		}
		assert(got == wrote);
		offset += wrote;
	}

	ocfs2_free(&buf);
	return ret;
}

static errcode_t swap_clone(ocfs2_filesys *fs, ocfs2_cached_inode *orig_ci,
			    ocfs2_cached_inode *clone_ci)
{
	uint32_t clusters;
	errcode_t ret;
	struct ocfs2_extent_list *tmp_el = NULL;
	struct ocfs2_extent_list *orig_el = &orig_ci->ci_inode->id2.i_list;
	struct ocfs2_extent_list *clone_el = &clone_ci->ci_inode->id2.i_list;
	int el_size = offsetof(struct ocfs2_extent_list, l_recs) +
		sizeof(struct ocfs2_extent_rec) * orig_el->l_count;

	ret = ocfs2_malloc0(el_size, &tmp_el);
	if (ret) {
		com_err(whoami, ret,
			"while allocating temporary memory to swap a "
			"cloned inode");
		goto out;
	}

	memcpy(tmp_el, orig_el, el_size);
	memcpy(orig_el, clone_el, el_size);
	memcpy(clone_el, tmp_el, el_size);

	/*
	 * In new_clone, we allocate all the clusters disregard whether
	 * there are holes. So here we need to update the i_clusters also.
	 */
	clusters = orig_ci->ci_inode->i_clusters;
	orig_ci->ci_inode->i_clusters = clone_ci->ci_inode->i_clusters;
	clone_ci->ci_inode->i_clusters = clusters;

	/*
	 * We write the cloned inode with the original extent list first.
	 * If we crash between writing the cloned inode and the original
	 * one, the cloned inode will appear to share the same extents
	 * as the original and the extents we just allocated to the clone
	 * will look unused to a subsequent fsck run.  They'll be reusable
	 * for recovery.
	 */
	ret = ocfs2_write_cached_inode(fs, clone_ci);
	if (ret) {
		com_err(whoami, ret,
			"while writing out clone inode %"PRIu64,
			clone_ci->ci_blkno);
		goto out;
	}

	ret = ocfs2_write_cached_inode(fs, orig_ci);
	if (ret)
		com_err(whoami, ret, "while writing out inode %"PRIu64,
			orig_ci->ci_blkno);

out:
	if (tmp_el)
		ocfs2_free(&tmp_el);
	return ret;
}

static int can_free(struct dup_context *dct, uint32_t cpos)
{
	struct dup_cluster *dc;
	int unhandled = 0;

	dc = dup_cluster_lookup(dct, cpos);
	/* We don't call can_free unless it's in the dup bitmap */
	assert(dc);

	/*
	 * See how many inodes still point to it.  It can't be zero,
	 * because we're working on an inode that points to it RIGHT
	 * NOW.
	 */
	for_each_owner(dct, dc, count_func, &unhandled);
	assert(unhandled > 0);
	if (unhandled > 1)
		return 0;
	return 1;
}

static errcode_t pass1d_free_clusters(ocfs2_filesys *fs, uint32_t len,
				      uint64_t start, void *free_data)
{
	errcode_t ret = 0;
	int was_set;
	struct fix_dup_context *fd = free_data;
	uint32_t p_cpos, p_start = ocfs2_blocks_to_clusters(fs, start);

	for (p_cpos = p_start; p_cpos < (p_start + len); p_cpos++) {
		verbosef("checking cpos %"PRIu32"\n", p_cpos);
		ret = ocfs2_bitmap_test(fd->fd_ost->ost_duplicate_clusters,
					p_cpos, &was_set);
		if (ret) {
			com_err(whoami, ret,
				"while testing cluster %"PRIu32" in "
				"the duplicate cluster map",
				p_cpos);
			break;
		}

		verbosef("cpos %"PRIu32" was_set == %d\n", p_cpos, was_set);
		if (was_set) {
			if (can_free(fd->fd_dct, p_cpos))
				verbosef("Freeing multiply-claimed cluster "
					 "%"PRIu32", as it is no longer used\n",
					 p_cpos);
			else
				continue;
		}

		verbosef("Freeing cluster %"PRIu32"\n", p_cpos);
		ret = ocfs2_free_clusters(fd->fd_ost->ost_fs, 1,
					  ocfs2_clusters_to_blocks(fs, p_cpos));
		if (ret) {
			com_err(whoami, ret,
				"while freeing duplicate cluster "
				"%"PRIu32,
				p_cpos);
			break;
		}
	}

	return ret;
}

static int delete_one_inode(struct fix_dup_context *fd, uint64_t ino)
{
	errcode_t ret;
	o2fsck_state *ost = fd->fd_ost;

	verbosef("Truncating inode %"PRIu64"\n", ino);
	ret = ocfs2_truncate_full(ost->ost_fs, ino, 0,
				  pass1d_free_clusters, fd);
	if (ret) {
		com_err(whoami, ret,
			"while truncating inode %"PRIu64" to remove it",
			ino);
		goto out;
	}

	verbosef("Deleting inode %"PRIu64"\n", ino);
	ret = ocfs2_delete_inode(ost->ost_fs, ino);
	if (ret)
		com_err(whoami, ret, "while removing inode %"PRIu64, ino);
	else
		o2fsck_icount_set(ost->ost_icount_in_inodes, ino, 0);

out:
	return ret ? 1 : 0;
}

static int clone_one_inode(struct fix_dup_context *fd, struct dup_inode *di)
{
	errcode_t ret, tmpret;
	ocfs2_filesys *fs = fd->fd_ost->ost_fs;
	ocfs2_cached_inode *orig_ci = NULL, *clone_ci = NULL;

	ret = ocfs2_read_cached_inode(fs, di->di_ino, &orig_ci);
	if (ret) {
		com_err(whoami, ret,
			"while reading inode \"%s\" to clone it",
			di->di_path);
		goto out;
	}

	ret = new_clone(fs, orig_ci, &clone_ci);
	if (ret)
		goto out;

	verbosef("Copying inode \"%s\" to clone %"PRIu64"\n", di->di_path,
		 clone_ci->ci_blkno);
	ret = copy_clone(fs, orig_ci, clone_ci);
	if (ret)
		goto out;

	ret = swap_clone(fs, orig_ci, clone_ci);

out:
	if (orig_ci)
		ocfs2_free_cached_inode(fs, orig_ci);
	if (clone_ci) {
		tmpret = delete_one_inode(fd, clone_ci->ci_blkno);
		if (!ret)
			ret = tmpret;
		ocfs2_free_cached_inode(fs, clone_ci);
	}
	return ret ? 1 : 0;
}

static int fix_dups_func(struct dup_cluster *dc, struct dup_inode *di,
			 struct dup_cluster_owner *dco, void *priv_data)
{
	int ret = 0;
	struct fix_dup_context *fd = priv_data;

	if (di->di_flags & OCFS2_CHAIN_FL) {
		printf("Inode \"%s\" is a chain allocator and cannot "
		       "be cloned or deleted.\n",
		       di->di_path);
		print_chain_warning();
		return 0;
	}

	if (di->di_flags & OCFS2_SYSTEM_FL) {
		if (prompt(fd->fd_ost, PY, PR_DUP_CLUSTERS_SYSFILE_CLONE,
			   "Inode \"%s\" is a system file. It may be "
			   "cloned but not deleted. Clone inode \"%s\" to "
			   "break claims on clusters it shares with other "
			   "inodes?",
			   di->di_path, di->di_path)) {
			ret = clone_one_inode(fd, di);
			if (!ret)
				di->di_state |= DUP_INODE_CLONED;
		}
	} else {
		if (prompt(fd->fd_ost, PY, PR_DUP_CLUSTERS_CLONE,
			   "Inode \"%s\" may be cloned or deleted to "
			   "break the claim it has on its clusters. "
			   "Clone inode \"%s\" to break claims on "
			   "clusters it shares with other inodes?",
			   di->di_path, di->di_path)) {
			ret = clone_one_inode(fd, di);
			if (!ret)
				di->di_state |= DUP_INODE_CLONED;
		} else if (prompt(fd->fd_ost, PN, PR_DUP_CLUSTERS_DELETE,
				  "Delete inode \"%s\" to break claims on "
				  "clusters it shares with other inodes?",
				  di->di_path)) {
			ret = delete_one_inode(fd, di->di_ino);
			if (!ret)
				di->di_state |= DUP_INODE_REMOVED;
		}
	}

	return ret;
}

struct create_refcount {
	o2fsck_state *cr_ost;
	uint64_t cr_refcount_loc;
	errcode_t cr_err;
};

static int create_refcount_func(struct dup_cluster *dc, struct dup_inode *di,
				struct dup_cluster_owner *dco, void *priv_data)
{
	errcode_t ret;
	struct create_refcount *cr = priv_data;
	ocfs2_filesys *fs = cr->cr_ost->ost_fs;

	if (!di->di_refcount_loc) {
		ret = ocfs2_attach_refcount_tree(fs, di->di_ino,
						 cr->cr_refcount_loc);
		if (ret) {
			com_err(whoami, ret,
				"while attaching file %"PRIu64" to"
				"refcount tree %"PRIu64, di->di_ino,
				cr->cr_refcount_loc);
			goto out;
		}
		di->di_refcount_loc = cr->cr_refcount_loc;
	}

	ret = ocfs2_change_refcount_flag(fs, di->di_ino,
					 dco->dco_cpos, 1, dc->dc_cluster,
					 OCFS2_EXT_REFCOUNTED, 0);
	if (ret) {
		com_err(whoami, ret,
			"while mark extent refcounted at %u in file %"PRIu64,
			dco->dco_cpos, di->di_ino);
		goto out;
	}

	ret = ocfs2_increase_refcount(fs, di->di_ino, dc->dc_cluster, 1);
	if (ret)
		com_err(whoami, ret,
			"while increasing refcount at %u for file %"PRIu64,
			dc->dc_cluster, di->di_ino);

out:
	cr->cr_err = ret;
	return ret;
}
/*
 * Create refcount record for all the files sharing the same clusters.
 * Create a new refcount tree if all the files don't have it(refcount_loc = 0).
 * If a file don't have refcount tree, attach it to that tree.
 */
static errcode_t o2fsck_create_refcount(o2fsck_state *ost,
					struct dup_context *dct,
					struct dup_cluster *dc,
					uint64_t refcount_loc)
{
	errcode_t ret = 0;
	struct create_refcount cr = {
		.cr_ost = ost,
		.cr_refcount_loc = refcount_loc,
	};

	if (!refcount_loc) {
		ret = ocfs2_create_refcount_tree(ost->ost_fs,
						 &refcount_loc);
		if (ret) {
			com_err(whoami, ret,
				"while allocating a new refcount block");
			goto out;
		}
		cr.cr_refcount_loc = refcount_loc;
	}

	for_each_owner(dct, dc, create_refcount_func, &cr);

	ret = cr.cr_err;
out:
	return ret;
}

static errcode_t o2fsck_pass1d(o2fsck_state *ost, struct dup_context *dct)
{
	errcode_t ret = 0;
	struct dup_cluster *dc;
	struct rb_node *node = rb_first(&dct->dup_clusters);
	uint64_t dups, refcount_loc;
	struct fix_dup_context fd = {
		.fd_ost = ost,
		.fd_dct = dct,
	};

	whoami = "pass1d";
	printf("Pass 1d: Reconciling multiply-claimed clusters\n");

	for (node = rb_first(&dct->dup_clusters); node; node = rb_next(node)) {
		dc = rb_entry(node, struct dup_cluster, dc_node);
		dups = 0;
		for_each_owner(dct, dc, count_func, &dups);
		if (dups < 2)
			continue;

		printf("Cluster %"PRIu32" is claimed by the following "
		       "inodes:\n",
		       dc->dc_cluster);
		for_each_owner(dct, dc, print_func, NULL);

		/* We try create refcount tree first. */
		if (ocfs2_refcount_tree(OCFS2_RAW_SB(ost->ost_fs->fs_super))) {
			refcount_loc = 0;
			for_each_owner(dct, dc, find_refcount_func,
				       &refcount_loc);
			if (refcount_loc != UINT64_MAX &&
			    prompt(ost, PY, PR_DUP_CLUSTERS_ADD_REFCOUNT,
				   "Create refcount record for it?")) {
				ret = o2fsck_create_refcount(ost, dct, dc,
							     refcount_loc);
				if (ret)
					break;
				continue;
			}
		}

		for_each_owner(dct, dc, fix_dups_func, &fd);
		if (fd.fd_err) {
			ret = fd.fd_err;
			break;
		}
	}

	return ret;
}


/*
 * Exported call
 */
errcode_t ocfs2_pass1_dups(o2fsck_state *ost)
{
	errcode_t ret;
	struct dup_context dct = {
		.dup_clusters = RB_ROOT,
		.dup_inodes = RB_ROOT,
	};

	ret = o2fsck_pass1b(ost, &dct);
	if (!ret) {
		o2fsck_pass1c(ost, &dct);
		ret = o2fsck_pass1d(ost, &dct);
	}

	o2fsck_empty_dup_context(&dct);
	return ret;
}
