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
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <linux/types.h>

#include "o2cb.h"

#include "o2cb_abi.h"

errcode_t o2cb_set_cluster_name(const char *cluster_name)
{
	errcode_t ret;
	int fd, rc, page_size = getpagesize();
	char *buf;
	nm_op *op;

	if (strlen(cluster_name) > NM_MAX_NAME_LEN)
		return O2CB_ET_INVALID_CLUSTER_NAME;

	buf = malloc(sizeof(char*) * page_size);
	if (!buf)
		return O2CB_ET_NO_MEMORY;

	op = (nm_op *)buf;
	op->magic = NM_OP_MAGIC;
	op->opcode = NM_OP_NAME_CLUSTER;
	strcpy(op->arg_u.name, cluster_name);

	ret = O2CB_ET_SERVICE_UNAVAILABLE;
	fd = open(O2CB_CLUSTER_FILE, O_RDWR);
	if (fd < 0)
		goto out;

	rc = write(fd, op, sizeof(nm_op));
	if (rc < 0)
		goto out_close;
	ret = O2CB_ET_IO;
	if (rc < sizeof(nm_op))
		goto out_close;

	memset(buf, 0, page_size);

	rc = read(fd, buf, page_size);
	if (rc < 0)
		goto out_close;

	ret = O2CB_ET_SERVICE_UNAVAILABLE;
	if (!rc)
		goto out_close;

	ret = O2CB_ET_IO;  /* FIXME */
	if (buf[0] == '0')
		ret = 0;

out_close:
	close(fd);
out:
	free(buf);

	return ret;
}  /* o2cb_set_cluster_name() */


errcode_t o2cb_add_node(nm_node_info *node)
{
	errcode_t ret;
	int fd, rc, page_size = getpagesize();
	char *buf;
	nm_op *op;

	buf = malloc(sizeof(char*) * page_size);
	if (!buf)
		return O2CB_ET_NO_MEMORY;

	op = (nm_op *)buf;
	op->magic = NM_OP_MAGIC;
	op->opcode = NM_OP_ADD_CLUSTER_NODE;
	memcpy(&(op->arg_u.node), node, sizeof(nm_node_info));

	ret = O2CB_ET_SERVICE_UNAVAILABLE;
	fd = open(O2CB_CLUSTER_FILE, O_RDWR);
	if (fd < 0)
		goto out;

	rc = write(fd, op, sizeof(nm_op));
	if (rc < 0)
		goto out_close;
	ret = O2CB_ET_IO;
	if (rc < sizeof(nm_op))
		goto out_close;

	memset(buf, 0, page_size);

	rc = read(fd, buf, page_size);
	if (rc < 0)
		goto out_close;

	ret = O2CB_ET_SERVICE_UNAVAILABLE;
	if (!rc)
		goto out_close;

	ret = O2CB_ET_IO;  /* FIXME */
	if (buf[0] == '0')
		ret = 0;

out_close:
	close(fd);
out:
	free(buf);

	return ret;
}  /* o2cb_add_node() */

errcode_t o2cb_activate_cluster()
{
	errcode_t ret;
	int fd, rc, page_size = getpagesize();
	char *buf;
	nm_op *op;

	buf = malloc(sizeof(char*) * page_size);
	if (!buf)
		return O2CB_ET_NO_MEMORY;

	op = (nm_op *)buf;
	op->magic = NM_OP_MAGIC;
	op->opcode = NM_OP_CREATE_CLUSTER;

	ret = O2CB_ET_SERVICE_UNAVAILABLE;
	fd = open(O2CB_CLUSTER_FILE, O_RDWR);
	if (fd < 0)
		goto out;

	rc = write(fd, op, sizeof(nm_op));
	if (rc < 0)
		goto out_close;

	ret = O2CB_ET_IO;
	if (rc < sizeof(nm_op))
		goto out_close;

	memset(buf, 0, page_size);

	rc = read(fd, buf, page_size);
	if (rc < 0)
		goto out_close;

	ret = O2CB_ET_SERVICE_UNAVAILABLE;
	if (!rc)
		goto out_close;

	ret = O2CB_ET_IO;  /* FIXME */
	if (buf[0] == '0')
		ret = 0;

out_close:
	close(fd);
out:
	free(buf);

	return ret;
}  /* o2cb_activate_cluster() */

/* FIXME: does this really belong here? */
errcode_t o2cb_activate_networking()
{
	errcode_t ret;
	int fd;
	net_ioc net;

	memset(&net, 0, sizeof(net_ioc));
	fd = open(O2CB_NETWORKING_FILE, O_RDONLY);
	if (fd < 0)
		return O2CB_ET_SERVICE_UNAVAILABLE;

	ret = 0;
	if (ioctl(fd, NET_IOC_ACTIVATE, &net)) {
		switch (errno) {
			default:
				ret = O2CB_ET_INTERNAL_FAILURE;
				break;

			case ENOTTY:
				ret = O2CB_ET_SERVICE_UNAVAILABLE;
				break;
		}
	}

	close(fd);
	return ret;
}  /* o2cb_activate_networking() */
