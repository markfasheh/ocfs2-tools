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
#include "strings.h"
#include "util.h"

static const char *whoami = "pass2";

int o2fsck_test_inode_allocated(o2fsck_state *ost, uint64_t blkno)
{
	errcode_t ret;
	int was_set;

	ret = ocfs2_test_inode_allocated(ost->ost_fs, blkno, &was_set);
	/* XXX this should stop fsck from marking the fs clean */
	if (ret) {
		com_err(whoami, ret, "while testing if inode %"PRIu64" is "
			"allocated.  Continuing as though it is.", blkno);
		was_set = 1;
	}
	return was_set;
}

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
		if (prompt(ost, PY, "Duplicate '%.*s' directory entry found, "
			   "remove it?", dirent->name_len, dirent->name)) {
			dirent->inode = 0;
			return OCFS2_DIRENT_CHANGED;
		}
	}

	if (!dirent_has_dots(dirent, expect_dots) &&
	    prompt(ost, PY, "The %s directory entry in directory inode "
		   "%"PRIu64" is '%.*s' instead of '%.*s'.  Clobber the "
		   "current name with the expected dot name?", 
		   expect_dots == 1 ? "first" : "second", dbe->e_ino, 
		   dirent->name_len, dirent->name, expect_dots, "..")) {

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
            prompt(ost, PY, "The '.' entry in directory inode %"PRIu64" "
		   "points to inode %"PRIu64" instead of itself.  Fix "
		   "the '.' entry?", dbe->e_ino, dirent->inode)) {
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
			prompt(ost, PY, "The '.' entry in directory inode "
			       "%"PRIu64" is too long.  Try to create another "
			       "directory entry from the excess?", 
			       dbe->e_ino))) {
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

	if (!prompt(ost, PY, "Directory inode %"PRIu64" corrupted in logical "
		    "block %"PRIu64" physical block %"PRIu64" offset %d. "
		    "Attempt to repair this block's directory entries?",
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

	if (len == 0) {
		if (prompt(ost, PY, "Directory entry has a zero-length name, "
				    "clear it?")) {
			dirent->inode = 0;
			ret_flags = OCFS2_DIRENT_CHANGED;
		}
	}

	for(; len-- > 0 && (*chr == '/' || *chr == '\0'); chr++) {
		/* XXX in %s parent name */
		if (!fix) {
			fix = prompt(ost, PY, "Directory entry '%.*s' "
				     "contains invalid characters, replace "
				     "them with dots?", dirent->name_len, 
				     dirent->name);
			if (!fix)
				return 0;
		}
		*chr = '.';
		ret_flags = OCFS2_DIRENT_CHANGED;
	}

	return ret_flags;
}

static int fix_dirent_inode(o2fsck_state *ost, o2fsck_dirblock_entry *dbe,
				struct ocfs2_dir_entry *dirent, int offset)
{
	if (ocfs2_block_out_of_range(ost->ost_fs, dirent->inode)) {
		if (prompt(ost, PY, "Directory entry '%.*s' refers to inode "
			   "number %"PRIu64" which is out of range, "
			   "clear the entry?", dirent->name_len, dirent->name, 
			   dirent->inode)) {
			dirent->inode = 0;
			return OCFS2_DIRENT_CHANGED;
		}
	}

	if (!o2fsck_test_inode_allocated(ost, dbe->e_ino) &&
	    prompt(ost, PY, "Directory entry '%.*s' refers to inode number "
		   "%"PRIu64" which isn't allocated, clear the entry?", 
		   dirent->name_len, dirent->name, dirent->inode)) {
		dirent->inode = 0;
		return OCFS2_DIRENT_CHANGED;
	}
	return 0;
}

#define type_entry(type) [type] = #type
static char *file_types[] = {
	type_entry(OCFS2_FT_UNKNOWN),
	type_entry(OCFS2_FT_REG_FILE),
	type_entry(OCFS2_FT_DIR),
	type_entry(OCFS2_FT_CHRDEV),
	type_entry(OCFS2_FT_BLKDEV),
	type_entry(OCFS2_FT_FIFO),
	type_entry(OCFS2_FT_SOCK),
	type_entry(OCFS2_FT_SYMLINK),
};
#undef type_entry

static char *file_type_string(uint8_t type)
{
	if (type >= OCFS2_FT_MAX)
		return "(unknown)";

	return file_types[type];
}

static int fix_dirent_filetype(o2fsck_state *ost, o2fsck_dirblock_entry *dbe,
				struct ocfs2_dir_entry *dirent, int offset)
{
	char *buf;
	ocfs2_dinode *dinode;
	uint8_t expected_type;
	errcode_t err;
	int was_set;

	/* XXX Do I care about possible bitmap_test errors here? */

	ocfs2_bitmap_test(ost->ost_dir_inodes, dirent->inode, &was_set);
	if (was_set) {
		expected_type = OCFS2_FT_DIR;
		goto check;
	}

	ocfs2_bitmap_test(ost->ost_reg_inodes, dirent->inode, &was_set);
	if (was_set) {
		expected_type = OCFS2_FT_REG_FILE;
		goto check;
	}

	ocfs2_bitmap_test(ost->ost_bad_inodes, dirent->inode, &was_set);
	if (was_set) {
		expected_type = OCFS2_FT_UNKNOWN;
		goto check;
	}

	err = ocfs2_malloc_block(ost->ost_fs->fs_io, &buf);
	if (err)
		fatal_error(err, "while allocating inode buffer to verify an "
				"inode's file type");

	err = ocfs2_read_inode(ost->ost_fs, dirent->inode, buf);
	if (err)
		fatal_error(err, "reading inode %"PRIu64" when verifying "
			"an entry's file type", dirent->inode);

	dinode = (ocfs2_dinode *)buf; 
	expected_type = ocfs_type_by_mode[(dinode->i_mode & S_IFMT)>>S_SHIFT];
	ocfs2_free(&buf);

check:
	if ((dirent->file_type != expected_type) &&
	    prompt(ost, PY, "Directory entry %.*s contains file type %s (%u) "
		"but its inode %"PRIu64" leads to type %s (%u).  Reset the "
		"entry's type to match the inode's?",
		dirent->name_len, dirent->name, 
		file_type_string(dirent->file_type), dirent->file_type,
		dirent->inode,
		file_type_string(expected_type), expected_type)) {

		dirent->file_type = expected_type;
		return OCFS2_DIRENT_CHANGED;
	}


	return 0;
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

	if (prompt(ost, 0, "Directory inode %"PRIu64" is not the first to "
		"claim to be the parent of subdir '%.*s' (inode %"PRIu64"). "
		"Clear this directory entry and leave the previous parent of "
		"the subdir's inode intact?", dbe->e_ino, 
		dirent->name_len, dirent->name, dirent->inode)) {

		dirent->inode = 0;
		return OCFS2_DIRENT_CHANGED;
	}

	return 0;
}

static int fix_dirent_dups(o2fsck_state *ost, o2fsck_dirblock_entry *dbe,
			   struct ocfs2_dir_entry *dirent, 
			   o2fsck_strings *strings, int *dups_in_block)
{
	errcode_t err;
	int was_set;

	if (*dups_in_block)
		return 0;

	/* does this need to be fatal?  It appears e2fsck just ignores
	 * the error. */
	err = o2fsck_strings_insert(strings, dirent->name, dirent->name_len, 
				   &was_set);
	if (err)
		fatal_error(err, "while allocating space to find duplicate "
				"directory entries");

	if (!was_set)
		return 0;

	fprintf(stderr, "Duplicate directory entry '%.*s' found.\n",
		      dirent->name_len, dirent->name);
	fprintf(stderr, "Marking its parent %"PRIu64" for rebuilding.\n",
			dbe->e_ino);

	err = ocfs2_bitmap_test(ost->ost_rebuild_dirs, dbe->e_ino, &was_set);
	if (err)
		fatal_error(err, "while checking for inode %"PRIu64" in "
				"the used bitmap", dbe->e_ino);

	*dups_in_block = 1;
	return 0;
}

/* this could certainly be more clever to issue reads in groups */
static unsigned pass2_dir_block_iterate(o2fsck_dirblock_entry *dbe, 
					void *priv_data) 
{
	struct dirblock_data *dd = priv_data;
	struct ocfs2_dir_entry *dirent, *prev = NULL;
	unsigned int offset = 0, this_flags, ret_flags = 0;
	o2fsck_strings strings;
	int dups_in_block = 0;
	errcode_t retval;

	if (!o2fsck_test_inode_allocated(dd->ost, dbe->e_ino)) {
		printf("Directory block %"PRIu64" belongs to directory inode "
		       "%"PRIu64" which isn't allocated.  Ignoring this "
		       "block.", dbe->e_blkno, dbe->e_ino);
		return 0;
	}

	o2fsck_strings_init(&strings);

 	retval = ocfs2_read_dir_block(dd->fs, dbe->e_blkno, dd->buf);
	if (retval && retval != OCFS2_ET_DIR_CORRUPTED) {
		/* XXX hum, ask to continue here.  more a prompt than a 
		 * fix.  need to expand problem.c's vocabulary. */
		fatal_error(retval, "while reading dir block %"PRIu64,
				dbe->e_blkno);
	}

	verbosef("dir block %"PRIu64"\n", dbe->e_blkno);

	while (offset < dd->fs->fs_blocksize) {
		dirent = (struct ocfs2_dir_entry *)(dd->buf + offset);

		verbosef("checking dirent offset %d, ino %"PRIu64" rec_len "
			"%"PRIu16" name_len %"PRIu8" file_type %"PRIu8"\n",
			offset, dirent->inode, dirent->rec_len, 
			dirent->name_len, dirent->file_type);

		/* XXX I wonder if we should be checking that the padding
		 * is 0 */


		/* first verify that we can trust the dirent's lengths
		 * to navigate to the next in the block.  This can try to
		 * get rid of a broken dirent by trying to shift remaining
		 * dirents into its place.  The 'contiune' attempts to let
		 * us recheck the current dirent.
		 *
		 * XXX this will have to do some swabbing as it tries
		 * to salvage as read_dir_block stops swabbing
		 * when it sees bad entries.
		 */
		this_flags = fix_dirent_lengths(dd->ost, dbe, dirent, offset, 
						 dd->fs->fs_blocksize - offset,
						 prev);
		ret_flags |= this_flags;
		if (this_flags & OCFS2_DIRENT_CHANGED)
			continue;

		/* 
		 * In general, these calls mark ->inode as 0 when they want it
		 * to be seen as deleted; ignored by fsck and reclaimed by the
		 * kernel.  The dots are a special case, of course.  This
		 * pass makes sure that they are the first two entries in
		 * the directory and pass3 fixes ".."'s ->inode.
		 *
		 * XXX should verify that ocfs2 reclaims entries like that.
		 */
		ret_flags |= fix_dirent_dots(dd->ost, dbe, dirent, offset, 
					     dd->fs->fs_blocksize - offset);
		if (dirent->inode == 0)
			goto next;

		ret_flags |= fix_dirent_name(dd->ost, dbe, dirent, offset);
		if (dirent->inode == 0)
			goto next;

		ret_flags |= fix_dirent_inode(dd->ost, dbe, dirent, offset);
		if (dirent->inode == 0)
			goto next;

		ret_flags |= fix_dirent_filetype(dd->ost, dbe, dirent, offset);
		if (dirent->inode == 0)
			goto next;

		ret_flags |= fix_dirent_linkage(dd->ost, dbe, dirent, offset);
		if (dirent->inode == 0)
			goto next;

		ret_flags |= fix_dirent_dups(dd->ost, dbe, dirent, &strings,
					     &dups_in_block);
		if (dirent->inode == 0)
			goto next;

		verbosef("dirent %.*s refs ino %"PRIu64"\n", dirent->name_len,
				dirent->name, dirent->inode);
		o2fsck_icount_delta(dd->ost->ost_icount_refs, dirent->inode, 1);
next:
		offset += dirent->rec_len;
		prev = dirent;
	}

	if (ret_flags & OCFS2_DIRENT_CHANGED) {
		retval = ocfs2_write_dir_block(dd->fs, dbe->e_blkno, dd->buf);
		if (retval) {
			/* XXX hum, ask to continue here.  more a prompt than a 
			 * fix.  need to expand problem.c's vocabulary. */
			fatal_error(retval, "while writing dir block %"PRIu64,
					dbe->e_blkno);
		}
	}

	o2fsck_strings_free(&strings);
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

	printf("Pass 2: Checking directory entries.\n");

	retval = ocfs2_malloc_block(ost->ost_fs->fs_io, &dd.buf);
	if (retval)
		return retval;

	/* 
	 * Mark the root directory's dirent parent as itself if we found the
	 * inode during inode scanning.  The dir will be created in pass3
	 * if it didn't exist already.  XXX we should do this for all our other
	 * magical directories.
	 */
	dp = o2fsck_dir_parent_lookup(&ost->ost_dir_parents, 
					ost->ost_fs->fs_root_blkno);
	if (dp)
		dp->dp_dirent = ost->ost_fs->fs_root_blkno;

	dp = o2fsck_dir_parent_lookup(&ost->ost_dir_parents, 
					ost->ost_fs->fs_sysdir_blkno);
	if (dp)
		dp->dp_dirent = ost->ost_fs->fs_sysdir_blkno;

	o2fsck_dir_block_iterate(&ost->ost_dirblocks, pass2_dir_block_iterate, 
			 	 &dd);
	ocfs2_free(&dd.buf);
	return 0;
}
