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
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

#include <linux/types.h>

#include "o2cb.h"

#include "o2cb_abi.h"

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

errcode_t o2cb_create_heartbeat_region_disk(const char *cluster_name,
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

	fd = open(device_name, O_RDWR);
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

errcode_t o2cb_remove_heartbeat_region_disk(const char *cluster_name,
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
	if (fd == -1)
		return errno;

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
