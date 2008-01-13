/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
 *  This copyrighted material is made available to anyone wishing to use,
 *  modify, copy, or redistribute it subject to the terms and conditions
 *  of the GNU General Public License v.2.
 */

/* Portions of this file are: */
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <syslog.h>
#include <time.h>
#include <openais/cpg.h>

#include "ocfs2-kernel/kernel-list.h"

#include "ocfs2_controld.h"

struct cgroup {
	struct list_head	cg_list;	/* List of all CPG groups */

	cpg_handle_t		cg_handle;
	int			cg_fd;
	int			cg_ci;

	struct cpg_name		cg_name;
	struct cpg_address	cg_members[CPG_MEMBERS_MAX];
	int			cg_member_count;

	/* Callback state */
	int			cg_got_confchg;
	struct cpg_address	cg_cb_members[CPG_MEMBERS_MAX];
	struct cpg_address	cg_cb_joined[CPG_MEMBERS_MAX];
	struct cpg_address	cg_cb_left[CPG_MEMBERS_MAX];
	int			cg_cb_member_count;
	int			cg_cb_joined_count;
	int			cg_cb_left_count;
};

struct cgroup daemon_group;
static int daemon_joined;
struct list_head group_list;
static int message_flow_control_on;

static void group_change(struct cgroup *cgroup)
{
}

static void daemon_change(struct cgroup *cgroup)
{
	int i, found = 0;

	log_debug("ocfs2_controld confchg: members %d, left %d, joined %d",
		  cgroup->cg_cb_member_count,
		  cgroup->cg_cb_left_count,
		  cgroup->cg_cb_joined_count);

	memcpy(&cgroup->cg_members, &cgroup->cg_cb_members,
	       sizeof(&cgroup->cg_cb_members));
	cgroup->cg_member_count = cgroup->cg_cb_member_count;

	for (i = 0; i < cgroup->cg_cb_member_count; i++) {
		if ((cgroup->cg_cb_members[i].nodeid == our_nodeid) &&
		    (cgroup->cg_cb_members[i].pid == (uint32_t)getpid())) {
		    found = 1;
		}
	}

	if (found)
		daemon_joined = 1;
	else
		log_error("this node is not in the ocfs2_controld confchg: %u %u",
			  our_nodeid, (uint32_t)getpid());

}

static void process_configuration_change(struct cgroup *cgroup)
{
	if (cgroup == &daemon_group)
		daemon_change(cgroup);
	else
		group_change(cgroup);
}

static struct cgroup *client_to_group(int ci)
{
	if (ci == daemon_group.cg_ci)
		return &daemon_group;

	log_error("unknown client %d", ci);
	return NULL;
}

static struct cgroup *handle_to_group(cpg_handle_t handle)
{
	if (handle == daemon_group.cg_handle)
		return &daemon_group;

	log_error("unknown handle %llu", (unsigned long long)handle);

	return NULL;
}

static void deliver_cb(cpg_handle_t handle, struct cpg_name *group_name,
		       uint32_t nodeid, uint32_t pid,
		       void *data, int data_len)
{
	log_debug("deliver called");
}

static void confchg_cb(cpg_handle_t handle, struct cpg_name *group_name,
		       struct cpg_address *member_list,
		       int member_list_entries,
		       struct cpg_address *left_list,
		       int left_list_entries,
		       struct cpg_address *joined_list,
		       int joined_list_entries)
{
	int i;
	struct cgroup *cgroup;

	log_debug("confchg called");

	cgroup = handle_to_group(handle);
	if (!cgroup)
		return;

	if (left_list_entries > CPG_MEMBERS_MAX) {
		log_debug("left_list_entries %d", left_list_entries);
		left_list_entries = CPG_MEMBERS_MAX;
	} else if (joined_list_entries > CPG_MEMBERS_MAX) {
		log_debug("joined_list_entries %d", joined_list_entries);
		joined_list_entries = CPG_MEMBERS_MAX;
	} else if (member_list_entries > CPG_MEMBERS_MAX) {
		log_debug("member_list_entries %d", member_list_entries);
		member_list_entries = CPG_MEMBERS_MAX;
	}

	cgroup->cg_cb_left_count = left_list_entries;
	cgroup->cg_cb_joined_count = joined_list_entries;
	cgroup->cg_cb_member_count = member_list_entries;

	for (i = 0; i < left_list_entries; i++)
		cgroup->cg_cb_left[i] = left_list[i];
	for (i = 0; i < joined_list_entries; i++)
		cgroup->cg_cb_joined[i] = joined_list[i];
	for (i = 0; i < member_list_entries; i++)
		cgroup->cg_cb_members[i] = member_list[i];

	cgroup->cg_got_confchg = 1;
}

static cpg_callbacks_t callbacks = {
	.cpg_deliver_fn	= deliver_cb,
	.cpg_confchg_fn	= confchg_cb,
};

static void process_cpg(int ci)
{
	cpg_error_t error;
	cpg_flow_control_state_t flow_control_state;
	struct cgroup *cgroup;

	cgroup = client_to_group(ci);
	if (!cgroup)
		return;

	cgroup->cg_got_confchg = 0;
	error = cpg_dispatch(cgroup->cg_handle, CPG_DISPATCH_ONE);
	if (error != CPG_OK) {
		log_error("cpg_dispatch error %d", error);
		return;
	}

	error = cpg_flow_control_state_get(cgroup->cg_handle,
					   &flow_control_state);
	if (error != CPG_OK)
		log_error("cpg_flow_control_state_get %d", error);
	else if (flow_control_state == CPG_FLOW_CONTROL_ENABLED) {
		message_flow_control_on = 1;
		log_debug("flow control on");
	} else {
		if (message_flow_control_on)
			log_debug("flow control off");
		message_flow_control_on = 0;
	}

	if (cgroup->cg_got_confchg)
		process_configuration_change(cgroup);
}

static void dead_cpg(int ci)
{
	if (ci == daemon_group.cg_ci) {
		log_error("cpg connection died");
		shutdown_daemon();

		/* We can't talk to cpg anymore */
		daemon_group.cg_handle = 0;
	}

	client_dead(ci);
}

int setup_cpg(void)
{
	cpg_error_t error;

	error = cpg_initialize(&daemon_group.cg_handle, &callbacks);
	if (error != CPG_OK) {
		log_error("cpg_initialize error %d", error);
		return error;
	}

	cpg_fd_get(daemon_group.cg_handle, &daemon_group.cg_fd);
	daemon_group.cg_ci = client_add(daemon_group.cg_fd,
					process_cpg, dead_cpg);

	strcpy(daemon_group.cg_name.value, "ocfs2_controld");
	daemon_group.cg_name.length = strlen(daemon_group.cg_name.value);

	do {
		error = cpg_join(daemon_group.cg_handle,
				 &daemon_group.cg_name);
		if (error == CPG_OK) {
			log_debug("cpg_join succeeded");
			error = 0;
		} else if (error == CPG_ERR_TRY_AGAIN) {
			log_debug("cpg_join retry");
			sleep(1);
		} else {
			log_error("cpg_join error %d", error);
			cpg_finalize(daemon_group.cg_handle);
			daemon_group.cg_handle = 0;
		}
	} while (error == CPG_ERR_TRY_AGAIN);

	return error;
}

void exit_cpg(void)
{
	int i;
	cpg_error_t error;

	if (!daemon_group.cg_handle)
		return;

	for (i = 0; i < 10; i++) {
		error = cpg_leave(daemon_group.cg_handle,
				  &daemon_group.cg_name);
		if (error == CPG_ERR_TRY_AGAIN) {
			if (!i)
				log_debug("cpg_leave retry");
			sleep(1);
			continue;
		}

		if (error == CPG_OK)
			log_debug("cpg_leave succeeded");
		else
			log_error("cpg_leave error %d", error);

		break;
	}

	log_debug("closing cpg connection");
	cpg_finalize(daemon_group.cg_handle);
}
