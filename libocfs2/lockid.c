/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * lockid.c
 *
 * Encode and decode lockres name
 *
 * Copyright (C) 2004, 2008 Oracle.  All rights reserved.
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
 */

#include "ocfs2/byteorder.h"
#include "ocfs2/ocfs2.h"

#include <string.h>
#include <inttypes.h>

enum ocfs2_lock_type ocfs2_get_lock_type(char c)
{
	switch (c) {
	case 'M':
		return OCFS2_LOCK_TYPE_META;
	case 'D':
		return OCFS2_LOCK_TYPE_DATA;
	case 'S':
		return OCFS2_LOCK_TYPE_SUPER;
	case 'R':
		return OCFS2_LOCK_TYPE_RENAME;
	case 'W':
		return OCFS2_LOCK_TYPE_RW;
	case 'N':
		return OCFS2_LOCK_TYPE_DENTRY;
	case 'O':
		return OCFS2_LOCK_TYPE_OPEN;
	case 'F':
		return OCFS2_LOCK_TYPE_FLOCK;
	default:
		return OCFS2_NUM_LOCK_TYPES;
	}
}

/*
 * This function encodes the lockname just like the filesystem. Meaning
 * the dentry lock gets encoded in its binary form.
 */
errcode_t ocfs2_encode_lockres(enum ocfs2_lock_type type, uint64_t blkno,
			       uint32_t generation, uint64_t parent,
			       char *lockres)
{
	uint64_t b;

	if (type >= OCFS2_NUM_LOCK_TYPES)
		return OCFS2_ET_INVALID_LOCKRES;

	blkno = (type == OCFS2_LOCK_TYPE_RENAME) ? 0 : blkno;
	generation = ((type == OCFS2_LOCK_TYPE_SUPER) ||
		      (type == OCFS2_LOCK_TYPE_RENAME)) ? 0 : generation;

	if (type != OCFS2_LOCK_TYPE_DENTRY) {
		snprintf(lockres, OCFS2_LOCK_ID_MAX_LEN, "%c%s%016"PRIx64"%08x",
			 ocfs2_lock_type_char(type), OCFS2_LOCK_ID_PAD,
			 blkno, generation);
	} else {
		snprintf(lockres, OCFS2_DENTRY_LOCK_INO_START, "%c%016llx",
			 ocfs2_lock_type_char(OCFS2_LOCK_TYPE_DENTRY),
			 (long long)parent);
		b = bswap_64(blkno);
		memcpy(&lockres[OCFS2_DENTRY_LOCK_INO_START], &b, sizeof(b));
	}

	return 0;
}

errcode_t ocfs2_decode_lockres(char *lockres, enum ocfs2_lock_type *type,
			       uint64_t *blkno, uint32_t *generation,
			       uint64_t *parent)
{
	int i = 0;
	enum ocfs2_lock_type t;
	char *l = lockres;
	uint64_t b = 0;
	uint64_t p = 0;
	uint32_t g = 0;
	
	if ((t = ocfs2_get_lock_type(*l)) >= OCFS2_NUM_LOCK_TYPES)
		return OCFS2_ET_INVALID_LOCKRES;

	if (t != OCFS2_LOCK_TYPE_DENTRY) {
		i = sscanf(l + 1, OCFS2_LOCK_ID_PAD"%016llx%08x", &b, &g);
		if (i != 2)
			return OCFS2_ET_INVALID_LOCKRES;
	} else {
		i = sscanf(l + 1, "%016llx", &p);
		if (i != 1)
			return OCFS2_ET_INVALID_LOCKRES;
		b = strtoull(&l[OCFS2_DENTRY_LOCK_INO_START], NULL, 16);
	}

	if (type)
		*type = t;

	if (blkno)
		*blkno = b;

	if (generation)
		*generation = g;

	if (parent)
		*parent = p;

	return 0;
}

/*
 * This function is useful when printing the dentry lock. It converts the
 * dentry lockname into a string using the same scheme as used in dlmglue.
 */
errcode_t ocfs2_printable_lockres(char *lockres, char *name, int len)
{
	uint64_t b;

	memset(name, 0, len);

	if (ocfs2_get_lock_type(*lockres) >= OCFS2_NUM_LOCK_TYPES)
		return OCFS2_ET_INVALID_LOCKRES;

	if (ocfs2_get_lock_type(*lockres) == OCFS2_LOCK_TYPE_DENTRY) {
		memcpy((uint64_t *)&b,
		       (char *)&lockres[OCFS2_DENTRY_LOCK_INO_START],
		       sizeof(uint64_t));
		snprintf(name, len, "%.*s%08x", OCFS2_DENTRY_LOCK_INO_START - 1,
			 lockres, (unsigned int)bswap_64(b));
	} else
		snprintf(name, len, "%s", lockres);

	return 0;
}
