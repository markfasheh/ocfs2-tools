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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 * Author: Zach Brown
 */

#ifndef __O2FSCK_ICOUNT_H__
#define __O2FSCK_ICOUNT_H__

#include "ocfs2.h"

typedef struct _o2fsck_icount {
	ocfs2_bitmap	*ic_single_bm;
	struct rb_root	ic_multiple_tree;
} o2fsck_icount;

void o2fsck_icount_update(o2fsck_icount *icount, uint64_t blkno, 
				uint16_t count);
errcode_t o2fsck_icount_new(ocfs2_filesys *fs, o2fsck_icount **ret);
void o2fsck_icount_free(o2fsck_icount *icount);

#endif /* __O2FSCK_ICOUNT_H__ */

