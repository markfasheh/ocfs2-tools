/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dir.c
 *
 * directory corruptions
 *
 * Copyright (C) 2006 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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

/* This file will create corruption for directory.
 *
 * Directory inode error: DIR_ZERO
 *
 * Dirent dot error:	DIRENT_DOTTY_DUP, DIRENT_NOT_DOTTY, DIRENT_DOT_INODE,
 *			DIRENT_DOT_EXCESS
 *
 * Dirent field error: 	DIRENT_ZERO, DIRENT_NAME_CHARS,DIRENT_INODE_RANGE,
 *			DIRENT_INODE_FREE, DIRENT_TYPE, DIRENT_DUPLICATE,
 *			DIRENT_LENGTH
 *
 * Directory parent duplicate error: DIR_PARENT_DUP
 *
 * Directory not connected error: DIR_NOT_CONNECTED
 *
 */

#include "main.h"

extern char *progname;

void create_directory(ocfs2_filesys *fs,
				uint64_t parentblk, uint64_t *blkno)
{
	errcode_t ret;
	char random_name[OCFS2_MAX_FILENAME_LEN];

	memset(random_name, 0, sizeof(random_name));
	sprintf(random_name, "testXXXXXX");
	
	/* Don't use mkstemp since it will create a file 
	 * in the working directory which is no use.
	 * Use mktemp instead Although there is a compiling warning.
	 * mktemp fails to work in some implementations follow BSD 4.3,
	 * but currently ocfs2 will only support linux,
	 * so it will not affect us.
	 */
	if (!mktemp(random_name))
		FSWRK_COM_FATAL(progname, errno);

	ret = ocfs2_lookup(fs, parentblk, random_name, strlen(random_name),
			   NULL, blkno);
	if (!ret)
		return;
	else if (ret != OCFS2_ET_FILE_NOT_FOUND)
		FSWRK_COM_FATAL(progname, ret);

	ret  = ocfs2_new_inode(fs, blkno, S_IFDIR | 0755);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_init_dir(fs, *blkno, parentblk);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_link(fs, parentblk, random_name, *blkno, OCFS2_FT_DIR);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	return;
}

struct dirent_corrupt_struct  {

	const char	*oldname;
	const char      *name;
	int             namelen;
	int		oldnamelen;
	int             done;
	int		reserved;
};

static int corrupt_match_dirent(struct dirent_corrupt_struct *dcs,
				struct ocfs2_dir_entry *dirent)
{
	if (!dcs->oldname)
		return 1;

	if (((dirent->name_len & 0xFF) != dcs->oldnamelen))
		return 0;

	if (strncmp(dcs->oldname, dirent->name, dirent->name_len & 0xFF))
		return 0;

	return 1;
}

static int rename_dirent_proc(struct ocfs2_dir_entry *dirent,
			      int        offset,
			      int        blocksize,
			      char       *buf,
			      void       *priv_data)
{
	struct  dirent_corrupt_struct *dcs = (struct dirent_corrupt_struct *) priv_data;

	if (!corrupt_match_dirent(dcs, dirent))
		return 0;
	
	if (dcs->namelen <= (dirent->rec_len -
			     offsetof(struct ocfs2_dir_entry, name))) {
		strcpy(dirent->name, dcs->name);
		dirent->name_len = dcs->namelen;
	} else
		FSWRK_FATAL("The lenght of new name for target dirent you"
			    "want to rename didn't fit the old one.\n");
	dcs->done++;

	return OCFS2_DIRENT_ABORT|OCFS2_DIRENT_CHANGED;
}

static int rename_dirent(ocfs2_filesys *fs, uint64_t dir,
			 const char *name, const char *oldname)
{
	errcode_t       rc;
	struct dirent_corrupt_struct dcs;

	if (!(fs->fs_flags & OCFS2_FLAG_RW))
		return OCFS2_ET_RO_FILESYS;

	dcs.name = name;
	dcs.oldname = oldname;
	dcs.namelen = name ? strlen(name) : 0;
	dcs.oldnamelen = oldname ? strlen(oldname) : 0;
	dcs.done = 0;

	rc = ocfs2_dir_iterate(fs, dir, 0, 0, rename_dirent_proc, &dcs);
	if (rc)
		return rc;

	return (dcs.done) ? 0 : OCFS2_ET_DIR_NO_SPACE;
}

static int corrupt_dirent_ino_proc(struct ocfs2_dir_entry *dirent,
				   int        offset,
				   int        blocksize,
				   char       *buf,
				   void       *priv_data)
{
	struct  dirent_corrupt_struct *dcs = (struct dirent_corrupt_struct*) priv_data;

	if (!corrupt_match_dirent(dcs, dirent))
		return 0;

	dirent->inode += dcs->reserved;

	dcs->reserved = dirent->inode;

	dcs->done++;

	return OCFS2_DIRENT_ABORT|OCFS2_DIRENT_CHANGED;
}

static int corrupt_dirent_ino(ocfs2_filesys *fs, uint64_t dir,
			      const char *name, uint64_t *new_ino, int inc)
{
	errcode_t       rc;
	struct dirent_corrupt_struct dcs;

	if (!(fs->fs_flags & OCFS2_FLAG_RW))
		return OCFS2_ET_RO_FILESYS;

	dcs.oldname = name;
	dcs.oldnamelen = name ? strlen(name) : 0;
	dcs.done = 0;
	dcs.reserved = inc;

	rc = ocfs2_dir_iterate(fs, dir, 0, 0, corrupt_dirent_ino_proc, &dcs);
	if (rc)
		return rc;

	*new_ino = dcs.reserved;

	return (dcs.done) ? 0 : OCFS2_ET_DIR_NO_SPACE;
}

static int corrupt_dirent_reclen_proc(struct ocfs2_dir_entry *dirent,
				      int        offset,
				      int        blocksize,
				      char       *buf,
				      void       *priv_data)
{
	struct  dirent_corrupt_struct *dcs = (struct dirent_corrupt_struct*) priv_data;

	if (!corrupt_match_dirent(dcs, dirent))
		return 0;

	dirent->rec_len += dcs->reserved;

	dcs->done++;

	dcs->reserved = dirent->rec_len;

	return OCFS2_DIRENT_ABORT|OCFS2_DIRENT_CHANGED;
}

static int corrupt_dirent_reclen(ocfs2_filesys *fs, uint64_t dir,
				 const char *name, uint64_t *new_reclen,
				 int inc)
{
	errcode_t       rc;
	struct dirent_corrupt_struct dcs;

	if (!(fs->fs_flags & OCFS2_FLAG_RW))
		return OCFS2_ET_RO_FILESYS;

	dcs.oldname = name;
	dcs.oldnamelen = name ? strlen(name) : 0;
	dcs.done = 0;
	dcs.reserved = inc;

	rc = ocfs2_dir_iterate(fs, dir, 0, 0, corrupt_dirent_reclen_proc, &dcs);
	if (rc)
		return rc;

	*new_reclen = dcs.reserved;

	return (dcs.done) ? 0 : OCFS2_ET_DIR_NO_SPACE;
}

static void damage_dir_content(ocfs2_filesys *fs, uint64_t dir,
				enum fsck_type type)
{
	errcode_t ret;
	uint64_t tmp_blkno, tmp_no;
	char name[OCFS2_MAX_FILENAME_LEN];
	mode_t mode;

	memset(name, 0, sizeof(name));
	sprintf(name, "testXXXXXX");
	if (!mktemp(name))
		FSWRK_COM_FATAL(progname, errno);

	switch (type) {
	case DIRENT_DOTTY_DUP:
		/* add another "." at the end of the directory */
		sprintf(name, ".");
		ret = ocfs2_link(fs, dir, name, dir, OCFS2_FT_DIR);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);
		fprintf(stdout, "DIRENT_DOTTY_DUP: "
			"Corrupt directory#%"PRIu64
			", add another '.' to it.\n", dir);
		break;
	case DIRENT_NOT_DOTTY:
		/* rename the first ent from "." to "a". */
		sprintf(name, "a");
		rename_dirent(fs, dir, name, ".");
		fprintf(stdout, "DIRENT_NOT_DOTTY: "
			"Corrupt directory#%"PRIu64
			", change '.' to %s.\n", dir, name);
		break;
	case DIRENT_DOT_INODE:
		fprintf(stdout, "DIRENT_DOT_INODE: "
			"Corrupt directory#%"PRIu64
			", change dot inode to #%"PRIu64".\n", dir, (dir+10));
		corrupt_dirent_ino(fs, dir, ".", &tmp_no, 10);
		break;
	case DIRENT_DOT_EXCESS:
		corrupt_dirent_reclen(fs, dir, ".", &tmp_no, OCFS2_DIR_PAD);
		fprintf(stdout, "DIR_DOT_EXCESS: "
			"Corrupt directory#%"PRIu64","
			"change dot's dirent length from %"PRIu64" "
			"to %"PRIu64"\n",
			dir, tmp_no - OCFS2_DIR_PAD, tmp_no);
		break;
	case DIR_DOTDOT:
		corrupt_dirent_ino(fs, dir, "..", &tmp_no, 10);
		fprintf(stdout, "DIR_DOTDOT: "
			"Corrupt directory#%"PRIu64
			", change dotdot inode from %"PRIu64" to %"PRIu64".\n",
			dir, tmp_no - 10, tmp_no);
		break;
	case DIRENT_ZERO:
		memset(name, 0, 1);
		ret = ocfs2_link(fs, dir, name, dir + 100, OCFS2_FT_DIR);
                if (ret)
                        FSWRK_COM_FATAL(progname, ret);
		fprintf(stdout, "DIRENT_ZERO: "
			"Corrupt directory#%"PRIu64
			", add an zero entry to it.\n", dir);
		break;
	case DIRENT_NAME_CHARS:
		name[0] = 47;
		mode = S_IFREG | 0755;
		ret = ocfs2_new_inode(fs, &tmp_blkno, mode);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);
		ret = ocfs2_link(fs, dir, name, tmp_blkno, OCFS2_FT_REG_FILE);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);
		fprintf(stdout, "DIRENT_NAME_CHARS: "
			"Corrupt directory#%"PRIu64
			", add an invalid entry to it.\n", dir);
		break;
	case DIRENT_INODE_RANGE:
		tmp_blkno = fs->fs_blocks;
		ret = ocfs2_link(fs, dir, name, tmp_blkno, OCFS2_FT_REG_FILE);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);
		corrupt_dirent_ino(fs, dir, name, &tmp_no, 1);
		fprintf(stdout, "DIRENT_INODE_RANGE: "
			"Corrupt directory#%"PRIu64
			", add an entry whose inode exceeds"
			" the limits.\n", dir);
		break;
	case DIRENT_INODE_FREE:
		tmp_blkno = dir + 1000;
		ret = ocfs2_link(fs, dir, name, tmp_blkno, OCFS2_FT_REG_FILE);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);
		fprintf(stdout, "DIRENT_INODE_FREE: "
			"Corrupt directory#%"PRIu64
			", add an entry's inode#%"PRIu64
			" whose inode isn't used.\n", dir, tmp_blkno);
		break;
	case DIRENT_TYPE:
		mode = S_IFREG | 0755;
		ret = ocfs2_new_inode(fs, &tmp_blkno, mode);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);
		ret = ocfs2_link(fs, dir, name, tmp_blkno, OCFS2_FT_SYMLINK);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);
		fprintf(stdout, "DIRENT_TYPE: "
			"Corrupt directory#%"PRIu64
			", change an entry's mode from %u to %u.\n",
			dir, mode, S_IFLNK | 0755);
		break;
	case DIRENT_DUPLICATE:
		mode = S_IFREG | 0755;
		ret = ocfs2_new_inode(fs, &tmp_blkno, mode);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);
		ret = ocfs2_link(fs, dir, name, tmp_blkno, OCFS2_FT_REG_FILE);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);
		ret = ocfs2_link(fs, dir, name, tmp_blkno, OCFS2_FT_REG_FILE);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);
		fprintf(stdout, "DIRENT_DUPLICATE: "
			"Corrupt directory#%"PRIu64
			", add two entries with the same name '%s'.\n",
			dir, name);
		break;
	case DIRENT_LENGTH:
		mode = S_IFREG | 0755;
		ret = ocfs2_new_inode(fs, &tmp_blkno, mode);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);
		ret = ocfs2_link(fs, dir, name, tmp_blkno, OCFS2_FT_REG_FILE);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);
		corrupt_dirent_reclen(fs, dir, name, &tmp_no, 1);
		fprintf(stdout, "DIRENT_LENGTH: "
			"Corrupt directory#%"PRIu64
			", modify entry#%"PRIu64" from %"PRIu64" "
			"to %"PRIu64".\n",
			dir, tmp_blkno, tmp_no - 1, tmp_no);
		break;
	default:
		FSWRK_FATAL("Invalid type = %d\n", type);	
	}

	return;
}

void mess_up_dir_dot(ocfs2_filesys *fs, enum fsck_type type, uint64_t blkno)
{
	uint64_t tmp_blkno;

	create_directory(fs, blkno, &tmp_blkno);
	damage_dir_content(fs, tmp_blkno, type);

	return;
}

void mess_up_dir_dotdot(ocfs2_filesys *fs, enum fsck_type type, uint64_t blkno)
{
	uint64_t tmp_blkno;

	create_directory(fs, blkno, &tmp_blkno);
	damage_dir_content(fs, tmp_blkno, type);

	return;
}

void mess_up_dir_ent(ocfs2_filesys *fs, enum fsck_type type, uint64_t blkno)
{
	uint64_t tmp_blkno;

	create_directory(fs, blkno, &tmp_blkno);
	damage_dir_content(fs, tmp_blkno, type);

	return;
}

void mess_up_dir_parent_dup(ocfs2_filesys *fs, enum fsck_type type,
			    uint64_t blkno)
{
	errcode_t ret;
	uint64_t parent1, parent2, tmp_blkno;
	char random_name[OCFS2_MAX_FILENAME_LEN];

	/* create 2 direcotories */
	create_directory(fs, blkno, &parent1);
	create_directory(fs, blkno, &parent2);

	/* create a directory under parent1, tmp_blkno indicates its inode. */
	create_directory(fs, parent1, &tmp_blkno);

	memset(random_name, 0, sizeof(random_name));
	sprintf(random_name, "testXXXXXX");
	/* Don't use mkstemp since it will create a file 
	 * in the working directory which is no use.
	 * Use mktemp instead Although there is a compiling warning.
	 * mktemp fails to work in some implementations follow BSD 4.3,
	 * but currently ocfs2 will only support linux,
	 * so it will not affect us.
	 */
	if (!mktemp(random_name))
		FSWRK_COM_FATAL(progname, errno);

	ret = ocfs2_link(fs, parent2, random_name, tmp_blkno, OCFS2_FT_DIR);
        if (ret)
		FSWRK_COM_FATAL(progname, ret);
	fprintf(stdout, "DIR_PARENT_DUP: "

		"Create a directory #%"PRIu64
		" which has two parents: #%"PRIu64" and #%"PRIu64".\n",
		tmp_blkno, parent1, parent2);

	return;
}

void mess_up_dir_inode(ocfs2_filesys *fs, enum fsck_type type, uint64_t blkno)
{
	errcode_t ret;
	char *buf = NULL;
	struct ocfs2_dinode *di;
	struct ocfs2_extent_list *el;
	uint64_t tmp_blkno;

	create_directory(fs, blkno, &tmp_blkno);

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_read_inode(fs, tmp_blkno, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	di = (struct ocfs2_dinode *)buf;

	if (!(di->i_flags & OCFS2_VALID_FL))
		FSWRK_FATAL("not a valid file");

	if (di->i_dyn_features & OCFS2_INLINE_DATA_FL) {

		FSWRK_FATAL("Inlined directory");

	} else {

		el = &(di->id2.i_list);
		if (el->l_next_free_rec == 0)
			FSWRK_FATAL("directory empty");

		el->l_next_free_rec = 0;
	}

	fprintf(stdout, "DIR_ZERO: "
		"Corrupt directory#%"PRIu64", empty its content.\n", 
		tmp_blkno);

	ret = ocfs2_write_inode(fs, tmp_blkno, buf);
	if (ret) 
		FSWRK_COM_FATAL(progname, ret);	

	if (buf)
		ocfs2_free(&buf);
	return;
}

void mess_up_dir_not_connected(ocfs2_filesys *fs, enum fsck_type type,
			       uint64_t blkno)
{
	errcode_t ret;
	uint64_t tmp_blkno;

	ret  = ocfs2_new_inode(fs, &tmp_blkno, S_IFDIR | 0755);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_init_dir(fs, tmp_blkno, blkno);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	fprintf(stdout, "DIR_NOT_CONNECTED: "
		"create a directory#%"PRIu64" which has no connections.\n",
		tmp_blkno);

	return;
}
