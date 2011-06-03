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
#include "libo2info.h"

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
		rc = o2info_get_fs_features(om->om_fs, &ofs);
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

static int get_volinfo_ioctl(struct o2info_operation *op,
			     int fd,
			     struct o2info_volinfo *vf)
{
	int rc = 0, flags = 0;
	uint32_t unknowns = 0, errors = 0, fills = 0;
	struct ocfs2_info_blocksize oib;
	struct ocfs2_info_clustersize oic;
	struct ocfs2_info_maxslots oim;
	struct ocfs2_info_label oil;
	struct ocfs2_info_uuid oiu;
	uint64_t reqs[5];
	struct ocfs2_info info;

	memset(vf, 0, sizeof(*vf));

	if (!cluster_coherent)
		flags |= OCFS2_INFO_FL_NON_COHERENT;

	o2info_fill_request((struct ocfs2_info_request *)&oib, sizeof(oib),
			    OCFS2_INFO_BLOCKSIZE, flags);
	o2info_fill_request((struct ocfs2_info_request *)&oic, sizeof(oic),
			    OCFS2_INFO_CLUSTERSIZE, flags);
	o2info_fill_request((struct ocfs2_info_request *)&oim, sizeof(oim),
			    OCFS2_INFO_MAXSLOTS, flags);
	o2info_fill_request((struct ocfs2_info_request *)&oil, sizeof(oil),
			    OCFS2_INFO_LABEL, flags);
	o2info_fill_request((struct ocfs2_info_request *)&oiu, sizeof(oiu),
			    OCFS2_INFO_UUID, flags);

	reqs[0] = (unsigned long)&oib;
	reqs[1] = (unsigned long)&oic;
	reqs[2] = (unsigned long)&oim;
	reqs[3] = (unsigned long)&oil;
	reqs[4] = (unsigned long)&oiu;

	info.oi_requests = (uint64_t)reqs;
	info.oi_count = 5;

	rc = ioctl(fd, OCFS2_IOC_INFO, &info);
	if (rc) {
		rc = errno;
		o2i_error(op, "ioctl failed: %s\n", strerror(rc));
		o2i_scan_requests(op, info, &unknowns, &errors, &fills);
		goto out;
	}

	if (oib.ib_req.ir_flags & OCFS2_INFO_FL_FILLED)
		vf->blocksize = oib.ib_blocksize;

	if (oic.ic_req.ir_flags & OCFS2_INFO_FL_FILLED)
		vf->clustersize = oic.ic_clustersize;

	if (oim.im_req.ir_flags & OCFS2_INFO_FL_FILLED)
		vf->maxslots = oim.im_max_slots;

	if (oil.il_req.ir_flags & OCFS2_INFO_FL_FILLED)
		memcpy(vf->label, oil.il_label, OCFS2_MAX_VOL_LABEL_LEN);

	if (oiu.iu_req.ir_flags & OCFS2_INFO_FL_FILLED)
		memcpy(vf->uuid_str, oiu.iu_uuid_str, OCFS2_TEXT_UUID_LEN + 1);

	rc = get_fs_features_ioctl(op, fd, &(vf->ofs));

out:
	return rc;
}

static int volinfo_run(struct o2info_operation *op,
		       struct o2info_method *om,
		       void *arg)
{
	int rc = 0;
	static struct o2info_volinfo vf;

	char *compat = NULL;
	char *incompat = NULL;
	char *rocompat = NULL;
	char *features = NULL;

#define VOLINFO "       Label: %s\n" \
		"        UUID: %s\n" \
		"  Block Size: %u\n" \
		"Cluster Size: %u\n" \
		"  Node Slots: %u\n"

	if (om->om_method == O2INFO_USE_IOCTL)
		rc = get_volinfo_ioctl(op, om->om_fd, &vf);
	else
		rc = o2info_get_volinfo(om->om_fs, &vf);
	if (rc)
		goto out;

	rc = o2info_get_compat_flag(vf.ofs.compat, &compat);
	if (rc)
		goto out;

	rc = o2info_get_incompat_flag(vf.ofs.incompat, &incompat);
	if (rc)
		goto out;

	rc = o2info_get_rocompat_flag(vf.ofs.rocompat, &rocompat);
	if (rc)
		goto out;

	features = malloc(strlen(compat) + strlen(incompat) +
			  strlen(rocompat) + 3);

	sprintf(features, "%s %s %s", compat, incompat, rocompat);

	fprintf(stdout, VOLINFO, vf.label, vf.uuid_str, vf.blocksize,
		vf.clustersize, vf.maxslots);

	o2info_print_line("    Features: ", features, ' ');

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

DEFINE_O2INFO_OP(volinfo,
		 volinfo_run,
		 NULL);

static int get_mkfs_ioctl(struct o2info_operation *op, int fd,
			  struct o2info_mkfs *oms)
{
	int rc = 0, flags = 0;
	uint32_t unknowns = 0, errors = 0, fills = 0;
	struct ocfs2_info_journal_size oij;
	uint64_t reqs[1];
	struct ocfs2_info info;

	memset(oms, 0, sizeof(*oms));

	if (!cluster_coherent)
		flags |= OCFS2_INFO_FL_NON_COHERENT;

	o2info_fill_request((struct ocfs2_info_request *)&oij, sizeof(oij),
			    OCFS2_INFO_JOURNAL_SIZE, flags);

	reqs[0] = (unsigned long)&oij;

	info.oi_requests = (uint64_t)reqs;
	info.oi_count = 1;

	rc = ioctl(fd, OCFS2_IOC_INFO, &info);
	if (rc) {
		rc = errno;
		o2i_error(op, "ioctl failed: %s\n", strerror(rc));
		o2i_scan_requests(op, info, &unknowns, &errors, &fills);
		goto out;
	}

	if (oij.ij_req.ir_flags & OCFS2_INFO_FL_FILLED)
		oms->journal_size = oij.ij_journal_size;

	rc = get_volinfo_ioctl(op, fd, &(oms->ovf));

out:
	return rc;
}

static int o2info_gen_mkfs_string(struct o2info_mkfs oms, char **mkfs)
{
	int rc = 0;
	char *compat = NULL;
	char *incompat = NULL;
	char *rocompat = NULL;
	char *features = NULL;
	char *ptr = NULL;
	char op_fs_features[PATH_MAX];
	char op_label[PATH_MAX];
	char buf[4096];

#define MKFS "-N %u "		\
	     "-J size=%llu "	\
	     "-b %u "		\
	     "-C %u "		\
	     "%s "		\
	     "%s "

	rc = o2info_get_compat_flag(oms.ovf.ofs.compat, &compat);
	if (rc)
		goto out;

	rc = o2info_get_incompat_flag(oms.ovf.ofs.incompat, &incompat);
	if (rc)
		goto out;

	rc = o2info_get_rocompat_flag(oms.ovf.ofs.rocompat, &rocompat);
	if (rc)
		goto out;

	features = malloc(strlen(compat) + strlen(incompat) +
			  strlen(rocompat) + 3);

	sprintf(features, "%s %s %s", compat, incompat, rocompat);

	ptr = features;

	while ((ptr = strchr(ptr, ' ')))
		*ptr = ',';

	if (strcmp("", features))
		snprintf(op_fs_features, PATH_MAX, "--fs-features %s",
			 features);
	else
		strcpy(op_fs_features, "");

	if (strcmp("", (char *)oms.ovf.label))
		snprintf(op_label, PATH_MAX, "-L %s", (char *)(oms.ovf.label));
	else
		strcpy(op_label, "");

	snprintf(buf, 4096, MKFS, oms.ovf.maxslots, oms.journal_size,
		 oms.ovf.blocksize, oms.ovf.clustersize, op_fs_features,
		 op_label);

	*mkfs = strdup(buf);
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

static int mkfs_run(struct o2info_operation *op, struct o2info_method *om,
		    void *arg)
{
	int rc = 0;
	static struct o2info_mkfs oms;
	char *mkfs = NULL;

	if (om->om_method == O2INFO_USE_IOCTL)
		rc = get_mkfs_ioctl(op, om->om_fd, &oms);
	else
		rc = o2info_get_mkfs(om->om_fs, &oms);
	if (rc)
		goto out;

	o2info_gen_mkfs_string(oms, &mkfs);

	fprintf(stdout, "%s\n", mkfs);
out:
	if (mkfs)
		ocfs2_free(&mkfs);

	return rc;
}

DEFINE_O2INFO_OP(mkfs,
		 mkfs_run,
		 NULL);
