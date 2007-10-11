/*
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

#include <main.h>

extern char *progname;

static void create_directory(ocfs2_filesys *fs,
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

	ret = ocfs2_expand_dir(fs, *blkno, parentblk);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_link(fs, parentblk, random_name, *blkno, OCFS2_FT_DIR);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	return;
}

/* Add an entry in the directory.
 * Currently this is only a simple function, and we don't consider
 * the situation that this block is not enough for the entry.
 * The mechanism is copied from ocfs2 kernel code.
 */
static void add_dir_ent(ocfs2_filesys *fs,
			struct ocfs2_dir_entry *de,
			uint64_t blkno,	char *name, int namelen, umode_t mode,
			struct ocfs2_dir_entry **retent)
{
	struct ocfs2_dir_entry *de1 = NULL;
	uint32_t offset = 0;
	uint16_t rec_len = OCFS2_DIR_REC_LEN(namelen);

	while (1) {
		if ((de->inode == 0 && de->rec_len >= rec_len) ||
		    (de->rec_len >= 
			(OCFS2_DIR_REC_LEN(de->name_len) + rec_len))) {
			offset += de->rec_len;
			if (de->inode) {
			de1 = (struct ocfs2_dir_entry *)((char *) de +
				OCFS2_DIR_REC_LEN(de->name_len));
			de1->rec_len = de->rec_len -
				OCFS2_DIR_REC_LEN(de->name_len);
			de->rec_len = OCFS2_DIR_REC_LEN(de->name_len);
			de = de1;
			}
			de->file_type = OCFS2_FT_UNKNOWN;
			de->inode = blkno;
			ocfs2_set_de_type(de, mode);
			de->name_len = namelen;
			memcpy(de->name, name, namelen);
			break;
		}
		offset += de->rec_len;
		de = (struct ocfs2_dir_entry *) ((char *) de + de->rec_len);
		if (offset >= fs->fs_blocksize)
			FSWRK_FATAL("no space for the new dirent");
	}

	*retent = de;
	return;
}

static void damage_dir_content(ocfs2_filesys *fs, uint64_t dir,
				enum fsck_type type)
{
	errcode_t ret;
	char *buf = NULL;
	uint64_t blkno, tmp_blkno;
	uint64_t contig;
	ocfs2_cached_inode *cinode = NULL;
	struct ocfs2_dir_entry *de = NULL, *newent = NULL;
	char name[OCFS2_MAX_FILENAME_LEN];
	int namelen;
	mode_t mode;

	ret = ocfs2_read_cached_inode(fs, dir, &cinode);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	/* get first blockno */
	ret = ocfs2_extent_map_get_blocks(cinode, 0, 1, &blkno, &contig, NULL);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = io_read_block(fs->fs_io, blkno, 1, buf);	
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	de = (struct ocfs2_dir_entry *)buf;

	sprintf(name, "test");
	namelen = strlen(name);

	switch (type) {
	case DIRENT_DOTTY_DUP:
		/* add another "." at the end of the directory */
		sprintf(name, ".");
		namelen = strlen(name);
		add_dir_ent(fs, de,
			 blkno, name, namelen, cinode->ci_inode->i_mode,
			 &newent);
		fprintf(stdout, "DIRENT_DOTTY_DUP: "
			"Corrupt directory#%"PRIu64
			", add another '.' to it.\n", dir);
		break;
	case DIRENT_NOT_DOTTY:
		/* rename the first ent from "." to "a". */
		sprintf(name, "a");
		namelen = strlen(name);
		memcpy(de->name, name, namelen);
		fprintf(stdout, "DIRENT_NOT_DOTTY: "
			"Corrupt directory#%"PRIu64
			", change '.' to %s.\n", dir, name);
		break;
	case DIRENT_DOT_INODE:
		fprintf(stdout, "DIRENT_DOT_INODE: "
			"Corrupt directory#%"PRIu64
			", change dot inode to #%"PRIu64".\n", dir, (dir+10));
		de->inode += 10;
		break;
	case DIRENT_DOT_EXCESS:
		fprintf(stdout, "DIR_DOT_EXCESS: "
			"Corrupt directory#%"PRIu64","
			"change dot's dirent length from %u to %u\n",
			dir, de->rec_len, (de->rec_len + OCFS2_DIR_PAD));
		de->rec_len += OCFS2_DIR_PAD;
		break;
	case DIRENT_ZERO:
		name[0] = '\0';
		namelen = strlen(name);
		add_dir_ent(fs, de,
			 dir + 100, name, namelen, S_IFREG | 0755,
			 &newent);
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
		add_dir_ent(fs, de,
			 tmp_blkno, name, namelen, mode,
			 &newent);
		fprintf(stdout, "DIRENT_NAME_CHARS: "
			"Corrupt directory#%"PRIu64
			", add an invalid entry to it.\n", dir);
		break;
	case DIRENT_INODE_RANGE:
		tmp_blkno =  fs->fs_blocks + 1;
		add_dir_ent(fs, de,
			 tmp_blkno, name, namelen, S_IFREG | 0755,
			 &newent);
		fprintf(stdout, "DIRENT_INODE_RANGE: "
			"Corrupt directory#%"PRIu64
			", add an entry whose inode exceeds"
			" the limits.\n", dir);
		break;
	case DIRENT_INODE_FREE:
		mode = S_IFREG | 0755;
		tmp_blkno = dir + 1000;
		add_dir_ent(fs, de,
			 tmp_blkno, name, namelen, mode,
			 &newent);
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
		add_dir_ent(fs, de,
			 tmp_blkno, name, namelen,S_IFLNK | 0755,
			 &newent);
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
		add_dir_ent(fs, de,
			 tmp_blkno, name, namelen, mode,
			 &newent);
		add_dir_ent(fs, de,
			 tmp_blkno, name, namelen, mode,
			 &newent);
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
		add_dir_ent(fs, de,
			 tmp_blkno, name, namelen, mode,
			 &newent);
		fprintf(stdout, "DIRENT_LENGTH: "
			"Corrupt directory#%"PRIu64
			", modify entry#%"PRIu64" from %u to %u.\n",
			dir, tmp_blkno, newent->rec_len, (newent->rec_len+1));
		newent->rec_len += 1;
		break;
	default:
		FSWRK_FATAL("Invalid type = %d\n", type);	
	}

	ret = io_write_block(fs->fs_io, blkno, 1, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	if (buf)
		ocfs2_free(&buf);
	if (cinode)
		ocfs2_free_cached_inode(fs, cinode);
	return;
}

void mess_up_dir_dot(ocfs2_filesys *fs, uint64_t blkno)
{
	int i;
	uint64_t tmp_blkno;
	enum fsck_type types[] ={ DIRENT_DOTTY_DUP, DIRENT_NOT_DOTTY,
				  DIRENT_DOT_INODE, DIRENT_DOT_EXCESS };

	for (i = 0; i < ARRAY_ELEMENTS(types); i++) {
		create_directory(fs, blkno, &tmp_blkno);
		damage_dir_content(fs, tmp_blkno, types[i]);
	}
	return;
}

void mess_up_dir_ent(ocfs2_filesys *fs, uint64_t blkno)
{
	int i;
	uint64_t tmp_blkno;
	enum fsck_type types[] = { DIRENT_ZERO, DIRENT_NAME_CHARS,
				   DIRENT_INODE_RANGE, DIRENT_INODE_FREE,
				   DIRENT_TYPE,	DIRENT_DUPLICATE,
				   DIRENT_LENGTH };

	for (i = 0; i < ARRAY_ELEMENTS(types); i++) {
		create_directory(fs, blkno, &tmp_blkno);
		damage_dir_content(fs, tmp_blkno, types[i]);
	}
	return;
}

void mess_up_dir_parent_dup(ocfs2_filesys *fs, uint64_t blkno)
{
	errcode_t ret;
	uint64_t contig;
	uint64_t parent1, parent2, tmp_blkno,extblk;
	char *buf = NULL;
	struct ocfs2_dir_entry *de = NULL, *newent = NULL;
	ocfs2_cached_inode *cinode = NULL;
	char name[OCFS2_MAX_FILENAME_LEN];
	int namelen;
	mode_t mode;

	/* create 2 direcotories */
	create_directory(fs, blkno, &parent1);
	create_directory(fs, blkno, &parent2);

	/* create a directory under parent1, tmp_blkno indicates its inode. */
	create_directory(fs, parent1, &tmp_blkno);

	/* Now we will create another dirent under parent2 which
	 * which also points to tmp_blkno. So tmp_blkno will have two
	 * parents: parent1 and parent2.
	 */
	ret = ocfs2_read_cached_inode(fs, parent2, &cinode);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_extent_map_get_blocks(cinode, 0, 1, &extblk, &contig, NULL);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = io_read_block(fs->fs_io, extblk, 1, buf);	
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	de = (struct ocfs2_dir_entry *)buf;
	sprintf(name, "test");
	namelen = strlen(name);
	mode = S_IFDIR | 0755;

	add_dir_ent(fs, de,
		 tmp_blkno, name, namelen, mode,
		 &newent);
	fprintf(stdout, "DIR_PARENT_DUP: "
		"Create a directory #%"PRIu64
		" which has two parents: #%"PRIu64" and #%"PRIu64".\n",
		tmp_blkno, parent1, parent2);

	ret = io_write_block(fs->fs_io, extblk, 1, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	if (buf)
		ocfs2_free(&buf);
	if (cinode)
		ocfs2_free_cached_inode(fs, cinode);
	return;
}

void mess_up_dir_inode(ocfs2_filesys *fs, uint64_t blkno)
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

	el = &(di->id2.i_list);
	if (el->l_next_free_rec == 0)
		FSWRK_FATAL("directory empty");

	el->l_next_free_rec = 0;
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

void mess_up_dir_not_connected(ocfs2_filesys *fs, uint64_t blkno)
{
	errcode_t ret;
	uint64_t tmp_blkno;

	ret  = ocfs2_new_inode(fs, &tmp_blkno, S_IFDIR | 0755);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_expand_dir(fs, tmp_blkno, blkno);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	fprintf(stdout, "DIR_NOT_CONNECTED: "
		"create a directory#%"PRIu64" which has no connections.\n",
		tmp_blkno);

	return;
}
