/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 */

/*
 * Copyright (C) 2008 Novell.
 *
 * Some portions Copyright Oracle.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>

#include <bzlib.h>

#include <pacemaker/crm_config.h>
/* heartbeat support is irrelevant here */
#undef SUPPORT_HEARTBEAT
#define SUPPORT_HEARTBEAT 0

#include <pacemaker/crm/crm.h>
#include <pacemaker/crm/ais.h>
#include <pacemaker/crm/common/cluster.h>
#include <pacemaker/crm/common/stack.h>
#include <pacemaker/crm/common/ipc.h>
#include <pacemaker/crm/msg_xml.h>

#include "ocfs2-kernel/kernel-list.h"
#include "o2cb/o2cb.h"

#include "ocfs2_controld.h"

#include <sys/utsname.h>

int			our_nodeid = 0;
static int		pcmk_ci;
static int		stonithd_ci;
static char *		clustername = "pacemaker";
extern struct list_head mounts;
const char *stackname = "pcmk";

extern int ais_fd_async;
char *local_node_uname = NULL;

static int pcmk_cluster_fd = 0;

static void stonith_callback(int ci)
{
    log_error("%s: Lost connection to the cluster", __FUNCTION__);
    pcmk_cluster_fd = 0;
    return;
}

int kill_stack_node(int nodeid)
{
	int fd = pcmk_cluster_fd;
	int rc = crm_terminate_member_no_mainloop(nodeid, NULL, &fd);


	if(fd > 0 && fd != pcmk_cluster_fd) {
		pcmk_cluster_fd = fd;
		connection_add(pcmk_cluster_fd, NULL, stonith_callback);
	}

	switch(rc) {
		case 1:
			log_debug("Requested that node %d be kicked from the cluster", nodeid);
			break;
		case -1:
			log_error("Don't know how to kick node %d from the cluster", nodeid);
			break;
		case 0:
			log_error("Could not kick node %d from the cluster", nodeid);
			break;
		default:
			log_error("Unknown result when kicking node %d from the cluster", nodeid);
			break;
	}

	return rc;
}

char *nodeid2name(int nodeid) {
	crm_node_t *node = crm_get_peer(nodeid, NULL);

	if(node->uname == NULL)
		return NULL;

	return strdup(node->uname);
}

int validate_cluster(const char *cluster)
{
	if (!clustername) {
		log_error("Trying to validate before pacemaker is alive");
		return 0;
	}

	if (!cluster)
		return 0;

	return !strcmp(cluster, clustername);
}

int get_clustername(const char **cluster)
{
	if (!clustername) {
		log_error("Trying to validate before pacemaker is alive");
		return -EIO;
	}

	if (!cluster) {
		log_error("NULL passed!");
		return -EINVAL;
	}

	*cluster = clustername;
	return 0;
}

static void dead_pcmk(int ci)
{
	if (ci != pcmk_ci) {
		log_error("Unknown connection %d", ci);
		return;
	}

	log_error("pacemaker connection died");
	shutdown_daemon();
	connection_dead(ci);
}

extern void terminate_ais_connection(void);

void exit_stack(void)
{
	log_debug("closing pacemaker connection");
	terminate_ais_connection();
}

static void process_pcmk(int ci)
{
	ais_dispatch(ais_fd_async, NULL);
}

int setup_stack(void)
{
	crm_log_init("ocfs2_controld", LOG_INFO, FALSE, TRUE, 0, NULL);

	if(init_ais_connection(NULL, NULL, NULL, &local_node_uname, &our_nodeid) == FALSE) {
		log_error("Connection to our AIS plugin (CRM) failed");
		return -1;
	}

	/* Sign up for membership updates */
	send_ais_text(crm_class_notify, "true", TRUE, NULL, crm_msg_ais);

	/* Requesting the current list of known nodes */
	send_ais_text(crm_class_members, __FUNCTION__, TRUE, NULL, crm_msg_ais);

	log_debug("Cluster connection established.  Local node id: %d", our_nodeid);

	pcmk_ci = connection_add(ais_fd_async, process_pcmk, dead_pcmk);
	if (pcmk_ci >= 0) {
		log_debug("Added Pacemaker as client %d with fd %d", pcmk_ci, ais_fd_async);
		return ais_fd_async;
	}

	log_error("Unable to add pacemaker client: %s", strerror(-pcmk_ci));
	exit_stack();
	return pcmk_ci;
}
