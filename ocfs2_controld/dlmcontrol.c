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

#include "ocfs2-kernel/kernel-list.h"

#include "ocfs2_controld.h"


/*
 * Structure to keep track of filesystems we've registered with
 * dlm_controld.
 */
struct dlmcontrol_fs {
	struct list_head	df_list;
	char			df_name[DLM_LOCKSPACE_LEN+1];
	struct list_head	df_notifications;
	void			(*df_result)(int status, void *user_data);
	void			*df_user_data;
};

/* Keeps track of pending node-down notifications */
struct dlmcontrol_notification {
	struct list_head	dn_list;
	int			dn_nodeid;
};

static int dlmcontrol_ci;	/* Client number in the main loop */
static int dlmcontrol_fd = -1;	/* fd for dlm_controld connection */

/* List of all filesystems we have registered */
static LIST_HEAD(register_list);

int dlmcontrol_register(const char *name,
			void (*result_func)(int status, void *user_data),
			void *user_data)
{
	int rc;
	struct dlmcontrol_fs *df;

	df = malloc(sizeof(struct dlmcontrol_fs));
	if (!df) {
		log_error("Unable to allocate memory to register \"%s\" "
			  "with dlm_controld",
			  name);
		rc = -ENOMEM;
		goto out;
	}

	snprintf(df->df_name, DLM_LOCKSPACE_LEN+1, "%s", name);
	INIT_LIST_HEAD(&df->df_notifications);
	df->df_result = result_func;
	df->df_user_data = user_data;

	/*
	 * We register *before* allowing the filesystem to mount.
	 * Otherwise we have a window between mount(2) and registration
	 * where notification is unordered.
	 */
	log_debug("Registering \"%s\" with dlm_controld", name);
	rc = dlmc_fs_register(dlmcontrol_fd, df->df_name);
	if (rc) {
		rc = -errno;
		log_error("Unable to register \"%s\" with dlm_controld: %s",
			  df->df_name, strerror(-rc));
		goto out;
	}

	list_add(&df->df_list, &register_list);

out:
	if (rc && df)
		free(df);

	return rc;
}

static struct dlmcontrol_fs *find_fs(const char *name)
{
	struct list_head *pos;
	struct dlmcontrol_fs *df;

	list_for_each(pos, &register_list) {
		df = list_entry(pos, struct dlmcontrol_fs, df_list);
		if (!strcmp(df->df_name, name))
			return df;
	}

	return NULL;
}

static struct dlmcontrol_notification *find_notification(struct dlmcontrol_fs *df,
							 int nodeid)
{
	struct list_head *pos;
	struct dlmcontrol_notification *dn;

	list_for_each(pos, &df->df_notifications) {
		dn = list_entry(pos, struct dlmcontrol_notification,
				dn_list);
		if (dn->dn_nodeid == nodeid)
			return dn;
	}

	return NULL;
}

static void complete_notification(struct dlmcontrol_fs *df, int nodeid)
{
	struct dlmcontrol_notification *dn;

	dn = find_notification(df, nodeid);
	if (dn) {
		log_debug("Completing notification on \"%s\" for node %d\n",
			  df->df_name, nodeid);
		list_del(&dn->dn_list);
		free(dn);
	}
}

static void complete_all_notifications(struct dlmcontrol_fs *df)
{
	struct list_head *pos, *n;
	struct dlmcontrol_notification *dn;

	list_for_each_safe(pos, n, &df->df_notifications) {
		dn = list_entry(pos, struct dlmcontrol_notification,
				dn_list);
		complete_notification(df, dn->dn_nodeid);
	}
}

int dlmcontrol_unregister(const char *name)
{
	int rc;
	struct dlmcontrol_fs *df;

	df = find_fs(name);
	if (!df) {
		log_error("Name \"%s\" is unknown", name);
		rc = -ENOENT;
		goto out;
	}

	list_del(&df->df_list);

	/*
	 * From here out, we're going to try to drop everything, even in
	 * the face of errors.
	 */
	log_debug("Unregistering \"%s\" from dlm_controld", name);

	complete_all_notifications(df);
	rc = dlmc_fs_unregister(dlmcontrol_fd, df->df_name);
	if (rc) {
		rc = -errno;
		log_error("Unable to unregister \"%s\" from dlm_controld: "
			  "%s",
			  name, strerror(-rc));
	}

	free(df);

out:
	return rc;
}

static void dlmcontrol_unregister_all(void)
{
	struct list_head *pos, *n;
	struct dlmcontrol_fs *df;

	list_for_each_safe(pos, n, &register_list) {
		df = list_entry(pos, struct dlmcontrol_fs, df_list);
		/* This is exit-time, don't care about errors */
		dlmcontrol_unregister(df->df_name);
	}
}

/*
 * This is a fire and forget function.  It will queue the notification and
 * send it to dlm_controld.  If dlm_controld responds in the negative, this
 * function is called again to send.  This goes on until dlm_controld sees
 * the node go down.
 *
 * We don't sleep because the process switching should be good enough.
 *
 * If there is any error, malloc/network/whatever, we kill the daemon.  We
 * can't continue safely if we're not interacting with dlm_controld.
 */
void dlmcontrol_node_down(const char *name, int nodeid)
{
	int rc;
	struct dlmcontrol_fs *df;
	struct dlmcontrol_notification *dn;

	df = find_fs(name);
	if (!df) {
		log_error("Name \"%s\" is unknown", name);
		return;
	}

	dn = find_notification(df, nodeid);
	if (!dn) {
		dn = malloc(sizeof(struct dlmcontrol_notification));
		if (!dn) {
			log_error("Unable to allocate memory");
			goto fail;
		}
		dn->dn_nodeid = nodeid;
		list_add(&dn->dn_list, &df->df_notifications);
	}

	log_debug("Sending notification of node %d for \"%s\"",
		  dn->dn_nodeid, df->df_name);
	rc = dlmc_fs_notified(dlmcontrol_fd, df->df_name, dn->dn_nodeid);
	if (rc) {
		log_error("Unable to send notification for node %d on "
			  "\"%s\": %s",
			  dn->dn_nodeid, df->df_name, strerror(errno));
		goto fail;
	}

	return;

fail:
	shutdown_daemon();
}

static void notify_result(struct dlmcontrol_fs *df, int nodeid, int status)
{
	if (!find_notification(df, nodeid)) {
		log_error("Notified for nodeid %d on \"%s\", but we never asked for it!",
			  nodeid, df->df_name);
		return;
	}

	if (!status) {
		complete_notification(df, nodeid);
		return;
	}

	dlmcontrol_node_down(df->df_name, nodeid);
}

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
	int rc, result_type, nodeid, status;
	char name[DLM_LOCKSPACE_LEN + 1];
	struct dlmcontrol_fs *df;

	memset(name, 0, sizeof(name));

	if (ci != dlmcontrol_ci) {
		log_error("Unknown connection %d", ci);
		return;
	}

	log_debug("message from dlmcontrol\n");

	rc = dlmc_fs_result(dlmcontrol_fd, name, &result_type, &nodeid,
			    &status);
	if (rc) {
		rc = -errno;
		log_error("Error from dlmc_fs_result: %s", strerror(-rc));
		return;
	}

	df = find_fs(name);
	if (!df) {
		log_error("Name \"%s\" is unknown", name);
		return;
	}

	switch (result_type) {
		case DLMC_RESULT_REGISTER:
			log_debug("Registration of \"%s\" complete",
				  name);
			df->df_result(status, df->df_user_data);
			break;

		case DLMC_RESULT_NOTIFIED:
			log_debug("Notified for \"%s\", node %d, status %d",
				  name, nodeid, status);
			notify_result(df, nodeid, status);
			break;

		default:
			log_error("Unknown message from dlm_controld: %d",
				  result_type);
			break;
	}
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

	dlmcontrol_unregister_all();
	log_debug("Closing dlm_controld connection");
	dlmc_fs_disconnect(dlmcontrol_fd);
}

