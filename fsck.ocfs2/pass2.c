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

/* XXX this needs much stronger messages */
static int fix_dirent_dots(o2fsck_state *ost, o2fsck_dirblock_entry *dbe,
			   struct ocfs2_dir_entry *dirent, int offset, 
			   int left)
{
	int expect_dots = 0;
	int ret_flags = 0, changed_len = 0;
	struct ocfs2_dir_entry *next;
	uint16_t new_len;

	if (dbe->e_blkcount == 0) {
		if (offset == 0)
			expect_dots = 1;
		if (offset == OCFS2_DIR_REC_LEN(1))
			expect_dots = 2;
	}

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

	/* 
	 * We don't have the parent inode for .. so we don't check it yet.
	 * It is reasonable for ..'s rec_len to go to the end of the
	 * block for an empty directory, .'s can't because .. is next.
	 */
	if (expect_dots == 2)
		return ret_flags;

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

/* this could certainly be more clever to issue reads in groups */
static unsigned pass2_dir_block_iterate(o2fsck_dirblock_entry *dbe, 
					void *priv_data) 
{
	struct dirblock_data *dd = priv_data;
	struct ocfs2_dir_entry *dirent, *prev = NULL;
	unsigned int offset = 0, this_flags, ret_flags = 0, rc;
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

		if (ret_flags & OCFS2_DIRENT_ABORT)
			break;

		ret_flags |= fix_dirent_dots(dd->ost, dbe, dirent, offset, 
					     dd->fs->fs_blocksize - offset);

		if (ret_flags & OCFS2_DIRENT_ABORT)
			break;

		offset += dirent->rec_len;
		prev = dirent;
	}

	return ret_flags;
}

errcode_t o2fsck_pass2(o2fsck_state *ost)
{
	errcode_t retval;
	struct dirblock_data dd = {
		.ost = ost,
		.fs = ost->ost_fs,
	};

	retval = ocfs2_malloc_block(ost->ost_fs->fs_io, &dd.buf);
	if (retval)
		return retval;

	o2fsck_dir_block_iterate(&ost->ost_dirblocks, pass2_dir_block_iterate, 
			 	 &dd);
	ocfs2_free(&dd.buf);
	return 0;
}
