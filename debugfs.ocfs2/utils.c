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
}

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

	if (flag & FLAG_FILE_UPDATE)
		g_string_append (str, "update ");

	if (flag & FLAG_FILE_RECOVERY)
		g_string_append (str, "recovery ");

	if (flag & FLAG_FILE_CREATE_DIR)
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
}

/*
 * get_journal_blktyp()
 *
 */
void get_journal_blktyp (__u32 jtype, GString *str)
{
	switch (jtype) {
	case JFS_DESCRIPTOR_BLOCK:
		g_string_append (str, "JFS_DESCRIPTOR_BLOCK");
		break;
	case JFS_COMMIT_BLOCK:
		g_string_append (str, "JFS_COMMIT_BLOCK");
		break;
	case JFS_SUPERBLOCK_V1:
		g_string_append (str, "JFS_SUPERBLOCK_V1");
		break;
	case JFS_SUPERBLOCK_V2:
		g_string_append (str, "JFS_SUPERBLOCK_V2");
		break;
	case JFS_REVOKE_BLOCK:
		g_string_append (str, "JFS_REVOKE_BLOCK");
		break;
	}

	if (!str->len)
		g_string_append (str, "none");

	return ;
}

/*
 * get_tag_flag()
 *
 */
void get_tag_flag (__u32 flags, GString *str)
{
	if (flags == 0) {
		g_string_append (str, "none");
		goto done;
	}

	if (flags & JFS_FLAG_ESCAPE)
		g_string_append (str, "JFS_FLAG_ESCAPE ");

	if (flags & JFS_FLAG_SAME_UUID)
		g_string_append (str, "JFS_FLAG_SAME_UUID ");

	if (flags & JFS_FLAG_DELETED)
		g_string_append (str, "JFS_FLAG_DELETED ");

	if (flags & JFS_FLAG_LAST_TAG)
		g_string_append (str, "JFS_FLAG_LAST_TAG");

done:
	return ;
}

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
}

/*
 * close_pager() -- copied from e2fsprogs-1.32/debugfs/util.c
 * 
 * Copyright (C) 1993, 1994 Theodore Ts'o.  This file may be
 * redistributed under the terms of the GNU Public License.
 */
void close_pager(FILE *stream)
{
	if (stream && stream != stdout) pclose(stream);
}


/*
 * string_to_inode()
 *
 * This routine is used whenever a command needs to turn a string into
 * an inode.
 *
 * Copyright (C) 1993, 1994 Theodore Ts'o.  This file may be
 * redistributed under the terms of the GNU Public License.
 */
errcode_t string_to_inode(ocfs2_filesys *fs, uint64_t root_blkno,
			  uint64_t cwd_blkno, char *str, uint64_t *blkno)
{
	int len;
	char *end;

	/*
	 * If the string is of the form <ino>, then treat it as an
	 * inode number.
	 */
	len = strlen(str);
	if ((len > 2) && (str[0] == '<') && (str[len-1] == '>')) {
		*blkno = strtoul(str+1, &end, 0);
		if (*end=='>')
			return 0;
	}

	return ocfs2_namei(fs, root_blkno, cwd_blkno, str, blkno);
}
