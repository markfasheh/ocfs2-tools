/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 */

/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
 *  This copyrighted material is made available to anyone wishing to use,
 *  modify, copy, or redistribute it subject to the terms and conditions
 *  of the GNU General Public License v.2.
 */

#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <netdb.h>
#include <unistd.h>

#include "o2cb_controld.h"
#include "o2cb.h"


static char *cluster_name;


static char *str_ip(char *addr)
{
	static char str_ip_buf[INET6_ADDRSTRLEN];
	struct sockaddr_storage *ss = (struct sockaddr_storage *)addr;
	struct sockaddr_in *sin = (struct sockaddr_in *)addr;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)addr;
	void *saddr;

	if (ss->ss_family == AF_INET6)
		saddr = &sin6->sin6_addr;
	else
		saddr = &sin->sin_addr;

	inet_ntop(ss->ss_family, saddr, str_ip_buf, sizeof(str_ip_buf));
	return str_ip_buf;
}

static char *str_port(char *addr)
{
	static char str_port_buf[32];
	struct sockaddr_storage *ss = (struct sockaddr_storage *)addr;
	struct sockaddr_in *sin = (struct sockaddr_in *)addr;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)addr;
	int port;

	if (ss->ss_family == AF_INET6)
		port = ntohs(sin6->sin6_port);
	else
		port = ntohs(sin->sin_port);

	/* Fall back to default port */
	if (!port)
		port = 7777;

	snprintf(str_port_buf, sizeof(str_port_buf), "%d", port);
	return str_port_buf;
}

static int fill_cluster_name(void)
{
	if (cluster_name)
		return 0;

	cluster_name = get_cluster_name();
	if (!cluster_name)
		return -1;

	return 0;
}

static void finalize_nodes(void)
{
	int i;
	char **nodes = NULL;
	errcode_t err;

	err = o2cb_list_nodes(cluster_name, &nodes);
	if (err) {
		if (err != O2CB_ET_SERVICE_UNAVAILABLE) {
			com_err(prog_name, err,
				"while listing nodes for cluster \"%s\"",
				cluster_name);
		}
		return;
	}

	for (i = 0; nodes && nodes[i] && &(nodes[i]); i++)
		del_configfs_node(nodes[i]);

	o2cb_free_nodes_list(nodes);
}

void finalize_cluster(void)
{
	errcode_t err;

	if (fill_cluster_name())
		return;

	log_debug("Cleaning up cluster \"%s\"", cluster_name);

	finalize_nodes();

	err = o2cb_remove_cluster(cluster_name);
	if (err && (err != O2CB_ET_SERVICE_UNAVAILABLE))
		com_err(prog_name, err,
			"Unable to de-configure cluster \"%s\"",
			cluster_name);
}

static int initialize_cluster(void)
{
	static int initialized = 0;
	errcode_t err;

	if (initialized)
		return 0;

	if (fill_cluster_name())
		return -1;

	err = o2cb_create_cluster(cluster_name);
	if (!err)
		return 0;
	if (err == O2CB_ET_CLUSTER_EXISTS)
		return 0;

	com_err(prog_name, err, "Unable to configure cluster \"%s\"",
		cluster_name);

	return -1;
}

int add_configfs_node(const char *name, int nodeid, char *addr, int addrlen,
		      int local)
{
	char numbuf[32];
	errcode_t err;
	int rv;

	log_debug("add_configfs_node %s %d %s local %d",
		  name, nodeid, str_ip(addr), local);

	rv = initialize_cluster();
	if (rv < 0)
		return rv;

	memset(numbuf, 0, sizeof(numbuf));
	snprintf(numbuf, 32, "%d", nodeid);

	err = o2cb_add_node(cluster_name, name, numbuf,
			    str_ip(addr), str_port(addr),
			    local ? "1" : "0");
	if (err && (err != O2CB_ET_NODE_EXISTS)) {
		com_err(prog_name, err, "while adding node \"%s\" to cluster \"%s",
			name, cluster_name);
		rv = -1;
	}

	return rv;
}

void del_configfs_node(const char *name)
{
	errcode_t err;

	log_debug("del_configfs_node \"%s\"", name);

	err = o2cb_del_node(cluster_name, name);
	if (err) {
		com_err(prog_name, err,
			"while deleting node \"%s\" in cluster\"%s\"",
			name, cluster_name);
	}
}


void initialize_o2cb(void)
{
	errcode_t err;

	err = o2cb_init();
	if (err) {
		com_err(prog_name, err, "Cannot initialize o2cb\n");
		exit(EXIT_FAILURE);
	}
}
