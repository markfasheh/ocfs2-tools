/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * utils.c
 *
 * utility functions for o2info
 *
 * Copyright (C) 2010 Oracle.  All rights reserved.
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

#define _XOPEN_SOURCE 600
#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE /* Because libc really doesn't want us using O_DIRECT? */

#include <unistd.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>

#include "ocfs2/ocfs2.h"
#include "tools-internal/verbose.h"

#include "utils.h"

int o2info_get_compat_flag(uint32_t flag, char **compat)
{
	errcode_t err;
	char buf[PATH_MAX];
	ocfs2_fs_options flags = {
		.opt_compat = flag,
	};

	*buf = '\0';
	err = ocfs2_snprint_feature_flags(buf, PATH_MAX, &flags);
	if (err) {
		tcom_err(err, "while processing feature flags");
		goto bail;
	}

	*compat = strdup(buf);
	if (!*compat) {
		errorf("No memory for allocation\n");
		err = -1;
	}

bail:
	return err;
}

int o2info_get_incompat_flag(uint32_t flag, char **incompat)
{
	errcode_t err;
	char buf[PATH_MAX];
	ocfs2_fs_options flags = {
		.opt_incompat = flag,
	};

	*buf = '\0';
	err = ocfs2_snprint_feature_flags(buf, PATH_MAX, &flags);
	if (err) {
		tcom_err(err, "while processing feature flags");
		goto bail;
	}

	*incompat = strdup(buf);
	if (!*incompat) {
		errorf("No memory for allocation\n");
		err = -1;
	}

bail:
	return err;
}

int o2info_get_rocompat_flag(uint32_t flag, char **rocompat)
{
	errcode_t err;
	char buf[PATH_MAX];
	ocfs2_fs_options flags = {
		.opt_ro_compat = flag,
	};

	*buf = '\0';
	err = ocfs2_snprint_feature_flags(buf, PATH_MAX, &flags);
	if (err) {
		tcom_err(err, "while processing feature flags");
		goto bail;
	}

	*rocompat = strdup(buf);
	if (!*rocompat) {
		errorf("No memory for allocation\n");
		err = -1;
	}

bail:
	return err;
}

errcode_t o2info_open(struct o2info_method *om, int flags)
{
	errcode_t err = 0;
	int fd, open_flags;
	ocfs2_filesys *fs = NULL;

	if (om->om_method == O2INFO_USE_LIBOCFS2) {
		open_flags = flags|OCFS2_FLAG_HEARTBEAT_DEV_OK|OCFS2_FLAG_RO;
		err = ocfs2_open(om->om_path, open_flags, 0, 0, &fs);
		if (err) {
			tcom_err(err, "while opening device %s", om->om_path);
			goto out;
		}
		om->om_fs = fs;
	} else {
		open_flags = flags | O_RDONLY;
		fd = open(om->om_path, open_flags);
		if (fd < 0) {
			err = errno;
			tcom_err(err, "while opening file %s", om->om_path);
			goto out;
		}
		om->om_fd = fd;
	}

out:
	return err;
}

errcode_t o2info_close(struct o2info_method *om)
{
	errcode_t err = 0;
	int rc = 0;

	if (om->om_method == O2INFO_USE_LIBOCFS2) {
		if (om->om_fs) {
			err = ocfs2_close(om->om_fs);
			if (err) {
				tcom_err(err, "while closing device");
				goto out;
			}
		}
	} else {
		if (om->om_fd >= 0) {
			rc = close(om->om_fd);
			if (rc < 0) {
				rc = errno;
				tcom_err(rc, "while closing fd: %d.\n",
					 om->om_fd);
				err = rc;
			}
		}
	}

out:
	return err;
}

int o2info_method(const char *path)
{
	int rc;
	struct stat st;

	rc = stat(path, &st);
	if (rc < 0) {
		tcom_err(errno, "while stating %s", path);
		goto out;
	}

	rc = O2INFO_USE_IOCTL;
	if ((S_ISBLK(st.st_mode)) || (S_ISCHR(st.st_mode)))
		rc = O2INFO_USE_LIBOCFS2;

out:
	return rc;
}

int o2info_get_filetype(struct stat st, char **filetype)
{
	int rc = 0;

	if (S_ISREG(st.st_mode))
		if (st.st_size != 0)
			*filetype = strdup("regular file");
		else
			*filetype = strdup("regular empty file");
	else if (S_ISDIR(st.st_mode))
		*filetype = strdup("directory");
	else if (S_ISCHR(st.st_mode))
		*filetype = strdup("character special file");
	else if (S_ISBLK(st.st_mode))
		*filetype = strdup("block special file");
	else if (S_ISFIFO(st.st_mode))
		*filetype = strdup("FIFO");
	else if (S_ISLNK(st.st_mode))
		if (st.st_blocks == 0)
			*filetype = strdup("fast symbolic link");
		else
			*filetype = strdup("symbolic link");
	else if (S_ISSOCK(st.st_mode))
		*filetype = strdup("socket");
	else {
		*filetype = strdup("unknown file type");
		rc = -1;
	}

	if (!*filetype) {
		errorf("No memory for allocation\n");
		rc = -1;
	}

	return rc;
}

int o2info_get_human_permission(mode_t st_mode, uint16_t *perm, char **h_perm)
{
	int rc = 0;
	char tmp[11] = "----------";

	*perm = (uint16_t)(st_mode & 0x00000FFF);

	tmp[10] = '\0';
	tmp[9] = (*perm & 0x0001) ? 'x' : '-';
	tmp[8] = (*perm & 0x0002) ? 'w' : '-';
	tmp[7] = (*perm & 0x0004) ? 'r' : '-';
	tmp[6] = (*perm & 0x0008) ? 'x' : '-';
	tmp[5] = (*perm & 0x0010) ? 'w' : '-';
	tmp[4] = (*perm & 0x0020) ? 'r' : '-';
	tmp[3] = (*perm & 0x0040) ? 'x' : '-';
	tmp[2] = (*perm & 0x0080) ? 'w' : '-';
	tmp[1] = (*perm & 0x0100) ? 'r' : '-';

	/*
	 * Handling the setuid/setgid/sticky bits,
	 * by following the convention stat obeys.
	 */
	if (*perm & 0x0200) {
		if (*perm & 0x0001)
			tmp[9] = 't';
		else
			tmp[9] = 'T';
	}

	if (*perm & 0x0400) {
		if (*perm & 0x0008)
			tmp[6] = 's';
		else
			tmp[6] = 'S';
	}

	if (*perm & 0x0800) {
		if (*perm & 0x0040)
			tmp[3] = 's';
		else
			tmp[3] = 'S';
	}

	if (S_ISCHR(st_mode))
		tmp[0] = 'c';
	else if (S_ISBLK(st_mode))
		tmp[0] = 'b';
	else if (S_ISFIFO(st_mode))
		tmp[0] = 'p';
	else if (S_ISLNK(st_mode))
		tmp[0] = 'l';
	else if (S_ISSOCK(st_mode))
		tmp[0] = 's';
	else if (S_ISDIR(st_mode))
		tmp[0] = 'd';

	*h_perm = strdup(tmp);
	if (!*h_perm) {
		errorf("No memory for allocation\n");
		rc = -1;
	}

	return rc;
}

int o2info_uid2name(uid_t uid, char **uname)
{
	struct passwd *entry;
	int ret = 0;

	entry = getpwuid(uid);

	if (!entry) {
		errorf("user %d does not exist!\n", uid);
		ret = -1;
	} else {
		*uname = strdup(entry->pw_name);
		if (*uname == NULL) {
			errorf("No memory for allocation\n");
			ret = -1;
		}
	}

	return ret;
}

int o2info_gid2name(gid_t gid, char **gname)
{
	struct group *group;
	int ret = 0;

	group = getgrgid(gid);

	if (!group) {
		errorf("group %d does not exist!\n", gid);
		ret = -1;
	} else {
		*gname = strdup(group->gr_name);
		if (*gname == NULL) {
			errorf("No memory for allocation\n");
			ret = -1;
		}
	}

	return ret;
}

struct timespec o2info_get_stat_atime(struct stat *st)
{
#ifdef __USE_MISC
	return st->st_atim;
#else
	struct timespec t;
	t.tv_sec = st->st_atime;
	t.tv_nsec = st->st_atimensec;
	return t;
#endif
}

struct timespec o2info_get_stat_mtime(struct stat *st)
{
#ifdef __USE_MISC
	return st->st_mtim;
#else
	struct timespec t;
	t.tv_sec = st->st_mtime;
	t.tv_nsec = st->st_mtimensec;
	return t;
#endif
}

struct timespec o2info_get_stat_ctime(struct stat *st)
{
#ifdef __USE_MISC
	return st->st_ctim;
#else
	struct timespec t;
	t.tv_sec = st->st_ctime;
	t.tv_nsec = st->st_ctimensec;
	return t;
#endif
}

static int *get_prefix(char *str_pattern, int pattern_len)
{
	int i = 1, j = 0;
	int *prefix = (int *)malloc(pattern_len * sizeof(int));

	prefix[0] = 0;

	while (i < pattern_len) {

		if (str_pattern[i] == str_pattern[j])
			prefix[i] = ++j;
		else {
			j = 0;
			prefix[i] = j;
		}

		i++;
	}

	return prefix;
}

static int kmp(const char *str_pattern, int pattern_len,
	       const char *str_target, int target_len,
	       int *prefix)
{
	int i = 0;
	int j = 0;

	if (!prefix)
		return -1;

	while ((i < pattern_len) && (j < target_len)) {

		if ((j == 0) || (str_pattern[i] == str_target[j])) {
			i++;
			j++;
		} else
			j = prefix[j];
	}

	if (prefix) {
		free(prefix);
		prefix = NULL;
	}

	if (j == target_len)
		return i - j + 1;
	else
		return -1;
}

static int print_nsec_to_htime(char *htime, unsigned long nsec,
			       const char *stuff)
{
	char *s_nsec;
	int index = 0, ret = 0;
	int *prefix = get_prefix(htime, strlen(htime));

	s_nsec = (char *)malloc(strlen(stuff) + 1);
	snprintf(s_nsec, strlen(stuff) + 1, "%lu", nsec);

	index = kmp(htime, strlen(htime), stuff, strlen(stuff), prefix);
	if (index < 0) {
		ret = -1;
		goto bail;
	}

	strncpy(&htime[index], s_nsec, strlen(stuff));

bail:
	if (s_nsec)
		free(s_nsec);

	return 0;
}

int o2info_get_human_time(char **htime, struct timespec t)
{
	struct tm const *tm = localtime(&t.tv_sec);
	int ret, size;

	size = strlen("YYYY-MM-DD HH:MM:SS.NNNNNNNNN +ZZZZ") + 1;
	*htime = (char *)malloc(size);

	strftime(*htime, size, "%Y-%m-%d %H:%M:%S.NNNNNNNNN %z", tm);
	ret = print_nsec_to_htime(*htime, t.tv_nsec, "NNNNNNNNN");
	if (ret < 0) {
		errorf("print n_seconds failed.");
		return -1;
	}

	return 0;
}

int o2info_get_human_path(mode_t st_mode, const char *path, char **h_path)
{
	int rc;
	char link[PATH_MAX];
	char tmp_path[PATH_MAX * 2 + 4];

	if (!S_ISLNK(st_mode))
		*h_path = strdup(path);
	else {
		rc = readlink(path, link, PATH_MAX);
		if (rc < 0) {
			rc = errno;
			tcom_err(rc, "while readlink %s", path);
			return -1;
		} else
			link[rc] = '\0';

		strncpy(tmp_path, path, PATH_MAX);
		strcat(tmp_path, " -> ");
		strcat(tmp_path, link);

		*h_path = strdup(tmp_path);
	}

	return 0;
}
