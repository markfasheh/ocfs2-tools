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

#include <crm/crm.h>
#include <crm/common/cluster.h>

#include "ocfs2-kernel/kernel-list.h"
#include "o2cb/o2cb.h"

#include "ocfs2_controld.h"

#include <bzlib.h>
#include <crm/crm.h>
#include <crm/ais.h>
#include <sys/utsname.h>

int			our_nodeid = 0;
static int		pcmk_ci;
static char *		clustername = "pacemaker";
extern struct list_head mounts;
const char *stackname = "pcmk";

extern int ais_fd_async;
char *local_node_uname = NULL;

int kill_stack_node(int nodeid)
{
	int error = 1;

	log_debug("killing node %d", nodeid);

	/* error = cman_kill_node(ch_admin, nodeid); */
	if (error)
		log_debug("Unable to kill node %d, %d %d", nodeid, error,
			  errno);

	return error;
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

void exit_stack(void)
{
	log_debug("closing pacemaker connection");
	if (ais_fd_async) {
		close(ais_fd_async);
		ais_fd_async = 0;
	}
	if (ais_fd_sync) {
		close(ais_fd_sync);
		ais_fd_sync = 0;
	}
}

static void process_pcmk(int ci)
{
	/* ci ::= client number */
	char *data = NULL;
	char *uncompressed = NULL;
	AIS_Message *msg = NULL;
	SaAisErrorT rc = SA_AIS_OK;
	mar_res_header_t *header = NULL;
	static int header_len = sizeof(mar_res_header_t);

	header = malloc(header_len);
	memset(header, 0, header_len);

	errno = 0;
	rc = saRecvRetry(ais_fd_async, header, header_len);
	if (rc != SA_AIS_OK) {
		cl_perror("Receiving message header failed: (%d) %s", rc,
			  ais_error2text(rc));
		goto bail;
	} else if(header->size == header_len) {
		log_error("Empty message: id=%d, size=%d, error=%d, header_len=%d",
			  header->id, header->size, header->error, header_len);
		goto done;
	} else if(header->size == 0 || header->size < header_len) {
		log_error("Mangled header: size=%d, header=%d, error=%d",
			  header->size, header_len, header->error);
		goto done;
	} else if(header->error != 0) {
		log_error("Header contined error: %d", header->error);
	}

	header = realloc(header, header->size);
	/* Use a char* so we can store the remainder into an offset */
	data = (char*)header;

	errno = 0;
	rc = saRecvRetry(ais_fd_async, data+header_len, header->size - header_len);
	msg = (AIS_Message*)data;

	if (rc != SA_AIS_OK) {
		cl_perror("Receiving message body failed: (%d) %s", rc, ais_error2text(rc));
		goto bail;
	}
    
	data = msg->data;
	if(msg->is_compressed && msg->size > 0) {
		int rc = BZ_OK;
		unsigned int new_size = msg->size;

		if (check_message_sanity(msg, NULL) == FALSE)
			goto badmsg;

		log_debug("Decompressing message data");
		uncompressed = malloc(new_size);
		memset(uncompressed, 0, new_size);

		rc = BZ2_bzBuffToBuffDecompress(
			uncompressed, &new_size, data, msg->compressed_size,
			1, 0);

		if(rc != BZ_OK) {
			log_error("Decompression failed: %d", rc);
			goto badmsg;
		}

		CRM_ASSERT(rc == BZ_OK);
		CRM_ASSERT(new_size == msg->size);

		data = uncompressed;

	} else if(check_message_sanity(msg, data) == FALSE) {
		goto badmsg;

	} else if(safe_str_eq("identify", data)) {
		int pid = getpid();
		char *pid_s = crm_itoa(pid);

		send_ais_text(0, pid_s, TRUE, NULL, crm_msg_ais);
		crm_free(pid_s);
		goto done;
	}

	if (msg->header.id == crm_class_members) {
		xmlNode *xml = string2xml(data);

		if(xml != NULL) {
			const char *value = crm_element_value(xml, "id");
			if(value)
				crm_peer_seq = crm_int_helper(value, NULL);

			log_debug("Updating membership %llu", crm_peer_seq);
			/* crm_log_xml_info(xml, __PRETTY_FUNCTION__); */
			xml_child_iter(xml, node, crm_update_ais_node(node, crm_peer_seq));
			crm_calculate_quorum();
			free_xml(xml);
		} else {
			log_error("Invalid peer update: %s", data);
		}
	} else {
		log_error("Unexpected AIS message type: %d", msg->header.id);
	}

done:
	free(uncompressed);
	free(msg);
	return;

badmsg:
	log_error("Invalid message (id=%d, dest=%s:%s, from=%s:%s.%d):"
		  " min=%d, total=%d, size=%d, bz2_size=%d",
		  msg->id, ais_dest(&(msg->host)), msg_type2text(msg->host.type),
		  ais_dest(&(msg->sender)), msg_type2text(msg->sender.type),
		  msg->sender.pid, (int)sizeof(AIS_Message),
		  msg->header.size, msg->size, msg->compressed_size);
	free(uncompressed);
	free(msg);
	return;

bail:
	log_error("AIS connection failed");
	return;
}

int setup_stack(void)
{
	int retries = 0;
	int pid;
	char *pid_s;
	int rc = SA_AIS_OK;
	struct utsname name;

	crm_peer_init();

	if (local_node_uname == NULL) {
		if (uname(&name) < 0) {
			cl_perror("uname(2) call failed");
			exit(100);
		}
		local_node_uname = crm_strdup(name.nodename);
		log_debug("Local node name: %s", local_node_uname);
	}

	/* 16 := CRM_SERVICE */
retry:
	log_debug("Creating connection to our AIS plugin");
	rc = saServiceConnect (&ais_fd_sync, &ais_fd_async, 16);
	if (rc != SA_AIS_OK) {
		log_error("Connection to our AIS plugin failed: %s (%d)",
			  ais_error2text(rc), rc);
	}	

	switch(rc) {
	case SA_AIS_OK:
		break;
	case SA_AIS_ERR_TRY_AGAIN:
		if(retries < 30) {
			sleep(1);
			retries++;
			goto retry;
		}
		log_error("Retry count exceeded");
		return 0;
	default:
		return 0;
	}

	log_debug("AIS connection established");

	pid = getpid();
	pid_s = crm_itoa(pid);
	send_ais_text(0, pid_s, TRUE, NULL, crm_msg_ais);
	crm_free(pid_s);

	/* Sign up for membership updates */
	send_ais_text(crm_class_notify, "true", TRUE, NULL, crm_msg_ais);

	/* Requesting the current list of known nodes */
	send_ais_text(crm_class_members, __FUNCTION__, TRUE, NULL, crm_msg_ais);

	pcmk_ci = connection_add(ais_fd_async, process_pcmk, dead_pcmk);
	if (pcmk_ci >= 0)
		return ais_fd_async;

	log_error("Unable to add pacemaker client: %s", strerror(-pcmk_ci));
	exit_stack();
	return pcmk_ci;
}
