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

#ifndef __O2FSCK_REFCOUNT_H__
#define __O2FSCK_REFCOUNT_H__

#include "fsck.h"

errcode_t o2fsck_check_refcount_tree(o2fsck_state *ost,
				     struct ocfs2_dinode *di);
#endif /* __O2FSCK_REFCOUNT_H__ */

