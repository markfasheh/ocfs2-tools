/*
 * refcount.h
 *
 * Function prototypes, macros, etc. for related 'C' files
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

#ifndef _FSWRECK_REFCOUNT_H_
#define _FSWRECK_REFCOUNT_H_

void mess_up_refcount_tree_block(ocfs2_filesys *fs, enum fsck_type type,
				 uint64_t blkno);
#endif		/* _FSWRECK_REFCOUNT_H_ */
