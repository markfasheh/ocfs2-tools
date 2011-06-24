/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * o2cbutils.c
 *
 * utility functions
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

/*
 * Only after the local node is added to the cluster is the cluster
 * considered registered
 *
 * Returns 1 if registered, 0 otherwise.
 */
int is_cluster_registered(char *clustername)
{
	char **nodename = NULL;
	uint32_t local;
	int ret = 0, i = 0;

	if (o2cb_list_nodes(clustername, &nodename))
		goto bail;

	while(nodename && nodename[i] && *(nodename[i])) {
		if (o2cb_get_node_local(clustername, nodename[i], &local))
			break;
		if (local) {
			ret = 1;
			break;
		}
		++i;
	}

	if (nodename)
		o2cb_free_nodes_list(nodename);

bail:
	return ret;
}

/*
 * Returns 1 if atleast one heartbeat region is active.
 * 0 otherwise.
 */
int is_heartbeat_active(char *clustername)
{
	gchar **regions = NULL;
	errcode_t err;
	int active = 0;

	/* lookup active heartbeats */
	err = o2cb_list_hb_regions(clustername, &regions);
	if (err)
		goto bail;

	/* if found, heartbeat is active */
	if (regions[0] && *(regions[0]))
		active = 1;

	if (regions)
		o2cb_free_hb_regions_list(regions);

bail:
	return active;
}
