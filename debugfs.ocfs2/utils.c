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
 * inodestr_to_inode()
 *
 * Returns ino if string is of the form <ino>
 *
 * Copyright (C) 1993, 1994 Theodore Ts'o.  This file may be
 * redistributed under the terms of the GNU Public License.
 */
int inodestr_to_inode(char *str, uint64_t *blkno)
{
	int len;
	char *end;

	len = strlen(str);
	if ((len > 2) && (str[0] == '<') && (str[len-1] == '>')) {
		*blkno = strtoul(str+1, &end, 0);
		if (*end=='>')
			return 0;
	}

	return -1;
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
	/*
	 * If the string is of the form <ino>, then treat it as an
	 * inode number.
	 */
	if (!inodestr_to_inode(str, blkno))
		return 0;

	return ocfs2_namei(fs, root_blkno, cwd_blkno, str, blkno);
}

/*
 * fix_perms()
 *
 */
static errcode_t fix_perms(const ocfs2_dinode *di, int *fd, char *out_file)
{
	struct utimbuf ut;
	int i;
	errcode_t ret = 0;

	i = fchmod(*fd, di->i_mode);
	if (i == -1) {
		ret = errno;
		goto bail;
	}

	i = fchown(*fd, di->i_uid, di->i_gid);
	if (i == -1) {
		ret = errno;
		goto bail;
	}

	close(*fd);
	*fd = -1;

	ut.actime = di->i_atime;
	ut.modtime = di->i_mtime;
	if (utime(out_file, &ut) == -1) {
		ret = errno;
		goto bail;
	}
bail:
	return ret;
}

/*
 * dump_file()
 *
 */
errcode_t dump_file(ocfs2_filesys *fs, uint64_t ino, int fd, char *out_file,
		    int preserve)
{
	errcode_t ret;
	char *buf = NULL;
	int buflen;
	uint32_t got;
	uint32_t wrote;
	ocfs2_cached_inode *ci = NULL;
	uint64_t offset = 0;

	ret = ocfs2_read_cached_inode(fs, ino, &ci);
	if (ret)
		goto bail;

	ret = ocfs2_extent_map_init(fs, ci);
	if (ret)
		goto bail;

	buflen = 1024 * 1024;

	ret = ocfs2_malloc_blocks(fs->fs_io,
				  (buflen >>
				   OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits),
				  &buf);
	if (ret)
		goto bail;

	while (1) {
		ret = ocfs2_file_read(ci, buf, buflen, offset, &got);
		if (ret)
			goto bail;

		if (!got)
			break;

		wrote = write(fd, buf, got);
		if (wrote != got) {
			ret = errno;
			goto bail;
		}

		if (got < buflen)
			break;
		else
			offset += got;
	}

	if (preserve)
		ret = fix_perms(ci->ci_inode, &fd, out_file);

bail:
	if (fd > 0 && fd != fileno(stdout))
		close(fd);
	if (buf)
		ocfs2_free(&buf);
	if (ci)
		ocfs2_free_cached_inode(fs, ci);
	return ret;
}


/*
 * read_whole_file()
 *
 * read in buflen bytes or whole file if buflen = 0
 *
 */
errcode_t read_whole_file(ocfs2_filesys *fs, uint64_t ino, char **buf,
			  uint32_t *buflen)
{
	errcode_t ret;
	uint32_t got;
	ocfs2_cached_inode *ci = NULL;

	ret = ocfs2_read_cached_inode(fs, ino, &ci);
	if (ret)
		goto bail;

	ret = ocfs2_extent_map_init(fs, ci);
	if (ret)
		goto bail;

	if (!*buflen) {
		*buflen = (((ci->ci_inode->i_size + fs->fs_blocksize - 1) >>
			    OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits) <<
			   OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits);
	}

	/* bail if file size is larger than reasonable :-) */
	if (*buflen > 100 * 1024 * 1024) {
		ret = OCFS2_ET_INTERNAL_FAILURE;
		goto bail;
	}

	ret = ocfs2_malloc_blocks(fs->fs_io,
				  (*buflen >>
				   OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits),
				  buf);
	if (ret)
		goto bail;

	ret = ocfs2_file_read(ci, *buf, *buflen, 0, &got);
	if (ret)
		goto bail;

bail:
	if (ci)
		ocfs2_free_cached_inode(fs, ci);
	return ret;
}

/*
 * inode_perms_to_str()
 *
 */
void inode_perms_to_str(uint16_t mode, char *str, int len)
{
	if (len < 11)
		DBGFS_FATAL("internal error");

	if (S_ISREG(mode))
		str[0] = '-';
	else if (S_ISDIR(mode))
		str[0] = 'd';
	else if (S_ISLNK(mode))
		str[0] = 'l';
	else if (S_ISCHR(mode))
		str[0] = 'c';
	else if (S_ISBLK(mode))
		str[0] = 'b';
	else if (S_ISFIFO(mode))
		str[0] = 'f';
	else if (S_ISSOCK(mode))
		str[0] = 's';
	else
		str[0] = '-';

	str[1] = (mode & S_IRUSR) ? 'r' : '-';
	str[2] = (mode & S_IWUSR) ? 'w' : '-';
	if (mode & S_ISUID)
		str[3] = (mode & S_IXUSR) ? 's' : 'S';
	else
		str[3] = (mode & S_IXUSR) ? 'x' : '-';
	
	str[4] = (mode & S_IRGRP) ? 'r' : '-';
	str[5] = (mode & S_IWGRP) ? 'w' : '-';
	if (mode & S_ISGID)
		str[6] = (mode & S_IXGRP) ? 's' : 'S';
	else
		str[6] = (mode & S_IXGRP) ? 'x' : '-';
	
	str[7] = (mode & S_IROTH) ? 'r' : '-';
	str[8] = (mode & S_IWOTH) ? 'w' : '-';
	if (mode & S_ISVTX)
		str[9] = (mode & S_IXOTH) ? 't' : 'T';
	else
		str[9] = (mode & S_IXOTH) ? 'x' : '-';

	str[10] = '\0';

	return ;
}

/*
 * inode_time_to_str()
 *
 */
void inode_time_to_str(uint64_t timeval, char *str, int len)
{
	time_t tt = (time_t) timeval;
	struct tm *tm;

	static const char *month_str[] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

	tm = localtime(&tt);

	snprintf(str, len, "%2d-%s-%4d %02d:%02d", tm->tm_mday,
		 month_str[tm->tm_mon], 1900 + tm->tm_year,
		 tm->tm_hour, tm->tm_min);

	return ;
}
