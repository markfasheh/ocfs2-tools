/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * o2cb_abi.c
 *
 * Kernel<->User ABI for modifying cluster configuration.
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

#define _XOPEN_SOURCE 600  /* Triggers XOPEN2K in features.h */
#define _LARGEFILE64_SOURCE

#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

#include <linux/types.h>

#include "o2cb.h"

#include "o2cb_abi.h"

#include "o2cb_crc32.h"

#include "ocfs2_nodemanager.h"

errcode_t o2cb_create_cluster(const char *cluster_name)
{
	char path[PATH_MAX];
	int ret;
	errcode_t err = 0;

	ret = snprintf(path, PATH_MAX - 1, O2CB_FORMAT_CLUSTER,
		       cluster_name);
	if ((ret <= 0) || (ret == (PATH_MAX - 1)))
		return O2CB_ET_INTERNAL_FAILURE;

	ret = mkdir(path,
		    S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
	if (ret) {
		switch (errno) {
			case EEXIST:
				err = O2CB_ET_CLUSTER_EXISTS;
				break;

			case EACCES:
			case EPERM:
			case EROFS:
				err = O2CB_ET_PERMISSION_DENIED;
				break;

			case ENOMEM:
				err = O2CB_ET_NO_MEMORY;
				break;

			case ENOTDIR:
			case ENOENT:
				err = O2CB_ET_SERVICE_UNAVAILABLE;
				break;

			default:
				err = O2CB_ET_INTERNAL_FAILURE;
				break;
		}
	}

	return err;
}

errcode_t o2cb_remove_cluster(const char *cluster_name)
{
	char path[PATH_MAX];
	int ret;
	errcode_t err = 0;

	ret = snprintf(path, PATH_MAX - 1, O2CB_FORMAT_CLUSTER,
		       cluster_name);
	if ((ret <= 0) || (ret == (PATH_MAX - 1)))
		return O2CB_ET_INTERNAL_FAILURE;

	ret = rmdir(path);
	if (ret) {
		switch (errno) {
			case EACCES:
			case EPERM:
			case EROFS:
				err = O2CB_ET_PERMISSION_DENIED;
				break;

			case ENOMEM:
				err = O2CB_ET_NO_MEMORY;
				break;

			case ENOTDIR:
				err = O2CB_ET_SERVICE_UNAVAILABLE;
				break;

			case ENOENT:
				err = 0;
				break;

			default:
				err = O2CB_ET_INTERNAL_FAILURE;
				break;
		}
	}

	return err;
}

static int do_read(int fd, void *bytes, size_t count)
{
	int total = 0;
	int ret;

	while (total < count) {
		ret = read(fd, bytes + total, count - total);
		if (ret < 0) {
			ret = -errno;
			if ((ret == -EAGAIN) || (ret == -EINTR))
				continue;
			total = ret;
			break;
		}
		if (ret == 0)
			break;
		total += ret;
	}

	return total;
}

static errcode_t do_write(int fd, const void *bytes, size_t count)
{
	int total = 0;
	int ret;
	int err = 0;

	while (total < count) {
		ret = write(fd, bytes + total, count - total);
		if (ret < 0) {
			ret = -errno;
			if ((ret == -EAGAIN) || (ret == -EINTR))
				continue;
			if (ret == -EIO)
				err = O2CB_ET_IO;
			else
				err = O2CB_ET_INTERNAL_FAILURE;
			break;
		}

		total += ret;
	}

	return err;
}

static errcode_t o2cb_set_attribute(const char *attr_path,
				    const char *attr_value)
{
	errcode_t err = 0;
	int fd;

	fd = open(attr_path, O_WRONLY);
	if (fd < 0) {
		switch (errno) {
			default:
				err = O2CB_ET_INTERNAL_FAILURE;
				break;

			case ENOTDIR:
			case ENOENT:
			case EISDIR:
				err = O2CB_ET_SERVICE_UNAVAILABLE;
				break;

			case EACCES:
			case EPERM:
			case EROFS:
				err = O2CB_ET_PERMISSION_DENIED;
				break;
		}
	} else {
		err = do_write(fd, attr_value, strlen(attr_value));
		close(fd);
	}

	return err;
}

static errcode_t o2cb_get_attribute(const char *attr_path,
				    char *attr_value,
				    size_t count)
{
	int ret;
	errcode_t err = 0;
	int fd;

	fd = open(attr_path, O_RDONLY);
	if (fd < 0) {
		switch (errno) {
			default:
				err = O2CB_ET_INTERNAL_FAILURE;
				break;

			case ENOTDIR:
			case ENOENT:
			case EISDIR:
				err = O2CB_ET_SERVICE_UNAVAILABLE;
				break;

			case EACCES:
			case EPERM:
			case EROFS:
				err = O2CB_ET_PERMISSION_DENIED;
				break;
		}
	} else {
		ret = do_read(fd, attr_value, count);
		close(fd);
		if (ret == -EIO)
			err = O2CB_ET_IO;
		else if (ret < 0)
			err = O2CB_ET_INTERNAL_FAILURE;
		else if (ret < count)
			attr_value[ret] = '\0';
	}

	return err;
}



static errcode_t o2cb_set_node_attribute(const char *cluster_name,
					 const char *node_name,
					 const char *attr_name,
					 const char *attr_value)
{
	int ret;
	char attr_path[PATH_MAX];

	ret = snprintf(attr_path, PATH_MAX - 1, O2CB_FORMAT_NODE_ATTR,
		       cluster_name, node_name, attr_name);
	if ((ret <= 0) || (ret == (PATH_MAX - 1)))
		return O2CB_ET_INTERNAL_FAILURE;

	return o2cb_set_attribute(attr_path, attr_value);
}

static errcode_t o2cb_get_node_attribute(const char *cluster_name,
					 const char *node_name,
					 const char *attr_name,
					 char *attr_value,
					 size_t count)
{
	int ret;
	char attr_path[PATH_MAX];

	ret = snprintf(attr_path, PATH_MAX - 1, O2CB_FORMAT_NODE_ATTR,
		       cluster_name, node_name, attr_name);
	if ((ret <= 0) || (ret == (PATH_MAX - 1)))
		return O2CB_ET_INTERNAL_FAILURE;

	return o2cb_get_attribute(attr_path, attr_value, count);
}

/* XXX there is no commit yet, so this just creates the node in place
 * and then sets the attributes in order.  if the ipaddr is set
 * successfully then the node is live */
errcode_t o2cb_add_node(const char *cluster_name,
			const char *node_name, const char *node_num,
			const char *ip_address, const char *ip_port,
			const char *local)
{
	char node_path[PATH_MAX];
	int ret;
	errcode_t err;

	ret = snprintf(node_path, PATH_MAX - 1,
		       O2CB_FORMAT_NODE,
		       cluster_name, node_name);
	if (ret <= 0 || ret == PATH_MAX - 1) {
		err = O2CB_ET_INTERNAL_FAILURE;
		goto out;
	}

	ret = mkdir(node_path,
		    S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
	if (ret) {
		switch (errno) {
			case EEXIST:
				err = O2CB_ET_NODE_EXISTS;
				break;

			case EACCES:
			case EPERM:
			case EROFS:
				err = O2CB_ET_PERMISSION_DENIED;
				break;

			case ENOMEM:
				err = O2CB_ET_NO_MEMORY;
				break;

			case ENOTDIR:
			case ENOENT:
				err = O2CB_ET_SERVICE_UNAVAILABLE;
				break;

			default:
				err = O2CB_ET_INTERNAL_FAILURE;
				break;
		}

		goto out;
	}

	err = o2cb_set_node_attribute(cluster_name, node_name,
				      "ipv4_port", ip_port);
	if (err)
		goto out_rmdir;

	err = o2cb_set_node_attribute(cluster_name, node_name,
				      "ipv4_address", ip_address);
	if (err)
		goto out_rmdir;

	err = o2cb_set_node_attribute(cluster_name, node_name,
				      "num", node_num);
	if (err)
		goto out_rmdir;

	err = o2cb_set_node_attribute(cluster_name, node_name,
				      "local", local);
out_rmdir:
	if (err)
		rmdir(node_path);

out:
	return err;
}

errcode_t o2cb_del_node(const char *cluster_name, const char *node_name)
{
	char node_path[PATH_MAX];
	int ret;
	errcode_t err = 0;

	ret = snprintf(node_path, PATH_MAX - 1, O2CB_FORMAT_NODE,
		       cluster_name, node_name);
	if (ret <= 0 || ret == PATH_MAX - 1) {
		err = O2CB_ET_INTERNAL_FAILURE;
		goto out;
	}

	ret = rmdir(node_path);
	if (ret) {
		switch (errno) {
			case EACCES:
			case EPERM:
			case EROFS:
				err = O2CB_ET_PERMISSION_DENIED;
				break;

			case ENOMEM:
				err = O2CB_ET_NO_MEMORY;
				break;

			case ENOTDIR:
				err = O2CB_ET_SERVICE_UNAVAILABLE;
				break;

			case ENOENT:
				err = 0;
				break;

			default:
				err = O2CB_ET_INTERNAL_FAILURE;
				break;
		}
	}

out:
	return err;
}

#define O2CB_NM_REVISION_PATH	"/proc/fs/ocfs2_nodemanager/interface_revision"
errcode_t o2cb_init(void)
{
	int ret, fd;
	unsigned int module_version;
	errcode_t err;
	char revision_string[16];

	fd = open(O2CB_NM_REVISION_PATH, O_RDONLY);
	if (fd == -1) {
		switch (errno) {
			default:
				err = O2CB_ET_INTERNAL_FAILURE;
				break;

			case ENOTDIR:
			case ENOENT:
			case EISDIR:
				err = O2CB_ET_SERVICE_UNAVAILABLE;
				break;

			case EACCES:
			case EPERM:
			case EROFS:
				err = O2CB_ET_PERMISSION_DENIED;
				break;
		}
		return err;
	}

	ret = do_read(fd, revision_string, sizeof(revision_string) - 1);
	close(fd);

	if (ret < 0) {
		err = O2CB_ET_INTERNAL_FAILURE;
		if (ret == -EIO)
			err = O2CB_ET_IO;
		return err;
	}

	revision_string[ret] = '\0';

	ret = sscanf(revision_string, "%u\n", &module_version);
	if (ret < 0)
		return O2CB_ET_INTERNAL_FAILURE;

	if (NM_API_VERSION < module_version)
		return O2CB_ET_BAD_VERSION;

	return 0;
}

static errcode_t o2cb_set_region_attribute(const char *cluster_name,
					   const char *region_name,
					   const char *attr_name,
					   const char *attr_value)
{
	int ret;
	char attr_path[PATH_MAX];

	ret = snprintf(attr_path, PATH_MAX - 1,
		       O2CB_FORMAT_HEARTBEAT_REGION_ATTR,
		       cluster_name, region_name, attr_name);
	if ((ret <= 0) || (ret == (PATH_MAX - 1)))
		return O2CB_ET_INTERNAL_FAILURE;

	return o2cb_set_attribute(attr_path, attr_value);
}

static errcode_t o2cb_get_region_attribute(const char *cluster_name,
					   const char *region_name,
					   const char *attr_name,
					   char *attr_value,
					   size_t count)
{
	int ret;
	char attr_path[PATH_MAX];

	ret = snprintf(attr_path, PATH_MAX - 1,
		       O2CB_FORMAT_HEARTBEAT_REGION_ATTR,
		       cluster_name, region_name, attr_name);
	if ((ret <= 0) || (ret == (PATH_MAX - 1)))
		return O2CB_ET_INTERNAL_FAILURE;

	return o2cb_get_attribute(attr_path, attr_value, count);
}

static errcode_t _fake_default_cluster(char *cluster)
{
	errcode_t ret;
	char **clusters;

	ret = o2cb_list_clusters(&clusters);
	if (ret)
		return ret;

	snprintf(cluster, NAME_MAX - 1, "%s", clusters[0]);

	o2cb_free_cluster_list(clusters);

	return 0;
}

static errcode_t o2cb_create_heartbeat_region(const char *cluster_name,
					      const char *region_name,
					      const char *device_name,
					      int block_bytes,
					      uint64_t start_block,
					      uint64_t blocks,
					      unsigned int timeout_ms,
					      unsigned int dead_iter)
{
	char _fake_cluster_name[NAME_MAX];
	char region_path[PATH_MAX];
	char num_buf[NAME_MAX];
	int ret, fd;
	errcode_t err;

	if (!cluster_name) {
		err = _fake_default_cluster(_fake_cluster_name);
		if (err)
			return err;
		cluster_name = _fake_cluster_name;
	}

#define O2CB_MAXIMUM_HEARTBEAT_BLOCKSIZE 4096
	if (block_bytes > O2CB_MAXIMUM_HEARTBEAT_BLOCKSIZE) {
		err = O2CB_ET_INVALID_BLOCK_SIZE;
		goto out;
	}

#define O2CB_MAX_NODE_COUNT 255
	if (!blocks || (blocks > O2CB_MAX_NODE_COUNT)) {
		err = O2CB_ET_INVALID_BLOCK_COUNT;
		goto out;
	}

	ret = snprintf(region_path, PATH_MAX - 1,
		       O2CB_FORMAT_HEARTBEAT_REGION,
		       cluster_name, region_name);
	if (ret <= 0 || ret == PATH_MAX - 1) {
		err = O2CB_ET_INTERNAL_FAILURE;
		goto out;
	}

	ret = mkdir(region_path,
		    S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
	if (ret) {
		switch (errno) {
			case EEXIST:
				err = O2CB_ET_REGION_EXISTS;
				break;

			case EACCES:
			case EPERM:
			case EROFS:
				err = O2CB_ET_PERMISSION_DENIED;
				break;

			case ENOMEM:
				err = O2CB_ET_NO_MEMORY;
				break;

			case ENOTDIR:
			case ENOENT:
				err = O2CB_ET_SERVICE_UNAVAILABLE;
				break;

			default:
				err = O2CB_ET_INTERNAL_FAILURE;
				break;
		}

		goto out;
	}

	ret = snprintf(num_buf, NAME_MAX - 1, "%d", block_bytes);
	if (ret <= 0 || ret == PATH_MAX - 1) {
		err = O2CB_ET_INTERNAL_FAILURE;
		goto out_rmdir;
	}

	err = o2cb_set_region_attribute(cluster_name, region_name,
					"block_bytes", num_buf);
	if (err)
		goto out_rmdir;

	ret = snprintf(num_buf, NAME_MAX - 1, "%"PRIu64, start_block);
	if (ret <= 0 || ret == PATH_MAX - 1) {
		err = O2CB_ET_INTERNAL_FAILURE;
		goto out_rmdir;
	}

	err = o2cb_set_region_attribute(cluster_name, region_name,
					"start_block", num_buf);
	if (err)
		goto out_rmdir;

	ret = snprintf(num_buf, NAME_MAX - 1, "%"PRIu64, blocks);
	if (ret <= 0 || ret == PATH_MAX - 1) {
		err = O2CB_ET_INTERNAL_FAILURE;
		goto out_rmdir;
	}

	err = o2cb_set_region_attribute(cluster_name, region_name,
					"blocks", num_buf);
	if (err)
		goto out_rmdir;

	ret = snprintf(num_buf, NAME_MAX - 1, "%u", timeout_ms);
	if (ret <= 0 || ret == PATH_MAX - 1) {
		err = O2CB_ET_INTERNAL_FAILURE;
		goto out_rmdir;
	}

	err = o2cb_set_region_attribute(cluster_name, region_name,
					"timeout_ms", num_buf);
	if (err)
		goto out_rmdir;

	ret = snprintf(num_buf, NAME_MAX - 1, "%u", dead_iter);
	if (ret <= 0 || ret == PATH_MAX - 1) {
		err = O2CB_ET_INTERNAL_FAILURE;
		goto out_rmdir;
	}

	err = o2cb_set_region_attribute(cluster_name, region_name,
					"dead_iter", num_buf);
	if (err)
		goto out_rmdir;

	fd = open64(device_name, O_RDWR);
	if (fd < 0) {
		switch (errno) {
			default:
				err = O2CB_ET_INTERNAL_FAILURE;
				break;

			case ENOTDIR:
			case ENOENT:
			case EISDIR:
				err = O2CB_ET_SERVICE_UNAVAILABLE;
				break;

			case EACCES:
			case EPERM:
			case EROFS:
				err = O2CB_ET_PERMISSION_DENIED;
				break;
		}

		goto out_rmdir;
	}

	ret = snprintf(num_buf, NAME_MAX - 1, "%d", fd);
	if (ret <= 0 || ret == PATH_MAX - 1) {
		err = O2CB_ET_INTERNAL_FAILURE;
		goto out_close;
	}

	err = o2cb_set_region_attribute(cluster_name, region_name,
					"dev", num_buf);

out_close:
	close(fd);

out_rmdir:
	if (err)
		rmdir(region_path);

out:
	return err;
}

static errcode_t o2cb_destroy_sem_set(int semid)
{
	int error;
	errcode_t ret = 0;

	error = semctl(semid, 0, IPC_RMID);
	if (error) {
		switch(errno) {
			case EPERM:
			case EACCES:
				ret = O2CB_ET_PERMISSION_DENIED;
				break;

			case EIDRM:
				/* Someone raced us to it... can this
				 * happen? */
				ret = 0;
				break;

			default:
				ret = O2CB_ET_INTERNAL_FAILURE;
		}
	}

	return ret;
}

static errcode_t o2cb_get_semid(const char *region,
				int *semid)
{
	int ret;
	key_t key;

	key = (key_t) o2cb_crc32(region);

	ret = semget(key, 2, IPC_CREAT);
	if (ret < 0)
		return O2CB_ET_BAD_SEM;

	*semid = ret;

	return 0;
}

static inline errcode_t o2cb_semop_err(int err)
{
	errcode_t ret;

	switch (err) {
		case EACCES:
			ret = O2CB_ET_PERMISSION_DENIED;
			break;

		case EIDRM:
			/* Other paths depend on us returning this for EIDRM */
			ret = O2CB_ET_NO_SEM;
			break;

		case EINVAL:
			ret = O2CB_ET_SERVICE_UNAVAILABLE;
			break;

		case ENOMEM:
			ret = O2CB_ET_NO_MEMORY;
			break;

		default:
			ret = O2CB_ET_INTERNAL_FAILURE;
	}
	return ret;
}

static errcode_t o2cb_mutex_down(int semid)
{
	int err;
	struct sembuf sops[2] = { 
		{ .sem_num = 0, .sem_op = 0, .sem_flg = SEM_UNDO },
		{ .sem_num = 0, .sem_op = 1, .sem_flg = SEM_UNDO }
	};

	err = semop(semid, sops, 2);
	if (err)
		return o2cb_semop_err(errno);

	return 0;
}

/* We have coded our semaphore destruction such that you will legally
 * only get EIDRM when waiting on the mutex. Use this function to look
 * it up and return it locked - it knows how to loop around on
 * EIDRM. */
static errcode_t o2cb_mutex_down_lookup(const char *region,
					int *semid)
{
	int tmpid;
	errcode_t ret;

	ret = O2CB_ET_NO_SEM;
	while (ret == O2CB_ET_NO_SEM) {
		ret = o2cb_get_semid(region, &tmpid);
		if (ret)
			return ret;

		ret = o2cb_mutex_down(tmpid);
		if (!ret) {
			/* At this point, we're the only ones who can destroy
			 * this sem set. */
			*semid = tmpid;
		}
	}

	return ret;
}

static errcode_t o2cb_mutex_up(int semid)
{
	int err;
	struct sembuf sop = { .sem_num = 0,
			      .sem_op = -1,
			      .sem_flg = SEM_UNDO };

	err = semop(semid, &sop, 1);
	if (err)
		return o2cb_semop_err(errno);

	return 0;
}

static errcode_t __o2cb_get_ref(int semid,
			      int undo)
{
	int err;
	struct sembuf sop = { .sem_num = 1,
			      .sem_op = 1,
			      .sem_flg = undo ? SEM_UNDO : 0 };

	err = semop(semid, &sop, 1);
	if (err)
		return o2cb_semop_err(errno);

	return 0;
}

errcode_t o2cb_get_region_ref(const char *region_name,
			      int undo)
{
	errcode_t ret, up_ret;
	int semid;

	ret = o2cb_mutex_down_lookup(region_name, &semid);
	if (ret)
		return ret;

	ret = __o2cb_get_ref(semid, undo);

	/* XXX: Maybe try to drop ref if we get an error here? */
	up_ret = o2cb_mutex_up(semid);
	if (up_ret && !ret)
		ret = up_ret;

	return ret;
}

static errcode_t __o2cb_drop_ref(int semid,
				 int undo)
{
	int err;
	struct sembuf sop = { .sem_num = 1,
			      .sem_op = -1,
			      .sem_flg = undo ? SEM_UNDO : 0 };

	err = semop(semid, &sop, 1);
	if (err)
		return o2cb_semop_err(errno);

	return 0;
}

errcode_t o2cb_put_region_ref(const char *region_name,
			      int undo)
{
	errcode_t ret, up_ret;
	int semid;

	ret = o2cb_mutex_down_lookup(region_name, &semid);
	if (ret)
		return ret;

	ret = __o2cb_drop_ref(semid, undo);

	up_ret = o2cb_mutex_up(semid);
	if (up_ret && !ret)
		ret = up_ret;

	return ret;
}

static errcode_t __o2cb_get_num_refs(int semid, int *num_refs)
{
	int ret;

	ret = semctl(semid, 1, GETVAL, NULL);
	if (ret == -1)
		return o2cb_semop_err(errno);

	*num_refs = ret;

	return 0;
}

errcode_t o2cb_num_region_refs(const char *region_name,
			       int *num_refs)
{
	errcode_t ret;
	int semid;

	ret = o2cb_get_semid(region_name, &semid);
	if (ret)
		return ret;

	ret = __o2cb_get_num_refs(semid, num_refs);

	/* The semaphore set was destroyed underneath us. We treat
	 * that as zero reference and return success. */
	if (ret == O2CB_ET_NO_SEM) {
		*num_refs = 0;
		ret = 0;
	}

	return ret;
}

static errcode_t o2cb_remove_heartbeat_region(const char *cluster_name,
					      const char *region_name)
{
	char _fake_cluster_name[NAME_MAX];
	char region_path[PATH_MAX];
	int ret;
	errcode_t err = 0;

	if (!cluster_name) {
		err = _fake_default_cluster(_fake_cluster_name);
		if (err)
			return err;
		cluster_name = _fake_cluster_name;
	}

	ret = snprintf(region_path, PATH_MAX - 1,
		       O2CB_FORMAT_HEARTBEAT_REGION,
		       cluster_name, region_name);
	if (ret <= 0 || ret == PATH_MAX - 1) {
		err = O2CB_ET_INTERNAL_FAILURE;
		goto out;
	}

	ret = rmdir(region_path);
	if (ret) {
		switch (errno) {
			case EACCES:
			case EPERM:
			case EROFS:
				err = O2CB_ET_PERMISSION_DENIED;
				break;

			case ENOMEM:
				err = O2CB_ET_NO_MEMORY;
				break;

			case ENOTDIR:
			case ENOENT:
				err = O2CB_ET_SERVICE_UNAVAILABLE;
				break;

			case ENOTEMPTY:
			case EBUSY:
				err = O2CB_ET_REGION_IN_USE;
				break;

			default:
				err = O2CB_ET_INTERNAL_FAILURE;
				break;
		}
	}

out:
	return err;
}

/* For ref counting purposes, we need to know whether this process
 * called o2cb_create_heartbeat_region_disk. If it did, then we want
 * to drop the reference taken during startup, otherwise that
 * reference was dropped automatically at process shutdown so there's
 * no need to drop one here. */
static errcode_t __o2cb_stop_heartbeat_region(const char *cluster_name,
					      const char *region_name,
					      int undo)
{
	errcode_t ret, up_ret;
	int hb_refs;
	int semid;

	ret = o2cb_mutex_down_lookup(region_name, &semid);
	if (ret)
		return ret;

	ret = __o2cb_get_num_refs(semid, &hb_refs);
	if (ret)
		goto up;

	/* A previous process may have died and left us with no
	 * references on the region. We avoid a negative error count
	 * here and clean up the region as normal. */
	if (hb_refs) {
		ret = __o2cb_drop_ref(semid, undo);
		if (ret)
			goto up;

		/* No need to call get_num_refs again -- this was
		 * atomic so we know what the new value must be. */
		hb_refs--;
	}

	if (!hb_refs) {
		/* XXX: If this fails, shouldn't we still destroy the
		 * semaphore set? */
		ret = o2cb_remove_heartbeat_region(cluster_name, region_name);
		if (ret)
			goto up;

		ret = o2cb_destroy_sem_set(semid);
		if (ret)
			goto up;

		goto done;
	}
up:
	up_ret = o2cb_mutex_up(semid);
	if (up_ret && !ret) /* XXX: Maybe stop heartbeat here then? */
		ret = up_ret;

done:
	return ret;
}

static errcode_t __o2cb_start_heartbeat_region(const char *cluster_name,
					       struct o2cb_region_desc *desc,
					       int undo)
{
	errcode_t ret, up_ret;
	int semid;

	ret = o2cb_mutex_down_lookup(desc->r_name, &semid);
	if (ret)
		return ret;

	ret = o2cb_create_heartbeat_region(cluster_name,
					   desc->r_name,
					   desc->r_device_name,
					   desc->r_block_bytes,
					   desc->r_start_block,
					   desc->r_blocks,
					   desc->r_timeout_ms,
					   desc->r_node_down_misses);
	if (ret && ret != O2CB_ET_REGION_EXISTS)
		goto up;

	ret = __o2cb_get_ref(semid, undo);
	/* XXX: Maybe stop heartbeat on error here? */
up:
	up_ret = o2cb_mutex_up(semid);
	if (up_ret && !ret)
		ret = up_ret;

	return ret;
}

errcode_t o2cb_start_heartbeat_region(const char *cluster_name,
				      struct o2cb_region_desc *desc)
{
	return __o2cb_start_heartbeat_region(cluster_name, desc, 1);
}

errcode_t o2cb_stop_heartbeat_region(const char *cluster_name,
				     const char *region_name)
{
	return __o2cb_stop_heartbeat_region(cluster_name, region_name, 1);
}

errcode_t o2cb_start_heartbeat_region_perm(const char *cluster_name,
					   struct o2cb_region_desc *desc)
{
	return __o2cb_start_heartbeat_region(cluster_name, desc, 0);
}

errcode_t o2cb_stop_heartbeat_region_perm(const char *cluster_name,
					  const char *region_name)
{
	return __o2cb_stop_heartbeat_region(cluster_name, region_name, 0);
}

static inline int is_dots(const char *name)
{
	size_t len = strlen(name);

	if (len == 0)
		return 0;

	if (name[0] == '.') {
		if (len == 1)
			return 1;
		if (len == 2 && name[1] == '.')
			return 1;
	}

	return 0;
}

static errcode_t o2cb_list_dir(char *path, char ***objs)
{
	errcode_t ret;
	int count;
	DIR *dir;
	struct dirent *dirent;
	struct dlist {
		struct dlist *next;
		char *name;
	} *tmp, *list;

	dir = opendir(path);
	if (!dir) {
		switch (errno) {
			default:
				ret = O2CB_ET_INTERNAL_FAILURE;
				break;

			case ENOTDIR:
			case ENOENT:
				ret = O2CB_ET_SERVICE_UNAVAILABLE;
				break;

			case ENOMEM:
				ret = O2CB_ET_NO_MEMORY;
				break;

			case EACCES:
				ret = O2CB_ET_PERMISSION_DENIED;
				break;
		}

		goto out;
	}

	ret = O2CB_ET_NO_MEMORY;
	count = 0;
	list = NULL;
	while ((dirent = readdir(dir)) != NULL) {
		if (is_dots(dirent->d_name))
			continue;

		tmp = malloc(sizeof(struct dlist));
		if (!tmp)
			goto out_free_list;

		tmp->name = strdup(dirent->d_name);
		if (!tmp->name) {
			free(tmp);
			goto out_free_list;
		}

		tmp->next = list;
		list = tmp;
		count++;
	}

	*objs = malloc(sizeof(char *) * (count + 1));
	if (!*objs)
		goto out_free_list;

	tmp = list;
	count = 0;
	for (tmp = list, count = 0; tmp; tmp = tmp->next, count++) {
		(*objs)[count] = tmp->name;
		tmp->name = NULL;
	}
	(*objs)[count] = NULL;

	ret = 0;

out_free_list:
	while (list) {
		tmp = list;
		list = list->next;

		if (tmp->name)
			free(tmp->name);
		free(tmp);
	}

	closedir(dir);

out:
	return ret;
}

static void o2cb_free_dir_list(char **objs)
{
	int i;

	for (i = 0; objs[i]; i++)
		free(objs[i]);

	free(objs);
}

errcode_t o2cb_list_clusters(char ***clusters)
{
	return o2cb_list_dir(O2CB_FORMAT_CLUSTER_DIR, clusters);
}

void o2cb_free_cluster_list(char **clusters)
{
	o2cb_free_dir_list(clusters);
}

errcode_t o2cb_list_nodes(char *cluster_name, char ***nodes)
{
	char path[PATH_MAX];
	errcode_t ret;

	ret = snprintf(path, PATH_MAX - 1, O2CB_FORMAT_NODE_DIR,
		       cluster_name);
	if ((ret <= 0) || (ret == (PATH_MAX - 1)))
		return O2CB_ET_INTERNAL_FAILURE;

	return o2cb_list_dir(path, nodes);
}

void o2cb_free_nodes_list(char **nodes)
{
	o2cb_free_dir_list(nodes);
}

errcode_t o2cb_get_node_num(const char *cluster_name, const char *node_name,
			    uint16_t *node_num)
{
	char val[30];
	char *p;
	errcode_t ret;

	ret = o2cb_get_node_attribute(cluster_name, node_name,
				      "num", val, sizeof(val));
	if (ret)
		return ret;

	*node_num = strtoul(val, &p, 0);
	if (!p || (*p && *p != '\n'))
		return O2CB_ET_INVALID_NODE_NUM;

	return 0;
}

errcode_t o2cb_get_hb_ctl_path(char *buf, int count)
{
	int fd;
	int total = 0;
	int ret;

#define HB_CTL_PATH	"/proc/sys/fs/ocfs2/nm/hb_ctl_path"

	fd = open(HB_CTL_PATH, O_RDONLY);
	if (fd == -1) {
		if (errno == ENOENT)
			return O2CB_ET_MODULE_NOT_LOADED;
		else
			return errno;
	}

	while (total < count) {
		ret = read(fd, buf + total, count - total);
		if (ret < 0) {
			ret = -errno;
			if ((ret == -EAGAIN) || (ret == -EINTR))
				continue;
			total = ret;
			break;
		}
		if (ret == 0)
			break;
		total += ret;
	}

	if (total < 0) {
		close(fd);
		return total;
	}

	buf[total] = '\0';
	if (buf[total - 1] == '\n')
		buf[total - 1] = '\0';

	close(fd);

	return 0;
}
