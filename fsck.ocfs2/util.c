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
 * Little helpers that are used by all passes.
 *
 * XXX
 * 	pull more in here.. look in include/pass?.h for incongruities
 *
 */
#include <inttypes.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include "ocfs2/ocfs2.h"

#include "util.h"

void o2fsck_write_inode(o2fsck_state *ost, uint64_t blkno,
			struct ocfs2_dinode *di)
{
	errcode_t ret;
	const char *whoami = __FUNCTION__;

	if (blkno != di->i_blkno) {
		com_err(whoami, OCFS2_ET_INTERNAL_FAILURE, "when asked to "
			"write an inode with an i_blkno of %"PRIu64" to block "
			"%"PRIu64, (uint64_t)di->i_blkno, blkno);
		return;
	}

	ret = ocfs2_write_inode(ost->ost_fs, blkno, (char *)di);
	if (ret) {
		com_err(whoami, ret, "while writing inode %"PRIu64,
			(uint64_t)di->i_blkno);
		ost->ost_saw_error = 1;
	}
}

void o2fsck_mark_cluster_allocated(o2fsck_state *ost, uint32_t cluster)
{
	int was_set = 0;
	errcode_t ret;
	const char *whoami = __FUNCTION__;

	o2fsck_bitmap_set(ost->ost_allocated_clusters, cluster, &was_set);

	if (!was_set)
		return;

	if (!ost->ost_duplicate_clusters) {
		fprintf(stderr,
			"Duplicate clusters detected.  Pass 1b will be run\n");

		ret = ocfs2_cluster_bitmap_new(ost->ost_fs,
					       "duplicate clusters",
					       &ost->ost_duplicate_clusters);
		if (ret) {
			com_err(whoami, ret,
				"while allocating duplicate cluster bitmap");
			return;
		}
	}

	verbosef("Cluster %"PRIu32" is allocated to more than one object\n",
		 cluster);
	ocfs2_bitmap_set(ost->ost_duplicate_clusters, cluster, NULL);
}

void o2fsck_mark_clusters_allocated(o2fsck_state *ost, uint32_t cluster,
				    uint32_t num)
{
	while(num--)
		o2fsck_mark_cluster_allocated(ost, cluster++);
}

void o2fsck_mark_cluster_unallocated(o2fsck_state *ost, uint32_t cluster)
{
	int was_set;

	o2fsck_bitmap_clear(ost->ost_allocated_clusters, cluster, &was_set);
}

errcode_t o2fsck_type_from_dinode(o2fsck_state *ost, uint64_t ino,
				  uint8_t *type)
{
	char *buf = NULL;
	errcode_t ret;
	struct ocfs2_dinode *dinode;
	const char *whoami = __FUNCTION__;

	*type = 0;

	ret = ocfs2_malloc_block(ost->ost_fs->fs_io, &buf);
	if (ret) {
		com_err(whoami, ret, "while allocating an inode buffer to "
			"read and discover the type of inode %"PRIu64, ino);
		goto out;
	}

	ret = ocfs2_read_inode(ost->ost_fs, ino, buf);
	if (ret) {
		com_err(whoami, ret, "while reading inode %"PRIu64" to "
			"discover its file type", ino);
		goto out;
	}

	dinode = (struct ocfs2_dinode *)buf; 
	*type = ocfs2_type_by_mode[(dinode->i_mode & S_IFMT)>>S_SHIFT];

out:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

size_t o2fsck_bitcount(unsigned char *bytes, size_t len)
{
	static unsigned char nibble_count[16] = {
		0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4
	};
	size_t count = 0;

	for (; len--; bytes++) {
		count += nibble_count[*bytes >> 4];
		count += nibble_count[*bytes & 0xf];
	}

	return count;
}

errcode_t handle_slots_system_file(ocfs2_filesys *fs,
				   int type,
				   errcode_t (*func)(ocfs2_filesys *fs,
						     struct ocfs2_dinode *di,
						     int slot))
{
	errcode_t ret;
	uint64_t blkno;
	int slot, max_slots;
	char *buf = NULL;
	struct ocfs2_dinode *di;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		goto bail;

	di = (struct ocfs2_dinode *)buf;

	max_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;

	for (slot = 0; slot < max_slots; slot++) {
		ret = ocfs2_lookup_system_inode(fs,
						type,
						slot, &blkno);
		if (ret)
			goto bail;

		ret = ocfs2_read_inode(fs, blkno, buf);
		if (ret)
			goto bail;

		if (func) {
			ret = func(fs, di, slot);
			if (ret)
				goto bail;
		}
	}

bail:

	if (buf)
		ocfs2_free(&buf);
	return ret;
}

/* Number of blocks available in the I/O cache */
static int cache_blocks;
/*
 * Number of blocks we've currently cached.  This is an imperfect guess
 * designed for pre-caching.  Code can keep slurping blocks until
 * o2fsck_worth_caching() returns 0.
 */
static int blocks_cached;

void o2fsck_init_cache(o2fsck_state *ost, enum o2fsck_cache_hint hint)
{
	errcode_t ret;
	uint64_t blocks_wanted;
	int leave_room;
	ocfs2_filesys *fs = ost->ost_fs;
	int max_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;

	switch (hint) {
		case O2FSCK_CACHE_MODE_FULL:
			leave_room = 1;
			blocks_wanted = fs->fs_blocks;
			break;
		case O2FSCK_CACHE_MODE_JOURNAL:
			/*
			 * We need enough blocks for all the journal
			 * data.  Let's guess at 256M journals.
			 */
			leave_room = 0;
			blocks_wanted = ocfs2_blocks_in_bytes(fs,
					max_slots * 1024 * 1024 * 256);
			break;
		case O2FSCK_CACHE_MODE_NONE:
			return;
		default:
			assert(0);
	}

	verbosef("Want %"PRIu64" blocks for the I/O cache\n",
		 blocks_wanted);

	/*
	 * leave_room means that we don't want our cache to be taking
	 * all available memory.  So we try to get twice as much as we
	 * want; if that works, we know that getting exactly as much as
	 * we want is going to be safe.
	 */
	if (leave_room)
		blocks_wanted <<= 1;

	if (blocks_wanted > INT_MAX)
		blocks_wanted = INT_MAX;

	while (blocks_wanted > 0) {
		io_destroy_cache(fs->fs_io);
		verbosef("Asking for %"PRIu64" blocks of I/O cache\n",
			 blocks_wanted);
		ret = io_init_cache(fs->fs_io, blocks_wanted);
		if (!ret) {
			/*
			 * We want to pin our cache; there's no point in
			 * having a large cache if half of it is in swap.
			 * However, some callers may not be privileged
			 * enough, so once we get down to a small enough
			 * number (512 blocks), we'll stop caring.
			 */
			ret = io_mlock_cache(fs->fs_io);
			if (ret && (blocks_wanted <= 512))
				ret = 0;
		}
		if (!ret) {
			verbosef("Got %"PRIu64" blocks\n", blocks_wanted);
			/*
			 * We've found an allocation that works.  If
			 * we're not leaving room, we're done.  But if
			 * we're leaving room, we clear leave_room and go
			 * around again.  We expect to succeed there.
			 */
			if (!leave_room) {
				cache_blocks = blocks_wanted;
				break;
			}

			verbosef("Leaving room for other %s\n",
				 "allocations");
			leave_room = 0;
		}

		blocks_wanted >>= 1;
	}
}

int o2fsck_worth_caching(int blocks_to_read)
{
	if ((blocks_to_read + blocks_cached) > cache_blocks)
		return 0;

	blocks_cached += blocks_to_read;
	return 1;
}

void o2fsck_reset_blocks_cached(void)
{
	blocks_cached = 0;
}

void __o2fsck_bitmap_set(ocfs2_bitmap *bitmap, uint64_t bitno, int *oldval,
			 const char *where)
{
	errcode_t ret;

	ret = ocfs2_bitmap_set(bitmap, bitno, oldval);
	if (ret) {
		com_err(where, ret,
			"while trying to set bit %"PRIu64", aborting\n",
			bitno);
		/*
		 * We abort with SIGTERM so that the signal handler can
		 * clean up the cluster stack.
		 */
		kill(getpid(), SIGTERM);
	}
}

void __o2fsck_bitmap_clear(ocfs2_bitmap *bitmap, uint64_t bitno, int *oldval,
			   const char *where)
{
	errcode_t ret;

	ret = ocfs2_bitmap_clear(bitmap, bitno, oldval);
	if (ret) {
		com_err(where, ret,
			"while trying to clear bit %"PRIu64", aborting\n",
			bitno);
		/*
		 * We abort with SIGTERM so that the signal handler can
		 * clean up the cluster stack.
		 */
		kill(getpid(), SIGTERM);
	}
}
