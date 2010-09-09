/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * Copyright (C) 1993-2004 by Theodore Ts'o.
 * Copyright (C) 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License, version 2,  as published by the Free Software Foundation.
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
 * --
 *
 * Pass 0 verifies that the basic linkage of the various chain allocators is
 * intact so that future passes can use them in place safely.  The actual
 * bitmaps in the allocators aren't worried about here.  Later passes will
 * clean them up by loading them in to memory, updating them, and writing them
 * back out.
 *
 * Pass 1, for example, wants to iterate over the inode blocks covered by the
 * inode chain allocators so it can verify them and update the allocation
 * bitmaps for inodes that are still in use.
 *
 * The cluster chain allocator is a special case because its group descriptors
 * are at regular predictable offsets throughout the volume.  fsck forces these
 * block descriptors into service and removes and block descriptors in the
 * chain that aren't at these offsets.
 *
 * pass0 updates group descriptor chains on disk.
 *
 * XXX
 * 	track blocks and clusters we see here that iteration won't
 * 	verify more inode fields?
 * 	make sure blocks don't overlap as part of cluster tracking
 * 	make sure _bits is correct, pass in from callers
 * 	generalize the messages to chain allocators instead of inode allocators
 */

#include <string.h>
#include <inttypes.h>
#include <time.h>

#include "ocfs2/ocfs2.h"
#include "ocfs2/bitops.h"

#include "dirblocks.h"
#include "dirparents.h"
#include "icount.h"
#include "fsck.h"
#include "pass0.h"
#include "pass1.h"
#include "problem.h"
#include "util.h"

static const char *whoami = "pass0";

struct chain_state {
	uint32_t	cs_free_bits;
	uint32_t	cs_total_bits;
	uint32_t	cs_chain_no;
	uint16_t	cs_cpg;
};

static void find_max_free_bits(struct ocfs2_group_desc *gd, int *max_free_bits)
{
	int end = 0;
	int start;
	int free_bits;

	*max_free_bits = 0;

	while (end < gd->bg_bits) {
		start = ocfs2_find_next_bit_clear(gd->bg_bitmap,
						  gd->bg_bits, end);
		if (start >= gd->bg_bits)
			break;

		end = ocfs2_find_next_bit_set(gd->bg_bitmap,
					      gd->bg_bits, start);
		free_bits = end - start;
		*max_free_bits += free_bits;
	}
}

/* check whether the group really exists in the specified chain of
 * the specified allocator file.
 */
static errcode_t check_group_parent(ocfs2_filesys *fs, uint64_t group,
				    uint64_t ino, uint16_t chain,int *exist)
{
	errcode_t ret;
	uint64_t gd_blkno;
	char *buf = NULL, *gd_buf = NULL;
	struct ocfs2_dinode *di = NULL;
	struct ocfs2_group_desc *gd = NULL;
	struct ocfs2_chain_rec *cr = NULL;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		goto out;

	ret = ocfs2_read_inode(fs, ino, buf);
	if (ret) {
		goto out;
	}

	di = (struct ocfs2_dinode *)buf;

	if (!(di->i_flags & OCFS2_VALID_FL) ||
	    !(di->i_flags & OCFS2_BITMAP_FL) ||
	    !(di->i_flags & OCFS2_CHAIN_FL))
		goto out;

	if (di->id1.bitmap1.i_total == 0)
		goto out;

	if (di->id2.i_chain.cl_next_free_rec <= chain)
		goto out;

	cr = &di->id2.i_chain.cl_recs[chain];

	ret = ocfs2_malloc_block(fs->fs_io, &gd_buf);
	if (ret)
		goto out;

	gd_blkno = cr->c_blkno;
	while (gd_blkno) {
		if (gd_blkno ==  group) {
			*exist = 1;
			break;
		}

		ret = ocfs2_read_group_desc(fs, gd_blkno, gd_buf);
		if (ret)
			goto out;
		gd = (struct ocfs2_group_desc *)gd_buf;

		gd_blkno = gd->bg_next_group;
	}

out:
	if (gd_buf)
		ocfs2_free(&gd_buf);
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

static errcode_t repair_group_desc(o2fsck_state *ost,
				   struct ocfs2_dinode *di,
				   struct chain_state *cs,
				   struct ocfs2_group_desc *bg,
				   uint64_t blkno,
				   int *clear_ref)
{
	errcode_t ret = 0;
	int changed = 0;
	int max_free_bits = 0;

	verbosef("checking desc at %"PRIu64"; blkno %"PRIu64" size %u bits %u "
		 "free_bits %u chain %u generation %u\n", blkno,
		 (uint64_t)bg->bg_blkno, bg->bg_size, bg->bg_bits,
		 bg->bg_free_bits_count, bg->bg_chain, bg->bg_generation);

	if (bg->bg_generation != ost->ost_fs_generation &&
	    prompt(ost, PY, PR_GROUP_GEN,
		   "Group descriptor at block %"PRIu64" has "
		   "a generation of %"PRIx32" which doesn't match the "
		   "volume's generation of %"PRIx32".  Change the generation "
		   "in the descriptor to match the volume?", blkno,
		   bg->bg_generation, ost->ost_fs_generation)) {

		bg->bg_generation = ost->ost_fs_generation;
		changed = 1;
	}

	/* XXX maybe for advanced pain we could check to see if these 
	 * kinds of descs have valid generations for the inodes they
	 * reference */
	if ((bg->bg_parent_dinode != di->i_blkno)) {
		int exist = 0;
		ret = check_group_parent(ost->ost_fs, bg->bg_blkno,
					 bg->bg_parent_dinode,
					 bg->bg_chain, &exist);

		/* If we finds that the group really exists in the specified
		 * chain of the specified alloc inode, then this may be a
		 * duplicated group and we may need to remove it from current
		 * inode.
		 */
		if (!ret && exist && prompt(ost, PY, PR_GROUP_DUPLICATE,
		   "Group descriptor at block %"PRIu64" is "
		   "referenced by inode %"PRIu64" but thinks its parent inode "
		   "is %"PRIu64" and we can also see it in that inode."
		    " So it may be duplicated.  Remove it from this inode?",
		    blkno, (uint64_t)di->i_blkno,
		    (uint64_t)bg->bg_parent_dinode)) {
			*clear_ref = 1;
			goto out;
		}

		if (prompt(ost, PY, PR_GROUP_PARENT,
		   "Group descriptor at block %"PRIu64" is "
		   "referenced by inode %"PRIu64" but thinks its parent inode "
		   "is %"PRIu64".  Fix the descriptor's parent inode?", blkno,
		   (uint64_t)di->i_blkno, (uint64_t)bg->bg_parent_dinode)) {
			bg->bg_parent_dinode = di->i_blkno;
			changed = 1;
		}

	}

	if ((bg->bg_blkno != blkno) &&
	    prompt(ost, PY, PR_GROUP_BLKNO,
		   "Group descriptor read from block %"PRIu64" "
		   "claims to be located at block %"PRIu64".  Update its "
		   "recorded block location?", blkno, (uint64_t)di->i_blkno)) {
		bg->bg_blkno = blkno;
		changed = 1;
	}

	if ((bg->bg_chain != cs->cs_chain_no) &&
	    prompt(ost, PY, PR_GROUP_CHAIN,
		   "Group descriptor at block %"PRIu64" was "
		   "found in chain %u but it claims to be in chain %u. Update "
		   "the descriptor's recorded chain?", blkno, cs->cs_chain_no,
		   bg->bg_chain)) {
		bg->bg_chain = cs->cs_chain_no;
		changed = 1;
	}

	find_max_free_bits(bg, &max_free_bits);

	if ((bg->bg_free_bits_count > max_free_bits) &&
	    prompt(ost, PY, PR_GROUP_FREE_BITS,
		   "Group descriptor at block %"PRIu64" claims to "
		   "have %u free bits which is more than %u bits"
		   " indicated by the bitmap. "
		   "Drop its free bit count down to the total?", blkno,
		   bg->bg_free_bits_count, max_free_bits)) {
		bg->bg_free_bits_count = max_free_bits;
		changed = 1;
	}

	/* XXX check bg_bits vs cpg/bpc. */

	if (changed) {
		ret = ocfs2_write_group_desc(ost->ost_fs, bg->bg_blkno,
					     (char *)bg);
		if (ret) {
			com_err(whoami, ret, "while writing a group "
				"descriptor to block %"PRIu64" somewhere in "
				"chain %d in group allocator inode %"PRIu64, 
				(uint64_t)bg->bg_blkno, cs->cs_chain_no,
				(uint64_t)di->i_blkno);
			ost->ost_saw_error = 1;
		}
	}

	cs->cs_total_bits += bg->bg_bits;
	cs->cs_free_bits += bg->bg_free_bits_count;
out:
	return ret;
}

/* we do this here instead of check_chain so that we can have two relatively
 * digesitible routines instead of one enormous spaghetti-fed monster. we've
 * already had a chance to repair the chains so any remaining damage is
 * the fault of -n, etc, and can simply abort us */
static void unlink_group_desc(o2fsck_state *ost,
			      struct ocfs2_dinode *di,
			      struct ocfs2_group_desc *bg,
			      uint64_t blkno)
{
	struct ocfs2_chain_list *cl;
	struct ocfs2_chain_rec *cr;
	uint16_t i, max_count;
	struct ocfs2_group_desc *link;
	int unlink = 0;
	char *buf = NULL;
	uint64_t next_desc;
	errcode_t ret;

	cl = &di->id2.i_chain;
	max_count = ocfs2_min(cl->cl_next_free_rec,
		(__u16)ocfs2_chain_recs_per_inode(ost->ost_fs->fs_blocksize));

	ret = ocfs2_malloc_block(ost->ost_fs->fs_io, &buf);
	if (ret) {
		com_err(whoami, ret, "while allocating block buffers");
		goto out;
	}
	link = (struct ocfs2_group_desc *)buf;

	for (cr = cl->cl_recs, i = 0; i < max_count && cr->c_blkno;
	     i++, cr++) {

		if (cr->c_blkno == blkno) {
			cr->c_blkno = bg->bg_next_group;
			unlink = 1;
			break;
		}

		next_desc = cr->c_blkno;
		while(next_desc) {
			ret = ocfs2_read_group_desc(ost->ost_fs, next_desc,
						    (char *)link);
			if (ret) {
				com_err(whoami, ret, "while reading a "
					"group descriptor from block %"PRIu64,
					next_desc);
				goto out;
			}

			if (link->bg_next_group != blkno) {
				next_desc = link->bg_next_group;
				continue;
			}

			link->bg_next_group = bg->bg_next_group;
			ret = ocfs2_write_group_desc(ost->ost_fs, next_desc,
						     (char *)link);
			if (ret) {
				com_err(whoami, ret, "while writing a group "
					"descriptor to block %"PRIu64" "
					"somewhere in chain %d in group "
					"allocator inode %"PRIu64, 
					next_desc, i, (uint64_t)di->i_blkno);
				ost->ost_saw_error = 1;
				goto out;
			}
			/* we only try to remove it once.. to do more we'd
			 * have to truncate chains at the offender rather than
			 * just removing it as a link to avoid creating
			 * chains that all reference the offender's children.
			 * we'd also need to update the cr/inode counts
			 * for each bg removed.. sounds weak. */
			unlink = 1;
			break;
		}
		if (unlink)
			break;
	}

	if (!unlink)
		goto out;

	/* XXX this is kind of risky.. how can we trust next_free_rec? */
	if (cl->cl_next_free_rec == i + 1 && cr->c_blkno == 0)
		cl->cl_next_free_rec--;

	cr->c_free -= bg->bg_free_bits_count;
	cr->c_total -= bg->bg_bits;
	di->id1.bitmap1.i_used -= bg->bg_bits - bg->bg_free_bits_count;
	di->id1.bitmap1.i_total -= bg->bg_bits;
	di->i_clusters -= (bg->bg_bits / cl->cl_bpc);
	di->i_size = (uint64_t)di->i_clusters * ost->ost_fs->fs_clustersize;

	ret = ocfs2_write_inode(ost->ost_fs, di->i_blkno, (char *)di);
	if (ret) {
		/* XXX ugh, undo the bitmap math? */
		com_err(whoami, ret, "while writing inode alloc inode "
			    "%"PRIu64, (uint64_t)di->i_blkno);
		ost->ost_saw_error = 1;
		goto out;
	}

out:
	if (buf)
		ocfs2_free(&buf);
}

static void mark_group_used(o2fsck_state *ost, struct chain_state *cs,
			    uint64_t blkno, int just_desc)
{
	uint16_t clusters;

	if (just_desc)
		clusters = 1;
	else
		clusters = cs->cs_cpg;

	o2fsck_mark_clusters_allocated(ost, 
				ocfs2_blocks_to_clusters(ost->ost_fs, blkno),
				clusters);
}

/*
 * Due to a glitch in old mkfs, cl->cl_cpg for the GLOBAL BITMAP could be
 * less than the max possible for volumes having just one cluster
 * group. Fix.
 */
static errcode_t maybe_fix_clusters_per_group(o2fsck_state *ost,
					      struct ocfs2_dinode *di)
{
	struct ocfs2_chain_list *cl;
	struct ocfs2_group_desc *gd;
	uint16_t new_cl_cpg = 0;
	uint64_t blkno;
	char *buf = NULL;
	int ret = 0;

	cl = &(di->id2.i_chain);
	if (cl->cl_next_free_rec > 1)
		goto out;

	ret = ocfs2_malloc_block(ost->ost_fs->fs_io, &buf);
	if (ret) {
		com_err(whoami, ret, "while allocating block buffers "
			"to fix cl_cpg");
		goto out;
	}
	gd = (struct ocfs2_group_desc *) buf;

	blkno = cl->cl_recs[0].c_blkno;

	ret = ocfs2_read_group_desc(ost->ost_fs, blkno, (char *)gd);
	if (ret) {
		com_err(whoami, ret, "while reading group descriptor "
			"at block %"PRIu64" to fix cl_cpg", blkno);
		goto out;
	}

	new_cl_cpg = 8 * gd->bg_size;
	if (cl->cl_cpg == new_cl_cpg)
		goto out;

	if (prompt(ost, PY, PR_CHAIN_CPG,
		   "Global bitmap at block %"PRIu64" has clusters per group "
		   "set to %u instead of %u. Fix?", (uint64_t)di->i_blkno,
		   cl->cl_cpg, new_cl_cpg)) {
		cl->cl_cpg = new_cl_cpg;
		ret = ocfs2_write_inode(ost->ost_fs, di->i_blkno, (char *)di);
		if (ret) {
			com_err(whoami, ret, "while writing inode alloc inode "
				"%"PRIu64" to fix cl_cpg",
				(uint64_t)di->i_blkno);
			ost->ost_saw_error = 1;
			ret = 0;
		}
	}

out:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

/* this takes a slightly ridiculous number of arguments :/ */
static errcode_t check_chain(o2fsck_state *ost,
			     struct ocfs2_dinode *di,
			     struct chain_state *cs,
			     struct ocfs2_chain_rec *chain,
			     char *buf1,
			     char *buf2,
			     char *pre_cache_buf,
			     int *chain_changed,
			     ocfs2_bitmap *allowed,
			     ocfs2_bitmap *forbidden)
{
	struct ocfs2_group_desc *bg1 = (struct ocfs2_group_desc *)buf1;
	struct ocfs2_group_desc *bg2 = (struct ocfs2_group_desc *)buf2;
	uint64_t blkno;
	errcode_t ret = 0;
	int depth = 0, clear_ref = 0;
	int blocks_per_group = ocfs2_clusters_to_blocks(ost->ost_fs,
							cs->cs_cpg);

	verbosef("free %u total %u blkno %"PRIu64"\n", chain->c_free,
		 chain->c_total, (uint64_t)chain->c_blkno);

	while(1) {
		/* fetch the next reference */
		if (depth == 0)
			blkno = chain->c_blkno;
		else {
			/* we only mark a group as used if it wasn't
			 * contentious.  if we weren't supposed to find it we
			 * mark it for a future pass to consider.  we do
			 * this here just as we're about to take the reference
			 * to the next group, implying that we've just
			 * decided that bg1 is valid. */
			blkno = bg1->bg_blkno;
			if (allowed) {
				int was_set;
				ocfs2_bitmap_test(allowed, blkno, &was_set);
				if (was_set) {
					o2fsck_bitmap_clear(allowed, blkno,
							    &was_set);
					mark_group_used(ost, cs, bg1->bg_blkno,
							allowed != NULL);
				} else if (forbidden)
					o2fsck_bitmap_set(forbidden, blkno,
							  &was_set);
			} else
				mark_group_used(ost, cs, bg1->bg_blkno,
						allowed != NULL);
			blkno = bg1->bg_next_group;
		}

		/* we're done */
		if (blkno == 0)
			break;

		/* is it even feasible? */
		if (ocfs2_block_out_of_range(ost->ost_fs, blkno)) {
			if (prompt(ost, PY, PR_CHAIN_LINK_RANGE,
				   "Chain %d in allocator at inode "
				   "%"PRIu64" contains a reference at depth "
				   "%d to block %"PRIu64" which is out "
				   "of range. Truncate this chain?",
				   cs->cs_chain_no, (uint64_t)di->i_blkno,
				   depth, blkno))  {

				clear_ref = 1;
				break;
			}
			/* this will just result in a bad blkno from
			 * the read below.. */
		}

		/*
		 * Pre-cache the entire group.  Don't care about failure.
		 * If it works, the following ocfs2_read_group_desc() will
		 * get the block out of the cache.
		 */
		if (pre_cache_buf)
			ocfs2_read_blocks(ost->ost_fs, blkno,
					  blocks_per_group, pre_cache_buf);

		ret = ocfs2_read_group_desc(ost->ost_fs, blkno, (char *)bg2);
		if (ret == OCFS2_ET_BAD_GROUP_DESC_MAGIC) {
			if (prompt(ost, PY, PR_CHAIN_LINK_MAGIC,
				   "Chain %d in allocator at inode "
				   "%"PRIu64" contains a reference at depth "
				   "%d to block %"PRIu64" which doesn't have "
				   "a valid checksum.  Truncate this chain?",
				   cs->cs_chain_no, (uint64_t)di->i_blkno,
				   depth, blkno))  {

				clear_ref = 1;
				break;
			}
			
			/* we're not interested in following a broken desc */
			ret = 0;
			break;
		}
		if (ret) {
			com_err(whoami, ret, "while reading a group "
				"descriptor from block %"PRIu64" as pointed "
				"to by chain %d in allocator at inode "
				"%"PRIu64" at depth %d", blkno, 
				cs->cs_chain_no, (uint64_t)di->i_blkno, depth);
			goto out;
		}

		if (bg2->bg_generation != ost->ost_fs_generation &&
		    prompt(ost, PY, PR_CHAIN_LINK_GEN,
			   "Group descriptor at block %"PRIu64" "
			   "has a generation of %"PRIx32" which doesn't match "
			   "the volume's generation of %"PRIx32".  Unlink "
			   "this group descriptor?", blkno, bg2->bg_generation,
			   ost->ost_fs_generation)) {

			clear_ref = 1;
			break;
		}

		ret = repair_group_desc(ost, di, cs, bg2, blkno, &clear_ref);
		if (ret)
			goto out;

		/* we found a duplicate chain, so we need to clear them from
		 * current chain.
		 *
		 * Please note that all the groups below this group will also
		 * be removed from this chain because this is the mechanism
		 * of removing slots in tunefs.ocfs2.
		 */
		if (clear_ref)
			break;

		/* the loop will now start by reading bg1->next_group */
		memcpy(buf1, buf2, ost->ost_fs->fs_blocksize);
		depth++;
	}

	/* we hit the premature end of a chain.. clear the last
	 * ref we were working from */
	if (clear_ref) {
		if (depth == 0) {
			chain->c_blkno = 0;
			*chain_changed = 1;
		} else {
			bg1->bg_next_group = 0;
			ret = ocfs2_write_group_desc(ost->ost_fs,
					             bg1->bg_blkno,
						     (char *)bg1);
			if (ret) {
				com_err(whoami, ret, "while writing a group "
					"descriptor at depth %d in chain %d "
					"in group allocator inode %"PRIu64" "
					"to block %"PRIu64, depth,
					cs->cs_chain_no, (uint64_t)di->i_blkno,
					(uint64_t)bg1->bg_blkno);
				ost->ost_saw_error = 1;
			}
		}
	}

	if (cs->cs_total_bits != chain->c_total ||
	    cs->cs_free_bits != chain->c_free) {
		if (prompt(ost, PY, PR_CHAIN_BITS,
			   "Chain %d in allocator inode %"PRIu64" "
			   "has %u bits marked free out of %d total bits "
			   "but the block groups in the chain have %u "
			   "free out of %u total.  Fix this by updating "
			   "the chain record?", cs->cs_chain_no,
			   (uint64_t)di->i_blkno,
			   chain->c_free, chain->c_total, cs->cs_free_bits,
			   cs->cs_total_bits)) {
			chain->c_total = cs->cs_total_bits;
			chain->c_free = cs->cs_free_bits;
			*chain_changed = 1;
		}
	}

out:
	return ret;
}

/* If this returns 0 then the inode allocator had better be amenable to
 * iteration. */
static errcode_t verify_chain_alloc(o2fsck_state *ost,
				    struct ocfs2_dinode *di,
				    char *buf1, char *buf2,
				    char *pre_cache_buf,
				    ocfs2_bitmap *allowed,
				    ocfs2_bitmap *forbidden)
{
	struct chain_state cs = {0, };
	struct ocfs2_chain_list *cl;
	int i, max_count;
	struct ocfs2_chain_rec *cr;
	uint32_t free = 0, total = 0;
	int changed = 0, trust_next_free = 1;
	errcode_t ret = 0;
	uint64_t chain_bytes;

	if (memcmp(di->i_signature, OCFS2_INODE_SIGNATURE,
		   strlen(OCFS2_INODE_SIGNATURE))) {
		printf("Allocator inode %"PRIu64" doesn't have an inode "
		       "signature.  fsck won't repair this.\n",
		       (uint64_t)di->i_blkno);
		ret = OCFS2_ET_BAD_INODE_MAGIC;
		goto out;
	}

	if (!(di->i_flags & OCFS2_VALID_FL)) {
		printf("Allocator inode %"PRIu64" is not active.  fsck won't "
		       "repair this.\n", (uint64_t)di->i_blkno);
		ret = OCFS2_ET_INODE_NOT_VALID;
		goto out;
	}

	if (!(di->i_flags & OCFS2_CHAIN_FL)) {
		printf("Allocator inode %"PRIu64" doesn't have the CHAIN_FL "
		       "flag set.  fsck won't repair this.\n",
		       (uint64_t)di->i_blkno);
		/* not _entirely_ accurate, but pretty close. */
		ret = OCFS2_ET_INODE_NOT_VALID;
		goto out;
	}

	/* XXX should we check suballoc_node? */

	cl = &di->id2.i_chain;

	verbosef("cl cpg %u bpc %u count %u next %u\n", 
		 cl->cl_cpg, cl->cl_bpc, cl->cl_count, cl->cl_next_free_rec);

	max_count = ocfs2_chain_recs_per_inode(ost->ost_fs->fs_blocksize);

	/* first, no rec should have a totally invalid blkno */
	for (i = 0; i < max_count; i++) {
		cr = &cl->cl_recs[i];

		if (cr->c_blkno != 0&&
		    ocfs2_block_out_of_range(ost->ost_fs, cr->c_blkno) &&
		    prompt(ost, PY, PR_CHAIN_HEAD_LINK_RANGE,
			   "Chain %d in allocator inode %"PRIu64" "
			   "contains an initial block reference to %"PRIu64" "
			   "which is out of range.  Clear this reference?",
			   i, (uint64_t)di->i_blkno, (uint64_t)cr->c_blkno)) {

			cr->c_blkno = 0;
			changed = 1;
		}
	}

	/* make sure cl_count is clamped to the size of the inode */
	if (cl->cl_count > max_count &&
	    prompt(ost, PY, PR_CHAIN_COUNT,
		   "Allocator inode %"PRIu64" claims to have %u "
		   "chains, but the maximum is %u. Fix the inode's count?",
		   (uint64_t)di->i_blkno, cl->cl_count, max_count)) {
		cl->cl_count = max_count;
		changed = 1;
	}

	if (max_count > cl->cl_count)
		max_count = cl->cl_count;

	if (cl->cl_next_free_rec > max_count) {
		if (prompt(ost, PY, PR_CHAIN_NEXT_FREE,
			   "Allocator inode %"PRIu64" claims %u "
			   "as the next free chain record, but fsck believes "
			   "the largest valid value is %u.  Clamp the next "
			   "record value?", (uint64_t)di->i_blkno,
			   cl->cl_next_free_rec,
			   max_count)) {
			cl->cl_next_free_rec = cl->cl_count;
			changed = 1;
		} else {
			trust_next_free = 0;
		}
	}

	/* iterate over all chains if we don't trust next_free_rec to mark
	 * the end of used chains */
	if (trust_next_free)
		max_count = cl->cl_next_free_rec;

	/*
	 * We walk the chains backwards for caching reasons.  Basically,
	 * at the end the last blocks we read will be the most recently
	 * used in the cache.  We want that to be the first chains,
	 * especially for the inode scan, which will read forwards.
	 */
	for (i = max_count - 1; i >= 0; i--) {
		cr = &cl->cl_recs[i];

		/* reset for each run */
		cs = (struct chain_state) {
			.cs_chain_no = i,
			.cs_cpg = cl->cl_cpg,
		};
		ret = check_chain(ost, di, &cs, cr, buf1, buf2, pre_cache_buf,
				  &changed, allowed, forbidden);
		/* XXX what?  not checking ret? */

		if (cr->c_blkno != 0) {
			free += cs.cs_free_bits;
			total += cs.cs_total_bits;
			continue;
		}

		if (prompt(ost, PY, PR_CHAIN_EMPTY,
			   "Chain %d in allocator inode %"PRIu64" "
			   "is empty.  Remove it from the chain record "
			   "array in the inode and shift further chains "
			   "into its place?", cs.cs_chain_no,
			   (uint64_t)di->i_blkno)) {

			if (!trust_next_free) {
				printf("Can't remove the chain becuase "
				       "next_free_rec hasn't been fixed\n");
				continue;
			}

			/* when we move a chain to a different rec we have
			 * to update bg_chain in all the descs in the chain.
			 * we copy the last chain into the missing spot
			 * instead of shifting everyone over a spot 
			 * to minimize the number of chains we have to
			 * update.  we then reset i so that we can go
			 * over that chain and fix bg_chain */
			if (i < (cl->cl_next_free_rec - 1)) {
				*cr = cl->cl_recs[cl->cl_next_free_rec - 1];
				memset(&cl->cl_recs[cl->cl_next_free_rec - 1],
					0, sizeof(struct ocfs2_chain_rec));
				i++;
			}

			cl->cl_next_free_rec--;
			max_count--;
			changed = 1;
			continue;
		}

	}

	for (i = cl->cl_next_free_rec; i < cl->cl_count; i++)
		memset(&cl->cl_recs[i], 0, sizeof(struct ocfs2_chain_rec));

	if (di->id1.bitmap1.i_total != total || 
	    (di->id1.bitmap1.i_used != total - free)) {
		if (prompt(ost, PY, PR_CHAIN_GROUP_BITS,
			   "Allocator inode %"PRIu64" has %u bits "
			   "marked used out of %d total bits but the chains "
			   "have %u used out of %u total.  Fix this by "
			   "updating the inode counts?", (uint64_t)di->i_blkno,
			   di->id1.bitmap1.i_used, di->id1.bitmap1.i_total,
			   total - free, total)) {
			   di->id1.bitmap1.i_used = total - free;
			   di->id1.bitmap1.i_total = total;

			   changed = 1;
		}
	}

	total /= cl->cl_bpc;

	if (di->i_clusters != total &&
	    prompt(ost, PY, PR_CHAIN_I_CLUSTERS,
		   "Allocator inode %"PRIu64" has %"PRIu32" clusters "
		   "represented in its allocator chains but has an "
		   "i_clusters value of %"PRIu32". Fix this by updating "
		   "i_clusters?", (uint64_t)di->i_blkno,
		   total, di->i_clusters)) {
		di->i_clusters = total;
		changed = 1;
	}

	chain_bytes = (uint64_t)total * ost->ost_fs->fs_clustersize;
	if (di->i_size != chain_bytes &&
	    prompt(ost, PY, PR_CHAIN_I_SIZE,
		   "Allocator inode %"PRIu64" has %"PRIu32" clusters "
		   "represented in its allocator chain which accounts for "
		   "%"PRIu64" total bytes, but its i_size is %"PRIu64". "
		   "Fix this by updating i_size?", (uint64_t)di->i_blkno,
		   di->id1.bitmap1.i_total, chain_bytes,
		   (uint64_t)di->i_size)) {
		di->i_size = chain_bytes;
		changed = 1;
	}

	if (changed) {
		ret = ocfs2_write_inode(ost->ost_fs, di->i_blkno, (char *)di);
		if (ret) {
			com_err(whoami, ret, "while writing inode alloc inode "
				    "%"PRIu64, (uint64_t)di->i_blkno);
			ost->ost_saw_error = 1;
			ret = 0;
		}
	}

out:
	return ret;
}

/* we know that the bitmap descs are at predictable places in the fs.  we
 * walk these locations and make sure there are valid group descs
 * there.  We fill a bitmap with the valid ones so that when we later walk
 * the chains we can restrict it to the set of expected blocks and also
 * be sure to add blocks that aren't linked in */
static errcode_t verify_bitmap_descs(o2fsck_state *ost,
				     struct ocfs2_dinode *di,
				     char *buf1, char *buf2)
{
	struct ocfs2_cluster_group_sizes cgs;
	uint16_t max_recs;
	uint16_t bits, chain;
	uint64_t blkno;
	struct ocfs2_group_desc *bg = (struct ocfs2_group_desc *)buf1;
	errcode_t ret;
	struct chain_state cs;
	struct ocfs2_chain_rec *rec;
	ocfs2_bitmap *allowed = NULL, *forbidden = NULL;
	int was_set, i;

	/* XXX ugh, only used by mark_ */
	cs.cs_cpg = di->id2.i_chain.cl_cpg;

	ret = ocfs2_block_bitmap_new(ost->ost_fs, "allowed group descriptors",
				     &allowed);
	if (ret) {
		com_err(whoami, ret, "while allocating allowed bitmap descs "
			"bitmap");
		goto out;
	}
	ret = ocfs2_block_bitmap_new(ost->ost_fs, "forbidden group "
				     "descriptors", &forbidden);
	if (ret) {
		com_err(whoami, ret, "while allocating forbidden descs "
			"bitmap");
		goto out;
	}
	
	ocfs2_calc_cluster_groups(ost->ost_fs->fs_clusters,
				  ost->ost_fs->fs_blocksize, &cgs);

	max_recs = ocfs2_chain_recs_per_inode(ost->ost_fs->fs_blocksize);

	for (i = 0, blkno = ost->ost_fs->fs_first_cg_blkno;
	     i < cgs.cgs_cluster_groups; 
	     i++, blkno = i * ocfs2_clusters_to_blocks(ost->ost_fs,
						       cgs.cgs_cpg)) {
		o2fsck_bitmap_set(allowed, blkno, NULL);
	}

	ret = verify_chain_alloc(ost, di, buf1, buf2, NULL, allowed, forbidden);
	if (ret) {
		com_err(whoami, ret, "while looking up chain allocator inode "
			"%"PRIu64, (uint64_t)di->i_blkno);
		goto out;
	}

	/* remove descs that we found in the chain that we didn't expect */
	for (blkno = ost->ost_fs->fs_first_cg_blkno;
	     !ocfs2_bitmap_find_next_set(forbidden, blkno, &blkno);
	     blkno++) {
		if (!prompt(ost, PY, PR_GROUP_UNEXPECTED_DESC,
			    "Block %"PRIu64" is a group "
			    "descriptor in the bitmap chain allocator but it "
			    "isn't at one of the pre-determined locations and "
			    "so shouldn't be in the allocator.  Remove it "
			    "from the chain?", blkno)) {

			mark_group_used(ost, &cs, blkno, 1);
			continue;
		}

		ret = ocfs2_read_group_desc(ost->ost_fs, blkno, (char *)bg);
		if (ret) {
			com_err(whoami, ret, "while reading a cluster bitmap "
				"group descriptor from block %"PRIu64,
				blkno);
			continue;
		}

		unlink_group_desc(ost, di, bg, blkno);
	}

	/* find the blocks that we think should have been in the chains
	 * but which weren't found */
	for (i = 0, blkno = ost->ost_fs->fs_first_cg_blkno;
	     i < cgs.cgs_cluster_groups; 
	     i++, blkno = i * ocfs2_clusters_to_blocks(ost->ost_fs,
						       cgs.cgs_cpg)) {

		if (ocfs2_bitmap_test(allowed, blkno, &was_set))
			continue;
		if (!was_set)
			continue;

		if (!prompt(ost, PY, PR_GROUP_EXPECTED_DESC,
			    "Block %"PRIu64" should be a group "
			    "descriptor for the bitmap chain allocator but it "
			    "wasn't found in any chains.  Reinitialize it as "
			    "a group desc and link it into the bitmap "
			    "allocator?", blkno))
			continue;

		/* some input that init_desc might need */
		if (i == cgs.cgs_cluster_groups - 1)
			bits = cgs.cgs_tail_group_bits;
		else
			bits = cgs.cgs_cpg;
		chain = i % max_recs;

		/* we've been asked to link in this desc specifically. we're
		 * using the predictability of the group descs to rebuild
		 * its values.. we only preserve the bitmap if the signature
		 * and generation match this volume */
		ret = ocfs2_read_group_desc(ost->ost_fs, blkno, (char *)bg);
		if (ret == OCFS2_ET_BAD_GROUP_DESC_MAGIC ||
		    bg->bg_generation != ost->ost_fs_generation) {
			memset(bg, 0, ost->ost_fs->fs_blocksize);
			ocfs2_init_group_desc(ost->ost_fs, bg, blkno,
					      ost->ost_fs_generation,
					      di->i_blkno,
					      bits, chain, 0);
			ret = 0;
		}
		if (ret) {
			com_err(whoami, ret, "while reading a cluster bitmap "
				"group descriptor from block %"PRIu64,
				blkno);
			continue;
		}

		/* first some easy fields */
		bg->bg_size = ocfs2_group_bitmap_size(
			ost->ost_fs->fs_blocksize, 0,
			OCFS2_RAW_SB(ost->ost_fs->fs_super)->s_feature_incompat);
		bg->bg_bits = bits;
		bg->bg_parent_dinode = di->i_blkno;
		bg->bg_blkno = blkno;
		ocfs2_set_bit(0, bg->bg_bitmap);
		bg->bg_free_bits_count = bg->bg_bits - 
					 o2fsck_bitcount(bg->bg_bitmap,
							 (bg->bg_bits + 7)/ 8);

		/* we have to be kind of careful with the chain */
		chain = ocfs2_min(chain,
				  di->id2.i_chain.cl_next_free_rec);
		chain = ocfs2_min(chain, max_recs);
		bg->bg_chain = chain;

		/* now really link it in */
		rec = &di->id2.i_chain.cl_recs[bg->bg_chain];
		bg->bg_next_group = rec->c_blkno;

		ret = ocfs2_write_group_desc(ost->ost_fs, blkno, (char *)bg);
		if (ret) {
			com_err(whoami, ret, "while writing a cluster group "
				"descriptor at block %"PRIu64, blkno);
			ost->ost_saw_error = 1;
			continue;
		}

		/* and update the calling inode */
		rec->c_free += bg->bg_free_bits_count;
		rec->c_total += bg->bg_bits;
		rec->c_blkno = blkno;

		/* ugh */
		if (di->id2.i_chain.cl_next_free_rec == bg->bg_chain &&
		    di->id2.i_chain.cl_next_free_rec < max_recs)
			di->id2.i_chain.cl_next_free_rec++;

		di->id1.bitmap1.i_used += bg->bg_bits - bg->bg_free_bits_count;
		di->id1.bitmap1.i_total += bg->bg_bits;
		di->i_clusters += (bg->bg_bits / di->id2.i_chain.cl_bpc);
		di->i_size = (uint64_t)di->i_clusters *
			     ost->ost_fs->fs_clustersize;

		ret = ocfs2_write_inode(ost->ost_fs, di->i_blkno, (char *)di);
		if (ret) {
			com_err(whoami, ret, "while writing inode alloc inode "
				    "%"PRIu64, (uint64_t)di->i_blkno);
			ost->ost_saw_error = 1;
			goto out;
		}

		mark_group_used(ost, &cs, bg->bg_blkno, 1);
	}

out:
	if (allowed)
		ocfs2_bitmap_free(allowed);
	if (forbidden)
		ocfs2_bitmap_free(forbidden);
	return ret;
}

/* this returns an error if it didn't leave the allocators in a state that
 * the iterators will be able to work with.  There is probably some room
 * for more resiliance here. */
errcode_t o2fsck_pass0(o2fsck_state *ost)
{
	errcode_t ret;
	uint64_t blkno;
	uint32_t pre_repair_clusters;
	char *blocks = NULL;
	char *pre_cache_buf = NULL;
	struct ocfs2_dinode *di = NULL;
	ocfs2_filesys *fs = ost->ost_fs;
	ocfs2_cached_inode **ci;
	int max_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;
	int i, type, bitmap_retried = 0;

	printf("Pass 0a: Checking cluster allocation chains\n");

	/*
	 * The I/O buffer is 3 blocks. We apportion our I/O buffer
	 * thusly:
	 *
	 * blocks[0] is the allocator inode we're working on.
	 * blocks[1] & blocks[2] are used to hold group descriptors
	 *     in functions below this one.
	 */
	ret = ocfs2_malloc_blocks(fs->fs_io, 3, &blocks);
	if (ret) {
		com_err(whoami, ret, "while allocating block buffers");
		goto out;
	}
	di = (struct ocfs2_dinode *)blocks;

	/*
	 * We also allocate a pre-cache buffer of 4MB for reading entire
	 * suballocator groups.  Some blocksizes have smaller groups, but
	 * none have larger (see
	 * libocfs2/alloc.c:ocfs2_clusters_per_group()).  This allows
	 * us to pre-fill the I/O cache; we're already reading the group
	 * descriptor, so slurping the whole thing shouldn't hurt.
	 *
	 * If this allocation fails, we just ignore it.  It's a cache.
	 */
	o2fsck_reset_blocks_cached();
	if (o2fsck_worth_caching(1)) {
		ret = ocfs2_malloc_blocks(fs->fs_io,
					  ocfs2_blocks_in_bytes(fs, 4 * 1024 * 1024),
					  &pre_cache_buf);
		if (ret)
			verbosef("Unable to allocate group pre-cache "
				 "buffer, %s\n",
				 "ignoring");
	}

	ret = ocfs2_malloc0(max_slots * sizeof(ocfs2_cached_inode *), 
			    &ost->ost_inode_allocs);
	if (ret) {
		com_err(whoami, ret, "while cached inodes for each node");
		goto out;
	}

	ret = ocfs2_lookup_system_inode(fs, GLOBAL_BITMAP_SYSTEM_INODE,
					0, &blkno);
	if (ret) {
		com_err(whoami, ret, "while looking up the global bitmap "
			"inode");
		goto out;
	}

	ret = ocfs2_read_inode(ost->ost_fs, blkno, (char *)di);
	if (ret) {
		com_err(whoami, ret, "reading inode alloc inode "
			"%"PRIu64" for verification", blkno);
		goto out;
	}

	verbosef("found inode alloc %"PRIu64" at block %"PRIu64"\n",
		 (uint64_t)di->i_blkno, blkno);

	ret = maybe_fix_clusters_per_group(ost, di);
	if (ret)
		goto out;

	/*
	 * during resize, we may update the global bitmap but fails to
	 * to update i_clusters in superblock, so ask the user which one
	 * to use before checking.
	 */
	if (fs->fs_super->i_clusters != di->i_clusters) {
		if (prompt(ost, PY, PR_SUPERBLOCK_CLUSTERS,
			   "Superblock has clusters set to %u instead of %u "
			   "recorded in global_bitmap, it may be caused by an "
			   "unsuccessful resize. Trust global_bitmap?",
			   fs->fs_super->i_clusters, di->i_clusters)) {
			ost->ost_num_clusters = di->i_clusters;
			fs->fs_clusters = di->i_clusters;
			fs->fs_blocks = ocfs2_clusters_to_blocks(fs,
							 fs->fs_clusters);
			ret = o2fsck_state_reinit(fs, ost);
			if (ret) {
				com_err(whoami, ret, "while reinit "
					"o2fsck_state.");
				goto out;
			}
		}
	}

retry_bitmap:
	pre_repair_clusters = di->i_clusters;
	ret = verify_bitmap_descs(ost, di, blocks + ost->ost_fs->fs_blocksize,
				  blocks + (ost->ost_fs->fs_blocksize * 2));

	if (ret)
		goto out;

	if (pre_repair_clusters != di->i_clusters) {
		if (prompt(ost, PY, PR_FIXED_CHAIN_CLUSTERS,
			   "Repair of global_bitmap changed the filesystem "
			   "from %u clusters to %u clusters.  Trust "
			   "global_bitmap?",
			   pre_repair_clusters, di->i_clusters)) {
			ost->ost_num_clusters = di->i_clusters;
			fs->fs_clusters = di->i_clusters;
			fs->fs_blocks = ocfs2_clusters_to_blocks(fs,
							 fs->fs_clusters);
			ret = o2fsck_state_reinit(fs, ost);
			if (ret) {
				com_err(whoami, ret, "while reinit "
					"o2fsck_state.");
				goto out;
			}

			/*
			 * The reinit clears the bits found during the
			 * scan of the global bitmap.  We need to go over
			 * them again.  They really should come out clean
			 * this time.  If they don't, we probably have
			 * a serious problem.
			 *
			 * In an interactive run, the user can keep
			 * retrying and abort when they give up.  In a
			 * non-interactive mode, we can't loop forever.
			 */
			if (ost->ost_ask || !bitmap_retried) {
				bitmap_retried = 1;
				verbosef("Restarting global_bitmap %s\n",
					 "scan");
				goto retry_bitmap;
			}
		}
	}

	printf("Pass 0b: Checking inode allocation chains\n");

	/* first the global inode alloc and then each of the node's
	 * inode allocators */
	type = GLOBAL_INODE_ALLOC_SYSTEM_INODE;
	i = -1;

	for ( ; i < max_slots; i++, type = INODE_ALLOC_SYSTEM_INODE) {
		ret = ocfs2_lookup_system_inode(fs, type, i, &blkno);
		if (ret) {
			com_err(whoami, ret, "while looking up the inode "
				"allocator type %d for node %d\n", type, i);
			goto out;
		}

		ret = ocfs2_read_inode(ost->ost_fs, blkno, (char *)di);
		if (ret) {
			com_err(whoami, ret, "reading inode alloc inode "
				"%"PRIu64" for verification", blkno);
			goto out;
		}

		verbosef("found inode alloc %"PRIu64" at block %"PRIu64"\n",
			 (uint64_t)di->i_blkno, blkno);

		ret = verify_chain_alloc(ost, di,
					 blocks + ost->ost_fs->fs_blocksize,
					 blocks + 
					 (ost->ost_fs->fs_blocksize * 2), 
					 pre_cache_buf, NULL, NULL);

		/* XXX maybe helped by the alternate super block */
		if (ret)
			goto out;

		if (i == -1)
			ci = &ost->ost_global_inode_alloc;
		else
			ci = &ost->ost_inode_allocs[i];

		ret = ocfs2_read_cached_inode(ost->ost_fs, blkno, ci);
		if (ret) {
			com_err(whoami, ret, "while reading node %d's inode "
				"allocator inode %"PRIu64, i, blkno);	
			goto out;
		}

		ret = ocfs2_load_chain_allocator(ost->ost_fs, *ci);
		if (ret) {
			com_err(whoami, ret, "while loading inode %"PRIu64" "
				"as a chain allocator", blkno);
			ocfs2_free_cached_inode(ost->ost_fs, *ci);
			*ci = NULL;
			goto out;
		}
	}

	printf("Pass 0c: Checking extent block allocation chains\n");

	for (i = 0; i < max_slots; i++) {
		ret = ocfs2_lookup_system_inode(fs, EXTENT_ALLOC_SYSTEM_INODE,
						i, &blkno);
		if (ret) {
			com_err(whoami, ret, "while looking up the extent "
				"allocator type %d for node %d\n", type, i);
			goto out;
		}

		ret = ocfs2_read_inode(ost->ost_fs, blkno, (char *)di);
		if (ret) {
			com_err(whoami, ret, "reading inode alloc inode "
				"%"PRIu64" for verification", blkno);
			goto out;
		}

		verbosef("found extent alloc %"PRIu64" at block %"PRIu64"\n",
			 (uint64_t)di->i_blkno, blkno);

		ret = verify_chain_alloc(ost, di,
					 blocks + ost->ost_fs->fs_blocksize,
					 blocks + 
					 (ost->ost_fs->fs_blocksize * 2), 
					 pre_cache_buf, NULL, NULL);

		/* XXX maybe helped by the alternate super block */
		if (ret)
			goto out;
	}

out:
	if (pre_cache_buf)
		ocfs2_free(&pre_cache_buf);
	if (blocks)
		ocfs2_free(&blocks);
	if (ret)
		o2fsck_free_inode_allocs(ost);

	return ret;
}
