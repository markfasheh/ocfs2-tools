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
 * Pass 1 is arguably where the greatest concentration of special rules
 * in fsck live.  It walks through all the inodes that it can get its hands
 * on and verifies them.  For now it only walks the inode allocator groups
 * that pass 0 was able to verify.  One can imagine it getting other potential
 * inodes from other places.
 *
 * The complexity comes in deciding that inodes are valid.  There are different
 * critera for system inodes, allocator inodes, and the usual different 
 * unix inode file types.
 *
 * Pass 1 build up in-memory copies of the inode allocators that are written
 * back as the real inode allocators if inconsistencies are found between
 * the bitmaps and the inodes.  It also builds up many inode-dependent data
 * structures that are used by future passes:
 *  - icount map of inodes to what their current on-disk i_link_count is
 *  - bitmap of which inodes are directories or regular files
 *  - directory blocks that it finds off of directory inodes
 *
 * The end of Pass 1 is when the found block bitmap should contain all the
 * blocks in the system that are in use.  This is used to derive the set of
 * clusters that should be allocated.  The cluster chain allocator is loaded
 * and synced up with this set and potentially written back.  After that point
 * fsck can use libocfs2 to allocate and free clusters as usual.
 *
 * XXX
 * 	check many, many, more i_ fields for each inode type
 * 	make sure the inode's dtime/count/valid match in update_inode_alloc
 * 	more carefully track cluster use in conjunction with pass 0
 * 	handle local alloc inodes for realsies
 * 	free an inodes chains and extents and such if we free it
 */
#include <string.h>
#include <inttypes.h>
#include <time.h>

#include "ocfs2.h"

#include "dirblocks.h"
#include "dirparents.h"
#include "extent.h"
#include "icount.h"
#include "fsck.h"
#include "pass1.h"
#include "problem.h"
#include "util.h"

static const char *whoami = "pass1";

void o2fsck_free_inode_allocs(o2fsck_state *ost)
{
	uint16_t i;

	ocfs2_free_cached_inode(ost->ost_fs, ost->ost_global_inode_alloc);

	for (i = 0; i < OCFS2_RAW_SB(ost->ost_fs->fs_super)->s_max_nodes;i++)
		ocfs2_free_cached_inode(ost->ost_fs, ost->ost_inode_allocs[i]);
}

/* update our in memory images of the inode chain alloc bitmaps.  these
 * will be written out at the end of pass1 and the library will read
 * them off disk for use from then on. */
static void update_inode_alloc(o2fsck_state *ost, ocfs2_dinode *di, 
			       uint64_t blkno, int val)
{
	uint16_t node, max_nodes, yn;
	errcode_t ret = OCFS2_ET_INTERNAL_FAILURE;
	ocfs2_cached_inode *cinode;
	int oldval;

	val = !!val;

	if (ost->ost_write_inode_alloc_asked && !ost->ost_write_inode_alloc)
		return;

	max_nodes = OCFS2_RAW_SB(ost->ost_fs->fs_super)->s_max_nodes;

	for (node = ~0; node != max_nodes; node++, 
					   ret = OCFS2_ET_INTERNAL_FAILURE) {

		if (node == (uint16_t)~0)
			cinode = ost->ost_global_inode_alloc;
		else
			cinode = ost->ost_inode_allocs[node];

		/* we might have had trouble reading the chains in pass 0 */
		if (cinode == NULL)
			continue;

		ret = ocfs2_chain_force_val(ost->ost_fs, cinode, blkno, val,
					    &oldval);
		if (ret) {
		       	if (ret != OCFS2_ET_INVALID_BIT)
				com_err(whoami, ret, "while trying to set "
					"inode %"PRIu64"'s allocation to '%d' "
					"in node %u's chain", blkno, val,
					node);
			continue;
		}

		/* hmm, de hmm.  it would be kind of nice if the bitmaps
		 * didn't use 'int' but rather some real boolean construct */
		oldval = !!oldval;

		/* this node covers the inode.  see if we've changed the 
		 * bitmap and if the user wants us to keep tracking it and
		 * write back the new map */
		if (oldval != val && !ost->ost_write_inode_alloc_asked) {
			yn = prompt(ost, PY, 0, "fsck found an inode whose "
				    "allocation does not match the chain "
				    "allocators.  Fix the allocation of this "
				    "and all future inodes?");
			ost->ost_write_inode_alloc_asked = 1;
			ost->ost_write_inode_alloc = !!yn;
			if (!ost->ost_write_inode_alloc)
				o2fsck_free_inode_allocs(ost);
		}
		break;
	}

	if (ret) {
		com_err(whoami, ret, "while trying to set inode %"PRIu64"'s "
			"allocation to '%d'.  None of the nodes chain "
			"allocator's had a group covering the inode.",
			blkno, val);
		goto out;
	}

	verbosef("updated inode %"PRIu64" alloc to %d in node %"PRIu16"\n",
		 blkno, val, node);

	/* make sure the inode's fields are consistent if it's allocated */
	if (val == 1 && node != (uint16_t)di->i_suballoc_node &&
	    prompt(ost, PY, 0, "Inode %"PRIu64" indicates that it was allocated "
		   "from node %"PRIu16" but node %"PRIu16"'s chain allocator "
		   "covers the inode.  Fix the inode's record of where it is "
		   "allocated?",
		   blkno, di->i_suballoc_node, node)) {
		di->i_suballoc_node = node;
		o2fsck_write_inode(ost, di->i_blkno, di);
	}
out:
	return;
}

/* Check the basics of the ocfs2_dinode itself.  If we find problems
 * we clear the VALID flag and the caller will see that and update
 * inode allocations and write the inode to disk. 
 *
 * XXX the o2fsck_write_inode helpers need to be fixed here*/
static void o2fsck_verify_inode_fields(ocfs2_filesys *fs, o2fsck_state *ost, 
				       uint64_t blkno, ocfs2_dinode *di)
{
	int clear = 0;

	verbosef("checking inode %"PRIu64"'s fields\n", blkno);

	if (!(di->i_flags & OCFS2_VALID_FL))
		goto out;

	if (di->i_fs_generation != ost->ost_fs_generation) {
		if (prompt(ost, PY, 0, "Inode read from block %"PRIu64" looks "
			   "like it is valid but it has a generation of %x "
			   "that doesn't match the current volume's "
			   "generation of %x.  This is probably a harmless "
			   "old inode.  Mark it deleted?", blkno, 
			   di->i_fs_generation, ost->ost_fs_generation)) {

			clear = 1;
			goto out;
		}
		if (prompt(ost, PY, 0, "Update the inode's generation to match "
			  "the volume?")) {

			di->i_fs_generation = ost->ost_fs_generation;
			o2fsck_write_inode(ost, blkno, di);
		}
	}

	/* do we want to detect and delete corrupt system dir/files here
	 * so we can recreate them later ? */

	/* also make sure the journal inode is ok? */

	/* clamp inodes to > OCFS2_SUPER_BLOCK_BLKNO && < fs->fs_blocks? */

	/* XXX need to compare the lifetime of inodes (uninitialized?
	 * in use?  orphaned?  deleted?  garbage?) to understand what
	 * fsck can do to fix it up */

	if (di->i_blkno != blkno &&
	    prompt(ost, PY, 0, "Inode read from block %"PRIu64" has i_blkno set "
		   "to %"PRIu64".  Set the inode's i_blkno value to reflect "
		   "its location on disk?", blkno, di->i_blkno)) {
		di->i_blkno = blkno;
		o2fsck_write_inode(ost, blkno, di);
	}

	if (di->i_links_count)
		o2fsck_icount_set(ost->ost_icount_in_inodes, di->i_blkno,
					di->i_links_count);

	/* offer to clear a non-directory root inode so that 
	 * pass3:check_root() can re-create it */
	if ((di->i_blkno == fs->fs_root_blkno) && !S_ISDIR(di->i_mode) && 
	    prompt(ost, PY, 0, "Root inode isn't a directory.  Clear it in "
		   "preparation for fixing it?")) {
		di->i_dtime = 0ULL;
		di->i_links_count = 0ULL;
		o2fsck_icount_set(ost->ost_icount_in_inodes, di->i_blkno,
				  di->i_links_count);

		o2fsck_write_inode(ost, blkno, di);
	}

	if (di->i_dtime && prompt(ost, PY, 0, "Inode %"PRIu64" is in use but has "
				  "a non-zero dtime.  Reset the dtime to 0?",  
				   di->i_blkno)) {
		di->i_dtime = 0ULL;
		o2fsck_write_inode(ost, blkno, di);
	}

	if (S_ISDIR(di->i_mode)) {
		ocfs2_bitmap_set(ost->ost_dir_inodes, blkno, NULL);
		o2fsck_add_dir_parent(&ost->ost_dir_parents, blkno, 0, 0);
	} else if (S_ISREG(di->i_mode)) {
		ocfs2_bitmap_set(ost->ost_reg_inodes, blkno, NULL);
	} else if (S_ISLNK(di->i_mode)) {
		/* we only make sure a link's i_size matches
		 * the link names length in the file data later when
		 * we walk the inode's blocks */
	} else {
		if (!S_ISCHR(di->i_mode) && !S_ISBLK(di->i_mode) &&
		    !S_ISFIFO(di->i_mode) && !S_ISSOCK(di->i_mode)) {
			clear = 1;
			goto out;
		}

		/* i_size?  what other sanity testing for devices? */
	}

out:
	if (clear) {
		di->i_flags &= ~OCFS2_SUPER_BLOCK_FL;
		o2fsck_write_inode(ost, blkno, di);
	}
}

struct verifying_blocks {
       unsigned		vb_clear:1,
       			vb_saw_link_null:1,
       			vb_link_read_error:1;

       uint64_t		vb_link_len;
       uint64_t		vb_num_blocks;	
       uint64_t		vb_last_block;	
       int		vb_errors;
       o2fsck_state 	*vb_ost;
       ocfs2_dinode	*vb_di;
};

/* last_block and num_blocks would be different in a sparse file */
static void vb_saw_block(struct verifying_blocks *vb, uint64_t bcount)
{
	vb->vb_num_blocks++;
	if (bcount > vb->vb_last_block)
		vb->vb_last_block = bcount;
}

static void process_link_block(struct verifying_blocks *vb, uint64_t blkno)
{
	char *buf, *null;
	errcode_t ret;
	unsigned int blocksize = vb->vb_ost->ost_fs->fs_blocksize;

	if (vb->vb_saw_link_null)
		return;

	ret = ocfs2_malloc_blocks(vb->vb_ost->ost_fs->fs_io, 1, &buf);
	if (ret)
		fatal_error(ret, "while allocating room to read a block of "
				 "link data");

	ret = io_read_block(vb->vb_ost->ost_fs->fs_io, blkno, 1, buf);
	if (ret) {
		goto out;
	}

	null = memchr(buf, 0, blocksize);
	if (null != NULL) {
		vb->vb_link_len += null - buf;
		vb->vb_saw_link_null = 1;
	} else {
		vb->vb_link_len += blocksize;
	}

out:
	ocfs2_free(&buf);
}

static void check_link_data(struct verifying_blocks *vb)
{
	ocfs2_dinode *di = vb->vb_di;
	o2fsck_state *ost = vb->vb_ost;
	uint64_t expected;

	verbosef("found a link: num %"PRIu64" last %"PRIu64" len "
		"%"PRIu64" null %d error %d\n", vb->vb_num_blocks, 
		vb->vb_last_block, vb->vb_link_len, vb->vb_saw_link_null, 
		vb->vb_link_read_error);

	if (vb->vb_link_read_error) {
		if (prompt(ost, PY, 0, "There was an error reading a data block "
			   "for symlink inode %"PRIu64".  Clear the inode?",
			   di->i_blkno)) {
			vb->vb_clear = 1;
			return;
		}
	}

	/* XXX this could offer to null terminate */
	if (!vb->vb_saw_link_null) {
		if (prompt(ost, PY, 0, "The target of symlink inode %"PRIu64" "
			   "isn't null terminated.  Clear the inode?",
			   di->i_blkno)) {
			vb->vb_clear = 1;
			return;
		}
	}

	expected = ocfs2_blocks_in_bytes(ost->ost_fs, vb->vb_link_len + 1);

	if (di->i_size != vb->vb_link_len) {
		if (prompt(ost, PY, 0, "The target of symlink inode %"PRIu64" "
			   "is %"PRIu64" bytes long on disk, but i_size is "
			   "%"PRIu64" bytes long.  Update i_size to reflect "
			   "the length on disk?",
			   di->i_blkno, vb->vb_link_len, di->i_size)) {
			di->i_size = vb->vb_link_len;
			o2fsck_write_inode(ost, di->i_blkno, di);
			return;
		}
	}

	/* maybe we don't shrink link target allocations, I don't know,
	 * someone will holler if this is wrong :) */
	if (vb->vb_num_blocks != expected) {
		if (prompt(ost, PN, 0, "The target of symlink inode %"PRIu64" "
			   "fits in %"PRIu64" blocks but the inode has "
			   "%"PRIu64" allocated.  Clear the inode?", 
			   di->i_blkno, expected, vb->vb_num_blocks)) {
			vb->vb_clear = 1;
			return;
		}
	}
}

static int verify_block(ocfs2_filesys *fs,
			    uint64_t blkno,
			    uint64_t bcount,
			    void *priv_data)
{
	struct verifying_blocks *vb = priv_data;
	ocfs2_dinode *di = vb->vb_di;
	o2fsck_state *ost = vb->vb_ost;
	
	/* someday we may want to worry about holes in files here */

	if ((blkno < OCFS2_SUPER_BLOCK_BLKNO) || (blkno > fs->fs_blocks)) {
		vb->vb_errors++;
#if 0 /* XXX ext2 does this by returning a value to libext2 which clears the 
	 block from the inode's allocation */
		if (prompt(ost, PY, 0, "inode %"PRIu64" references bad physical "
			   "block %"PRIu64" at logical block %"PRIu64", "
			   "should it be cleared?", di->i_blkno, bklno, 
			   bcount)) {
		}
#endif
	}

	/* XXX this logic should be more sophisticated.  It's not really clear
	 * what ext2 is trying to do in theirs. */
	if (vb->vb_errors == 12) {
		if (prompt(ost, PY, 0, "inode %"PRIu64" has seen many errors, "
			   "should it be cleared?", di->i_blkno)) {
			vb->vb_clear = 1;
			return OCFS2_BLOCK_ABORT;
		}
	}

	if (S_ISDIR(di->i_mode)) {
		verbosef("adding dir block %"PRIu64"\n", blkno);
		o2fsck_add_dir_block(&ost->ost_dirblocks, di->i_blkno, blkno,
					bcount);
	}

	if (S_ISLNK(di->i_mode))
		process_link_block(vb, blkno);

	vb_saw_block(vb, bcount);

	return 0;
}

/* XXX this is only really building up the vb data so that the caller can
 * verify the chain allocator inode's fields.  I wonder if we shouldn't have
 * already done that in pass 0. */
static int check_gd_block(ocfs2_filesys *fs, uint64_t gd_blkno, int chain_num,
			   void *priv_data)
{
	struct verifying_blocks *vb = priv_data;
	verbosef("found gd block %"PRIu64"\n", gd_blkno);
	/* XXX should arguably be verifying that pass 0 marked the group desc
	 * blocks found */
	/* don't have bcount */
	vb_saw_block(vb, vb->vb_num_blocks);
	return 0;
}

/* XXX do something with these */
static errcode_t handle_local_alloc(o2fsck_state *ost, ocfs2_dinode *di)
{
	ocfs2_local_alloc *lab = &di->id2.i_lab;
	verbosef("la_bm_off %u size %u total %u used %u\n", lab->la_bm_off,
		 lab->la_size, di->id1.bitmap1.i_total,
		 di->id1.bitmap1.i_used);
	return 0;
}

static void o2fsck_check_blocks(ocfs2_filesys *fs, o2fsck_state *ost,
				uint64_t blkno, ocfs2_dinode *di)
{
	struct verifying_blocks vb = {0, };
	uint64_t expected = 0;
	errcode_t ret;

	vb.vb_ost = ost;
	vb.vb_di = di;

	if (di->i_flags & OCFS2_LOCAL_ALLOC_FL)
		ret = handle_local_alloc(ost, di);
	else if (di->i_flags & OCFS2_CHAIN_FL)
		ret = ocfs2_chain_iterate(fs, blkno, check_gd_block, &vb);
	else {
		ret = o2fsck_check_extents(ost, di);
		if (ret == 0)
			ret = ocfs2_block_iterate_inode(fs, di, 0,
							verify_block, &vb);
	}

	if (ret) {
		fatal_error(ret, "while iterating over the blocks for inode "
			         "%"PRIu64, di->i_blkno);	
	}

	if (S_ISLNK(di->i_mode))
		check_link_data(&vb);

	if (S_ISDIR(di->i_mode) && vb.vb_num_blocks == 0) {
		if (prompt(ost, PY, 0, "Inode %"PRIu64" is a zero length "
			   "directory, clear it?", di->i_blkno)) {
			vb.vb_clear = 1;
		}
	}

	/*
	 * XXX we should have a helper that clears an inode and backs it out of
	 * any book-keeping that it might have been included in, as though it
	 * was never seen.  the alternative is to restart pass1 which seems
	 * goofy. 
	 */
	if (vb.vb_clear) {
		di->i_links_count = 0;
		o2fsck_icount_set(ost->ost_icount_in_inodes, di->i_blkno,
				  di->i_links_count);
		di->i_dtime = time(0);
		o2fsck_write_inode(ost, di->i_blkno, di);
		/* XXX clear valid flag and stuff? */
	}

#if 0 /* boy, this is just broken */
	if (vb.vb_num_blocks > 0)
		expected = (vb.vb_last_block + 1) * fs->fs_blocksize;

	/* i_size is checked for symlinks elsewhere */
	if (!S_ISLNK(di->i_mode) && di->i_size > expected &&
	    prompt(ost, PY, 0, "Inode %"PRIu64" has a size of %"PRIu64" but has "
		    "%"PRIu64" bytes of actual data. Correct the file size?",
		    di->i_blkno, di->i_size, expected)) {
		di->i_size = expected;
		o2fsck_write_inode(ost, blkno, di);
	}
#endif

	if (vb.vb_num_blocks > 0)
		expected = ocfs2_clusters_in_blocks(fs, vb.vb_last_block + 1);

	if (di->i_clusters < expected &&
	    prompt(ost, PY, 0, "inode %"PRIu64" has %"PRIu32" clusters but its "
		   "blocks fit in %"PRIu64" clusters.  Correct the number of "
		   "clusters?", di->i_blkno, di->i_clusters, expected)) {
		di->i_clusters = expected;
		o2fsck_write_inode(ost, blkno, di);
	}
}

/* once we've iterated all the inodes we should have the current working
 * set of which blocks we think are in use.  we use this to derive the set
 * of clusters that should be allocated in the cluster chain allocators.  we
 * don't iterate over all clusters like we do inodes.. */
static void write_cluster_alloc(o2fsck_state *ost)
{
	ocfs2_cached_inode *ci = NULL;
	errcode_t ret;
	uint64_t blkno, last_cbit, cbit, cbit_found;
	struct ocfs2_cluster_group_sizes cgs;

	ocfs2_calc_cluster_groups(ost->ost_fs->fs_clusters,
				  ost->ost_fs->fs_blocksize, &cgs);

	/* first load the cluster chain alloc so we can compare */
	ret = ocfs2_lookup_system_inode(ost->ost_fs,
					GLOBAL_BITMAP_SYSTEM_INODE, 0, &blkno);
	if (ret) {
		com_err(whoami, ret, "while looking up the cluster bitmap "
			"allocator inode");
		goto out;
	}

	/* load in the cluster chain allocator */
	ret = ocfs2_read_cached_inode(ost->ost_fs, blkno, &ci);
	if (ret) {
		com_err(whoami, ret, "while reading the cluster bitmap "
			"allocator inode from block %"PRIu64, blkno);
		goto out;
	}

	ret = ocfs2_load_chain_allocator(ost->ost_fs, ci);
	if (ret) {
		com_err(whoami, ret, "while loading the cluster bitmap "
			"allocator from block %"PRIu64, blkno);
		goto out;
	}

	/* we walk our found blocks bitmap to find clusters that we think
	 * are in use.  each time we find a block in a cluster we skip ahead
	 * to the first block of the next cluster when looking for the next.
	 *
	 * once we have a cluster we think is allocated we walk the cluster
	 * chain alloc bitmaps from the last cluster we thought was allocated
	 * to make sure that all the bits are cleared on the way.
	 *
	 * we special case the number of clusters as the cluster offset which
	 * indicates that the rest of the bits to the end of the bitmap
	 * should be clear.
	 */
	for (last_cbit = 0, cbit = 0;
	     cbit < ost->ost_fs->fs_clusters; 
	     cbit++, last_cbit = cbit) {

		ret = ocfs2_bitmap_find_next_set(ost->ost_allocated_clusters,
						 cbit, &cbit);

		/* clear to the end */
		if (ret == OCFS2_ET_BIT_NOT_FOUND)
			cbit = ost->ost_fs->fs_clusters;

		ret = ocfs2_bitmap_find_next_set(ci->ci_chains, last_cbit, 
						 &cbit_found);
		if (ret == OCFS2_ET_BIT_NOT_FOUND)
			cbit_found = ost->ost_fs->fs_clusters;

		verbosef("cbit %"PRIu64" last_cbit %"PRIu64" cbit_found "
			 "%"PRIu64"\n", cbit, last_cbit, cbit_found);

		if (cbit_found == cbit)
			continue;

		if (!ost->ost_write_cluster_alloc_asked) {
			int yn;
			yn = prompt(ost, PY, 0, "The cluster bitmap doesn't "
				    "match what fsck thinks should be in use "
				    "and freed.  Update the bitmap on disk?");
			ost->ost_write_cluster_alloc_asked = 1;
			ost->ost_write_cluster_alloc = !!yn;
			if (!ost->ost_write_cluster_alloc)
				goto out;
		}

		/* clear set bits that should have been clear up to cbit */
		while (cbit_found < cbit) {
			ret = ocfs2_chain_force_val(ost->ost_fs, ci,
						    cbit_found, 0, NULL);
			if (ret) {
				com_err(whoami, ret, "while trying to clear "
					"bit %"PRIu64" in the cluster bitmap.",
					cbit_found);
				goto out;
			}
			cbit_found++;
			ret = ocfs2_bitmap_find_next_set(ci->ci_chains, cbit, 
							 &cbit_found);
			if (ret == OCFS2_ET_BIT_NOT_FOUND)
				cbit_found = ost->ost_fs->fs_clusters;
		}

		/* make sure cbit is set before moving on */
		if (cbit_found != cbit && cbit != ost->ost_fs->fs_clusters) {
			ret = ocfs2_chain_force_val(ost->ost_fs, ci, cbit, 1,
						    NULL);
			if (ret) {
				com_err(whoami, ret, "while trying to set bit "
					"%"PRIu64" in the cluster bitmap.",
					cbit);
				goto out;
			}
		}
	}

	ret = ocfs2_write_chain_allocator(ost->ost_fs, ci);
	if (ret)
		com_err(whoami, ret, "while trying to write back the cluster "
			"bitmap allocator");

out:
	if (ci)
		ocfs2_free_cached_inode(ost->ost_fs, ci);
}

static void write_inode_alloc(o2fsck_state *ost)
{
	int max_nodes = OCFS2_RAW_SB(ost->ost_fs->fs_super)->s_max_nodes;
	ocfs2_cached_inode **ci;
	errcode_t ret;
	int i = -1;

	if (!ost->ost_write_inode_alloc)
		return;

	for ( ; i < max_nodes; i++) {
		if (i == -1)
			ci = &ost->ost_global_inode_alloc;
		else
			ci = &ost->ost_inode_allocs[i];

		if (*ci == NULL)
			continue;

		verbosef("writing node %d's allocator\n", i);

		ret = ocfs2_write_chain_allocator(ost->ost_fs, *ci);
		if (ret)
			com_err(whoami, ret, "while trying to write back node "
				"%d's inode allocator", i);
	}

	o2fsck_free_inode_allocs(ost);
}

errcode_t o2fsck_pass1(o2fsck_state *ost)
{
	errcode_t ret;
	uint64_t blkno;
	char *buf;
	ocfs2_dinode *di;
	ocfs2_inode_scan *scan;
	ocfs2_filesys *fs = ost->ost_fs;
	int valid;

	printf("Pass 1: Checking inodes and blocks.\n");

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret) {
		com_err(whoami, ret,
			"while allocating inode buffer");
		goto out;
	}

	di = (ocfs2_dinode *)buf;

	ret = ocfs2_open_inode_scan(fs, &scan);
	if (ret) {
		com_err(whoami, ret,
			"while opening inode scan");
		goto out_free;
	}

	for(;;) {
		ret = ocfs2_get_next_inode(scan, &blkno, buf);
		if (ret) {
			/* we don't deal with corrupt inode allocation
			 * files yet.  They won't be files for much longer.
			 * In the future the intent is to clean up inode
			 * allocation if scanning returns an error. */
			com_err(whoami, ret,
				"while getting next inode");
			goto out_close_scan;
		}
		if (blkno == 0)
			break;

		valid = 0;

		/* scanners have to skip over uninitialized inodes */
		if (!memcmp(di->i_signature, OCFS2_INODE_SIGNATURE,
		    strlen(OCFS2_INODE_SIGNATURE))) {
			o2fsck_verify_inode_fields(fs, ost, blkno, di);

			/* XXX be able to mark the blocks in the inode as 
			 * bad if the inode was bad */
			o2fsck_check_blocks(fs, ost, blkno, di);
			valid = di->i_flags & OCFS2_VALID_FL;
		}

		update_inode_alloc(ost, di, blkno, valid);
	}

	write_cluster_alloc(ost);
	write_inode_alloc(ost);

out_close_scan:
	ocfs2_close_inode_scan(scan);
out_free:
	ocfs2_free(&buf);

out:
	return 0;
}
