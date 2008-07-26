/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * feature_backup_super.c
 *
 * ocfs2 tune utility to enable and disable the backup superblock feature.
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
#include <string.h>
#include <ctype.h>
#include <inttypes.h>

#include "ocfs2/ocfs2.h"

#include "libocfs2ne.h"


static errcode_t empty_backup_supers(ocfs2_filesys *fs)
{
	errcode_t ret;
	int num;
	uint64_t blocks[OCFS2_MAX_BACKUP_SUPERBLOCKS];

	num = ocfs2_get_backup_super_offsets(fs, blocks,
					     ARRAY_SIZE(blocks));
	if (!num)
		return 0;

	ret = ocfs2_clear_backup_super_list(fs, blocks, num);
	if (ret)
		tcom_err(ret, "while freeing backup  superblock locations");

	return ret;
}

static errcode_t fill_backup_supers(ocfs2_filesys *fs)
{
	errcode_t ret;
	int num;
	uint64_t blocks[OCFS2_MAX_BACKUP_SUPERBLOCKS];

	num = ocfs2_get_backup_super_offsets(fs, blocks,
					     ARRAY_SIZE(blocks));
	ret = ocfs2_set_backup_super_list(fs, blocks, num);
	if (ret)
		tcom_err(ret, "while backing up the superblock");

	return ret;
}

static int disable_backup_super(ocfs2_filesys *fs, int flags)
{
	errcode_t err = 0;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);

	if (!OCFS2_HAS_COMPAT_FEATURE(super,
				      OCFS2_FEATURE_COMPAT_BACKUP_SB)) {
		verbosef(VL_APP,
			 "Backup superblock feature is not enabled; "
			 "nothing to disable\n");
		goto out;
	}

	if (!tunefs_interact("Disable the backup superblock feature on "
			     "device \"%s\"? ",
			     fs->fs_devname))
		goto out;

	tunefs_block_signals();
	err = empty_backup_supers(fs);
	if (!err) {
		super->s_feature_compat &= ~OCFS2_FEATURE_COMPAT_BACKUP_SB;
		err = ocfs2_write_super(fs);
		if (err)
			tcom_err(err,
				 "while writing out the superblock\n"
				 "Unable to disable the backup superblock "
				 "feature on device \"%s\"",
				 fs->fs_devname);
	}
	tunefs_unblock_signals();

out:
	return err;
}

static errcode_t load_global_bitmap(ocfs2_filesys *fs,
				    ocfs2_cached_inode** inode)
{
	errcode_t ret;
	uint64_t blkno;

	ret = ocfs2_lookup_system_inode(fs, GLOBAL_BITMAP_SYSTEM_INODE,
					0, &blkno);
	if (ret)
		goto bail;

	ret = ocfs2_read_cached_inode(fs, blkno, inode);
	if (ret)
		goto bail;

	ret = ocfs2_load_chain_allocator(fs, *inode);

bail:
	return ret;
}

static errcode_t check_backup_offsets(ocfs2_filesys *fs)
{

	errcode_t ret;
	int i, num, val, failed = 0;
	ocfs2_cached_inode *chain_alloc = NULL;
	uint64_t blocks[OCFS2_MAX_BACKUP_SUPERBLOCKS];

	num = ocfs2_get_backup_super_offsets(fs, blocks,
					     ARRAY_SIZE(blocks));
	if (!num) {
		ret = 1;
		errorf("Volume on device \"%s\" is too small to contain "
		       "backup superblocks\n",
		       fs->fs_devname);
		goto bail;
	}

	ret = load_global_bitmap(fs, &chain_alloc);
	if (ret) {
		tcom_err(ret, "while loading the global bitmap");
		goto bail;
	}

	for (i = 0; i < num; i++) {
		ret = ocfs2_bitmap_test(chain_alloc->ci_chains,
					ocfs2_blocks_to_clusters(fs, blocks[i]),
					&val);
		if (ret) {
			tcom_err(ret,
				 "looking up backup superblock locations "
				 "in the global bitmap");
			goto bail;
		}

		if (val) {
			verbosef(VL_APP,
				 "Backup superblock location %d at block "
				 "%"PRIu64" is in use\n", i, blocks[i]);
			/* in order to verify all the block in the 'blocks',
			 * we don't stop the loop here.
			 */
			failed = 1;
		}
	}

	if (failed) {
		ret = 1;
		errorf("One or more backup superblock locations are "
		       "already in use\n");
	} else
		ret = 0;

	if (chain_alloc)
		ocfs2_free_cached_inode(fs, chain_alloc);

bail:
	return ret;
}

static int enable_backup_super(ocfs2_filesys *fs, int flags)
{
	errcode_t err = 0;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);

	if (OCFS2_HAS_COMPAT_FEATURE(super,
				     OCFS2_FEATURE_COMPAT_BACKUP_SB)) {
		verbosef(VL_APP,
			 "Backup superblock feature is already enabled; "
			 "nothing to enable\n");
		goto out;
	}

	if (!tunefs_interact("Enable the backup superblock feature on "
			     "device \"%s\"? ",
			     fs->fs_devname))
		goto out;

	tunefs_block_signals();
	err = check_backup_offsets(fs);
	if (!err)
		err = fill_backup_supers(fs);
	if (!err) {
		super->s_feature_compat |= OCFS2_FEATURE_COMPAT_BACKUP_SB;
		err = ocfs2_write_super(fs);
		if (err)
			tcom_err(err, "while writing out the superblock\n");
	}
	tunefs_unblock_signals();

	if (err)
		errorf("Unable to enable the backup superblock feature on "
		       "device \"%s\"\n",
		       fs->fs_devname);
out:
	return err;
}

DEFINE_TUNEFS_FEATURE_COMPAT(backup_super,
			     OCFS2_FEATURE_COMPAT_BACKUP_SB,
			     TUNEFS_FLAG_RW | TUNEFS_FLAG_ALLOCATION,
			     enable_backup_super,
			     disable_backup_super);

#ifdef DEBUG_EXE
int main(int argc, char *argv[])
{
	return tunefs_feature_main(argc, argv, &backup_super_feature);
}
#endif
