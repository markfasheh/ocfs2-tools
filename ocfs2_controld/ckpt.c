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
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <inttypes.h>
#include <errno.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <openais/saAis.h>
#include <openais/saCkpt.h>

#include "ocfs2_controld.h"


/* Ought to be enough for most things */
#define RETRY_TRIES	5


static SaCkptHandleT handle;

/* This is the version OpenAIS supports */
static SaVersionT version = { 'B', 1, 1 };

static SaCkptCallbacksT callbacks = {
	NULL,
	NULL,
};

static void ais_err_to_errno(SaAisErrorT error, int *rc, char **reason)
{
	switch (error) {
		case SA_AIS_OK:
			*rc = 0;
			*reason = "Success";
			break;
		case SA_AIS_ERR_LIBRARY:
			*rc = -ENXIO;
			*reason = "Internal library error";
			break;
		case SA_AIS_ERR_TIMEOUT:
			*rc = -ETIMEDOUT;
			*reason = "Timed out";
			break;
		case SA_AIS_ERR_TRY_AGAIN:
			*rc = -EAGAIN;
			*reason = "Try again";
			break;
		case SA_AIS_ERR_INVALID_PARAM:
			*rc = -EINVAL;
			*reason = "Invalid parameter";
			break;
		case SA_AIS_ERR_NO_MEMORY:
			*rc = -ENOMEM;
			*reason = "Out of memory";
			break;
		case SA_AIS_ERR_NO_RESOURCES:
			*rc = -EBUSY;
			*reason = "Insufficient resources";
			break;
		case SA_AIS_ERR_VERSION:
			*rc = -EPROTOTYPE;
			*reason = "Protocol not compatible";
			break;
		default:
			*rc = -ENOSYS;
			*reason = "Unknown error";
			log_error("Unknown error seen!");
			break;
	}
}

int setup_ckpt(void)
{
	int rc, retrycount;
	char *reason;
	SaAisErrorT error;

	for (retrycount = 0; retrycount < RETRY_TRIES; retrycount++) {
		log_debug("Initializing CKPT service (try %d)",
			  retrycount + 1);
		error = saCkptInitialize(&handle, &callbacks, &version);
		ais_err_to_errno(error, &rc, &reason);
		if (!rc) {
			log_debug("Connected to CKPT service with handle 0x%llx",
				  handle);
			break;
		}
		if (rc != -EAGAIN) {
			log_error("Unable to connect to CKPT: %s", reason);
			break;
		}
		if ((retrycount + 1) < RETRY_TRIES)
			sleep(1);
		else
			log_error("Unable to connect to CKPT: too many tries");
	}

	return rc;
}

void exit_ckpt(void)
{
	int rc, retrycount;
	char *reason;
	SaAisErrorT error;

	if (!handle)
		return;

	for (retrycount = 0; retrycount < RETRY_TRIES; retrycount++) {
		log_debug("Disconnecting from CKPT service (try %d)",
			  retrycount + 1);
		error = saCkptFinalize(handle);
		ais_err_to_errno(error, &rc, &reason);
		if (!rc) {
			log_debug("Disconnected from CKPT service");
			break;
		}
		if (rc != -EAGAIN) {
			log_error("Unable to disconnect from CKPT: %s",
				  reason);
			break;
		}
		if ((retrycount + 1) < RETRY_TRIES)
			sleep(1);
		else
			log_error("Unable to disconnect from CKPT: too many tries");
	}
}

int dump_point, dump_wrap, daemon_debug_opt = 1;
char daemon_debug_buf[1024];
char dump_buf[DUMP_SIZE];
void daemon_dump_save(void)
{
	int len, i;

	len = strlen(daemon_debug_buf);

	for (i = 0; i < len; i++) {
		dump_buf[dump_point++] = daemon_debug_buf[i];

		if (dump_point == DUMP_SIZE) {
			dump_point = 0;
			dump_wrap = 1;
		}
	}
}

int main(int argc, char *argv[])
{
	int rc;

	rc = setup_ckpt();
	if (rc)
		goto out;

	exit_ckpt();

out:
	return rc;
}
