/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * libocfs2ne.c
 *
 * Shared routines for the ocfs2 tunefs utility
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

#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE /* for getopt_long and O_DIRECT */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <getopt.h>
#include <assert.h>

#include "ocfs2/ocfs2.h"
#include "ocfs2/bitops.h"

#include "libocfs2ne.h"

#define WHOAMI "tunefs.ocfs2"


/*
 * Keeps track of how ocfs2ne sees the filesystem.  This structure is
 * filled in by the master ocfs2_filesys (the first caller to
 * tunefs_open()).  Every other ocfs2_filesys refers to it.
 */
struct tunefs_filesystem_state {
	/* The master ocfs2_filesys (first tunefs_open()) */
	ocfs2_filesys	*ts_master;

	/*
	 * When a single-node (local) filesystem is opened, we prevent
	 * concurrent mount(2) by opening the device O_EXCL.  This is the
	 * fd we used.  The value is -1 for cluster-aware filesystems.
	 */
	int		ts_local_fd;

	/*
	 * Already-mounted filesystems can only do online operations.
	 * This is the fd we send ioctl(2)s to.  If the filesystem isn't
	 * in use, this is -1.
	 */
	int		ts_online_fd;

	/*
	 * Do we have the cluster locked?  This can be zero if we're a
	 * local filesystem.  If it is non-zero, ts_master->fs_dlm_ctxt
	 * must be valid.
	 */
	int		ts_cluster_locked;

	/* Non-zero if we've ever mucked with the allocator */
	int		ts_allocation;

	/*
	 * Number of clusters in the filesystem.  If changed by a
	 * resized filesystem, it is tracked here and used at final
	 * close.
	 */
	uint32_t	ts_fs_clusters;

	/* Size of the largest journal seen in tunefs_journal_check() */
	uint32_t	ts_journal_clusters;

	/* Journal feature bits found during tunefs_journal_check() */
	ocfs2_fs_options	ts_journal_features;
};

struct tunefs_private {
	struct list_head		tp_list;
	ocfs2_filesys			*tp_fs;

	/* All tunefs_privates point to the master state. */
	struct tunefs_filesystem_state	*tp_state;

	/* Flags passed to tunefs_open() for this ocfs2_filesys */
	int				tp_open_flags;
};

/* List of all ocfs2_filesys objects opened by tunefs_open() */
static LIST_HEAD(fs_list);

/* Refcount for calls to tunefs_[un]block_signals() */
static unsigned int blocked_signals_count;

/* For DEBUG_EXE programs */
static const char *usage_string;


/*
 * Code to manage the fs_private state.
 */

static inline struct tunefs_private *to_private(ocfs2_filesys *fs)
{
	return fs->fs_private;
}

static struct tunefs_filesystem_state *tunefs_get_master_state(void)
{
	struct tunefs_filesystem_state *s = NULL;
	struct tunefs_private *tp;

	if (!list_empty(&fs_list)) {
		tp = list_entry(fs_list.prev, struct tunefs_private,
			       tp_list);
		s = tp->tp_state;
	}

	return s;
}

static struct tunefs_filesystem_state *tunefs_get_state(ocfs2_filesys *fs)
{
	struct tunefs_private *tp = to_private(fs);

	return tp->tp_state;
}

static errcode_t tunefs_set_state(ocfs2_filesys *fs)
{
	errcode_t err = 0;
	struct tunefs_private *tp = to_private(fs);
	struct tunefs_filesystem_state *s = tunefs_get_master_state();

	if (!s) {
		err = ocfs2_malloc0(sizeof(struct tunefs_filesystem_state),
				    &s);
		if (!err) {
			s->ts_local_fd = -1;
			s->ts_online_fd = -1;
			s->ts_master = fs;
			s->ts_fs_clusters = fs->fs_clusters;
		} else
			s = NULL;
	}

	tp->tp_state = s;

	return err;
}


/*
 * Functions for use by operations.
 */

/* Call this with SIG_BLOCK to block and SIG_UNBLOCK to unblock */
static void block_signals(int how)
{
     sigset_t sigs;

     sigfillset(&sigs);
     sigdelset(&sigs, SIGTRAP);
     sigdelset(&sigs, SIGSEGV);
     sigprocmask(how, &sigs, NULL);
}

void tunefs_block_signals(void)
{
	if (!blocked_signals_count)
		block_signals(SIG_BLOCK);
	blocked_signals_count++;
}

void tunefs_unblock_signals(void)
{
	if (blocked_signals_count) {
		blocked_signals_count--;
		if (!blocked_signals_count)
			block_signals(SIG_UNBLOCK);
	} else
		errorf("Trying to unblock signals, but signals were not "
		       "blocked\n");
}

errcode_t tunefs_dlm_lock(ocfs2_filesys *fs, const char *lockid,
			  int flags, enum o2dlm_lock_level level)
{
	struct tunefs_filesystem_state *state = tunefs_get_state(fs);

	if (ocfs2_mount_local(fs))
		return 0;

	return o2dlm_lock(state->ts_master->fs_dlm_ctxt, lockid, flags,
			  level);
}

errcode_t tunefs_dlm_unlock(ocfs2_filesys *fs, char *lockid)
{
	struct tunefs_filesystem_state *state = tunefs_get_state(fs);

	if (ocfs2_mount_local(fs))
		return 0;

	return o2dlm_unlock(state->ts_master->fs_dlm_ctxt, lockid);
}

errcode_t tunefs_online_ioctl(ocfs2_filesys *fs, int op, void *arg)
{
	int rc;
	struct tunefs_filesystem_state *state = tunefs_get_state(fs);

	if (state->ts_online_fd < 0)
		return TUNEFS_ET_INTERNAL_FAILURE;

	rc = ioctl(state->ts_online_fd, op, arg);
	if (rc) {
		switch (errno) {
			case EBADF:
			case EFAULT:
				return TUNEFS_ET_INTERNAL_FAILURE;
				break;

			case ENOTTY:
				return TUNEFS_ET_ONLINE_NOT_SUPPORTED;
				break;

			default:
				return TUNEFS_ET_ONLINE_FAILED;
				break;
		}
	}

	return 0;
}

errcode_t tunefs_get_number(char *arg, uint64_t *res)
{
	char *ptr = NULL;
	uint64_t num;

	num = strtoull(arg, &ptr, 0);

	if ((ptr == arg) || (num == UINT64_MAX))
		return TUNEFS_ET_INVALID_NUMBER;

	switch (*ptr) {
	case '\0':
		break;

	case 'p':
	case 'P':
		num *= 1024;
		/* FALL THROUGH */

	case 't':
	case 'T':
		num *= 1024;
		/* FALL THROUGH */

	case 'g':
	case 'G':
		num *= 1024;
		/* FALL THROUGH */

	case 'm':
	case 'M':
		num *= 1024;
		/* FALL THROUGH */

	case 'k':
	case 'K':
		num *= 1024;
		/* FALL THROUGH */

	case 'b':
	case 'B':
		break;

	default:
		return TUNEFS_ET_INVALID_NUMBER;
	}

	*res = num;

	return 0;
}

errcode_t tunefs_set_in_progress(ocfs2_filesys *fs, int flag)
{
	/* RESIZE is a special case due for historical reasons */
	if (flag == OCFS2_FEATURE_INCOMPAT_RESIZE_INPROG) {
		OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat |=
			OCFS2_FEATURE_INCOMPAT_RESIZE_INPROG;
	} else {
		OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat |=
			OCFS2_FEATURE_INCOMPAT_TUNEFS_INPROG;
		OCFS2_RAW_SB(fs->fs_super)->s_tunefs_flag |= flag;
	}

	return ocfs2_write_primary_super(fs);
}

errcode_t tunefs_clear_in_progress(ocfs2_filesys *fs, int flag)
{
	/* RESIZE is a special case due for historical reasons */
	if (flag == OCFS2_FEATURE_INCOMPAT_RESIZE_INPROG) {
		OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat &=
			~OCFS2_FEATURE_INCOMPAT_RESIZE_INPROG;
	} else {
		OCFS2_RAW_SB(fs->fs_super)->s_tunefs_flag &= ~flag;
		if (OCFS2_RAW_SB(fs->fs_super)->s_tunefs_flag == 0)
			OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat &=
				~OCFS2_FEATURE_INCOMPAT_TUNEFS_INPROG;
	}

	return ocfs2_write_primary_super(fs);
}

errcode_t tunefs_set_journal_size(ocfs2_filesys *fs, uint64_t new_size,
				  ocfs2_fs_options mask,
				  ocfs2_fs_options options)
{
	errcode_t ret = 0;
	char jrnl_file[OCFS2_MAX_FILENAME_LEN];
	uint64_t blkno;
	int i;
	int max_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;
	uint32_t num_clusters;
	char *buf = NULL;
	struct ocfs2_dinode *di;
	struct tunefs_filesystem_state *state = tunefs_get_state(fs);
	struct tools_progress *prog;
	ocfs2_fs_options new_features, *newfeat = &new_features, *curfeat;
	int features_change;

	num_clusters =
		ocfs2_clusters_in_blocks(fs,
					 ocfs2_blocks_in_bytes(fs,
							       new_size));

	/* If no size was passed in, use the size we found at open() */
	if (!num_clusters)
		num_clusters = state->ts_journal_clusters;

	/*
	 * This can't come from a NOCLUSTER operation, so we'd better
	 * have a size in ts_journal_clusters
	 */
	assert(num_clusters);

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret) {
		verbosef(VL_LIB,
			 "%s while allocating inode buffer for journal "
			 "resize\n",
			 error_message(ret));
		return ret;
	}

	prog = tools_progress_start("Setting journal size", "jsize",
				    max_slots);
	if (!prog) {
		ret = TUNEFS_ET_NO_MEMORY;
		verbosef(VL_LIB,
			 "%s while initializing progress display for "
			 "journal resize\n",
			 error_message(ret));
		return ret;
	}

	curfeat = &state->ts_journal_features;
	newfeat->opt_compat =
		(curfeat->opt_compat & ~mask.opt_compat) |
		(options.opt_compat & mask.opt_compat);
	newfeat->opt_incompat =
		(curfeat->opt_incompat & ~mask.opt_incompat) |
		(options.opt_incompat & mask.opt_incompat);
	newfeat->opt_ro_compat =
		(curfeat->opt_ro_compat & ~mask.opt_ro_compat) |
		(options.opt_ro_compat & mask.opt_ro_compat);
	features_change =
		(newfeat->opt_compat ^ curfeat->opt_compat) ||
		(newfeat->opt_incompat ^ curfeat->opt_incompat) ||
		(newfeat->opt_ro_compat ^ curfeat->opt_ro_compat);

	for (i = 0; i < max_slots; ++i) {
		ocfs2_sprintf_system_inode_name(jrnl_file,
						OCFS2_MAX_FILENAME_LEN,
						JOURNAL_SYSTEM_INODE, i);
		ret = ocfs2_lookup_system_inode(fs, JOURNAL_SYSTEM_INODE, i,
						&blkno);
		if (ret) {
			verbosef(VL_LIB,
				 "%s while looking up \"%s\" during "
				 "journal resize\n",
				 error_message(ret),
				 jrnl_file);
			goto bail;
		}

		ret = ocfs2_read_inode(fs, blkno, buf);
		if (ret) {
			verbosef(VL_LIB,
				 "%s while reading journal inode "
				 "%"PRIu64" for resizing\n",
				 error_message(ret), blkno);
			goto bail;
		}

		di = (struct ocfs2_dinode *)buf;
		if (num_clusters == di->i_clusters && !features_change) {
			tools_progress_step(prog, 1);
			continue;
		}

		verbosef(VL_LIB,
			 "Resizing journal \"%s\" to %"PRIu32" clusters\n",
			 jrnl_file, num_clusters);
		ret = ocfs2_make_journal(fs, blkno, num_clusters, newfeat);
		if (ret) {
			verbosef(VL_LIB,
				 "%s while resizing \"%s\" at block "
				 "%"PRIu64" to %"PRIu32" clusters\n",
				 error_message(ret), jrnl_file, blkno,
				 num_clusters);
			goto bail;
		}
		verbosef(VL_LIB, "Successfully resized journal \"%s\"\n",
			 jrnl_file);
		tools_progress_step(prog, 1);
	}

bail:
	tools_progress_stop(prog);
	if (buf)
		ocfs2_free(&buf);

	return ret;
}

errcode_t tunefs_empty_clusters(ocfs2_filesys *fs, uint64_t start_blk,
				uint32_t num_clusters)
{
	errcode_t ret;
	char *buf = NULL;
	uint64_t bpc = ocfs2_clusters_to_blocks(fs, 1);
	uint64_t total_blocks = ocfs2_clusters_to_blocks(fs, num_clusters);
	uint64_t io_blocks = total_blocks;

	ret = ocfs2_malloc_blocks(fs->fs_io, io_blocks, &buf);
	if (ret == OCFS2_ET_NO_MEMORY) {
		io_blocks = bpc;
		ret = ocfs2_malloc_blocks(fs->fs_io, io_blocks, &buf);
	}
	if (ret)
		goto bail;

	memset(buf, 0, io_blocks * fs->fs_blocksize);

	while (total_blocks) {
		ret = io_write_block_nocache(fs->fs_io, start_blk,
					     io_blocks, buf);
		if (ret)
			goto bail;

		total_blocks -= io_blocks;
		start_blk += io_blocks;
	}

bail:
	if (buf)
		ocfs2_free(&buf);

	return ret;
}

errcode_t tunefs_get_free_clusters(ocfs2_filesys *fs, uint32_t *clusters)
{
	errcode_t ret;
	uint64_t blkno;
	char *buf = NULL;
	struct ocfs2_dinode *di = NULL;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		goto bail;

	ret = ocfs2_lookup_system_inode(fs, GLOBAL_BITMAP_SYSTEM_INODE,
					0, &blkno);
	if (ret)
		goto bail;

	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret)
		goto bail;

	di = (struct ocfs2_dinode *)buf;
	if (clusters)
		*clusters = di->id1.bitmap1.i_total - di->id1.bitmap1.i_used;
bail:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

static errcode_t tunefs_validate_inode(ocfs2_filesys *fs,
				       struct ocfs2_dinode *di)
{
	if (memcmp(di->i_signature, OCFS2_INODE_SIGNATURE,
		    strlen(OCFS2_INODE_SIGNATURE)))
		return OCFS2_ET_BAD_INODE_MAGIC;

	ocfs2_swap_inode_to_cpu(fs, di);

	if (di->i_fs_generation != fs->fs_super->i_fs_generation)
		return OCFS2_ET_INODE_NOT_VALID;

	if (!(di->i_flags & OCFS2_VALID_FL))
		return OCFS2_ET_INODE_NOT_VALID;

	return 0;
}

errcode_t tunefs_foreach_inode(ocfs2_filesys *fs,
			       errcode_t (*func)(ocfs2_filesys *fs,
						 struct ocfs2_dinode *di,
						 void *user_data),
			       void *user_data)
{
	errcode_t ret;
	uint64_t blkno;
	char *buf;
	struct ocfs2_dinode *di;
	ocfs2_inode_scan *scan;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret) {
		verbosef(VL_LIB,
			 "%s while allocating a buffer for inode scanning\n",
			 error_message(ret));
		goto out;
	}

	di = (struct ocfs2_dinode *)buf;

	ret = ocfs2_open_inode_scan(fs, &scan);
	if (ret) {
		verbosef(VL_LIB,
			 "%s while opening inode scan\n",
			 error_message(ret));
		goto out_free;
	}

	for(;;) {
		ret = ocfs2_get_next_inode(scan, &blkno, buf);
		if (ret) {
			verbosef(VL_LIB, "%s while getting next inode\n",
				 error_message(ret));
			break;
		}
		if (blkno == 0)
			break;

		ret = tunefs_validate_inode(fs, di);
		if (ret)
			continue;

		if (func) {
			ret = func(fs, di, user_data);
			if (ret)
				break;
		}
	}

	ocfs2_close_inode_scan(scan);
out_free:
	ocfs2_free(&buf);

out:
	return ret;
}

/* A dirblock we have to add a trailer to */
struct tunefs_trailer_dirblock {
	struct list_head db_list;
	uint64_t db_blkno;
	char *db_buf;

	/*
	 * These require a little explanation.  They point to
	 * ocfs2_dir_entry structures inside db_buf.
	 *
	 * db_last is the entry we're going to *keep*.  If the last
	 * entry in the dirblock has enough extra rec_len to allow the
	 * trailer, db_last points to it.  We will shorten its rec_len
	 * and insert the trailer.
	 *
	 * However, if the last entry in the dirblock cannot be
	 * truncated, db_last points to the entry before that - the
	 * last entry we're keeping in this dirblock.
	 *
	 * Examples:
	 *
	 * - The last entry in the dirblock has a name_len of 1 and a
	 *   rec_len of 128.  We can easily change the rec_len to 64 and
	 *   insert the trailer.  db_last points to this entry.
	 *
	 * - The last entry in the dirblock has a name_len of 1 and a
	 *   rec_len of 48.  The previous entry has a name_len of 1 and a
	 *   rec_len of 32.  We have to move the last entry out.  The
	 *   second-to-last entry can have its rec_len truncated to 16, so
	 *   we put it in db_last.
	 */
	struct ocfs2_dir_entry *db_last;
};

void tunefs_trailer_context_free(struct tunefs_trailer_context *tc)
{
	struct tunefs_trailer_dirblock *db;
	struct list_head *n, *pos;

	if (!list_empty(&tc->d_list))
		list_del(&tc->d_list);

	list_for_each_safe(pos, n, &tc->d_dirblocks) {
		db = list_entry(pos, struct tunefs_trailer_dirblock, db_list);
		list_del(&db->db_list);
		ocfs2_free(&db->db_buf);
		ocfs2_free(&db);
	}

	ocfs2_free(&tc);
}

/*
 * We're calculating how many bytes we need to add to make space for
 * the dir trailers.  But we need to make sure that the added directory
 * blocks also have room for a trailer.
 */
static void add_bytes_needed(ocfs2_filesys *fs,
			     struct tunefs_trailer_context *tc,
			     unsigned int rec_len)
{
	unsigned int toff = ocfs2_dir_trailer_blk_off(fs);
	unsigned int block_offset = tc->d_bytes_needed % fs->fs_blocksize;

	/*
	 * If the current byte offset would put us into a trailer, push
	 * it out to the start of the next block.  Remember, dirents have
	 * to be at least 16 bytes, which is why we check against the
	 * smallest rec_len.
	 */
	if ((block_offset + rec_len) > (toff - OCFS2_DIR_REC_LEN(1)))
		tc->d_bytes_needed += fs->fs_blocksize - block_offset;

	tc->d_bytes_needed += rec_len;
	tc->d_blocks_needed =
		ocfs2_blocks_in_bytes(fs, tc->d_bytes_needed);
}

static errcode_t walk_dirblock(ocfs2_filesys *fs,
			       struct tunefs_trailer_context *tc,
			       struct tunefs_trailer_dirblock *db)
{
	errcode_t ret = 0;
	struct ocfs2_dir_entry *dirent, *prev = NULL;
	unsigned int real_rec_len;
	unsigned int offset = 0;
	unsigned int toff = ocfs2_dir_trailer_blk_off(fs);

	while (offset < fs->fs_blocksize) {
		dirent = (struct ocfs2_dir_entry *) (db->db_buf + offset);
		if (((offset + dirent->rec_len) > fs->fs_blocksize) ||
		    (dirent->rec_len < 8) ||
		    ((dirent->rec_len % 4) != 0) ||
		    (((dirent->name_len & 0xFF)+8) > dirent->rec_len)) {
			ret = OCFS2_ET_DIR_CORRUPTED;
			break;
		}

		real_rec_len = dirent->inode ?
			OCFS2_DIR_REC_LEN(dirent->name_len) :
			OCFS2_DIR_REC_LEN(1);
		if ((offset + real_rec_len) <= toff)
			goto next;

		/*
		 * The first time through, we store off the last dirent
		 * before the trailer.
		 */
		if (!db->db_last)
			db->db_last = prev;

		/* Only live dirents need to be moved */
		if (dirent->inode) {
			verbosef(VL_DEBUG,
				 "Will move dirent %.*s out of "
				 "directory block %"PRIu64" to make way "
				 "for the trailer\n",
				 dirent->name_len, dirent->name,
				 db->db_blkno);
			add_bytes_needed(fs, tc, real_rec_len);
		}

next:
		prev = dirent;
		offset += dirent->rec_len;
	}

	/* There were no dirents across the boundary */
	if (!db->db_last)
		db->db_last = prev;

	return ret;
}

static int dirblock_scan_iterate(ocfs2_filesys *fs, uint64_t blkno,
				 uint64_t bcount, uint16_t ext_flags,
				 void *priv_data)
{
	errcode_t ret = 0;
	struct tunefs_trailer_dirblock *db = NULL;
	struct tunefs_trailer_context *tc = priv_data;

	ret = ocfs2_malloc0(sizeof(struct tunefs_trailer_dirblock), &db);
	if (ret)
		goto out;

	ret = ocfs2_malloc_block(fs->fs_io, &db->db_buf);
	if (ret)
		goto out;

	db->db_blkno = blkno;

	verbosef(VL_DEBUG,
		 "Reading dinode %"PRIu64" dirblock %"PRIu64" at block "
		 "%"PRIu64"\n",
		 tc->d_di->i_blkno, bcount, blkno);
	ret = ocfs2_read_dir_block(fs, tc->d_di, blkno, db->db_buf);
	if (ret)
		goto out;

	ret = walk_dirblock(fs, tc, db);
	if (ret)
		goto out;

	list_add_tail(&db->db_list, &tc->d_dirblocks);
	db = NULL;

out:
	if (db) {
		if (db->db_buf)
			ocfs2_free(&db->db_buf);
		ocfs2_free(&db);
	}

	if (ret) {
		tc->d_err = ret;
		return OCFS2_BLOCK_ABORT;
	}

	return 0;
}

errcode_t tunefs_prepare_dir_trailer(ocfs2_filesys *fs,
				     struct ocfs2_dinode *di,
				     struct tunefs_trailer_context **tc_ret)
{
	errcode_t ret = 0;
	struct tunefs_trailer_context *tc = NULL;

	if (ocfs2_dir_has_trailer(fs, di))
		goto out;

	ret = ocfs2_malloc0(sizeof(struct tunefs_trailer_context), &tc);
	if (ret)
		goto out;

	tc->d_blkno = di->i_blkno;
	tc->d_di = di;
	INIT_LIST_HEAD(&tc->d_list);
	INIT_LIST_HEAD(&tc->d_dirblocks);

	ret = ocfs2_block_iterate_inode(fs, tc->d_di, 0,
					dirblock_scan_iterate, tc);
	if (!ret)
		ret = tc->d_err;
	if (ret)
		goto out;

	*tc_ret = tc;
	tc = NULL;

out:
	if (tc)
		tunefs_trailer_context_free(tc);

	return ret;
}

/*
 * We are hand-coding the directory expansion because we're going to
 * build the new directory blocks ourselves.  We can't just use
 * ocfs2_expand_dir() and ocfs2_link(), because we're moving around
 * entries.
 */
static errcode_t expand_dir_if_needed(ocfs2_filesys *fs,
				      struct ocfs2_dinode *di,
				      uint64_t blocks_needed)
{
	errcode_t ret = 0;
	uint64_t used_blocks, total_blocks;
	uint32_t clusters_needed;

	/* This relies on the fact that i_size of a directory is a
	 * multiple of blocksize */
	used_blocks = ocfs2_blocks_in_bytes(fs, di->i_size);
	total_blocks = ocfs2_clusters_to_blocks(fs, di->i_clusters);
	if ((used_blocks + blocks_needed) <= total_blocks)
		goto out;

	clusters_needed =
		ocfs2_clusters_in_blocks(fs,
					 (used_blocks + blocks_needed) -
					 total_blocks);
	ret = ocfs2_extend_allocation(fs, di->i_blkno, clusters_needed);
	if (ret)
		goto out;

	/* Pick up changes to the inode */
	ret = ocfs2_read_inode(fs, di->i_blkno, (char *)di);

out:
	return ret;
}

static void shift_dirent(ocfs2_filesys *fs,
			 struct tunefs_trailer_context *tc,
			 struct ocfs2_dir_entry *dirent)
{
	/* Using the real rec_len */
	unsigned int rec_len = OCFS2_DIR_REC_LEN(dirent->name_len);
	unsigned int offset, remain;

	/*
	 * If the current byte offset would put us into a trailer, push
	 * it out to the start of the next block.  Remember, dirents have
	 * to be at least 16 bytes, which is why we check against the
	 * smallest rec_len.
	 */
	if (rec_len > (tc->d_next_dirent->rec_len - OCFS2_DIR_REC_LEN(1))) {
		tc->d_cur_block += fs->fs_blocksize;
		tc->d_next_dirent = (struct ocfs2_dir_entry *)tc->d_cur_block;
	}

	assert(ocfs2_blocks_in_bytes(fs,
				     tc->d_cur_block - tc->d_new_blocks) <
	       tc->d_blocks_needed);

	offset = (char *)(tc->d_next_dirent) - tc->d_cur_block;
	remain = tc->d_next_dirent->rec_len - rec_len;

	memcpy(tc->d_cur_block + offset, dirent, rec_len);
	tc->d_next_dirent->rec_len = rec_len;

	verbosef(VL_DEBUG,
		 "Installed dirent %.*s at offset %u of new block "
		 "%"PRIu64", rec_len %u\n",
		 tc->d_next_dirent->name_len, tc->d_next_dirent->name,
		 offset,
		 ocfs2_blocks_in_bytes(fs, tc->d_cur_block - tc->d_new_blocks),
		 rec_len);


	offset += rec_len;
	tc->d_next_dirent =
		(struct ocfs2_dir_entry *)(tc->d_cur_block + offset);
	tc->d_next_dirent->rec_len = remain;

	verbosef(VL_DEBUG,
		 "New block %"PRIu64" has its last dirent at %u, with %u "
		 "bytes left\n",
		 ocfs2_blocks_in_bytes(fs, tc->d_cur_block - tc->d_new_blocks),
		 offset, remain);
}

static errcode_t fixup_dirblock(ocfs2_filesys *fs,
				struct tunefs_trailer_context *tc,
				struct tunefs_trailer_dirblock *db)
{
	errcode_t ret = 0;
	struct ocfs2_dir_entry *dirent;
	unsigned int real_rec_len;
	unsigned int offset;
	unsigned int toff = ocfs2_dir_trailer_blk_off(fs);

	/*
	 * db_last is the last dirent we're *keeping*.  So we need to 
	 * move out every valid dirent *after* db_last.
	 *
	 * tunefs_prepare_dir_trailer() should have calculated this
	 * correctly.
	 */
	offset = ((char *)db->db_last) - db->db_buf;
	offset += db->db_last->rec_len;
	while (offset < fs->fs_blocksize) {
		dirent = (struct ocfs2_dir_entry *) (db->db_buf + offset);
		if (((offset + dirent->rec_len) > fs->fs_blocksize) ||
		    (dirent->rec_len < 8) ||
		    ((dirent->rec_len % 4) != 0) ||
		    (((dirent->name_len & 0xFF)+8) > dirent->rec_len)) {
			ret = OCFS2_ET_DIR_CORRUPTED;
			break;
		}

		real_rec_len = dirent->inode ?
			OCFS2_DIR_REC_LEN(dirent->name_len) :
			OCFS2_DIR_REC_LEN(1);

		assert((offset + real_rec_len) > toff);

		/* Only live dirents need to be moved */
		if (dirent->inode) {
			verbosef(VL_DEBUG,
				 "Moving dirent %.*s out of directory "
				 "block %"PRIu64" to make way for the "
				 "trailer\n",
				 dirent->name_len, dirent->name,
				 db->db_blkno);
			shift_dirent(fs, tc, dirent);
		}

		offset += dirent->rec_len;
	}

	/*
	 * Now that we've moved any dirents out of the way, we need to
	 * fix up db_last and install the trailer.
	 */
	offset = ((char *)db->db_last) - db->db_buf;
	verbosef(VL_DEBUG,
		 "Last valid dirent of directory block %"PRIu64" "
		 "(\"%.*s\") is %u bytes in.  Setting rec_len to %u and "
		 "installing the trailer\n",
		 db->db_blkno, db->db_last->name_len, db->db_last->name,
		 offset, toff - offset);
	db->db_last->rec_len = toff - offset;
	ocfs2_init_dir_trailer(fs, tc->d_di, db->db_blkno, db->db_buf);

	return ret;
}

static errcode_t run_dirblocks(ocfs2_filesys *fs,
			       struct tunefs_trailer_context *tc)
{
	errcode_t ret = 0;
	struct list_head *pos;
	struct tunefs_trailer_dirblock *db;

	list_for_each(pos, &tc->d_dirblocks) {
		db = list_entry(pos, struct tunefs_trailer_dirblock, db_list);
		ret = fixup_dirblock(fs, tc, db);
		if (ret)
			break;
	}

	return ret;
}

static errcode_t write_dirblocks(ocfs2_filesys *fs,
				 struct tunefs_trailer_context *tc)
{
	errcode_t ret = 0;
	struct list_head *pos;
	struct tunefs_trailer_dirblock *db;

	list_for_each(pos, &tc->d_dirblocks) {
		db = list_entry(pos, struct tunefs_trailer_dirblock, db_list);
		ret = ocfs2_write_dir_block(fs, tc->d_di, db->db_blkno,
					    db->db_buf);
		if (ret) {
			verbosef(VL_DEBUG,
				 "Error writing dirblock %"PRIu64"\n",
				 db->db_blkno);
			break;
		}
	}

	return ret;
}

static errcode_t init_new_dirblocks(ocfs2_filesys *fs,
				    struct tunefs_trailer_context *tc)
{
	int i;
	errcode_t ret;
	uint64_t blkno;
	uint64_t orig_block = ocfs2_blocks_in_bytes(fs, tc->d_di->i_size);
	ocfs2_cached_inode *cinode;
	char *blockptr;
	struct ocfs2_dir_entry *first;

	ret = ocfs2_read_cached_inode(fs, tc->d_blkno, &cinode);
	if (ret)
		goto out;
	assert(!memcmp(tc->d_di, cinode->ci_inode, fs->fs_blocksize));

	for (i = 0; i < tc->d_blocks_needed; i++) {
		ret = ocfs2_extent_map_get_blocks(cinode, orig_block + i,
						  1, &blkno, NULL, NULL);
		if (ret)
			goto out;
		blockptr = tc->d_new_blocks + (i * fs->fs_blocksize);
		memset(blockptr, 0, fs->fs_blocksize);
		first = (struct ocfs2_dir_entry *)blockptr;
		first->rec_len = ocfs2_dir_trailer_blk_off(fs);
		ocfs2_init_dir_trailer(fs, tc->d_di, blkno, blockptr);
	}

out:
	return ret;
}

static errcode_t write_new_dirblocks(ocfs2_filesys *fs,
				     struct tunefs_trailer_context *tc)
{
	int i;
	errcode_t ret;
	uint64_t blkno;
	uint64_t orig_block = ocfs2_blocks_in_bytes(fs, tc->d_di->i_size);
	ocfs2_cached_inode *cinode;
	char *blockptr;

	ret = ocfs2_read_cached_inode(fs, tc->d_blkno, &cinode);
	if (ret)
		goto out;
	assert(!memcmp(tc->d_di, cinode->ci_inode, fs->fs_blocksize));

	for (i = 0; i < tc->d_blocks_needed; i++) {
		ret = ocfs2_extent_map_get_blocks(cinode, orig_block + i,
						  1, &blkno, NULL, NULL);
		if (ret)
			goto out;
		blockptr = tc->d_new_blocks + (i * fs->fs_blocksize);
		ret = ocfs2_write_dir_block(fs, tc->d_di, blkno, blockptr);
		if (ret) {
			verbosef(VL_DEBUG,
				 "Error writing dirblock %"PRIu64"\n",
				 blkno);
			goto out;
		}
	}

out:
	return ret;
}

errcode_t tunefs_install_dir_trailer(ocfs2_filesys *fs,
					struct ocfs2_dinode *di,
					struct tunefs_trailer_context *tc)
{
	errcode_t ret = 0;
	struct tunefs_trailer_context *our_tc = NULL;

	if ((di->i_dyn_features & OCFS2_INLINE_DATA_FL) ||
	    ocfs2_dir_has_trailer(fs, di))
		goto out;

	if (!tc) {
		ret = tunefs_prepare_dir_trailer(fs, di, &our_tc);
		if (ret)
			goto out;
		tc = our_tc;
	}

	if (tc->d_di != di) {
		ret = OCFS2_ET_INVALID_ARGUMENT;
		goto out;
	}

	if (tc->d_blocks_needed) {
		ret = ocfs2_malloc_blocks(fs->fs_io, tc->d_blocks_needed,
					  &tc->d_new_blocks);
		if (ret)
			goto out;

		tc->d_cur_block = tc->d_new_blocks;

		ret = expand_dir_if_needed(fs, di, tc->d_blocks_needed);
		if (ret)
			goto out;

		ret = init_new_dirblocks(fs, tc);
		if (ret)
			goto out;
		tc->d_next_dirent = (struct ocfs2_dir_entry *)tc->d_cur_block;
		verbosef(VL_DEBUG, "t_next_dirent has rec_len of %u\n",
			 tc->d_next_dirent->rec_len);
	}

	ret = run_dirblocks(fs, tc);
	if (ret)
		goto out;

	/*
	 * We write in a specific order.  We write any new dirblocks first
	 * so that they are on disk.  Then we write the new i_size in the
	 * inode.  If we crash at this point, the directory has duplicate
	 * entries but no lost entries.  fsck can clean it up.  Finally, we
	 * write the modified dirblocks with trailers.
	 */
	if (tc->d_blocks_needed) {
		ret = write_new_dirblocks(fs, tc);
		if (ret)
			goto out;

		di->i_size += ocfs2_blocks_to_bytes(fs, tc->d_blocks_needed);
		ret = ocfs2_write_inode(fs, di->i_blkno, (char *)di);
		if (ret)
			goto out;
	}

	ret = write_dirblocks(fs, tc);

out:
	if (our_tc)
		tunefs_trailer_context_free(our_tc);
	return ret;
}

/*
 * Starting, opening, closing, and exiting.
 */

static void tunefs_close_all(void)
{
	struct list_head *pos, *n;
	struct tunefs_private *tp;

	list_for_each_safe(pos, n, &fs_list) {
		tp = list_entry(pos, struct tunefs_private, tp_list);
		tunefs_close(tp->tp_fs);
	}
}

static void handle_signal(int caught_sig)
{
	int exitp = 0, abortp = 0;
	static int segv_already = 0;

	switch (caught_sig) {
		case SIGQUIT:
			abortp = 1;
			/* FALL THROUGH */

		case SIGTERM:
		case SIGINT:
		case SIGHUP:
			errorf("Caught signal %d, exiting\n", caught_sig);
			exitp = 1;
			break;

		case SIGSEGV:
			errorf("Segmentation fault, exiting\n");
			exitp = 1;
			if (segv_already) {
				errorf("Segmentation fault loop detected\n");
				abortp = 1;
			} else
				segv_already = 1;
			break;

		default:
			errorf("Caught signal %d, ignoring\n", caught_sig);
			break;
	}

	if (!exitp)
		return;

	if (abortp)
		abort();

	tunefs_close_all();

	exit(1);
}

static int setup_signals(void)
{
	int rc = 0;
	struct sigaction act;

	act.sa_sigaction = NULL;
	sigemptyset(&act.sa_mask);
	act.sa_handler = handle_signal;
#ifdef SA_INTERRUPT
	act.sa_flags = SA_INTERRUPT;
#endif

	rc += sigaction(SIGTERM, &act, NULL);
	rc += sigaction(SIGINT, &act, NULL);
	rc += sigaction(SIGHUP, &act, NULL);
	rc += sigaction(SIGQUIT, &act, NULL);
	rc += sigaction(SIGSEGV, &act, NULL);
	act.sa_handler = SIG_IGN;
	rc += sigaction(SIGPIPE, &act, NULL);  /* Get EPIPE instead */

	return rc;
}

void tunefs_init(const char *argv0)
{
	initialize_o2ne_error_table();
	initialize_ocfs_error_table();
	initialize_o2dl_error_table();
	initialize_o2cb_error_table();

	tools_setup_argv0(argv0);

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	if (setup_signals()) {
		errorf("%s\n", error_message(TUNEFS_ET_SIGNALS_FAILED));
		exit(1);
	}
}

/*
 * Single-node filesystems need to prevent mount(8) from happening
 * while tunefs.ocfs2 is running.  bd_claim does this for us when we
 * open O_EXCL.
 */
static errcode_t tunefs_lock_local(ocfs2_filesys *fs, int flags)
{
	errcode_t err = 0;
	int mount_flags;
	int rc;
	struct tunefs_filesystem_state *state = tunefs_get_state(fs);

	if (state->ts_local_fd > -1)
		return 0;

	rc = open64(fs->fs_devname, O_RDWR | O_EXCL);
	if (rc < 0) {
		if (errno == EBUSY) {
			/* bd_claim has a hold, let's see if it's ocfs2 */
			err = ocfs2_check_if_mounted(fs->fs_devname,
						     &mount_flags);
			if (!err) {
				if (!(mount_flags & OCFS2_MF_MOUNTED) ||
				    (mount_flags & OCFS2_MF_READONLY) ||
				    (mount_flags & OCFS2_MF_SWAP) ||
				    !(flags & TUNEFS_FLAG_ONLINE))
					err = TUNEFS_ET_DEVICE_BUSY;
				else
					err = TUNEFS_ET_PERFORM_ONLINE;
			}
		} else if (errno == ENOENT)
			err = OCFS2_ET_NAMED_DEVICE_NOT_FOUND;
		else
			err = OCFS2_ET_IO;
	} else
		state->ts_local_fd = rc;

	return err;
}

static void tunefs_unlock_local(ocfs2_filesys *fs)
{
	struct tunefs_filesystem_state *state = tunefs_get_state(fs);

	assert(state->ts_master == fs);
	if (state->ts_local_fd > -1) {
		close(state->ts_local_fd);  /* Don't care about errors */
		state->ts_local_fd = -1;
	}
}

static errcode_t tunefs_unlock_cluster(ocfs2_filesys *fs)
{
	errcode_t tmp, err = 0;
	struct tunefs_filesystem_state *state = tunefs_get_state(fs);
	struct tools_progress *prog = NULL;

	if (fs->fs_dlm_ctxt)
		prog = tools_progress_start("Unlocking filesystem",
					    "unlocking", 2);
	/*
	 * We continue even with no progress, because we're unlocking
	 * and probably exiting.
	 */

	assert(state->ts_master == fs);
	if (state->ts_cluster_locked) {
		assert(fs->fs_dlm_ctxt);

		tunefs_block_signals();
		err = ocfs2_release_cluster(fs);
		tunefs_unblock_signals();
		state->ts_cluster_locked = 0;
	}
	if (prog)
		tools_progress_step(prog, 1);

	/* We shut down the dlm regardless of err */
	if (fs->fs_dlm_ctxt) {
		tmp = ocfs2_shutdown_dlm(fs, WHOAMI);
		if (!err)
			err = tmp;
	}
	if (prog) {
		tools_progress_step(prog, 1);
		tools_progress_stop(prog);
	}

	return err;
}

/*
 * We only unlock if we're closing the master filesystem.  We unlock
 * both local and cluster locks, because we may have started as a local
 * filesystem, then switched to a cluster filesystem in the middle.
 */
static errcode_t tunefs_unlock_filesystem(ocfs2_filesys *fs)
{
	errcode_t err = 0;
	struct tunefs_filesystem_state *state = tunefs_get_state(fs);

	if (state->ts_master == fs) {
		tunefs_unlock_local(fs);
		err = tunefs_unlock_cluster(fs);
	}

	return err;
}

static errcode_t tunefs_lock_cluster(ocfs2_filesys *fs, int flags)
{
	errcode_t err = 0;
	struct tunefs_filesystem_state *state = tunefs_get_state(fs);
	ocfs2_filesys *master_fs = state->ts_master;
	struct tools_progress *prog = NULL;

	if (state->ts_cluster_locked)
		goto out;

	if (flags & TUNEFS_FLAG_SKIPCLUSTER) {
		err = TUNEFS_ET_CLUSTER_SKIPPED;
		goto out;
	}

	prog = tools_progress_start("Locking filesystem", "locking", 2);
	if (!prog) {
		err = TUNEFS_ET_NO_MEMORY;
		goto out;
	}

	if (!master_fs->fs_dlm_ctxt) {
		err = o2cb_init();
		if (err)
			goto out;

		err = ocfs2_initialize_dlm(master_fs, WHOAMI);
		if (flags & TUNEFS_FLAG_NOCLUSTER) {
			if (err == O2CB_ET_INVALID_STACK_NAME ||
			    err == O2CB_ET_INVALID_CLUSTER_NAME ||
			    err == O2CB_ET_INVALID_HEARTBEAT_MODE) {
				/*
				 * We expected this - why else ask for
				 * TUNEFS_FLAG_NOCLUSTER?
				 *
				 * Note that this is distinct from the O2CB
				 * error, as that is a real error when
				 * TUNEFS_FLAG_NOCLUSTER is not specified.
				 */
				err = TUNEFS_ET_INVALID_STACK_NAME;
			}
			/*
			 * Success means do nothing, any other error
			 * propagates up.
			 */
			goto out;
		} else if (err)
			goto out;
	}

	tools_progress_step(prog, 1);

	tunefs_block_signals();
	err = ocfs2_lock_down_cluster(master_fs);
	tunefs_unblock_signals();
	if (!err)
		state->ts_cluster_locked = 1;
	else if ((err == O2DLM_ET_TRYLOCK_FAILED) &&
		 (flags & TUNEFS_FLAG_ONLINE))
		err = TUNEFS_ET_PERFORM_ONLINE;
	else
		ocfs2_shutdown_dlm(fs, WHOAMI);

	tools_progress_step(prog, 1);

out:
	if (prog)
		tools_progress_stop(prog);

	return err;
}

/*
 * We try to lock the filesystem in *this* ocfs2_filesys.  We get the
 * state off of the master, but the filesystem may have changed since
 * the master opened its ocfs2_filesys.  It might have been switched to
 * LOCAL or something.  We trust the current status in order to make our
 * decision.
 *
 * Inside the underlying lock functions, they check the state to see if
 * they actually need to do anything.  If they don't have it locked, they
 * will always retry the lock.  The filesystem may have gotten unmounted
 * right after we ran our latest online operation.
 */
static errcode_t tunefs_lock_filesystem(ocfs2_filesys *fs, int flags)
{
	errcode_t err = 0;

	if (ocfs2_mount_local(fs))
		err = tunefs_lock_local(fs, flags);
	else
		err = tunefs_lock_cluster(fs, flags);

	return err;
}

static int tunefs_count_free_bits(struct ocfs2_group_desc *gd)
{
	int end = 0;
	int start;
	int bits = 0;

	while (end < gd->bg_bits) {
		start = ocfs2_find_next_bit_clear(gd->bg_bitmap, gd->bg_bits, end);
		if (start >= gd->bg_bits)
			break;
		end = ocfs2_find_next_bit_set(gd->bg_bitmap, gd->bg_bits, start);
		bits += (end - start);
	}

	return bits;
}

static errcode_t tunefs_validate_chain_group(ocfs2_filesys *fs,
					     struct ocfs2_dinode *di,
					     int chain)
{
	errcode_t ret = 0;
	uint64_t blkno;
	char *buf = NULL;
	struct ocfs2_group_desc *gd;
	struct ocfs2_chain_list *cl;
	struct ocfs2_chain_rec *cr;
	uint32_t total = 0;
	uint32_t free = 0;
	uint16_t bits;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret) {
		verbosef(VL_LIB,
			 "%s while allocating a buffer for chain group "
			 "validation\n",
			 error_message(ret));
		goto bail;
	}

	total = 0;
	free = 0;

	cl = &(di->id2.i_chain);
	cr = &(cl->cl_recs[chain]);
	blkno = cr->c_blkno;

	while (blkno) {
		ret = ocfs2_read_group_desc(fs, blkno, buf);
		if (ret) {
			verbosef(VL_LIB,
				 "%s while reading chain group descriptor "
				 "at block %"PRIu64"\n",
				 error_message(ret), blkno);
			goto bail;
		}

		gd = (struct ocfs2_group_desc *)buf;

		if (gd->bg_parent_dinode != di->i_blkno) {
			ret = OCFS2_ET_CORRUPT_CHAIN;
			verbosef(VL_LIB,
				 "Chain allocator at block %"PRIu64" is "
				 "corrupt.  It contains group descriptor "
				 "at %"PRIu64", but that descriptor says "
				 "it belongs to allocator %"PRIu64"\n",
				 (uint64_t)di->i_blkno, blkno,
				 (uint64_t)gd->bg_parent_dinode);
			goto bail;
		}

		if (gd->bg_chain != chain) {
			ret = OCFS2_ET_CORRUPT_CHAIN;
			verbosef(VL_LIB,
				 "Chain allocator at block %"PRIu64" is "
				 "corrupt.  Group descriptor at %"PRIu64" "
				 "was found on chain %u, but it says it "
				 "belongs to chain %u\n",
				 (uint64_t)di->i_blkno, blkno,
				 chain, gd->bg_chain);
			goto bail;
		}

		bits = tunefs_count_free_bits(gd);
		if (bits != gd->bg_free_bits_count) {
			ret = OCFS2_ET_CORRUPT_CHAIN;
			verbosef(VL_LIB,
				 "Chain allocator at block %"PRIu64" is "
				 "corrupt.  Group descriptor at %"PRIu64" "
				 "has %u free bits but says it has %u\n",
				 (uint64_t)di->i_blkno, (uint64_t)blkno,
				 bits, gd->bg_free_bits_count);
			goto bail;
		}

		if (gd->bg_bits > gd->bg_size * 8) {
			ret = OCFS2_ET_CORRUPT_CHAIN;
			verbosef(VL_LIB,
				 "Chain allocator at block %"PRIu64" is "
				 "corrupt.  Group descriptor at %"PRIu64" "
				 "can only hold %u bits, but it claims to "
				 "have %u\n",
				 (uint64_t)di->i_blkno, (uint64_t)blkno,
				 gd->bg_size * 8, gd->bg_bits);
			goto bail;
		}

		if (gd->bg_free_bits_count >= gd->bg_bits) {
			ret = OCFS2_ET_CORRUPT_CHAIN;
			verbosef(VL_LIB,
				 "Chain allocator at block %"PRIu64" is "
				 "corrupt.  Group descriptor at %"PRIu64" "
				 "claims to have more free bits than "
				 "total bits\n",
				 (uint64_t)di->i_blkno, (uint64_t)blkno);
			goto bail;
		}

		total += gd->bg_bits;
		free += gd->bg_free_bits_count;
		blkno = gd->bg_next_group;
	}

	if (cr->c_total != total) {
		ret = OCFS2_ET_CORRUPT_CHAIN;
		verbosef(VL_LIB,
			 "Chain allocator at block %"PRIu64" is corrupt. "
			 "It contains %u total bits, but it says it has "
			 "%u\n",
			 (uint64_t)di->i_blkno, total, cr->c_total);
		goto bail;

	}

	if (cr->c_free != free) {
		ret = OCFS2_ET_CORRUPT_CHAIN;
		verbosef(VL_LIB,
			 "Chain allocator at block %"PRIu64" is corrupt. "
			 "It contains %u free bits, but it says it has "
			 "%u\n",
			 (uint64_t)di->i_blkno, free, cr->c_free);
		goto bail;
	}

bail:
	if (buf)
		ocfs2_free(&buf);

	return ret;
}

static errcode_t tunefs_global_bitmap_check(ocfs2_filesys *fs)
{
	errcode_t ret = 0;
	uint64_t bm_blkno = 0;
	char *buf = NULL;
	struct ocfs2_chain_list *cl;
	struct ocfs2_dinode *di;
	int i;

	verbosef(VL_LIB, "Verifying the global allocator\n");

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret) {
		verbosef(VL_LIB,
			 "%s while allocating an inode buffer to validate "
			 "the global bitmap\n",
			 error_message(ret));
		goto bail;
	}

	ret = ocfs2_lookup_system_inode(fs, GLOBAL_BITMAP_SYSTEM_INODE, 0,
					&bm_blkno);
	if (ret) {
		verbosef(VL_LIB,
			 "%s while looking up the global bitmap inode\n",
			 error_message(ret));
		goto bail;
	}

	ret = ocfs2_read_inode(fs, bm_blkno, buf);
	if (ret) {
		verbosef(VL_LIB,
			 "%s while reading the global bitmap inode at "
			 "block %"PRIu64"",
			 error_message(ret), bm_blkno);
		goto bail;
	}

	di = (struct ocfs2_dinode *)buf;
	cl = &(di->id2.i_chain);

	/* Warm up the cache with the groups */
	ret = ocfs2_cache_chain_allocator_blocks(fs, di);
	if (ret)
		verbosef(VL_LIB, "Caching global bitmap failed, err %d\n",
			 (int)ret);
	ret = 0;

	for (i = 0; i < cl->cl_next_free_rec; ++i) {
		ret = tunefs_validate_chain_group(fs, di, i);
		if (ret)
			goto bail;
	}

bail:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

static errcode_t tunefs_open_bitmap_check(ocfs2_filesys *fs)
{
	struct tunefs_private *tp = to_private(fs);
	struct tunefs_filesystem_state *state = tunefs_get_state(fs);

	if (!(tp->tp_open_flags & TUNEFS_FLAG_ALLOCATION))
		return 0;

	state->ts_allocation = 1;
	return tunefs_global_bitmap_check(fs);
}

void tunefs_update_fs_clusters(ocfs2_filesys *fs)
{
	struct tunefs_private *tp = to_private(fs);
	struct tunefs_filesystem_state *state = tunefs_get_state(fs);

	if (!(tp->tp_open_flags & TUNEFS_FLAG_ALLOCATION)) {
		verbosef(VL_LIB,
			 "Operation that claimed it would do no allocation "
			 "just attempted to update the filesystem size\n");
		return;
	}

	state->ts_fs_clusters = fs->fs_clusters;
}

static errcode_t tunefs_close_bitmap_check(ocfs2_filesys *fs)
{
	errcode_t ret;
	uint32_t old_clusters;
	struct tunefs_filesystem_state *state = tunefs_get_state(fs);

	if (!state->ts_allocation)
		return 0;

	if (state->ts_master != fs)
		return 0;

	/*
	 * An operation that resized the filesystem will have called
	 * tunefs_update_fs_clusters().  The bitmap check needs this
	 * new value, so we swap it in for the call.
	 */
	old_clusters = fs->fs_clusters;
	fs->fs_clusters = state->ts_fs_clusters;
	fs->fs_blocks = ocfs2_clusters_to_blocks(fs, fs->fs_clusters);
	ret = tunefs_global_bitmap_check(fs);
	fs->fs_clusters = old_clusters;
	fs->fs_blocks = ocfs2_clusters_to_blocks(fs, fs->fs_clusters);

	return ret;
}

static errcode_t tunefs_journal_check(ocfs2_filesys *fs)
{
	errcode_t ret;
	char *jsb_buf = NULL;
	ocfs2_cached_inode *ci = NULL;
	uint64_t blkno, contig;
	journal_superblock_t *jsb;
	int i, dirty = 0;
	uint16_t max_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;
	struct tunefs_private *tp = to_private(fs);
	struct tunefs_filesystem_state *state = tunefs_get_state(fs);

	/* We only need to check the journal once */
	if (state->ts_journal_clusters)
		return 0;

	verbosef(VL_LIB, "Checking for dirty journals\n");

	ret = ocfs2_malloc_block(fs->fs_io, &jsb_buf);
	if (ret) {
		verbosef(VL_LIB,
			"%s while allocating a block during journal "
			"check\n",
			error_message(ret));
		goto bail;
	}

	for (i = 0; i < max_slots; ++i) {
		ret = ocfs2_lookup_system_inode(fs, JOURNAL_SYSTEM_INODE, i,
						&blkno);
		if (ret) {
			verbosef(VL_LIB,
				 "%s while looking up journal inode for "
				 "slot %u during journal check\n",
				 error_message(ret), i);
			goto bail;
		}

		ret = ocfs2_read_cached_inode(fs, blkno, &ci);
		if (ret) {
			verbosef(VL_LIB,
				 "%s while reading inode %"PRIu64" during "
				 " journal check",
				 error_message(ret), blkno);
			goto bail;
		}

		state->ts_journal_clusters =
			ocfs2_max(state->ts_journal_clusters,
				  ci->ci_inode->i_clusters);

		dirty = (ci->ci_inode->id1.journal1.ij_flags &
			 OCFS2_JOURNAL_DIRTY_FL);
		if (dirty) {
			ret = TUNEFS_ET_JOURNAL_DIRTY;
			verbosef(VL_LIB,
				 "Node slot %d's journal is dirty. Run "
				 "fsck.ocfs2 to replay all dirty journals.",
				 i);
			break;
		}

		ret = ocfs2_extent_map_get_blocks(ci, 0, 1, &blkno, &contig, NULL);
		if (!ret)
			ret = ocfs2_read_journal_superblock(fs, blkno,
							    jsb_buf);
		if (ret) {
			verbosef(VL_LIB,
				 "%s while reading journal superblock "
				 "for inode %"PRIu64" during journal "
				 "check",
				 error_message(ret), ci->ci_blkno);
			goto bail;
		}

		jsb = (journal_superblock_t *)jsb_buf;
		state->ts_journal_features.opt_compat |=
			jsb->s_feature_compat;
		state->ts_journal_features.opt_ro_compat |=
			jsb->s_feature_ro_compat;
		state->ts_journal_features.opt_incompat |=
			jsb->s_feature_incompat;
	}

	/*
	 * If anything follows a NOCLUSTER operation, it will have
	 * closed and reopened the filesystem.  It must recheck the
	 * journals.
	 */
	if (tp->tp_open_flags & TUNEFS_FLAG_NOCLUSTER)
		state->ts_journal_clusters = 0;

bail:
	if (ci)
		ocfs2_free_cached_inode(fs, ci);
	if (jsb_buf)
		ocfs2_free(&jsb_buf);

	return ret;
}

static errcode_t tunefs_open_online_descriptor(ocfs2_filesys *fs)
{
	int rc, flags = 0;
	errcode_t ret = 0;
	char mnt_dir[PATH_MAX];
	struct tunefs_filesystem_state *state = tunefs_get_state(fs);

	if (state->ts_online_fd > -1)
		goto out;

	memset(mnt_dir, 0, sizeof(mnt_dir));

	ret = ocfs2_check_mount_point(fs->fs_devname, &flags,
				      mnt_dir, sizeof(mnt_dir));
	if (ret)
		goto out;

	if (!(flags & OCFS2_MF_MOUNTED) ||
	    (flags & OCFS2_MF_READONLY) ||
	    (flags & OCFS2_MF_SWAP)) {
		ret = TUNEFS_ET_NOT_MOUNTED;
		goto out;
	}

	rc = open64(mnt_dir, O_RDONLY);
	if (rc < 0) {
		if (errno == EBUSY)
			ret = TUNEFS_ET_DEVICE_BUSY;
		else if (errno == ENOENT)
			ret = TUNEFS_ET_NOT_MOUNTED;
		else
			ret = OCFS2_ET_IO;
	} else
		state->ts_online_fd = rc;

out:
	return ret;
}

static void tunefs_close_online_descriptor(ocfs2_filesys *fs)
{
	struct tunefs_filesystem_state *state = tunefs_get_state(fs);

	if ((state->ts_master == fs) && (state->ts_online_fd > -1)) {
		close(state->ts_online_fd);  /* Don't care about errors */
		state->ts_online_fd = -1;
	}
}

/*
 * If io_init_cache fails, we will go do the work without the
 * io_cache, so there is no check for failure here.
 */
static void tunefs_init_cache(ocfs2_filesys *fs)
{
	errcode_t err;
	struct tunefs_private *tp = to_private(fs);
	struct tunefs_filesystem_state *state = tunefs_get_state(fs);
	uint64_t blocks_wanted;
	int scale_down;

	/*
	 * We have one I/O cache for all ocfs2_filesys structures.  This
	 * guarantees a consistent view of the disk.  The master filesys
	 * allocates it, child filesyses just use it.
	 */
	if (state->ts_master != fs) {
		io_share_cache(state->ts_master->fs_io, fs->fs_io);
		return;
	}

	/*
	 * Operations needing a large cache really want enough to
	 * hold the whole filesystem in memory.  The rest of the
	 * operations don't need much at all.  A cache big enough to
	 * hold a chain allocator group should be enough.  Our largest
	 * chain allocator is 4MB, so let's do 8MB and allow for
	 * incidental blocks.
	 */
	if (tp->tp_open_flags & TUNEFS_FLAG_LARGECACHE)
		blocks_wanted = fs->fs_blocks;
	else
		blocks_wanted = ocfs2_blocks_in_bytes(fs, 8 * 1024 * 1024);

	/*
	 * We don't want to exhaust memory, so we start with twice our
	 * actual need.  When we find out how much we can get, we actually
	 * get half that.
	 */
	blocks_wanted <<= 1;
	scale_down = 1;

	while (blocks_wanted > 0) {
		io_destroy_cache(fs->fs_io);
		verbosef(VL_LIB,
			 "Asking for %"PRIu64" blocks of I/O cache\n",
			 blocks_wanted);
		err = io_init_cache(fs->fs_io, blocks_wanted);
		if (!err) {
			/*
			 * We want to pin our cache; there's no point in
			 * having a large cache if half of it is in swap.
			 * However, some callers may not be privileged
			 * enough, so once we get down to a small enough
			 * number (512 blocks), we'll stop caring.
			 */
			err = io_mlock_cache(fs->fs_io);
			if (err && (blocks_wanted <= 512))
				err = 0;
		}
		if (!err) {
			verbosef(VL_LIB, "Got %"PRIu64" blocks\n",
				 blocks_wanted);
			/* If we've already scaled down, we're done. */
			if (!scale_down)
				break;
			scale_down = 0;
		}

		blocks_wanted >>= 1;
	}
}

static errcode_t tunefs_add_fs(ocfs2_filesys *fs, int flags)
{
	errcode_t err;
	struct tunefs_private *tp;

	err = ocfs2_malloc0(sizeof(struct tunefs_private), &tp);
	if (err)
		goto out;

	tp->tp_open_flags = flags;
	fs->fs_private = tp;
	tp->tp_fs = fs;

	err = tunefs_set_state(fs);
	if (err) {
		fs->fs_private = NULL;
		ocfs2_free(&tp);
		goto out;
	}

	/*
	 * This is purposely a push.  The first open of the filesystem
	 * will be the one holding the locks, so we want it to be the last
	 * close (a FILO stack).  When signals happen, tunefs_close_all()
	 * pops each off in turn, finishing with the lock holder.
	 */
	list_add(&tp->tp_list, &fs_list);

out:
	return err;
}

static void tunefs_remove_fs(ocfs2_filesys *fs)
{
	struct tunefs_private *tp = to_private(fs);
	struct tunefs_filesystem_state *s = NULL;

	if (tp) {
		s = tp->tp_state;
		list_del(&tp->tp_list);
		tp->tp_fs = NULL;
		fs->fs_private = NULL;
		ocfs2_free(&tp);
	}

	if (s && (s->ts_master == fs)) {
		assert(list_empty(&fs_list));
		ocfs2_free(&s);
	}
}


/*
 * Return true if this error code is a special (non-fatal) ocfs2ne
 * error code.
 */
static int tunefs_special_errorp(errcode_t err)
{
	if (err == TUNEFS_ET_CLUSTER_SKIPPED)
		return 1;
	if (err == TUNEFS_ET_INVALID_STACK_NAME)
		return 1;
	if (err == TUNEFS_ET_PERFORM_ONLINE)
		return 1;

	return 0;
}

errcode_t tunefs_open(const char *device, int flags,
		      ocfs2_filesys **ret_fs)
{
	int rw = flags & TUNEFS_FLAG_RW;
	errcode_t err, tmp;
	int open_flags;
	ocfs2_filesys *fs = NULL;

	verbosef(VL_LIB, "Opening device \"%s\"\n", device);

	open_flags = OCFS2_FLAG_HEARTBEAT_DEV_OK;
	if (rw)
		open_flags |= OCFS2_FLAG_RW | OCFS2_FLAG_STRICT_COMPAT_CHECK;
	else
		open_flags |= OCFS2_FLAG_RO;

	err = ocfs2_open(device, open_flags, 0, 0, &fs);
	if (err)
		goto out;

	err = tunefs_add_fs(fs, flags);
	if (err)
		goto out;

	if (!rw)
		goto out;

	if (OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat &
	    OCFS2_FEATURE_INCOMPAT_HEARTBEAT_DEV) {
		err = TUNEFS_ET_HEARTBEAT_DEV;
		goto out;
	}

	if (OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat &
	    OCFS2_FEATURE_INCOMPAT_RESIZE_INPROG) {
		err = TUNEFS_ET_RESIZE_IN_PROGRESS;
		goto out;
	}

	if (OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat &
	    OCFS2_FEATURE_INCOMPAT_TUNEFS_INPROG) {
		err = TUNEFS_ET_TUNEFS_IN_PROGRESS;
		goto out;
	}

	err = tunefs_lock_filesystem(fs, flags);
	if (err && !tunefs_special_errorp(err))
		goto out;

	/*
	 * We will use block cache in io.  Now, whether the cluster is
	 * locked or the volume is mount local, in both situation we can
	 * safely use cache.  If we're not locked
	 * (tunefs_special_errorp(err) != 0), we can't safely use it.
	 * If this tunefs run has both special and regular operations,
	 * ocfs2ne will retry with the regular arguments and will get
	 * the cache for the regular operations.
	 */
	if (!err)
		tunefs_init_cache(fs);

	/*
	 * SKIPCLUSTER operations don't check the journals - they couldn't
	 * replay them anyway.
	 */
	if (err == TUNEFS_ET_CLUSTER_SKIPPED)
		goto out;

	/* Offline operations need clean journals */
	if (err != TUNEFS_ET_PERFORM_ONLINE) {
		tmp = tunefs_journal_check(fs);
		if (!tmp)
			tmp = tunefs_open_bitmap_check(fs);
		if (tmp) {
			err = tmp;
			tunefs_unlock_filesystem(fs);
		}
	} else {
		tmp = tunefs_open_online_descriptor(fs);
		if (tmp) {
			err = tmp;
			tunefs_unlock_filesystem(fs);
		}
	}

out:
	if (err && !tunefs_special_errorp(err)) {
		if (fs) {
			tunefs_remove_fs(fs);
			ocfs2_close(fs);
			fs = NULL;
		}
		verbosef(VL_LIB, "Open of device \"%s\" failed\n", device);
	} else {
		verbosef(VL_LIB, "Device \"%s\" opened\n", device);
		*ret_fs = fs;
	}

	return err;
}

int tunefs_is_journal64(ocfs2_filesys *fs)
{
	struct tunefs_filesystem_state *state = tunefs_get_state(fs);
	if (state->ts_journal_features.opt_incompat & JBD2_FEATURE_INCOMPAT_64BIT)
		return 1;
	return 0;
}

errcode_t tunefs_close(ocfs2_filesys *fs)
{
	errcode_t tmp, err = 0;

	/*
	 * We want to clean up everything we can even if there
	 * are errors, but we preserve the first error we get.
	 */
	if (fs) {
		verbosef(VL_LIB, "Closing device \"%s\"\n", fs->fs_devname);
		tunefs_close_online_descriptor(fs);
		err = tunefs_close_bitmap_check(fs);
		tmp = tunefs_unlock_filesystem(fs);
		if (!err)
			err = tmp;

		tunefs_remove_fs(fs);
		tmp = ocfs2_close(fs);
		if (!err)
			err = tmp;

		if (!err)
			verbosef(VL_LIB, "Device closed\n");
		else
			verbosef(VL_LIB, "Close of device failed\n");
		fs = NULL;
	}

	return err;
}


/*
 * Helper functions for the main code.
 */

errcode_t tunefs_feature_run(ocfs2_filesys *master_fs,
			     struct tunefs_feature *feat)
{
	int rc = 0;
	errcode_t err, tmp;
	ocfs2_filesys *fs;
	int flags;

	verbosef(VL_DEBUG, "Running feature \"%s\"\n", feat->tf_name);

	flags = feat->tf_open_flags & ~(TUNEFS_FLAG_ONLINE |
				      TUNEFS_FLAG_NOCLUSTER);
	err = tunefs_open(master_fs->fs_devname, feat->tf_open_flags, &fs);
	if (err == TUNEFS_ET_PERFORM_ONLINE)
		flags |= TUNEFS_FLAG_ONLINE;
	else if (err == TUNEFS_ET_INVALID_STACK_NAME)
		flags |= TUNEFS_FLAG_NOCLUSTER;
	else if (err)
		goto out;

	err = 0;
	switch (feat->tf_action) {
		case FEATURE_ENABLE:
			rc = feat->tf_enable(fs, flags);
			break;

		case FEATURE_DISABLE:
			rc = feat->tf_disable(fs, flags);
			break;

		case FEATURE_NOOP:
			verbosef(VL_APP,
				 "Ran NOOP for feature \"%s\" - how'd "
				 "that happen?\n",
				 feat->tf_name);
			break;

		default:
			errorf("Unknown action %d called against feature "
			       "\"%s\"\n",
			       feat->tf_action, feat->tf_name);
			err = TUNEFS_ET_INTERNAL_FAILURE;
			break;
	}

	if (rc)
		err = TUNEFS_ET_OPERATION_FAILED;

	tmp = tunefs_close(fs);
	if (!err)
		err = tmp;

out:
	return err;
}

errcode_t tunefs_op_run(ocfs2_filesys *master_fs,
			struct tunefs_operation *op)
{
	errcode_t err, tmp;
	ocfs2_filesys *fs;
	int flags;

	verbosef(VL_DEBUG, "Running operation \"%s\"\n", op->to_name);

	flags = op->to_open_flags & ~(TUNEFS_FLAG_ONLINE |
				      TUNEFS_FLAG_NOCLUSTER);
	err = tunefs_open(master_fs->fs_devname, op->to_open_flags, &fs);
	if (err == TUNEFS_ET_PERFORM_ONLINE)
		flags |= TUNEFS_FLAG_ONLINE;
	else if (err == TUNEFS_ET_INVALID_STACK_NAME)
		flags |= TUNEFS_FLAG_NOCLUSTER;
	else if (err == TUNEFS_ET_CLUSTER_SKIPPED)
		flags |= TUNEFS_FLAG_SKIPCLUSTER;
	else if (err)
		goto out;

	err = 0;
	if (op->to_run(op, fs, flags))
		err = TUNEFS_ET_OPERATION_FAILED;

	tmp = tunefs_close(fs);
	if (!err)
		err = tmp;

out:
	return err;
}


/*
 * Helper calls for operation and feature DEBUG_EXE code
 */

static errcode_t copy_argv(char **argv, char ***new_argv)
{
	int i;
	char **t_argv;

	for (i = 0; argv[i]; i++)
		;  /* Count argv */

	/* This is intentionally leaked */
	t_argv = malloc(sizeof(char *) * (i + 1));
	if (!t_argv)
		return TUNEFS_ET_NO_MEMORY;

	for (i = 0; argv[i]; i++)
		t_argv[i] = (char *)argv[i];
	t_argv[i] = NULL;

	*new_argv = t_argv;
	return 0;
}

/* All the +1 are to leave argv[0] in place */
static void shuffle_argv(int *argc, int optind, char **argv)
{
	int src, dst;
	int new_argc = *argc - optind + 1;

	for (src = optind, dst = 1; src < *argc; src++, dst++)
		argv[dst] = argv[src];
	if (dst != new_argc)
		verbosef(VL_DEBUG,
			 "dst is not new_argc %d %d\n", dst, new_argc);

	argv[dst] = NULL;
	*argc = new_argc;
}

static void tunefs_debug_usage(int error)
{
	enum tools_verbosity_level level = VL_ERR;

	if (!error)
		level = VL_OUT;

	verbosef(level, "%s", usage_string ? usage_string : "(null)");
	verbosef(level,
		 "[opts] can be any mix of:\n"
		 "\t-i|--interactive\n"
		 "\t-v|--verbose (more than one increases verbosity)\n"
		 "\t-q|--quiet (more than one decreases verbosity)\n"
		 "\t-h|--help\n"
		 "\t-V|--version\n");
}

extern int optind, opterr, optopt;
extern char *optarg;
static void tunefs_parse_core_options(int *argc, char ***argv, char *usage)
{
	errcode_t err;
	int c;
	char **new_argv;
	int print_usage = 0, print_version = 0;
	char error[PATH_MAX];
	static struct option long_options[] = {
		{ "help", 0, NULL, 'h' },
		{ "version", 0, NULL, 'V' },
		{ "verbose", 0, NULL, 'v' },
		{ "quiet", 0, NULL, 'q' },
		{ "interactive", 0, NULL, 'i'},
		{ 0, 0, 0, 0}
	};

	usage_string = usage;
	err = copy_argv(*argv, &new_argv);
	if (err) {
		tcom_err(err, "while processing command-line arguments");
		exit(1);
	}

	opterr = 0;
	error[0] = '\0';
	while ((c = getopt_long(*argc, new_argv,
				":hVvqi", long_options, NULL)) != EOF) {
		switch (c) {
			case 'h':
				print_usage = 1;
				break;

			case 'V':
				print_version = 1;
				break;

			case 'v':
				tools_verbose();
				break;

			case 'q':
				tools_quiet();
				break;

			case 'i':
				tools_interactive();
				break;

			case '?':
				snprintf(error, PATH_MAX,
					 "Invalid option: \'-%c\'",
					 optopt);
				print_usage = 1;
				break;

			case ':':
				snprintf(error, PATH_MAX,
					 "Option \'-%c\' requires an argument",
					 optopt);
				print_usage = 1;
				break;

			default:
				snprintf(error, PATH_MAX,
					 "Shouldn't get here %c %c",
					 optopt, c);
				break;
		}

		if (*error)
			break;
	}

	if (*error)
		errorf("%s\n", error);

	if (print_version)
		tools_version();

	if (print_usage)
		tunefs_debug_usage(*error != '\0');

	if (print_usage || print_version)
		exit(0);

	if (*error)
		exit(1);

	shuffle_argv(argc, optind, new_argv);
	*argv = new_argv;
}

static int single_feature_parse_option(struct tunefs_operation *op,
				       char *arg)
{
	int rc = 0;
	struct tunefs_feature *feat = op->to_private;

	if (!arg) {
		errorf("No action specified\n");
		rc = 1;
	} else if (!strcmp(arg, "enable"))
		feat->tf_action = FEATURE_ENABLE;
	else if (!strcmp(arg, "disable"))
		feat->tf_action = FEATURE_DISABLE;
	else {
		errorf("Invalid action: \"%s\"\n", arg);
		rc = 1;
	}

	return rc;
}

static int single_feature_run(struct tunefs_operation *op,
			      ocfs2_filesys *fs, int flags)
{
	errcode_t err;
	struct tunefs_feature *feat = op->to_private;

	err = tunefs_feature_run(fs, feat);
	if (err && (err != TUNEFS_ET_OPERATION_FAILED))
		tcom_err(err, "while toggling feature \"%s\"",
			 feat->tf_name);

	return err;
}

DEFINE_TUNEFS_OP(single_feature,
		 NULL,
		 0,
		 single_feature_parse_option,
		 single_feature_run);

int tunefs_feature_main(int argc, char *argv[], struct tunefs_feature *feat)
{
	char usage[PATH_MAX];

	snprintf(usage, PATH_MAX,
		 "Usage: ocfs2ne_feature_%s [opts] <device> "
		 "{enable|disable}\n",
		 feat->tf_name);
	single_feature_op.to_debug_usage = usage;
	single_feature_op.to_open_flags = feat->tf_open_flags;
	single_feature_op.to_private = feat;

	return tunefs_op_main(argc, argv, &single_feature_op);
}

int tunefs_op_main(int argc, char *argv[], struct tunefs_operation *op)
{
	errcode_t err;
	int rc = 1;
	ocfs2_filesys *fs;
	char *arg = NULL;

	tunefs_init(argv[0]);
	tunefs_parse_core_options(&argc, &argv, op->to_debug_usage);
	if (argc < 2) {
		errorf("No device specified\n");
		tunefs_debug_usage(1);
		goto out;
	}

	if (op->to_parse_option) {
		if (argc > 3) {
			errorf("Too many arguments\n");
			tunefs_debug_usage(1);
			goto out;
		}
		if (argc == 3)
			arg = argv[2];

		rc = op->to_parse_option(op, arg);
		if (rc) {
			tunefs_debug_usage(1);
			goto out;
		}
	} else if (argc > 2) {
		errorf("Too many arguments\n");
		tunefs_debug_usage(1);
		goto out;
	}

	err = tunefs_open(argv[1], op->to_open_flags, &fs);
	if (err && !tunefs_special_errorp(err)) {
		tcom_err(err, "- Unable to open device \"%s\" read-write.",
			 argv[1]);
		goto out;
	}

	err = tunefs_op_run(fs, op);
	if (!err)
		rc = 0;
	else if (err != TUNEFS_ET_OPERATION_FAILED)
		tcom_err(err, "while running operation \"%s\"",
			 op->to_name);

	err = tunefs_close(fs);
	if (err) {
		tcom_err(err, "while closing device \"%s\"", argv[1]);
		rc = 1;
	}

out:
	return rc;
}

#ifdef DEBUG_EXE

int parent = 0;


static void closeup(ocfs2_filesys *fs, const char *device)
{
	errcode_t err;

	verbosef(VL_OUT, "success\n");
	err = tunefs_close(fs);
	if (err)  {
		tcom_err(err, "- Unable to close device \"%s\".", device);
	}
}

int main(int argc, char *argv[])
{
	errcode_t err;
	const char *device;
	ocfs2_filesys *fs;

	tunefs_init(argv[0]);
	tunefs_parse_core_options(&argc, &argv,
				  "Usage: debug_libocfs2ne [-p] <device>\n");

	if (argc > 3) {
		errorf("Too many arguments\n");
		tunefs_debug_usage(1);
		return 1;
	}
	if (argc == 3) {
		if (strcmp(argv[1], "-p")) {
			errorf("Invalid argument: \'%s\'\n", argv[1]);
			tunefs_debug_usage(1);
			return 1;
		}
		parent = 1;
		device = argv[2];
	} else if ((argc == 2) &&
		   strcmp(argv[1], "-p")) {
		device = argv[1];
	} else {
		errorf("Device must be specified\n");
		tunefs_debug_usage(1);
		return 1;
	}

	verbosef(VL_OUT, "Opening device \"%s\" read-only... ", device);
	err = tunefs_open(device, TUNEFS_FLAG_RO, &fs);
	if (err) {
		verbosef(VL_OUT, "failed\n");
		tcom_err(err, "- Unable to open device \"%s\" read-only.",
			 device);
	} else
		closeup(fs, device);

	verbosef(VL_OUT, "Opening device \"%s\" read-write... ", device);
	err = tunefs_open(device, TUNEFS_FLAG_RW, &fs);
	if (err) {
		verbosef(VL_OUT, "failed\n");
		tcom_err(err, "- Unable to open device \"%s\" read-write.",
			 device);
	} else
		closeup(fs, device);

	verbosef(VL_OUT,
		 "Opening device \"%s\" for an online operation... ",
		 device);
	err = tunefs_open(device, TUNEFS_FLAG_RW | TUNEFS_FLAG_ONLINE,
			  &fs);
	if (err == TUNEFS_ET_PERFORM_ONLINE) {
		closeup(fs, device);
		verbosef(VL_OUT, "Operation would have been online\n");
	} else if (!err) {
		closeup(fs, device);
		verbosef(VL_OUT, "Operation would have been offline\n");
	} else {
		verbosef(VL_OUT, "failed\n");
		tcom_err(err, "- Unable to open device \"%s\" read-write.",
			 device);
	}

	verbosef(VL_OUT,
		 "Opening device \"%s\" for a stackless operation... ",
		 device);
	err = tunefs_open(device, TUNEFS_FLAG_RW | TUNEFS_FLAG_NOCLUSTER,
			  &fs);
	if (err == TUNEFS_ET_INVALID_STACK_NAME) {
		closeup(fs, device);
		verbosef(VL_OUT, "Expected cluster stack mismatch found\n");
	} else if (!err) {
		closeup(fs, device);
		verbosef(VL_OUT, "Cluster stacks already match\n");
	} else {
		verbosef(VL_OUT, "failed\n");
		tcom_err(err, "- Unable to open device \"%s\" read-write.",
			 device);
	}

	return 0;
}


#endif /* DEBUG_EXE */

