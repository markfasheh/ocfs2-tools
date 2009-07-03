/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * journal.c
 *
 * Copyright (C) 2004, 2008 Oracle.  All rights reserved.
 *
 * Corruptions for journal system file.
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

#include "main.h"

extern char *progname;

/* This file will corrupt jorunal system file.
 *
 * Journal Error: JOURNAL_FILE_INVALID, JOURNAL_UNKNOWN_FEATURE,
 * JOURNAL_MISSING_FEATURE, JOURNAL_TOO_SMALL.
 *
 */
void mess_up_journal(ocfs2_filesys *fs, enum fsck_type type, uint16_t slotnum)
{
	errcode_t ret;
	uint64_t j_blkno, jsb_blkno;
	uint64_t contig;
	uint32_t old_no;
	uint16_t max_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;
	ocfs2_cached_inode *ci = NULL;
	char *buf = NULL;
	journal_superblock_t *jsb;

	uint64_t tmp_j_blkno, tmp_jsb_blkno;
	uint64_t tmp_contig;
	uint16_t adj_slotnum;
	ocfs2_cached_inode *tmp_ci = NULL;
	journal_superblock_t *tmp_jsb;
	char *tmp_buf = NULL;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_lookup_system_inode(fs, JOURNAL_SYSTEM_INODE,
					slotnum, &j_blkno);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_read_cached_inode(fs, j_blkno, &ci);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_extent_map_get_blocks(ci, 0, 1, &jsb_blkno, &contig, NULL);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_read_journal_superblock(fs, jsb_blkno, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	jsb = (journal_superblock_t *)buf;

	switch (type) {
	case JOURNAL_FILE_INVALID:
		old_no = JBD2_MAGIC_NUMBER;
		jsb->s_header.h_magic = ~JBD2_MAGIC_NUMBER;
		fprintf(stdout, "JOURNAL_FILE_INVALID: "
			"Corrupt journal system inode#%"PRIu64"'s "
			"superblock's magic number from %x to %x.\n",
			j_blkno, old_no, jsb->s_header.h_magic);
		break;
	case JOURNAL_UNKNOWN_FEATURE:
		jsb->s_feature_incompat |= ~JBD2_KNOWN_INCOMPAT_FEATURES;
		jsb->s_feature_ro_compat |= ~JBD2_KNOWN_ROCOMPAT_FEATURES;
		fprintf(stdout, "JOURNAL_UNKNOWN_FEATURE: "
			"Corrupt journal system inode#%"PRIu64" by "
			"adding unsupported features.\n", j_blkno);
		break;
	case JOURNAL_MISSING_FEATURE:
		/* First of all, set all supported features for
		 * another journal file if existed, therefore, we
		 * easily let the journal file with current slotnum
		 * become features-missing when compared to the rest
		 * of journal files.
		 */
		if (max_slots == 1)
			FSWRK_FATAL("should specfiy a volume with multiple "
				    "slots to do this corruption\n");
		else
			adj_slotnum = (slotnum + 1 > max_slots - 1) ?
				       slotnum - 1 : slotnum + 1;

		ret = ocfs2_malloc_block(fs->fs_io, &tmp_buf);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);

		ret = ocfs2_lookup_system_inode(fs, JOURNAL_SYSTEM_INODE,
						adj_slotnum, &tmp_j_blkno);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);

		ret = ocfs2_read_cached_inode(fs, tmp_j_blkno, &tmp_ci);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);

		ret = ocfs2_extent_map_get_blocks(tmp_ci, 0, 1, &tmp_jsb_blkno,
						  &tmp_contig, NULL);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);

		ret = ocfs2_read_journal_superblock(fs, tmp_jsb_blkno, tmp_buf);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);

		tmp_jsb = (journal_superblock_t *)tmp_buf;

		tmp_jsb->s_feature_compat |= JBD2_KNOWN_COMPAT_FEATURES;
		tmp_jsb->s_feature_incompat |= JBD2_KNOWN_INCOMPAT_FEATURES;
		tmp_jsb->s_feature_ro_compat |= JBD2_KNOWN_ROCOMPAT_FEATURES;

		ret = ocfs2_write_journal_superblock(fs, tmp_jsb_blkno,
						     tmp_buf);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);

		if (tmp_buf)
			ocfs2_free(&tmp_buf);
		if (tmp_ci)
			ocfs2_free_cached_inode(fs, tmp_ci);
		/*
		 * Clear features bits for journal file with
		 * current slot number.
		 */
		jsb->s_feature_compat &= ~JBD2_KNOWN_COMPAT_FEATURES;
		jsb->s_feature_incompat &= ~JBD2_KNOWN_INCOMPAT_FEATURES;
		jsb->s_feature_ro_compat &= ~JBD2_KNOWN_ROCOMPAT_FEATURES;
		fprintf(stdout, "JOURNAL_MISSING_FEATURE: "
			"Corrupt journal system inode#%"PRIu64" by "
			"removing supported features.\n", j_blkno);
		break;
	case JOURNAL_TOO_SMALL:
		old_no = ci->ci_inode->i_clusters;
		ci->ci_inode->i_clusters = 0;
		fprintf(stdout, "JOURNAL_TOO_SMALL: "
			"Corrupt journal system inode#%"PRIu64"'s "
			"i_clusters from %u to zero.\n", j_blkno, old_no);
		break;
	default:
		FSWRK_FATAL("Invalid type[%d]\n", type);
	}

	ret = ocfs2_write_journal_superblock(fs, jsb_blkno, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_write_cached_inode(fs, ci);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	if (buf)
		ocfs2_free(&buf);
	if (ci)
		ocfs2_free_cached_inode(fs, ci);

	return;
}
