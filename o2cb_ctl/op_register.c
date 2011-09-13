/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * op_register.c
 *
 * Registers and unregisters the configured cluster with configfs
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

extern const char *stackname;

static errcode_t node_is_local(gchar *nodename, gboolean *is_local)
{
	char hostname[PATH_MAX];
	size_t host_len, node_len = strlen(nodename);
	gboolean local = 0;
	errcode_t ret = 0;

	ret = gethostname(hostname, sizeof(hostname));
	if (ret) {
		errorf("Unable to determine hostname, %s\n", strerror(errno));
		ret = O2CB_ET_HOSTNAME_UNKNOWN;
		goto bail;
	}

	host_len = strlen(hostname);
	if (host_len < node_len)
		goto bail;

	/*
	 * nodes are only considered local if they match the hostname.  we want
	 * to be sure to catch the node name being "localhost" and the hostname
	 * being "localhost.localdomain".  we consider them equal if the
	 * configured node name matches the start of the hostname up to a '.'
	 */
	if (!strncasecmp(nodename, hostname, node_len) &&
	    (hostname[node_len] == '\0' || hostname[node_len] == '.'))
		local = 1;

bail:
	*is_local = local;

	return ret;
}

/*
 * Compares the attributes of the registered node with that of the
 * configured node. Sets different if the comparison fails.
 */
static errcode_t compare_node_attributes(O2CBCluster *cluster,
					 gchar *clustername, gchar *nodename,
					 int *different)
{
	errcode_t ret = 0;
	O2CBNode *node;
	char r_ip[30];
	gchar *c_ip = NULL;
	uint32_t r_port;
	guint c_port;
	uint16_t r_nodenum;
	gint c_nodenum;

	*different = 0;

	/* lookup nodename, ip, etc in config file */
	node = o2cb_cluster_get_node_by_name(cluster, nodename);
	if (!node) {
		*different = 1;
		verbosef(VL_DEBUG, "Registered node %s not found in config\n",
			 nodename);
		goto bail;
	}
	c_ip = o2cb_node_get_ip_string(node);
	c_port = o2cb_node_get_port(node);
	c_nodenum = o2cb_node_get_number(node);

	/* lookup the registered ip, port, nodenum */
	ret = o2cb_get_node_ip_string(clustername, nodename, r_ip,
				      sizeof(r_ip));
	if (!ret)
		ret = o2cb_get_node_port(clustername, nodename, &r_port);
	if (!ret)
		ret = o2cb_get_node_num(clustername, nodename, &r_nodenum);
	if (ret)
		goto bail;

	/* compare */
	if (strncmp(c_ip, r_ip, sizeof(r_ip)) || (c_port != r_port) ||
	    (c_nodenum != r_nodenum)) {
		*different = 1;
		verbosef(VL_DEBUG, "Registered node %s has changed. "
			 "%d, %s:%d => %d, %s:%d\n", nodename,
			 r_nodenum, r_ip, r_port, c_nodenum, c_ip, c_port);
	}

bail:
	g_free(c_ip);
	return ret;
}

/*
 * unregister_nodes()
 * If cluster != NULL, unregister nodes that are _no_ longer in the config file,
 * or, are in it with different attributes.
 * If cluster == NULL, unregister all nodes.
 */
static errcode_t unregister_nodes(O2CBCluster *cluster, gchar *clustername)
{
	errcode_t ret;
	char **nodenames = NULL;
	int i = 0, different;

	ret = o2cb_list_nodes(clustername, &nodenames);
	if (ret)
		goto bail;

	if (!nodenames)
		goto bail;

	while (nodenames[i] && *(nodenames[i])) {
		if (cluster) {
			ret = compare_node_attributes(cluster, clustername,
						      nodenames[i], &different);
			if (ret) {
				tcom_err(ret, "while comparing node attributes "
					 "for node %s", nodenames[i]);
				break;
			}
			if (!different) {
				i++;
				continue;
			}
		}

		verbosef(VL_DEBUG, "Unregistering node %s\n", nodenames[i]);

		ret = o2cb_del_node(clustername, nodenames[i]);
		if (!ret) {
			i++;
			continue;
		}
		tcom_err(ret, "while unregistering node '%s'", nodenames[i]);
		break;
	}

bail:
	if (nodenames)
		o2cb_free_nodes_list(nodenames);

	return ret;
}

static errcode_t register_nodes(O2CBCluster *cluster, gchar *clustername)
{
	errcode_t ret = 0;
	O2CBNode *node;
	JIterator *iter;
	gchar *nodename, *ip;
	gint port, nodenum;
	gboolean local;
	gchar s_port[16], s_nodenum[16], s_local[16];

	/* Unregister nodes that have been removed/changed in config */
	ret = unregister_nodes(cluster, clustername);
	if (ret)
		goto bail;

	/* Register nodes... silently skip if node already registered */
	iter = o2cb_cluster_get_nodes(cluster);

	while (!ret && j_iterator_has_more(iter)) {
		node = j_iterator_get_next(iter);
		nodename = o2cb_node_get_name(node);
		ip = o2cb_node_get_ip_string(node);
		nodenum = o2cb_node_get_number(node);
		port = o2cb_node_get_port(node);

		ret = node_is_local(nodename, &local);
		if (ret)
			goto free;

		snprintf(s_port, sizeof(s_port), "%d", port);
		snprintf(s_nodenum, sizeof(s_nodenum), "%d", nodenum);
		snprintf(s_local, sizeof(s_local), "%d", local);

		verbosef(VL_DEBUG, "Registering node %d, %s, %s:%d, %d\n",
			 nodenum, nodename, ip, port, local);

		ret = o2cb_add_node(clustername, nodename, s_nodenum,
				    ip, s_port, s_local);
		if (ret && ret != O2CB_ET_NODE_EXISTS) {
			tcom_err(ret, "while registering node '%s'", nodename);
			goto free;
		}

		verbosef(VL_DEBUG, "Node %s %s\n", nodename,
			 ((ret == O2CB_ET_NODE_EXISTS) ? "skipped" : "added"));

		ret = 0;
free:
		g_free(nodename);
		g_free(ip);
	}

	j_iterator_free(iter);
bail:

	return ret;
}

static errcode_t register_heartbeat_mode(O2CBCluster *cluster,
					 gchar *clustername)
{
	errcode_t ret;
	gchar *hbmode = NULL;

	hbmode = o2cb_cluster_get_heartbeat_mode(cluster);

	ret = o2cb_set_heartbeat_mode(clustername, hbmode);
	if (ret)
		tcom_err(ret, "while registering heartbeat mode '%s'", hbmode);

	if (hbmode)
		g_free(hbmode);

	return ret;
}

static errcode_t unregister_cluster(gchar *clustername)
{
	errcode_t ret;

	ret = o2cb_remove_cluster(clustername);
	if (ret)
		tcom_err(ret, "while unregistering cluster '%s'", clustername);

	return ret;
}

static errcode_t register_cluster(gchar *clustername)
{
	errcode_t ret;

	ret = o2cb_create_cluster(clustername);
	if (!ret)
		goto bail;

	if (ret == O2CB_ET_CLUSTER_EXISTS) {
		ret = 0;
		goto bail;
	}

	tcom_err(ret, "while registering cluster '%s'", clustername);

bail:
	return ret;
}

/*
 * register-cluster <clustername>
 */
errcode_t o2cbtool_register_cluster(struct o2cb_command *cmd)
{
	O2CBCluster *cluster;
	errcode_t ret = -1;
	gchar *clustername;

	o2cbtool_block_signals(SIG_BLOCK);

	if (cmd->o_argc < 2)
		goto bail;

	cmd->o_print_usage = 0;

	clustername = cmd->o_argv[1];

	cluster = o2cb_config_get_cluster_by_name(cmd->o_config, clustername);
	if (!cluster) {
		errorf("Unknown cluster '%s'\n", clustername);
		goto bail;
	}

	ret = o2cbtool_validate_clustername(clustername);
	if (ret)
		goto bail;

	ret = o2cbtool_init_cluster_stack();
	if (ret)
		goto bail;

	verbosef(VL_DEBUG, "Registering cluster '%s'\n", clustername);
	ret = register_cluster(clustername);
	if (ret)
		goto bail;

	verbosef(VL_DEBUG, "Registering heartbeat mode in cluster '%s'\n",
		 clustername);
	ret = register_heartbeat_mode(cluster, clustername);
	if (ret)
		goto bail;

	verbosef(VL_DEBUG, "Registering nodes in cluster '%s'\n", clustername);
	ret = register_nodes(cluster, clustername);
	if (ret)
		goto bail;

	verbosef(VL_APP, "Cluster '%s' registered\n", clustername);
bail:
	o2cbtool_block_signals(SIG_UNBLOCK);
	return ret;
}

static errcode_t proceed_unregister(char *name)
{
	gchar **clusternames = NULL, **regions = NULL;
	errcode_t ret;

	/* lookup the registered cluster */
	ret = o2cb_list_clusters(&clusternames);
	if (ret) {
		tcom_err(ret, "while looking up the registered cluster");
		goto bail;
	}

	if (!clusternames)
		goto bail;

	/* check if name matches to the registered cluster */
	if (!clusternames[0] || strcmp(clusternames[0], name)) {
		errorf("Cluster '%s' is not active\n", name);
		ret = -1;
		goto bail;
	}

	/* lookup active heartbeats */
	ret = o2cb_list_hb_regions(name, &regions);
	if (ret) {
		tcom_err(ret, "while looking up the active heartbeat regions");
		goto bail;
	}

	/* error, if found */
	if (regions[0] && *(regions[0])) {
		ret = -1;
		errorf("Atleast one heartbeat region is still active\n");
		goto bail;
	}

bail:
	if (regions)
		o2cb_free_hb_regions_list(regions);
	if (clusternames)
		o2cb_free_cluster_list(clusternames);
	return ret;
}
/*
 * unregister-cluster <clustername>
 */
errcode_t o2cbtool_unregister_cluster(struct o2cb_command *cmd)
{
	errcode_t ret = -1;
	gchar *clustername;

	o2cbtool_block_signals(SIG_BLOCK);

	if (cmd->o_argc < 2)
		goto bail;

	cmd->o_print_usage = 0;

	clustername = cmd->o_argv[1];

	ret = o2cbtool_init_cluster_stack();
	if (ret)
		goto bail;

	verbosef(VL_DEBUG, "Looking up cluster '%s'\n", clustername);
	ret = proceed_unregister(clustername);
	if (ret)
		goto bail;

	verbosef(VL_DEBUG, "Unregistering nodes in cluster '%s'\n",
		 clustername);
	ret = unregister_nodes(NULL, clustername);
	if (ret)
		goto bail;

	verbosef(VL_DEBUG, "Unregistering cluster '%s'\n", clustername);
	ret = unregister_cluster(clustername);
	if (ret)
		goto bail;

	verbosef(VL_APP, "Cluster '%s' unregistered\n", clustername);

bail:
	o2cbtool_block_signals(SIG_UNBLOCK);
	return ret;
}
