/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * o2dlm.c
 *
 * Defines the userspace locking api
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

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>

#include <sys/statfs.h>
#include <string.h>

#include <kernel-list.h>

#include <o2dlm.h>

#define USER_DLMFS_MAGIC	0x76a9f425

static errcode_t o2dlm_lock_nochecks(struct o2dlm_ctxt *ctxt,
				     const char *lockid,
				     int lockflags,
				     enum o2dlm_lock_level level);

static errcode_t o2dlm_generate_random_value(int64_t *value)
{
	int randfd = 0;
	int readlen = sizeof(*value);

	if ((randfd = open("/dev/urandom", O_RDONLY)) == -1)
		return O2DLM_ET_RANDOM;

	if (read(randfd, value, readlen) != readlen)
		return O2DLM_ET_RANDOM;

	close(randfd);

	return 0;
}

static errcode_t o2dlm_alloc_ctxt(const char *mnt_path,
				  const char *dirname,
				  struct o2dlm_ctxt **dlm_ctxt)
{
	errcode_t err;
	struct o2dlm_ctxt *ctxt;
	int64_t rand;
	int len;

	err = o2dlm_generate_random_value(&rand);
	if (err)
		return err;

	ctxt = malloc(sizeof(*ctxt));
	if (!ctxt)
		return O2DLM_ET_NO_MEMORY;

	len = snprintf(ctxt->ct_ctxt_lock_name, O2DLM_LOCK_ID_MAX_LEN,
		       ".%016"PRIx64, rand);
	if (len == O2DLM_LOCK_ID_MAX_LEN) {
		err = O2DLM_ET_NAME_TOO_LONG;
		goto exit_and_free;
	} else if (len < 0) {
		err = O2DLM_ET_OUTPUT_ERROR;
		goto exit_and_free;
	}

	INIT_LIST_HEAD(&ctxt->ct_locks);

	len = snprintf(ctxt->ct_domain_path, PATH_MAX + 1, "%s/%s",
		       mnt_path, dirname);
	if (len == (PATH_MAX + 1)) {
		err = O2DLM_ET_BAD_DOMAIN_DIR;
		goto exit_and_free;
	} else if (len < 0) {
		err = O2DLM_ET_OUTPUT_ERROR;
		goto exit_and_free;
	}

	*dlm_ctxt = ctxt;
	err = 0;
exit_and_free:
	if (err)
		free(ctxt);
	return err;
}

static void o2dlm_free_ctxt(struct o2dlm_ctxt *ctxt)
{
	free(ctxt);
}

static errcode_t o2dlm_check_user_dlmfs(const char *dlmfs_path)
{
	struct statfs statfs_buf;
	struct stat stat_buf;
	int ret, fd;

	fd = open(dlmfs_path, O_RDONLY);
	if (fd < 0)
		return O2DLM_ET_OPEN_DLM_DIR;

	ret = fstat(fd, &stat_buf);
	if (ret) {
		close(fd);
		return O2DLM_ET_STATFS;
	}

	if (!S_ISDIR(stat_buf.st_mode)) {
		close(fd);
		return O2DLM_ET_NO_FS_DIR;
	}

	ret = fstatfs(fd, &statfs_buf);
	if (ret) {
		close(fd);
		return O2DLM_ET_STATFS;
	}

	close(fd);

	if (statfs_buf.f_type != USER_DLMFS_MAGIC)
		return O2DLM_ET_NO_FS;

	return 0;
}

static errcode_t o2dlm_check_domain_dir(struct o2dlm_ctxt *ctxt)
{
	int status;
	char *dirpath = ctxt->ct_domain_path;
	struct stat st;

	status = stat(dirpath, &st);
	if (status) {
		if (errno == ENOENT)
			return O2DLM_ET_NO_DOMAIN_DIR;
		return O2DLM_ET_BAD_DOMAIN_DIR;
	}

	if (!S_ISDIR(st.st_mode))
		return O2DLM_ET_BAD_DOMAIN_DIR;

	return 0;
}

#define O2DLM_DOMAIN_DIR_MODE 0755

static errcode_t o2dlm_create_domain(struct o2dlm_ctxt *ctxt)
{
	int status;
	char *dirpath = ctxt->ct_domain_path;

	status = mkdir(dirpath, O2DLM_DOMAIN_DIR_MODE);
	if (status)
		return O2DLM_ET_DOMAIN_CREATE;
	return 0;
}

static errcode_t o2dlm_delete_domain_dir(struct o2dlm_ctxt *ctxt)
{
	int ret;

	ret = rmdir(ctxt->ct_domain_path);
	if (ret) {
		if (errno == ENOTEMPTY)
			return O2DLM_ET_BUSY_DOMAIN_DIR;
		return O2DLM_ET_DOMAIN_DESTROY;
	}
	return 0;
}

errcode_t o2dlm_initialize(const char *dlmfs_path,
			   const char *domain_name,
			   struct o2dlm_ctxt **dlm_ctxt)
{
	errcode_t ret, dir_created = 0;
	struct o2dlm_ctxt *ctxt;

	if (!dlmfs_path || !domain_name || !dlm_ctxt)
		return O2DLM_ET_INVALID_ARGS;

	if (strlen(domain_name) >= O2DLM_DOMAIN_MAX_LEN)
		return O2DLM_ET_NAME_TOO_LONG;

	if ((strlen(dlmfs_path) + strlen(domain_name)) >
	    O2DLM_MAX_FULL_DOMAIN_PATH)
		return O2DLM_ET_NAME_TOO_LONG;

	ret = o2dlm_check_user_dlmfs(dlmfs_path);
	if (ret)
		return ret;

	ret = o2dlm_alloc_ctxt(dlmfs_path, domain_name, &ctxt);
	if (ret)
		return ret;

	ret = o2dlm_check_domain_dir(ctxt);
	if (ret) {
		if (ret != O2DLM_ET_NO_DOMAIN_DIR) {
			o2dlm_free_ctxt(ctxt);
			return ret;
		}

		/* the domain does not yet exist - create it ourselves. */
		ret = o2dlm_create_domain(ctxt);
		if (ret) {
			o2dlm_free_ctxt(ctxt);
			return ret;
		}
		dir_created = 1;
	}

	/* What we want to do here is create a lock which we'll hold
	 * open for the duration of this context. This way if another
	 * process won't be able to shut down this domain underneath
	 * us. */
	ret = o2dlm_lock_nochecks(ctxt, ctxt->ct_ctxt_lock_name, 0,
				  O2DLM_LEVEL_PRMODE);
	if (ret) {
		if (dir_created)
			o2dlm_delete_domain_dir(ctxt); /* best effort
							* cleanup. */
		o2dlm_free_ctxt(ctxt);
		return ret;
	}

	*dlm_ctxt = ctxt;
	return 0;
}

static errcode_t o2dlm_full_path(char *path,
				 struct o2dlm_ctxt *ctxt,
				 const char *filename)
{
	int ret;
	int len = PATH_MAX + 1;

	ret = snprintf(path, len, "%s/%s", ctxt->ct_domain_path, filename);
	if (ret == len)
		return O2DLM_ET_NAME_TOO_LONG;
	else if (ret < 0)
		return O2DLM_ET_OUTPUT_ERROR;
	return 0;
}

static struct o2dlm_lock_res *o2dlm_find_lock_res(struct o2dlm_ctxt *ctxt,
						  const char *lockid)
{
	struct o2dlm_lock_res *lockres;
	struct list_head *p;

	list_for_each(p, &ctxt->ct_locks) {
		lockres = list_entry(p, struct o2dlm_lock_res, l_list);
		if (!strcmp(lockid, lockres->l_id))
			return lockres;
	}
	return NULL;
}

static int o2dlm_translate_lock_flags(enum o2dlm_lock_level level,
				      int lockflags)
{
	int flags;

	switch (level) {
	case O2DLM_LEVEL_PRMODE:
		flags = O_RDONLY;
		break;
	case O2DLM_LEVEL_EXMODE:
		flags = O_WRONLY;
		break;
	default:
		flags = 0;
	}

	if (lockflags & O2DLM_TRYLOCK)
		flags |= O_NONBLOCK;

	return flags;
}

static struct o2dlm_lock_res *o2dlm_new_lock_res(const char *id,
						 enum o2dlm_lock_level level,
						 int flags)
{
	struct o2dlm_lock_res *lockres;

	lockres = malloc(sizeof(*lockres));
	if (lockres) {
		memset(lockres, 0, sizeof(*lockres));

		INIT_LIST_HEAD(&lockres->l_list);

		strncpy(lockres->l_id, id, O2DLM_LOCK_ID_MAX_LEN);

		lockres->l_flags = flags;
		lockres->l_level = level;
		lockres->l_fd    = -1;
	}
	return lockres;
}

#define O2DLM_OPEN_MODE         0664

/* Use this internally to avoid the check for a reserved name */
static errcode_t o2dlm_lock_nochecks(struct o2dlm_ctxt *ctxt,
				     const char *lockid,
				     int lockflags,
				     enum o2dlm_lock_level level)
{
	int ret, flags, fd;
	char *path;
	struct o2dlm_lock_res *lockres;

	if (strlen(lockid) >= O2DLM_LOCK_ID_MAX_LEN)
		return O2DLM_ET_INVALID_LOCK_NAME;

	if (level != O2DLM_LEVEL_PRMODE && level != O2DLM_LEVEL_EXMODE)
		return O2DLM_ET_INVALID_LOCK_LEVEL;

	lockres = o2dlm_find_lock_res(ctxt, lockid);
	if (lockres)
		return O2DLM_ET_RECURSIVE_LOCK;

	path = malloc(PATH_MAX + 1);
	if (!path)
		return O2DLM_ET_NO_MEMORY;

	ret = o2dlm_full_path(path, ctxt, lockid);
	if (ret) {
		free(path);
		return ret;
	}

	lockflags &= O2DLM_VALID_FLAGS;
	flags = o2dlm_translate_lock_flags(level, lockflags);

	lockres = o2dlm_new_lock_res(lockid, level, flags);
	if (!lockres) {
		free(path);
		return O2DLM_ET_NO_MEMORY;
	}

	fd = open(path, flags|O_CREAT, O2DLM_OPEN_MODE);
	if (fd < 0) {
		free(path);
		free(lockres);
		return O2DLM_ET_LOCKING;
	}

	lockres->l_flags = lockflags;
	lockres->l_fd = fd;

	list_add_tail(&lockres->l_list, &ctxt->ct_locks);

	free(path);

	return 0;
}

errcode_t o2dlm_lock(struct o2dlm_ctxt *ctxt,
		     const char *lockid,
		     int lockflags,
		     enum o2dlm_lock_level level)
{
	if (!ctxt || !lockid)
		return O2DLM_ET_INVALID_ARGS;

	/* names starting with '.' are reserved. */
	if (lockid[0] == '.')
		return O2DLM_ET_INVALID_LOCK_NAME;

	return o2dlm_lock_nochecks(ctxt, lockid, lockflags, level);
}

static errcode_t o2dlm_unlock_lock_res(struct o2dlm_ctxt *ctxt,
				      struct o2dlm_lock_res *lockres)
{
	int ret, len = PATH_MAX + 1;
	char *path;

	/* This does the actual unlock. */
	close(lockres->l_fd);

	/* From here on down, we're trying to unlink the lockres file
	 * from the dlm file system. Note that EBUSY from unlink is
	 * not a fatal error here -- it simply means that the lock is
	 * in use by some other process. */
	path = malloc(len);
	if (!path)
		return O2DLM_ET_NO_MEMORY;

	ret = o2dlm_full_path(path, ctxt, lockres->l_id);
	if (ret) {
		free(path);
		return ret;
	}

	ret = unlink(path);
	free (path);
	if (ret) {
		if (errno == EBUSY)
			return O2DLM_ET_BUSY_LOCK;
		return O2DLM_ET_UNLINK;
	}
	return 0;
}

errcode_t o2dlm_unlock(struct o2dlm_ctxt *ctxt,
		       char *lockid)
{
	int ret;
	struct o2dlm_lock_res *lockres = NULL;

	if (!ctxt || !lockid)
		return O2DLM_ET_INVALID_ARGS;

	lockres = o2dlm_find_lock_res(ctxt, lockid);
	if (!lockres)
		return O2DLM_ET_UNKNOWN_LOCK;

	list_del(&lockres->l_list);

	ret = o2dlm_unlock_lock_res(ctxt, lockres);

	free(lockres);

	if (ret && (ret != O2DLM_ET_BUSY_LOCK))
		return ret;
	return 0;
}

static errcode_t o2dlm_unlink_all(struct o2dlm_ctxt *ctxt)
{
	int ret;
	char *name;
        DIR *dir;
        struct dirent *de;

	name = malloc(PATH_MAX + 1);
	if (!name)
		return O2DLM_ET_NO_MEMORY;

	dir = opendir(ctxt->ct_domain_path);
	if (!dir) {
		free(name);
		return O2DLM_ET_DOMAIN_DIR;
	}

	de = readdir(dir);
	while(de) {
		if ((strlen(de->d_name) == 1) &&
		    (de->d_name[0] == '.'))
			goto next;
		if ((strlen(de->d_name) == 2) &&
		    (de->d_name[0] == '.') &&
		    (de->d_name[1] == '.'))
			goto next;

		ret = o2dlm_full_path(name, ctxt, de->d_name);
		if (ret)
			goto close_and_free;

		ret = unlink(name);
		if (ret) {
			if (errno != EBUSY) {
				ret = O2DLM_ET_UNLINK;
				goto close_and_free;
			}
		}
next:
		de = readdir(dir);
	}

	ret = 0;
close_and_free:
	closedir(dir);
	free(name);
	return ret;
}

errcode_t o2dlm_destroy(struct o2dlm_ctxt *ctxt)
{
	int ret;
	int error = 0;
	struct o2dlm_lock_res *lockres;
        struct list_head *p, *n;

	if (!ctxt)
		return O2DLM_ET_INVALID_ARGS;

	list_for_each_safe(p, n, &ctxt->ct_locks) {
		lockres = list_entry(p, struct o2dlm_lock_res, l_list);
		list_del(&lockres->l_list);

		ret = o2dlm_unlock_lock_res(ctxt, lockres);
		if (ret && (ret != O2DLM_ET_BUSY_LOCK))
			error = O2DLM_ET_FAILED_UNLOCKS;
		free(lockres);
	}
	if (error)
		goto free_and_exit;

	ret = o2dlm_unlink_all(ctxt);
	if (ret && ret != O2DLM_ET_BUSY_LOCK) {
		error = ret;
		goto free_and_exit;
	}

	ret = o2dlm_delete_domain_dir(ctxt);
	if (ret && ret != O2DLM_ET_BUSY_DOMAIN_DIR)
		error = ret;

free_and_exit:
	o2dlm_free_ctxt(ctxt);
	return error;
}
