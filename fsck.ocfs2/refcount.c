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
#include "ocfs2/ocfs2.h"
#include "problem.h"

static const char *whoami = "refcount.c";

static errcode_t check_rb(o2fsck_state *ost, uint64_t blkno,
			  uint64_t root_blkno, int *is_valid)
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
