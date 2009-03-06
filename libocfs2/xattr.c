/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * xattr.c
 *
 * Copyright (C) 2004, 2008 Oracle.  All rights reserved.
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
#include <string.h>
#include <inttypes.h>

#include "ocfs2/byteorder.h"
#include "ocfs2/ocfs2.h"

uint32_t ocfs2_xattr_uuid_hash(unsigned char *uuid)
{
	uint32_t i, hash = 0;

	for (i = 0; i < OCFS2_VOL_UUID_LEN; i++) {
		hash = (hash << OCFS2_HASH_SHIFT) ^
			(hash >> (8*sizeof(hash) - OCFS2_HASH_SHIFT)) ^
			*uuid++;
	}
	return hash;
}

