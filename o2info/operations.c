/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * operations.c
 *
 * Implementations for all o2info's operation.
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

#include <errno.h>
#include <sys/raw.h>
#include <inttypes.h>

#include "ocfs2/ocfs2.h"
#include "ocfs2/bitops.h"
#include "ocfs2-kernel/ocfs2_ioctl.h"
#include "ocfs2-kernel/kernel-list.h"
#include "tools-internal/verbose.h"

#include "utils.h"

extern void print_usage(int rc);
extern int cluster_coherent;

static inline void o2info_fill_request(struct ocfs2_info_request *req,
				       size_t size,
				       enum ocfs2_info_type code,
				       int flags)
{
	memset(req, 0, size);

	req->ir_magic = OCFS2_INFO_MAGIC;
	req->ir_size = size;
	req->ir_code = code,
	req->ir_flags = flags;
}

static void o2i_error(struct o2info_operation *op, const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "%s: ", op->to_name);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);

	return;
}
