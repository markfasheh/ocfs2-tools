/*
 * utils.c
 *
 * utility functions
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
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
 * Authors: Sunil Mushran
 */

#include <main.h>
#include <commands.h>
#include <dump.h>
#include <readfs.h>
#include <utils.h>
#include <journal.h>

/*
 * add_extent_rec()
 *
 */
void add_extent_rec (GArray *arr, ocfs2_extent_rec *rec)
{
	ocfs2_extent_rec *new;

	if (!arr)
		return ;

	if (!(new = malloc(sizeof(ocfs2_extent_rec))))
		DBGFS_FATAL("%s", strerror(errno));

	memcpy(new, rec, sizeof(ocfs2_extent_rec));
	g_array_append_vals(arr, new, 1);

	return ;
}				/* add_extent_rec */

/*
 * add_dir_rec()
 *
 */
void add_dir_rec (GArray *arr, struct ocfs2_dir_entry *rec)
{
	struct ocfs2_dir_entry *new;

	if (!arr)
		return ;

	if (!(new = malloc(sizeof(struct ocfs2_dir_entry))))
		DBGFS_FATAL("%s", strerror(errno));

	memset(new, 0, sizeof(struct ocfs2_dir_entry));

	new->inode = rec->inode;
	new->rec_len = rec->rec_len;
	new->name_len = rec->name_len;
	new->file_type = rec->file_type;
	strncpy(new->name, rec->name, rec->name_len);
	new->name[rec->name_len] = '\0';

	g_array_append_vals(arr, new, 1);

	return ;
}				/* add_dir_rec */

/*
 * get_vote_flag()
 *
 */
void get_vote_flag (__u32 flag, GString *str)
{
	if (flag & FLAG_VOTE_NODE)
		g_string_append (str, "ok ");

	if (flag & FLAG_VOTE_OIN_UPDATED)
		g_string_append (str, "oin_upd ");

	if (flag & FLAG_VOTE_OIN_ALREADY_INUSE)
		 g_string_append (str, "inuse ");

	if (flag & FLAG_VOTE_UPDATE_RETRY)
		 g_string_append (str, "retry ");

	if (flag & FLAG_VOTE_FILE_DEL)
		g_string_append (str, "del ");

	if (flag & ~(FLAG_VOTE_NODE | FLAG_VOTE_OIN_UPDATED |
		     FLAG_VOTE_OIN_ALREADY_INUSE |
		     FLAG_VOTE_UPDATE_RETRY | FLAG_VOTE_FILE_DEL))
		g_string_append (str, "unknown");

	if (!str->len)
		g_string_append (str, "none");

	return ;
}				/* get_vote_flag */

/*
 * get_publish_flag()
 *
 */
void get_publish_flag (__u32 flag, GString *str)
{
	if (flag & FLAG_FILE_CREATE)
		g_string_append (str, "create ");

	if (flag & FLAG_FILE_EXTEND)
		g_string_append (str, "extend ");

	if (flag & FLAG_FILE_DELETE)
		g_string_append (str, "delete ");

	if (flag & FLAG_FILE_RENAME)
		g_string_append (str, "rename ");

	if (flag &  FLAG_FILE_UPDATE)
		g_string_append (str, "update ");

	if (flag &  FLAG_FILE_RECOVERY)
		g_string_append (str, "recovery ");

	if (flag &  FLAG_FILE_CREATE_DIR)
		g_string_append (str, "createdir ");

	if (flag & FLAG_FILE_UPDATE_OIN)
		g_string_append (str, "upd_oin ");

	if (flag & FLAG_FILE_RELEASE_MASTER)
		g_string_append (str, "rls_mstr ");

	if (flag & FLAG_RELEASE_DENTRY)
		g_string_append (str, "rls_dntry ");

	if (flag & FLAG_CHANGE_MASTER)
		g_string_append (str, "chng_mstr ");

	if (flag & FLAG_ADD_OIN_MAP)
		g_string_append (str, "add_oin ");

	if (flag & FLAG_DIR)
		g_string_append (str, "dir ");

	if (flag & FLAG_REMASTER)
		g_string_append (str, "re_mstr ");

	if (flag & FLAG_FAST_PATH_LOCK)
		g_string_append (str, "fast_path");

	if (flag & FLAG_FILE_RELEASE_CACHE)
		g_string_append (str, "rls_cache ");

	if (flag & FLAG_FILE_TRUNCATE)
		g_string_append (str, "trunc ");

	if (flag & FLAG_DROP_READONLY)
		g_string_append (str, "drop_ro ");

	if (flag & FLAG_READDIR)
		g_string_append (str, "rddir ");

	if (flag & FLAG_ACQUIRE_LOCK)
		g_string_append (str, "acq ");

	if (flag & FLAG_RELEASE_LOCK)
		g_string_append (str, "rls ");

	if (flag & ~(FLAG_FILE_CREATE | FLAG_FILE_EXTEND | FLAG_FILE_DELETE |
		     FLAG_FILE_RENAME | FLAG_FILE_UPDATE | FLAG_FILE_RECOVERY |
		     FLAG_FILE_CREATE_DIR | FLAG_FILE_UPDATE_OIN |
		     FLAG_FILE_RELEASE_MASTER | FLAG_RELEASE_DENTRY |
		     FLAG_CHANGE_MASTER | FLAG_ADD_OIN_MAP | FLAG_DIR |
		     FLAG_REMASTER | FLAG_FAST_PATH_LOCK |
		     FLAG_FILE_RELEASE_CACHE | FLAG_FILE_TRUNCATE |
		     FLAG_DROP_READONLY | FLAG_READDIR | FLAG_ACQUIRE_LOCK |
		     FLAG_RELEASE_LOCK))
		g_string_append (str, "unknown");

	if (!str->len)
		g_string_append (str, "none");

	return ;
}				/* get_publish_flag */

/*
 * open_pager() -- copied from e2fsprogs-1.32/debugfs/util.c
 * 
 * Copyright (C) 1993, 1994 Theodore Ts'o.  This file may be
 * redistributed under the terms of the GNU Public License.
 *
 */
FILE *open_pager(void)
{
	FILE *outfile;
	const char *pager = getenv("PAGER");

	signal(SIGPIPE, SIG_IGN);
	if (pager) {
		if (strcmp(pager, "__none__") == 0) {
			return stdout;
		}
	} else
		pager = "more";

	outfile = popen(pager, "w");

	return (outfile ? outfile : stdout);
}				/* open_pager */

/*
 * close_pager() -- copied from e2fsprogs-1.32/debugfs/util.c
 * 
 * Copyright (C) 1993, 1994 Theodore Ts'o.  This file may be
 * redistributed under the terms of the GNU Public License.
 */
void close_pager(FILE *stream)
{
	if (stream && stream != stdout) pclose(stream);
}				/* close_pager */
