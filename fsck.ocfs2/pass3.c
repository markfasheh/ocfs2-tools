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
 * Pass 3 makes sure that all directories are connected to the file system
 * tree and that their are no cycles in the tree.  It starts by marking
 * the root and system directories in the filesystem as connected.  It then
 * iterates through the directories found in pass 1.  For each directory
 * it ascends to the root of the file system via the chain of parent dir
 * entries as built up by pass 2.  If a directory is found which doesn't have
 * a parent it is connected to lost+found.  connect_directory() is careful
 * to stop before following a parent that it has already seen.  This lets it
 * connect to lost+found instead and break cycles.
 */
#include <inttypes.h>
#include <string.h>
#include <limits.h>

#include <ocfs2.h>

#include "dirparents.h"
#include "fsck.h"
#include "pass2.h"
#include "pass3.h"
#include "problem.h"
#include "util.h"

static const char *whoami = "pass3";

static void check_root(o2fsck_state *ost)
{
	ocfs2_super_block *sb = OCFS2_RAW_SB(ost->ost_fs->fs_super);
	errcode_t ret;
	uint64_t blkno, old_root;
	int was_set;

	if (o2fsck_test_inode_allocated(ost, ost->ost_fs->fs_root_blkno)) {
		ocfs2_bitmap_test(ost->ost_dir_inodes, 
				ost->ost_fs->fs_root_blkno, &was_set);
		if (!was_set)
			printf("The root inode exists but isn't a "
			       "directory.\n");
		return;
	}

	if (!prompt(ost, PY, PR_ROOT_DIR_MISSING,
		    "The super block claims that inode %"PRIu64" is the root "
		    "directory but it isn't allocated.  Create a new root "
		    "directory and update the super block?",
		    ost->ost_fs->fs_root_blkno))
		return;

	ret = ocfs2_new_inode(ost->ost_fs, &blkno, 0755 | S_IFDIR);
	if (ret) {
		com_err(whoami, ret, "while trying to allocate a new inode "
			"for the root directory\n");
		return;
	}

	ret = ocfs2_expand_dir(ost->ost_fs, blkno, blkno);
	if (ret) {
		com_err(whoami, ret, "while trying to expand a new root "
			"directory");
		goto out;
	}

	o2fsck_icount_set(ost->ost_icount_in_inodes, blkno, 1);
	o2fsck_icount_set(ost->ost_icount_refs, blkno, 1);
	ret = o2fsck_add_dir_parent(&ost->ost_dir_parents, blkno, 
				    ost->ost_fs->fs_root_blkno,
				    ost->ost_fs->fs_root_blkno);
	if (ret) {
		com_err(whoami, ret, "while recording a new root directory");
		goto out;
	}

	old_root = sb->s_root_blkno;
	ost->ost_fs->fs_root_blkno = blkno;
	sb->s_root_blkno = blkno;

	ret = ocfs2_write_super(ost->ost_fs);
	if (ret) {
		com_err(whoami, ret, "while writing the super block with a "
			"new root directory inode");
		ost->ost_fs->fs_root_blkno = old_root;
		sb->s_root_blkno = old_root;
		goto out;
	}

	blkno = 0;

out:
	if (blkno) {
		ret = ocfs2_delete_inode(ost->ost_fs, blkno);
		if (ret) {
			com_err(whoami, ret, "while trying to clean up an "
			        "an allocated inode after linking /lost+found "
				"failed");
		}
	}
}

static void check_lostfound(o2fsck_state *ost)
{
	char name[] = "lost+found";
	int namelen = sizeof(name) - 1;
	uint64_t blkno;
	errcode_t ret;

	ret = ocfs2_lookup(ost->ost_fs, ost->ost_fs->fs_root_blkno, name,
			   namelen, NULL, &ost->ost_lostfound_ino);
	if (ret == 0)
		return;

	if (!prompt(ost, PY, PR_LOSTFOUND_MISSING,
		    "/lost+found does not exist.  Create it so "
		    "that we can possibly fill it with orphaned inodes?"))
		return;

	ret = ocfs2_new_inode(ost->ost_fs, &blkno, 0755 | S_IFDIR);
	if (ret) {
		com_err(whoami, ret, "while trying to allocate a new inode "
			"for /lost+found");
		return;
	}

	ret = ocfs2_expand_dir(ost->ost_fs, blkno, ost->ost_fs->fs_root_blkno);
	if (ret) {
		com_err(whoami, ret, "while trying to expand a new "
			"/lost+found directory");
		goto out;
	}

	/* XXX expand_dir itself will leak added dir blocks in some error
	 * paths so we don't bother trying to clean them up either */
	ret = ocfs2_link(ost->ost_fs, ost->ost_fs->fs_root_blkno, name, blkno,
			 OCFS2_FT_DIR);
	if (ret) {
		com_err(whoami, ret, "while linking inode %"PRIu64" as "
			"/lost+found", blkno);
		goto out;
	}

	/* XXX maybe this should be a helper to clean up the dir tracking
	 * for any new dir.  "2" for both the l+f dirent pointing to the
	 * inode and the "." dirent in its dirblock */
	o2fsck_icount_set(ost->ost_icount_in_inodes, blkno, 2);
	o2fsck_icount_set(ost->ost_icount_refs, blkno, 2);
	ret = o2fsck_add_dir_parent(&ost->ost_dir_parents, blkno, 
				    ost->ost_fs->fs_root_blkno,
				    ost->ost_fs->fs_root_blkno);
	if (ret) {
		com_err(whoami, ret, "while recording a new /lost+found "
			"directory");
		goto out;
	}

	/* we've already iterated through the dirblocks in pass2 so there
	 * is no need to register l+f's new dir block */

	ost->ost_lostfound_ino = blkno;
	blkno = 0;
out:
	if (blkno) {
		ret = ocfs2_delete_inode(ost->ost_fs, blkno);
		if (ret) {
			com_err(whoami, ret, "while trying to clean up an "
			        "an allocated inode after linking /lost+found "
				"failed");
		}
	}
}

struct fix_dot_dot_args {
	o2fsck_state	*ost;
	uint64_t	parent;
	int		fixed;
};

static int fix_dot_dot_dirent(struct ocfs2_dir_entry *dirent,
			      int	offset,
			      int	blocksize,
			      char	*buf,
			      void	*priv_data)
{
	struct fix_dot_dot_args *args = priv_data;

	if (dirent->name_len != 2 || strncmp(dirent->name, "..", 2))
		return 0;

	verbosef("fixing '..' entry to point to %"PRIu64"\n", args->parent);
	
	if (dirent->inode != 0)
		o2fsck_icount_delta(args->ost->ost_icount_refs, dirent->inode, 
					-1);
	o2fsck_icount_delta(args->ost->ost_icount_refs, args->parent, 1);

	dirent->inode = args->parent;
	args->fixed = 1;

	return OCFS2_DIRENT_ABORT | OCFS2_DIRENT_CHANGED;
}

static void fix_dot_dot(o2fsck_state *ost, o2fsck_dir_parent *dir)
{
	errcode_t ret;

	struct fix_dot_dot_args args = {
		.ost = ost,
		.parent = dir->dp_dirent,
		.fixed = 0,
	};

	ret = ocfs2_dir_iterate(ost->ost_fs, dir->dp_ino, 
				OCFS2_DIRENT_FLAG_INCLUDE_EMPTY, NULL,
				fix_dot_dot_dirent, &args);
	if (ret) {
		com_err("fix_dot_dot", ret, "while iterating through dir "
			"inode %"PRIu64"'s directory entries.", dir->dp_dirent);
		/* XXX mark fs invalid */
		return;
	}

	if (!args.fixed) {
		fprintf(stderr, "Didn't find a '..' entry to fix.\n");
		/* XXX mark fs invalid */
		return;
	}

	dir->dp_dot_dot = dir->dp_dirent;
}

/* add a directory entry that points to a given inode in lost+found. */
void o2fsck_reconnect_file(o2fsck_state *ost, uint64_t inode)
{
	static char iname[NAME_MAX + 1];
	char name[] = "lost+found";
	int namelen = sizeof(name) - 1;
	o2fsck_dir_parent *dp;
	errcode_t ret;
	uint8_t type;
	int len;

	if (ost->ost_lostfound_ino == 0) {
		ret = ocfs2_lookup(ost->ost_fs, ost->ost_fs->fs_root_blkno,
				   name, namelen, NULL,
				   &ost->ost_lostfound_ino);
		if (ret) {
			com_err(whoami, ret, "while trying to find the "
				"/lost+found directory so that inode "
				"%"PRIu64" could be moved there.", inode);
			goto out;
		}
	}

	len = snprintf(iname, sizeof(iname), "#%"PRIu64, inode);
	if (len <= 0) {
		ret = OCFS2_ET_NO_MEMORY;
		com_err(whoami, ret, "while trying to build a new file name "
			"for inode %"PRIu64" to use in /lost+found", inode);
		goto out;
	}

	ret = o2fsck_type_from_dinode(ost, inode, &type);
	if (ret)
		goto out;

	/* XXX I gotta say, ocfs2_link_and_expand() seems pretty reasonable */
	ret = ocfs2_link(ost->ost_fs, ost->ost_lostfound_ino, iname, inode,
			 type);
	if (ret) {
		if (ret == OCFS2_ET_DIR_NO_SPACE) {
			ret = ocfs2_expand_dir(ost->ost_fs,
					       ost->ost_lostfound_ino,
					       ost->ost_fs->fs_root_blkno);
			if (ret == 0)
				ret = ocfs2_link(ost->ost_fs,
						 ost->ost_lostfound_ino,
						 iname, inode, type);
		}
		com_err(whoami, ret, "while trying to link inode %"PRIu64" "
			"into /lost+found", inode);
		goto out;
	}

	/* add another ref to account for this new dirent */
	o2fsck_icount_delta(ost->ost_icount_refs, inode, 1);

	/* if we just added a directory to l+f we need to track that 
	 * the new dirent points to the dir.  we leave the dot_dot tracking
	 * intact because we didn't change that in the dirblock.. */
	if (type == OCFS2_FT_DIR) {
		dp = o2fsck_dir_parent_lookup(&ost->ost_dir_parents, inode);
		if (dp == NULL) {
			ret = OCFS2_ET_INTERNAL_FAILURE;
			com_err(whoami, ret, "while looking up the directory "
				"parent structure for inode %"PRIu64, inode);
			goto out;
		}
		dp->dp_dirent = ost->ost_lostfound_ino;
	}

out:
	return;
}

static uint64_t loop_no = 0;

static errcode_t connect_directory(o2fsck_state *ost,
				   o2fsck_dir_parent *dir)
{
	o2fsck_dir_parent *dp = dir, *par;
	errcode_t ret = 0;
	int fix;

	verbosef("checking dir inode %"PRIu64" parent %"PRIu64" dot_dot "
		"%"PRIu64"\n", dir->dp_ino, dp->dp_dirent, dp->dp_dot_dot);

	loop_no++;

	while(!dp->dp_connected) {

		/* we either will ascend to a parent that is connected or
		 * we'll graft the subtree with this directory on to lost
		 * and found. */ 
		dp->dp_connected = 1;

		/* move on to the parent dir only if it exists and we haven't
		 * already traversed it in this instance of parent walking */
		if (dp->dp_dirent) {
			par = o2fsck_dir_parent_lookup(&ost->ost_dir_parents, 
							dp->dp_dirent);
			if (par == NULL) {
				ret = OCFS2_ET_INTERNAL_FAILURE;
				com_err(whoami, ret, "no dir info for parent "
					"%"PRIu64, dp->dp_dirent);
				goto out;
			}
			if (par->dp_loop_no != loop_no) {
				par->dp_loop_no = loop_no;
				dp = par;
				continue;
			}
		}

		/* ok, we hit an orphan subtree with no parent or are at 
		 * the dir in a subtree that is the first to try to reference
		 * a dir in its children */
		fix = prompt(ost, PY, PR_DIR_NOT_CONNECTED,
			     "Directory inode %"PRIu64" isn't "
			     "connected to the filesystem.  Move it to "
			     "lost+found?", dp->dp_ino);
		if (fix)
			o2fsck_reconnect_file(ost, dp->dp_ino);

		break;
	}

	if (dir->dp_dirent != dir->dp_dot_dot) {
		fix = prompt(ost, PY, PR_DIR_DOTDOT,
			     "Directory inode %"PRIu64" is "
			     "referenced by a dirent in directory %"PRIu64" "
			     "but its '..' entry points to inode %"PRIu64". "
			     "Fix the '..' entry to reference %"PRIu64"?", 
			     dir->dp_ino, dir->dp_dirent, dir->dp_dot_dot, 
			     dir->dp_dirent);
		if (fix)
			fix_dot_dot(ost, dir);
	}
out:
	return ret;
}

errcode_t o2fsck_pass3(o2fsck_state *ost)
{
	o2fsck_dir_parent *dp;
	errcode_t ret = 0;

	printf("Pass 3: Checking directory connectivity.\n");

	/* these could probably share more code.  We might need to treat the
	 * other required directories like root here */

	check_root(ost);
	check_lostfound(ost);

	dp = o2fsck_dir_parent_lookup(&ost->ost_dir_parents, 
					ost->ost_fs->fs_root_blkno);
	if (dp == NULL) {
		ret = OCFS2_ET_INTERNAL_FAILURE;
		com_err(whoami, ret, "root inode %"PRIu64" wasn't marked as "
			"a directory in pass1", ost->ost_fs->fs_root_blkno);
		goto out;
	}
	dp->dp_connected = 1;

	dp = o2fsck_dir_parent_lookup(&ost->ost_dir_parents, 
					ost->ost_fs->fs_sysdir_blkno);
	if (dp == NULL) {
		ret = OCFS2_ET_INTERNAL_FAILURE;
		com_err(whoami, ret, "system dir inode %"PRIu64" wasn't "
			"marked as a directory in pass1",
			ost->ost_fs->fs_sysdir_blkno);
		goto out;
	}
	dp->dp_connected = 1;

	for(dp = o2fsck_dir_parent_first(&ost->ost_dir_parents) ;
	    dp; dp = o2fsck_dir_parent_next(dp)) {
		/* XXX hmm, make sure dir->ino is in the dir map? */
		ret = connect_directory(ost, dp);
		if (ret)
			goto out;
	}

out:
	return ret;
}
