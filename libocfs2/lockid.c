/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * lockid.c
 *
 * Encode and decode lockres name
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
 */

#include "ocfs2.h"

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
	default:
		return OCFS2_NUM_LOCK_TYPES;
	}
}

char *ocfs2_get_lock_type_string(enum ocfs2_lock_type type)
{
	switch (type) {
	case OCFS2_LOCK_TYPE_META:
		return "Metadata";
	case OCFS2_LOCK_TYPE_DATA:
		return "Data";
	case OCFS2_LOCK_TYPE_SUPER:
		return "Superblock";
	case OCFS2_LOCK_TYPE_RENAME:
		return "Rename";
	case OCFS2_LOCK_TYPE_RW:
		return "Write/Read";
	default:
		return NULL;
	}
}

errcode_t ocfs2_encode_lockres(enum ocfs2_lock_type type, uint64_t blkno,
			       uint32_t generation, char *lockres)
{
	if (type >= OCFS2_NUM_LOCK_TYPES)
		return OCFS2_ET_INVALID_LOCKRES;

	blkno = (type == OCFS2_LOCK_TYPE_RENAME) ? 0 : blkno;
	generation = ((type == OCFS2_LOCK_TYPE_SUPER) ||
		      (type == OCFS2_LOCK_TYPE_RENAME)) ? 0 : generation;

	snprintf(lockres, OCFS2_LOCK_ID_MAX_LEN, "%c%s%016"PRIx64"%08x",
		 ocfs2_lock_type_char(type), OCFS2_LOCK_ID_PAD,
		 blkno, generation);

	return 0;
}

errcode_t ocfs2_decode_lockres(char *lockres, int len, enum ocfs2_lock_type *type,
			       uint64_t *blkno, uint32_t *generation)
{
	char *lock = NULL;
	errcode_t ret = OCFS2_ET_NO_MEMORY;
	char blkstr[20];
	int i = 0;
	
	if (len != -1) {
		lock = calloc(len+1, 1);
		if (!lock)
			goto bail;
		strncpy(lock, lockres, len);
	} else
		lock = lockres;

	ret = OCFS2_ET_INVALID_LOCKRES;
	
	if ((strlen(lock) + 1) != OCFS2_LOCK_ID_MAX_LEN)
		goto bail;

	if (ocfs2_get_lock_type(lock[0]) >= OCFS2_NUM_LOCK_TYPES)
		goto bail;

	if (type)
		*type = ocfs2_get_lock_type(lock[i]);

	i = 1 + strlen(OCFS2_LOCK_ID_PAD);
	memset(blkstr, 0, sizeof(blkstr));
	memcpy(blkstr, &lock[i], 16);
	if (blkno)
		*blkno = strtoull(blkstr, NULL, 16);

	i += 16;
	if (generation)
		*generation = strtoul(&lock[i], NULL, 16);

	ret = 0;
bail:
	if (len != -1 && lock)
		free(lock);
	return ret;
}
