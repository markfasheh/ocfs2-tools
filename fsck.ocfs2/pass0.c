/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
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
 * Pass 0 verifies that the inode suballocators can be iterated over by later
 * passes without risk of running into corruption.  This is so the passes can
 * build up state without having to worry about tearing it down half way
 * through to clean up the suballocators.  For now fsck treats failure to find
 * and verify the suballocator inodes themselves as fatal.  It will only clean
 * up the data they point to.
 *
 * pass0 updates group descriptor chains on disk.
 *
 * XXX
 * 	track used blocks that iteration won't see?
 * 	verify more inode fields?
 * 	use prompt to mark soft errors
 * 	make sure blocks don't overlap as part of cluster tracking
 */

#include <string.h>
#include <inttypes.h>
#include <time.h>

#include "ocfs2.h"

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
};

/* returns 0 if the group desc is valid */
static int check_group_desc(o2fsck_state *ost, ocfs2_dinode *di,
			    struct chain_state *cs, ocfs2_group_desc *bg,
			    uint64_t blkno)
{
	int changed = 0;

	verbosef("checking desc at %"PRIu64"; blkno %"PRIu64" size %u bits %u "
		 "free_bits %u chain %u generation %u\n", blkno, bg->bg_blkno,
		 bg->bg_size, bg->bg_bits, bg->bg_free_bits_count, 
		 bg->bg_chain, bg->bg_generation);

	/* Once we think it's a valid group desc we aggressively tie it
	 * into the inode that pointed to it for fear of losing any
	 * descriptors. */
	if (memcmp(bg->bg_signature, OCFS2_GROUP_DESC_SIGNATURE,
		   strlen(OCFS2_GROUP_DESC_SIGNATURE))) {
		printf("Group descriptor at block %"PRIu64" has an invalid "
			"signature.\n", blkno);
		return OCFS2_ET_BAD_GROUP_DESC_MAGIC;
	}

	/* XXX maybe for advanced pain we could check to see if these 
	 * kinds of descs have valid generations for the inodes they
	 * reference */
	if ((bg->bg_parent_dinode != di->i_blkno) &&
	    prompt(ost, PY, "Group descriptor at block %"PRIu64" is "
		   "referenced by inode %"PRIu64" but thinks its parent inode "
		   "is %"PRIu64".  Fix the descriptor's parent inode?", blkno,
		   di->i_blkno, bg->bg_parent_dinode)) {
		bg->bg_parent_dinode = di->i_blkno;
		changed = 1;
	}

	if ((bg->bg_generation != di->i_generation) &&
	    prompt(ost, PY, "Group descriptor at block %"PRIu64" is "
		   "referenced by inode %"PRIu64" who has a generation of "
		   "%u, but the descriptor has a generation of %u.  Update "
		   "the descriptor's generation?", blkno, di->i_blkno,
		   di->i_generation, bg->bg_generation)) {
		bg->bg_generation = di->i_generation;
		changed = 1;
	}

	if ((bg->bg_blkno != blkno) &&
	    prompt(ost, PY, "Group descriptor read from block %"PRIu64" "
		   "claims to be located at block %"PRIu64".  Update its "
		   "recorded block location?", blkno, di->i_blkno)) {
		bg->bg_blkno = blkno;
		changed = 1;
	}

	if ((bg->bg_chain != cs->cs_chain_no) &&
	    prompt(ost, PY, "Group descriptor at block %"PRIu64" was "
		   "found in chain %u but it claims to be in chain %u. Update "
		   "the descriptor's recorded chain?", blkno, cs->cs_chain_no,
		   bg->bg_chain)) {
		bg->bg_chain = cs->cs_chain_no;
		changed = 1;
	}

	if ((bg->bg_free_bits_count > bg->bg_bits) &&
	    prompt(ost, PY, "Group descriptor at block %"PRIu64" claims to "
		   "have %u free bits which is more than its %u total bits. "
		   "Drop its free bit count down to the total?", blkno,
		   bg->bg_free_bits_count, bg->bg_bits)) {
		bg->bg_free_bits_count = bg->bg_bits;
		changed = 1;
	}

	/* XXX check bg_bits vs cpg/bpc. */

	if (changed) {
		errcode_t ret;
		ret = ocfs2_write_group_desc(ost->ost_fs, bg->bg_blkno,
					     (char *)bg);
		if (ret) {
			com_err(whoami, ret, "while writing a group "
				"descriptor to block %"PRIu64" somewhere in "
				"chain %d in group allocator inode %"PRIu64, 
				bg->bg_blkno, cs->cs_chain_no, di->i_blkno);
			ost->ost_write_error = 1;
		}
	}

	cs->cs_total_bits += bg->bg_bits;
	cs->cs_free_bits += bg->bg_free_bits_count;

	return 0;
}

/* returns non-zero if the chain_rec was updated */
static int check_chain(o2fsck_state *ost, ocfs2_dinode *di,
		       struct chain_state *cs, ocfs2_chain_rec *chain,
		       char *buf1, char *buf2)
{
	ocfs2_group_desc *bg1 = (ocfs2_group_desc *)buf1;
	ocfs2_group_desc *bg2 = (ocfs2_group_desc *)buf2;
	uint64_t blkno = chain->c_blkno;
	errcode_t ret;
	int rc, changed = 0;

	verbosef("free %u total %u blkno %"PRIu64"\n", chain->c_free,
		 chain->c_total, chain->c_blkno);

	if (chain->c_blkno == 0)
		goto out;

	if (ocfs2_block_out_of_range(ost->ost_fs, blkno)) {
		if (prompt(ost, PY, "Chain record %d in group allocator inode "
			    "%"PRIu64" points to block %"PRIu64" which is out "
 			    "of range.  fsck can't continue without deleting "
			    "this chain.  Delete it?", cs->cs_chain_no,
			    di->i_blkno, blkno))  {
			chain->c_blkno = 0;
			changed = 1;
		}

		goto out;
	}

	ret = ocfs2_read_group_desc(ost->ost_fs, blkno, buf1);
	if (ret) {
		com_err(whoami, ret, "while reading a group descriptor from "
			"block %"PRIu64" as pointed to by chain record %d in "
			"group allocator inode %"PRIu64, blkno, 
			cs->cs_chain_no, di->i_blkno);
		goto out;
	}

	ret = check_group_desc(ost, di, cs, bg1, blkno);
	if (ret) {
		if (prompt(ost, PY, "Chain %d in group allocator inode "
			   "%"PRIu64" points to an invalid descriptor block "
			   "at %"PRIu64".  Delete the chain?",
			   cs->cs_chain_no, di->i_blkno, blkno)) {
			chain->c_blkno = 0;
			changed = 1;
		}
		goto out;
	}

	/* read in each group desc and check it.  if we see an error we try
	 * to truncate the list after the last good desc */
	while (bg1->bg_next_group) {
		ret = ocfs2_read_group_desc(ost->ost_fs, bg1->bg_next_group,
					    buf2);
		if (ret) {
			com_err(whoami, ret, "while reading a group descriptor "
				    "from block %"PRIu64" as pointed to by "
				    "chain record %d in group allocator inode "
				    "%"PRIu64, bg1->bg_next_group, 
				    cs->cs_chain_no, di->i_blkno);
			goto out;
		} 

		rc = check_group_desc(ost, di, cs, bg2, bg1->bg_next_group);
		if (rc == 0) {
			memcpy(buf1, buf2, ost->ost_fs->fs_blocksize);
			continue;
		}

		if (prompt(ost, PY, "Chain %d in group allocator inode "
			   "%"PRIu64" contains an invalid descriptor block "
			   "at %"PRIu64".  Truncate the chain to the last "
			   "valid descriptor block?", cs->cs_chain_no,
			   di->i_blkno, bg1->bg_next_group)) {
			bg1->bg_next_group = 0;
			ret = ocfs2_write_group_desc(ost->ost_fs, 
						     bg1->bg_blkno,
						     (char *)bg1);
			if (ret) {
				com_err(whoami, ret, "while writing a group "
					"descriptor to block %"PRIu64" "
					"somewhere in chain %d in group "
					"allocator inode %"PRIu64, 
					bg1->bg_blkno, cs->cs_chain_no,
					di->i_blkno);
				ost->ost_write_error = 1;
			}
			break;
		}
	}

	/* XXX exit if it isn't updated? */
	if (cs->cs_total_bits != chain->c_total ||
	    cs->cs_free_bits != chain->c_free) {
		if (prompt(ost, PY, "Chain %d in allocator inode %"PRIu64" "
			   "has %u bits marked free out of %d total bits "
			   "but the block groups in the chain have %u "
			   "free out of %u total.  Fix this by updating "
			   "the chain record?", cs->cs_chain_no, di->i_blkno,
			   chain->c_free, chain->c_total, cs->cs_free_bits,
			   cs->cs_total_bits)) {
			chain->c_total = cs->cs_total_bits;
			chain->c_free = cs->cs_free_bits;
			changed = 1;
		}
	}

out:
	return changed;
}

/* If this returns 0 then the inode allocator had better be amenable to
 * iteration. */
static errcode_t verify_inode_alloc(o2fsck_state *ost, ocfs2_dinode *di,
				    char *buf1, char *buf2)
{
	struct chain_state cs = {0, };
	ocfs2_chain_list *cl;
	uint16_t i, max_count;
	ocfs2_chain_rec *cr;
	uint32_t free = 0, total = 0;
	int changed = 0, trust_next_free = 1;
	errcode_t ret = 0;

	if (memcmp(di->i_signature, OCFS2_INODE_SIGNATURE,
		   strlen(OCFS2_INODE_SIGNATURE))) {
		printf("Allocator inode %"PRIu64" doesn't have an inode "
		       "signature.  fsck won't repair this.\n", di->i_blkno);
		ret = OCFS2_ET_BAD_INODE_MAGIC;
		goto out;
	}

	if (!(di->i_flags & OCFS2_VALID_FL)) {
		printf("Allocator inode %"PRIu64" is not active.  fsck won't "
		       "repair this.\n", di->i_blkno);
		ret = OCFS2_ET_INODE_NOT_VALID;
		goto out;
	}

	if (!(di->i_flags & OCFS2_CHAIN_FL)) {
		printf("Allocator inode %"PRIu64" doesn't have the CHAIN_FL "
			"flag set.  fsck won't repair this.\n", di->i_blkno);
		/* not _entirely_ accurate, but pretty close. */
		ret = OCFS2_ET_INODE_NOT_VALID;
		goto out;
	}

	/* XXX should we check suballoc_node? */

	cl = &di->id2.i_chain;

	verbosef("cl count %u next %u\n", cl->cl_count, cl->cl_next_free_rec);

	max_count = ocfs2_chain_recs_per_inode(ost->ost_fs->fs_blocksize);

	/* make sure cl_count is clamped to the size of the inode */
	if (cl->cl_count > max_count &&
	    prompt(ost, PY, "Allocator inode %"PRIu64" claims to have %u "
		   "chains, but the maximum is %u. Fix the inode's count?",
		   di->i_blkno, cl->cl_count, max_count)) {
		cl->cl_count = max_count;
		changed = 1;
	}

	if (max_count > cl->cl_count)
		max_count = cl->cl_count;

	if (cl->cl_next_free_rec > max_count) {
		if (prompt(ost, PY, "Allocator inode %"PRIu64" claims %u as "
			   "the next free chain record, but fsck believes the "
			   "largest valid value is %u.  Clamp the next record "
			   "value?", di->i_blkno, cl->cl_next_free_rec,
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

	for (i = 0; i < max_count; i++) {
		cr = &cl->cl_recs[i];

		/* reset for each run */
		cs = (struct chain_state) {
			.cs_chain_no = i,
		};
		changed |= check_chain(ost, di, &cs, cr, buf1, buf2);

		if (cr->c_blkno != 0) {
			free += cs.cs_free_bits;
			total += cs.cs_total_bits;
			continue;
		}

		if (prompt(ost, PY, "Chain %d in allocator inode %"PRIu64" "
			   "isn't in use any more.  Remove it from the inode?",
			   cs.cs_chain_no, di->i_blkno)) {

			if (!trust_next_free) {
				printf("Can't remove the chain becuase "
				       "next_free_rec hasn't been fixed\n");
				continue;
			}

			/* move later lists down if there are any */
			if (i < (cl->cl_next_free_rec - 1)) {
				*cr = cl->cl_recs[cl->cl_next_free_rec - 1];
				i--;
			}

			cl->cl_next_free_rec--;
			max_count--;
			changed = 1;
			continue;
		}

	}

	if (di->id1.bitmap1.i_total != total || 
	    (di->id1.bitmap1.i_used != total - free)) {
		if (prompt(ost, PY, "Allocator inode %"PRIu64" has %u bits "
			   "marked used out of %d total bits but the chains "
			   "have %u used out of %u total.  Fix this by "
			   "updating the inode counts?", di->i_blkno,
			   di->id1.bitmap1.i_used, di->id1.bitmap1.i_total,
			   total - free, total)) {
			   di->id1.bitmap1.i_used = total - free;
			   di->id1.bitmap1.i_total = total;
			   changed = 1;
		}
	}

	if (changed) {
		/* if we're writing it anyway, we might as well clear the
		 * unused chain entries */ 
		if (cl->cl_next_free_rec != max_count)
			memset(&cl->cl_recs[cl->cl_next_free_rec], 0,
			       (max_count - cl->cl_next_free_rec) * 
			       sizeof(ocfs2_chain_rec));

		ret = ocfs2_write_inode(ost->ost_fs, di->i_blkno, (char *)di);
		if (ret) {
			com_err(whoami, ret, "while writing inode alloc inode "
				    "%"PRIu64, di->i_blkno);
			ost->ost_write_error = 1;
		}
	}

out:
	return ret;
}

/* this returns an error if it didn't leave the allocators in a state that
 * the iterators will be able to work with.  There is probably some room
 * for more resiliance here. */
errcode_t o2fsck_pass0(o2fsck_state *ost)
{
	errcode_t ret;
	uint64_t blkno;
	char *blocks = NULL;
	ocfs2_dinode *di = NULL;
	ocfs2_filesys *fs = ost->ost_fs;
	ocfs2_cached_inode **ci;
	int max_nodes = OCFS2_RAW_SB(fs->fs_super)->s_max_nodes;
	int i, type;

	printf("Pass 0: Checking allocation structures\n");

	ret = ocfs2_malloc_blocks(fs->fs_io, 3, &blocks);
	if (ret) {
		com_err(whoami, ret, "while allocating block buffers");
		goto out;
	}
	di = (ocfs2_dinode *)blocks;

	ret = ocfs2_malloc0(max_nodes * sizeof(ocfs2_cached_inode *), 
			    &ost->ost_inode_allocs);
	if (ret) {
		com_err(whoami, ret, "while cached inodes for each node");
		goto out;
	}

	/* first the global inode alloc and then each of the node's
	 * inode allocators */
	type = GLOBAL_INODE_ALLOC_SYSTEM_INODE;
	i = -1;

	for ( ; i < max_nodes; i++, type = INODE_ALLOC_SYSTEM_INODE) {
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
			 di->i_blkno, blkno);

		ret = verify_inode_alloc(ost, di,
					 blocks + ost->ost_fs->fs_blocksize,
					 blocks + 
					 (ost->ost_fs->fs_blocksize * 2));

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

out:
	if (blocks)
		ocfs2_free(&blocks);
	if (ret)
		o2fsck_free_inode_allocs(ost);

	return ret;
}
