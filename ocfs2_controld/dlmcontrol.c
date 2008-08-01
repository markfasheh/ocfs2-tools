/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * Copyright (C) 2008 Oracle.  All rights reserved.
 *
 *  This copyrighted material is made available to anyone wishing to use,
 *  modify, copy, or redistribute it subject to the terms and conditions
 *  of the GNU General Public License v.2.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <inttypes.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <syslog.h>
#include <libdlm.h>
#include <libdlmcontrol.h>

#include "ocfs2_controld.h"

static int dlmcontrol_ci;
static int dlmcontrol_fd = -1;

static void dead_dlmcontrol(int ci)
{
	if (ci != dlmcontrol_ci) {
		log_error("Unknown connection %d", ci);
		return;
	}

	log_error("dlmcontrol connection died");
	shutdown_daemon();
	connection_dead(ci);
}

static void process_dlmcontrol(int ci)
{
	if (ci != dlmcontrol_ci) {
		log_error("Unknown connection %d", ci);
		return;
	}

	log_debug("message from dlmcontrol\n");
}

int setup_dlmcontrol(void)
{
	int rc;

	rc = dlmc_fs_connect();
	if (rc < 0) {
		rc = -errno;
		log_error("Unable to connect to dlm_controld: %s",
			  strerror(-rc));
		goto out;
	}
	dlmcontrol_fd = rc;

	dlmcontrol_ci = connection_add(dlmcontrol_fd, process_dlmcontrol,
				       dead_dlmcontrol);

	if (dlmcontrol_ci < 0) {
		rc = dlmcontrol_ci;
		log_error("Unable to add dlmcontrol client: %s",
			  strerror(-dlmcontrol_ci));
		dlmc_fs_disconnect(dlmcontrol_fd);
		goto out;
	}

	rc = 0;

out:
	return rc;
}

void exit_dlmcontrol(void)
{
	if (dlmcontrol_fd < 0)
		return;

	log_debug("Closing dlm_controld connection");
	dlmc_fs_disconnect(dlmcontrol_fd);
}

