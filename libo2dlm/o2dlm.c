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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301 USA.
 *
 * Authors: Mark Fasheh <mark.fasheh@oracle.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <assert.h>
#include <inttypes.h>
#include <sys/statfs.h>
#include <string.h>
#include <dlfcn.h>

#include <errno.h>
#include <libdlm.h>

#include "o2dlm/o2dlm.h"
#include "ocfs2-kernel/kernel-list.h"

#define USER_DLMFS_MAGIC	0x76a9f425

struct o2dlm_lock_bast
{
	struct list_head	b_bucket; /* to hang us off of the bast list */
	int			b_fd;     /* the fd of the lock file */
	void			(*b_bast)(void *);
	void			*b_arg;   /* Argument to ->b_bast() */
};

struct o2dlm_lock_res
{
	struct list_head      l_bucket; /* to hang us off the locks list */
	char                  l_id[O2DLM_LOCK_ID_MAX_LEN]; /* 32 byte,
							    * null
							    * terminated
							    * string */
	int                   l_flags; /* limited set of flags */
	enum o2dlm_lock_level l_level; /* either PR or EX */
	int                   l_fd;    /* the fd returned by the open call */
#ifdef HAVE_FSDLM
	struct dlm_lksb       l_lksb;  /* lksb for fsdlm locking */
	char                  l_lvb[DLM_LVB_LEN]; /* LVB for fsdlm */
#endif
};

struct o2dlm_ctxt
{
	int		ct_classic;
	int		ct_supports_bast;
	struct list_head *ct_hash;
	struct list_head *ct_bast_hash;
	unsigned int     ct_hash_size;
	char             ct_domain_path[O2DLM_MAX_FULL_DOMAIN_PATH]; /* domain
								      * dir */
	char             ct_ctxt_lock_name[O2DLM_LOCK_ID_MAX_LEN];
#ifdef HAVE_FSDLM
	void		*ct_lib_handle;
	dlm_lshandle_t   ct_handle;
#endif
};


static errcode_t o2dlm_lock_nochecks(struct o2dlm_ctxt *ctxt,
				     const char *lockid,
				     int lockflags,
				     enum o2dlm_lock_level level);
static errcode_t o2dlm_unlock_lock_res(struct o2dlm_ctxt *ctxt,
				       struct o2dlm_lock_res *lockres);

static errcode_t o2dlm_generate_random_value(int64_t *value)
{
	int randfd = 0;
	int readlen = sizeof(*value);

	if ((randfd = open("/dev/urandom", O_RDONLY)) == -1)
		return O2DLM_ET_RANDOM;

	if (read(randfd, value, readlen) != readlen) {
		close(randfd);
		return O2DLM_ET_RANDOM;
	}
	close(randfd);

	return 0;
}

static void o2dlm_free_ctxt(struct o2dlm_ctxt *ctxt)
{
	if (ctxt->ct_hash)
		free(ctxt->ct_hash);
	if (ctxt->ct_bast_hash)
		free(ctxt->ct_bast_hash);
	free(ctxt);
}

#define O2DLM_DEFAULT_HASH_SIZE 4096

static errcode_t o2dlm_alloc_ctxt(const char *mnt_path,
				  const char *dirname,
				  struct o2dlm_ctxt **dlm_ctxt)
{
	errcode_t err;
	struct o2dlm_ctxt *ctxt;
	int64_t rand;
	int len, i;

	err = o2dlm_generate_random_value(&rand);
	if (err)
		return err;

	ctxt = malloc(sizeof(*ctxt));
	if (!ctxt)
		return O2DLM_ET_NO_MEMORY;

	memset(ctxt, 0, sizeof(*ctxt));
	ctxt->ct_supports_bast = -1;
	ctxt->ct_hash_size = O2DLM_DEFAULT_HASH_SIZE;

	ctxt->ct_hash = calloc(ctxt->ct_hash_size, sizeof(struct list_head));
	ctxt->ct_bast_hash = calloc(ctxt->ct_hash_size,
				    sizeof(struct list_head));
	if (!ctxt->ct_hash || !ctxt->ct_bast_hash) {
		err = O2DLM_ET_NO_MEMORY;
		goto exit_and_free;
	}

	for(i = 0; i < ctxt->ct_hash_size; i++) {
		INIT_LIST_HEAD(&ctxt->ct_hash[i]);
		INIT_LIST_HEAD(&ctxt->ct_bast_hash[i]);
	}

	len = snprintf(ctxt->ct_ctxt_lock_name, O2DLM_LOCK_ID_MAX_LEN,
		       ".%016"PRIx64, rand);
	if (len == O2DLM_LOCK_ID_MAX_LEN) {
		err = O2DLM_ET_NAME_TOO_LONG;
		goto exit_and_free;
	} else if (len < 0) {
		err = O2DLM_ET_OUTPUT_ERROR;
		goto exit_and_free;
	}

	if (mnt_path) {
		ctxt->ct_classic = 1;
		len = snprintf(ctxt->ct_domain_path, PATH_MAX + 1, "%s/%s",
			       mnt_path, dirname);
	} else {
		ctxt->ct_classic = 0;
		len = snprintf(ctxt->ct_domain_path, PATH_MAX + 1, "%s",
			       dirname);
	}
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
		o2dlm_free_ctxt(ctxt);
	return err;
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

/* 
 * Most of this hash function taken from dcache.h. It has the
 * following copyright line at the top:
 *	
 * (C) Copyright 1997 Thomas Schoebel-Theuer,
 * with heavy changes by Linus Torvalds
 */
static inline unsigned long
partial_name_hash(unsigned long c, unsigned long prevhash)
{
	return (prevhash + (c << 4) + (c >> 4)) * 11;
}

static inline unsigned int o2dlm_hash_lockname(struct o2dlm_ctxt *ctxt,
					       const char *name)
{
	unsigned long hash = 0;
	unsigned int bucket;

	while(*name)
		hash = partial_name_hash(*name++, hash);

	bucket = (unsigned int) hash % ctxt->ct_hash_size;

	assert(bucket < ctxt->ct_hash_size);

	return bucket;
}

static struct o2dlm_lock_res *o2dlm_find_lock_res(struct o2dlm_ctxt *ctxt,
						  const char *lockid)
{
	struct o2dlm_lock_res *lockres;
	struct list_head *p;
	unsigned int bucket;

	bucket = o2dlm_hash_lockname(ctxt, lockid);

	list_for_each(p, &ctxt->ct_hash[bucket]) {
		lockres = list_entry(p, struct o2dlm_lock_res, l_bucket);
		if (!strcmp(lockid, lockres->l_id))
			return lockres;
	}
	return NULL;
}

static void o2dlm_insert_lock_res(struct o2dlm_ctxt *ctxt,
				  struct o2dlm_lock_res *lockres)
{
	unsigned int bucket;

	bucket = o2dlm_hash_lockname(ctxt, lockres->l_id);

	list_add_tail(&lockres->l_bucket, &ctxt->ct_hash[bucket]);
}

static inline void o2dlm_remove_lock_res(struct o2dlm_lock_res *lockres)
{
	list_del(&lockres->l_bucket);
	INIT_LIST_HEAD(&lockres->l_bucket);
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
		flags = O_RDWR;
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

		INIT_LIST_HEAD(&lockres->l_bucket);

		strncpy(lockres->l_id, id, O2DLM_LOCK_ID_MAX_LEN);

		lockres->l_flags = flags;
		lockres->l_level = level;
		lockres->l_fd    = -1;
#ifdef HAVE_FSDLM
		lockres->l_lksb.sb_lvbptr = lockres->l_lvb;
		memset(lockres->l_lksb.sb_lvbptr, 0, DLM_LVB_LEN);
#endif
	}
	return lockres;
}


static inline unsigned int o2dlm_hash_bast(struct o2dlm_ctxt *ctxt, int fd)
{
	return fd % ctxt->ct_hash_size;
}

static struct o2dlm_lock_bast *o2dlm_find_bast(struct o2dlm_ctxt *ctxt, int fd)
{
	struct o2dlm_lock_bast *bast;
	struct list_head *p;
	unsigned int bucket;

	bucket = o2dlm_hash_bast(ctxt, fd);

	list_for_each(p, &ctxt->ct_bast_hash[bucket]) {
		bast = list_entry(p, struct o2dlm_lock_bast, b_bucket);
		if (fd == bast->b_fd)
			return bast;
	}
	return NULL;
}

static void o2dlm_insert_bast(struct o2dlm_ctxt *ctxt,
			      struct o2dlm_lock_bast *bast)
{
	unsigned int bucket;

	bucket = o2dlm_hash_bast(ctxt, bast->b_fd);

	list_add_tail(&bast->b_bucket, &ctxt->ct_bast_hash[bucket]);
}

static inline void o2dlm_remove_bast(struct o2dlm_lock_bast *bast)
{
	list_del(&bast->b_bucket);
	INIT_LIST_HEAD(&bast->b_bucket);
}

static struct o2dlm_lock_bast *o2dlm_new_bast(int fd,
					      void (*bast_func)(void *),
					      void *bastarg)
{
	struct o2dlm_lock_bast *bast;

	bast = malloc(sizeof(*bast));
	if (bast) {
		memset(bast, 0, sizeof(*bast));

		INIT_LIST_HEAD(&bast->b_bucket);

		bast->b_bast = bast_func;
		bast->b_arg = bastarg;
		bast->b_fd = fd;
	}
	return bast;
}

#define O2DLM_OPEN_MODE         0664


/*
 * Classic o2dlm
 */

static errcode_t o2dlm_lock_classic(struct o2dlm_ctxt *ctxt,
				    const char *lockid, int lockflags,
				    enum o2dlm_lock_level level)
{
	int ret, flags, fd;
	char *path;
	struct o2dlm_lock_res *lockres;

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
		if ((lockflags & O2DLM_TRYLOCK) &&
		    (errno == ETXTBSY))
			return O2DLM_ET_TRYLOCK_FAILED;
		return O2DLM_ET_LOCKING;
	}

	lockres->l_flags = lockflags;
	lockres->l_fd = fd;

	o2dlm_insert_lock_res(ctxt, lockres);

	free(path);

	return 0;
}

static errcode_t o2dlm_drop_lock_classic(struct o2dlm_ctxt *ctxt,
					 const char *lockid)
{
	int ret, len = PATH_MAX + 1;
	char *path;

	/*
	 * We're trying to unlink the lockres file from the dlm file
	 * system. Note that EBUSY from unlink is not a fatal error here
	 * -- it simply means that the lock is in use by some other
	 * process.
	 * */
	path = malloc(len);
	if (!path)
		return O2DLM_ET_NO_MEMORY;

	ret = o2dlm_full_path(path, ctxt, lockid);
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

static errcode_t o2dlm_unlock_lock_res_classic(struct o2dlm_ctxt *ctxt,
					       struct o2dlm_lock_res *lockres)
{
	close(lockres->l_fd);
	return 0;
}

static errcode_t o2dlm_read_lvb_classic(struct o2dlm_ctxt *ctxt,
					char *lockid,
					char *lvb,
					unsigned int len,
					unsigned int *bytes_read)
{
	int fd, ret;
	struct o2dlm_lock_res *lockres;

	lockres = o2dlm_find_lock_res(ctxt, lockid);
	if (!lockres)
		return O2DLM_ET_UNKNOWN_LOCK;

	fd = lockres->l_fd;

	ret = lseek(fd, 0, SEEK_SET);
	if (ret < 0)
		return O2DLM_ET_SEEK;

	ret = read(fd, lvb, len);
	if (ret < 0)
		return O2DLM_ET_LVB_READ;
	if (!ret)
		return O2DLM_ET_LVB_INVALID;

	if (bytes_read)
		*bytes_read = ret;

	return 0;
}

static errcode_t o2dlm_write_lvb_classic(struct o2dlm_ctxt *ctxt,
					 char *lockid,
					 const char *lvb,
					 unsigned int len,
					 unsigned int *bytes_written)
{
	int fd, ret;
	struct o2dlm_lock_res *lockres;

	if (!ctxt || !lockid || !lvb)
		return O2DLM_ET_INVALID_ARGS;

	lockres = o2dlm_find_lock_res(ctxt, lockid);
	if (!lockres)
		return O2DLM_ET_UNKNOWN_LOCK;

	fd = lockres->l_fd;

	ret = lseek(fd, 0, SEEK_SET);
	if (ret < 0)
		return O2DLM_ET_SEEK;

	ret = write(fd, lvb, len);
	if (ret < 0)
		return O2DLM_ET_LVB_WRITE;

	if (bytes_written)
		*bytes_written = ret;

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

static errcode_t o2dlm_destroy_classic(struct o2dlm_ctxt *ctxt)
{
	int ret, i;
	int error = 0;
	struct o2dlm_lock_res *lockres;
	struct o2dlm_lock_bast *bast;
        struct list_head *p, *n, *bucket;

	for(i = 0; i < ctxt->ct_hash_size; i++) {
		bucket = &ctxt->ct_bast_hash[i];
		list_for_each_safe(p, n, bucket) {
			bast = list_entry(p, struct o2dlm_lock_bast,
					  b_bucket);
			o2dlm_remove_bast(bast);
			free(bast);
		}

		bucket = &ctxt->ct_hash[i];
		list_for_each_safe(p, n, bucket) {
			lockres = list_entry(p, struct o2dlm_lock_res,
					     l_bucket);

			o2dlm_remove_lock_res(lockres);

			ret = o2dlm_unlock_lock_res(ctxt, lockres);
			if (ret && (ret != O2DLM_ET_BUSY_LOCK))
				error = O2DLM_ET_FAILED_UNLOCKS;
			free(lockres);
		}
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

static errcode_t o2dlm_initialize_classic(const char *dlmfs_path,
					  const char *domain_name,
					  struct o2dlm_ctxt **dlm_ctxt)
{
	errcode_t ret, dir_created = 0;
	struct o2dlm_ctxt *ctxt;

	if (!dlmfs_path)
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

/*
 * fsdlm operations
 */

#ifdef HAVE_FSDLM
/* Dynamic symbols */
static dlm_lshandle_t (*fsdlm_create_lockspace)(const char *name,
						mode_t mode);
static int (*fsdlm_release_lockspace)(const char *name,
				      dlm_lshandle_t ls,
				      int force);
static int (*fsdlm_ls_lock_wait)(dlm_lshandle_t ls,
				 uint32_t mode,
				 struct dlm_lksb *lksb,
				 uint32_t flags,
				 const void *name,
				 unsigned int namelen,
				 uint32_t parent,
				 void *bastarg,
				 void (*bastaddr)(void *bastarg),
				 void *range);
static int (*fsdlm_ls_unlock_wait)(dlm_lshandle_t ls,
				   uint32_t lkid,
				   uint32_t flags,
				   struct dlm_lksb *lksb);

static errcode_t load_fsdlm(struct o2dlm_ctxt *ctxt)
{
	errcode_t ret = O2DLM_ET_SERVICE_UNAVAILABLE;

	if (ctxt->ct_lib_handle) {
		ret = 0;
		goto out;
	}

	ctxt->ct_lib_handle = dlopen("libdlm_lt.so",
				     RTLD_NOW | RTLD_LOCAL);
	if (!ctxt->ct_lib_handle)
		goto out;

#define load_sym(_sym) do {				\
	fs ## _sym = dlsym(ctxt->ct_lib_handle, #_sym);	\
	if (! fs ## _sym)				\
		goto out;				\
} while (0)

	load_sym(dlm_create_lockspace);
	load_sym(dlm_release_lockspace);
	load_sym(dlm_ls_lock_wait);
	load_sym(dlm_ls_unlock_wait);

	ret = 0;

out:
	if (ret && ctxt->ct_lib_handle) {
		dlclose(ctxt->ct_lib_handle);
		ctxt->ct_lib_handle = NULL;
	}
	return ret;
}

static void to_fsdlm_lock(enum o2dlm_lock_level level, int lockflags,
			  uint32_t *fsdlm_mode, uint32_t *fsdlm_flags)
{
	switch (level) {
		case O2DLM_LEVEL_PRMODE:
			*fsdlm_mode = LKM_PRMODE;
			break;
		case O2DLM_LEVEL_EXMODE:
			*fsdlm_mode = LKM_EXMODE;
			break;
		default:
			*fsdlm_mode = LKM_NLMODE;
			break;
	}

	if (lockflags & O2DLM_TRYLOCK)
		*fsdlm_flags = LKF_NOQUEUE;
	else
		*fsdlm_flags = 0;
}

static errcode_t o2dlm_lock_fsdlm(struct o2dlm_ctxt *ctxt,
				  const char *lockid, int lockflags,
				  enum o2dlm_lock_level level)
{
	int rc;
	errcode_t ret = 0;
	struct o2dlm_lock_res *lockres;
	uint32_t mode, flags;

	if (!fsdlm_ls_lock_wait)
		return O2DLM_ET_SERVICE_UNAVAILABLE;

	lockres = o2dlm_find_lock_res(ctxt, lockid);
	if (lockres)
		return O2DLM_ET_RECURSIVE_LOCK;

	lockflags &= O2DLM_VALID_FLAGS;
	lockres = o2dlm_new_lock_res(lockid, level, lockflags);
	if (!lockres)
		return O2DLM_ET_NO_MEMORY;

	to_fsdlm_lock(level, lockflags, &mode, &flags);
	flags |= LKF_VALBLK;  /* Always use LVBs */
	rc = fsdlm_ls_lock_wait(ctxt->ct_handle, mode, &lockres->l_lksb,
				flags, lockid, strlen(lockid),
				0, NULL, NULL, NULL);
	if (rc)
		rc = errno;
	else
		rc = lockres->l_lksb.sb_status;
	switch (rc) {
		case 0:
			/* Success! */
			break;
		case EAGAIN:
			if (lockflags & O2DLM_TRYLOCK)
				ret = O2DLM_ET_TRYLOCK_FAILED;
			else
				ret = O2DLM_ET_LOCKING;
			break;
		case EINVAL:
			ret = O2DLM_ET_INVALID_ARGS;
			break;
		case ENOMEM:
			ret = O2DLM_ET_NO_MEMORY;
			break;
		case ECANCEL:
			ret = O2DLM_ET_LOCKING;
			break;
		default:
			ret = O2DLM_ET_INTERNAL_FAILURE;
			break;
	}

	if (!ret)
		o2dlm_insert_lock_res(ctxt, lockres);
	else
		free(lockres);

	return ret;
}

static errcode_t o2dlm_unlock_lock_res_fsdlm(struct o2dlm_ctxt *ctxt,
					     struct o2dlm_lock_res *lockres)
{
	int rc;
	errcode_t ret = 0;

	if (!fsdlm_ls_unlock_wait)
		return O2DLM_ET_SERVICE_UNAVAILABLE;

	rc = fsdlm_ls_unlock_wait(ctxt->ct_handle, lockres->l_lksb.sb_lkid,
				  LKF_VALBLK, &lockres->l_lksb);
	if (rc)
		rc = errno;
	else
		rc = lockres->l_lksb.sb_status;

	switch (rc) {
		case 0:
		case EUNLOCK:
			/* Success! */
			break;
		case ENOTCONN:
			ret = O2DLM_ET_SERVICE_UNAVAILABLE;
			break;
		case EINVAL:
			ret = O2DLM_ET_INVALID_ARGS;
			break;
		case ENOENT:
			ret = O2DLM_ET_UNKNOWN_LOCK;
			break;
		default:
			ret = O2DLM_ET_INTERNAL_FAILURE;
			break;
	}

	return ret;
}

static errcode_t o2dlm_write_lvb_fsdlm(struct o2dlm_ctxt *ctxt,
				       char *lockid,
				       const char *lvb,
				       unsigned int len,
				       unsigned int *bytes_written)
{
	struct o2dlm_lock_res *lockres;

	if (!ctxt || !lockid || !lvb)
		return O2DLM_ET_INVALID_ARGS;

	lockres = o2dlm_find_lock_res(ctxt, lockid);
	if (!lockres)
		return O2DLM_ET_UNKNOWN_LOCK;

	/* fsdlm only supports DLM_LVB_LEN for userspace locks */
	if (len > DLM_LVB_LEN)
		len = DLM_LVB_LEN;
	memcpy(lockres->l_lksb.sb_lvbptr, lvb, len);
	if (bytes_written)
		*bytes_written = len;

	return 0;
}

static errcode_t o2dlm_read_lvb_fsdlm(struct o2dlm_ctxt *ctxt,
				      char *lockid,
				      char *lvb,
				      unsigned int len,
				      unsigned int *bytes_read)
{
	struct o2dlm_lock_res *lockres;

	lockres = o2dlm_find_lock_res(ctxt, lockid);
	if (!lockres)
		return O2DLM_ET_UNKNOWN_LOCK;

	/* fsdlm only supports DLM_LVB_LEN for userspace locks */
	if (len > DLM_LVB_LEN)
		len = DLM_LVB_LEN;
	memcpy(lvb, lockres->l_lksb.sb_lvbptr, len);
	if (bytes_read)
		*bytes_read = len;

	return 0;
}

static errcode_t o2dlm_initialize_fsdlm(const char *domain_name,
					struct o2dlm_ctxt **dlm_ctxt)
{
	errcode_t ret;
	struct o2dlm_ctxt *ctxt;

	if (strlen(domain_name) >= O2DLM_DOMAIN_MAX_LEN)
		return O2DLM_ET_NAME_TOO_LONG;

	ret = o2dlm_alloc_ctxt(NULL, domain_name, &ctxt);
	if (ret)
		return ret;

	ret = load_fsdlm(ctxt);
	if (ret) {
		o2dlm_free_ctxt(ctxt);
		return ret;
	}

	ctxt->ct_handle = fsdlm_create_lockspace(ctxt->ct_domain_path, 0600);
	if (!ctxt->ct_handle) {
		switch (errno) {
			case EINVAL:
				ret = O2DLM_ET_INVALID_ARGS;
				break;
			case ENOMEM:
				ret = O2DLM_ET_NO_MEMORY;
				break;
			case EEXIST:
				/*
				 * This is a special case for older
				 * versions of fs/dlm.
				 */
				ret = O2DLM_ET_DOMAIN_BUSY;
				break;
			case EACCES:
			case EPERM:
				ret = O2DLM_ET_BAD_DOMAIN_DIR;
				break;
			default:
				ret = O2DLM_ET_INTERNAL_FAILURE;
				break;
		}
		o2dlm_free_ctxt(ctxt);
		return ret;
	}

	/* What we want to do here is create a lock which we'll hold
	 * open for the duration of this context. This way if another
	 * process won't be able to shut down this domain underneath
	 * us. */
	ret = o2dlm_lock_nochecks(ctxt, ctxt->ct_ctxt_lock_name, 0,
				  O2DLM_LEVEL_PRMODE);
	if (ret) {
		/* Ignore the error, we want ret to be propagated */
		fsdlm_release_lockspace(ctxt->ct_domain_path,
					ctxt->ct_handle, 0);
		o2dlm_free_ctxt(ctxt);
		return ret;
	}

	*dlm_ctxt = ctxt;
	return 0;
}

static errcode_t o2dlm_destroy_fsdlm(struct o2dlm_ctxt *ctxt)
{
	int i, rc;
	errcode_t ret, error = 0;
	struct o2dlm_lock_res *lockres;
        struct list_head *p, *n, *bucket;

	if (!fsdlm_release_lockspace)
		return O2DLM_ET_SERVICE_UNAVAILABLE;

	for(i = 0; i < ctxt->ct_hash_size; i++) {
		bucket = &ctxt->ct_hash[i];

		list_for_each_safe(p, n, bucket) {
			lockres = list_entry(p, struct o2dlm_lock_res,
					     l_bucket);

			o2dlm_remove_lock_res(lockres);

			ret = o2dlm_unlock_lock_res(ctxt, lockres);
			if (ret && (ret != O2DLM_ET_BUSY_LOCK))
				error = O2DLM_ET_FAILED_UNLOCKS;
			free(lockres);
		}
	}
	if (error)
		goto free_and_exit;

	rc = fsdlm_release_lockspace(ctxt->ct_domain_path, ctxt->ct_handle,
				     0);
	if (!rc)
		goto free_and_exit;

	switch(errno) {
		case EBUSY:
			/* Do nothing */
			break;
		case EINVAL:
			error = O2DLM_ET_INVALID_ARGS;
			break;
		case ENOMEM:
			error = O2DLM_ET_NO_MEMORY;
			break;
		case EACCES:
		case EPERM:
			error = O2DLM_ET_BAD_DOMAIN_DIR;
			break;
		default:
			error = O2DLM_ET_INTERNAL_FAILURE;
			break;
	}

free_and_exit:
	o2dlm_free_ctxt(ctxt);
	return error;
}
#else  /* HAVE_FSDLM */
static errcode_t o2dlm_lock_fsdlm(struct o2dlm_ctxt *ctxt,
				  const char *lockid, int lockflags,
				  enum o2dlm_lock_level level)
{
	return O2DLM_ET_SERVICE_UNAVAILABLE;
}

static errcode_t o2dlm_unlock_lock_res_fsdlm(struct o2dlm_ctxt *ctxt,
					     struct o2dlm_lock_res *lockres)
{
	return O2DLM_ET_SERVICE_UNAVAILABLE;
}

static errcode_t o2dlm_read_lvb_fsdlm(struct o2dlm_ctxt *ctxt,
				      char *lockid,
				      char *lvb,
				      unsigned int len,
				      unsigned int *bytes_read)
{
	return O2DLM_ET_SERVICE_UNAVAILABLE;
}

static errcode_t o2dlm_write_lvb_fsdlm(struct o2dlm_ctxt *ctxt,
				       char *lockid,
				       const char *lvb,
				       unsigned int len,
				       unsigned int *bytes_written)
{
	return O2DLM_ET_SERVICE_UNAVAILABLE;
}

static errcode_t o2dlm_initialize_fsdlm(const char *domain_name,
					struct o2dlm_ctxt **dlm_ctxt)
{
	return O2DLM_ET_SERVICE_UNAVAILABLE;
}

static errcode_t o2dlm_destroy_fsdlm(struct o2dlm_ctxt *ctxt)
{
	return O2DLM_ET_SERVICE_UNAVAILABLE;
}
#endif  /* HAVE_FSDLM */

/*
 * Public API
 */

/* Use this internally to avoid the check for a reserved name */
static errcode_t o2dlm_lock_nochecks(struct o2dlm_ctxt *ctxt,
				     const char *lockid,
				     int lockflags,
				     enum o2dlm_lock_level level)
{
	if (strlen(lockid) >= O2DLM_LOCK_ID_MAX_LEN)
		return O2DLM_ET_INVALID_LOCK_NAME;

	if (level != O2DLM_LEVEL_PRMODE && level != O2DLM_LEVEL_EXMODE)
		return O2DLM_ET_INVALID_LOCK_LEVEL;

	if (ctxt->ct_classic)
		return o2dlm_lock_classic(ctxt, lockid, lockflags, level);
	else
		return o2dlm_lock_fsdlm(ctxt, lockid, lockflags, level);
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

static errcode_t o2dlm_ctxt_supports_bast(struct o2dlm_ctxt *ctxt)
{
	int support;
	errcode_t ret = 0;

	if (ctxt->ct_supports_bast == -1) {
		ret = o2dlm_supports_bast(&support);
		if (!ret) {
			ctxt->ct_supports_bast = support;
		}
	}

	if (!ret && !ctxt->ct_supports_bast)
		ret = O2DLM_ET_BAST_UNSUPPORTED;
	return ret;
}

errcode_t o2dlm_lock_with_bast(struct o2dlm_ctxt *ctxt,
			       const char *lockid,
			       int lockflags,
			       enum o2dlm_lock_level level,
			       void (*bast_func)(void *bast_arg),
			       void *bast_arg,
			       int *poll_fd)
{
	errcode_t ret;
	struct o2dlm_lock_res *lockres;
	struct o2dlm_lock_bast *bast;

	if (!ctxt->ct_classic)
		return O2DLM_ET_BAST_UNSUPPORTED;
	if (!bast_func || !poll_fd)
		return O2DLM_ET_INVALID_ARGS;

	ret = o2dlm_ctxt_supports_bast(ctxt);
	if (ret)
		return ret;

	ret = o2dlm_lock(ctxt, lockid, lockflags, level);
	if (ret)
		return ret;

	lockres = o2dlm_find_lock_res(ctxt, lockid);
	if (!lockres) {
		o2dlm_unlock(ctxt, lockid);
		return O2DLM_ET_INTERNAL_FAILURE;
	}

	bast = o2dlm_new_bast(lockres->l_fd, bast_func, bast_arg);
	if (!bast) {
		o2dlm_unlock(ctxt, lockid);
		return O2DLM_ET_NO_MEMORY;
	}

	o2dlm_insert_bast(ctxt, bast);
	*poll_fd = lockres->l_fd;
	return 0;
}

static errcode_t o2dlm_unlock_lock_res(struct o2dlm_ctxt *ctxt,
				       struct o2dlm_lock_res *lockres)
{
	if (ctxt->ct_classic)
		return o2dlm_unlock_lock_res_classic(ctxt, lockres);
	else
		return o2dlm_unlock_lock_res_fsdlm(ctxt, lockres);
}

/*
 * Dropping locks is only available on dlmfs.  No one should be using
 * libdlm if they can help it.
 */
errcode_t o2dlm_drop_lock(struct o2dlm_ctxt *ctxt, const char *lockid)
{
	if (!ctxt || !lockid)
		return O2DLM_ET_INVALID_ARGS;

	if (o2dlm_find_lock_res(ctxt, lockid))
		return O2DLM_ET_BUSY_LOCK;

	if (ctxt->ct_classic)
		return o2dlm_drop_lock_classic(ctxt, lockid);
	else
		return O2DLM_ET_SERVICE_UNAVAILABLE;
}

errcode_t o2dlm_unlock(struct o2dlm_ctxt *ctxt,
		       const char *lockid)
{
	int ret;
	struct o2dlm_lock_res *lockres;
	struct o2dlm_lock_bast *bast;

	if (!ctxt || !lockid)
		return O2DLM_ET_INVALID_ARGS;

	lockres = o2dlm_find_lock_res(ctxt, lockid);
	if (!lockres)
		return O2DLM_ET_UNKNOWN_LOCK;

	bast = o2dlm_find_bast(ctxt, lockres->l_fd);
	if (bast)
		o2dlm_remove_bast(bast);

	o2dlm_remove_lock_res(lockres);

	ret = o2dlm_unlock_lock_res(ctxt, lockres);

	free(lockres);

	if (ret && (ret != O2DLM_ET_BUSY_LOCK))
		return ret;
	return 0;
}

errcode_t o2dlm_read_lvb(struct o2dlm_ctxt *ctxt,
			 char *lockid,
			 char *lvb,
			 unsigned int len,
			 unsigned int *bytes_read)
{
	if (!ctxt || !lockid || !lvb)
		return O2DLM_ET_INVALID_ARGS;

	if (ctxt->ct_classic)
		return o2dlm_read_lvb_classic(ctxt, lockid, lvb, len,
					      bytes_read);
	else
		return o2dlm_read_lvb_fsdlm(ctxt, lockid, lvb, len,
					    bytes_read);
}

errcode_t o2dlm_write_lvb(struct o2dlm_ctxt *ctxt,
			  char *lockid,
			  const char *lvb,
			  unsigned int len,
			  unsigned int *bytes_written)
{
	if (!ctxt || !lockid || !lvb)
		return O2DLM_ET_INVALID_ARGS;

	if (ctxt->ct_classic)
		return o2dlm_write_lvb_classic(ctxt, lockid, lvb, len,
					       bytes_written);
	else
		return o2dlm_write_lvb_fsdlm(ctxt, lockid, lvb, len,
					     bytes_written);
}

void o2dlm_process_bast(struct o2dlm_ctxt *ctxt, int poll_fd)
{
	struct o2dlm_lock_bast *bast;

	bast = o2dlm_find_bast(ctxt, poll_fd);
	if (bast)
		bast->b_bast(bast->b_arg);
}

/* NULL dlmfs_path means fsdlm */
errcode_t o2dlm_initialize(const char *dlmfs_path,
			   const char *domain_name,
			   struct o2dlm_ctxt **dlm_ctxt)
{
	if (!domain_name || !dlm_ctxt)
		return O2DLM_ET_INVALID_ARGS;

	if (dlmfs_path)
		return o2dlm_initialize_classic(dlmfs_path, domain_name,
						dlm_ctxt);
	else
		return o2dlm_initialize_fsdlm(domain_name, dlm_ctxt);
}

errcode_t o2dlm_destroy(struct o2dlm_ctxt *ctxt)
{
	if (!ctxt)
		return O2DLM_ET_INVALID_ARGS;

	if (ctxt->ct_classic)
		return o2dlm_destroy_classic(ctxt);
	else
		return o2dlm_destroy_fsdlm(ctxt);
}
