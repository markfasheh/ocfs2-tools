/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * utils.c
 *
 * utility functions
 *
 * Copyright (C) 2004, 2011 Oracle.  All rights reserved.
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

#include "main.h"
#include "ocfs2/bitops.h"

extern struct dbgfs_gbls gbls;

void get_incompat_flag(struct ocfs2_super_block *sb, char *buf, size_t count)
{
	errcode_t err;
	ocfs2_fs_options flags = {
		.opt_incompat = sb->s_feature_incompat,
	};

	*buf = '\0';
	err = ocfs2_snprint_feature_flags(buf, count, &flags);
	if (err)
		com_err(gbls.cmd, err, "while processing incompat flags");
}

void get_tunefs_flag(struct ocfs2_super_block *sb, char *buf, size_t count)
{
	errcode_t err;

	*buf = '\0';
	err = ocfs2_snprint_tunefs_flags(buf, count, sb->s_tunefs_flag);
	if (err)
		com_err(gbls.cmd, err, "while processing tunefs flags");
}

void get_compat_flag(struct ocfs2_super_block *sb, char *buf, size_t count)
{
	errcode_t err;
	ocfs2_fs_options flags = {
		.opt_compat = sb->s_feature_compat,
	};

	*buf = '\0';
	err = ocfs2_snprint_feature_flags(buf, count, &flags);
	if (err)
		com_err(gbls.cmd, err, "while processing compat flags");
}

void get_rocompat_flag(struct ocfs2_super_block *sb, char *buf, size_t count)
{
	errcode_t err;
	ocfs2_fs_options flags = {
		.opt_ro_compat = sb->s_feature_ro_compat,
	};

	*buf = '\0';
	err = ocfs2_snprint_feature_flags(buf, count, &flags);
	if (err)
		com_err(gbls.cmd, err, "while processing ro compat flags");
}

void get_cluster_info_flag(struct ocfs2_super_block *sb, char *buf,
			   size_t count)
{
	errcode_t err = 0;

	*buf = '\0';
	if (ocfs2_o2cb_stack(sb))
		err = ocfs2_snprint_cluster_o2cb_flags(buf, count,
					sb->s_cluster_info.ci_stackflags);

	if (err)
		com_err(gbls.cmd, err, "while processing clusterinfo flags");
}

/*
 * get_journal_block_type()
 *
 */
void get_journal_block_type(uint32_t jtype, GString *str)
{
	switch (jtype) {
	case JBD2_DESCRIPTOR_BLOCK:
		g_string_append(str, "JBD2_DESCRIPTOR_BLOCK");
		break;
	case JBD2_COMMIT_BLOCK:
		g_string_append(str, "JBD2_COMMIT_BLOCK");
		break;
	case JBD2_SUPERBLOCK_V1:
		g_string_append(str, "JBD2_SUPERBLOCK_V1");
		break;
	case JBD2_SUPERBLOCK_V2:
		g_string_append(str, "JBD2_SUPERBLOCK_V2");
		break;
	case JBD2_REVOKE_BLOCK:
		g_string_append(str, "JBD2_REVOKE_BLOCK");
		break;
	}

	if (!str->len)
		g_string_append(str, "none");

	return ;
}

/*
 * get_tag_flag()
 *
 */
void get_tag_flag(uint32_t flags, GString *str)
{
	if (flags == 0) {
		g_string_append(str, "none");
		goto done;
	}

	if (flags & JBD2_FLAG_ESCAPE)
		g_string_append(str, "JBD2_FLAG_ESCAPE ");

	if (flags & JBD2_FLAG_SAME_UUID)
		g_string_append(str, "JBD2_FLAG_SAME_UUID ");

	if (flags & JBD2_FLAG_DELETED)
		g_string_append(str, "JBD2_FLAG_DELETED ");

	if (flags & JBD2_FLAG_LAST_TAG)
		g_string_append(str, "JBD2_FLAG_LAST_TAG");

done:
	return ;
}

/*
 * Adds nanosec to the ctime output. eg. Tue Jul 19 13:36:52.123456 2011
 * On error, returns an empty string.
 */
void ctime_nano(struct timespec *t, char *buf, int buflen)
{
	time_t sec;
	char tmp[26], *p;

	sec = (time_t)t->tv_sec;
	if (!ctime_r(&sec, tmp))
		return;

	/* Find the last space where we will append the nanosec */
	if ((p = strrchr(tmp, ' '))) {
		*p = '\0';
		snprintf(buf, buflen, "%s.%ld %s", tmp, t->tv_nsec, ++p);
	}
}

/*
 * open_pager() -- copied from e2fsprogs-1.32/debugfs/util.c
 * 
 * Copyright (C) 1993, 1994 Theodore Ts'o.  This file may be
 * redistributed under the terms of the GNU Public License.
 *
 */
FILE *open_pager(int interactive)
{
	FILE *outfile = NULL;
	const char *pager = getenv("PAGER");

	if (interactive) {
		signal(SIGPIPE, SIG_IGN);
		if (pager) {
			if (strcmp(pager, "__none__") == 0) {
				return stdout;
			}
		} else
			pager = "more";

		outfile = popen(pager, "w");
	}

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
 */
int inodestr_to_inode(char *str, uint64_t *blkno)
{
	int len;
	char *buf = NULL;
	char *end;
	int ret = OCFS2_ET_INVALID_LOCKRES;

	len = strlen(str);
	if (!((len > 2) && (str[0] == '<') && (str[len - 1] == '>')))
		goto bail;

	ret = OCFS2_ET_NO_MEMORY;
	buf = strndup(str + 1, len - 2);
	if (!buf)
		goto bail;

	ret = 0;
	if (ocfs2_get_lock_type(buf[0]) < OCFS2_NUM_LOCK_TYPES)
		ret = ocfs2_decode_lockres(buf, NULL, blkno, NULL, NULL);
	else {
		*blkno = strtoull(buf, &end, 0);
		if (*end)
			ret = OCFS2_ET_INVALID_LOCKRES;
	}

bail:
	if (buf)
		free(buf);

	return ret;
}

/*
 * string_to_inode()
 *
 * This routine is used whenever a command needs to turn a string into
 * an inode.
 *
 * Code based on similar function in e2fsprogs-1.32/debugfs/util.c
 *
 * Copyright (C) 1993, 1994 Theodore Ts'o.  This file may be
 * redistributed under the terms of the GNU Public License.
 */
errcode_t string_to_inode(ocfs2_filesys *fs, uint64_t root_blkno,
			  uint64_t cwd_blkno, char *str, uint64_t *blkno)
{
	uint64_t root = root_blkno;

	/*
	 * If the string is of the form <ino>, then treat it as an
	 * inode number.
	 */
	if (!inodestr_to_inode(str, blkno))
		return 0;

	/* // is short for system directory */
	if (!strncmp(str, "//", 2)) {
		root = fs->fs_sysdir_blkno;
		++str;
	}

	return ocfs2_namei(fs, root, cwd_blkno, str, blkno);
}

/*
 * fix_perms()
 *
 * Code based on similar function in e2fsprogs-1.32/debugfs/dump.c
 *
 * Copyright (C) 1994 Theodore Ts'o.  This file may be redistributed
 * under the terms of the GNU Public License.
 */
static errcode_t fix_perms(const struct ocfs2_dinode *di, int *fd,
                           char *name)
{
	struct utimbuf ut;
	int i;
	errcode_t ret = 0;

	if (*fd != -1)
		i = fchmod(*fd, di->i_mode);
	else
		i = chmod(name, di->i_mode);
	if (i == -1) {
		ret = errno;
		goto bail;
	}

	if (*fd != -1)
		i = fchown(*fd, di->i_uid, di->i_gid);
	else
		i = chown(name, di->i_uid, di->i_gid);
	if (i == -1) {
		ret = errno;
		goto bail;
	}

	if (*fd != -1) {
		close(*fd);
		*fd = -1;
	}

	ut.actime = di->i_atime;
	ut.modtime = di->i_mtime;
	if (utime(name, &ut) == -1)
		ret = errno;

bail:
	return ret;
}

/*
 * dump_file()
 *
 */
static errcode_t dump_symlink(ocfs2_filesys *fs, uint64_t blkno, char *name,
			      struct ocfs2_dinode *inode);

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
	if (ret) {
		com_err(gbls.cmd, ret, "while reading inode %"PRIu64, ino);
		goto bail;
	}

	if (S_ISLNK(ci->ci_inode->i_mode)) {

		ret = unlink(out_file);
		if (ret)
			goto bail;

		ret = dump_symlink(fs, ino, out_file, ci->ci_inode);
		goto bail;
	}

	buflen = 1024 * 1024;

	ret = ocfs2_malloc_blocks(fs->fs_io,
				  (buflen >>
				   OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits),
				  &buf);
	if (ret) {
		com_err(gbls.cmd, ret, "while allocating %u bytes", buflen);
		goto bail;
	}

	while (1) {
		ret = ocfs2_file_read(ci, buf, buflen, offset, &got);
		if (ret) {
			com_err(gbls.cmd, ret, "while reading file %"PRIu64" "
				"at offset %"PRIu64, ci->ci_blkno, offset);
			goto bail;
		}

		if (!got)
			break;

		wrote = write(fd, buf, got);
		if (wrote != got) {
			com_err(gbls.cmd, errno, "while writing file");
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
	if (ret) {
		com_err(gbls.cmd, ret, "while reading inode %"PRIu64, ino);
		goto bail;
	}

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
	if (ret) {
		com_err(gbls.cmd, ret, "while allocating %u bytes", *buflen);
		goto bail;
	}

	ret = ocfs2_file_read(ci, *buf, *buflen, 0, &got);
	if (ret) {
		com_err(gbls.cmd, ret, "while reading file at inode %"PRIu64,
			ci->ci_blkno);
		goto bail;
	}

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


/*
 * dump_symlink()
 *
 * Code based on similar function in e2fsprogs-1.32/debugfs/dump.c
 *
 * Copyright (C) 1994 Theodore Ts'o.  This file may be redistributed
 * under the terms of the GNU Public License.
 */
static errcode_t dump_symlink(ocfs2_filesys *fs, uint64_t blkno, char *name,
			      struct ocfs2_dinode *inode)
{
	char *buf = NULL;
	uint32_t len = 0;
	errcode_t ret = 0;

	char *link = NULL;

	if (!inode->i_clusters)
		link = (char *)inode->id2.i_symlink;
	else {
		ret = read_whole_file(fs, blkno, &buf, &len);
		if (ret)
			goto bail;
		link = buf;
	}

	if (symlink(link, name) == -1)
		ret = errno;

bail:
	if (buf)
		ocfs2_free(&buf);

	return ret;
}

/*
 * rdump_dirent()
 *
 * Code based on similar function in e2fsprogs-1.32/debugfs/dump.c
 *
 * Copyright (C) 1994 Theodore Ts'o.  This file may be redistributed
 * under the terms of the GNU Public License.
 */
static int rdump_dirent(struct ocfs2_dir_entry *rec, uint64_t blocknr,
			int offset, int blocksize, char *buf, void *priv_data)
{
	struct rdump_opts *rd = (struct rdump_opts *)priv_data;
	char tmp = rec->name[rec->name_len];
	errcode_t ret = 0;

	rec->name[rec->name_len] = '\0';

	if (!strcmp(rec->name, ".") || !strcmp(rec->name, ".."))
		goto bail;

	ret = rdump_inode(rd->fs, rec->inode, rec->name, rd->fullname,
			  rd->verbose);

bail:
	rec->name[rec->name_len] = tmp;

	return ret;
}

/*
 * rdump_inode()
 *
 * Code based on similar function in e2fsprogs-1.32/debugfs/dump.c
 *
 * Copyright (C) 1994 Theodore Ts'o.  This file may be redistributed
 * under the terms of the GNU Public License.
 */
errcode_t rdump_inode(ocfs2_filesys *fs, uint64_t blkno, const char *name,
		      const char *dumproot, int verbose)
{
	char *fullname = NULL;
	int len;
	errcode_t ret;
	char *buf = NULL;
	char *dirbuf = NULL;
	struct ocfs2_dinode *di;
	int fd;
	struct rdump_opts rd_opts = { NULL, NULL, NULL, 0 };

	len = strlen(dumproot) + strlen(name) + 2;
	ret = ocfs2_malloc(len, &fullname);
	if (ret) {
		com_err(gbls.cmd, ret, "while allocating %u bytes", len);
		goto bail;
	}

	snprintf(fullname, len, "%s/%s", dumproot, name);

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret) {
		com_err(gbls.cmd, ret, "while allocating a block");
		goto bail;
	}

	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret) {
		com_err(gbls.cmd, ret, "while reading inode %"PRIu64,
		       	blkno);
		goto bail;
	}

	di = (struct ocfs2_dinode *)buf;

	if (S_ISLNK(di->i_mode)) {
		ret = dump_symlink(fs, blkno, fullname, di);
		if (ret)
			goto bail;
	} else if (S_ISREG(di->i_mode)) {
		if (verbose)
			fprintf(stdout, "%s\n", fullname);
		fd = open64(fullname, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
		if (fd == -1) {
			com_err(gbls.cmd, errno, "while opening file %s",
				fullname);
			ret = errno;
			goto bail;
		}

		ret = dump_file(fs, blkno, fd, fullname, 1);
		if (ret)
			goto bail;
	} else if (S_ISDIR(di->i_mode) && strcmp(name, ".") &&
		   strcmp(name, "..")) {

		if (verbose)
			fprintf(stdout, "%s\n", fullname);
		/* Create the directory with 0700 permissions, because we
		 * expect to have to create entries it.  Then fix its perms
		 * once we've done the traversal. */
		if (mkdir(fullname, S_IRWXU) == -1) {
			com_err(gbls.cmd, errno, "while making directory %s",
				fullname);
			ret = errno;
			goto bail;
		}

		ret = ocfs2_malloc_block(fs->fs_io, &dirbuf);
		if (ret) {
			com_err(gbls.cmd, ret, "while allocating a block");
			goto bail;
		}

		rd_opts.fs = fs;
		rd_opts.buf = dirbuf;
		rd_opts.fullname = fullname;
		rd_opts.verbose = verbose;

		ret = ocfs2_dir_iterate(fs, blkno, 0, NULL,
					rdump_dirent, (void *)&rd_opts);
		if (ret) {
			com_err(gbls.cmd, ret, "while iterating directory at "
				"block %"PRIu64, blkno);
			goto bail;
		}

		fd = -1;
		ret = fix_perms(di, &fd, fullname);
		if (ret)
			goto bail;
	}
	/* else do nothing (don't dump device files, sockets, fifos, etc.) */

bail:
	if (fullname)
		ocfs2_free(&fullname);
	if (buf)
		ocfs2_free(&buf);
	if (dirbuf)
		ocfs2_free(&dirbuf);

	return ret;
}

/*
 * crunch_strsplit()
 *
 * Moves empty strings to the end in args returned by g_strsplit(),
 *
 */
void crunch_strsplit(char **args)
{
	char *p;
	int i, j;
	
	i = j = 0;
	while(args[i]) {
		if (!strlen(args[i])) {
			j = max(j, i+1);
			while(args[j]) {
				if (strlen(args[j])) {
					p = args[i];
					args[i] = args[j];
					args[j] = p;
					break;
				}
				++j;
			}
			if (!args[j])
				break;
		}
		++i;
	}

	return ;
}

/*
 * find_max_contig_free_bits()
 *
 */
void find_max_contig_free_bits(struct ocfs2_group_desc *gd, int *max_contig_free_bits)
{
	int end = 0;
	int start;
	int free_bits;

	*max_contig_free_bits = 0;

	while (end < gd->bg_bits) {
		start = ocfs2_find_next_bit_clear(gd->bg_bitmap, gd->bg_bits, end);
		if (start >= gd->bg_bits)
			break;

		end = ocfs2_find_next_bit_set(gd->bg_bitmap, gd->bg_bits, start);
		free_bits = end - start;
		if (*max_contig_free_bits < free_bits)
			*max_contig_free_bits = free_bits;
	}
}

#define SYSFS_BASE		"/sys/kernel/"
#define DEBUGFS_PATH		SYSFS_BASE "debug"
#define DEBUGFS_ALTERNATE_PATH	"/debug"
#define DEBUGFS_MAGIC		0x64626720
errcode_t get_debugfs_path(char *debugfs_path, int len)
{
	errcode_t ret;
	int err;
	struct stat64 stat_buf;
	struct statfs64 statfs_buf;
	char *path = DEBUGFS_PATH;

	err = stat64(SYSFS_BASE, &stat_buf);
	if (err)
		path = DEBUGFS_ALTERNATE_PATH;

	ret = stat64(path, &stat_buf);
	if (ret || !S_ISDIR(stat_buf.st_mode))
		return O2CB_ET_SERVICE_UNAVAILABLE;

	ret = statfs64(path, &statfs_buf);
	if (ret || (statfs_buf.f_type != DEBUGFS_MAGIC))
		return O2CB_ET_SERVICE_UNAVAILABLE;

	strncpy(debugfs_path, path, len);

	return 0;
}

errcode_t open_debugfs_file(const char *debugfs_path, const char *dirname,
			    const char *uuid, const char *filename, FILE **fd)
{
	errcode_t ret = 0;
	char path[PATH_MAX];

	if (uuid)
		ret = snprintf(path, PATH_MAX - 1, "%s/%s/%s/%s",
			       debugfs_path, dirname, uuid, filename);
	else
		ret = snprintf(path, PATH_MAX - 1, "%s/%s/%s",
			       debugfs_path, dirname, filename);

	if ((ret <= 0) || (ret == (PATH_MAX - 1)))
		return O2CB_ET_INTERNAL_FAILURE;

	*fd = fopen(path, "r");
	if (!*fd) {
		switch (errno) {
		default:
			ret = O2CB_ET_INTERNAL_FAILURE;
			break;

		case ENOTDIR:
		case ENOENT:
		case EISDIR:
			ret = O2CB_ET_SERVICE_UNAVAILABLE;
			break;

		case EACCES:
		case EPERM:
		case EROFS:
			ret = O2CB_ET_PERMISSION_DENIED;
			break;
		}
		goto out;
	}

	ret = 0;
out:
	return ret;
}

void init_stringlist(struct list_head *strlist)
{
	INIT_LIST_HEAD(strlist);
}

errcode_t add_to_stringlist(char *str, struct list_head *strlist)
{
	struct strings *s;

	if (!str || !strlen(str))
		return 0;

	s = calloc(1, sizeof(struct strings));
	if (!s)
		return OCFS2_ET_NO_MEMORY;

	INIT_LIST_HEAD(&s->s_list);
	s->s_str = strdup(str);
	if (!s->s_str) {
		free(s);
		return OCFS2_ET_NO_MEMORY;
	}

	list_add_tail(&s->s_list, strlist);

	return 0;
}

void free_stringlist(struct list_head *strlist)
{
	struct strings *s;
	struct list_head *iter, *iter2;

	if (list_empty(strlist))
		return;

	list_for_each_safe(iter, iter2, strlist) {
		s = list_entry(iter, struct strings, s_list);
		list_del(iter);
		free(s->s_str);
		free(s);
	}
}

int del_from_stringlist(char *str, struct list_head *strlist)
{
	struct strings *s;
	struct list_head *iter, *iter2;

	if (!list_empty(strlist)) {
		list_for_each_safe(iter, iter2, strlist) {
			s = list_entry(iter, struct strings, s_list);
			if (!strcmp(str, s->s_str)) {
				list_del(iter);
				free(s->s_str);
				free(s);
				return 1;
			}
		}
	}

	return 0;
}

errcode_t traverse_extents(ocfs2_filesys *fs, struct ocfs2_extent_list *el,
			   FILE *out)
{
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_rec *rec;
	errcode_t ret = 0;
	char *buf = NULL;
	int i;
	uint32_t clusters;

	dump_extent_list(out, el);

	for (i = 0; i < el->l_next_free_rec; ++i) {
		rec = &(el->l_recs[i]);
		clusters = ocfs2_rec_clusters(el->l_tree_depth, rec);

		/*
		 * In a unsuccessful insertion, we may shift a tree
		 * add a new branch for it and do no insertion. So we
		 * may meet a extent block which have
		 * clusters == 0, this should only be happen
		 * in the last extent rec. */
		if (!clusters && i == el->l_next_free_rec - 1)
			break;

		if (el->l_tree_depth) {
			ret = ocfs2_malloc_block(gbls.fs->fs_io, &buf);
			if (ret)
				goto bail;

			ret = ocfs2_read_extent_block(fs, rec->e_blkno, buf);
			if (ret)
				goto bail;

			eb = (struct ocfs2_extent_block *)buf;

			dump_extent_block(out, eb);

			ret = traverse_extents(fs, &(eb->h_list), out);
			if (ret)
				goto bail;
		}
	}

bail:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

errcode_t traverse_chains(ocfs2_filesys *fs, struct ocfs2_chain_list *cl,
			  FILE *out)
{
	struct ocfs2_group_desc *grp;
	struct ocfs2_chain_rec *rec;
	errcode_t ret = 0;
	char *buf = NULL;
	uint64_t blkno;
	int i;
	int index;

	dump_chain_list(out, cl);

	ret = ocfs2_malloc_block(gbls.fs->fs_io, &buf);
	if (ret)
		goto bail;

	for (i = 0; i < cl->cl_next_free_rec; ++i) {
		rec = &(cl->cl_recs[i]);
		blkno = rec->c_blkno;
		index = 0;
		fprintf(out, "\n");
		while (blkno) {
			ret = ocfs2_read_group_desc(fs, blkno, buf);
			if (ret)
				goto bail;

			grp = (struct ocfs2_group_desc *)buf;
			dump_group_descriptor(out, grp, index);
			blkno = grp->bg_next_group;
			index++;
		}
	}

bail:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}
