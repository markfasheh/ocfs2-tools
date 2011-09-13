/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * blocktype.c
 *
 * Detect various metadata block types, etc.
 *
 * Copyright (C) 2011 Oracle.  All rights reserved.
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
 */

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE

#include <string.h>

#include "ocfs2/ocfs2.h"

struct block_types {
	enum ocfs2_block_type	bt_type;
	char 			bt_signature[8];
	int			bt_offset;
};

static struct block_types bts[] = {
	{
		OCFS2_BLOCK_INODE, OCFS2_INODE_SIGNATURE,
		offsetof(struct ocfs2_dinode, i_signature),
	},
	{
		OCFS2_BLOCK_SUPERBLOCK, OCFS2_SUPER_BLOCK_SIGNATURE,
		offsetof(struct ocfs2_dinode, i_signature),
	},
	{
		OCFS2_BLOCK_EXTENT_BLOCK, OCFS2_EXTENT_BLOCK_SIGNATURE,
		offsetof(struct ocfs2_extent_block, h_signature),
	},
	{
		OCFS2_BLOCK_GROUP_DESCRIPTOR, OCFS2_GROUP_DESC_SIGNATURE,
		offsetof(struct ocfs2_group_desc, bg_signature),
	},
	{
		OCFS2_BLOCK_DIR_BLOCK, OCFS2_DIR_TRAILER_SIGNATURE,
		offsetof(struct ocfs2_dir_block_trailer, db_signature),
	},
	{
		OCFS2_BLOCK_XATTR, OCFS2_XATTR_BLOCK_SIGNATURE,
		offsetof(struct ocfs2_xattr_block, xb_signature),
	},
	{
		OCFS2_BLOCK_REFCOUNT, OCFS2_REFCOUNT_BLOCK_SIGNATURE,
		offsetof(struct ocfs2_refcount_block, rf_signature),
	},
	{
		OCFS2_BLOCK_DXROOT, OCFS2_DX_ROOT_SIGNATURE,
		offsetof(struct ocfs2_dx_root_block, dr_signature),
	},
	{
		OCFS2_BLOCK_DXLEAF, OCFS2_DX_LEAF_SIGNATURE,
		offsetof(struct ocfs2_dx_leaf, dl_signature),
	},
};

enum ocfs2_block_type ocfs2_detect_block(char *buf)
{
	int i, numtypes = sizeof(bts)/sizeof(struct block_types);

	for (i = 0; i < numtypes; ++i) {
		if (!strncmp(buf + bts[i].bt_offset, bts[i].bt_signature,
			     strlen(bts[i].bt_signature)))
			return bts[i].bt_type;
	}

	return OCFS2_BLOCK_UNKNOWN;
}
