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


/*
 * A tentative retry is something we don't want to spend a lot of time on;
 * it works or we error.  A serious retry we really want to complete.
 */
#define TENTATIVE_RETRY_TRIES	2
#define SERIOUS_RETRY_TRIES	5


struct ckpt_handle {
	SaNameT			ch_name;
	SaCkptCheckpointHandleT	ch_handle;
};

static SaCkptHandleT daemon_handle;
struct ckpt_handle *global_handle;

/* This is the version OpenAIS supports */
static SaVersionT version = { 'B', 1, 1 };

static SaCkptCallbacksT callbacks = {
	NULL,
	NULL,
};

/*
 * All of our checkpoints store 4K of data in 32 sections of 128bytes.  We
 * probably won't actually use more than one section of each checkpoint,
 * but we spec them larger so that we can use space later compatibly.  Note
 * that data space is only allocated when needed, so if we store one section
 * of 10 bytes, the checkpoint uses 10 bytes, not 4K.
 *
 * Retention time is 0 - when a daemon exits, it should disappear.
 *
 * Max section ID size is basically big enough to hold a uuid (32
 * characters) plus something extra.  We don't use uuids in section names
 * yet, but just in case.
 */
static SaCkptCheckpointCreationAttributesT ckpt_attributes = {
	.creationFlags		= SA_CKPT_WR_ALL_REPLICAS,
	.checkpointSize		= 4096,
	.retentionDuration	= 0LL,
	.maxSections		= 32,
	.maxSectionSize		= 128,
	.maxSectionIdSize	= 40,
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
		case SA_AIS_ERR_BAD_HANDLE:
			*rc = -EINVAL;
			*reason = "Bad Ckpt handle";
			break;
		case SA_AIS_ERR_INIT:
			*rc = -ENODEV;
			*reason = "Initialization not complete";
			break;
		case SA_AIS_ERR_NOT_EXIST:
			*rc = -ENOENT;
			*reason = "Object does not exist";
			break;
		case SA_AIS_ERR_EXIST:
			*rc = -EEXIST;
			*reason = "Object already exists";
			break;
		case SA_AIS_ERR_BAD_FLAGS:
			*rc = -EINVAL;
			*reason = "Invalid flags";
			break;
		case SA_AIS_ERR_ACCESS:
			*rc = -EACCES;
			*reason = "Permission denied";
			break;
		default:
			*rc = -ENOSYS;
			*reason = "Unknown error";
			log_error("Unknown error seen! (%d)", error);
			break;
	}
}

/*
 * Our retention-time scheme of 0 means that we need to create any
 * checkpoint we want to update.  Nobody is writing to the same checkpoint
 * at the same time.
 */
static int call_ckpt_open(struct ckpt_handle *handle, int write)
{
	int rc, retrycount;
	char *reason;
	SaAisErrorT error;
	int flags = SA_CKPT_CHECKPOINT_READ;

	if (write)
		flags |= (SA_CKPT_CHECKPOINT_WRITE |
			  SA_CKPT_CHECKPOINT_CREATE);

	for (retrycount = 0; retrycount < TENTATIVE_RETRY_TRIES; retrycount++) {
		log_debug("Opening checkpoint \"%.*s\" (try %d)",
			  handle->ch_name.length, handle->ch_name.value,
			  retrycount + 1);
		error = saCkptCheckpointOpen(daemon_handle,
					     &handle->ch_name,
					     write ? &ckpt_attributes : NULL,
					     flags, 0, &handle->ch_handle);
		ais_err_to_errno(error, &rc, &reason);
		if (!rc) {
			log_debug("Opened checkpoint \"%.*s\" with handle 0x%llx",
				  handle->ch_name.length,
				  handle->ch_name.value,
				  handle->ch_handle);
			break;
		}
		if ((rc != -EAGAIN) &&
		    (!write || (rc != -EEXIST))){
			log_error("Unable to open checkpoint \"%.*s\": %s",
				  handle->ch_name.length,
				  handle->ch_name.value,
				  reason);
			break;
		}
		if (write && (rc == -EEXIST))
			log_debug("Checkpoint \"%.*s\" exists, retrying after delay",
				  handle->ch_name.length,
				  handle->ch_name.value);

		if ((retrycount + 1) < TENTATIVE_RETRY_TRIES)
			sleep(1);
		else
			log_error("Unable to open checkpoint \"%.*s\": "
				  "too many tries",
				  handle->ch_name.length,
				  handle->ch_name.value);
	}

	return rc;
}

static void call_ckpt_close(struct ckpt_handle *handle)
{
	int rc, retrycount;
	char *reason;
	SaAisErrorT error;

	for (retrycount = 0; retrycount < TENTATIVE_RETRY_TRIES; retrycount++) {
		log_debug("Closing checkpoint \"%.*s\" (try %d)",
			  handle->ch_name.length, handle->ch_name.value,
			  retrycount + 1);
		error = saCkptCheckpointClose(handle->ch_handle);
		ais_err_to_errno(error, &rc, &reason);
		if (!rc) {
			log_debug("Closed checkpoint \"%.*s\"",
				  handle->ch_name.length,
				  handle->ch_name.value);
			break;
		}
		if (rc != -EAGAIN) {
			log_error("Unable to close checkpoint \"%.*s\": %s",
				  handle->ch_name.length,
				  handle->ch_name.value,
				  reason);
			break;
		}
		if ((retrycount + 1) < TENTATIVE_RETRY_TRIES)
			sleep(1);
		else
			log_error("Unable to close checkpoint \"%.*s\": "
				  "too many tries",
				  handle->ch_name.length,
				  handle->ch_name.value);
	}
}

/*
 * We name our ckeckpoints in one of three ways, all prefixed with 'ocfs2:'.
 *
 * The global checkpoint is named 'ocfs2:controld'.
 * The node info checkpoint is named 'ocfs2:controld:<8-hex-char-nodeid>'
 * A mount checkpoint is named 'ocfs2:<uuid>:<8-hex-char-nodeid>'
 */
#define CKPT_PREFIX "ocfs2:"
static int ckpt_new(const char *name, int write, struct ckpt_handle **handle)
{
	int rc;
	size_t namelen = strlen(name) + strlen(CKPT_PREFIX);
	struct ckpt_handle *h;

	if (namelen > SA_MAX_NAME_LENGTH) {
		log_error("Checkpoint name \"%s\" too long", name);
		return -EINVAL;
	}

	h = malloc(sizeof(struct ckpt_handle));
	if (!h) {
		log_error("Unable to allocate checkpoint handle");
		return -ENOMEM;
	}

	memset(h, 0, sizeof(struct ckpt_handle));
	h->ch_name.length = snprintf((char *)(h->ch_name.value),
				     SA_MAX_NAME_LENGTH, "%s%s",
				     CKPT_PREFIX, name);

	rc = call_ckpt_open(h, write);
	if (!rc)
		*handle = h;
	else
		free(h);

	return rc;
}

static void ckpt_free(struct ckpt_handle *handle)
{
	if (handle->ch_handle)
		call_ckpt_close(handle);

	free(handle);
}

static int ckpt_open_global(int write)
{
	if (global_handle)
		return 0;

	return ckpt_new("controld", write, &global_handle);
}

static void ckpt_close_global(void)
{
	if (global_handle) {
		ckpt_free(global_handle);
		global_handle = NULL;
	}
}

int ckpt_open_node(int nodeid, struct ckpt_handle **handle)
{
	char name[SA_MAX_NAME_LENGTH];

	snprintf(name, SA_MAX_NAME_LENGTH, "controld:%08x", nodeid);

	return ckpt_new(name, 0, handle);
}

int ckpt_open_this_node(struct ckpt_handle **handle)
{
	char name[SA_MAX_NAME_LENGTH];

	snprintf(name, SA_MAX_NAME_LENGTH, "controld:%08x", our_nodeid);

	return ckpt_new(name, 1, handle);
}

void ckpt_close(struct ckpt_handle *handle)
{
	ckpt_free(handle);
}

int setup_ckpt(void)
{
	int rc, retrycount;
	char *reason;
	SaAisErrorT error;

	for (retrycount = 0; retrycount < SERIOUS_RETRY_TRIES; retrycount++) {
		log_debug("Initializing CKPT service (try %d)",
			  retrycount + 1);
		error = saCkptInitialize(&daemon_handle, &callbacks,
					 &version);
		ais_err_to_errno(error, &rc, &reason);
		if (!rc) {
			log_debug("Connected to CKPT service with handle 0x%llx",
				  daemon_handle);
			break;
		}
		if (rc != -EAGAIN) {
			log_error("Unable to connect to CKPT: %s", reason);
			break;
		}
		if ((retrycount + 1) < SERIOUS_RETRY_TRIES)
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

	if (!daemon_handle)
		return;

	for (retrycount = 0; retrycount < SERIOUS_RETRY_TRIES; retrycount++) {
		log_debug("Disconnecting from CKPT service (try %d)",
			  retrycount + 1);
		error = saCkptFinalize(daemon_handle);
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
		if ((retrycount + 1) < SERIOUS_RETRY_TRIES)
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

int our_nodeid = 2;
int main(int argc, char *argv[])
{
	int rc;
	struct ckpt_handle *h;

	rc = setup_ckpt();
	if (rc)
		goto out;

	rc = ckpt_open_global(1);
	if (rc)
		goto out_exit;
	ckpt_close_global();

	rc = ckpt_open_this_node(&h);
	if (rc)
		goto out_exit;
	ckpt_close(h);

	rc = ckpt_open_node(4, &h);
	if (rc)
		goto out_exit;
	ckpt_close(h);

out_exit:
	exit_ckpt();

out:
	return rc;
}
