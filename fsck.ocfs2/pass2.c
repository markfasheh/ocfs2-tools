/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * pass2.c
 *
 * file system checker for OCFS2
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
 * Authors: Zach Brown
 */
#include <string.h>
#include <inttypes.h>
#include <time.h>

#include "ocfs2.h"

#include "dirparents.h"
#include "icount.h"
#include "fsck.h"
#include "pass2.h"
#include "problem.h"
#include "util.h"

struct dirblock_data {
	o2fsck_state *ost;
	ocfs2_filesys *fs;
	char *buf;
};

static int dirent_has_dots(struct ocfs2_dir_entry *dirent, int num_dots)
{
	if (num_dots < 1 || num_dots > 2 || num_dots != dirent->name_len)
		return 0;

	if (num_dots == 2 && dirent->name[1] != '.')
		return 0;

	return dirent->name[0] == '.';
}

static int expected_dots(o2fsck_dirblock_entry *dbe, int offset)
{
	if (dbe->e_blkcount == 0) {
		if (offset == 0)
			return 1;
		if (offset == OCFS2_DIR_REC_LEN(1))
			return 2;
	}

	return 0;
}

/* XXX this needs much stronger messages */
static int fix_dirent_dots(o2fsck_state *ost, o2fsck_dirblock_entry *dbe,
			   struct ocfs2_dir_entry *dirent, int offset, 
			   int left)
{
	int expect_dots = expected_dots(dbe, offset);
	int ret_flags = 0, changed_len = 0;
	struct ocfs2_dir_entry *next;
	uint16_t new_len;

	if (!expect_dots) {
	       	if (!dirent_has_dots(dirent, 1) && !dirent_has_dots(dirent, 2))
			return 0;
		if (should_fix(ost, FIX_DEFYES, 
			       "Duplicate '%*s' directory found, remove?",
			       dirent->name_len, dirent->name)) {
			/* XXX I don't understand the inode = 0 clearing */
			dirent->inode = 0;
			return OCFS2_DIRENT_CHANGED;
		}
	}

	if (!dirent_has_dots(dirent, expect_dots) &&
	    should_fix(ost, FIX_DEFYES, 
		       "didn't find dots when expecting them")) {
		dirent->name_len = expect_dots;
		memset(dirent->name, '.', expect_dots);
		changed_len = 1;
		ret_flags = OCFS2_DIRENT_CHANGED;
	}

	/* we only record where .. points for now and that ends the
	 * checks for .. */
	if (expect_dots == 2) {
		o2fsck_dir_parent *dp;
		dp = o2fsck_dir_parent_lookup(&ost->ost_dir_parents,
						dbe->e_ino);
		if (dp == NULL)
			fatal_error(OCFS2_ET_INTERNAL_FAILURE,
				    "no dir parents for '..' entry for "
				    "inode %"PRIu64, dbe->e_ino);

		dp->dp_dot_dot = dirent->inode;
					
		return ret_flags;
	}

	if ((dirent->inode != dbe->e_ino) &&
            should_fix(ost, FIX_DEFYES, "invalid . directory, replace?")) {
		dirent->inode = dbe->e_ino;
		ret_flags = OCFS2_DIRENT_CHANGED;
	}

	/* 
	 * we might have slop at the end of this "." dirent.  split
	 * it into another seperate dirent if there is enough room and
	 * we've just updated it's name_len or the user says we should.
	 */
	new_len = OCFS2_DIR_REC_LEN(dirent->name_len) - dirent->rec_len;
	if (new_len && (changed_len || 
			should_fix(ost, FIX_DEFNO,
				   "'.' entry is too big, split?"))) {
		dirent->rec_len = OCFS2_DIR_REC_LEN(dirent->name_len);

		next = (struct ocfs2_dir_entry *)((char *)dirent + 
							dirent->rec_len);
		next->inode = 0;
		next->name_len = 0;
		next->rec_len = OCFS2_DIR_REC_LEN(next->rec_len);
		ret_flags = OCFS2_DIRENT_CHANGED;
	}
	return ret_flags;
}

/* we just copy ext2's fixing behaviour here.  'left' is the number of bytes
 * from the start of the dirent struct to the end of the block. */
static int fix_dirent_lengths(o2fsck_state *ost, o2fsck_dirblock_entry *dbe,
				struct ocfs2_dir_entry *dirent, int offset,
				int left, struct ocfs2_dir_entry *prev)
{
	if ((dirent->rec_len >= OCFS2_DIR_REC_LEN(1)) &&
	    ((dirent->rec_len & OCFS2_DIR_ROUND) == 0) &&
	    (dirent->rec_len <= left) &&
	    (OCFS2_DIR_REC_LEN(dirent->name_len) <= dirent->rec_len))
		return 0;

	if (!should_fix(ost, FIX_DEFYES, 
			"Directory inode %"PRIu64" corrupted in logical "
			"block %"PRIu64" physical block %"PRIu64" offset %d",
			dbe->e_ino, dbe->e_blkcount, dbe->e_blkno, offset))
		fatal_error(OCFS2_ET_DIR_CORRUPTED, "in pass2");

	/* special casing an empty dirent that doesn't include the
	 * extra rec_len alignment */
	if ((left >= OCFS2_DIR_MEMBER_LEN) && 
			(dirent->rec_len == OCFS2_DIR_MEMBER_LEN)) {
		char *cp = (char *)dirent;
		left -= dirent->rec_len;
		memmove(cp, cp + dirent->rec_len, left);
		memset(cp + left, 0, dirent->rec_len);
		return OCFS2_DIRENT_CHANGED;
	}

	/* clamp rec_len to the remainder of the block if name_len
	 * is within the block */
	if (dirent->rec_len > left && dirent->name_len <= left) {
		dirent->rec_len = left;
		return OCFS2_DIRENT_CHANGED;
	}

	/* from here on in we're losing directory entries by adding their
	 * space onto previous entries.  If we think we can trust this 
	 * entry's length we leave potential later entries intact.  
	 * otherwise we consume all the space left in the block */
	if (prev && ((dirent->rec_len & OCFS2_DIR_ROUND) == 0) &&
		(dirent->rec_len <= left)) {
		prev->rec_len += left;
	} else {
		dirent->rec_len = left;
		dirent->name_len = 0;
		dirent->inode = 0;
		dirent->file_type = OCFS2_FT_UNKNOWN;
	}

	return OCFS2_DIRENT_CHANGED;
}

static int fix_dirent_name(o2fsck_state *ost, o2fsck_dirblock_entry *dbe,
			   struct ocfs2_dir_entry *dirent, int offset)
{
	char *chr = dirent->name;
	int len = dirent->name_len, fix = 0, ret_flags = 0;

	for(; len-- && (*chr == '/' || *chr == '\0'); chr++) {
		/* XXX in %s parent name */
		if (!fix) {
			fix = should_fix(ost, FIX_DEFYES, "Entry '%*s' "
					"contains invalid characters, replace "
					"with dots?", dirent->name_len, 
					dirent->name);
			if (!fix)
				return 0;
		}
		*chr = '.';
		ret_flags = OCFS2_DIRENT_CHANGED;
	}

	return ret_flags;
}

static int fix_dirent_linkage(o2fsck_state *ost, o2fsck_dirblock_entry *dbe,
			      struct ocfs2_dir_entry *dirent, int offset)
{
	int expect_dots = expected_dots(dbe, offset);
	o2fsck_dir_parent *dp;
	errcode_t err;
	int is_dir;

	/* we already took care of special-casing the dots */
	if (expect_dots)
		return 0;

	/* we're only checking the linkage if we already found the dir 
	 * this inode claims to be pointing to */
	err = ocfs2_bitmap_test(ost->ost_dir_inodes, dirent->inode, &is_dir);
	if (err)
		fatal_error(err, "while checking for inode %"PRIu64" in the "
				"dir bitmap", dirent->inode);
	if (!is_dir)
		return 0;

	dp = o2fsck_dir_parent_lookup(&ost->ost_dir_parents, dirent->inode);
	if (dp == NULL)
		fatal_error(OCFS2_ET_INTERNAL_FAILURE, "no dir parents for "
				"'..' entry for inode %"PRIu64, dbe->e_ino);

	/* if no dirents have pointed to this inode yet we record ours
	 * as the first and move on */
	if (dp->dp_dirent == 0) {
		dp->dp_dirent = dbe->e_ino;
		return 0;
	}

	if (should_fix(ost, 0, "directory inode %"PRIu64" is not the first to "
		"claim to be the parent of subdir '%*s' (%"PRIu64").  Forget "
		"this linkage and leave the previous parent of '%*s' intact?",
		dbe->e_ino, dirent->name_len, dirent->name, dirent->inode,
		dirent->name_len, dirent->name)) {

		dirent->inode = 0;
		return OCFS2_DIRENT_CHANGED;
	}

	return 0;
}

/* this could certainly be more clever to issue reads in groups */
static unsigned pass2_dir_block_iterate(o2fsck_dirblock_entry *dbe, 
					void *priv_data) 
{
	struct dirblock_data *dd = priv_data;
	struct ocfs2_dir_entry *dirent, *prev = NULL;
	unsigned int offset = 0, this_flags, ret_flags = 0;
	errcode_t retval;

	/* XXX there is no byte swapping story here, which is wrong.  we might
	 * be able to salvage more than read_dir_block() if we did our own
	 * swabing, so maybe that's what's needed. */
 	retval = io_read_block(dd->fs->fs_io, dbe->e_blkno, 1, dd->buf);
	if (retval)
		return OCFS2_DIRENT_ABORT;

	printf("found %"PRIu64" %"PRIu64" %"PRIu64"\n", dbe->e_ino, 
			dbe->e_blkno, dbe->e_blkcount);

	while (offset < dd->fs->fs_blocksize) {
		dirent = (struct ocfs2_dir_entry *)(dd->buf + offset);

		/* I wonder if we should be checking that the padding
		 * is 0 */

		printf("dir entry %u %*s\n", dirent->rec_len, dirent->name_len,
						dirent->name);

		this_flags = fix_dirent_lengths(dd->ost, dbe, dirent, offset, 
						 dd->fs->fs_blocksize - offset,
						 prev);

		ret_flags |= this_flags;

		/* dirtying might have swapped in a new dirent in place
		 * of the one we just checked. */
		if (this_flags & OCFS2_DIRENT_CHANGED)
			continue;

		ret_flags |= fix_dirent_dots(dd->ost, dbe, dirent, offset, 
					     dd->fs->fs_blocksize - offset);

		ret_flags |= fix_dirent_name(dd->ost, dbe, dirent, offset);

		ret_flags |= fix_dirent_linkage(dd->ost, dbe, dirent, offset);

		offset += dirent->rec_len;
		prev = dirent;
	}

	return ret_flags;
}

errcode_t o2fsck_pass2(o2fsck_state *ost)
{
	errcode_t retval;
	o2fsck_dir_parent *dp;
	struct dirblock_data dd = {
		.ost = ost,
		.fs = ost->ost_fs,
	};

	retval = ocfs2_malloc_block(ost->ost_fs->fs_io, &dd.buf);
	if (retval)
		return retval;

	/* 
	 * Mark the root directory's dirent parent as itself if we found the
	 * inode during inode scanning.  The dir will be created in pass3
	 * if it didn't exist already.  XXX we should do this for our other
	 * magical directories.
	 */
	dp = o2fsck_dir_parent_lookup(&ost->ost_dir_parents, 
					ost->ost_fs->fs_root_blkno);
	if (dp)
		dp->dp_dirent = ost->ost_fs->fs_root_blkno;

	o2fsck_dir_block_iterate(&ost->ost_dirblocks, pass2_dir_block_iterate, 
			 	 &dd);
	ocfs2_free(&dd.buf);
	return 0;
}
