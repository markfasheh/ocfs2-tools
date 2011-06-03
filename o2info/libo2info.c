/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * libo2info.c
 *
 * Shared routines for the ocfs2 o2info utility
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

#define _XOPEN_SOURCE 600
#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE

#include "ocfs2/ocfs2.h"
#include "tools-internal/verbose.h"
#include "libo2info.h"

int o2info_get_fs_features(ocfs2_filesys *fs, struct o2info_fs_features *ofs)
{
	int rc = 0;
	struct ocfs2_super_block *sb = NULL;

	memset(ofs, 0, sizeof(*ofs));

	sb = OCFS2_RAW_SB(fs->fs_super);
	ofs->compat = sb->s_feature_compat;
	ofs->incompat = sb->s_feature_incompat;
	ofs->rocompat = sb->s_feature_ro_compat;

	return rc;
}

int o2info_get_volinfo(ocfs2_filesys *fs, struct o2info_volinfo *vf)
{
	int rc = 0;
	struct ocfs2_super_block *sb = NULL;

	memset(vf, 0, sizeof(*vf));

	sb = OCFS2_RAW_SB(fs->fs_super);
	vf->blocksize = fs->fs_blocksize;
	vf->clustersize = fs->fs_clustersize;
	vf->maxslots = sb->s_max_slots;
	memcpy(vf->label, sb->s_label, OCFS2_MAX_VOL_LABEL_LEN);
	memcpy(vf->uuid_str, fs->uuid_str, OCFS2_TEXT_UUID_LEN + 1);
	rc = o2info_get_fs_features(fs, &(vf->ofs));

	return rc;
}

int o2info_get_mkfs(ocfs2_filesys *fs, struct o2info_mkfs *oms)
{
	errcode_t err;
	uint64_t blkno;
	char *buf = NULL;
	struct ocfs2_dinode *di = NULL;

	memset(oms, 0, sizeof(*oms));

	err = ocfs2_malloc_block(fs->fs_io, &buf);
	if (err) {
		tcom_err(err, "while allocating buffer");
		goto out;
	}

	err = ocfs2_lookup_system_inode(fs, JOURNAL_SYSTEM_INODE, 0, &blkno);
	if (err) {
		tcom_err(err, "while looking up journal system inode");
		goto out;
	}

	err = ocfs2_read_inode(fs, blkno, buf);
	if (err) {
		tcom_err(err, "while reading journal system inode");
		goto out;
	}

	di = (struct ocfs2_dinode *)buf;
	oms->journal_size = di->i_size;
	err = o2info_get_volinfo(fs, &(oms->ovf));

out:
	if (buf)
		ocfs2_free(&buf);

	return err;
}
