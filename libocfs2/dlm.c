/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dlm.c
 *
 * Interface the OCFS2 userspace library to the userspace DLM library
 *
 * Copyright (C) 2005 Oracle.  All rights reserved.
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
 */

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE

#include "ocfs2/ocfs2.h"

#define DEFAULT_DLMFS_PATH	"/dlm/"

static errcode_t ocfs2_get_journal_blkno(ocfs2_filesys *fs, uint64_t *jrnl_blkno)
{
	struct ocfs2_super_block *sb = OCFS2_RAW_SB(fs->fs_super);
	char sysfile[OCFS2_MAX_FILENAME_LEN];
	int i;
	errcode_t ret = 0;

	for (i = 0; i < sb->s_max_slots; ++i) {
		snprintf (sysfile, sizeof(sysfile),
			  ocfs2_system_inodes[JOURNAL_SYSTEM_INODE].si_name, i);
		ret = ocfs2_lookup(fs, fs->fs_sysdir_blkno, sysfile,
				   strlen(sysfile), NULL, &(jrnl_blkno[i]));
		if (ret)
			goto bail;
	}

bail:
	return ret;
}

errcode_t ocfs2_lock_down_cluster(ocfs2_filesys *fs)
{
	struct ocfs2_super_block *sb = OCFS2_RAW_SB(fs->fs_super);
	uint64_t jrnl_blkno[OCFS2_MAX_SLOTS];
	ocfs2_cached_inode *ci;
	errcode_t ret = 0;
	int i;

	ret = ocfs2_get_journal_blkno(fs, jrnl_blkno);
	if (ret)
		goto bail;

	ret = ocfs2_super_lock(fs);
	if (ret)
		goto bail;

	for (i = 0; i < sb->s_max_slots; ++i) {
		ret = ocfs2_read_cached_inode(fs, jrnl_blkno[i], &ci);
		if (ret) {
			ocfs2_super_unlock(fs);
			goto bail;
		}

		ret = ocfs2_meta_lock(fs, ci, O2DLM_LEVEL_EXMODE, O2DLM_TRYLOCK);
		if (ret) {
			ocfs2_super_unlock(fs);
			ocfs2_free_cached_inode(fs, ci);
			goto bail;
		}

		ocfs2_meta_unlock(fs, ci);

		ocfs2_free_cached_inode(fs, ci);
	}

bail:
	return ret;
}

errcode_t ocfs2_release_cluster(ocfs2_filesys *fs)
{
	errcode_t ret = 0;

	ret = ocfs2_super_unlock(fs);
	if (ret)
		goto bail;

bail:
	return ret;
}

errcode_t ocfs2_fill_cluster_desc(ocfs2_filesys *fs,
				  struct o2cb_cluster_desc *desc)
{
	errcode_t ret = 0;
	struct ocfs2_super_block *sb = OCFS2_RAW_SB(fs->fs_super);

	if (!ocfs2_clusterinfo_valid(sb)) {
		desc->c_stack = NULL;
		desc->c_cluster = NULL;
		return 0;
	}

	ret = ocfs2_malloc0(OCFS2_STACK_LABEL_LEN + 1, &desc->c_stack);
	if (ret)
		return ret;

	ret = ocfs2_malloc0(OCFS2_CLUSTER_NAME_LEN + 1, &desc->c_cluster);
	if (ret) {
		ocfs2_free(&desc->c_stack);
		return ret;
	}

	memcpy(desc->c_stack, sb->s_cluster_info.ci_stack,
	       OCFS2_STACK_LABEL_LEN);
	memcpy(desc->c_cluster, sb->s_cluster_info.ci_cluster,
	       OCFS2_CLUSTER_NAME_LEN);

	return 0;
}

errcode_t ocfs2_set_cluster_desc(ocfs2_filesys *fs,
				 struct o2cb_cluster_desc *desc)
{
	errcode_t ret;
	struct ocfs2_super_block *sb = OCFS2_RAW_SB(fs->fs_super);

	if (desc->c_stack) {
		if (!desc->c_stack[0] || !desc->c_cluster ||
		    !desc->c_cluster[0]) {
			ret = OCFS2_ET_INVALID_ARGUMENT;
			goto out;
		}

		if (!ocfs2_uses_extended_slot_map(sb)) {
			sb->s_feature_incompat |=
				OCFS2_FEATURE_INCOMPAT_EXTENDED_SLOT_MAP;
			ret = ocfs2_format_slot_map(fs);
			if (ret)
				goto out;
		}

		/*
		 * if clusterinfo is not set and the stackname != o2cb,
		 * then set the userspace flag
		 */
		if (!(sb->s_feature_incompat &
		      OCFS2_FEATURE_INCOMPAT_CLUSTERINFO)) {
			if (strcmp(desc->c_stack, OCFS2_CLASSIC_CLUSTER_STACK))
				sb->s_feature_incompat |=
					OCFS2_FEATURE_INCOMPAT_USERSPACE_STACK;
		}
		memcpy(sb->s_cluster_info.ci_stack, desc->c_stack,
		       OCFS2_STACK_LABEL_LEN);
		memcpy(sb->s_cluster_info.ci_cluster, desc->c_cluster,
		       OCFS2_CLUSTER_NAME_LEN);
	} else {
		sb->s_feature_incompat &=
			~OCFS2_FEATURE_INCOMPAT_USERSPACE_STACK;
	}

	ret = ocfs2_write_super(fs);

out:
	return ret;
}

errcode_t ocfs2_initialize_dlm(ocfs2_filesys *fs, const char *service)
{
	struct o2dlm_ctxt *dlm_ctxt = NULL;
	errcode_t ret = 0;
	int stackglue_support;
	struct o2cb_cluster_desc cluster;
	struct o2cb_region_desc desc;
	char *stack_path;

	ret = ocfs2_fill_cluster_desc(fs, &cluster);
	if (ret)
		goto bail;

	ret = ocfs2_fill_heartbeat_desc(fs, &desc);
	if (ret)
		goto bail;

	ret = o2dlm_supports_stackglue(&stackglue_support);
	if (ret)
		goto bail;

	desc.r_service = (char *)service;
	desc.r_persist = 0;
	ret = o2cb_begin_group_join(&cluster, &desc);
	if (ret)
		goto bail;

	/*
	 * We want to use dlmfs if we can, as it provides the full feature
	 * set of libo2dlm.  Any dlmfs with the 'stackglue' capability will
	 * support all cluster stacks.  An empty cluster.c_stack means
	 * o2cb, which always supports dlmfs.
	 *
	 * If we're unlucky enough to have older userspace stack code,
	 * we pass NULL to avoid dlmfs.
	 */
	if (stackglue_support || !cluster.c_stack)
		stack_path = DEFAULT_DLMFS_PATH;
	else
		stack_path = NULL;
	ret = o2dlm_initialize(stack_path, fs->uuid_str, &dlm_ctxt);
	if (ret) {
		/* What to do with an error code? */

		/* Ignore the result of complete_group_join, as we want
		 * to propagate our o2dlm_initialize() error */
		o2cb_complete_group_join(&cluster, &desc, ret);
		goto bail;
	}

	ret = o2cb_complete_group_join(&cluster, &desc, 0);

	if (!ret)
		fs->fs_dlm_ctxt = dlm_ctxt;
	else
		o2dlm_destroy(dlm_ctxt);

bail:
	return ret;
}

errcode_t ocfs2_shutdown_dlm(ocfs2_filesys *fs, const char *service)
{
	errcode_t ret;
	struct o2cb_cluster_desc cluster;
	struct o2cb_region_desc desc;

	ret = o2dlm_destroy(fs->fs_dlm_ctxt);
	if (ret)
		goto bail;

	fs->fs_dlm_ctxt = NULL;

	ret = ocfs2_fill_cluster_desc(fs, &cluster);
	if (ret)
		goto bail;

	ret = ocfs2_fill_heartbeat_desc(fs, &desc);
	if (ret)
		goto bail;

	desc.r_service = (char *)service;
	desc.r_persist = 0;
	ret = o2cb_group_leave(&cluster, &desc);

bail:
	return ret;
}

errcode_t ocfs2_super_lock(ocfs2_filesys *fs)
{
	char lock_name[OCFS2_LOCK_ID_MAX_LEN];
	errcode_t ret;

	ocfs2_encode_lockres(OCFS2_LOCK_TYPE_SUPER, OCFS2_SUPER_BLOCK_BLKNO,
			     0, 0, lock_name);

	ret = o2dlm_lock(fs->fs_dlm_ctxt, lock_name,
			 O2DLM_TRYLOCK, O2DLM_LEVEL_EXMODE);

	return ret;
}

errcode_t ocfs2_super_unlock(ocfs2_filesys *fs)
{
	char lock_name[OCFS2_LOCK_ID_MAX_LEN];
	errcode_t ret;

	ocfs2_encode_lockres(OCFS2_LOCK_TYPE_SUPER, OCFS2_SUPER_BLOCK_BLKNO,
			     0, 0, lock_name);

	ret = o2dlm_unlock(fs->fs_dlm_ctxt, lock_name);

	return ret;
}

errcode_t ocfs2_meta_lock(ocfs2_filesys *fs,
			  ocfs2_cached_inode *ci,
			  enum o2dlm_lock_level level,
			  int flags)
{
	char lock_name[OCFS2_LOCK_ID_MAX_LEN];
	errcode_t ret;

	ocfs2_encode_lockres(OCFS2_LOCK_TYPE_META, ci->ci_blkno,
			     ci->ci_inode->i_generation, 0, lock_name);

	ret = o2dlm_lock(fs->fs_dlm_ctxt, lock_name, flags, level);

	return ret;
}

errcode_t ocfs2_meta_unlock(ocfs2_filesys *fs,
			    ocfs2_cached_inode *ci)
{
	char lock_name[OCFS2_LOCK_ID_MAX_LEN];
	errcode_t ret;

	ocfs2_encode_lockres(OCFS2_LOCK_TYPE_META, ci->ci_blkno,
			     ci->ci_inode->i_generation, 0, lock_name);

	ret = o2dlm_unlock(fs->fs_dlm_ctxt, lock_name);

	return ret;

}

#ifdef DEBUG_EXE
#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <libgen.h>

#define DEBUG_SERVICE "debug"

static void print_usage(void)
{
	fprintf(stderr, "Usage: dlm <filename>\n");
}

extern int opterr, optind;
extern char *optarg;

int main(int argc, char *argv[])
{
	errcode_t ret;
	int c;
	char *filename;
	ocfs2_filesys *fs = NULL;
	char *progname;

	initialize_ocfs_error_table();
	initialize_o2dl_error_table();

	if (argc < 2) {
		print_usage();
		exit(1);
	}

	filename = argv[1];
	progname = basename(argv[0]);

	ret = ocfs2_open(filename, OCFS2_FLAG_RO, 0, 0, &fs);
	if (ret) {
		com_err(progname, ret,
			"while opening file \"%s\"", filename);
		goto out;
	}

	ret = ocfs2_initialize_dlm(fs, DEBUG_SERVICE);
	if (ret) {
		com_err(progname, ret, "while initializing dlm");
		goto out;
	}

	printf("DLM initialized\n");
	ret = ocfs2_lock_down_cluster(fs);
	if (ret) {
		com_err(progname, ret, "while locking cluster");
		goto out;
	}

	printf("Cluster is locked\nPress any key to continue...");
	c = getchar();

	ret = ocfs2_release_cluster(fs);
	if (ret) {
		com_err(progname, ret, "while releasing cluster");
		goto out;
	}

	printf("Cluster released\n");

out:
	if (fs->fs_dlm_ctxt) {
		ret = ocfs2_shutdown_dlm(fs, DEBUG_SERVICE);
		if (ret)
			com_err(progname, ret, "while shutting down dlm");
	}

	if (fs) {
		ret = ocfs2_close(fs);
		if (ret)
			com_err(progname, ret,
				"while closing file \"%s\"", filename);
	}

	return 0;
}
#endif  /* DEBUG_EXE */
