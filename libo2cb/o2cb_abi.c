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
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <linux/types.h>

#include "o2cb.h"

#include "o2cb_abi.h"

int o2cb_set_cluster_name(const char *cluster_name)
{
	int fd, rc, page_size = getpagesize();
	char *buf;
	nm_op *op;

	if (strlen(cluster_name) > NM_MAX_NAME_LEN)
		return O2CB_ET_INVALID_CLUSTER_NAME;

	buf = malloc(sizeof(char*) * page_size);
	if (!buf)
		return -errno;

	op = (nm_op *)buf;
	op->magic = NM_OP_MAGIC;
	op->opcode = NM_OP_NAME_CLUSTER;
	strcpy(op->arg_u.name, cluster_name);

	fd = open(O2CB_CLUSTER_FILE, O_RDWR);
	if (fd < 0) {
		rc = -errno;
		goto out;
	}

	rc = write(fd, op, sizeof(nm_op));
	if (rc < 0) {
		rc = -errno;
		goto out_close;
	} else if (rc < sizeof(nm_op)) {
		/* FIXME: What to do here? */
	}

	memset(buf, 0, page_size);
	rc = read(fd, buf, page_size);
	if (rc < 0) {
		rc = -errno;
		goto out_close;
	} else if (!rc) {
		/* FIXME: What to do here? */
	} else {
		if (buf[0] == '\0')
			rc = 0;
		/* FIXME: genericize, make better, etc */
	}

out_close:
	close(fd);
out:
	free(buf);

	return rc;
}  /* o2cb_set_cluster_name() */

