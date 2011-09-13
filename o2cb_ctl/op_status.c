/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * op_status.c
 *
 * Returns status of the cluster
 *
 * Copyright (C) 2011 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License, version 2,  as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include "o2cbtool.h"

extern const char *stackname;

static errcode_t get_active_clustername(char *name, int namelen)
{
	gchar **clusternames = NULL;
	errcode_t ret;

	/* Lookup the registered cluster */
	ret = o2cb_list_clusters(&clusternames);
	if (ret) {
		tcom_err(ret, "while looking up the registered cluster");
		goto bail;
	}

	if (!*clusternames) {
		ret = O2CB_ET_SERVICE_UNAVAILABLE;
		goto bail;
	}

	strncpy(name, clusternames[0], namelen);
	name[namelen] = '\0';

bail:
	if (clusternames)
		o2cb_free_cluster_list(clusternames);
	return ret;
}

/*
 * cluster-status [<clustername>]
 *
 * Return 0 if online and 1 otherwise.
 */
errcode_t o2cbtool_cluster_status(struct o2cb_command *cmd)
{
	errcode_t err;
	char clustername[OCFS2_CLUSTER_NAME_LEN + 1];
	int global = 0, status = 1;

	*clustername = '\0';
	cmd->o_print_usage = 0;

	err = o2cbtool_init_cluster_stack();
	if (err)
		goto bail;

	/* Get active clustername */
	err = get_active_clustername(clustername, OCFS2_CLUSTER_NAME_LEN);
	if (err)
		goto bail;

	/* If none, found exit */
	if (strlen(clustername) == 0)
		goto bail;
	verbosef(VL_DEBUG, "Active cluster '%s'\n", clustername);

	/* If found but name != arg, then exit */
	if (cmd->o_argc > 1 &&
	    strncmp(clustername, cmd->o_argv[1], OCFS2_CLUSTER_NAME_LEN)) {
		strncpy(clustername, cmd->o_argv[1], OCFS2_CLUSTER_NAME_LEN);
		clustername[OCFS2_CLUSTER_NAME_LEN] = '\0';
		goto bail;
	}

	/* Check if cluster is registered */
	if (!is_cluster_registered(clustername))
		goto bail;
	verbosef(VL_DEBUG, "Cluster '%s' is registered\n", clustername);

	/* Get heartbeat mode */
	err = o2cb_global_heartbeat_mode(clustername, &global);
	if (err)
		goto bail;

	/* If local heartbeat, then cluster is online */
	if (!global) {
		status = 0;
		goto bail;
	}
	verbosef(VL_DEBUG, "Global heartbeat is enabled\n");

	/*
	 * In global heartbeat mode, atleast one region should be active
	 * for the cluster to be called online
	 */ 
	if (!is_heartbeat_active(clustername))
		goto bail;

	status = 0;
bail:
	if (strlen(clustername))
		verbosef(VL_OUT, "Cluster '%s' is %s\n", clustername,
			 (!status ? "online" : "offline"));
	else
		verbosef(VL_OUT, "%s\n", (!status ? "online" : "offline"));

	return status;
}
