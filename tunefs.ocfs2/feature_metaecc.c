/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * feature_metaecc.c
 *
 * ocfs2 tune utility for enabling and disabling the metaecc feature.
 *
 * Copyright (C) 2004, 2008 Oracle.  All rights reserved.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>
#include <inttypes.h>
#include <assert.h>

#include "ocfs2-kernel/kernel-list.h"
#include "ocfs2/kernel-rbtree.h"
#include "ocfs2/ocfs2.h"

#include "libocfs2ne.h"



/*
 * Since we have to scan the inodes in our first pass to find directories
 * that need trailers, we might as well store them off and avoid reading
 * them again when its time to write ECC data.  In fact, we'll do all the
 * scanning up-front, including extent blocks and group descriptors.  The
 * only metadata block we don't store is the superblock, because we'll
 * write that last from fs->fs_super.
 *
 * We store all of this in an rb-tree of block_to_ecc structures.  We can
 * look blocks back up if needed, and we have writeback functions attached.
 *
 * For directory inodes, we pass e_buf into tunefs_prepare_dir_trailer(),
 * which does not copy off the inode.  Thus, when
 * tunefs_install_dir_trailer() modifies the inode, this is the one that
 * gets updated.
 *
 * For directory blocks, tunefs_prepare_dir_trailer() makes its own copies.
 * After we run tunefs_install_dir_trailer(), we'll have to copy the
 * changes back to our copy.
 */
struct block_to_ecc {
	struct rb_node e_node;
	uint64_t e_blkno;
	struct ocfs2_dinode *e_di;
	char *e_buf;
	errcode_t (*e_write)(ocfs2_filesys *fs, struct block_to_ecc *block);
};

/*
 * We have to do chain allocators at the end, because we may use them
 * as we add dirblock trailers.  Really, we only need the inode block
 * number.
 */
struct chain_to_ecc {
	struct list_head ce_list;
	uint64_t ce_blkno;
};

struct add_ecc_context {
	errcode_t ae_ret;
	struct tools_progress *ae_prog;

	uint32_t ae_clusters;
	struct list_head ae_dirs;
	uint64_t ae_dircount;
	struct list_head ae_chains;
	uint64_t ae_chaincount;
	struct rb_root ae_blocks;
	uint64_t ae_blockcount;
};

static void block_free(struct block_to_ecc *block)
{
	if (block->e_buf)
		ocfs2_free(&block->e_buf);
	ocfs2_free(&block);
}

static struct block_to_ecc *block_lookup(struct add_ecc_context *ctxt,
					 uint64_t blkno)
{
	struct rb_node *p = ctxt->ae_blocks.rb_node;
	struct block_to_ecc *block;

	while (p) {
		block = rb_entry(p, struct block_to_ecc, e_node);
		if (blkno < block->e_blkno) {
			p = p->rb_left;
		} else if (blkno > block->e_blkno) {
			p = p->rb_right;
		} else
			return block;
	}

	return NULL;
}

static void dump_ecc_tree(struct add_ecc_context *ctxt)
{
	struct rb_node *n;
	struct block_to_ecc *tmp;

	verbosef(VL_DEBUG, "Dumping ecc block tree\n");
	n = rb_first(&ctxt->ae_blocks);
	while (n) {
		tmp = rb_entry(n, struct block_to_ecc, e_node);
		verbosef(VL_DEBUG, "Block %"PRIu64", struct %p, buf %p\n",
			 tmp->e_blkno, tmp, tmp->e_buf);
		n = rb_next(n);
	}
}

static errcode_t block_insert(struct add_ecc_context *ctxt,
			      struct block_to_ecc *block)
{
	errcode_t ret;
	struct block_to_ecc *tmp;
	struct rb_node **p = &ctxt->ae_blocks.rb_node;
	struct rb_node *parent = NULL;

	while (*p) {
		parent = *p;
		tmp = rb_entry(parent, struct block_to_ecc, e_node);
		if (block->e_blkno < tmp->e_blkno) {
			p = &(*p)->rb_left;
			tmp = NULL;
		} else if (block->e_blkno > tmp->e_blkno) {
			p = &(*p)->rb_right;
			tmp = NULL;
		} else {
			dump_ecc_tree(ctxt);
			assert(0);  /* We shouldn't find it. */
		}
	}

	rb_link_node(&block->e_node, parent, p);
	rb_insert_color(&block->e_node, &ctxt->ae_blocks);
	ctxt->ae_blockcount++;
	ret = 0;

	return ret;
}

static errcode_t dinode_write_func(ocfs2_filesys *fs,
				   struct block_to_ecc *block)
{
	return ocfs2_write_inode(fs, block->e_blkno, block->e_buf);
}

static errcode_t block_insert_dinode(ocfs2_filesys *fs,
				     struct add_ecc_context *ctxt,
				     struct ocfs2_dinode *di)
{
	errcode_t ret;
	struct block_to_ecc *block = NULL;

	ret = ocfs2_malloc0(sizeof(struct block_to_ecc), &block);
	if (ret)
		goto out;

	ret = ocfs2_malloc_block(fs->fs_io, &block->e_buf);
	if (ret)
		goto out;

	memcpy(block->e_buf, di, fs->fs_blocksize);
	block->e_di = (struct ocfs2_dinode *)block->e_buf;
	block->e_blkno = di->i_blkno;
	block->e_write = dinode_write_func;
	block_insert(ctxt, block);

out:
	if (ret && block)
		block_free(block);
	return ret;
}

static errcode_t eb_write_func(ocfs2_filesys *fs,
			       struct block_to_ecc *block)
{
	return ocfs2_write_extent_block(fs, block->e_blkno, block->e_buf);
}

static errcode_t block_insert_eb(ocfs2_filesys *fs,
				 struct add_ecc_context *ctxt,
				 struct ocfs2_dinode *di,
				 struct ocfs2_extent_block *eb)
{
	errcode_t ret;
	struct block_to_ecc *block = NULL;

	ret = ocfs2_malloc0(sizeof(struct block_to_ecc), &block);
	if (ret)
		goto out;

	ret = ocfs2_malloc_block(fs->fs_io, &block->e_buf);
	if (ret)
		goto out;

	memcpy(block->e_buf, eb, fs->fs_blocksize);
	block->e_blkno = eb->h_blkno;
	block->e_write = eb_write_func;
	block_insert(ctxt, block);

out:
	if (ret && block)
		block_free(block);
	return ret;
}

static errcode_t gd_write_func(ocfs2_filesys *fs,
			       struct block_to_ecc *block)
{
	return ocfs2_write_group_desc(fs, block->e_blkno, block->e_buf);
}

static errcode_t block_insert_gd(ocfs2_filesys *fs,
				 struct add_ecc_context *ctxt,
				 struct ocfs2_dinode *di,
				 struct ocfs2_group_desc *gd)
{
	errcode_t ret;
	struct block_to_ecc *block = NULL;

	ret = ocfs2_malloc0(sizeof(struct block_to_ecc), &block);
	if (ret)
		goto out;

	ret = ocfs2_malloc_block(fs->fs_io, &block->e_buf);
	if (ret)
		goto out;

	memcpy(block->e_buf, gd, fs->fs_blocksize);
	block->e_blkno = gd->bg_blkno;
	block->e_write = gd_write_func;
	block_insert(ctxt, block);

out:
	if (ret && block)
		block_free(block);
	return ret;
}

static errcode_t dirblock_write_func(ocfs2_filesys *fs,
				     struct block_to_ecc *block)
{
	return ocfs2_write_dir_block(fs, block->e_di, block->e_blkno,
				     block->e_buf);
}

static errcode_t block_insert_dirblock(ocfs2_filesys *fs,
				       struct add_ecc_context *ctxt,
				       struct ocfs2_dinode *di,
				       uint64_t blkno, char *buf)
{
	errcode_t ret;
	struct block_to_ecc *block = NULL;

	ret = ocfs2_malloc0(sizeof(struct block_to_ecc), &block);
	if (ret)
		goto out;

	ret = ocfs2_malloc_block(fs->fs_io, &block->e_buf);
	if (ret)
		goto out;

	memcpy(block->e_buf, buf, fs->fs_blocksize);
	block->e_di = di;
	block->e_blkno = blkno;
	block->e_write = dirblock_write_func;
	block_insert(ctxt, block);

out:
	if (ret && block)
		block_free(block);
	return ret;

}

static void empty_ecc_blocks(struct add_ecc_context *ctxt)
{
	struct block_to_ecc *block;
	struct rb_node *node;

	while ((node = rb_first(&ctxt->ae_blocks)) != NULL) {
		block = rb_entry(node, struct block_to_ecc, e_node);

		rb_erase(&block->e_node, &ctxt->ae_blocks);
		ocfs2_free(&block->e_buf);
		ocfs2_free(&block);
	}
}

static errcode_t add_ecc_chain(struct add_ecc_context *ctxt,
			       uint64_t blkno)
{
	errcode_t ret;
	struct chain_to_ecc *cte;

	ret = ocfs2_malloc0(sizeof(struct chain_to_ecc), &cte);
	if (!ret) {
		cte->ce_blkno = blkno;
		list_add_tail(&cte->ce_list, &ctxt->ae_chains);
		ctxt->ae_chaincount++;
	}

	return ret;
}

static void empty_add_ecc_context(struct add_ecc_context *ctxt)
{
	struct tunefs_trailer_context *tc;
	struct chain_to_ecc *cte;
	struct list_head *n, *pos;

	list_for_each_safe(pos, n, &ctxt->ae_chains) {
		cte = list_entry(pos, struct chain_to_ecc, ce_list);
		list_del(&cte->ce_list);
		ocfs2_free(&cte);
	}

	list_for_each_safe(pos, n, &ctxt->ae_dirs) {
		tc = list_entry(pos, struct tunefs_trailer_context, d_list);
		tunefs_trailer_context_free(tc);
	}

	empty_ecc_blocks(ctxt);
}

struct add_ecc_iterate {
	struct add_ecc_context *ic_ctxt;
	struct ocfs2_dinode *ic_di;
};

static int chain_iterate(ocfs2_filesys *fs, uint64_t gd_blkno,
			 int chain_num, void *priv_data)
{
	struct add_ecc_iterate *iter = priv_data;
	struct ocfs2_group_desc *gd = NULL;
	errcode_t ret;
	int iret = 0;

	ret = ocfs2_malloc_block(fs->fs_io, &gd);
	if (ret)
		goto out;

	verbosef(VL_DEBUG, "Reading group descriptor at %"PRIu64"\n",
		 gd_blkno);
	ret = ocfs2_read_group_desc(fs, gd_blkno, (char *)gd);
	if (ret)
		goto out;

	ret = block_insert_gd(fs, iter->ic_ctxt, iter->ic_di, gd);

out:
	if (gd)
		ocfs2_free(&gd);
	if (ret) {
		iter->ic_ctxt->ae_ret = ret;
		iret = OCFS2_CHAIN_ABORT;
	}

	return iret;
}

/*
 * Right now, this only handles directory data.  Quota stuff will want
 * to genericize this or copy it.
 */
static int dirdata_iterate(ocfs2_filesys *fs, struct ocfs2_extent_rec *rec,
			   int tree_depth, uint32_t ccount,
			   uint64_t ref_blkno, int ref_recno,
			   void *priv_data)
{
	errcode_t ret = 0;
	struct add_ecc_iterate *iter = priv_data;
	char *buf = NULL;
	struct ocfs2_extent_block *eb;
	int iret = 0;
	uint64_t blocks, i;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		goto out;

	if (tree_depth) {
		verbosef(VL_DEBUG, "Reading extent block at %"PRIu64"\n",
			 rec->e_blkno);
		eb = (struct ocfs2_extent_block *)buf;
		ret = ocfs2_read_extent_block(fs, rec->e_blkno, (char *)eb);
		if (ret)
			goto out;

		ret = block_insert_eb(fs, iter->ic_ctxt, iter->ic_di, eb);
	} else {
		blocks = ocfs2_clusters_to_blocks(fs, rec->e_leaf_clusters);
		for (i = 0; i < blocks; i++) {
			ret = ocfs2_read_dir_block(fs, iter->ic_di,
						   rec->e_blkno + i, buf);
			if (ret)
				break;

			ret = block_insert_dirblock(fs, iter->ic_ctxt,
						    iter->ic_di,
						    rec->e_blkno + i, buf);
			if (ret)
				break;
		}
	}

out:
	if (buf)
		ocfs2_free(&buf);
	if (ret) {
		iter->ic_ctxt->ae_ret = ret;
		iret = OCFS2_EXTENT_ABORT;
	}

	return iret;
}

static int metadata_iterate(ocfs2_filesys *fs, struct ocfs2_extent_rec *rec,
			    int tree_depth, uint32_t ccount,
			    uint64_t ref_blkno, int ref_recno,
			    void *priv_data)
{
	errcode_t ret = 0;
	struct add_ecc_iterate *iter = priv_data;
	struct ocfs2_extent_block *eb = NULL;
	int iret = 0;

	if (!tree_depth)
		goto out;

	ret = ocfs2_malloc_block(fs->fs_io, &eb);
	if (ret)
		goto out;

	verbosef(VL_DEBUG, "Reading extent block at %"PRIu64"\n",
		 rec->e_blkno);
	ret = ocfs2_read_extent_block(fs, rec->e_blkno, (char *)eb);
	if (ret)
		goto out;

	ret = block_insert_eb(fs, iter->ic_ctxt, iter->ic_di, eb);

out:
	if (eb)
		ocfs2_free(&eb);
	if (ret) {
		iter->ic_ctxt->ae_ret = ret;
		iret = OCFS2_EXTENT_ABORT;
	}

	return iret;
}

/*
 * This walks all the chain allocators we've stored off and adds their
 * blocks to the list.
 */
static errcode_t find_chain_blocks(ocfs2_filesys *fs,
				   struct add_ecc_context *ctxt)
{
	errcode_t ret;
	struct list_head *pos;
	struct chain_to_ecc *cte;
	struct ocfs2_dinode *di;
	struct block_to_ecc *block;
	char *buf = NULL;
	struct tools_progress *prog;
	struct add_ecc_iterate iter = {
		.ic_ctxt = ctxt,
	};

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		goto out;

	prog = tools_progress_start("Scanning allocators", "chains",
				    ctxt->ae_chaincount);
	if (!prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		goto out;
	}

	list_for_each(pos, &ctxt->ae_chains) {
		cte = list_entry(pos, struct chain_to_ecc, ce_list);
		ret = ocfs2_read_inode(fs, cte->ce_blkno, buf);
		if (ret)
			break;

		di = (struct ocfs2_dinode *)buf;
		ret = block_insert_dinode(fs, ctxt, di);
		if (ret)
			break;

		/* We need the inode, look it back up */
		block = block_lookup(ctxt, di->i_blkno);
		if (!block) {
			ret = TUNEFS_ET_INTERNAL_FAILURE;
			break;
		}

		/* Now using our copy of the inode */
		di = (struct ocfs2_dinode *)block->e_buf;
		assert(di->i_blkno == cte->ce_blkno);

		iter.ic_di = di;
		ret = ocfs2_chain_iterate(fs, di->i_blkno, chain_iterate,
					  &iter);
		if (ret)
			break;
		tools_progress_step(prog, 1);
	}

	tools_progress_stop(prog);

out:
	if (buf)
		ocfs2_free(&buf);

	return ret;
}


static errcode_t inode_iterate(ocfs2_filesys *fs, struct ocfs2_dinode *di,
			       void *user_data)
{
	errcode_t ret;
	struct block_to_ecc *block = NULL;
	struct tunefs_trailer_context *tc;
	struct add_ecc_context *ctxt = user_data;
	struct add_ecc_iterate iter = {
		.ic_ctxt = ctxt,
	};

	/*
	 * We have to handle chain allocators later, after the dir
	 * trailer code has done any allocation it needs.
	 */
	if (di->i_flags & OCFS2_CHAIN_FL) {
		ret = add_ecc_chain(ctxt, di->i_blkno);
		goto out;
	}

	ret = block_insert_dinode(fs, ctxt, di);
	if (ret)
		goto out;

	/* These inodes have no other metadata on them */
	if ((di->i_flags & (OCFS2_SUPER_BLOCK_FL | OCFS2_LOCAL_ALLOC_FL |
			    OCFS2_DEALLOC_FL)) ||
	    (S_ISLNK(di->i_mode) && di->i_clusters == 0) ||
	    (di->i_dyn_features & OCFS2_INLINE_DATA_FL))
		goto out;

	/* We need the inode, look it back up */
	block = block_lookup(ctxt, di->i_blkno);
	if (!block) {
		ret = TUNEFS_ET_INTERNAL_FAILURE;
		goto out;
	}

	/* Now using our copy of the inode */
	di = (struct ocfs2_dinode *)block->e_buf;
	iter.ic_di = di;

	/*
	 * Ok, it's a regular file or directory.
	 * If it's a regular file, gather extent blocks for this inode.
	 * If it's a directory that has trailers, we need to gather all
	 * of its blocks, data and metadata.
	 *
	 * We don't gather extent info for directories that need trailers
	 * yet, because they might get modified as they gain trailers.
	 * We'll add them after we insert their trailers.
	 */
	if (!S_ISDIR(di->i_mode))
		ret = ocfs2_extent_iterate_inode(fs, di, 0, NULL,
						 metadata_iterate, &iter);
	else if (ocfs2_dir_has_trailer(fs, di))
		ret = ocfs2_extent_iterate_inode(fs, di, 0, NULL,
						 dirdata_iterate, &iter);
	else {
		ret = tunefs_prepare_dir_trailer(fs, di, &tc);
		if (!ret) {
			verbosef(VL_DEBUG,
				 "Directory %"PRIu64" needs %"PRIu64" "
				 "more blocks\n",
				 tc->d_blkno, tc->d_blocks_needed);
			list_add(&tc->d_list, &ctxt->ae_dirs);
			ctxt->ae_dircount++;
			ctxt->ae_clusters +=
				ocfs2_clusters_in_blocks(fs,
							 tc->d_blocks_needed);
		}
	}

out:
	tools_progress_step(ctxt->ae_prog, 1);

	return ret;
}

static errcode_t find_blocks(ocfs2_filesys *fs, struct add_ecc_context *ctxt)
{
	errcode_t ret;
	uint32_t free_clusters = 0;

	ctxt->ae_prog = tools_progress_start("Scanning filesystem",
					     "scanning", 0);
	if (!ctxt->ae_prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		goto bail;
	}

	ret = tunefs_foreach_inode(fs, inode_iterate, ctxt);
	if (ret)
		goto bail;
	tools_progress_stop(ctxt->ae_prog);
	ctxt->ae_prog = NULL;

	ret = tunefs_get_free_clusters(fs, &free_clusters);
	if (ret)
		goto bail;

	verbosef(VL_APP,
		 "We have %u clusters free, and need %u clusters to add "
		 "trailers to every directory\n",
		 free_clusters, ctxt->ae_clusters);

	if (free_clusters < ctxt->ae_clusters)
		ret = OCFS2_ET_NO_SPACE;

bail:
	if (ctxt->ae_prog)
		tools_progress_stop(ctxt->ae_prog);
	return ret;
}

static errcode_t install_trailers(ocfs2_filesys *fs,
				  struct add_ecc_context *ctxt)
{
	errcode_t ret = 0;
	struct tunefs_trailer_context *tc;
	struct list_head *n, *pos;
	struct tools_progress *prog;
	struct add_ecc_iterate iter = {
		.ic_ctxt = ctxt,
	};

	prog = tools_progress_start("Installing dir trailers",
				    "trailers", ctxt->ae_dircount);
	list_for_each_safe(pos, n, &ctxt->ae_dirs) {
		tc = list_entry(pos, struct tunefs_trailer_context, d_list);
		verbosef(VL_DEBUG,
			 "Writing trailer for dinode %"PRIu64"\n",
			 tc->d_di->i_blkno);
		tunefs_block_signals();
		ret = tunefs_install_dir_trailer(fs, tc->d_di, tc);
		tunefs_unblock_signals();
		if (ret)
			break;

		iter.ic_di = tc->d_di;
		tunefs_trailer_context_free(tc);

		/*
		 * Now that we've put trailers on the directory, we know
		 * its allocation won't change.  Add its blocks to the
		 * block list.  These will be cached, as installing the
		 * trailer will have just touched them.
		 */
		ret = ocfs2_extent_iterate_inode(fs, tc->d_di, 0, NULL,
						 dirdata_iterate, &iter);
		if (ret)
			break;

		tools_progress_step(prog, 1);
	}
	tools_progress_stop(prog);

	return ret;
}

static errcode_t write_ecc_blocks(ocfs2_filesys *fs,
				  struct add_ecc_context *ctxt)
{
	errcode_t ret = 0;
	struct rb_node *n;
	struct block_to_ecc *block;
	struct tools_progress *prog;

	prog = tools_progress_start("Writing blocks", "ECC",
				    ctxt->ae_blockcount);
	if (!prog)
		return TUNEFS_ET_NO_MEMORY;

	n = rb_first(&ctxt->ae_blocks);
	while (n) {
		block = rb_entry(n, struct block_to_ecc, e_node);
		verbosef(VL_DEBUG, "Writing block %"PRIu64"\n",
			 block->e_blkno);

		tools_progress_step(prog, 1);
		ret = block->e_write(fs, block);
		if (ret)
			break;

		n = rb_next(n);
	}
	tools_progress_stop(prog);

	return ret;
}

static int enable_metaecc(ocfs2_filesys *fs, int flags)
{
	errcode_t ret = 0;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);
	struct add_ecc_context ctxt;
	struct tools_progress *prog = NULL;

	if (ocfs2_meta_ecc(super)) {
		verbosef(VL_APP,
			 "The metadata ECC feature is already enabled; "
			 "nothing to enable\n");
		goto out;
	}

	if (!tools_interact("Enable the metadata ECC feature on device "
			    "\"%s\"? ",
			    fs->fs_devname))
		goto out;

	prog = tools_progress_start("Enabling metaecc", "metaecc", 5);
	if (!prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		tcom_err(ret, "while initializing the progress display");
		goto out;
	}

	memset(&ctxt, 0, sizeof(ctxt));
	INIT_LIST_HEAD(&ctxt.ae_dirs);
	INIT_LIST_HEAD(&ctxt.ae_chains);
	ctxt.ae_blocks = RB_ROOT;
	ret = find_blocks(fs, &ctxt);
	if (ret) {
		if (ret == OCFS2_ET_NO_SPACE)
			errorf("There is not enough space to add directory "
			       "trailers to the directories on device "
			       "\"%s\"\n",
			       fs->fs_devname);
		else
			tcom_err(ret,
				 "while trying to find directory blocks");
		goto out_cleanup;
	}
	tools_progress_step(prog, 1);

	ret = tunefs_set_in_progress(fs, OCFS2_TUNEFS_INPROG_DIR_TRAILER);
	if (ret)
		goto out_cleanup;

	ret = install_trailers(fs, &ctxt);
	if (ret) {
		tcom_err(ret,
			 "while trying to install directory trailers on "
			 "device \"%s\"",
			 fs->fs_devname);
		goto out_cleanup;
	}

	ret = tunefs_clear_in_progress(fs, OCFS2_TUNEFS_INPROG_DIR_TRAILER);
	if (ret)
		goto out_cleanup;

	tools_progress_step(prog, 1);

	/* We're done with allocation, scan the chain allocators */
	ret = find_chain_blocks(fs, &ctxt);
	if (ret)
		goto out_cleanup;

	tools_progress_step(prog, 1);

	/* Set the feature bit in-memory and rewrite all our blocks */
	OCFS2_SET_INCOMPAT_FEATURE(super, OCFS2_FEATURE_INCOMPAT_META_ECC);
	ret = write_ecc_blocks(fs, &ctxt);
	if (ret)
		goto out_cleanup;

	tools_progress_step(prog, 1);

	tunefs_block_signals();
	ret = ocfs2_write_super(fs);
	tunefs_unblock_signals();
	if (ret)
		tcom_err(ret, "while writing out the superblock");

	tools_progress_step(prog, 1);

out_cleanup:
	empty_add_ecc_context(&ctxt);

out:
	if (prog)
		tools_progress_stop(prog);

	return ret;
}

static int disable_metaecc(ocfs2_filesys *fs, int flags)
{
	errcode_t ret = 0;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);
	struct tools_progress *prog = NULL;

	if (!ocfs2_meta_ecc(super)) {
		verbosef(VL_APP,
			 "The metadata ECC feature is not enabled; "
			 "nothing to disable\n");
		goto out;
	}

	if (!tools_interact("Disable the metadata ECC feature on device "
			    "\"%s\"? ",
			    fs->fs_devname))
		goto out;

	prog = tools_progress_start("Disabling metaecc", "nometaecc", 1);
	if (!prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		goto out;
	}


	OCFS2_CLEAR_INCOMPAT_FEATURE(super,
				     OCFS2_FEATURE_INCOMPAT_META_ECC);
	tunefs_block_signals();
	ret = ocfs2_write_super(fs);
	tunefs_unblock_signals();
	if (ret)
		tcom_err(ret, "while writing out the superblock");

	tools_progress_step(prog, 1);

out:
	if (prog)
		tools_progress_stop(prog);

	return ret;
}


DEFINE_TUNEFS_FEATURE_INCOMPAT(metaecc,
			       OCFS2_FEATURE_INCOMPAT_META_ECC,
			       TUNEFS_FLAG_RW | TUNEFS_FLAG_ALLOCATION |
			       TUNEFS_FLAG_LARGECACHE,
			       enable_metaecc,
			       disable_metaecc);

#ifdef DEBUG_EXE
int main(int argc, char *argv[])
{
	return tunefs_feature_main(argc, argv, &metaecc_feature);
}
#endif
