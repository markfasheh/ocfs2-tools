/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * o2cb_abi.c
 *
 * Kernel<->User ABI for modifying cluster configuration.
 *
 * Copyright (C) 2004,2007 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License, version 2, as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#define _XOPEN_SOURCE 600  /* Triggers XOPEN2K in features.h */
#define _LARGEFILE64_SOURCE

#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

#include <linux/types.h>

#include "o2cb/o2cb.h"
#include "o2cb/o2cb_client_proto.h"
#include "o2cb_abi.h"
#include "o2cb_crc32.h"

#define CLUSTER_STACK_FILE	"/sys/fs/ocfs2/cluster_stack"
#define LOCKING_PROTOCOL_FILE	"/sys/fs/ocfs2/max_locking_protocol"
#define OCFS2_STACK_LABEL_LEN	4
#define CONTROL_DEVICE		"/dev/misc/ocfs2_control"


struct o2cb_stack_ops {
	errcode_t (*list_clusters)(char ***clusters);
	errcode_t (*begin_group_join)(struct o2cb_cluster_desc *cluster,
				      struct o2cb_region_desc *region);
	errcode_t (*complete_group_join)(struct o2cb_cluster_desc *cluster,
					 struct o2cb_region_desc *region,
					 int result);
	errcode_t (*group_leave)(struct o2cb_cluster_desc *cluster,
				 struct o2cb_region_desc *region);
};

struct o2cb_stack {
	char s_name[OCFS2_STACK_LABEL_LEN + 1];
	struct o2cb_stack_ops *s_ops;
};

static errcode_t classic_list_clusters(char ***clusters);
static errcode_t classic_begin_group_join(struct o2cb_cluster_desc *cluster,
					  struct o2cb_region_desc *region);
static errcode_t classic_complete_group_join(struct o2cb_cluster_desc *cluster,
					     struct o2cb_region_desc *region,
					     int result);
static errcode_t classic_group_leave(struct o2cb_cluster_desc *cluster,
				     struct o2cb_region_desc *region);
static struct o2cb_stack_ops classic_ops = {
	.list_clusters		= classic_list_clusters,
	.begin_group_join	= classic_begin_group_join,
	.complete_group_join	= classic_complete_group_join,
	.group_leave		= classic_group_leave,
};
static struct o2cb_stack classic_stack = {
	.s_name 	= "o2cb",
	.s_ops		= &classic_ops,
};

static errcode_t user_list_clusters(char ***clusters);
static errcode_t user_begin_group_join(struct o2cb_cluster_desc *cluster,
				       struct o2cb_region_desc *region);
static errcode_t user_complete_group_join(struct o2cb_cluster_desc *cluster,
					  struct o2cb_region_desc *region,
					  int result);
static errcode_t user_group_leave(struct o2cb_cluster_desc *cluster,
				  struct o2cb_region_desc *region);
static struct o2cb_stack_ops user_ops = {
	.list_clusters		= user_list_clusters,
	.begin_group_join	= user_begin_group_join,
	.complete_group_join	= user_complete_group_join,
	.group_leave		= user_group_leave,
};
static struct o2cb_stack user_stack = {
	.s_ops		= &user_ops,
};

static struct o2cb_stack *current_stack;
static int control_daemon_fd = -1;
static int control_device_fd = -1;

static char *configfs_path;


static ssize_t read_single_line_file(const char *file, char *line,
				     size_t count)
{
	ssize_t ret = 0;
	FILE *f;

	f = fopen(file, "r");
	if (f) {
		if (fgets(line, count, f))
			ret = strlen(line);
		fclose(f);
	} else
		ret = -errno;

	return ret;
}

static ssize_t read_stack_file(char *line, size_t count)
{
	return read_single_line_file(CLUSTER_STACK_FILE, line, count);
}

static errcode_t determine_stack(void)
{
	ssize_t len;
	char line[100];
	errcode_t err = O2CB_ET_SERVICE_UNAVAILABLE;

	len = read_stack_file(line, sizeof(line));
	if (len > 0) {
		if (line[len - 1] == '\n') {
			line[len - 1] = '\0';
			len--;
		}

		if (len != OCFS2_STACK_LABEL_LEN)
			err = O2CB_ET_INTERNAL_FAILURE;
		else if (!strcmp(line, classic_stack.s_name)) {
			current_stack = &classic_stack;
			err = 0;
		} else {
			strncpy(user_stack.s_name, line,
				OCFS2_STACK_LABEL_LEN);
			current_stack = &user_stack;
			err = 0;
		}
	} else if (len == -ENOENT) {
		current_stack = &classic_stack;
		err = 0;
	}

	return err;
}

errcode_t o2cb_get_stack_name(const char **name)
{
	if (!current_stack)
		return O2CB_ET_SERVICE_UNAVAILABLE;

	*name = current_stack->s_name;
	return 0;
}

static ssize_t read_locking_proto_file(char *line, size_t count)
{
	return read_single_line_file(LOCKING_PROTOCOL_FILE, line, count);
}

errcode_t o2cb_get_max_locking_protocol(struct ocfs2_protocol_version *proto)
{
	ssize_t len;
	char line[100];
	errcode_t err;
	unsigned int major, minor;

	len = read_locking_proto_file(line, sizeof(line));
	if (len <= 0) {
		switch (-len) {
			case EACCES:
			case EPERM:
				err = O2CB_ET_PERMISSION_DENIED;
				break;

			case ENOMEM:
				err = O2CB_ET_NO_MEMORY;
				break;

			case 0:
			case ENOENT:
			case ENOTDIR:
				err = O2CB_ET_SERVICE_UNAVAILABLE;
				break;

			default:
				err = O2CB_ET_INTERNAL_FAILURE;
				break;
		}
		goto out;
	}

	if (line[len - 1] == '\n') {
		line[len - 1] = '\0';
		len--;
	}

	err = O2CB_ET_SERVICE_UNAVAILABLE;
	if (sscanf(line, "%u.%u", &major, &minor) != 2)
		goto out;
	/* Major and minor can't be more than a u8 */
	if ((major > (uint8_t)-1) || (minor > (uint8_t)-1))
		goto out;

	proto->pv_major = major;
	proto->pv_minor = minor;
	err = 0;

out:
	return err;
}


errcode_t o2cb_create_cluster(const char *cluster_name)
{
	char path[PATH_MAX];
	int ret;
	errcode_t err = 0;

	ret = snprintf(path, PATH_MAX - 1, O2CB_FORMAT_CLUSTER,
		       configfs_path, cluster_name);
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
		       configfs_path, cluster_name);
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
		       configfs_path, cluster_name, node_name,
		       attr_name);
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
		       configfs_path, cluster_name, node_name,
		       attr_name);
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
		       configfs_path, cluster_name, node_name);
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
		       configfs_path, cluster_name, node_name);
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


static errcode_t try_file(const char *name, int *fd)
{
	int open_fd;
	errcode_t err = 0;

	open_fd = open(name, O_RDONLY);
	if (open_fd < 0) {
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
	}

	if (!err)
		*fd = open_fd;

	return err;
}

#define O2CB_NEW_CONFIGFS_PATH "/sys/kernel"
#define O2CB_OLD_CONFIGFS_PATH ""
#define CONFIGFS_MAGIC 0x62656570
static errcode_t try_configfs_path(const char *path)
{
	errcode_t ret;
	char attr_path[PATH_MAX];
	struct stat64 stat_buf;
	struct statfs64 statfs_buf;

	ret = snprintf(attr_path, PATH_MAX - 1, CONFIGFS_FORMAT_PATH,
		       path);
	if ((ret <= 0) || (ret == (PATH_MAX - 1)))
		return O2CB_ET_INTERNAL_FAILURE;

	ret = stat64(attr_path, &stat_buf);
	if (ret || !S_ISDIR(stat_buf.st_mode))
		return O2CB_ET_SERVICE_UNAVAILABLE;
	ret = statfs64(attr_path, &statfs_buf);
	if (ret || (statfs_buf.f_type != CONFIGFS_MAGIC))
		return O2CB_ET_SERVICE_UNAVAILABLE;

	return 0;
}

static errcode_t init_configfs(void)
{
	configfs_path = O2CB_NEW_CONFIGFS_PATH;
	if (!try_configfs_path(configfs_path))
		return 0;

	configfs_path = O2CB_OLD_CONFIGFS_PATH;
	if (!try_configfs_path(configfs_path))
		return 0;

	configfs_path = NULL;

	return O2CB_ET_SERVICE_UNAVAILABLE;
}

#define O2CB_INTERFACE_REVISION_PATH_OLD_PROC	"/proc/fs/ocfs2_nodemanager/interface_revision"
#define O2CB_INTERFACE_REVISION_PATH_OLD_SYS	"/sys/o2cb/interface_revision"
#define O2CB_INTERFACE_REVISION_PATH		"/sys/fs/o2cb/interface_revision"
errcode_t o2cb_init(void)
{
	int ret, fd;
	unsigned int module_version;
	errcode_t err;
	char revision_string[16];

	err = determine_stack();
	if (err)
		return err;

	err = try_file(O2CB_INTERFACE_REVISION_PATH, &fd);
	if (err == O2CB_ET_SERVICE_UNAVAILABLE)
		err = try_file(O2CB_INTERFACE_REVISION_PATH_OLD_SYS, &fd);
	if (err == O2CB_ET_SERVICE_UNAVAILABLE)
		err = try_file(O2CB_INTERFACE_REVISION_PATH_OLD_PROC, &fd);
	if (err)
		return err;

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

	if (O2NM_API_VERSION < module_version)
		return O2CB_ET_BAD_VERSION;

	return init_configfs();
}

/* o2cb_get_region_attribute() would just be s/set/get/ of this function */
static errcode_t o2cb_set_region_attribute(const char *cluster_name,
					   const char *region_name,
					   const char *attr_name,
					   const char *attr_value)
{
	int ret;
	char attr_path[PATH_MAX];

	ret = snprintf(attr_path, PATH_MAX - 1,
		       O2CB_FORMAT_HEARTBEAT_REGION_ATTR,
		       configfs_path, cluster_name, region_name,
		       attr_name);
	if ((ret <= 0) || (ret == (PATH_MAX - 1)))
		return O2CB_ET_INTERNAL_FAILURE;

	return o2cb_set_attribute(attr_path, attr_value);
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
					      uint64_t blocks)
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
		       configfs_path, cluster_name, region_name);
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
		       configfs_path, cluster_name, region_name);
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
static errcode_t classic_group_leave(struct o2cb_cluster_desc *cluster,
				     struct o2cb_region_desc *region)
{
	errcode_t ret, up_ret;
	int hb_refs;
	int semid;

	ret = o2cb_mutex_down_lookup(region->r_name, &semid);
	if (ret)
		return ret;

	ret = __o2cb_get_num_refs(semid, &hb_refs);
	if (ret)
		goto up;

	/* A previous process may have died and left us with no
	 * references on the region. We avoid a negative error count
	 * here and clean up the region as normal. */
	if (hb_refs) {
		ret = __o2cb_drop_ref(semid, !region->r_persist);
		if (ret)
			goto up;

		/* No need to call get_num_refs again -- this was
		 * atomic so we know what the new value must be. */
		hb_refs--;
	}

	if (!hb_refs) {
		/* XXX: If this fails, shouldn't we still destroy the
		 * semaphore set? */
		ret = o2cb_remove_heartbeat_region(cluster->c_cluster,
						   region->r_name);
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

static errcode_t classic_begin_group_join(struct o2cb_cluster_desc *cluster,
					  struct o2cb_region_desc *region)
{
	errcode_t ret, up_ret;
	int semid;

	ret = o2cb_mutex_down_lookup(region->r_name, &semid);
	if (ret)
		return ret;

	ret = o2cb_create_heartbeat_region(cluster->c_cluster,
					   region->r_name,
					   region->r_device_name,
					   region->r_block_bytes,
					   region->r_start_block,
					   region->r_blocks);
	if (ret && ret != O2CB_ET_REGION_EXISTS)
		goto up;

	ret = __o2cb_get_ref(semid, !region->r_persist);
	/* XXX: Maybe stop heartbeat on error here? */
up:
	up_ret = o2cb_mutex_up(semid);
	if (up_ret && !ret)
		ret = up_ret;

	return ret;
}

static errcode_t classic_complete_group_join(struct o2cb_cluster_desc *cluster,
					     struct o2cb_region_desc *region,
					     int result)
{
	errcode_t ret = 0;

	if (result)
		ret = classic_group_leave(cluster, region);

	return ret;
}

static errcode_t user_parse_status(char **args, int *error, char **error_msg)
{
	errcode_t err = O2CB_ET_IO;
	long result;
	char *ptr = NULL;

	result = strtol(args[0], &ptr, 10);
	if (ptr && *ptr != '\0') {
		/* fprintf(stderr, "Invalid error code string: %s", args[0]); */
	} else if ((result == LONG_MIN) || (result == LONG_MAX) ||
		   (result < INT_MIN) || (result > INT_MAX)) {
		/* fprintf(stderr, "Error code %ld out of range", err); */
	} else {
		*error_msg = args[1];
		*error = result;
		err = 0;
	}

	return err;
}

static errcode_t user_begin_group_join(struct o2cb_cluster_desc *cluster,
				       struct o2cb_region_desc *region)
{
	errcode_t err;
	int rc;
	int error;
	char *error_msg;
	client_message message;
	char *argv[OCFS2_CONTROLD_MAXARGS + 1];
	char buf[OCFS2_CONTROLD_MAXLINE];

	if (control_daemon_fd != -1) {
		/* fprintf(stderr, "Join already in progress!\n"); */
		err = O2CB_ET_INTERNAL_FAILURE;
		goto out;
	}

	rc = ocfs2_client_connect();
	if (rc < 0) {
		/* fprintf(stderr, "Unable to connect to ocfs2_controld: %s\n",
			strerror(-rc)); */
		switch (rc) {
			case -EACCES:
			case -EPERM:
				err = O2CB_ET_PERMISSION_DENIED;
				break;

			default:
				err = O2CB_ET_SERVICE_UNAVAILABLE;
				break;
		}
		goto out;
	}
	control_daemon_fd = rc;

	rc = send_message(control_daemon_fd, CM_MOUNT, OCFS2_FS_NAME,
			  region->r_name, cluster->c_cluster,
			  region->r_device_name, region->r_service);
	if (rc) {
		/* fprintf(stderr, "Unable to send MOUNT message: %s\n",
			strerror(-rc)); */
		err = O2CB_ET_IO;
		goto out;
	}

	rc = receive_message(control_daemon_fd, buf, &message, argv);
	if (rc < 0) {
		/* fprintf(stderr, "Error reading from daemon: %s\n",
			strerror(-rc)); */
		err = O2CB_ET_IO;
		goto out;
	}

	switch (message) {
		case CM_STATUS:
			err = user_parse_status(argv, &error, &error_msg);
			if (err) {
				/* fprintf(stderr, "Bad status message: %s\n",
					strerror(-rc)); */
				goto out;
			}
			if (error && (error != EALREADY)) {
				/* fprintf(stderr,
					"Error %d from daemon: %s\n",
					error, error_msg); */
				err = O2CB_ET_CONFIGURATION_ERROR;
				goto out;
			}
			break;

		default:
			/* fprintf(stderr,
				"Unexpected message %s from daemon\n",
				message_to_string(message)); */
			err = O2CB_ET_INTERNAL_FAILURE;
			goto out;
			break;
	}

	err = 0;

out:
	if (err && (control_daemon_fd != -1)) {
		close(control_daemon_fd);
		control_daemon_fd = -1;
	}

	return err;
}

static errcode_t user_complete_group_join(struct o2cb_cluster_desc *cluster,
					  struct o2cb_region_desc *region,
					  int result)
{
	errcode_t err = O2CB_ET_SERVICE_UNAVAILABLE;
	int rc;
	int error;
	char *error_msg;
	client_message message;
	char *argv[OCFS2_CONTROLD_MAXARGS + 1];
	char buf[OCFS2_CONTROLD_MAXLINE];

	if (control_daemon_fd == -1) {
		/* fprintf(stderr, "Join not started!\n"); */
		err = O2CB_ET_SERVICE_UNAVAILABLE;
		goto out;
	}

	rc = send_message(control_daemon_fd, CM_MRESULT, OCFS2_FS_NAME,
			  region->r_name, result, region->r_service);
	if (rc) {
		/* fprintf(stderr, "Unable to send MRESULT message: %s\n",
			strerror(-rc)); */
		err = O2CB_ET_IO;
		goto out;
	}

	rc = receive_message(control_daemon_fd, buf, &message, argv);
	if (rc < 0) {
		/* fprintf(stderr, "Error reading from daemon: %s\n",
			strerror(-rc)); */
		err = O2CB_ET_IO;
		goto out;
	}

	switch (message) {
		case CM_STATUS:
			err = user_parse_status(argv, &error, &error_msg);
			if (err) {
				/* fprintf(stderr, "Bad status message: %s\n",
					strerror(-rc)); */
				goto out;
			}
			if (error) {
				/* fprintf(stderr,
					"Error %d from daemon: %s\n",
					error, error_msg); */
				err = O2CB_ET_CONFIGURATION_ERROR;
			}
			break;

		default:
			/* fprintf(stderr,
				"Unexpected message %s from daemon\n",
				message_to_string(message)); */
			err = O2CB_ET_INTERNAL_FAILURE;
			goto out;
			break;
	}

	err = 0;

out:
	if (control_daemon_fd != -1) {
		close(control_daemon_fd);
		control_daemon_fd = -1;
	}

	return err;
}

static errcode_t user_group_leave(struct o2cb_cluster_desc *cluster,
				  struct o2cb_region_desc *region)
{
	errcode_t err = O2CB_ET_SERVICE_UNAVAILABLE;
	int rc;
	int error;
	char *error_msg;
	client_message message;
	char *argv[OCFS2_CONTROLD_MAXARGS + 1];
	char buf[OCFS2_CONTROLD_MAXLINE];

	if (control_daemon_fd != -1) {
		/* fprintf(stderr, "Join in progress!\n"); */
		err = O2CB_ET_INTERNAL_FAILURE;
		goto out;
	}

	rc = ocfs2_client_connect();
	if (rc < 0) {
		/* fprintf(stderr, "Unable to connect to ocfs2_controld: %s\n",
			strerror(-rc)); */
		switch (rc) {
			case -EACCES:
			case -EPERM:
				err = O2CB_ET_PERMISSION_DENIED;
				break;

			default:
				err = O2CB_ET_SERVICE_UNAVAILABLE;
				break;
		}
		goto out;
	}
	control_daemon_fd = rc;

	rc = send_message(control_daemon_fd, CM_UNMOUNT, OCFS2_FS_NAME,
			  region->r_name, region->r_service);
	if (rc) {
		/* fprintf(stderr, "Unable to send UNMOUNT message: %s\n",
			strerror(-rc)); */
		err = O2CB_ET_IO;
		goto out;
	}

	rc = receive_message(control_daemon_fd, buf, &message, argv);
	if (rc < 0) {
		/* fprintf(stderr, "Error reading from daemon: %s\n",
			strerror(-rc)); */
		err = O2CB_ET_IO;
		goto out;
	}

	switch (message) {
		case CM_STATUS:
			err = user_parse_status(argv, &error, &error_msg);
			if (err) {
				/* fprintf(stderr, "Bad status message: %s\n",
					strerror(-rc)); */
				goto out;
			}
			if (error) {
				/* fprintf(stderr,
					"Error %d from daemon: %s\n",
					error, error_msg); */
				err = O2CB_ET_CONFIGURATION_ERROR;
				goto out;
			}
			break;

		default:
			/* fprintf(stderr,
				"Unexpected message %s from daemon\n",
				message_to_string(message)); */
			err = O2CB_ET_INTERNAL_FAILURE;
			goto out;
			break;
	}

	err = 0;

out:
	if (control_daemon_fd != -1) {
		close(control_daemon_fd);
		control_daemon_fd = -1;
	}

	return err;
}

static errcode_t o2cb_validate_cluster_desc(struct o2cb_cluster_desc *desc)
{
	errcode_t err;
	const char *name;

	if (!desc)
		return O2CB_ET_INVALID_STACK_NAME;

	if (desc->c_stack && !desc->c_cluster)
		return O2CB_ET_INVALID_STACK_NAME;

	err = o2cb_get_stack_name(&name);
	if (err)
		return err;

	if (desc->c_stack) {
		if (strcmp(desc->c_stack, name))
			return O2CB_ET_INVALID_STACK_NAME;
	} else if (strcmp(name, classic_stack.s_name))
		return O2CB_ET_INVALID_STACK_NAME;

	return 0;
}

errcode_t o2cb_begin_group_join(struct o2cb_cluster_desc *cluster,
				struct o2cb_region_desc *region)
{
	errcode_t err;
	struct o2cb_cluster_desc desc;
	char _fake_cluster_name[NAME_MAX];

	if (!current_stack)
		return O2CB_ET_SERVICE_UNAVAILABLE;

	err = o2cb_validate_cluster_desc(cluster);
	if (err)
		return err;

	desc = *cluster;
	if (!desc.c_cluster) {
		err = _fake_default_cluster(_fake_cluster_name);
		if (err)
			return err;
		desc.c_cluster = _fake_cluster_name;
	}

	return current_stack->s_ops->begin_group_join(&desc, region);
}

errcode_t o2cb_complete_group_join(struct o2cb_cluster_desc *cluster,
				   struct o2cb_region_desc *region,
				   int result)
{
	errcode_t err;
	struct o2cb_cluster_desc desc;
	char _fake_cluster_name[NAME_MAX];

	if (!current_stack)
		return O2CB_ET_SERVICE_UNAVAILABLE;

	err = o2cb_validate_cluster_desc(cluster);
	if (err)
		return err;

	desc = *cluster;
	if (!desc.c_cluster) {
		err = _fake_default_cluster(_fake_cluster_name);
		if (err)
			return err;
		desc.c_cluster = _fake_cluster_name;
	}

	return current_stack->s_ops->complete_group_join(&desc, region,
							 result);
}

errcode_t o2cb_group_leave(struct o2cb_cluster_desc *cluster,
			   struct o2cb_region_desc *region)
{
	errcode_t err;
	struct o2cb_cluster_desc desc;
	char _fake_cluster_name[NAME_MAX];

	if (!current_stack)
		return O2CB_ET_SERVICE_UNAVAILABLE;

	err = o2cb_validate_cluster_desc(cluster);
	if (err)
		return err;

	desc = *cluster;
	if (!desc.c_cluster) {
		err = _fake_default_cluster(_fake_cluster_name);
		if (err)
			return err;
		desc.c_cluster = _fake_cluster_name;
	}

	return current_stack->s_ops->group_leave(&desc, region);
}

void o2cb_free_cluster_desc(struct o2cb_cluster_desc *cluster)
{
	if (cluster->c_stack)
		free(cluster->c_stack);
	if (cluster->c_cluster)
		free(cluster->c_cluster);
}

errcode_t o2cb_running_cluster_desc(struct o2cb_cluster_desc *cluster)
{
	errcode_t err;
	const char *stack;
	char **clusters = NULL;

	err = o2cb_get_stack_name(&stack);
	if (err)
		return err;

	if (!strcmp(stack, classic_stack.s_name)) {
		cluster->c_stack = NULL;
		cluster->c_cluster = NULL;
		return 0;
	}

	cluster->c_stack = strdup(stack);
	if (!cluster->c_stack)
		return O2CB_ET_NO_MEMORY;

	err = o2cb_list_clusters(&clusters);
	if (err) {
		free(cluster->c_stack);
		return err;
	}

	/* The first cluster is the default cluster */
	if (clusters[0]) {
		cluster->c_cluster = strdup(clusters[0]);
		if (!cluster->c_cluster) {
			free(cluster->c_stack);
			err = O2CB_ET_NO_MEMORY;
		}
	}
	o2cb_free_cluster_list(clusters);

	return 0;
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
	char statpath[PATH_MAX];
	struct stat stat_buf;
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

		snprintf(statpath, sizeof(statpath), "%s/%s", path,
			 dirent->d_name);

		/* Silently ignore, we can't access it anyway */
		if (lstat(statpath, &stat_buf))
			continue;

		/* Non-directories are attributes */
		if (!S_ISDIR(stat_buf.st_mode))
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

static errcode_t classic_list_clusters(char ***clusters)
{
	char path[PATH_MAX];
	errcode_t ret;

	if (configfs_path == NULL)
		return O2CB_ET_SERVICE_UNAVAILABLE;

	ret = snprintf(path, PATH_MAX - 1, O2CB_FORMAT_CLUSTER_DIR,
		       configfs_path);
	if ((ret <= 0) || (ret == (PATH_MAX - 1)))
		return O2CB_ET_INTERNAL_FAILURE;

	return o2cb_list_dir(path, clusters);
}

static errcode_t user_list_clusters(char ***clusters)
{
	errcode_t err = O2CB_ET_SERVICE_UNAVAILABLE;
	int rc, fd = -1;
	char buf[OCFS2_CONTROLD_MAXLINE];

	rc = ocfs2_client_connect();
	if (rc < 0) {
		/* fprintf(stderr, "Unable to connect to ocfs2_controld: %s\n",
			strerror(-rc)); */
		switch (rc) {
			case -EACCES:
			case -EPERM:
				err = O2CB_ET_PERMISSION_DENIED;
				break;

			default:
				err = O2CB_ET_SERVICE_UNAVAILABLE;
				break;
		}
		goto out;
	}
	fd = rc;

	rc = send_message(fd, CM_LISTCLUSTERS);
	if (rc) {
		/* fprintf(stderr,
			"Unable to send LISTCLUSTERS message: %s\n",
			strerror(-rc)); */
		err = O2CB_ET_IO;
		goto out;
	}

	rc = receive_list(fd, buf, clusters);
	if (rc) {
		/* fprintf(stderr, "Error reading from daemon: %s\n",
			strerror(-rc)); */
		err = O2CB_ET_IO;
		goto out;
	}

	err = 0;

out:
	if (fd != -1)
		close(fd);

	return err;
}

errcode_t o2cb_list_clusters(char ***clusters)
{
	if (!current_stack)
		return O2CB_ET_SERVICE_UNAVAILABLE;

	return current_stack->s_ops->list_clusters(clusters);
}

void o2cb_free_cluster_list(char **clusters)
{
	o2cb_free_dir_list(clusters);
}

errcode_t o2cb_list_nodes(char *cluster_name, char ***nodes)
{
	char path[PATH_MAX];
	errcode_t ret;

	if (configfs_path == NULL)
		return O2CB_ET_SERVICE_UNAVAILABLE;

	ret = snprintf(path, PATH_MAX - 1, O2CB_FORMAT_NODE_DIR,
		       configfs_path, cluster_name);
	if ((ret <= 0) || (ret == (PATH_MAX - 1)))
		return O2CB_ET_INTERNAL_FAILURE;

	return o2cb_list_dir(path, nodes);
}

void o2cb_free_nodes_list(char **nodes)
{
	o2cb_free_dir_list(nodes);
}

static errcode_t dump_list_to_string(char **dump_list, char **debug)
{
	int i;
	size_t len, count = 0;
	char *ptr;

	for (i = 0; dump_list[i]; i++)
		count += strlen(dump_list[i]);

	*debug = malloc(sizeof(char) * (count + 1));
	if (!*debug)
		return O2CB_ET_NO_MEMORY;

	ptr = *debug;
	ptr[count] = '\0';
	for (i = 0; dump_list[i]; i++) {
		len = strlen(dump_list[i]);
		memcpy(ptr, dump_list[i], len);
		ptr += len;
	}

	return 0;
}

errcode_t o2cb_control_daemon_debug(char **debug)
{
	errcode_t err = O2CB_ET_SERVICE_UNAVAILABLE;
	int rc, fd = -1;
	char buf[OCFS2_CONTROLD_MAXLINE];
	char **dump_list = NULL;

	rc = ocfs2_client_connect();
	if (rc < 0) {
		/* fprintf(stderr, "Unable to connect to ocfs2_controld: %s\n",
			strerror(-rc)); */
		switch (rc) {
			case -EACCES:
			case -EPERM:
				err = O2CB_ET_PERMISSION_DENIED;
				break;

			default:
				err = O2CB_ET_SERVICE_UNAVAILABLE;
				break;
		}
		goto out;
	}
	fd = rc;

	rc = send_message(fd, CM_DUMP);
	if (rc) {
		/* fprintf(stderr,
			"Unable to send DUMP message: %s\n",
			strerror(-rc)); */
		err = O2CB_ET_IO;
		goto out;
	}

	rc = receive_list(fd, buf, &dump_list);
	if (rc) {
		/* fprintf(stderr, "Error reading from daemon: %s\n",
			strerror(-rc)); */
		err = O2CB_ET_IO;
		goto out;
	}

	err = dump_list_to_string(dump_list, debug);
	o2cb_free_dir_list(dump_list);

out:
	if (fd != -1)
		close(fd);

	return err;
}


errcode_t o2cb_get_hb_thread_pid (const char *cluster_name, const char *region_name,
			   pid_t *pid)
{
	char attr_path[PATH_MAX];
	char _fake_cluster_name[NAME_MAX];
	char attr_value[16];
	errcode_t ret;

	if (!cluster_name) {
		ret = _fake_default_cluster(_fake_cluster_name);
		if (ret)
			return ret;
		cluster_name = _fake_cluster_name;
	}

	ret = snprintf(attr_path, PATH_MAX - 1,
		       O2CB_FORMAT_HEARTBEAT_REGION_ATTR,
		       configfs_path, cluster_name, region_name,
		       "pid");

	if ((ret <= 0) || (ret == (PATH_MAX - 1)))
		return O2CB_ET_INTERNAL_FAILURE;

	ret = o2cb_get_attribute(attr_path, attr_value, sizeof(attr_value) - 1);
	if (ret == 0)
		*pid = atoi (attr_value);

	return ret;
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

/*
 * The handshake is pretty simple.  We need to read all supported control
 * device protocols from the kernel.  Once we've read them, we can write
 * the protocol we want to use.  After that, we're good to go.
 *
 * Right now, we will just allow the T01 protocol and not write any
 * code to handle multiples.  We'll add that later if and when it is
 * necessary.
 *
 * The versions read from the kernel are all 4 characers including the
 * newline.
 */
#define OCFS2_CONTROL_PROTO			"T01\n"
#define OCFS2_CONTROL_PROTO_LEN			4
#define OCFS2_CONTROL_MESSAGE_SETNODE_OP	"SETN"
#define OCFS2_CONTROL_MESSAGE_SETNODE_TOTAL_LEN	14
#define OCFS2_CONTROL_MESSAGE_SETVERSION_OP	"SETV"
#define OCFS2_CONTROL_MESSAGE_SETVERSION_TOTAL_LEN	11
#define OCFS2_CONTROL_MESSAGE_DOWN_OP		"DOWN"
#define OCFS2_CONTROL_MESSAGE_DOWN_TOTAL_LEN	47
#define OCFS2_CONTROL_MESSAGE_NODENUM_LEN	8
static errcode_t o2cb_control_handshake(unsigned int this_node,
					struct ocfs2_protocol_version *proto)
{
	errcode_t err = 0;
	int found = 0;
	size_t ret;
	char buf[OCFS2_CONTROL_MESSAGE_SETNODE_TOTAL_LEN + 1];

	if (control_device_fd == -1) {
		err = O2CB_ET_INTERNAL_FAILURE;
		goto out;
	}

	buf[OCFS2_CONTROL_PROTO_LEN] = '\0';
	while (1)
	{
		ret = read(control_device_fd, buf, OCFS2_CONTROL_PROTO_LEN);
		if (ret == OCFS2_CONTROL_PROTO_LEN) {
			if (!found && !strcmp(buf,
					      OCFS2_CONTROL_PROTO))
				found = 1;
			continue;
		}

		if (ret != 0)
			err = O2CB_ET_IO;
		else if (!found)
			err = O2CB_ET_SERVICE_UNAVAILABLE;  /* no match */
		break;
	}

	if (err)
		goto out;

	ret = write(control_device_fd, OCFS2_CONTROL_PROTO,
		    OCFS2_CONTROL_PROTO_LEN);
	if (ret != OCFS2_CONTROL_PROTO_LEN) {
		err = O2CB_ET_IO;
		goto out;
	}

	snprintf(buf, OCFS2_CONTROL_MESSAGE_SETNODE_TOTAL_LEN + 1,
		 OCFS2_CONTROL_MESSAGE_SETNODE_OP " %08X\n", this_node);
	ret = write(control_device_fd, buf,
		    OCFS2_CONTROL_MESSAGE_SETNODE_TOTAL_LEN);
	if (ret != OCFS2_CONTROL_MESSAGE_SETNODE_TOTAL_LEN)
		err = O2CB_ET_IO;

	snprintf(buf, OCFS2_CONTROL_MESSAGE_SETVERSION_TOTAL_LEN + 1,
		 OCFS2_CONTROL_MESSAGE_SETVERSION_OP " %02X %02X\n",
		 proto->pv_major, proto->pv_minor);
	ret = write(control_device_fd, buf,
		    OCFS2_CONTROL_MESSAGE_SETVERSION_TOTAL_LEN);
	if (ret != OCFS2_CONTROL_MESSAGE_SETVERSION_TOTAL_LEN)
		err = O2CB_ET_IO;

out:
	return err;
}

errcode_t o2cb_control_open(unsigned int this_node,
			    struct ocfs2_protocol_version *proto)
{
	errcode_t err = 0;
	int rc;

	if (!current_stack) {
		err = O2CB_ET_SERVICE_UNAVAILABLE;
		goto out;
	}

	if (control_device_fd != -1)
		goto out;

	rc = open(CONTROL_DEVICE, O_RDWR);
	if (rc < 0) {
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
		goto out;
	}

	control_device_fd = rc;

	err = o2cb_control_handshake(this_node, proto);
	if (err) {
		close(control_device_fd);
		control_device_fd = -1;
	}

out:
	return err;
}

void o2cb_control_close(void)
{
	if (control_device_fd != -1) {
		close(control_device_fd);
		control_device_fd = -1;
	}
}

errcode_t o2cb_control_node_down(const char *uuid, unsigned int nodeid)
{
	errcode_t err = 0;
	size_t ret;
	char buf[OCFS2_CONTROL_MESSAGE_DOWN_TOTAL_LEN + 1];

	if (control_device_fd == -1)
		return O2CB_ET_INTERNAL_FAILURE;

	snprintf(buf, OCFS2_CONTROL_MESSAGE_DOWN_TOTAL_LEN + 1,
		 "DOWN %.32s %08X\n", uuid, nodeid);
	ret = write(control_device_fd, buf,
		    OCFS2_CONTROL_MESSAGE_DOWN_TOTAL_LEN);
	if (ret != OCFS2_CONTROL_MESSAGE_DOWN_TOTAL_LEN)
		err = O2CB_ET_IO;

	return err;
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
