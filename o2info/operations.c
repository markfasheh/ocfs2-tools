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

static void o2i_info(struct o2info_operation *op, const char *fmt, ...)
{
	va_list ap;

	fprintf(stdout, "%s Info: ", op->to_name);
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);

	return;
}

static void o2i_error(struct o2info_operation *op, const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "%s Error: ", op->to_name);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);

	return;
}

/*
 * Helper to scan all requests:
 *
 * - Print all errors and unknown requests.
 * - Return number of unknown requests.
 * - Return number of errors.
 * - Return number of handled requesets.
 * - Return first and last error code.
 */
static void o2i_scan_requests(struct o2info_operation *op,
			      struct ocfs2_info info, uint32_t *unknowns,
			      uint32_t *errors, uint32_t *fills)
{
	uint32_t i, num_unknown = 0, num_error = 0, num_filled = 0;
	uint64_t *reqs;
	struct ocfs2_info_request *req;

	for (i = 0; i < info.oi_count; i++) {

		reqs = (uint64_t *)info.oi_requests;
		req = (struct ocfs2_info_request *)reqs[i];
		if (req->ir_flags & OCFS2_INFO_FL_ERROR) {
			o2i_error(op, "o2info request(%d) failed.\n",
				  req->ir_code);
			num_error++;
			continue;
		}

		if (!(req->ir_flags & OCFS2_INFO_FL_FILLED)) {
			o2i_info(op, "o2info request(%d) is unsupported.\n",
				 req->ir_code);
			num_unknown++;
			continue;
		}

		num_filled++;
	}

	*unknowns = num_unknown;
	*errors = num_error;
	*fills = num_filled;
}

struct o2info_fs_features {
	uint32_t compat;
	uint32_t incompat;
	uint32_t rocompat;
};

static int get_fs_features_ioctl(struct o2info_operation *op,
				 int fd,
				 struct o2info_fs_features *ofs)
{
	int rc = 0, flags = 0;
	uint32_t unknowns = 0, errors = 0, fills = 0;
	uint64_t reqs[1];
	struct ocfs2_info_fs_features oif;
	struct ocfs2_info info;

	memset(ofs, 0, sizeof(*ofs));

	if (!cluster_coherent)
		flags |= OCFS2_INFO_FL_NON_COHERENT;

	o2info_fill_request((struct ocfs2_info_request *)&oif, sizeof(oif),
			    OCFS2_INFO_FS_FEATURES, flags);

	reqs[0] = (unsigned long)&oif;

	info.oi_requests = (uint64_t)reqs;
	info.oi_count = 1;

	rc = ioctl(fd, OCFS2_IOC_INFO, &info);
	if (rc) {
		rc = errno;
		o2i_error(op, "ioctl failed: %s\n", strerror(rc));
		o2i_scan_requests(op, info, &unknowns, &errors, &fills);
		goto out;
	}

	if (oif.if_req.ir_flags & OCFS2_INFO_FL_FILLED) {
		ofs->compat = oif.if_compat_features;
		ofs->incompat = oif.if_incompat_features;
		ofs->rocompat = oif.if_ro_compat_features;
	}

out:
	return rc;
}

static int get_fs_features_libocfs2(struct o2info_operation *op,
				    ocfs2_filesys *fs,
				    struct o2info_fs_features *ofs)
{
	int rc = 0;
	struct ocfs2_super_block *sb = NULL;

	memset(ofs, 0, sizeof(*ofs));

	sb = OCFS2_RAW_SB(fs->fs_super);
	ofs->compat = sb->s_feature_compat;
	ofs->incompat = sb->s_feature_incompat;
	ofs->rocompat = sb->s_feature_ro_compat;

	return rc;
}

static void o2info_print_line(char const *qualifier, char *content,
			      char splitter)
{
	char *ptr = NULL, *token = NULL, *tmp = NULL;
	uint32_t max_len = 80, len = 0;

	tmp = malloc(max_len);
	ptr = content;

	snprintf(tmp, max_len, "%s", qualifier);
	fprintf(stdout, "%s", tmp);
	len += strlen(tmp);

	while (ptr) {

		token = ptr;
		ptr = strchr(ptr, splitter);

		if (ptr)
			*ptr = 0;

		if (strcmp(token, "") != 0) {
			snprintf(tmp, max_len, "%s ", token);
			len += strlen(tmp);
			if (len > max_len) {
				fprintf(stdout, "\n");
				len = 0;
				snprintf(tmp, max_len, "%s", qualifier);
				fprintf(stdout, "%s", tmp);
				len += strlen(tmp);
				snprintf(tmp, max_len, "%s ", token);
				fprintf(stdout, "%s", tmp);
				len += strlen(tmp);
			} else
				fprintf(stdout, "%s", tmp);
		}

		if (!ptr)
			break;

		ptr++;
	}

	fprintf(stdout, "\n");

	if (tmp)
		ocfs2_free(&tmp);
}

static int fs_features_run(struct o2info_operation *op,
			   struct o2info_method *om,
			   void *arg)
{
	int rc = 0;
	static struct o2info_fs_features ofs;

	char *compat = NULL;
	char *incompat = NULL;
	char *rocompat = NULL;
	char *features = NULL;

	if (om->om_method == O2INFO_USE_IOCTL)
		rc = get_fs_features_ioctl(op, om->om_fd, &ofs);
	else
		rc = get_fs_features_libocfs2(op, om->om_fs, &ofs);
	if (rc)
		goto out;

	rc = o2info_get_compat_flag(ofs.compat, &compat);
	if (rc)
		goto out;

	rc = o2info_get_incompat_flag(ofs.incompat, &incompat);
	if (rc)
		goto out;

	rc = o2info_get_rocompat_flag(ofs.rocompat, &rocompat);
	if (rc)
		goto out;

	features = malloc(strlen(compat) + strlen(incompat) +
			  strlen(rocompat) + 3);

	sprintf(features, "%s %s %s", compat, incompat, rocompat);

	o2info_print_line("", features, ' ');

out:
	if (compat)
		ocfs2_free(&compat);

	if (incompat)
		ocfs2_free(&incompat);

	if (rocompat)
		ocfs2_free(&rocompat);

	if (features)
		ocfs2_free(&features);

	return rc;
}

DEFINE_O2INFO_OP(fs_features,
		 fs_features_run,
		 NULL);
