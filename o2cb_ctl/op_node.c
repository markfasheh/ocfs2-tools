/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * op_node.c
 *
 * Manipulates nodes in the o2cb cluster configuration
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

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

static int add_node_parse_options(int argc, char *argv[], char **ip, int *port,
				  int *nodenum, char **nodename,
				  char **clustername)
{
	int c, ret = -1, show_usage = 0;
	char *p;
	static struct option long_options[] = {
		{ "ip", 1, 0, IP_OPTION },
		{ "port", 1, 0, PORT_OPTION },
		{ "number", 1, 0, NODENUM_OPTION },
		{ 0, 0, 0, 0 },
	};

	while (1) {
		c = getopt_long(argc, argv, "", long_options, NULL);
		if (c == -1)
			break;
		switch (c) {
		case IP_OPTION:
			*ip = strdup(optarg);
			if (!*ip) {
				errorf("out-off-memory while copying ip\n");
				goto bail;
			}
			break;
		case PORT_OPTION:
			*port = strtol(optarg, &p, 0);
			if (*p) {
				errorf("invalid port number\n");
				goto bail;
			}
			break;
		case NODENUM_OPTION:
			*nodenum = strtol(optarg, &p, 0);
			if (*p) {
				errorf("invalid node number\n");
				goto bail;
			}
			break;
		default:
			++show_usage;
			break;
		}
	}

	if (optind + 2 > argc || show_usage)
		goto bail;

	*clustername = argv[optind];

	*nodename = tools_strstrip(argv[optind + 1]);
	if (!strlen(*nodename)) {
		errorf("node name cannot be zero length\n");
		goto bail;
	}

	ret = 0;
	verbosef(VL_DEBUG, "Add node '%s' in cluster '%s' having ip '%s', "
		 "port '%d' and number '%d'\n", *nodename, *clustername,
		 (*ip ? *ip : "auto"), *port, *nodenum);

bail:
	return ret;
}

static int validate_ip_address(char *nodename, char **ip)
{
	struct addrinfo hints, *ai = NULL;
	struct in_addr addr;
	int ret;

	/* if given, validate format */
	if (*ip) {
		ret = inet_pton(AF_INET, *ip, &addr);
		if (ret <= 0) {
			tcom_err(ret, "Bad IP Address '%s'", *ip);
			ret = -1;
		}
		ret = (ret > 0) ? 0 : ret;
		goto bail;
	}

	/* if not provided, discover it */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_socktype = SOCK_STREAM;

	ret = getaddrinfo(nodename, NULL, &hints, &ai);
	if (ret) {
		errorf("%s, while looking up the IP address for '%s'\n",
		       gai_strerror(ret), nodename);
		goto bail;
	}

	addr.s_addr = ((struct sockaddr_in *)(ai->ai_addr))->sin_addr.s_addr;

	*ip = strdup(inet_ntoa(addr));
	if (!*ip) {
		tcom_err(O2CB_ET_NO_MEMORY, "while setting ip for node '%s'",
			 nodename);
		goto bail;
	}

	ret = 0;

bail:
	if (ai)
		freeaddrinfo(ai);
	if (!ret)
		verbosef(VL_DEBUG, "Validated ip address '%s'\n", *ip);
	return ret;
}

static int validate_nodenum(O2CBCluster *cluster, int *nodenum)
{
	O2CBNode *node;
	int i, ret = -1;

	/* if none, then get the first unused nodenum */
	if (*nodenum == -1) {
		for(i = 0; i < O2NM_MAX_NODES; ++i) {
			node = o2cb_cluster_get_node(cluster, i);
			if (!node) {
				*nodenum = i;
				ret = 0;
				goto bail;
			}
		}
		errorf("Cluster is full - No more nodes can be added to it\n");
		goto bail;
	}

	/* if provided, validate range... */
	if (!(*nodenum >= 0 && *nodenum < O2NM_MAX_NODES)) {
		errorf("Nodenum should be >=0 and < %d but is %d\n",
		       O2NM_MAX_NODES, *nodenum);
		goto bail;
	}

	/* ... and ensure it is not inuse */
	node = o2cb_cluster_get_node(cluster, *nodenum);
	if (node) {
		errorf("Choose another node number as '%d' is in use\n",
		       *nodenum);
		goto bail;
	}

	ret = 0;
bail:
	if (!ret)
		verbosef(VL_DEBUG, "Validated node number '%d'\n", *nodenum);
	return ret;
}

/*
 * o2cb add-node [--ip <ip>] [--port <port>] [--number <num>]
 * 			<clustername> <nodename>
 */
errcode_t o2cbtool_add_node(struct o2cb_command *cmd)
{
	O2CBCluster *cluster;
	O2CBNode *node;
	errcode_t ret;
	gchar *clustername = '\0', *nodename = '\0', *ip = '\0';
	gint port = -1, nodenum = -1;

	ret = add_node_parse_options(cmd->o_argc, cmd->o_argv, &ip, &port,
				     &nodenum, &nodename, &clustername);
	if (ret) {
		errorf("usage: %s %s\n", cmd->o_name, cmd->o_usage);
		goto bail;
	}

	ret = -1;
	cluster = o2cb_config_get_cluster_by_name(cmd->o_config, clustername);
	if (!cluster) {
		errorf("unknown cluster '%s'\n", clustername);
		goto bail;
	}

	/* validate */
	ret = validate_ip_address(nodename, &ip);
	if (ret)
		goto bail;

	ret = validate_nodenum(cluster, &nodenum);
	if (ret)
		goto bail;

	if (port == -1)
		port = O2CB_DEFAULT_IP_PORT;

	ret = -1;
	node = o2cb_cluster_add_node(cluster, nodename);
	if (!node) {
		errorf("node '%s' already exists\n", nodename);
		goto bail;
	}

	ret = o2cb_node_set_ip_string(node, ip);
	if (ret) {
		tcom_err(ret, "while setting ip '%s'", ip);
		goto bail;
	}

	o2cb_node_set_port(node, port);
	o2cb_node_set_number(node, nodenum);

	cmd->o_modified = 1;
	ret = 0;
	verbosef(VL_APP, "Added node '%s' in cluster '%s' having ip '%s', "
		 "port '%d' and number '%d'\n", nodename, clustername, ip,
		 port, nodenum);

bail:
	return ret;
}

/*
 * o2cb remove-node <clustername> <nodename>
 */
errcode_t o2cbtool_remove_node(struct o2cb_command *cmd)
{
	O2CBCluster *cluster;
	errcode_t ret = -1;
	gchar *clustername, *nodename;

	if (cmd->o_argc < 3) {
		errorf("usage: %s %s\n", cmd->o_name, cmd->o_usage);
		goto bail;
	}

	clustername = cmd->o_argv[1];
	nodename = cmd->o_argv[2];

	cluster = o2cb_config_get_cluster_by_name(cmd->o_config, clustername);
	if (!cluster) {
		errorf("unknown cluster '%s'\n", clustername);
		goto bail;
	}

	ret = o2cb_cluster_delete_node(cluster, nodename);
	if (ret) {
		errorf("unknown node '%s'\n", nodename);
		goto bail;
	}

	cmd->o_modified = 1;
	ret = 0;
	verbosef(VL_APP, "Removed node '%s' from cluster '%s'\n", nodename,
		 clustername);

bail:
	return ret;
}
