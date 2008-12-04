/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dump_dlm_locks.c
 *
 * Interface with the kernel and dump current dlm locking state
 *
 * Copyright (C) 2008 Oracle.  All rights reserved.
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
 *
 */

#define _XOPEN_SOURCE 600  /* Triggers XOPEN2K in features.h */
#define _LARGEFILE64_SOURCE

#include "main.h"
#include "ocfs2/byteorder.h"
#include "ocfs2_internals.h"

static void dump_raw_lvb(const char *lvb, FILE *out)
{
	int i, j;

	fprintf(out, "Raw LVB:\t");

	for (i = 0, j = 0; i < DLM_LVB_LEN; i++, j += 2) {
		fprintf(out, "%c%c ", lvb[j], lvb[j+1]);
		if (!((i+1) % 16) && i != (DLM_LVB_LEN-1))
			fprintf(out, "\n\t\t");
	}
	fprintf(out, "\n");
}

static void get_lock_level(int level, char *str, int len)
{
	switch (level) {
	case 0:
		strncpy(str, "NL", len);
		break;
	case 3:
		strncpy(str, "PR", len);
		break;
	case 5:
		strncpy(str, "EX", len);
		break;
	default:
		snprintf(str, len, "%d", level);
		break;
	}
}

static void dump_lock(struct lock *lock, char *queue, FILE *out)
{
	GString *action = NULL;
	char *ast, *bast, level[5], conv[5];

	action = g_string_new(NULL);

	get_lock_level(lock->type, level, sizeof(level));
	get_lock_level(lock->convert_type, conv, sizeof(conv));

	ast = (lock->ast_list) ? "Yes" : "No";
	bast = (lock->bast_list) ? "Yes" : "No";

	if (lock->ast_pending)
		g_string_append(action, "Ast ");
	if (lock->bast_pending)
		g_string_append(action, "Bast ");
	if (lock->convert_pending)
		g_string_append(action, "Convert ");
	if (lock->lock_pending)
		g_string_append(action, "Lock ");
	if (lock->cancel_pending)
		g_string_append(action, "Cancel ");
	if (lock->unlock_pending)
		g_string_append(action, "Unlock ");
	if (!action->len)
		g_string_append(action, "None");

	fprintf(out, " %-10s  %-4d  %-5s  %-4s  %-15s  %-4d  %-3s  %-4s  %s\n",
		queue, lock->node, level, conv, lock->cookie,
		lock->refs, ast, bast, action->str);

	g_string_free(action, 1);
}

static void get_lockres_state(__u16 state, GString *str)
{
	if (state & DLM_LOCK_RES_UNINITED)
		g_string_append(str, "Uninitialized ");
	if (state & DLM_LOCK_RES_RECOVERING)
		g_string_append(str, "Recovering ");
	if (state & DLM_LOCK_RES_READY)
		g_string_append(str, "Ready ");
	if (state & DLM_LOCK_RES_DIRTY)
		g_string_append(str, "Dirty ");
	if (state & DLM_LOCK_RES_IN_PROGRESS)
		g_string_append(str, "InProgress ");
	if (state & DLM_LOCK_RES_MIGRATING)
		g_string_append(str, "Migrating ");
	if (state & DLM_LOCK_RES_DROPPING_REF)
		g_string_append(str, "DroppingRef ");
	if (state & DLM_LOCK_RES_BLOCK_DIRTY)
		g_string_append(str, "BlockDirty ");
	if (state & DLM_LOCK_RES_SETREF_INPROG)
		g_string_append(str, "SetRefInProg ");
	if (!str->len)
		g_string_append(str, "");
}

static void dump_lockres(char *name, struct lockres *res, FILE *out)
{
	struct lock *lock;
	struct list_head *iter;
	GString *lists = NULL;
	GString *state = NULL;
	int numlocks = 0;

	state = g_string_new(NULL);
	lists = g_string_new(NULL);

	if (res->purge)
		g_string_append(lists, "Purge ");
	if (res->dirty)
		g_string_append(lists, "Dirty ");
	if (res->recovering)
		g_string_append(lists, "Recovering ");
	if (!lists->len)
		g_string_append(lists, "None");

	get_lockres_state(res->state, state);

	if (!list_empty(&res->granted))
		list_for_each(iter, &res->granted)
			++numlocks;

	if (!list_empty(&res->converting))
		list_for_each(iter, &res->converting)
			++numlocks;

	if (!list_empty(&res->blocked))
		list_for_each(iter, &res->blocked)
			++numlocks;

	/* Lockres: xx  Owner: xx  State: 0x1 xxx */
	fprintf(out, "Lockres: %-32s  Owner: %-3d  State: 0x%X %s\n",
		name, res->owner, res->state, state->str);

	/* Last Used: x  ASTs Reserved: x  Inflight: x  Migration Pending: x */
	fprintf(out, "Last Used: %-5d  ASTs Reserved: %-3d  Inflight: %-3d  "
		"Migration Pending: %s\n", res->last_used, res->asts_reserved,
		res->inflight_locks, (res->migration_pending ? "Yes" : "No"));

	/* Refs: xx  Locks: xx  On Lists: xx */
	fprintf(out, "Refs: %-3d  Locks: %-3d  On Lists: %s\n", res->refs,
		numlocks, lists->str);

	/* Reference Map: xx, xx, xx, xx, xx */
	fprintf(out, "Reference Map: ");
	if (res->refmap)
		fprintf(out, "%s", res->refmap);
	fprintf(out, "\n");

	/* Raw LVB: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 */
	if (res->lvb)
		dump_raw_lvb(res->lvb, out);

	/* Lock-Queue  Node  Level  Conv  Cookie  Refs  AST  BAST  Pendi... */
	fprintf(out, " %-10s  %-4s  %-5s  %-4s  %-15s  %-4s  %-3s  %-4s  %s\n",
		"Lock-Queue", "Node", "Level", "Conv", "Cookie", "Refs", "AST",
		"BAST", "Pending-Action");

	/* Granted Queue: */
	if (!list_empty(&res->granted)) {
		list_for_each(iter, &res->granted) {
			lock = list_entry(iter, struct lock, list);
			dump_lock(lock, "Granted", out);
		}
	}

	/* Converting Queue: */
	if (!list_empty(&res->converting)) {
		list_for_each(iter, &res->converting) {
			lock = list_entry(iter, struct lock, list);
			dump_lock(lock, "Converting", out);
		}
	}

	/* Blocked Queue: */
	if (!list_empty(&res->blocked)) {
		list_for_each(iter, &res->blocked) {
			lock = list_entry(iter, struct lock, list);
			dump_lock(lock, "Blocked", out);
		}
	}

	fprintf(out, "\n");

	g_string_free(state, 1);
	g_string_free(lists, 1);
}

static int read_lvbx(char *line, struct lockres *res)
{
	res->lvb = strdup(line);
	return !!res->lvb;
}

static int read_rmap(char *line, struct lockres *res)
{
	int i;

	res->refmap = strdup(line);
	if (!res->refmap)
		return 0;

	i = strlen(res->refmap);
	if (i)
		res->refmap[i - 1] = '\0';
	return 1;
}

#define CURRENT_LOCK_PROTO	1
static int read_lock(char *line, struct lockres *res)
{
	char data[512];
	int version;
	struct lock *lock = NULL;
	__u8 queue;
	int ret;
	__u32 ck1, ck2;

	lock = calloc(1, sizeof(struct lock));
	if (!lock)
		goto bail;

	INIT_LIST_HEAD(&lock->list);

	/* read version */
	ret = sscanf(line, "%d,%s\n", &version, data);
	if (ret != 2)
		goto bail;

	if (version > CURRENT_LOCK_PROTO) {
		fprintf(stdout, "Lock string proto %u found, but %u is the "
			"highest I understand.\n", version, CURRENT_LOCK_PROTO);
		goto bail;
	}

	/* Version 1 */
	if (version == 1) {
		ret = sscanf(data, "%hhu,%hhd,%hhd,%hhu,%u:%u,%hhu,%hhu,%hhu,"
			     "%hhu,%hhu,%hhu,%hhu,%hhu,%u", &queue, &lock->type,
			     &lock->convert_type, &lock->node, &ck1, &ck2,
			     &lock->ast_list, &lock->bast_list,
			     &lock->ast_pending, &lock->bast_pending,
			     &lock->convert_pending, &lock->lock_pending,
			     &lock->cancel_pending, &lock->unlock_pending,
			     &lock->refs);
		if (ret != 15)
			goto bail;

		snprintf(lock->cookie, sizeof(lock->cookie) - 1, "%u:%u",
			 ck1, ck2);
	}

	switch (queue) {
	case GRANTED: list_add_tail(&lock->list, &res->granted);
		      break;
	case CONVERTING:
		      list_add_tail(&lock->list, &res->converting);
		      break;
	case BLOCKED:
		      list_add_tail(&lock->list, &res->blocked);
		      break;
	default:
		      free(lock);
		      return 0;
	}

	return 1;

bail:
	if (lock)
		free(lock);
	return 0;
}

#define CURRENT_LRES_PROTO	1
static int read_lres(char *line, struct lockres *res)
{
	char data[512];
	int version;
	int ret;

	/* read version */
	ret = sscanf(line, "%d,%s\n", &version, data);
	if (ret != 2)
		return 0;

	if (version > CURRENT_LRES_PROTO) {
		fprintf(stdout, "Lockres string proto %u found, but %u is the "
			"highest I understand.\n", version,
			CURRENT_LRES_PROTO);
		return 0;
	}

	/* Version 1 */
	if (version == 1) {
		ret = sscanf(data, "%hhu,%hu,%u,%hhu,%hhu,%hhu,%u,%hhu,%u,%u",
			     &res->owner, &res->state, &res->last_used,
			     &res->purge, &res->dirty, &res->recovering,
			     &res->inflight_locks, &res->migration_pending,
			     &res->asts_reserved, &res->refs);
		if (ret != 10)
			return 0;
	}

	return 1;
}

static void init_lockres(struct lockres *res)
{
	memset(res, 0, sizeof(struct lockres));
	INIT_LIST_HEAD(&res->granted);
	INIT_LIST_HEAD(&res->converting);
	INIT_LIST_HEAD(&res->blocked);
}

static void clean_lockres(struct lockres *res)
{
	struct list_head *iter, *iter2;
	struct lock *lock;

	if (res->lvb)
		free(res->lvb);
	if (res->refmap)
		free(res->refmap);

	if (!list_empty(&res->granted)) {
		list_for_each_safe(iter, iter2, &res->granted) {
			lock = list_entry(iter, struct lock, list);
			list_del(iter);
			free(lock);
		}
	}

	if (!list_empty(&res->converting)) {
		list_for_each_safe(iter, iter2, &res->converting) {
			lock = list_entry(iter, struct lock, list);
			list_del(iter);
			free(lock);
		}
	}

	if (!list_empty(&res->blocked)) {
		list_for_each_safe(iter, iter2, &res->blocked) {
			lock = list_entry(iter, struct lock, list);
			list_del(iter);
			free(lock);
		}
	}

	init_lockres(res);
}

static void read_lockres(FILE *file, struct lockres *res, int lvb)
{
	char line[512];

	while (fgets(line, sizeof(line), file)) {
		if (line[0] == '\n')
			break;
		if (!strncmp(line, "LRES:", 5))
			read_lres(line + 5, res);
		else if (!strncmp(line, "RMAP:", 5))
			read_rmap(line + 5, res);
		else if (!strncmp(line, "LOCK:", 5))
			read_lock(line + 5, res);
		else if (!strncmp(line, "LVBX:", 5)) {
			if (lvb)
				read_lvbx(line + 5, res);
		}
	}
}

static int get_next_dlm_lockname(FILE *file, char *name, int len)
{
	char line[512];

	while (fgets(line, sizeof(line), file)) {
		if (strncmp(line, "NAME:", 5))
			continue;
		sscanf(line + 5, "%s\n", name);
		return 1;
	}

	return 0;
}

void dump_dlm_locks(char *uuid, FILE *out, char *path, int dump_lvbs,
		    struct list_head *locklist)
{
	errcode_t ret;
	char debugfs_path[PATH_MAX];
	FILE *file;
	char name[OCFS2_LOCK_ID_MAX_LEN];
	struct lockres res;
	int show_all_locks = 0;

	if (path == NULL) {
		ret = get_debugfs_path(debugfs_path, sizeof(debugfs_path));
		if (ret) {
			fprintf(stderr, "Could not locate debugfs file system. "
				"Perhaps it is not mounted?\n");
			return;
		}

		ret = open_debugfs_file(debugfs_path, "o2dlm", uuid,
					"locking_state", &file);
		if (ret) {
			fprintf(stderr,
				"Could not open debug state for \"%s\".\n"
				"Perhaps that OCFS2 file system is not mounted?\n",
				uuid);
			return;
		}
	} else {
		file = fopen(path, "r");
		if (!file) {
			fprintf(stderr, "Could not open file at \"%s\"\n",
				path);
			return;
		}
	}

	show_all_locks = list_empty(locklist);
	init_lockres(&res);

	while (get_next_dlm_lockname(file, name, sizeof(name))) {
		if (show_all_locks || del_from_stringlist(name, locklist)) {
			read_lockres(file, &res, dump_lvbs);

			dump_lockres(name, &res, out);

			clean_lockres(&res);
		}

		if (!show_all_locks && list_empty(locklist))
			break;
	}

	fclose(file);
}
