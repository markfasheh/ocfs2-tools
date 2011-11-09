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

#include <inttypes.h>
#include <assert.h>

#include "ocfs2/kernel-rbtree.h"
#include "ocfs2-kernel/kernel-list.h"

#include "ocfs2/ocfs2.h"
#include "problem.h"
#include "fsck.h"
#include "extent.h"
#include "util.h"
#include "refcount.h"

static const char *whoami = "refcount.c";

struct check_refcount_rec {
	uint64_t root_blkno;
	uint64_t c_end;
};

/* every REFCOUNTED ocfs2_extent_rec will become one. */
struct refcount_extent {
	struct rb_node ext_node;
	uint32_t v_cpos;
	uint32_t clusters;
	uint64_t p_cpos;
};

struct refcount_file {
	struct list_head list;
	uint64_t i_blkno;
	struct rb_root ref_extents; /* store every refcounted extent rec
				     * in this file. */
};

struct refcount_tree {
	struct rb_node ref_node;
	uint64_t rf_blkno;
	uint64_t rf_end;
	struct list_head files_list;
	int files_count;
	int is_valid;
	char *root_buf;
	char *leaf_buf;
	/* the cluster offset we have checked against this tree. */
	uint64_t p_cend;
};

static errcode_t check_rb(o2fsck_state *ost, uint64_t blkno,
			  uint64_t root_blkno, uint64_t *c_end,
			  uint32_t offset, int no_holes, int *is_valid);

static void check_rl(o2fsck_state *ost,
		     uint64_t rb_blkno, uint64_t root_blkno,
		     struct ocfs2_refcount_list *rl,
		     uint64_t *c_end, int *changed)
{
	struct ocfs2_refcount_rec *rec;
	uint16_t i;
	size_t cpy;
	int trust_used = 1;
	int max_recs = ocfs2_refcount_recs_per_rb(ost->ost_fs->fs_blocksize);

	verbosef("count %u used %u\n", rl->rl_count, rl->rl_used);

	if (rl->rl_count > max_recs &&
	    prompt(ost, PY, PR_REFCOUNT_LIST_COUNT,
		   "Refcount list in refcount tree %"PRIu64" claims to have %u "
		   "records, but the maximum is %u. Fix the list's count?",
		   root_blkno, rl->rl_count, max_recs)) {

		rl->rl_count = max_recs;
		*changed = 1;
	}

	if (max_recs > rl->rl_count)
		max_recs = rl->rl_count;

	if (rl->rl_used > max_recs) {
		if (prompt(ost, PY, PR_REFCOUNT_LIST_USED,
			   "Refcount list in refcount tree %"PRIu64" claims %u "
			   "as the used record, but fsck believes "
			   "the largest valid value is %u.  Clamp the used "
			   "record value?", root_blkno,
			   rl->rl_used, max_recs)) {

			rl->rl_used = rl->rl_count;
			*changed = 1;
		} else
			trust_used = 0;
	}

	if (trust_used)
		max_recs = rl->rl_used;

	for (i = 0; i < max_recs; i++) {
		rec = &rl->rl_recs[i];

		/* offer to remove records that point to nowhere */
		if (ocfs2_block_out_of_range(ost->ost_fs,
			ocfs2_clusters_to_blocks(ost->ost_fs,
					rec->r_cpos + rec->r_clusters - 1)) &&
		    prompt(ost, PY, PR_REFCOUNT_CLUSTER_RANGE,
			   "Refcount record %u in refcount block %"PRIu64" "
			   "of refcount tree %"PRIu64" refers to a cluster "
			   "that is out of range.  Remove "
			   "this record from the refcount list?",
			   i, rb_blkno, root_blkno)) {
			if (!trust_used) {
				printf("Can't remove the record becuase "
				       "rl_used hasn't been fixed\n");
				continue;
			}
			goto remove_rec;
		}

		if (rec->r_cpos < *c_end &&
		    prompt(ost, PY, PR_REFCOUNT_CLUSTER_COLLISION,
			   "Refcount record %u in refcount block %"PRIu64" "
			   "of refcount tree %"PRIu64" refers to a cluster "
			   "that is collided with the previous record.  Remove "
			   "this record from the refcount list?",
			   i, rb_blkno, root_blkno)) {
			if (!trust_used) {
				printf("Can't remove the record becuase "
				       "rl_used hasn't been fixed\n");
				continue;
			}
			goto remove_rec;
		}

		*c_end = rec->r_cpos + rec->r_clusters;
		continue;
remove_rec:
		cpy = (max_recs - i - 1) * sizeof(*rec);
		/* shift the remaining recs into this ones place */
		if (cpy != 0) {
			memcpy(rec, rec + 1, cpy);
			memset(&rl->rl_recs[max_recs - 1], 0,
			       sizeof(*rec));
			i--;
		}
		rl->rl_used--;
		max_recs--;
		*changed = 1;
		continue;
	}
}

static errcode_t refcount_check_leaf_extent_rec(o2fsck_state *ost,
						uint64_t owner,
						struct ocfs2_extent_list *el,
						struct ocfs2_extent_rec *er,
						int *changed, uint32_t offset,
						int no_holes,
						void *para)
{
	errcode_t ret;
	int is_valid = 1;
	struct check_refcount_rec *check = para;

	ret = check_rb(ost, er->e_blkno,
		       check->root_blkno, &check->c_end, offset, no_holes,
		       &is_valid);

	if (!is_valid &&
	    prompt(ost, PY, PR_REFCOUNT_BLOCK_INVALID,
		   "Refcount block %"PRIu64 " for tree %"PRIu64" is invalid. "
		   "Remove it from the tree?",
		   (uint64_t)er->e_blkno, check->root_blkno)) {
		er->e_blkno = 0;
		*changed = 1;
	}

	return ret;
}

static errcode_t check_rb(o2fsck_state *ost, uint64_t blkno,
			  uint64_t root_blkno, uint64_t *c_end,
			  uint32_t offset, int no_holes, int *is_valid)
{
	int changed = 0;
	char *buf = NULL;
	struct ocfs2_refcount_block *rb;
	errcode_t ret;

	/* XXX test that the block isn't already used */

	/* we only consider a refcount block invalid if we were able to read
	 * it and it didn't have a refcount block signature */
	*is_valid = 1;

	ret = ocfs2_malloc_block(ost->ost_fs->fs_io, &buf);
	if (ret) {
		com_err(whoami, ret, "while allocating a block-sized buffer "
			"for a refcount block");
		goto out;
	}

	ret = ocfs2_read_refcount_block_nocheck(ost->ost_fs, blkno, buf);
	if (ret) {
		com_err(whoami, ret, "reading refcount block at %"PRIu64" in "
			"refcount tree %"PRIu64" for verification", blkno,
			root_blkno);
		if (ret == OCFS2_ET_BAD_EXTENT_BLOCK_MAGIC)
			*is_valid = 0;
		goto out;
	}

	rb = (struct ocfs2_refcount_block *)buf;

	if (rb->rf_blkno != blkno &&
	    prompt(ost, PY, PR_RB_BLKNO,
		   "A refcount block at %"PRIu64" in refcount tree %"PRIu64" "
		   "claims to be located at block %"PRIu64".  Update the "
		   "refcount block's location?", blkno, root_blkno,
		   (uint64_t)rb->rf_blkno)) {
		rb->rf_blkno = blkno;
		changed = 1;
	}

	if (rb->rf_fs_generation != ost->ost_fs_generation) {
		if (prompt(ost, PY, PR_RB_GEN,
			   "A refcount block at %"PRIu64" in refcount tree "
			   "%"PRIu64" has a generation of %x which doesn't "
			   "match the volume's generation of %x.  Consider "
			   "this refcount block invalid?", blkno,
			   root_blkno, rb->rf_fs_generation,
			   ost->ost_fs_generation)) {

			*is_valid = 0;
			goto out;
		}
		if (prompt(ost, PY, PR_RB_GEN_FIX,
			   "Update the refcount block's generation to match "
			   "the volume?")) {
			rb->rf_fs_generation = ost->ost_fs_generation;
			changed = 1;
		}
	}

	if (rb->rf_blkno != root_blkno &&
	    rb->rf_parent != root_blkno &&
	    prompt(ost, PY, PR_RB_PARENT,
		   "A refcount block at %"PRIu64" in refcount tree %"PRIu64" "
		   "claims to belong to tree %"PRIu64".  Update the "
		   "parent's information?", blkno, root_blkno,
		   (uint64_t)rb->rf_parent)) {
		rb->rf_parent = root_blkno;
		changed = 1;
	}

	/* XXX worry about suballoc node/bit */

	if (rb->rf_flags & OCFS2_REFCOUNT_TREE_FL) {
		struct check_refcount_rec check = {root_blkno, *c_end};
		struct extent_info ei = {0, };
		uint16_t max_recs =
			ocfs2_extent_recs_per_rb(ost->ost_fs->fs_blocksize);

		ei.para = &check;
		ei.chk_rec_func = refcount_check_leaf_extent_rec;
		/*
		 * leaf extent rec for a refcount tree is allocated from
		 * extent_alloc, so we don't need to set mark_rec_alloc_func
		 * here.
		 */
		check_el(ost, &ei, rb->rf_blkno, &rb->rf_list,
			 max_recs, offset, no_holes, &changed);
		*c_end = check.c_end;

		if (ei.ei_clusters != rb->rf_clusters &&
		    prompt(ost, PY, PR_REFCOUNT_CLUSTERS,
			   "Refcount tree %"PRIu64" claims to have %u "
			   "clusters, but we only found %u. "
			   "Fix it?", root_blkno,
			   rb->rf_clusters, (uint32_t)ei.ei_clusters)) {
			rb->rf_clusters = ei.ei_clusters;
			changed = 1;
		}
	} else {
		assert(c_end);
		check_rl(ost, root_blkno, blkno,
			 &rb->rf_records, c_end, &changed);

		/* We allow the root block to be empty. */
		if (root_blkno != blkno && !rb->rf_records.rl_used &&
		    prompt(ost, PY, PR_REFCOUNT_LIST_EMPTY,
			   "Refcount block %"PRIu64" claims to have no "
			   "refcount record in it. Consider it as invalid "
			   "and Remove from tree?",
			   (uint64_t)rb->rf_blkno)) {
			*is_valid = 0;
			changed = 1;
		}
	}

	if (changed) {
		ret = ocfs2_write_refcount_block(ost->ost_fs, blkno, buf);
		if (ret) {
			com_err(whoami, ret, "while writing an updated "
				"refcount block at %"PRIu64" for refcount "
				"tree %"PRIu64,	blkno, root_blkno);
			goto out;
		}
	}

out:
	if (buf)
		ocfs2_free(&buf);
	return 0;
}

/* See if the recount_tree rbtree has the given ref_blkno.  */
static struct refcount_tree*
refcount_tree_lookup(o2fsck_state *ost, uint64_t ref_blkno)
{
	struct rb_node *p = ost->ost_refcount_trees.rb_node;
	struct refcount_tree *ref_tree;

	while (p) {
		ref_tree = rb_entry(p, struct refcount_tree, ref_node);
		if (ref_blkno < ref_tree->rf_blkno)
			p = p->rb_left;
		else if (ref_blkno > ref_tree->rf_blkno)
			p = p->rb_right;
		else
			return ref_tree;
	}

	return NULL;
}

static void refcount_tree_insert(o2fsck_state *ost,
				 struct refcount_tree *insert_rb)
{
	struct rb_node **p = &ost->ost_refcount_trees.rb_node;
	struct rb_node *parent = NULL;
	struct refcount_tree *ref_tree = NULL;

	while (*p) {
		parent = *p;
		ref_tree = rb_entry(parent, struct refcount_tree, ref_node);
		if (insert_rb->rf_blkno < ref_tree->rf_blkno)
			p = &(*p)->rb_left;
		else if (insert_rb->rf_blkno > ref_tree->rf_blkno)
			p = &(*p)->rb_right;
		else
			assert(0);  /* Caller checked */
	}

	rb_link_node(&insert_rb->ref_node, parent, p);
	rb_insert_color(&insert_rb->ref_node, &ost->ost_refcount_trees);
}

errcode_t o2fsck_check_refcount_tree(o2fsck_state *ost,
				     struct ocfs2_dinode *di)
{
	errcode_t ret = 0;
	uint64_t c_end = 0;
	int is_valid = 1;
	struct refcount_tree *tree;
	struct refcount_file *file;

	if (!(di->i_dyn_features & OCFS2_HAS_REFCOUNT_FL))
		return 0;

	tree = refcount_tree_lookup(ost, di->i_refcount_loc);
	if (tree)
		goto check_valid;

	ret = ocfs2_malloc0(sizeof(struct refcount_tree), &tree);
	if (ret)
		return ret;

	ret = check_rb(ost, di->i_refcount_loc, di->i_refcount_loc,
		       &c_end, 0, 0, &is_valid);

	/*
	 * Add refcount tree to the rb-tree.
	 * rf_end records the end of the refcount record we have.
	 * It will be used later.
	 */
	tree->rf_blkno = di->i_refcount_loc;
	tree->is_valid = is_valid;
	tree->rf_end = c_end;
	INIT_LIST_HEAD(&tree->files_list);
	refcount_tree_insert(ost, tree);

check_valid:
	if (!tree->is_valid &&
	    prompt(ost, PY, PR_REFCOUNT_ROOT_BLOCK_INVALID,
		   "Refcount tree %"PRIu64 " for inode %"PRIu64" is invalid. "
		   "Remove it and clear the flag for the inode?",
		   (uint64_t)di->i_refcount_loc, (uint64_t)di->i_blkno)) {
		di->i_refcount_loc = 0;
		di->i_dyn_features &= ~OCFS2_HAS_REFCOUNT_FL;

		o2fsck_write_inode(ost, di->i_blkno, di);
	} else {
		ret = ocfs2_malloc0(sizeof(struct refcount_file), &file);
		if (!ret) {
			file->i_blkno = di->i_blkno;
			INIT_LIST_HEAD(&file->list);

			list_add_tail(&file->list, &tree->files_list);
			tree->files_count++;
		}
	}

	return ret;
}

static void refcount_extent_insert(struct refcount_file *file,
				   struct refcount_extent *insert)
{
	struct rb_node **p = &file->ref_extents.rb_node;
	struct rb_node *parent = NULL;
	struct refcount_extent *extent = NULL;

	while (*p) {
		parent = *p;
		extent = rb_entry(parent, struct refcount_extent, ext_node);
		if (insert->p_cpos < extent->p_cpos)
			p = &(*p)->rb_left;
		else if (insert->p_cpos > extent->p_cpos)
			p = &(*p)->rb_right;
		else
			assert(0);  /* Caller checked */
	}

	rb_link_node(&insert->ext_node, parent, p);
	rb_insert_color(&insert->ext_node, &file->ref_extents);
}

errcode_t o2fsck_mark_clusters_refcounted(o2fsck_state *ost,
					  uint64_t rf_blkno,
					  uint64_t i_blkno,
					  uint64_t p_cpos,
					  uint32_t clusters,
					  uint32_t v_cpos)
{
	errcode_t ret;
	struct refcount_tree *tree;
	struct refcount_file *file = ost->ost_latest_file;
	struct list_head *p, *next;
	struct refcount_extent *extent;

	if (file && file->i_blkno == i_blkno)
		goto add_clusters;

	tree = refcount_tree_lookup(ost, rf_blkno);
	/* We should already insert the tree during refcount tree check. */
	assert(tree);

	list_for_each_safe(p, next, &tree->files_list) {
		file = list_entry(p, struct refcount_file, list);
		if (file->i_blkno == i_blkno)
			goto add_clusters;
	}
	/* We should already insert the file during refcount tree check. */
	assert(0);

add_clusters:
	ost->ost_latest_file = file;
	ret = ocfs2_malloc0(sizeof(struct refcount_extent), &extent);
	if (ret)
		return ret;

	extent->v_cpos = v_cpos;
	extent->clusters = clusters;
	extent->p_cpos = p_cpos;

	refcount_extent_insert(file, extent);
	return 0;
}

/*
 * Given a refcount tree, find the lowest p_cpos of all
 * the files sharing the tree.
 */
static int get_refcounted_extent(struct refcount_tree *tree,
				 uint64_t *p_cpos,
				 uint32_t *p_clusters,
				 uint32_t *p_refcount)
{
	struct refcount_extent *extent;
	struct refcount_file *file;
	struct list_head *p, *next;
	struct rb_node *node;
	uint64_t cpos = UINT64_MAX;
	uint32_t clusters = 0, refcount = 0;
	int found = 0;

	list_for_each_safe(p, next, &tree->files_list) {
		file = list_entry(p, struct refcount_file, list);
		node = rb_first(&file->ref_extents);

		/*
		 * If the file has no extent, go to next file.
		 * XXX: We can improve it here by removing the empty file.
		 */
		if (!node)
			continue;

		found = 1;

		extent = rb_entry(node, struct refcount_extent, ext_node);
		if (extent->p_cpos < cpos) {
			/* We meet with a new start. */
			clusters = cpos - extent->p_cpos < extent->clusters ?
				   cpos - extent->p_cpos : extent->clusters;
			cpos = extent->p_cpos;
			refcount = 1;
		} else if (extent->p_cpos == cpos) {
			clusters = clusters < extent->clusters ?
				   clusters : extent->clusters;
			refcount++;
		} else if (extent->p_cpos < cpos + clusters) {
			/*
			 * extent->p_cpos > cpos, change clusters accordingly.
			 */
			clusters = extent->p_cpos - cpos;
		}
	}

	if (!found)
		return 0;

	*p_cpos = cpos;
	*p_clusters = clusters;
	*p_refcount = refcount;

	return 1;
}

/*
 * Remove pair(cpos, clusters) from the all the files sharing the tree.
 * The pair is actually got by get_refcounted_extent.
 */
static void remove_refcounted_extent(struct refcount_tree *tree,
				     uint64_t cpos,
				     uint32_t clusters)
{
	struct refcount_extent *extent;
	struct refcount_file *file;
	struct list_head *p, *next;
	struct rb_node *node;

	/* Remove the tuple from the refcounted file. */
	list_for_each_safe(p, next, &tree->files_list) {
		file = list_entry(p, struct refcount_file, list);
		node = rb_first(&file->ref_extents);

		/* If the file has no extent, go to next file. */
		if (!node)
			continue;

		extent = rb_entry(node, struct refcount_extent, ext_node);
		assert(extent->p_cpos >= cpos);

		if (cpos + clusters <= extent->p_cpos)
			continue;

		assert(extent->p_cpos + extent->clusters >= cpos + clusters);

		if (cpos + clusters == extent->p_cpos + extent->clusters) {
			rb_erase(&extent->ext_node, &file->ref_extents);
			ocfs2_free(&extent);
		} else {
			extent->clusters =
				(extent->p_cpos + extent->clusters) -
				(cpos + clusters);
			extent->p_cpos = cpos + clusters;
		}
	}
}

/*
 * Check all the files sharing the tree and if there is a file contains
 * the (p_cpos, len) with refcounted flag, we clear it.
 * Note:
 * This function is only called when checking a continuous clusters.
 * The pair (p_cpos, len) is a part of the original tuple we get from
 * get_refcounted_extent, so it can't be in 2 different refcount_extent.
 */
static errcode_t o2fsck_clear_refcount(o2fsck_state *ost,
				       struct refcount_tree *tree,
				       uint64_t p_cpos,
				       uint32_t len)
{
	errcode_t ret = 0;
	struct refcount_extent *extent;
	struct refcount_file *file;
	struct list_head *p, *next;
	struct rb_node *node;
	uint32_t v_start;

	list_for_each_safe(p, next, &tree->files_list) {
		file = list_entry(p, struct refcount_file, list);
		node = file->ref_extents.rb_node;

		/* If the file has no extent, go to next file. */
		if (!node)
			continue;

		while (node) {
			extent = rb_entry(node,
					  struct refcount_extent, ext_node);
			if (extent->p_cpos > p_cpos + len)
				node = node->rb_left;
			else if (extent->p_cpos + extent->clusters <= p_cpos)
				node = node->rb_right;
			else
				break;
		}

		if (node &&
		    (extent->p_cpos <= p_cpos &&
		     extent->p_cpos + extent->clusters >= p_cpos + len)) {
			v_start = p_cpos - extent->p_cpos + extent->v_cpos;
			ret = ocfs2_change_refcount_flag(ost->ost_fs,
							 file->i_blkno,
							 v_start, len,
							 p_cpos, 0,
							 OCFS2_EXT_REFCOUNTED);
			if (ret) {
				com_err(whoami, ret,
					"while clearing refcount flag at "
					"%u in file %"PRIu64,
					v_start, file->i_blkno);
				goto out;
			}
		}
	}

out:
	return ret;
}

/*
 * o2fsck_refcount_punch_hole and o2fsck_change_refcount are just wrappers
 * for the corresponding libocfs2 functions with one addition: re-read
 * root refcount block since we may have changed the tree during the operation.
 */
static errcode_t o2fsck_refcount_punch_hole(o2fsck_state *ost,
					    struct refcount_tree *tree,
					    uint64_t p_cpos, uint32_t len)
{
	errcode_t ret;

	ret = ocfs2_refcount_punch_hole(ost->ost_fs, tree->rf_blkno,
					p_cpos, len);
	if (ret) {
		com_err(whoami, ret, "while punching hole in "
			"(%"PRIu64", %u) in refcount tree %"PRIu64,
			p_cpos, len, tree->rf_blkno);
		goto out;
	}

	/* re-read the root blkno since we may have changed it somehow. */
	ret = ocfs2_read_refcount_block(ost->ost_fs,
					tree->rf_blkno, tree->root_buf);

out:
	return ret;
}

static errcode_t o2fsck_change_refcount(o2fsck_state *ost,
					struct refcount_tree *tree,
					uint64_t p_cpos, uint32_t len,
					uint32_t refcount)
{
	errcode_t ret;

	ret = ocfs2_change_refcount(ost->ost_fs, tree->rf_blkno,
				    p_cpos, len, refcount);
	if (ret) {
		com_err(whoami, ret, "while changing refcount in "
			"(%"PRIu64", %u) in refcount tree %"PRIu64" to %u",
			p_cpos, len, tree->rf_blkno, refcount);
		goto out;
	}

	/* re-read the root blkno since we may have changed it somehow. */
	ret = ocfs2_read_refcount_block(ost->ost_fs,
					tree->rf_blkno, tree->root_buf);

out:
	return ret;

}

/*
 * Given [cpos, end), remove all the refcount records in this range from
 * the refcount tree.
 */
static errcode_t o2fsck_remove_refcount_range(o2fsck_state *ost,
					      struct refcount_tree *tree,
					      uint64_t cpos,
					      uint64_t end)
{
	errcode_t ret = 0;
	int index;
	unsigned int len;
	struct ocfs2_refcount_rec rec;
	uint64_t range = end - cpos;

	while (range) {
		len = range > UINT_MAX ? UINT_MAX : range;

		ret = ocfs2_get_refcount_rec(ost->ost_fs, tree->root_buf,
				     cpos, len, &rec,
				     &index, tree->leaf_buf);
		if (ret) {
			com_err(whoami, ret, "while getting refcount rec at "
				"%"PRIu64" in tree %"PRIu64,
				cpos, tree->rf_blkno);
			goto out;
		}

		if (!rec.r_refcount) {
			cpos += rec.r_clusters;
			range -= rec.r_clusters;
			continue;
		}

		/*
		 * In case we found some refcount rec, just ask for
		 * punching hole for the whole range (cpos, len), and
		 * o2fsck_refcount_punch_hole will handle the complex
		 * issue for us.
		 */
		if (prompt(ost, PY, PR_REFCOUNT_REC_REDUNDANT,
			   "refcount records among clusters (%"PRIu64
			   ", %u) are found with no physical clusters "
			   "corresponding to them. Remove them?", cpos, len)) {
			ret = o2fsck_refcount_punch_hole(ost, tree, cpos, len);
			if (ret) {
				com_err(whoami, ret, "while punching "
					"hole in (%"PRIu64", %u) in refcount "
					"tree %"PRIu64, cpos, len,
					tree->rf_blkno);
				goto out;
			}
		}
		cpos += len;
		range -= len;
	}

out:
	return ret;
}

/*
 * Given tuple(p_cpos, clusters, refcount), check whether the refcount
 * tree has the corresponding refcount record. If not, add/update them.
 * If the user don't allow us to change the refcount tree, add them to
 * to duplicate_clusters and let it handle them.
 */
static errcode_t o2fsck_check_clusters_in_refcount(o2fsck_state *ost,
						   struct refcount_tree *tree,
						   uint64_t p_cpos,
						   uint32_t clusters,
						   uint32_t refcount)
{
	errcode_t ret = 0;
	uint32_t rec_len;
	int index;
	struct ocfs2_refcount_rec rec;

	if (!clusters)
		return 0;

	/*
	 * the previous check ended at tree->p_cend, and now we get
	 * p_cpos, so any refcount record between p_cend and p_cpos
	 * should be considered as redundant.
	 */
	ret = o2fsck_remove_refcount_range(ost, tree, tree->p_cend,
					   p_cpos);
	if (ret) {
		com_err(whoami, ret, "while removing refcount rec from "
			"%"PRIu64" to %"PRIu64" in tree %"PRIu64,
			tree->p_cend, p_cpos, tree->rf_blkno);
		goto out;
	}

	tree->p_cend = p_cpos + clusters;
again:
	ret = ocfs2_get_refcount_rec(ost->ost_fs, tree->root_buf,
				     p_cpos, clusters, &rec,
				     &index, tree->leaf_buf);
	if (ret) {
		com_err(whoami, ret, "while getting refcount rec at "
			"%"PRIu64" in tree %"PRIu64,
			p_cpos, tree->rf_blkno);
		goto out;
	}

	/*
	 * Actually ocfs2_get_refcount_rec will fake some refcount record
	 * in case it can't find p_cpos in the refcount tree. So we really
	 * shouldn't meet with a case rec->r_cpos > p_cpos.
	 */
	assert(rec.r_cpos <= p_cpos);

	rec_len = ocfs2_min(p_cpos + clusters,
			    (uint64_t)rec.r_cpos + rec.r_clusters) - p_cpos;
	if (rec.r_refcount != refcount) {
		if (prompt(ost, PY, PR_REFCOUNT_COUNT_INVALID,
			   "clusters %"PRIu64 " with len %u have %u refcount "
			   "while there are %u files point to them. "
			   "Correct the refcount value?",
			   p_cpos, rec_len, rec.r_refcount, refcount)) {
			ret = o2fsck_change_refcount(ost, tree,
						     p_cpos, rec_len, refcount);
			if (ret) {
				com_err(whoami, ret, "while updating refcount "
					"%u at %"PRIu64" len %u in tree "
					"%"PRIu64, refcount, p_cpos,
					rec_len, tree->rf_blkno);
				goto out;
			}
		} else {
			/*
			 * XXX:
			 * Do we need to ask user for adding them to dup?
			 *
			 * Call o2fsck_mark_clusters_allocated will add them
			 * them to duplicate_clusters automatically.
			 */
			o2fsck_mark_clusters_allocated(ost, p_cpos, rec_len);
			ret = o2fsck_refcount_punch_hole(ost, tree,
							 p_cpos, rec_len);
			if (ret) {
				com_err(whoami, ret, "while punching "
					"hole at %"PRIu64"in refcount "
					"tree %"PRIu64, p_cpos,
					tree->rf_blkno);
				goto out;
			}

			ret = o2fsck_clear_refcount(ost, tree, p_cpos, rec_len);
			if (ret) {
				com_err(whoami, ret,
					"while clearing refcount for "
					"cluster %"PRIu64" len %u in %"PRIu64,
					p_cpos, rec_len, tree->rf_blkno);
				goto out;
			}
		}
	}

	/* we have finished checking (p_cpos, clusters). */
	if (p_cpos + clusters <= rec.r_cpos + rec.r_clusters)
		goto out;

	/*
	 * now we have finished checking current refcount_rec,
	 * p_cpos + clusters > rec.r_cpos + rec.r_clusters,
	 * need to read next refcount_rec.
	 */
	clusters += p_cpos;
	p_cpos = rec.r_cpos + rec.r_clusters;
	clusters -= p_cpos;
	goto again;

out:
	return ret;
}

static errcode_t o2fsck_check_refcount_clusters(o2fsck_state *ost,
						struct refcount_tree *tree,
						uint64_t start,
						uint32_t len,
						uint32_t refcount)
{
	int val;
	errcode_t ret = 0;
	uint64_t p_cend;
	uint32_t clusters;

	o2fsck_mark_clusters_allocated(ost, start, len);

	while (len) {
		if (ost->ost_duplicate_clusters) {
			/*
			 * Check whether the clusters can be found in
			 * duplicated cluster list.
			 */
			p_cend = start;
			clusters = len;
			while (clusters) {
				ocfs2_bitmap_test(
					ost->ost_duplicate_clusters,
					p_cend, &val);
				if (val)
					break;

				clusters--;
				p_cend++;
			}
		} else
			p_cend = start + len;

		/*
		 * p_cend points to the end cluster we will check in this loop.
		 *
		 * If there is a cluster which is already setted by other
		 * owner(find in duplicate_clusters), p_end now points to it.
		 *
		 * So we check the refcounted clusters [start, p_cend) and then
		 * punch a hole in refcount tree at p_cend in case.
		 */
		clusters = p_cend - start;

		ret = o2fsck_check_clusters_in_refcount(ost,
							tree,
							start,
							clusters,
							refcount);
		if (ret) {
			com_err(whoami, ret, "while checking "
				"refcounted clusters");
			goto out;
		}

		if (len > clusters) {
			/*
			 * We haven't finished our check and the reason
			 * is that p_cend is setted in dup_clusters, so
			 * punch a hole, clear the refcount flag for
			 * p_cend and continue our check.
			 */
			ret = o2fsck_refcount_punch_hole(ost, tree,
							 p_cend, 1);
			if (ret) {
				com_err(whoami, ret, "while punching "
					"hole at %"PRIu64"in refcount "
					"tree %"PRIu64, p_cend,
					tree->rf_blkno);
					goto out;
			}
			ret = o2fsck_clear_refcount(ost, tree,
						    p_cend, 1);
			if (ret) {
				com_err(whoami, ret,
					"while clearing refcount for "
					"cluster %"PRIu64" in %"PRIu64,
					p_cend, tree->rf_blkno);
				goto out;
			}
			/* start check from next cluster. */
			p_cend++;
		}

		len = start + len - p_cend;
		start = p_cend;
	}

out:
	return ret;
}

/*
 * Given a refcount tree, check the refcounted clusters and their refcount.
 */
static errcode_t o2fsck_check_refcount(o2fsck_state *ost,
				       struct refcount_tree *tree)
{
	errcode_t ret;
	uint64_t p_cpos = 0;
	uint32_t clusters = 0, refcount = 0;
	struct ocfs2_refcount_block *root_rb;

	ret = ocfs2_malloc_block(ost->ost_fs->fs_io, &tree->root_buf);
	if (ret) {
		com_err(whoami, ret, "while allocating a block-sized buffer "
			"for a refcount block");
		goto out;
	}

	ret = ocfs2_malloc_block(ost->ost_fs->fs_io, &tree->leaf_buf);
	if (ret) {
		com_err(whoami, ret, "while allocating a block-sized buffer "
			"for a refcount block");
		goto out;
	}

	ret = ocfs2_read_refcount_block(ost->ost_fs,
					tree->rf_blkno, tree->root_buf);
	if (ret) {
		com_err(whoami, ret, "while reading root refcount block at"
			" %"PRIu64, tree->rf_blkno);
		goto out;
	}

	root_rb = (struct ocfs2_refcount_block *)tree->root_buf;
	if (tree->files_count != root_rb->rf_count &&
	    prompt(ost, PY, PR_REFCOUNT_COUNT,
		   "Refcount tree at %"PRIu64" claims to have %u "
		   "files associated with it, but we only found %u."
		   "Update the count number?", tree->rf_blkno,
		   root_rb->rf_count, tree->files_count)) {
		root_rb->rf_count = tree->files_count;

		ret = ocfs2_write_refcount_block(ost->ost_fs, tree->rf_blkno,
						 tree->root_buf);
		if (ret) {
			com_err(whoami, ret, "while updati rb_count for tree "
				"%"PRIu64, tree->rf_blkno);
			goto out;
		}
	}

	while (get_refcounted_extent(tree, &p_cpos,
				     &clusters, &refcount)) {
		ret = o2fsck_check_refcount_clusters(ost, tree, p_cpos,
						     clusters, refcount);
		if (ret) {
			com_err(whoami, ret, "while checking refcount clusters "
				"(%"PRIu64", %u, %u) in tree %"PRIu64,
				p_cpos, clusters, refcount, tree->rf_blkno);
			goto out;
		}
		remove_refcounted_extent(tree, p_cpos, clusters);
	}

	/*
	 * Remove all the refcount rec passed p_cpos + clusters from the tree
	 * since there is no corresponding refcounted clusters.
	 */
	if (tree->rf_end > p_cpos + clusters) {
		ret = o2fsck_remove_refcount_range(ost, tree, p_cpos + clusters,
						   tree->rf_end);
		if (ret)
			com_err(whoami, ret,
				"while deleting redundant refcount rec");
	}
out:
	if (tree->root_buf)
		ocfs2_free(&tree->root_buf);
	if (tree->leaf_buf)
		ocfs2_free(&tree->leaf_buf);
	return ret;
}

errcode_t o2fsck_check_mark_refcounted_clusters(o2fsck_state *ost)
{
	errcode_t ret = 0;
	struct refcount_tree *tree;
	struct rb_node *node;
	struct list_head *p, *next;
	struct refcount_file *file;

	if (!ocfs2_refcount_tree(OCFS2_RAW_SB(ost->ost_fs->fs_super)))
		return 0;

	while ((node = rb_first(&ost->ost_refcount_trees)) != NULL) {
		tree = rb_entry(node, struct refcount_tree, ref_node);

		if (tree->is_valid) {
			ret = o2fsck_check_refcount(ost, tree);
			if (ret)
				goto out;
		}

		list_for_each_safe(p, next, &tree->files_list) {
			file = list_entry(p, struct refcount_file, list);
			node = rb_first(&file->ref_extents);
			assert(!node);
			list_del(&file->list);
			ocfs2_free(&file);
		}
		rb_erase(&tree->ref_node, &ost->ost_refcount_trees);
		ocfs2_free(&tree);
	}
out:
	return ret;
}
