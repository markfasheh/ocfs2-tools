/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * check.c
 *
 * OCFS2 format check utility
 *
 * Copyright (C) 2005 Oracle.  All rights reserved.
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
 *
 */

#include "mkfs.h"

#include <ocfs2.h>
#include <ocfs2_fs.h>
#include <ocfs1_fs_compat.h>
#include <kernel-list.h>

int ocfs2_check_volume(State *s)
{
	ocfs2_filesys *fs = NULL;
	errcode_t ret;

	initialize_ocfs_error_table();
	initialize_o2dl_error_table();

	ret = ocfs2_open(s->device_name, OCFS2_FLAG_RW, 0, 0, &fs);
	if (ret) {
		if (ret == OCFS2_ET_OCFS_REV)
			fprintf(stdout, "Overwriting existing ocfs partition.\n");
		return 0;
	} else
		fprintf(stdout, "Overwriting existing ocfs2 partition.\n");

	if (!s->force) {
		ret = ocfs2_initialize_dlm(fs);
		if (ret) {
			ocfs2_close(fs);
			com_err(s->progname, ret, "while initializing the dlm");
			return -1;
		}

		ret = ocfs2_lock_down_cluster(fs);
		if (ret) {
			ocfs2_shutdown_dlm(fs);
			ocfs2_close(fs);
			com_err(s->progname, ret, "while locking the cluster");
			return -1;
		}

		ocfs2_release_cluster(fs);
		ocfs2_shutdown_dlm(fs);
	} else {
		fprintf(stdout, "Cluster check has been disabled by --force.\n"
			"Please ensure that the volume is not mounted on any other node\n"
			"else re-run %s without --force.\n", s->progname);
	}

	ocfs2_close(fs);

	return 0;
}
