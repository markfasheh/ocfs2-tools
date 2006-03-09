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
	o2fsck_state 	*ost;
	ocfs2_filesys 	*fs;
	char 		*buf;
	errcode_t	ret;
	o2fsck_strings	strings;
	uint64_t	last_ino;
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

static errcode_t fix_dirent_dots(o2fsck_state *ost, o2fsck_dirblock_entry *dbe,
				 struct ocfs2_dir_entry *dirent, int offset, 
				 int left, int *flags)
{
	int expect_dots = expected_dots(dbe, offset);
	int changed_len = 0;
	struct ocfs2_dir_entry *next;
	uint16_t new_len;
	errcode_t ret = 0;

	if (!expect_dots) {
	       	if (!dirent_has_dots(dirent, 1) && !dirent_has_dots(dirent, 2))
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
		   "the '.' entry?", dbe->e_ino, dirent->inode)) {
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
			       int *flags)
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
			    int *flags)
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
			     int *flags)
{
	if (ocfs2_block_out_of_range(ost->ost_fs, dirent->inode) &&
	    prompt(ost, PY, PR_DIRENT_INODE_RANGE,
		   "Directory entry '%.*s' refers to inode "
		   "number %"PRIu64" which is out of range, clear the entry?",
		   dirent->name_len, dirent->name, dirent->inode)) {

		dirent->inode = 0;
		*flags |= OCFS2_DIRENT_CHANGED;
		goto out;
	}

	if (!o2fsck_test_inode_allocated(ost, dbe->e_ino) &&
	    prompt(ost, PY, PR_DIRENT_INODE_FREE,
		   "Directory entry '%.*s' refers to inode number "
		   "%"PRIu64" which isn't allocated, clear the entry?", 
		   dirent->name_len, dirent->name, dirent->inode)) {
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
				     int *flags)
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

	ret = ocfs2_bitmap_test(ost->ost_bad_inodes, dirent->inode, &was_set);
	if (ret)
		goto out;
	if (was_set) {
		expected_type = OCFS2_FT_UNKNOWN;
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
		dirent->inode,
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
				    int *flags)
{
	int expect_dots = expected_dots(dbe, offset);
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
			"the dir bitmap", dirent->inode);
	if (!is_dir)
		goto out;

	dp = o2fsck_dir_parent_lookup(&ost->ost_dir_parents, dirent->inode);
	if (dp == NULL) {
		ret = OCFS2_ET_INTERNAL_FAILURE;
		com_err(whoami, ret, "no dir parents recorded for inode "
			"%"PRIu64, dirent->inode);
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
		dirent->name_len, dirent->name, dirent->inode)) {

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
				 int *flags)
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

static int corrupt_dirent_lengths(struct ocfs2_dir_entry *dirent, int left)
{
	if ((dirent->rec_len >= OCFS2_DIR_REC_LEN(1)) &&
	    ((dirent->rec_len & OCFS2_DIR_ROUND) == 0) &&
	    (dirent->rec_len <= left) &&
	    (OCFS2_DIR_REC_LEN(dirent->name_len) <= dirent->rec_len) &&
	    !dirent_leaves_partial(dirent, left))
		return 0;

	verbosef("corrupt dirent: %"PRIu64" rec_len %u name_len %u\n",
		dirent->inode, dirent->rec_len, dirent->name_len);

	return 1;
}

static size_t nr_zeros(unsigned char *buf, size_t len)
{
	size_t ret = 0;

	while(len-- > 0 && *(buf++) == 0)
		ret++;

	return ret;
}

/* this could certainly be more clever to issue reads in groups */
static unsigned pass2_dir_block_iterate(o2fsck_dirblock_entry *dbe, 
					void *priv_data) 
{
	struct dirblock_data *dd = priv_data;
	struct ocfs2_dir_entry *dirent, *prev = NULL;
	unsigned int offset = 0, ret_flags = 0;
	errcode_t ret;

	if (!o2fsck_test_inode_allocated(dd->ost, dbe->e_ino)) {
		printf("Directory block %"PRIu64" belongs to directory inode "
		       "%"PRIu64" which isn't allocated.  Ignoring this "
		       "block.", dbe->e_blkno, dbe->e_ino);
		return 0;
	}

	if (dbe->e_ino != dd->last_ino) {
		o2fsck_strings_free(&dd->strings);
		dd->last_ino = dbe->e_ino;
	}

 	ret = ocfs2_read_dir_block(dd->fs, dbe->e_blkno, dd->buf);
	if (ret && ret != OCFS2_ET_DIR_CORRUPTED) {
		com_err(whoami, ret, "while reading dir block %"PRIu64,
			dbe->e_blkno);
		goto out;
	}

	/*
	 * pass1 records all the blocks that have been allocated to the
	 * dir so that it can verify i_clusters and make sure dirs don't
	 * share blocks, etc.  Unfortunately allocated blocks that are
	 * beyond i_size aren't initialized.. we special case a block of
	 * all 0s as an uninitialized dir block, though we don't actually
	 * make sure that it's outside i_size.
	 */
	if (nr_zeros(dd->buf, dd->fs->fs_blocksize) == dd->fs->fs_blocksize)
		return 0;

	verbosef("dir block %"PRIu64"\n", dbe->e_blkno);

	while (offset < dd->fs->fs_blocksize) {
		dirent = (struct ocfs2_dir_entry *)(dd->buf + offset);

		verbosef("checking dirent offset %d, ino %"PRIu64" rec_len "
			"%"PRIu16" name_len %"PRIu8" file_type %"PRIu8"\n",
			offset, dirent->inode, dirent->rec_len, 
			dirent->name_len, dirent->file_type);

		/* XXX I wonder if we should be checking that the padding
		 * is 0 */

		/* if we can't trust this dirent then fix it up or skip
		 * the whole block */
		if (corrupt_dirent_lengths(dirent,
					   dd->fs->fs_blocksize - offset)) {
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
			fix_dirent_lengths(dirent,
					   dd->fs->fs_blocksize - offset,
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
					     dd->fs->fs_blocksize - offset,
					     &ret_flags);
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

		verbosef("dirent %.*s refs ino %"PRIu64"\n", dirent->name_len,
				dirent->name, dirent->inode);
		o2fsck_icount_delta(dd->ost->ost_icount_refs, dirent->inode, 1);
next:
		offset += dirent->rec_len;
		prev = dirent;
	}

	if (ret_flags & OCFS2_DIRENT_CHANGED) {
		ret = ocfs2_write_dir_block(dd->fs, dbe->e_blkno, dd->buf);
		if (ret) {
			com_err(whoami, ret, "while writing dir block %"PRIu64,
				dbe->e_blkno);
			dd->ost->ost_write_error = 1;
		}
	}

out:
	if (ret)
		dd->ret = ret;
	return ret_flags;
}

errcode_t o2fsck_pass2(o2fsck_state *ost)
{
	errcode_t retval;
	o2fsck_dir_parent *dp;
	struct dirblock_data dd = {
		.ost = ost,
		.fs = ost->ost_fs,
		.last_ino = 0,
	};

	printf("Pass 2: Checking directory entries.\n");

	o2fsck_strings_init(&dd.strings);

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
	o2fsck_strings_free(&dd.strings);
	ocfs2_free(&dd.buf);
	return 0;
}
