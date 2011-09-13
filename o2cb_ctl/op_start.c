/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * op_start.c
 *
 * Starts and stops the global heartbeat
 *
 * Copyright (C) 2010, 2011 Oracle.  All rights reserved.
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
#include "o2cb_scandisk.h"

extern const char *stackname;

static errcode_t stop_global_heartbeat(O2CBCluster *cluster, char *clustername,
				       int only_missing);

static void stop_heartbeat(struct o2cb_device *od)
{
	errcode_t ret;

	if (!(od->od_flags & O2CB_DEVICE_FOUND) ||
	    !(od->od_flags & O2CB_DEVICE_HB_STARTED))
		return;

	verbosef(VL_DEBUG, "Stopping heartbeat on region %s, device %s\n",
		 od->od_region.r_name, od->od_region.r_device_name);

	ret = o2cb_stop_heartbeat(&od->od_cluster, &od->od_region);
	if (ret)
		tcom_err(ret, "while stopping heartbeat on region "
			 "'%s'", od->od_uuid);
	else
		od->od_flags &= ~(O2CB_DEVICE_HB_STARTED);
}

static void free_region_descs(struct list_head *hbdevs, int stop_hb)
{
	struct list_head *pos, *pos1;
	struct o2cb_device *od;

	list_for_each_safe(pos, pos1, hbdevs) {
		od = list_entry(pos, struct o2cb_device, od_list);
		if (stop_hb)
			stop_heartbeat(od);
		list_del(pos);
		free(od->od_uuid);
		free(od);
	}
}

static errcode_t get_region_descs(O2CBCluster *cluster,
				  struct list_head *hbdevs)
{
	errcode_t ret = O2CB_ET_NO_MEMORY;
	O2CBHeartbeat *heartbeat;
	JIterator *iter;
	gchar *region;
	struct o2cb_device *od;

	iter = o2cb_cluster_get_heartbeat_regions(cluster);

	while (j_iterator_has_more(iter)) {
		heartbeat = (O2CBHeartbeat *)j_iterator_get_next(iter);

		region = o2cb_heartbeat_get_region(heartbeat);

		verbosef(VL_DEBUG, "Heartbeat region %s\n", region);
		od = calloc(1, sizeof(struct o2cb_device));
		if (od) {
			list_add_tail(&od->od_list, hbdevs);
			od->od_uuid = strdup(region);
		}

		g_free(region);

		if (!od || !od->od_uuid) {
			tcom_err(ret, "while reading regions");
			goto bail;
		}
	}

	verbosef(VL_DEBUG, "Scanning devices\n");
	o2cb_scandisk(hbdevs);

	ret = 0;

bail:
	if (iter)
		j_iterator_free(iter);

	return ret;
}

static errcode_t start_heartbeat(struct o2cb_device *od)
{
	errcode_t ret = O2CB_ET_UNKNOWN_REGION;

	if (!(od->od_flags & O2CB_DEVICE_FOUND)) {
		tcom_err(ret, "%s", od->od_uuid);
		goto bail;
	}

	verbosef(VL_DEBUG, "Starting heartbeat on region %s, device %s\n",
		 od->od_region.r_name, od->od_region.r_device_name);

	ret = o2cb_start_heartbeat(&od->od_cluster, &od->od_region);
	if (ret) {
		tcom_err(ret, "while starting heartbeat on region '%s'",
			 od->od_uuid);
		goto bail;
	}

	od->od_flags |= O2CB_DEVICE_HB_STARTED;

bail:
	return ret;
}

static errcode_t start_global_heartbeat(O2CBCluster *cluster, char *clustername)
{
	struct list_head hbdevs, *pos;
	struct o2cb_device *od;
	errcode_t ret;

	o2cbtool_block_signals(SIG_BLOCK);

	INIT_LIST_HEAD(&hbdevs);
	ret = get_region_descs(cluster, &hbdevs);
	if (ret)
		goto bail;

	verbosef(VL_DEBUG, "About to start heartbeat\n");
	list_for_each(pos, &hbdevs) {
		od = list_entry(pos, struct o2cb_device, od_list);
		ret = start_heartbeat(od);
		if (ret)
			goto bail;
	}

	verbosef(VL_DEBUG, "Stop heartbeat on devices removed from config\n");
	ret = stop_global_heartbeat(cluster, clustername, 1);
	if (ret)
		goto bail;

bail:
	o2cbtool_block_signals(SIG_UNBLOCK);
	free_region_descs(&hbdevs, !!ret);
	return ret;
}

/*
 * o2cb start-heartbeat <clustername>
 */
errcode_t o2cbtool_start_heartbeat(struct o2cb_command *cmd)
{
	O2CBCluster *cluster;
	int ret = -1;
	gchar *clustername;
	int global = 0;

	if (cmd->o_argc < 2)
		goto bail;

	cmd->o_print_usage = 0;

	clustername = cmd->o_argv[1];

	cluster = o2cb_config_get_cluster_by_name(cmd->o_config, clustername);
	if (!cluster) {
		errorf("Unknown cluster '%s'\n", clustername);
		goto bail;
	}

	ret = o2cbtool_init_cluster_stack();
	if (ret)
		goto bail;

	if (!is_cluster_registered(clustername)) {
		errorf("Cluster '%s' not registered\n", clustername);
		goto bail;
	}

	verbosef(VL_DEBUG, "Checking heartbeat mode\n");

	ret = o2cb_global_heartbeat_mode(clustername, &global);
	if (ret) {
		tcom_err(ret, "while starting heartbeat");
		goto bail;
	}

	if (!global)
		goto bail;
	verbosef(VL_DEBUG, "Global heartbeat enabled\n");

	ret = start_global_heartbeat(cluster, clustername);
	if (ret)
		goto bail;

	verbosef(VL_OUT, "Global heartbeat started\n");

bail:
	return ret;
}

static errcode_t _fake_default_cluster_desc(
					struct o2cb_cluster_desc *cluster_desc)
{
	errcode_t ret;
	char **clusters;

	memset(cluster_desc, 0, sizeof(struct o2cb_cluster_desc));

	ret = o2cb_list_clusters(&clusters);
	if (ret)
		return ret;

	cluster_desc->c_stack = strdup(stackname);
	cluster_desc->c_cluster = strdup(clusters[0]);

	if (!cluster_desc->c_stack || !cluster_desc->c_cluster) {
		ret = O2CB_ET_NO_MEMORY;
		free(cluster_desc->c_stack);
		free(cluster_desc->c_cluster);
	}

	o2cb_free_cluster_list(clusters);

	return ret;
}

/*
 * Only the elements needed by o2cb_stop_heartbeat() are initialized in
 * _fake_region_desc().
 */
static errcode_t _fake_region_desc(struct o2cb_region_desc *region_desc,
				   char *region_name)
{
	memset(region_desc, 0, sizeof(struct o2cb_region_desc));

	region_desc->r_name = strdup(region_name);
	if (!region_desc->r_name)
		return O2CB_ET_NO_MEMORY;
	region_desc->r_persist = 1;

	return 0;
}

static errcode_t stop_global_heartbeat(O2CBCluster *cluster, char *clustername,
				       int only_missing)
{
	struct o2cb_cluster_desc cluster_desc = { NULL, NULL };
	struct o2cb_region_desc region_desc = { NULL, };
	O2CBHeartbeat *hb;
	errcode_t ret;
	gchar **regions = NULL;
	int i;

	o2cbtool_block_signals(SIG_BLOCK);

	ret = _fake_default_cluster_desc(&cluster_desc);
	if (ret) {
		tcom_err(ret, "while looking up the active cluster");
		goto bail;
	}

	if (strcmp(cluster_desc.c_cluster, clustername)) {
		errorf("Cluster %s is not active\n", clustername);
		ret = -1;
		goto bail;
	}

	verbosef(VL_DEBUG, "Looking up active heartbeat regions\n");

	ret = o2cb_list_hb_regions(clustername, &regions);
	if (ret) {
		tcom_err(ret, "while looking up the active heartbeat regions");
		goto bail;
	}

	/* error, if found */
	for (i = 0; regions[i]; ++i) {
		if (!regions[i] || !(*(regions[i])))
			continue;

		if (only_missing) {
			hb = o2cb_cluster_get_heartbeat_by_region(cluster,
								  regions[i]);
			if (hb)
				continue;
			verbosef(VL_DEBUG, "Registered heartbeat region '%s' "
				 "not found in config\n", regions[i]);
		}

		ret = _fake_region_desc(&region_desc, regions[i]);
		if (ret) {
			tcom_err(ret, "while filling region description");
			goto bail;
		}

		verbosef(VL_DEBUG, "Stopping heartbeat on region %s\n",
			 regions[i]);

		ret = o2cb_stop_heartbeat(&cluster_desc, &region_desc);
		if (ret) {
			tcom_err(ret, "while stopping heartbeat on region "
				 "'%s'", regions[i]);
			goto bail;
		}
		free(region_desc.r_name);
		region_desc.r_name = NULL;
	}

	ret = 0;

bail:
	o2cbtool_block_signals(SIG_UNBLOCK);
	if (regions)
		o2cb_free_hb_regions_list(regions);
	free(cluster_desc.c_stack);
	free(cluster_desc.c_cluster);
	free(region_desc.r_name);

	return ret;
}

/*
 * o2cb stop-heartbeat <clustername>
 */
errcode_t o2cbtool_stop_heartbeat(struct o2cb_command *cmd)
{
	O2CBCluster *cluster;
	int ret = -1;
	gchar *clustername;
	int global = 0;

	if (cmd->o_argc < 2)
		goto bail;

	cmd->o_print_usage = 0;

	clustername = cmd->o_argv[1];

	cluster = o2cb_config_get_cluster_by_name(cmd->o_config, clustername);
	if (!cluster) {
		errorf("Unknown cluster '%s'\n", clustername);
		goto bail;
	}

	ret = o2cbtool_init_cluster_stack();
	if (ret)
		goto bail;

	if (!is_cluster_registered(clustername)) {
		errorf("Cluster '%s' not registered\n", clustername);
		goto bail;
	}

	verbosef(VL_DEBUG, "Checking heartbeat mode\n");

	ret = o2cb_global_heartbeat_mode(clustername, &global);
	if (ret) {
		tcom_err(ret, "while stopping heartbeat");
		goto bail;
	}

	if (!global) {
		verbosef(VL_DEBUG, "Global heartbeat not enabled\n");
		goto bail;
	}

	verbosef(VL_DEBUG, "Global heartbeat enabled\n");

	ret = stop_global_heartbeat(cluster, clustername, 0);
	if (ret)
		goto bail;

	verbosef(VL_OUT, "Global heartbeat stopped\n");
bail:
	return ret;
}
