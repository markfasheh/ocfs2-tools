/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * feature_discontig_alloc.c
 *
 * ocfs2 tune utility for enabling and disabling the discontig_alloc feature.
 *
 * Copyright (C) 2010 Oracle.  All rights reserved.
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

#include "ocfs2/ocfs2.h"

#include "libocfs2ne.h"

static int enable_discontig_bg(ocfs2_filesys *fs, int flags)
{
	errcode_t ret = 0;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);
	struct tools_progress *prog;

	if (ocfs2_supports_discontig_bg(super)) {
		verbosef(VL_APP,
			 "Discontiguous block group feature is already enabled;"
			 " nothing to enable\n");
		goto out;
	}

	if (!tools_interact("Enable the discontiguous block group feature on "
			    "device \"%s\"? ",
			    fs->fs_devname))
		goto out;

	prog = tools_progress_start("Enable discontig block group",
				    "discontig bg", 1);
	if (!prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		tcom_err(ret, "while initializing the progress display");
		goto out;
	}

	OCFS2_SET_INCOMPAT_FEATURE(super, OCFS2_FEATURE_INCOMPAT_DISCONTIG_BG);
	tunefs_block_signals();
	ret = ocfs2_write_super(fs);
	tunefs_unblock_signals();
	if (ret)
		tcom_err(ret, "while writing out the superblock");

	tools_progress_step(prog, 1);
	tools_progress_stop(prog);
out:
	return ret;
}

struct discontig_bg {
	uint64_t bg_blkno;
	struct discontig_bg *next;
};

struct no_discontig_bg_ctxt {
	ocfs2_filesys *fs;
	struct tools_progress *prog;
	char *bg_buf;
	errcode_t ret;
	int has_discontig;
	struct discontig_bg *bg_list;
};

/*
 * Check whether the gd_blkno is a discontig block group, and
 * if yes set has_discontig and abort.
 * It also check whether bg_size is a new value, if yes, add
 * it to the list so that we can change it later.
 */
static int check_discontig_bg(ocfs2_filesys *fs, uint64_t gd_blkno,
			     int chain_num, void *priv_data)
{
	struct no_discontig_bg_ctxt *ctxt = priv_data;
	struct ocfs2_group_desc *gd;
	struct discontig_bg *bg;

	ctxt->ret = ocfs2_read_group_desc(fs, gd_blkno, ctxt->bg_buf);
	if (ctxt->ret) {
		tcom_err(ctxt->ret, "while reading group descriptor %"PRIu64,
			 gd_blkno);
		return OCFS2_CHAIN_ERROR;
	}

	gd = (struct ocfs2_group_desc *)ctxt->bg_buf;

	if (ocfs2_gd_is_discontig(gd)) {
		ctxt->has_discontig = 1;
		return OCFS2_CHAIN_ABORT;
	}

	if (gd->bg_size == ocfs2_group_bitmap_size(fs->fs_blocksize, 0,
			OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat))
		return 0;

	/*
	 * OK, now the gd isn't discontiguous while bg_size has
	 * the new size. Record it so that we can change it later.
	 */
	ctxt->ret = ocfs2_malloc0(sizeof(struct discontig_bg), &bg);
	if (ctxt->ret) {
		tcom_err(ctxt->ret, "while allocating discontig_bg");
		return OCFS2_CHAIN_ABORT;
	}

	bg->bg_blkno = gd_blkno;
	bg->next = ctxt->bg_list;
	ctxt->bg_list = bg;
	return 0;
}

static errcode_t find_discontig_bg(struct no_discontig_bg_ctxt *ctxt)
{
	int i, iret;
	uint64_t blkno;

	ctxt->prog = tools_progress_start("Scanning suballocators",
					  "scanning", 0);
	if (!ctxt->prog) {
		ctxt->ret = TUNEFS_ET_NO_MEMORY;
		tcom_err(ctxt->ret, "while initializing the progress display");
		goto out;
	}

	/* iterate every inode_alloc first. */
	for (i = 0; i < OCFS2_RAW_SB(ctxt->fs->fs_super)->s_max_slots; i++) {
		ctxt->ret = ocfs2_lookup_system_inode(ctxt->fs,
						INODE_ALLOC_SYSTEM_INODE,
						i, &blkno);
		if (ctxt->ret) {
			tcom_err(ctxt->ret, "while finding inode alloc %d", i);
			goto out;
		}

		iret = ocfs2_chain_iterate(ctxt->fs, blkno,
					   check_discontig_bg, ctxt);
		if (ctxt->ret) {
			tcom_err(ctxt->ret, "while iterating inode alloc "
				 "%d", i);
			goto out;
		}
		if (iret == OCFS2_CHAIN_ABORT || iret == OCFS2_CHAIN_ERROR)
			goto out;
		tools_progress_step(ctxt->prog, 1);
	}

	/* iterate every extent_alloc now. */
	for (i = 0; i < OCFS2_RAW_SB(ctxt->fs->fs_super)->s_max_slots; i++) {
		ctxt->ret = ocfs2_lookup_system_inode(ctxt->fs,
						EXTENT_ALLOC_SYSTEM_INODE,
						i, &blkno);
		if (ctxt->ret) {
			tcom_err(ctxt->ret, "while finding extent alloc %d", i);
			goto out;
		}

		iret = ocfs2_chain_iterate(ctxt->fs, blkno,
					   check_discontig_bg, ctxt);
		if (ctxt->ret) {
			tcom_err(ctxt->ret, "while iterating extent alloc "
				 "%d", i);
			goto out;
		}
		if (iret == OCFS2_CHAIN_ABORT || iret == OCFS2_CHAIN_ERROR)
			goto out;
		tools_progress_step(ctxt->prog, 1);
	}

out:
	if (ctxt->prog) {
		tools_progress_stop(ctxt->prog);
		ctxt->prog = NULL;
	}

	return ctxt->ret;
}

static errcode_t change_bg_size(struct no_discontig_bg_ctxt *ctxt)
{
	errcode_t ret = 0;
	uint64_t bg_blkno;
	struct discontig_bg *bg;
	struct ocfs2_group_desc *gd;

	while (ctxt->bg_list) {
		bg = ctxt->bg_list;
		ctxt->bg_list = bg->next;
		bg_blkno = bg->bg_blkno;
		ocfs2_free(&bg);

		ret = ocfs2_read_group_desc(ctxt->fs, bg_blkno, ctxt->bg_buf);
		if (ret) {
			tcom_err(ctxt->ret, "while reading group descriptor "
				 "%"PRIu64, bg_blkno);
			goto out;
		}

		gd = (struct ocfs2_group_desc *)ctxt->bg_buf;

		gd->bg_size = ocfs2_group_bitmap_size(ctxt->fs->fs_blocksize,
						      0, 0);

		ret = ocfs2_write_group_desc(ctxt->fs,
					     bg_blkno, ctxt->bg_buf);
		if (ret) {
			tcom_err(ctxt->ret, "while writing group descriptor "
				 "%"PRIu64, bg->bg_blkno);
			goto out;
		}
	}

out:
	return ret;
}

static int disable_discontig_bg(ocfs2_filesys *fs, int flags)
{
	errcode_t ret = 0;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);
	struct tools_progress *prog = NULL;
	struct no_discontig_bg_ctxt ctxt;
	struct discontig_bg *tmp;

	memset(&ctxt, 0, sizeof(ctxt));

	if (!ocfs2_supports_discontig_bg(super)) {
		verbosef(VL_APP,
			 "Discontiguous block group feature is already "
			 "disabled; nothing to disable\n");
		goto out;
	}

	if (!tools_interact("Disable the discontiguous block group feature on "
			    "device \"%s\"? ",
			    fs->fs_devname))
		goto out;

	prog = tools_progress_start("Enable discontig block group",
				    "nodiscontig-bg", 4);
	if (!prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		tcom_err(ret, "while initializing the progress display");
		goto out;
	}

	ctxt.fs = fs;
	ret = ocfs2_malloc_block(fs->fs_io, &ctxt.bg_buf);
	if (ret) {
		tcom_err(ret, "while mallocing blocks for group read");
		goto out;
	}

	ret = find_discontig_bg(&ctxt);
	if (ret) {
		tcom_err(ret, "while finding discontiguous block group");
		goto out;
	}

	tools_progress_step(prog, 1);

	if (ctxt.has_discontig) {
		tcom_err(0, "We can't disable discontig feature while "
			 "we have some discontiguous block groups");
		goto out;
	}

	ret = change_bg_size(&ctxt);
	if (ret) {
		tcom_err(ret, "while changing bg size for block group");
		goto out;
	}

	OCFS2_CLEAR_INCOMPAT_FEATURE(super,
				     OCFS2_FEATURE_INCOMPAT_DISCONTIG_BG);
	tunefs_block_signals();
	ret = ocfs2_write_super(fs);
	tunefs_unblock_signals();
	if (ret)
		tcom_err(ret, "while writing out the superblock");

	tools_progress_step(prog, 1);
out:
	if (ctxt.bg_buf)
		ocfs2_free(&ctxt.bg_buf);
	while (ctxt.bg_list) {
		tmp = ctxt.bg_list;
		ctxt.bg_list = tmp->next;
		ocfs2_free(&tmp);
	}

	if (prog)
		tools_progress_stop(prog);
	return ret;
}

DEFINE_TUNEFS_FEATURE_INCOMPAT(discontig_bg,
			       OCFS2_FEATURE_INCOMPAT_DISCONTIG_BG,
			       TUNEFS_FLAG_RW | TUNEFS_FLAG_ALLOCATION |
			       TUNEFS_FLAG_LARGECACHE,
			       enable_discontig_bg,
			       disable_discontig_bg);

#ifdef DEBUG_EXE
int main(int argc, char *argv[])
{
	return tunefs_feature_main(argc, argv, &discontig_bg_feature);
}
#endif
