/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * quota.c
 *
 * Copyright (C) 2004, 2008 Oracle.  All rights reserved.
 *
 * Corruptions for quota system file.
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
static int g_actref;

/* This file will corrupt quota system file.
 *
 * Quota Error: QMAGIC_INVALID, QTREE_BLK_INVALID, DQBLK_INVALID
 *		DUP_DQBLK_INVALID, DUP_DQBLK_VALID
 *
 */

static char *type2name(int type)
{
	if (type == USRQUOTA)
		return "user";

	return "group";
}

static errcode_t o2fswreck_read_blk(ocfs2_filesys *fs, int type, char *buf,
				    uint32_t blk)
{
	uint32_t got;
	errcode_t ret;

	ret = ocfs2_file_read(fs->qinfo[type].qi_inode, buf, fs->fs_blocksize,
			      blk * fs->fs_blocksize, &got);
	if (ret)
		return ret;
	if (got != fs->fs_blocksize)
		return OCFS2_ET_SHORT_READ;

	return 0;
}

static errcode_t o2fswreck_write_blk(ocfs2_filesys *fs, int type, char *buf,
				     uint32_t blk)
{
	errcode_t err;
	uint32_t written;

	err = ocfs2_file_write(fs->qinfo[type].qi_inode, buf, fs->fs_blocksize,
			       blk * fs->fs_blocksize, &written);
	if (err)
		return err;
	if (written != fs->fs_blocksize)
		return OCFS2_ET_SHORT_WRITE;

	return 0;
}


static errcode_t o2fswreck_get_data_blk(ocfs2_filesys *fs, int type,
					uint32_t blk, int depth,
					char *buf)
{
	errcode_t ret;
	int epb = (fs->fs_blocksize - OCFS2_QBLK_RESERVED_SPACE) >> 2;
	int tree_depth = ocfs2_qtree_depth(fs->fs_blocksize);
	uint32_t *refs = (uint32_t *)buf, actref;
	int i;

	ret = o2fswreck_read_blk(fs, type, buf, blk);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	for (i = 0; i < epb; i++) {

		actref = le32_to_cpu(refs[i]);
		if (!actref)
			continue;

		if (depth + 1 < tree_depth) {
			ret = o2fswreck_get_data_blk(fs, type, actref,
						     depth + 1,
						     buf + fs->fs_blocksize);
		} else {

			ret = o2fswreck_read_blk(fs, type,
						 buf + fs->fs_blocksize,
						 actref);
			if (ret)
				FSWRK_COM_FATAL(progname, ret);

			g_actref = actref;

			return 0;
		}

		if (ret)
			goto out;
	}

out:
	return ret;
}


void mess_up_quota(ocfs2_filesys *fs, enum fsck_type type, uint16_t slotnum)
{
	errcode_t ret;
	int qtype;
	char *buf;
	int tree_depth = ocfs2_qtree_depth(fs->fs_blocksize);

	struct ocfs2_disk_dqheader *header;
	struct ocfs2_global_disk_dqblk *ddquot;
	struct qt_disk_dqdbheader *dh;

	ret = ocfs2_init_fs_quota_info(fs, USRQUOTA);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_init_fs_quota_info(fs, GRPQUOTA);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_malloc_blocks(fs->fs_io,
				  tree_depth + 1,
				  &buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	switch (type) {
	case  QMAGIC_INVALID:
		qtype = USRQUOTA;

		ret = o2fswreck_read_blk(fs, qtype, buf, 0);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);

		header = (struct ocfs2_disk_dqheader *)buf;

		ocfs2_swap_quota_header(header);

		header->dqh_magic = ~header->dqh_magic;

		ocfs2_swap_quota_header(header);

		ret = o2fswreck_write_blk(fs, qtype, buf, 0);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);

		fprintf(stdout, "QMAGIC_INVALID: "
			"Corrupt global %s quota file's magic number "
			"in its header.\n", type2name(qtype));
		break;
	case QTREE_BLK_INVALID:
		qtype = GRPQUOTA;

		ret = o2fswreck_read_blk(fs, qtype, buf, QT_TREEOFF);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);

		struct ocfs2_disk_dqtrailer *dqt =
				ocfs2_block_dqtrailer(fs->fs_blocksize, buf);

		dqt->dq_check.bc_crc32e = ~dqt->dq_check.bc_crc32e;
		dqt->dq_check.bc_ecc = ~dqt->dq_check.bc_ecc;

		ret = o2fswreck_write_blk(fs, qtype, buf, QT_TREEOFF);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);

		fprintf(stdout, "QTREE_BLK_INVALID: "
			"Corrupt global %s quota tree block.\n",
			type2name(qtype));
		break;
	case DQBLK_INVALID:
		qtype = USRQUOTA;

		ret = o2fswreck_get_data_blk(fs, qtype, QT_TREEOFF, 0, buf);

		ddquot = (struct ocfs2_global_disk_dqblk *)(buf +
					fs->fs_blocksize * tree_depth +
					sizeof(struct qt_disk_dqdbheader));

		ocfs2_swap_quota_global_dqblk(ddquot);

		ddquot->dqb_id = 0xFFFFFFF6;

		ddquot->dqb_isoftlimit += 1;
		ddquot->dqb_ihardlimit += 2;
		ddquot->dqb_bsoftlimit += 3;
		ddquot->dqb_bhardlimit += 4;

		ocfs2_swap_quota_global_dqblk(ddquot);

		dh = (struct qt_disk_dqdbheader *)(buf +
				fs->fs_blocksize * tree_depth);

		ocfs2_swap_quota_leaf_block_header(dh);
		dh->dqdh_next_free = 0xFFFFFFFF;
		dh->dqdh_prev_free = 0xFFFFFFFF;
		dh->dqdh_entries = 0xFFFFFFFF;
		ocfs2_swap_quota_leaf_block_header(dh);

		ret = o2fswreck_write_blk(fs, qtype,
					  buf + fs->fs_blocksize * tree_depth,
					  g_actref);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);

		fprintf(stdout, "DQBLK_INVALID: "
			"Corrupt global %s quota data block.\n",
			type2name(qtype));

		break;
	case DUP_DQBLK_INVALID:
		qtype = GRPQUOTA;

		ret = o2fswreck_get_data_blk(fs, qtype, QT_TREEOFF, 0, buf);

		ddquot = (struct ocfs2_global_disk_dqblk *)(buf +
					fs->fs_blocksize * tree_depth +
					sizeof(struct qt_disk_dqdbheader));

		ddquot[1].dqb_id = ddquot[0].dqb_id;

		ddquot[1].dqb_isoftlimit += 1;
		ddquot[1].dqb_ihardlimit += 2;
		ddquot[1].dqb_bsoftlimit += 3;
		ddquot[1].dqb_bhardlimit += 4;

		dh = (struct qt_disk_dqdbheader *)(buf +
				fs->fs_blocksize * tree_depth);

		ocfs2_swap_quota_leaf_block_header(dh);
		dh->dqdh_next_free = 0xFFFFFFFF;
		dh->dqdh_prev_free = 0xFFFFFFFF;
		dh->dqdh_entries = 0xFFFFFFFF;
		ocfs2_swap_quota_leaf_block_header(dh);

		ret = o2fswreck_write_blk(fs, qtype,
					  buf + fs->fs_blocksize * tree_depth,
					  g_actref);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);

		fprintf(stdout, "DUP_DQBLK_INVALID: "
			"Duplicate %s quota data block with a invalid entry.\n",
			type2name(qtype));

		break;
	case DUP_DQBLK_VALID:
		qtype = GRPQUOTA;

		ret = o2fswreck_get_data_blk(fs, qtype, QT_TREEOFF, 0, buf);

		ddquot = (struct ocfs2_global_disk_dqblk *)(buf +
					fs->fs_blocksize * tree_depth +
					sizeof(struct qt_disk_dqdbheader));

		ddquot[1].dqb_id = ddquot[0].dqb_id;
		ddquot[1].dqb_isoftlimit = ddquot[0].dqb_isoftlimit;
		ddquot[1].dqb_ihardlimit = ddquot[0].dqb_isoftlimit;
		ddquot[1].dqb_bsoftlimit = ddquot[0].dqb_isoftlimit;
		ddquot[1].dqb_bhardlimit = ddquot[0].dqb_isoftlimit;

		ret = o2fswreck_write_blk(fs, qtype,
					  buf + fs->fs_blocksize * tree_depth,
					  g_actref);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);

		fprintf(stdout, "DUP_DQBLK_VALID: "
			"Duplicate %s quota data block with a valid entry.\n",
			type2name(qtype));

		break;
	default:
		FSWRK_FATAL("Invalid type[%d]\n", type);
	}

	if (buf)
		ocfs2_free(&buf);
}
