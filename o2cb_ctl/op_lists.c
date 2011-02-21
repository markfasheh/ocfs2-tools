/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * op_lists.c
 *
 * Lists various entities in the o2cb cluster config
 *
 * Copyright (C) 2010 Oracle.  All rights reserved.
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

static int list_parse_options(int argc, char *argv[], int *oneline,
			      char **clustername)
{
	int c, ret = -1, show_usage = 0;
	static struct option long_options[] = {
		{ "oneline", 0, 0, ONELINE_OPTION },
		{ 0, 0, 0, 0 },
	};

	while (1) {
		c = getopt_long(argc, argv, "", long_options, NULL);
		if (c == -1)
			break;
		switch (c) {
		case ONELINE_OPTION:
			*oneline = 1;
			break;
		default:
			++show_usage;
			break;
		}
	}

	if (optind + 1 > argc || show_usage)
		goto bail;

	*clustername = argv[optind];

	ret = 0;
bail:
	return ret;
}

static void show_heartbeats(O2CBCluster *cluster, gchar *clustername,
			    int oneline)
{
	O2CBHeartbeat *hb;
	JIterator *iter;
	gchar *region;
	gchar *format;

	if (oneline) {
		format = "heartbeat: %s %s\n";
	} else
		format = "heartbeat:\n\tregion = %s\n\tcluster = %s\n\n";

	iter = o2cb_cluster_get_heartbeat_regions(cluster);
	while (j_iterator_has_more(iter)) {
		hb = j_iterator_get_next(iter);
		region = o2cb_heartbeat_get_region(hb);
		verbosef(VL_OUT, format, region, clustername);
		g_free(region);
	}
	j_iterator_free(iter);
}

static void show_nodes(O2CBCluster *cluster, gchar *clustername,
		       int oneline)
{
	O2CBNode *node;
	JIterator *iter;
	gchar *nodename, *ip;
	gint port, nodenum;
	gchar *format;

	if (oneline)
		format = "node: %d %s %s:%d %s\n";
	else
		format = "node:\n\tnumber = %d\n\tname = %s\n\t"
			"ip_address = %s\n\tip_port = %d\n\tcluster = %s\n\n";

	iter = o2cb_cluster_get_nodes(cluster);
	while (j_iterator_has_more(iter)) {
		node = j_iterator_get_next(iter);
		nodename = o2cb_node_get_name(node);
		ip = o2cb_node_get_ip_string(node);
		nodenum = o2cb_node_get_number(node);
		port = o2cb_node_get_port(node);
		verbosef(VL_OUT, format, nodenum, nodename, ip, port,
			 clustername);
		g_free(nodename);
		g_free(ip);
	}
	j_iterator_free(iter);
}

static void show_cluster(O2CBCluster *cluster, gchar *clustername,
			 int oneline)
{
	guint nodecount;
	gchar *hbmode;
	gchar *format;

	if (oneline)
		format = "cluster: %d %s %s\n";
	else
		format = "cluster:\n\tnode_count = %d\n\theartbeat_mode = %s\n"
			"\tname = %s\n\n";

	nodecount = o2cb_cluster_get_node_count(cluster);
	hbmode = o2cb_cluster_get_heartbeat_mode(cluster);
	if (!hbmode)
		hbmode = strdup(O2CB_LOCAL_HEARTBEAT_TAG);
	verbosef(VL_OUT, format, nodecount, hbmode, clustername);
	g_free(hbmode);
}

/*
 * list-cluster [--oneline] <clustername>
 * list-nodes [--oneline] <clustername>
 * list-heartbeats [--oneline] <clustername>
 */
errcode_t o2cbtool_list_objects(struct o2cb_command *cmd)
{
	O2CBCluster *cluster;
	gchar *clustername;
	int ret = -1;
	int oneline = 0;

	ret = list_parse_options(cmd->o_argc, cmd->o_argv, &oneline,
				 &clustername);
	if (ret)
		goto bail;

	cmd->o_print_usage = 0;

	cluster = o2cb_config_get_cluster_by_name(cmd->o_config, clustername);
	if (!cluster) {
		errorf("Unknown cluster '%s'\n", clustername);
		goto bail;
	}

	if (!strcmp(cmd->o_argv[0], "list-heartbeats"))
		show_heartbeats(cluster, clustername, oneline);
	else if (!strcmp(cmd->o_argv[0], "list-nodes"))
		show_nodes(cluster, clustername, oneline);
	else {
		show_heartbeats(cluster, clustername, oneline);
		show_nodes(cluster, clustername, oneline);
		show_cluster(cluster, clustername, oneline);
	}

	ret = 0;

bail:
	return ret;
}

/*
 * list-clusters
 */
errcode_t o2cbtool_list_clusters(struct o2cb_command *cmd)
{
	O2CBCluster *cluster;
	gchar *clustername;
	JIterator *iter;

	iter = o2cb_config_get_clusters(cmd->o_config);
	if (!iter)
		return -1;

	while (j_iterator_has_more(iter)) {
		cluster = j_iterator_get_next(iter);
		clustername = o2cb_cluster_get_name(cluster);
		verbosef(VL_OUT, "%s\n", clustername);
		g_free(clustername);
	}
	j_iterator_free(iter);

	return 0;
}
