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
                                                                                                                                                         
#include "ocfs2.h"

#define DEFAULT_DLMFS_PATH	"/dlm/"

static errcode_t ocfs2_get_journal_blkno(ocfs2_filesys *fs, uint64_t *jrnl_blkno)
{
	ocfs2_super_block *sb = OCFS2_RAW_SB(fs->fs_super);
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
	ocfs2_super_block *sb = OCFS2_RAW_SB(fs->fs_super);
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

errcode_t ocfs2_initialize_dlm(ocfs2_filesys *fs)
{
	struct o2dlm_ctxt *dlm_ctxt = NULL;
	errcode_t ret = 0;

	ret = ocfs2_start_heartbeat(fs);
	if (ret)
		goto bail;

	ret = o2dlm_initialize(DEFAULT_DLMFS_PATH, fs->uuid_str, &dlm_ctxt);
	if (ret) {
		/* What to do with an error code? */
		ocfs2_stop_heartbeat(fs);
		goto bail;
	}

	fs->fs_dlm_ctxt = dlm_ctxt;

bail:
	return ret;
}

errcode_t ocfs2_shutdown_dlm(ocfs2_filesys *fs)
{
	errcode_t ret;

	ret = o2dlm_destroy(fs->fs_dlm_ctxt);
	if (ret)
		goto bail;

	fs->fs_dlm_ctxt = NULL;

	ret = ocfs2_stop_heartbeat(fs);

bail:
	return ret;
}

errcode_t ocfs2_super_lock(ocfs2_filesys *fs)
{
	char lock_name[OCFS2_LOCK_ID_MAX_LEN];
	errcode_t ret;

	ocfs2_encode_lockres(OCFS2_LOCK_TYPE_SUPER, OCFS2_SUPER_BLOCK_BLKNO,
			     0, lock_name);

	ret = o2dlm_lock(fs->fs_dlm_ctxt, lock_name,
			 O2DLM_TRYLOCK, O2DLM_LEVEL_EXMODE);

	return ret;
}

errcode_t ocfs2_super_unlock(ocfs2_filesys *fs)
{
	char lock_name[OCFS2_LOCK_ID_MAX_LEN];
	errcode_t ret;

	ocfs2_encode_lockres(OCFS2_LOCK_TYPE_SUPER, OCFS2_SUPER_BLOCK_BLKNO,
			     0, lock_name);

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
			     ci->ci_inode->i_generation, lock_name);

	ret = o2dlm_lock(fs->fs_dlm_ctxt, lock_name, flags, level);

	return ret;
}

errcode_t ocfs2_meta_unlock(ocfs2_filesys *fs,
			    ocfs2_cached_inode *ci)
{
	char lock_name[OCFS2_LOCK_ID_MAX_LEN];
	errcode_t ret;

	ocfs2_encode_lockres(OCFS2_LOCK_TYPE_META, ci->ci_blkno,
			     ci->ci_inode->i_generation, lock_name);

	ret = o2dlm_unlock(fs->fs_dlm_ctxt, lock_name);

	return ret;

}

#ifdef DEBUG_EXE
#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <libgen.h>

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

	ret = ocfs2_initialize_dlm(fs);
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
		ret = ocfs2_shutdown_dlm(fs);
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
