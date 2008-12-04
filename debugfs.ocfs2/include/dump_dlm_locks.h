/*
 * dump_dlm_locks.h
 *
 * Function prototypes, macros, etc. for related 'C' files
 *
 * Copyright (C) 2008 Oracle.  All rights reserved.
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
 */

#ifndef _DUMP_DLM_LOCKS_H_
#define _DUMP_DLM_LOCKS_H_

#define DLM_LOCK_RES_UNINITED             0x00000001
#define DLM_LOCK_RES_RECOVERING           0x00000002
#define DLM_LOCK_RES_READY                0x00000004
#define DLM_LOCK_RES_DIRTY                0x00000008
#define DLM_LOCK_RES_IN_PROGRESS          0x00000010
#define DLM_LOCK_RES_MIGRATING            0x00000020
#define DLM_LOCK_RES_DROPPING_REF         0x00000040
#define DLM_LOCK_RES_BLOCK_DIRTY          0x00001000
#define DLM_LOCK_RES_SETREF_INPROG        0x00002000

#define GRANTED		0
#define CONVERTING	1
#define BLOCKED		2

struct lockres {
	__u8 owner;
	__u16 state;
	__u32 last_used;
	__u32 inflight_locks;
	__u32 asts_reserved;
	__u32 refs;
	__u8 purge;
	__u8 dirty;
	__u8 recovering;
	__u8 migration_pending;
	char *refmap;
	char *lvb;
	struct list_head granted;
	struct list_head converting;
	struct list_head blocked;
};

struct lock {
	__s8 type;
	__s8 convert_type;
	__u8 node;
	__u8 ast_list;
	__u8 bast_list;
	__u8 ast_pending;
	__u8 bast_pending;
	__u8 convert_pending;
	__u8 lock_pending;
	__u8 cancel_pending;
	__u8 unlock_pending;
	__u32 refs;
	char cookie[32];
	struct list_head list;
};

void dump_dlm_locks(char *uuid, FILE *out, char *path, int dump_lvbs,
		    struct list_head *locklist);

#endif		/* _DUMP_DLM_LOCKS_H_ */
