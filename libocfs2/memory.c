/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * memory.c
 *
 * Memory routines for the OCFS2 userspace library.
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
 * Portions of this code from e2fsprogs/lib/ext2fs/ext2fs.h
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001,
 *  	2002 by Theodore Ts'o.
 */

#define _XOPEN_SOURCE 600  /* Triggers XOPEN2K in features.h */
#define _LARGEFILE64_SOURCE

#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "ocfs2/ocfs2.h"

errcode_t ocfs2_malloc(unsigned long size, void *ptr)
{
    void **pp = (void **)ptr;

    *pp = malloc(size);
    if (!*pp)
        return OCFS2_ET_NO_MEMORY;
    return 0;
}

errcode_t ocfs2_malloc0(unsigned long size, void *ptr)
{
	errcode_t ret;
	void **pp = (void **)ptr;

	ret = ocfs2_malloc(size, ptr);
	if (ret)
		return ret;

	memset(*pp, 0, size);
	return 0;
}

errcode_t ocfs2_free(void *ptr)
{
	void **pp = (void **)ptr;

	free(*pp);
	*pp = NULL;
	return 0;
}

errcode_t ocfs2_realloc(unsigned long size, void *ptr)
{
	void *p;
	void **pp = (void **)ptr;

	p = realloc(*pp, size);
	if (!p)
		return OCFS2_ET_NO_MEMORY;
	*pp = p;
	return 0;
}

errcode_t ocfs2_realloc0(unsigned long size, void *ptr,
			 unsigned long old_size)
{
	errcode_t ret;
	char *p;
	void **pp = (void **)ptr;

	ret = ocfs2_realloc(size, ptr);
	if (ret)
		return ret;

	if (size > old_size) {
		p = (char *)(*pp);
		memset(p + old_size, 0, size - old_size);
	}
	return 0;
}

errcode_t ocfs2_malloc_blocks(io_channel *channel, int num_blocks,
			      void *ptr)
{
	errcode_t ret;
	int blksize;
	size_t bytes;
	void **pp = (void **)ptr;
	void *tmp;

	blksize = io_get_blksize(channel);
	if (((unsigned long long)num_blocks * blksize) > SIZE_MAX)
		return OCFS2_ET_NO_MEMORY;
	bytes = num_blocks * blksize;

	/*
	 * Older glibcs abort when they can't memalign() something.
	 * Ugh!  Check with malloc() first.
	 */
	tmp = malloc(bytes);
	if (!tmp)
		return OCFS2_ET_NO_MEMORY;
	free(tmp);

	ret = posix_memalign(pp, blksize, bytes);
	if (!ret)
		return 0;
	if (errno == ENOMEM)
		return OCFS2_ET_NO_MEMORY;
	/* blksize better be valid */
	abort();
}

errcode_t ocfs2_malloc_block(io_channel *channel, void *ptr)
{
	return ocfs2_malloc_blocks(channel, 1, ptr);
}
