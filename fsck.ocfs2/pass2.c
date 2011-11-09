/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * Copyright (C) 1993-2004 by Theodore Ts'o.
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
 * Pass 2 iterates through the directory blocks that pass 1 found under 
 * directory inodes.  The basic dirent structures are made consistent
 * in each block.  Directory entries much point to active inodes.  "dot dot"
 * must be in the first blocks of the dir and nowhere else.  Duplicate entries
 * are detected but little more.  Slashes and nulls in names are replaced
 * with dots.  The file type in the entry is synced with the type found in
 * the inode it points to.  Throughout this invalid entries are cleared 
 * by simply setting their inode field to 0 so that the fs will reuse them.
 *
 * Pass 2 builds up the parent dir linkage as it scans the directory entries
 * so that pass 3 can walk the directory trees to find disconnected inodes.
 */
#include <string.h>
#include <inttypes.h>
#include <time.h>

#include "ocfs2/ocfs2.h"
#include "ocfs2/kernel-rbtree.h"

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
	o2fsck_state 	*ost;
	ocfs2_filesys 	*fs;
	char 		*dirblock_buf;
	char 		*inoblock_buf;
	errcode_t	ret;
	o2fsck_strings	strings;
	uint64_t	last_ino;
	struct rb_root	re_idx_dirs;
};

static int dirent_has_dots(struct ocfs2_dir_entry *dirent, int num_dots)
{
	if (num_dots < 1 || num_dots > 2 || num_dots != dirent->name_len)
		return 0;

	if (num_dots == 2 && dirent->name[1] != '.')
		return 0;

	return dirent->name[0] == '.';
}

static int expected_dots(o2fsck_state *ost,
			 o2fsck_dirblock_entry *dbe,
			 int offset)
{
	int inline_off = offsetof(struct ocfs2_dinode, id2.i_data.id_data);

	if (dbe->e_blkcount == 0) {
		if (offset == 0 ||
		    (dbe->e_ino == dbe->e_blkno && offset == inline_off))
			return 1;
		if (offset == OCFS2_DIR_REC_LEN(1) ||
		    (dbe->e_ino == dbe->e_blkno &&
		     offset == inline_off + OCFS2_DIR_REC_LEN(1)))
			return 2;
	}

	return 0;
}

static errcode_t fix_dirent_dots(o2fsck_state *ost, o2fsck_dirblock_entry *dbe,
				 struct ocfs2_dir_entry *dirent, int offset, 
				 int left, unsigned int *flags)
{
	int expect_dots = expected_dots(ost, dbe, offset);
	int changed_len = 0;
	struct ocfs2_dir_entry *next;
	uint16_t new_len;
	errcode_t ret = 0;

	if (!expect_dots) {
		if (!dirent->inode ||
		    (!dirent_has_dots(dirent, 1) && !dirent_has_dots(dirent, 2)))
			goto out;

		if (prompt(ost, PY, PR_DIRENT_DOTTY_DUP,
			   "Duplicate '%.*s' directory entry found, remove "
			   "it?", dirent->name_len, dirent->name)) {

			dirent->inode = 0;
			*flags |= OCFS2_DIRENT_CHANGED;
			goto out;
		}
	}

	if (!dirent_has_dots(dirent, expect_dots) &&
	    prompt(ost, PY, PR_DIRENT_NOT_DOTTY,
		   "The %s directory entry in directory inode "
		   "%"PRIu64" is '%.*s' instead of '%.*s'.  Clobber the "
		   "current name with the expected dot name?", 
		   expect_dots == 1 ? "first" : "second", dbe->e_ino, 
		   dirent->name_len, dirent->name, expect_dots, "..")) {

		dirent->name_len = expect_dots;
		memset(dirent->name, '.', expect_dots);
		dirent->file_type = OCFS2_FT_DIR;
		changed_len = 1;
		*flags |= OCFS2_DIRENT_CHANGED;
	}

	/* we only record where .. points for now and that ends the
	 * checks for .. */
	if (expect_dots == 2) {
		o2fsck_dir_parent *dp;
		dp = o2fsck_dir_parent_lookup(&ost->ost_dir_parents,
						dbe->e_ino);
		if (dp == NULL) {
			ret = OCFS2_ET_INTERNAL_FAILURE;
			com_err(whoami, ret, "no dir parents for '..' entry "
				"for inode %"PRIu64, dbe->e_ino);
		} else 
			dp->dp_dot_dot = dirent->inode;

		goto out;
	}

	if ((dirent->inode != dbe->e_ino) &&
            prompt(ost, PY, PR_DIRENT_DOT_INODE,
		   "The '.' entry in directory inode %"PRIu64" "
		   "points to inode %"PRIu64" instead of itself.  Fix "
		   "the '.' entry?", dbe->e_ino, (uint64_t)dirent->inode)) {
		dirent->inode = dbe->e_ino;
		*flags |= OCFS2_DIRENT_CHANGED;
	}

	/* 
	 * we might have slop at the end of this "." dirent.  split
	 * it into another seperate dirent if there is enough room and
	 * we've just updated it's name_len or the user says we should.
	 */
	new_len = OCFS2_DIR_REC_LEN(dirent->name_len) - dirent->rec_len;
	if (new_len && (changed_len || 
			prompt(ost, PY, PR_DIRENT_DOT_EXCESS,
			       "The '.' entry in directory inode "
			       "%"PRIu64" is too long.  Try to create another "
			       "directory entry from the excess?", 
			       dbe->e_ino))) {
		dirent->rec_len = OCFS2_DIR_REC_LEN(dirent->name_len);

		next = (struct ocfs2_dir_entry *)((char *)dirent + 
							dirent->rec_len);
		next->inode = 0;
		next->name_len = 0;
		next->rec_len = OCFS2_DIR_REC_LEN(next->rec_len);
		*flags |= OCFS2_DIRENT_CHANGED;
	}

out:
	return ret;
}

/*
 * The directory trailer has compatibility fields so it can be treated
 * as an empty (deleted) dirent.  We need to make sure those are correct.
 */
static void fix_dir_trailer(o2fsck_state *ost, o2fsck_dirblock_entry *dbe,
			    struct ocfs2_dir_block_trailer *trailer,
			    unsigned int *flags)
{
	if (trailer->db_compat_inode &&
	    prompt(ost, PY, PR_DIR_TRAILER_INODE,
		   "Directory block trailer for logical block %"PRIu64" "
		   "physcal block %"PRIu64" in directory inode %"PRIu64" "
		   "has a non-zero inode number.  Clear it?",
		   dbe->e_blkcount, dbe->e_blkno, dbe->e_ino)) {
		trailer->db_compat_inode = 0;
		*flags |= OCFS2_DIRENT_CHANGED;
	}

	if (trailer->db_compat_name_len &&
	    prompt(ost, PY, PR_DIR_TRAILER_NAME_LEN,
		   "Directory block trailer for logical block %"PRIu64" "
		   "physcal block %"PRIu64" in directory inode %"PRIu64" "
		   "has a non-zero name_len.  Clear it?",
		   dbe->e_blkcount, dbe->e_blkno, dbe->e_ino)) {
		trailer->db_compat_name_len = 0;
		*flags |= OCFS2_DIRENT_CHANGED;
	}

	if ((trailer->db_compat_rec_len !=
	     sizeof(struct ocfs2_dir_block_trailer)) &&
	    prompt(ost, PY, PR_DIR_TRAILER_REC_LEN,
		   "Directory block trailer for logical block %"PRIu64" "
		   "physcal block %"PRIu64" in directory inode %"PRIu64" "
		   "has an invalid rec_len.  Fix it?",
		   dbe->e_blkcount, dbe->e_blkno, dbe->e_ino)) {
		trailer->db_compat_rec_len =
			sizeof(struct ocfs2_dir_block_trailer);
		*flags |= OCFS2_DIRENT_CHANGED;
	}

	if ((trailer->db_blkno != dbe->e_blkno) &&
	    prompt(ost, PY, PR_DIR_TRAILER_BLKNO,
		   "Directory block trailer for logical block %"PRIu64" "
		   "physcal block %"PRIu64" in directory inode %"PRIu64" "
		   "has an invalid db_blkno of %"PRIu64".  Fix it?",
		   dbe->e_blkcount, dbe->e_blkno, dbe->e_ino,
		   trailer->db_blkno)) {
		trailer->db_blkno = dbe->e_blkno;
		*flags |= OCFS2_DIRENT_CHANGED;
	}

	if ((trailer->db_parent_dinode != dbe->e_ino) &&
	    prompt(ost, PY, PR_DIR_TRAILER_PARENT_INODE,
		   "Directory block trailer for logical block %"PRIu64" "
		   "physcal block %"PRIu64" in directory inode %"PRIu64" "
		   "claims it belongs to inoe %"PRIu64".  Fix it?",
		   dbe->e_blkcount, dbe->e_blkno, dbe->e_ino,
		   trailer->db_parent_dinode)) {
		trailer->db_parent_dinode = dbe->e_ino;
		*flags |= OCFS2_DIRENT_CHANGED;
	}
}

static int dirent_leaves_partial(struct ocfs2_dir_entry *dirent, int left)
{
	left -= dirent->rec_len;
	return (left > 0 && left < OCFS2_DIR_MEMBER_LEN);
}

/*
 * The caller has found that either of rec_len or name_len are garbage.  The
 * caller trusts us to fix them up in place and will be checking them again
 * before proceeding.  We have to update the lengths to make forward progress.
 * 'left' is the number of bytes from the start of this dirent struct that
 * remain in the block.  
 *
 * We're called for invalid dirents, and having a dirent
 * that leaves a partial dirent at the end of the block is considered invalid,
 * and we pad out partials at the end of this call so we can't be called here
 * with left < OCFS2_DIR_MEMBER_LEN.
 *
 * we're pretty limited in the repairs we can make:
 *
 * - We can't just set name_len if rec_len looks valid, we might guess 
 *   name_len wrong and create a bogus file name.
 * - we can't just set rec_len based on name_len.  rec_len could have
 *   included an arbitrary part of the name from a previously freed dirent.
 */
static void fix_dirent_lengths(struct ocfs2_dir_entry *dirent,
			       int left, struct ocfs2_dir_entry *prev,
			       unsigned int *flags)
{
	/* 
	 * as described above we can't reconstruct either value if it is
	 * complete nonsense.  We can only proceed if we can work off of
	 * one that is kind of valid looking.
	 * name_len could well be 0 from the dirent being cleared.
	 */
	if (dirent->rec_len < OCFS2_DIR_MEMBER_LEN ||
	   (dirent->rec_len > left ||
	    dirent->name_len > left))
	    goto wipe;

	/* if we see a dirent with no file name then we remove it by
	 * shifting the remaining dirents forward */
	if ((dirent->rec_len == OCFS2_DIR_MEMBER_LEN)) {
		char *cp = (char *)dirent;
		left -= dirent->rec_len;
		memmove(cp, cp + dirent->rec_len, left);
		memset(cp + left, 0, dirent->rec_len);
		goto out;
	}

	/* if rec_len just appears to be mis-rounded in a way that doesn't
	 * affect following dirents then we can probably save this dirent */
	if (OCFS2_DIR_REC_LEN(dirent->name_len) != dirent->rec_len &&
	    OCFS2_DIR_REC_LEN(dirent->name_len) == 
					OCFS2_DIR_REC_LEN(dirent->rec_len)) {
		dirent->rec_len = OCFS2_DIR_REC_LEN(dirent->name_len);
		left -= dirent->rec_len;
		goto out;
	}

	/* if name_len is too far off, however, we're going to lose this
	 * dirent.. we might be able to just lose this one dirent if rec_len
	 * appears to be intact. */
	if ((dirent->rec_len & OCFS2_DIR_ROUND) == 0 &&
	    !dirent_leaves_partial(dirent, left)) {
		left -= dirent->rec_len;
		dirent->name_len = 0;
		dirent->inode = 0;
		dirent->file_type = OCFS2_FT_UNKNOWN;
		goto out;
	}

	/* 
	 * if we can't trust rec_len, however, then we don't know where the
	 * next dirent might begin.  We've lost the trail of dirents created by
	 * the file system and run the risk of parsing file names as dirents.
	 * So we're forced to wipe the block and leave the rest to lost+found.
	 */
wipe:
	dirent->rec_len = left;
	dirent->name_len = 0;
	dirent->inode = 0;
	dirent->file_type = OCFS2_FT_UNKNOWN;
	left = 0;
out:

	/* 
	 * rec_len must be valid and left must reflect the space *after* the
	 * current dirent by this point.  if there isn't enough room for
	 * another dirent after the one we've just repaired then we tack the
	 * remaining space onto the current dirent.
	 */
	if (dirent_leaves_partial(dirent, left))
		dirent->rec_len += left;

	*flags |= OCFS2_DIRENT_CHANGED;
}

static void fix_dirent_name(o2fsck_state *ost, o2fsck_dirblock_entry *dbe,
			    struct ocfs2_dir_entry *dirent, int offset,
			    unsigned int *flags)
{
	char *chr = dirent->name;
	int len = dirent->name_len, fix = 0;

	if (len == 0) {
		if (prompt(ost, PY, PR_DIRENT_ZERO,
			   "Directory entry has a zero-length name, "
				    "clear it?")) {
			dirent->inode = 0;
			*flags |= OCFS2_DIRENT_CHANGED;
		}
	}

	for(; len-- > 0 && (*chr == '/' || *chr == '\0'); chr++) {
		/* XXX in %s parent name */
		if (!fix) {
			fix = prompt(ost, PY, PR_DIRENT_NAME_CHARS,
				     "Directory entry '%.*s' "
				     "contains invalid characters, replace "
				     "them with dots?", dirent->name_len, 
				     dirent->name);
			if (!fix)
				break;
		}
		*chr = '.';
		*flags |= OCFS2_DIRENT_CHANGED;
	}
}

static void fix_dirent_inode(o2fsck_state *ost, o2fsck_dirblock_entry *dbe,
			     struct ocfs2_dir_entry *dirent, int offset,
			     unsigned int *flags)
{
	if (ocfs2_block_out_of_range(ost->ost_fs, dirent->inode) &&
	    prompt(ost, PY, PR_DIRENT_INODE_RANGE,
		   "Directory entry '%.*s' refers to inode "
		   "number %"PRIu64" which is out of range, clear the entry?",
		   dirent->name_len, dirent->name, (uint64_t)dirent->inode)) {

		dirent->inode = 0;
		*flags |= OCFS2_DIRENT_CHANGED;
		goto out;
	}

	if (!o2fsck_test_inode_allocated(ost, dirent->inode) &&
	    prompt(ost, PY, PR_DIRENT_INODE_FREE,
		   "Directory entry '%.*s' refers to inode number "
		   "%"PRIu64" which isn't allocated, clear the entry?", 
		   dirent->name_len, dirent->name, (uint64_t)dirent->inode)) {
		dirent->inode = 0;
		*flags |= OCFS2_DIRENT_CHANGED;
	}
out:
	return;
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

static errcode_t fix_dirent_filetype(o2fsck_state *ost,
				     o2fsck_dirblock_entry *dbe,
				     struct ocfs2_dir_entry *dirent,
				     int offset,
				     unsigned int *flags)
{
	uint8_t expected_type;
	errcode_t ret;
	int was_set;

	ret = ocfs2_bitmap_test(ost->ost_dir_inodes, dirent->inode, &was_set);
	if (ret)
		goto out;
	if (was_set) {
		expected_type = OCFS2_FT_DIR;
		goto check;
	}

	ret = ocfs2_bitmap_test(ost->ost_reg_inodes, dirent->inode, &was_set);
	if (ret)
		goto out;
	if (was_set) {
		expected_type = OCFS2_FT_REG_FILE;
		goto check;
	}

	ret = o2fsck_type_from_dinode(ost, dirent->inode, &expected_type);
	if (ret)
		goto out;

check:
	if ((dirent->file_type != expected_type) &&
	    prompt(ost, PY, PR_DIRENT_TYPE,
		   "Directory entry %.*s contains file type %s (%u) "
		"but its inode %"PRIu64" leads to type %s (%u).  Reset the "
		"entry's type to match the inode's?",
		dirent->name_len, dirent->name, 
		file_type_string(dirent->file_type), dirent->file_type,
		(uint64_t)dirent->inode,
		file_type_string(expected_type), expected_type)) {

		dirent->file_type = expected_type;
		*flags |= OCFS2_DIRENT_CHANGED;
	}

out:
	if (ret)
		com_err(whoami, ret, "while trying to verify the file type "
			"of directory entry %.*s", dirent->name_len,
			dirent->name);

	return ret;
}

static errcode_t fix_dirent_linkage(o2fsck_state *ost,
				    o2fsck_dirblock_entry *dbe,
				    struct ocfs2_dir_entry *dirent,
				    int offset,
				    unsigned int *flags)
{
	int expect_dots = expected_dots(ost, dbe, offset);
	o2fsck_dir_parent *dp;
	errcode_t ret = 0;
	int is_dir;

	/* we already took care of special-casing the dots */
	if (expect_dots)
		goto out;

	/* we're only checking the linkage if we already found the dir 
	 * this inode claims to be pointing to */
	ret = ocfs2_bitmap_test(ost->ost_dir_inodes, dirent->inode, &is_dir);
	if (ret)
		com_err(whoami, ret, "while checking for inode %"PRIu64" in "
			"the dir bitmap", (uint64_t)dirent->inode);
	if (!is_dir)
		goto out;

	dp = o2fsck_dir_parent_lookup(&ost->ost_dir_parents, dirent->inode);
	if (dp == NULL) {
		ret = OCFS2_ET_INTERNAL_FAILURE;
		com_err(whoami, ret, "no dir parents recorded for inode "
			"%"PRIu64, (uint64_t)dirent->inode);
		goto out;
	}

	/* if no dirents have pointed to this inode yet we record ours
	 * as the first and move on */
	if (dp->dp_dirent == 0) {
		dp->dp_dirent = dbe->e_ino;
		goto out;
	}

	if (prompt(ost, 0, PR_DIR_PARENT_DUP,
		   "Directory inode %"PRIu64" is not the first to "
		"claim to be the parent of subdir '%.*s' (inode %"PRIu64"). "
		"Clear this directory entry and leave the previous parent of "
		"the subdir's inode intact?", dbe->e_ino, 
		dirent->name_len, dirent->name, (uint64_t)dirent->inode)) {

		dirent->inode = 0;
		*flags |= OCFS2_DIRENT_CHANGED;
	}

out:
	return ret;
}

/* detecting dups is irritating because of the storage requirements of
 * detecting duplicates.  e2fsck avoids the storage burden for a regular fsck
 * pass by only detecting duplicate entries that occur in the same directory
 * block.  its repair pass then suffers under enormous directories because it
 * reads the whole thing into memory to detect duplicates.
 *
 * we'll take a compromise which expands the reach of a regular fsck pass by
 * using a slightly larger block size but which repairs in place rather than
 * reading the dir into memory.
 *
 * if we ever truly care to invest in duplicate detection and repair we could
 * either explicitly use some external sort and merge algo or perhaps just
 * combine mmap and some internal sort that has strong enough locality of
 * reference to work well with the vm.
 */
static errcode_t fix_dirent_dups(o2fsck_state *ost,
				 o2fsck_dirblock_entry *dbe,
				 struct ocfs2_dir_entry *dirent,
				 o2fsck_strings *strings,
				 unsigned int *flags)
{
	errcode_t ret = 0;
	char *new_name = NULL;
	int was_set, i;

	/* start over every N bytes of dirent */
	if (o2fsck_strings_bytes_allocated(strings) > (4 * 1024 * 1024))
		o2fsck_strings_free(strings);

	ret = o2fsck_strings_insert(strings, dirent->name, dirent->name_len, 
				    &was_set);
	if (ret) {
		com_err(whoami, ret, "while allocating space to find "
			"duplicate directory entries");
		goto out;
	}

	if (!was_set)
		goto out;

	new_name = calloc(1, dirent->rec_len + 1);
	if (new_name == NULL) {
		ret = OCFS2_ET_NO_MEMORY;
		com_err(whoami, ret, "while trying to generate a new name "
			"for duplicate file name '%.*s' in dir inode "
			"%"PRIu64, dirent->name_len, dirent->name,
			dbe->e_ino);
		goto out;
	}

	/* just simple mangling for now */ 
	memcpy(new_name, dirent->name, dirent->name_len);
	was_set = 1;
	/* append '_' to free space in the dirent until its unique */
	for (i = dirent->name_len ; was_set && i < dirent->rec_len; i++){
		new_name[i] = '_';
		if (!o2fsck_strings_exists(strings, new_name, strlen(new_name)))
			was_set = 0;
	}

	/* rename characters at the end to '_' until its unique */
	for (i = dirent->name_len - 1 ; was_set && i >= 0; i--) {
		new_name[i] = '_';
		if (!o2fsck_strings_exists(strings, new_name, strlen(new_name)))
			was_set = 0;
	}

	if (was_set) {
		printf("Directory inode %"PRIu64" contains a duplicate "
		       "occurance " "of the file name '%.*s' but fsck was "
		       "unable to come up with a unique name so this duplicate "
		       "name will not be dealt with.\n.",
			dbe->e_ino, dirent->name_len, dirent->name);
		goto out;
	}

	if (!prompt(ost, PY, PR_DIRENT_DUPLICATE,
		    "Directory inode %"PRIu64" contains a duplicate occurance "
		    "of the file name '%.*s'.  Replace this duplicate name "
		    "with '%s'?", dbe->e_ino, dirent->name_len, dirent->name,
		    new_name)) {
		/* we don't really care that we leak new_name's recording
		 * in strings, it'll be freed later */
		goto out;
	}

	ret = o2fsck_strings_insert(strings, new_name, strlen(new_name),
				    NULL);
	if (ret) {
		com_err(whoami, ret, "while allocating space to track "
			"duplicates of a newly renamed dirent");
		goto out;
	}

	dirent->name_len = strlen(new_name);
	memcpy(dirent->name, new_name, dirent->name_len);
	*flags |= OCFS2_DIRENT_CHANGED;

out:
	if (new_name != NULL)
		free(new_name);
	return ret;
}

static errcode_t fix_dirent_index(o2fsck_dirblock_entry *dbe,
				  struct dirblock_data *dd,
				  struct ocfs2_dir_entry *dirent,
				  unsigned int *flags)
{
	errcode_t ret = 0;
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)dd->inoblock_buf;
	uint64_t ino;

	if (!ocfs2_supports_indexed_dirs(OCFS2_RAW_SB(dd->fs->fs_super)))
		goto out;

	if (di->i_dyn_features  & OCFS2_INDEXED_DIR_FL) {
		ret = ocfs2_lookup(dd->fs, dbe->e_ino, dirent->name,
				   dirent->name_len, NULL, &ino);
		if (ret) {
			if (ret != OCFS2_ET_FILE_NOT_FOUND)
				goto out;
			ret = 0;

			if (prompt(dd->ost, PY, PR_DX_LOOKUP_FAILED,
				   "Directory inode %"PRIu64" is missing "
				   "an index entry for the file \"%.*s\""
				   " (inode # %"PRIu64")\n. Repair this by "
				   "rebuilding the directory index?",
				   dbe->e_ino, dirent->name_len, dirent->name,
				   ino))
				*flags |= OCFS2_DIRENT_CHANGED;
			goto out;
		}
	}
out:
	return ret;
}

static int corrupt_dirent_lengths(struct ocfs2_dir_entry *dirent, int left)
{
	if ((dirent->rec_len >= OCFS2_DIR_REC_LEN(1)) &&
	    ((dirent->rec_len & OCFS2_DIR_ROUND) == 0) &&
	    (dirent->rec_len <= left) &&
	    (OCFS2_DIR_REC_LEN(dirent->name_len) <= dirent->rec_len) &&
	    !dirent_leaves_partial(dirent, left))
		return 0;

	verbosef("corrupt dirent: %"PRIu64" rec_len %u name_len %u\n",
		 (uint64_t)dirent->inode, dirent->rec_len, dirent->name_len);

	return 1;
}

/* this could certainly be more clever to issue reads in groups */
static unsigned pass2_dir_block_iterate(o2fsck_dirblock_entry *dbe, 
					void *priv_data) 
{
	struct dirblock_data *dd = priv_data;
	struct ocfs2_dir_entry *dirent, *prev = NULL;
	unsigned int offset = 0, ret_flags = 0, end = dd->fs->fs_blocksize;
	unsigned int write_off, saved_reclen;
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)dd->inoblock_buf; 
	errcode_t ret = 0;

	if (!o2fsck_test_inode_allocated(dd->ost, dbe->e_ino)) {
		printf("Directory block %"PRIu64" belongs to directory inode "
		       "%"PRIu64" which isn't allocated.  Ignoring this "
		       "block.", dbe->e_blkno, dbe->e_ino);
		goto out;
	}

	if (dbe->e_ino != dd->last_ino) {
		o2fsck_strings_free(&dd->strings);
		dd->last_ino = dbe->e_ino;

		ret = ocfs2_read_inode(dd->ost->ost_fs, dbe->e_ino,
				       dd->inoblock_buf);
		if (ret) {
			com_err(whoami, ret, "while reading dir inode %"PRIu64,
				dbe->e_ino);
			ret_flags |= OCFS2_DIRENT_ABORT;
			goto out;
		}

		verbosef("dir inode %"PRIu64" i_size %"PRIu64"\n",
			 dbe->e_ino, (uint64_t)di->i_size);

		/* Set the flag for index rebuilding */
		if (ocfs2_supports_indexed_dirs(OCFS2_RAW_SB(dd->fs->fs_super))
			&& !(di->i_dyn_features & OCFS2_INLINE_DATA_FL)
			&& !(di->i_dyn_features & OCFS2_INDEXED_DIR_FL)) {
			ret_flags |= OCFS2_DIRENT_CHANGED;
		}

	}

	verbosef("dir block %"PRIu64" block offs %"PRIu64" in ino\n",
		 dbe->e_blkno, dbe->e_blkcount);

	if (di->i_dyn_features & OCFS2_INLINE_DATA_FL) {
		if (dbe->e_ino != dbe->e_blkno)
			goto out;

		memcpy(dd->dirblock_buf, dd->inoblock_buf,
		       dd->fs->fs_blocksize);
		offset = offsetof(struct ocfs2_dinode, id2.i_data.id_data);
	} else {
		if (dbe->e_blkcount >= ocfs2_blocks_in_bytes(dd->fs,
							     di->i_size))
			goto out;

		ret = ocfs2_read_dir_block(dd->fs, di, dbe->e_blkno,
					   dd->dirblock_buf);
		if (ret && ret != OCFS2_ET_DIR_CORRUPTED) {
			com_err(whoami, ret, "while reading dir block %"PRIu64,
				dbe->e_blkno);
			goto out;
		}

		if (ocfs2_dir_has_trailer(dd->fs, di))
			end = ocfs2_dir_trailer_blk_off(dd->fs);
	}

	write_off = offset;

	while (offset < end) {
		dirent = (struct ocfs2_dir_entry *)(dd->dirblock_buf + offset);

		verbosef("checking dirent offset %d, rec_len %"PRIu16" "
			 "name_len %"PRIu8" file_type %"PRIu8"\n",
			offset, dirent->rec_len, 
			dirent->name_len, dirent->file_type);

		/* XXX I wonder if we should be checking that the padding
		 * is 0 */

		/* if we can't trust this dirent then fix it up or skip
		 * the whole block */
		if (corrupt_dirent_lengths(dirent,
					   end - offset)) {
			if (!prompt(dd->ost, PY, PR_DIRENT_LENGTH,
				    "Directory inode %"PRIu64" "
				    "corrupted in logical block %"PRIu64" "
				    "physical block %"PRIu64" offset %d. "
				    "Attempt to repair this block's directory "
				    "entries?", dbe->e_ino, dbe->e_blkcount,
				    dbe->e_blkno, offset))
				break;

			/* we edit the dirent in place so we try to parse
			 * it again after fixing it */
			fix_dirent_lengths(dirent, end - offset,
					   prev, &ret_flags);
			continue;

		}

		/* 
		 * In general, these calls mark ->inode as 0 when they want it
		 * to be seen as deleted; ignored by fsck and reclaimed by the
		 * kernel.  The dots are a special case, of course.  This
		 * pass makes sure that they are the first two entries in
		 * the directory and pass3 fixes ".."'s ->inode.
		 *
		 * XXX should verify that ocfs2 reclaims entries like that.
		 */
		ret = fix_dirent_dots(dd->ost, dbe, dirent, offset, 
				      end - offset, &ret_flags);
		if (ret)
			goto out;
		if (dirent->inode == 0)
			goto next;

		fix_dirent_name(dd->ost, dbe, dirent, offset, &ret_flags);
		if (dirent->inode == 0)
			goto next;

		fix_dirent_inode(dd->ost, dbe, dirent, offset, &ret_flags);
		if (dirent->inode == 0)
			goto next;

		ret = fix_dirent_filetype(dd->ost, dbe, dirent, offset,
					  &ret_flags);
		if (ret)
			goto out;
		if (dirent->inode == 0)
			goto next;

		ret = fix_dirent_linkage(dd->ost, dbe, dirent, offset,
					 &ret_flags);
		if (ret)
			goto out;
		if (dirent->inode == 0)
			goto next;

		ret = fix_dirent_dups(dd->ost, dbe, dirent, &dd->strings,
				      &ret_flags);
		if (ret)
			goto out;
		if (dirent->inode == 0)
			goto next;

		ret = fix_dirent_index(dbe, dd, dirent, &ret_flags);
		if (ret)
			goto out;

		verbosef("dirent %.*s refs ino %"PRIu64"\n", dirent->name_len,
				dirent->name, (uint64_t)dirent->inode);
		o2fsck_icount_delta(dd->ost->ost_icount_refs, dirent->inode, 1);
next:
		saved_reclen = dirent->rec_len;
		if (dd->ost->ost_compress_dirs) {
			if (prev && prev->inode) {
				/*Bring previous rec_len to required space */
				prev->rec_len = OCFS2_DIR_REC_LEN(prev->name_len);
				write_off += prev->rec_len;
			}
			if (write_off < offset) {
				verbosef("ino: %llu woff: %u off: %u\n",
					dirent->inode, write_off, offset);
				memmove(dd->dirblock_buf + write_off,
					dd->dirblock_buf + offset,
					OCFS2_DIR_REC_LEN(dirent->name_len));
				dirent = (struct ocfs2_dir_entry *)(dd->dirblock_buf + write_off);
				/* Cover space from our new location to
				* the next dirent */
				dirent->rec_len = saved_reclen + offset - write_off;

				ret_flags |= OCFS2_DIRENT_CHANGED;
			}
		}
		prev = dirent;
		offset += saved_reclen;
	}

	if (ocfs2_dir_has_trailer(dd->fs, di))
		fix_dir_trailer(dd->ost,
				dbe,
				ocfs2_dir_trailer_from_block(dd->fs,
							     dd->dirblock_buf),
							     &ret_flags);

	if (ret_flags & OCFS2_DIRENT_CHANGED) {
		if (di->i_dyn_features & OCFS2_INLINE_DATA_FL) {
			memcpy(dd->inoblock_buf, dd->dirblock_buf,
			       dd->fs->fs_blocksize);
			ret = ocfs2_write_inode(dd->fs, dbe->e_ino,
						dd->dirblock_buf);
		} else
			ret = ocfs2_write_dir_block(dd->fs, di, dbe->e_blkno,
						    dd->dirblock_buf);
		if (ret) {
			com_err(whoami, ret, "while writing dir block %"PRIu64,
				dbe->e_blkno);
			dd->ost->ost_write_error = 1;
			goto out;
		}

		if (ocfs2_supports_indexed_dirs(OCFS2_RAW_SB(dd->fs->fs_super))
			&& !(di->i_dyn_features & OCFS2_INLINE_DATA_FL)) {
			di->i_dyn_features |= OCFS2_INDEXED_DIR_FL;
			ret = o2fsck_try_add_reidx_dir(&dd->re_idx_dirs, dbe->e_ino);
			if (ret) {
				com_err(whoami, ret, "while adding block for "
					"directory inode %"PRIu64" to rebuild "
					"dir index", dbe->e_ino);
				goto out;
			}
		}
	}

	/* truncate invalid indexed tree */
	if ((!ocfs2_supports_indexed_dirs(OCFS2_RAW_SB(dd->fs->fs_super)))&&
	     di->i_dyn_features & OCFS2_INDEXED_DIR_FL ) {
		/* ignore the return value */
		if (prompt(dd->ost, PY, PR_IV_DX_TREE, "A directory index was "
			   "found on inode %"PRIu64" but this filesystem does"
			   "not support directory indexes. Truncate the invalid index?",
			   dbe->e_ino))
			ocfs2_dx_dir_truncate(dd->fs, dbe->e_ino);
	}

out:
	if (ret)
		dd->ret = ret;
	return ret_flags;
}

static void release_re_idx_dirs_rbtree(struct rb_root * root)
{
	struct rb_node *node;
	o2fsck_dirblock_entry *dp;

	while ((node = rb_first(root)) != NULL) {
		dp = rb_entry(node, o2fsck_dirblock_entry, e_node);
		rb_erase(&dp->e_node, root);
		ocfs2_free(&dp);
	}
}

errcode_t o2fsck_pass2(o2fsck_state *ost)
{
	o2fsck_dir_parent *dp;
	errcode_t ret;
	struct dirblock_data dd = {
		.ost = ost,
		.fs = ost->ost_fs,
		.last_ino = 0,
		.re_idx_dirs = RB_ROOT,
	};

	printf("Pass 2: Checking directory entries.\n");

	o2fsck_strings_init(&dd.strings);

	ret = ocfs2_malloc_block(ost->ost_fs->fs_io, &dd.dirblock_buf);
	if (ret) {
		com_err(whoami, ret, "while allocating a block buffer to "
			"store directory blocks.");
		goto out;
	}

	ret = ocfs2_malloc_block(ost->ost_fs->fs_io, &dd.inoblock_buf);
	if (ret) {
		com_err(whoami, ret, "while allocating a block buffer to "
			"store a directory inode.");
		goto out;
	}

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

	o2fsck_dir_block_iterate(ost, pass2_dir_block_iterate, &dd);

	if (dd.re_idx_dirs.rb_node) {
		ret = o2fsck_rebuild_indexed_dirs(ost->ost_fs, &dd.re_idx_dirs);
		if (ret)
			com_err(whoami, ret, "while rebuild indexed dirs.");
	}
	release_re_idx_dirs_rbtree(&dd.re_idx_dirs);

	o2fsck_strings_free(&dd.strings);
out:
	if (dd.dirblock_buf)
		ocfs2_free(&dd.dirblock_buf);
	if (dd.inoblock_buf)
		ocfs2_free(&dd.inoblock_buf);
	return ret;
}
