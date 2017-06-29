/*
 * icount.h
 *
 * Copyright (C) 2002 Oracle Corporation.  All rights reserved.
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301 USA.
 *
 * Author: Zach Brown
 */

#ifndef __O2FSCK_ICOUNT_H__
#define __O2FSCK_ICOUNT_H__

#include "ocfs2/ocfs2.h"
#include "ocfs2/kernel-rbtree.h"

typedef struct _o2fsck_icount {
	ocfs2_bitmap	*ic_single_bm;
	struct rb_root	ic_multiple_tree;
} o2fsck_icount;

errcode_t o2fsck_icount_set(o2fsck_icount *icount, uint64_t blkno, 
			    uint16_t count);
uint16_t o2fsck_icount_get(o2fsck_icount *icount, uint64_t blkno);
errcode_t o2fsck_icount_new(ocfs2_filesys *fs, o2fsck_icount **ret);
void o2fsck_icount_free(o2fsck_icount *icount);
void o2fsck_icount_delta(o2fsck_icount *icount, uint64_t blkno, 
			 int delta);
errcode_t o2fsck_icount_next_blkno(o2fsck_icount *icount, uint64_t start,
				   uint64_t *found);

#endif /* __O2FSCK_ICOUNT_H__ */

