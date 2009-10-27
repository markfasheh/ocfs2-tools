/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * feature_quota.c
 *
 * ocfs2 tune utility for enabling and disabling quota support.
 *
 * Copyright (C) 2008 Novell.  All rights reserved.
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
#include <ctype.h>
#include <inttypes.h>
#include <assert.h>

#include "ocfs2/ocfs2.h"

#include "libocfs2ne.h"

static char *type2name(int type)
{
	if (type == USRQUOTA)
		return "user";
	return "group";
}

static errcode_t create_system_file(ocfs2_filesys *fs, int type, int node)
{
	char fname[OCFS2_MAX_FILENAME_LEN];
	uint64_t blkno;
	errcode_t ret;

	ocfs2_sprintf_system_inode_name(fname, sizeof(fname),
		type, node);
	ret = ocfs2_lookup(fs, fs->fs_sysdir_blkno, fname, strlen(fname), NULL,
			   &blkno);
	if (!ret) {
		verbosef(VL_APP, "System file \"%s\" already exists!\n",
			 fname);
		return 0;
	}
	ret = ocfs2_new_system_inode(fs, &blkno,
				ocfs2_system_inodes[type].si_mode,
				ocfs2_system_inodes[type].si_iflags);
	if (ret) {
		tcom_err(ret, "while creating system file \"%s\"", fname);
		return ret;
	}

	ret = ocfs2_link(fs, fs->fs_sysdir_blkno, fname, blkno,
			 OCFS2_FT_REG_FILE);
	if (ret) {
		tcom_err(ret, "while linking file \"%s\" in the system "
			 "directory", fname);
		return ret;
	}
	return 0;
}

static errcode_t create_quota_files(ocfs2_filesys *fs, int type,
				    struct tools_progress *prog)
{
	ocfs2_quota_hash *hash;
	errcode_t ret;
	int num_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;
	int i;
	int local_type = (type == USRQUOTA) ?
				LOCAL_USER_QUOTA_SYSTEM_INODE :
				LOCAL_GROUP_QUOTA_SYSTEM_INODE;
	int global_type = (type == USRQUOTA) ?
				USER_QUOTA_SYSTEM_INODE :
				GROUP_QUOTA_SYSTEM_INODE;

	verbosef(VL_APP, "Creating %s quota system files\n", type2name(type));
	ret = create_system_file(fs, global_type, 0);
	if (ret)
		return ret;
	for (i = 0; i < num_slots; i++) {
		ret = create_system_file(fs, local_type, i);
		if (ret)
			return ret;
	}
	tools_progress_step(prog, 1);

	verbosef(VL_APP, "Initializing global %s quota file\n",
		 type2name(type));
	ret = ocfs2_init_fs_quota_info(fs, type);
	if (ret) {
		tcom_err(ret, "while looking up global %s quota file",
			 type2name(type));
		return ret;
	}
	fs->qinfo[type].flags = OCFS2_QF_INFO_LOADED;
	fs->qinfo[type].qi_info.dqi_syncms = OCFS2_DEF_QUOTA_SYNC;
	fs->qinfo[type].qi_info.dqi_bgrace = OCFS2_DEF_BLOCK_GRACE;
	fs->qinfo[type].qi_info.dqi_igrace = OCFS2_DEF_INODE_GRACE;

	ret = ocfs2_init_global_quota_file(fs, type);
	if (ret) {
		tcom_err(ret, "while initilizing global %s quota files",
			 type2name(type));
		return ret;
	}
	tools_progress_step(prog, 1);

	verbosef(VL_APP, "Initializing local %s quota files\n",
		 type2name(type));
	ret = ocfs2_init_local_quota_files(fs, type);
	if (ret) {
		tcom_err(ret, "while initilizing local %s quota files",
			 type2name(type));
		return ret;
	}
	tools_progress_step(prog, 1);

	verbosef(VL_APP, "Computing %s quota usage\n",
		 type2name(type));
	ret = ocfs2_new_quota_hash(&hash);
	if (ret) {
		tcom_err(ret, "while creating quota hash");
		return ret;
	}
	if (type == USRQUOTA)
		ret = ocfs2_compute_quota_usage(fs, hash, NULL);
	else
		ret = ocfs2_compute_quota_usage(fs, NULL, hash);
	if (ret) {
		tcom_err(ret, "while scanning filesystem to gather "
			 "quota usage");
		return ret;
	}
	tools_progress_step(prog, 1);

	verbosef(VL_APP, "Write %s quotas to file\n",
		 type2name(type));
	ret = ocfs2_write_release_dquots(fs, type, hash);
	if (ret) {
		tcom_err(ret, "while writing %s quota usage to disk",
			 type2name(type));
		return ret;
	}
	tools_progress_step(prog, 1);

	ret = ocfs2_free_quota_hash(hash);
	if (ret)
		tcom_err(ret, "while freeing quota hash");
	return ret;
}

struct remove_quota_files_ctxt {
	ocfs2_filesys *fs;
	errcode_t err;
	int type;
};

static int remove_quota_files_iterate(struct ocfs2_dir_entry *dirent,
				      int offset, int blocksize, char *buf,
				      void *priv_data)
{
	struct remove_quota_files_ctxt *ctxt = priv_data;
	char dname[OCFS2_MAX_FILENAME_LEN];
	char wname[OCFS2_MAX_FILENAME_LEN];
	errcode_t ret;
	int tail, i;
	int ret_flags = 0;

	strncpy(dname, dirent->name, dirent->name_len);
	dname[dirent->name_len] = 0;

	/* Check whether entry is quota file of type we want - i.e. matching
	 * aquota.user:[0-9][0-9][0-9][0-9] or aquota.user for type == USRQUOTA
	 * and similarly for type == GRPQUOTA */
	strcpy(wname, "aquota.");
	strcat(wname, type2name(ctxt->type));
	tail = strlen(wname);
	if (strncmp(dname, wname, tail))
		return 0;
	if (dname[tail] == ':') {	/* May be local file? */
		tail++;
		for (i = 0; i < 4; i++)
			if (dname[tail + i] < '0' || dname[tail + i] > '9')
				return 0;
		if (dname[tail + i])
			return 0;
	} else if (dname[tail])		/* May be global file? */
		return 0;

	verbosef(VL_APP, "Deleting quota file %s\n",
		 dname);
	ret = ocfs2_truncate(ctxt->fs, dirent->inode, 0);
	if (ret) {
		tcom_err(ret, "while truncating quota file \"%s\"", dname);
		ret_flags |= OCFS2_DIRENT_ERROR;
		ctxt->err = ret;
		goto out;
	}
	ret = ocfs2_delete_inode(ctxt->fs, dirent->inode);
	if (ret) {
		tcom_err(ret, "while deleting quota file \"%s\"", dname);
		ret_flags |= OCFS2_DIRENT_ERROR;
		ctxt->err = ret;
	} else {
		dirent->inode = 0;
		ret_flags |= OCFS2_DIRENT_CHANGED;
	}
out:
	return ret_flags;
}

static errcode_t remove_quota_files(ocfs2_filesys *fs, int type,
				    struct tools_progress *prog)
{
	struct remove_quota_files_ctxt ctxt = {
		.fs = fs,
		.type = type,
		.err = 0,
	};

	ocfs2_dir_iterate(fs, fs->fs_sysdir_blkno,
			  OCFS2_DIRENT_FLAG_EXCLUDE_DOTS, NULL,
			  remove_quota_files_iterate, &ctxt);
	tools_progress_step(prog, 1);
	return ctxt.err;
}

static int enable_usrquota(ocfs2_filesys *fs, int flags)
{
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);
	errcode_t ret;
	struct tools_progress *prog = NULL;

	if (OCFS2_HAS_RO_COMPAT_FEATURE(super,
	    OCFS2_FEATURE_RO_COMPAT_USRQUOTA)) {
		verbosef(VL_APP, "User quotas are already enabled; "
			 "nothing to enable\n");
		return 0;
	}

	if (!tools_interact("Enable user quota feature on device "
			    "\"%s\"? ",
			    fs->fs_devname))
		return 0;

	prog = tools_progress_start("Enabling user quota", "usrquota", 6);
	if (!prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		tcom_err(ret, "while initializing progress display");
		return ret;
	}
	tunefs_block_signals();
	ret = create_quota_files(fs, USRQUOTA, prog);
	if (ret) {
		tcom_err(ret, "while creating user quota files");
		goto bail;
	}
	OCFS2_SET_RO_COMPAT_FEATURE(super,
				    OCFS2_FEATURE_RO_COMPAT_USRQUOTA);
	ret = ocfs2_write_super(fs);
	tools_progress_step(prog, 1);
bail:
	tunefs_unblock_signals();
	tools_progress_stop(prog);
	return ret;
}

static int disable_usrquota(ocfs2_filesys *fs, int flags)
{
	errcode_t ret;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);
	struct tools_progress *prog = NULL;

	if (!OCFS2_HAS_RO_COMPAT_FEATURE(super,
	    OCFS2_FEATURE_RO_COMPAT_USRQUOTA)) {
		verbosef(VL_APP, "User quotas are already disabled; "
			 "nothing to disable\n");
		return 0;
	}

	if (!tools_interact("Disable user quota feature on device "
			    "\"%s\"? ",
			    fs->fs_devname))
		return 0;

	prog = tools_progress_start("Disabling user quota", "nousrquota", 2);
	if (!prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		tcom_err(ret, "while initializing progress display");
		return ret;
	}
	tunefs_block_signals();
	ret = remove_quota_files(fs, USRQUOTA, prog);
	if (ret) {
		tcom_err(ret, "while removing user quota files");
		goto bail;
	}
	OCFS2_CLEAR_RO_COMPAT_FEATURE(super,
				      OCFS2_FEATURE_RO_COMPAT_USRQUOTA);
	ret = ocfs2_write_super(fs);
	tools_progress_step(prog, 1);
bail:
	tunefs_unblock_signals();
	tools_progress_stop(prog);
	return ret;
}

static int enable_grpquota(ocfs2_filesys *fs, int flags)
{
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);
	errcode_t ret;
	struct tools_progress *prog = NULL;

	if (OCFS2_HAS_RO_COMPAT_FEATURE(super,
	    OCFS2_FEATURE_RO_COMPAT_GRPQUOTA)) {
		verbosef(VL_APP, "Group quotas are already enabled; "
			 "nothing to enable\n");
		return 0;
	}

	if (!tools_interact("Enable group quota feature on device "
			    "\"%s\"? ",
			    fs->fs_devname))
		return 0;
	prog = tools_progress_start("Enabling group quota", "grpquota", 6);
	if (!prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		tcom_err(ret, "while initializing progress display");
		return ret;
	}

	tunefs_block_signals();
	ret = create_quota_files(fs, GRPQUOTA, prog);
	if (ret) {
		tcom_err(ret, "while creating group quota files");
		goto bail;
	}
	OCFS2_SET_RO_COMPAT_FEATURE(super,
				    OCFS2_FEATURE_RO_COMPAT_GRPQUOTA);
	ret = ocfs2_write_super(fs);
	tools_progress_step(prog, 1);
bail:
	tools_progress_stop(prog);
	tunefs_unblock_signals();
	return ret;
}

static int disable_grpquota(ocfs2_filesys *fs, int flags)
{
	errcode_t ret;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);
	struct tools_progress *prog = NULL;

	if (!OCFS2_HAS_RO_COMPAT_FEATURE(super,
	    OCFS2_FEATURE_RO_COMPAT_GRPQUOTA)) {
		verbosef(VL_APP, "Group quotas are already disabled; "
			 "nothing to disable\n");
		return 0;
	}

	if (!tools_interact("Disable group quota feature on device "
			    "\"%s\"? ",
			    fs->fs_devname))
		return 0;
	prog = tools_progress_start("Disabling user quota", "nousrquota", 2);
	if (!prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		tcom_err(ret, "while initializing progress display");
		return ret;
	}

	tunefs_block_signals();
	ret = remove_quota_files(fs, GRPQUOTA, prog);
	if (ret) {
		tcom_err(ret, "while removing group quota files");
		goto bail;
	}
	OCFS2_CLEAR_RO_COMPAT_FEATURE(super,
				      OCFS2_FEATURE_RO_COMPAT_GRPQUOTA);
	ret = ocfs2_write_super(fs);
	tools_progress_step(prog, 1);
bail:
	tools_progress_stop(prog);
	tunefs_unblock_signals();
	return ret;
}

DEFINE_TUNEFS_FEATURE_RO_COMPAT(usrquota,
				OCFS2_FEATURE_RO_COMPAT_USRQUOTA,
				TUNEFS_FLAG_RW | TUNEFS_FLAG_ALLOCATION,
				enable_usrquota,
				disable_usrquota);

DEFINE_TUNEFS_FEATURE_RO_COMPAT(grpquota,
				OCFS2_FEATURE_RO_COMPAT_GRPQUOTA,
				TUNEFS_FLAG_RW | TUNEFS_FLAG_ALLOCATION,
				enable_grpquota,
				disable_grpquota);
#ifdef DEBUG_EXE
int main(int argc, char *argv[])
{
	int ret;

	ret = tunefs_feature_main(argc, argv, &usrquota_feature);
	if (ret)
		return ret;
	return tunefs_feature_main(argc, argv, &grpquota_feature);
}
#endif
