/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * pass0.c
 *
 * file system checker for OCFS2
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
 * Authors: Zach Brown
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
#include "problem.h"
#include "util.h"

static const char *whoami = "pass0";

struct chain_state {
	uint32_t	cs_free_bits;
	uint32_t	cs_total_bits;
	uint32_t	cs_chain_no;
};

static int check_group_desc(o2fsck_state *ost, ocfs2_dinode *di,
			    struct chain_state *cs, ocfs2_group_desc *bg,
			    uint64_t blkno)
{
	verbosef("checking desc at %"PRIu64" bg: %"PRIu64"\n", blkno, 
		 bg->bg_blkno);

	/* We'll only consider this a valid descriptor if its signature,
	 * parent inode, and generation all check out */
	if (memcmp(bg->bg_signature, OCFS2_GROUP_DESC_SIGNATURE,
		   strlen(OCFS2_GROUP_DESC_SIGNATURE))) {
		printf("Group descriptor at block %"PRIu64" has an invalid "
			"signature.\n", blkno);
		return -1;
	}

	/* XXX maybe for advanced pain we could check to see if these 
	 * kinds of descs have valid generations for the inodes they
	 * reference */
	if (bg->bg_parent_dinode != di->i_blkno) {
		printf("Group descriptor at block %"PRIu64" is referenced by "
			"inode %"PRIu64" but thinks its parent inode is "
			"%"PRIu64"\n", blkno, di->i_blkno, 
			bg->bg_parent_dinode);
		return -1;
	}

	if (bg->bg_generation != di->i_generation) {
		printf("Group descriptor at block %"PRIu64" is referenced by "
			"inode %"PRIu64" who has a generation of %u, but "
			"the descriptor has a generation of %u\n",blkno, 
			di->i_blkno, di->i_generation, bg->bg_generation);
		return -1;
	}

	/* XXX check bg_blkno */

	/* XXX check bg_chain */

	/* XXX check _chain and worry about cpg/bpc lining up with bg_bits. 
	 * ah, bpc/cpg changes between the global bitmap and inode allocators,
	 * not within an inode allocator.  and its variable for clustersize/
	 * blocksize. */

#if 0
	/* XXX hmm, do we care about these checks?  if we want to be able
	 * to use the allocator, I think so.  This means walking them and
	 * fixing up the bitmaps.  maybe we'll fix them up after we've
	 * iterated through inodes but before we start allocating? */
	if (bg->bg_bits != (u32)chain->cl_cpg * (u32)chain->cl_bpc) {
	}
#endif

	cs->cs_total_bits += bg->bg_bits;
	cs->cs_free_bits += bg->bg_free_bits_count;

	return 0;
}

static int check_chain(o2fsck_state *ost, ocfs2_dinode *di,
		       struct chain_state *cs, ocfs2_chain_rec *chain,
		       char *buf1, char *buf2)
{
	ocfs2_group_desc *bg1 = (ocfs2_group_desc *)buf1;
	ocfs2_group_desc *bg2 = (ocfs2_group_desc *)buf2;
	uint64_t blkno = chain->c_blkno;
	errcode_t ret;
	int rc;

	if (ocfs2_block_out_of_range(ost->ost_fs, blkno))
		return 0;

	ret = ocfs2_read_group_desc(ost->ost_fs, blkno, buf1);
	if (ret) {
		/* trans or persis io error hmm. */
		rc = -1;
	}

	rc = check_group_desc(ost, di, cs, bg1, blkno);
	if (rc < 0 && prompt(ost, PY, "Chain %d in group allocator inode "
			     "%"PRIu64" points to an invalid descriptor block "
			     "at %"PRIu64".  Truncate this chain by removing "
			     " this reference?", cs->cs_chain_no, di->i_blkno,
			     blkno)) {
		/* this essentially frees this chain. */
		chain->c_free = 0;
		chain->c_total = 0;
		chain->c_blkno = 0;
		return 1;
	}
	if (rc > 1) {
		/* XXX write */
	}

	while (bg1->bg_next_group) {
		ret = ocfs2_read_group_desc(ost->ost_fs, bg1->bg_next_group,
					    buf2);
		if (ret) {
			/* trans or persis io error hmm. */
			rc = -1;
		}

		rc = check_group_desc(ost, di, cs, bg2, bg1->bg_next_group);
		if (rc > 1) {
			/* XXX write */
		}

		if (rc == 0) {
			blkno = bg1->bg_next_group;
			memcpy(buf1, buf2, ost->ost_fs->fs_blocksize);

			continue;
		}

		if (prompt(ost, PY, "Desc %"PRIu64" points to an invalid "
			   "descriptor at block %"PRIu64".  Truncate this "
			   "chain by removing this reference?", blkno,
			   bg1->bg_next_group)) {
			bg1->bg_next_group = 0;
			/* XXX write */
			return 1;
		}
	}

	if (cs->cs_total_bits != chain->c_total ||
	    cs->cs_free_bits != chain->c_free) {
		if (prompt(ost, PY, "Chain %d in allocator inode %"PRIu64" "
			   "has %u bits marked free out of %d total bits "
			   "but the block groups in the chain have %u "
			   "recorded out of %u total.  Fix this by updating "
			   "the chain record?", cs->cs_chain_no, di->i_blkno,
			   chain->c_free, chain->c_total, cs->cs_free_bits,
			   cs->cs_total_bits)) {
			chain->c_total = cs->cs_total_bits;
			chain->c_free = cs->cs_free_bits;
		}
	}

	return 0;
}

static errcode_t verify_inode_alloc(o2fsck_state *ost, ocfs2_dinode *di,
				    char *buf1, char *buf2)
{
	struct chain_state cs = {0, };
	ocfs2_chain_list *cl;
	uint16_t i, max_chain_rec;
	errcode_t ret;

	if (memcmp(di->i_signature, OCFS2_INODE_SIGNATURE,
		   strlen(OCFS2_INODE_SIGNATURE))) {
		printf("Allocator inode %"PRIu64" doesn't have an inode "
		       "signature.  fsck won't repair this.\n", di->i_blkno);
		return OCFS2_ET_BAD_INODE_MAGIC;
	}

	if (!(di->i_flags & OCFS2_VALID_FL)) {
		printf("Allocator inode %"PRIu64" is not active.  fsck won't "
		       "repair this.\n", di->i_blkno);
		return OCFS2_ET_INODE_NOT_VALID;
	}

	if (!(di->i_flags & OCFS2_CHAIN_FL)) {
		printf("Allocator inode %"PRIu64" doesn't have the CHAIN_FL "
			"flag set.  fsck won't repair this.\n", di->i_blkno);
		/* not _entirely_ accurate, but pretty close. */
		return OCFS2_ET_INODE_NOT_VALID;
	}

	/* XXX should we check suballoc_node? */

	cl = &di->id2.i_chain;

	verbosef("cl count %u next %u\n", cl->cl_count, cl->cl_next_free_rec);

	max_chain_rec = (ost->ost_fs->fs_blocksize - 
			offsetof(ocfs2_dinode, id2.i_chain.cl_recs)) / 
				sizeof(ocfs2_chain_rec);

	if (cl->cl_next_free_rec > max_chain_rec) {
		if (prompt(ost, PY, "Allocator inode %"PRIu64" claims %u "
			   "as the next free chain record, but it can only "
			   "have %u total.  Set the next record value?",
			   di->i_blkno, cl->cl_next_free_rec, max_chain_rec)) {
			cl->cl_next_free_rec = max_chain_rec;
		}
	} else
		max_chain_rec = cl->cl_next_free_rec;

	for (i = 0; i < max_chain_rec; i++) {
		cs.cs_chain_no = i;
		ret = check_chain(ost, di, &cs, &cl->cl_recs[i], buf1, buf2);
		/* XXX do things :) */
	}

	return 0;
}

/* 
 * here's a little rough-draft of what I think the procedure should
 * look like.  I'm probably missing things.
 *
 * - replay the journals if needed
 * 	- walk the journal extents looking for simple inconsistencies
 * 		- loops, doubly referenced blocks
 * 		- need this code later anyway for verifying files
 * 		  and i_clusters/i_size
 * 	- prompt to proceed if errors (mention backup superblock)
 * 		- ignore entirely or partially replay?
 *
 * - clean up the inode allocators
 * 	- kill loops, chains can't share groups
 * 	- move local allocs back to the global or something?
 * 	- verify just enough of the fields to make iterating work
 *
 * - walk inodes
 * 	- record all valid clusters that inodes point to
 * 	- make sure extent trees in inodes are consistent
 * 	- inconsistencies mark inodes for deletion
 *
 * - update cluster bitmap
 * 	- have bits reflect our set of referenced clusters
 * 	- again, how to resolve local/global?
 * 	* from this point on the library can trust the cluster bitmap
 *
 * - update the inode allocators
 * 	- make sure our set of valid inodes matches the bits
 * 	- make sure all the bit totals add up
 * 	* from this point on the library can trust the inode allocators
 *
 * This makes it so only these early passes need to have global 
 * allocation goo in memory.  The rest can use the library as 
 * usual.
 */

errcode_t o2fsck_pass0(o2fsck_state *ost)
{
	errcode_t ret;
	uint64_t blkno;
	char *blocks = NULL;
	ocfs2_dinode *di = NULL;
	ocfs2_filesys *fs = ost->ost_fs;
	int i, type;

	printf("Pass 1: Checking allocation structures\n");

	ret = ocfs2_malloc_blocks(fs->fs_io, 3, &blocks);
	if (ret) {
		com_err(whoami, ret, "while allocating block buffers");
		goto out;
	}
	di = (ocfs2_dinode *)blocks;

	/* first the global inode alloc and then each of the node's
	 * inode allocators */
	type = GLOBAL_INODE_ALLOC_SYSTEM_INODE;
	i = -1;

	do {
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
		if (ret) {
		}

		type = INODE_ALLOC_SYSTEM_INODE;
	} while (++i < OCFS2_RAW_SB(fs->fs_super)->s_max_nodes);

out:
	if (di)
		ocfs2_free(&di);
	if (blocks)
		ocfs2_free(&blocks);

	return 0;
}
