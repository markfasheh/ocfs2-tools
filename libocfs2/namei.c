/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * namei.c
 *
 * ocfs2 directory lookup operations
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301 USA.
 *
 * This code is a port of e2fsprogs/lib/ext2fs/namei.c
 * Copyright (C) 1993, 1994, 1994, 1995 Theodore Ts'o.
 */

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE

#include <stdio.h>
#include <string.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "ocfs2/ocfs2.h"

static errcode_t open_namei(ocfs2_filesys *fs, uint64_t root, uint64_t base,
			    const char *pathname, size_t pathlen, int follow,
			    int link_count, char *buf, uint64_t *res_inode);

/*
 * follow_link()
 *
 */
static errcode_t follow_link(ocfs2_filesys *fs, uint64_t root, uint64_t dir,
			     uint64_t inode, int link_count,
			     char *buf, uint64_t *res_inode)
{
	char *pathname;
	char *buffer = NULL;
	errcode_t ret;
	struct ocfs2_dinode *di = NULL;
	struct ocfs2_extent_list *el;
	uint64_t blkno;

#ifdef NAMEI_DEBUG
	printf("follow_link: root=%lu, dir=%lu, inode=%lu, lc=%d\n",
	       root, dir, inode, link_count);
	
#endif

	ret = ocfs2_malloc_block(fs->fs_io, &di);
	if (ret)
		goto bail;

	ret = ocfs2_read_inode(fs, inode, (char *)di);
	if (ret)
		goto bail;

	if (!S_ISLNK(di->i_mode)) {
		*res_inode = inode;
		ret = 0;
		goto bail;
	}

	if (link_count++ > 5) {
		ret = OCFS2_ET_SYMLINK_LOOP;
		goto bail;
	}

	el = &(di->id2.i_list);

	if (!di->i_clusters || !el->l_next_free_rec) {
		ret = OCFS2_ET_INTERNAL_FAILURE;
		goto bail;
	}

	blkno = el->l_recs[0].e_blkno;

	ret = ocfs2_malloc_block(fs->fs_io, &buffer);
	if (ret)
		goto bail;

	ret = ocfs2_read_blocks(fs, blkno, 1, buffer);
	if (ret)
		goto bail;

	pathname = buffer;

	ret = open_namei(fs, root, dir, pathname, di->i_size, 1,
			 link_count, buf, res_inode);

bail:
	if (buffer)
		ocfs2_free(&buffer);

	if (di)
		ocfs2_free(&di);

	return ret;
}

/*
 * dir_namei()
 *
 * This routine interprets a pathname in the context of the current
 * directory and the root directory, and returns the inode of the
 * containing directory, and a pointer to the filename of the file
 * (pointing into the pathname) and the length of the filename.
 */
static errcode_t dir_namei(ocfs2_filesys *fs, uint64_t root, uint64_t dir,
			   const char *pathname, int pathlen,
			   int link_count, char *buf,
			   const char **name, int *namelen,
			   uint64_t *res_inode)
{
	char c;
	const char *thisname;
	int len;
	uint64_t inode;
	errcode_t ret;

	if ((c = *pathname) == '/') {
        	dir = root;
		pathname++;
		pathlen--;
	}
	while (1) {
        	thisname = pathname;
		for (len=0; --pathlen >= 0;len++) {
			c = *(pathname++);
			if (c == '/')
				break;
		}

		if (pathlen < 0)
			break;

		ret = ocfs2_lookup (fs, dir, thisname, len, buf, &inode);
		if (ret)
			return ret;

        	ret = follow_link (fs, root, dir, inode, link_count, buf, &dir);
        	if (ret)
			return ret;
    	}

	*name = thisname;
	*namelen = len;
	*res_inode = dir;
	return 0;
}

/*
 * open_namei()
 *
 */
static errcode_t open_namei(ocfs2_filesys *fs, uint64_t root, uint64_t base,
			    const char *pathname, size_t pathlen, int follow,
			    int link_count, char *buf, uint64_t *res_inode)
{
	const char *basename;
	int namelen;
	uint64_t dir, inode;
	errcode_t ret;

#ifdef NAMEI_DEBUG
	printf("open_namei: root=%lu, dir=%lu, path=%*s, lc=%d\n",
	       root, base, pathlen, pathname, link_count);
#endif
	ret = dir_namei(fs, root, base, pathname, pathlen,
			   link_count, buf, &basename, &namelen, &dir);
	if (ret)
		return ret;

	if (!namelen) {                     /* special case: '/usr/' etc */
		*res_inode=dir;
		return 0;
	}

	ret = ocfs2_lookup (fs, dir, basename, namelen, buf, &inode);
	if (ret)
		return ret;

	if (follow) {
		ret = follow_link(fs, root, dir, inode, link_count, buf, &inode);
		if (ret)
			return ret;
	}
#ifdef NAMEI_DEBUG
	printf("open_namei: (link_count=%d) returns %lu\n",
	       link_count, inode);
#endif
	*res_inode = inode;
	return 0;
}

/*
 * ocfs2_namei()
 *
 */
errcode_t ocfs2_namei(ocfs2_filesys *fs, uint64_t root, uint64_t cwd,
		      const char *name, uint64_t *inode)
{
	char *buf;
	errcode_t ret;
	
	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = open_namei(fs, root, cwd, name, strlen(name), 0, 0, buf, inode);

	ocfs2_free(&buf);
	return ret;
}

/*
 * ocfs2_namei_follow()
 *
 */
errcode_t ocfs2_namei_follow(ocfs2_filesys *fs, uint64_t root, uint64_t cwd,
			     const char *name, uint64_t *inode)
{
	char *buf;
	errcode_t ret;
	
	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;
	
	ret = open_namei(fs, root, cwd, name, strlen(name), 1, 0, buf, inode);

	ocfs2_free(&buf);
	return ret;
}

/*
 * ocfs2_follow_link()
 *
 */
errcode_t ocfs2_follow_link(ocfs2_filesys *fs, uint64_t root, uint64_t cwd,
			    uint64_t inode, uint64_t *res_inode)
{
	char *buf;
	errcode_t ret;
	
	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = follow_link(fs, root, cwd, inode, 0, buf, res_inode);

	ocfs2_free(&buf);
	return ret;
}
