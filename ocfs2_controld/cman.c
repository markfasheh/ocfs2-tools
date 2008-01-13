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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <libcman.h>

#include "ocfs2-kernel/kernel-list.h"
#include "o2cb/o2cb.h"

#include "ocfs2_controld.h"

int			our_nodeid;
int			cman_ci;
char *			clustername;
cman_cluster_t		cluster;
static cman_handle_t	ch;
static cman_handle_t	ch_admin;
extern struct list_head mounts;
static cman_node_t      old_nodes[O2NM_MAX_NODES];
static int              old_node_count;
static cman_node_t      cman_nodes[O2NM_MAX_NODES];
static int              cman_node_count;


int kill_cman(int nodeid)
{
	return cman_kill_node(ch_admin, nodeid);
}

static int is_member(cman_node_t *node_list, int count, int nodeid)
{
	int i;

	for (i = 0; i < count; i++) {
		if (node_list[i].cn_nodeid == nodeid)
			return node_list[i].cn_member;
	}
	return 0;
}

static int is_old_member(int nodeid)
{
	return is_member(old_nodes, old_node_count, nodeid);
}

static int is_cman_member(int nodeid)
{
	return is_member(cman_nodes, cman_node_count, nodeid);
}

static cman_node_t *find_cman_node(int nodeid)
{
	int i;

	for (i = 0; i < cman_node_count; i++) {
		if (cman_nodes[i].cn_nodeid == nodeid)
			return &cman_nodes[i];
	}
	return NULL;
}

char *nodeid2name(int nodeid)
{
	cman_node_t *cn;

	cn = find_cman_node(nodeid);
	if (!cn)
		return NULL;
	return cn->cn_name;
}

/* keep track of the nodes */
static void statechange(void)
{
	int i, rv;

	old_node_count = cman_node_count;
	memcpy(&old_nodes, &cman_nodes, sizeof(old_nodes));

	cman_node_count = 0;
	memset(&cman_nodes, 0, sizeof(cman_nodes));
	rv = cman_get_nodes(ch, O2NM_MAX_NODES, &cman_node_count,
			    cman_nodes);
	if (rv < 0) {
		log_debug("cman_get_nodes error %d %d", rv, errno);
		return;
	}

	for (i = 0; i < old_node_count; i++) {
		if (old_nodes[i].cn_member &&
		    !is_cman_member(old_nodes[i].cn_nodeid)) {

			log_debug("cman: node %d removed",
				   old_nodes[i].cn_nodeid);
		}
	}

	for (i = 0; i < cman_node_count; i++) {
		if (cman_nodes[i].cn_member &&
		    !is_old_member(cman_nodes[i].cn_nodeid)) {

			log_debug("cman: node %d added",
				  cman_nodes[i].cn_nodeid);
		}
	}
}

static void cman_callback(cman_handle_t h, void *private, int reason, int arg)
{
	switch (reason) {
		case CMAN_REASON_TRY_SHUTDOWN:
#if 0
			if (list_empty(&mounts))
#endif
				cman_replyto_shutdown(ch, 1);
#if 0
			else {
				log_debug("no to cman shutdown");
				cman_replyto_shutdown(ch, 0);
			}
#endif
			break;

		case CMAN_REASON_STATECHANGE:
			statechange();
			break;
	}
}

static void dead_cman(int ci)
{
	if (ci != cman_ci) {
		log_error("Unknown connection %d", ci);
		return;
	}

	log_error("cman connection died");
	shutdown_daemon();
	client_dead(ci);
}

static void process_cman(int ci)
{
	int rv;

	if (ci != cman_ci) {
		log_error("Unknown connection %d", ci);
		return;
	}

	rv = cman_dispatch(ch, CMAN_DISPATCH_ALL);
	if (rv == -1 && errno == EHOSTDOWN) {
		log_error("cman connection died");
		shutdown_daemon();
	}
}

int setup_cman(void)
{
	cman_node_t node;
	int rv, fd;

	ch = cman_init(NULL);
	if (!ch) {
		log_error("cman_init error %d", errno);
		rv = -ENOTCONN;
		goto fail_finish;
	}

	ch_admin = cman_admin_init(NULL);
	if (!ch) {
		log_error("cman_admin_init error %d", errno);
		rv = -ENOTCONN;
		goto fail_finish;
	}

	rv = cman_start_notification(ch, cman_callback);
	if (rv < 0) {
		log_error("cman_start_notification error %d %d", rv, errno);
		goto fail_finish;
	}

	/* FIXME: wait here for us to be a member of the cluster */

	memset(&cluster, 0, sizeof(cluster));
	rv = cman_get_cluster(ch, &cluster);
	if (rv < 0) {
		log_error("cman_get_cluster error %d %d", rv, errno);
		goto fail_stop;
	}
	clustername = cluster.ci_name;

	memset(&node, 0, sizeof(node));
	rv = cman_get_node(ch, CMAN_NODEID_US, &node);
	if (rv < 0) {
		log_error("cman_get_node error %d %d", rv, errno);
		goto fail_stop;
	}
	our_nodeid = node.cn_nodeid;

	fd = cman_get_fd(ch);

	old_node_count = 0;
	memset(&old_nodes, 0, sizeof(old_nodes));
	cman_node_count = 0;
	memset(&cman_nodes, 0, sizeof(cman_nodes));

	/* Fill the node list */
	statechange();

	cman_ci = client_add(fd, process_cman, dead_cman);
	return 0;

 fail_stop:
	cman_stop_notification(ch);
 fail_finish:
	if (ch_admin)
		cman_finish(ch_admin);
	if (ch)
		cman_finish(ch);
	ch = ch_admin = NULL;
	return rv;
}

void exit_cman(void)
{
	if (ch_admin)
		cman_finish(ch_admin);
	if (ch) {
		log_debug("closing cman connection");
		cman_stop_notification(ch);
		cman_finish(ch);
	}
}
