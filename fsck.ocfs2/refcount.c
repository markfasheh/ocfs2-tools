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

#include "ocfs2/ocfs2.h"
#include "problem.h"
#include "fsck.h"
#include "extent.h"

static const char *whoami = "refcount.c";

struct check_refcount_rec {
	uint64_t root_blkno;
	uint64_t c_end;
};

static errcode_t check_rb(o2fsck_state *ost, uint64_t blkno,
			  uint64_t root_blkno, uint64_t *c_end, int *is_valid);

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
						int *changed,
						void *para)
{
	errcode_t ret;
	int is_valid = 1;
	struct check_refcount_rec *check = para;

	ret = check_rb(ost, er->e_blkno,
		       check->root_blkno, &check->c_end, &is_valid);

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
			  uint64_t root_blkno, uint64_t *c_end, int *is_valid)
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
		check_el(ost, &ei, rb->rf_blkno, &rb->rf_list,
			 max_recs, &changed);
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
