/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * refcount.h
 *
 * Copyright (C) 2009 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#ifndef _LIBOCFS2_REFCOUNT_H_
#define _LIBOCFS2_REFCOUNT_H_

typedef errcode_t (ocfs2_post_refcount_func)(ocfs2_filesys *fs,
					     void *para);

/*
 * Some refcount caller need to do more work after we modify the data b-tree
 * during refcount operation(including CoW and add refcount flag), and make the
 * transaction complete. So it must give us this structure so that we can do it
 * within our transaction.
 *
 */
struct ocfs2_post_refcount {
	ocfs2_post_refcount_func *func;	/* real function. */
	void *para;
};
#endif		/* _LIBOCFS2_REFCOUNT_H_ */
