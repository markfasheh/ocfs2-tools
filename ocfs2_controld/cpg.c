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
#ifdef HAVE_COROSYNC
# include <corosync/cpg.h>
#else
# include <openais/cpg.h>
#endif

#include "ocfs2-kernel/kernel-list.h"

#include "ocfs2_controld.h"

struct cnode {
	struct list_head	cn_list;
	int			cn_nodeid;
};

struct cgroup {
	struct list_head	cg_list;	/* List of all CPG groups */

	cpg_handle_t		cg_handle;
	int			cg_joined;
	int			cg_fd;
	int			cg_ci;

	/* CPG's idea of the group */
	struct cpg_name		cg_name;
	struct cpg_address	cg_members[CPG_MEMBERS_MAX];
	int			cg_member_count;

	/*
	 * Our idea of the group.
	 * This lags cg_members until join/leave processing is complete.
	 */
	struct list_head	cg_nodes;
	int			cg_node_count;

	/* Hooks for mounters */
	void			(*cg_set_cgroup)(struct cgroup *cg,
						 void *user_data);
	void			(*cg_node_down)(int nodeid,
						void *user_data);
	void			*cg_user_data;

	/* Callback state */
	int			cg_got_confchg;
	struct cpg_address	cg_cb_members[CPG_MEMBERS_MAX];
	struct cpg_address	cg_cb_joined[CPG_MEMBERS_MAX];
	struct cpg_address	cg_cb_left[CPG_MEMBERS_MAX];
	int			cg_cb_member_count;
	int			cg_cb_joined_count;
	int			cg_cb_left_count;
};

/* Note, we never store cnode structures on daemon_group.  Thus,
 * ->node_down() (which is NULL) is never called */
struct cgroup daemon_group;
struct list_head group_list;
static int message_flow_control_on;

static void for_each_node_list(struct cpg_address list[], int count,
			       void (*func)(struct cpg_address *addr,
					    void *user_data),
			       void *user_data)
{
	int i;

	for (i = 0; i < count; i++)
		func(&list[i], user_data);
}

struct fen_proxy {
	void (*fp_func)(int nodeid,
			void *user_data);
	void *fp_data;
};

static void for_each_node_proxy(struct cpg_address *addr,
				void *user_data)
{
	struct fen_proxy *fen = user_data;

	fen->fp_func(addr->nodeid, fen->fp_data);
}

void for_each_node(struct cgroup *cg,
		   void (*func)(int nodeid,
				void *user_data),
		   void *user_data)
{
	struct fen_proxy fen = {
		.fp_func	= func,
		.fp_data	= user_data,
	};

	for_each_node_list(cg->cg_members, cg->cg_member_count,
			   for_each_node_proxy, &fen);
}

static struct cnode *find_node(struct cgroup *cg, int nodeid)
{
	struct list_head *p;
	struct cnode *cn = NULL;

	list_for_each(p, &cg->cg_nodes) {
		cn = list_entry(p, struct cnode, cn_list);
		if (cn->cn_nodeid == nodeid)
			break;

		cn = NULL;
	}

	return cn;
}

static void push_node(struct cgroup *cg, int nodeid)
{
	struct cnode *cn;

	if (find_node(cg, nodeid)) {
		log_error("Node %d is already part of group %.*s", nodeid,
			  cg->cg_name.length, cg->cg_name.value);
		/*
		 * If we got lost in our group members, we can't interact
		 * safely.
		 */
		shutdown_daemon();
		return;
	}

	cn = malloc(sizeof(struct cnode));
	if (!cn) {
		log_error("Unable to allocate node structure, exiting");
		/*
		 * If we can't keep track of the group, we can't
		 * interact safely.
		 */
		shutdown_daemon();
		return;
	}

	cn->cn_nodeid = nodeid;
	list_add(&cn->cn_list, &cg->cg_nodes);
	cg->cg_node_count++;
}

static void pop_node(struct cgroup *cg, int nodeid)
{
	struct cnode *cn = find_node(cg, nodeid);

	if (cn) {
		list_del(&cn->cn_list);
		cg->cg_node_count--;
	} else {
		log_error("Unable to find node %d in group %.*s", nodeid,
			  cg->cg_name.length, cg->cg_name.value);
	}

	if (cg->cg_node_count < 0) {
		log_error("cg_node_count went negative for group %.*s",
			  cg->cg_name.length, cg->cg_name.value);
		cg->cg_node_count = 0;
	}
}

static void push_node_on_join(struct cpg_address *addr,
			      void *user_data)
{
	struct cgroup *cg = user_data;

	log_debug("Filling node %d to group %.*s", addr->nodeid,
		  cg->cg_name.length, cg->cg_name.value);

	push_node(cg, addr->nodeid);
}

static void handle_node_join(struct cpg_address *addr,
			     void *user_data)
{
	struct cgroup *cg = user_data;

	log_debug("Node %d joins group %.*s",
		  addr->nodeid, cg->cg_name.length, cg->cg_name.value);

	/*
	 * If I read group/daemon/cpg.c correctly, you cannot have more than
	 * one entry in the join_list when you yourself join.  Thus, it is
	 * safe to add all members of cg_cb_members.  There will not be
	 * a duplicate in cg_cb_joined.
	 */
	if (addr->nodeid == our_nodeid) {
		if (cg->cg_joined) {
			log_error("This node has joined group %.*s more than once",
				  cg->cg_name.length, cg->cg_name.value);
		} else {
			log_debug("This node joins group %.*s",
				  cg->cg_name.length, cg->cg_name.value);
			for_each_node_list(cg->cg_cb_members,
					   cg->cg_cb_member_count,
					   push_node_on_join,
					   cg);
			cg->cg_joined = 1;
			cg->cg_set_cgroup(cg, cg->cg_user_data);
		}
	} else
		push_node(cg, addr->nodeid);

}

static void pop_nodes_on_leave(struct cpg_address *addr, void *user_data)
{
	struct cgroup *cg = user_data;

	pop_node(cg, addr->nodeid);
}

static void finalize_group(struct cgroup *cg)
{
	/* First, tell our mounter */
	cg->cg_set_cgroup(NULL, cg->cg_user_data);

	for_each_node_list(cg->cg_members, cg->cg_member_count,
			   pop_nodes_on_leave, cg);
	/* We're not in members anymore */
	pop_node(cg, our_nodeid);

	if (cg->cg_node_count)
		log_error("node count is not zero on group %.*s!",
			  cg->cg_name.length, cg->cg_name.value);
	if (!list_empty(&cg->cg_nodes))
		log_error("node list is not empty on group %.*s!",
			  cg->cg_name.length, cg->cg_name.value);

	cpg_finalize(cg->cg_handle);
	connection_dead(cg->cg_ci);

	list_del(&cg->cg_list);
	free(cg);
}

static void handle_node_leave(struct cpg_address *addr,
			      void *user_data)
{
	struct cgroup *cg = user_data;

	switch (addr->reason) {
		case CPG_REASON_LEAVE:
			log_debug("Node %d leaves group %.*s",
				  addr->nodeid, cg->cg_name.length,
				  cg->cg_name.value);
			if (addr->nodeid == our_nodeid)
				finalize_group(cg);
			else
				pop_node(cg, addr->nodeid);
			break;

		case CPG_REASON_NODEDOWN:
		case CPG_REASON_PROCDOWN:
			/*
			 * These are ignored here.  They will be handled
			 * at the daemon group level in daemon_change().
			 */
			break;

		case CPG_REASON_NODEUP:
		case CPG_REASON_JOIN:
			log_error("Unexpected reason %d while looking at group leave event for node %d",
				  addr->reason,
				  addr->nodeid);
			break;

		default:
			log_error("Invalid reason %d while looking at group leave event for node %d",
				  addr->reason,
				  addr->nodeid);
			break;
	}
}

static void group_change(struct cgroup *cg)
{
	log_debug("group \"%.*s\" confchg: members %d, left %d, joined %d",
		  cg->cg_name.length, cg->cg_name.value,
		  cg->cg_cb_member_count, cg->cg_cb_left_count,
		  cg->cg_cb_joined_count);

	for_each_node_list(cg->cg_cb_joined, cg->cg_cb_joined_count,
			   handle_node_join, cg);

	for_each_node_list(cg->cg_cb_left, cg->cg_cb_left_count,
			   handle_node_leave, cg);
}

static void handle_daemon_left(struct cpg_address *addr,
			       void *user_data)
{
	log_debug("node daemon left %d", addr->nodeid);

	switch (addr->reason) {
		case CPG_REASON_LEAVE:
			break;

		case CPG_REASON_PROCDOWN:
			/*
			 * ocfs2_controld failed but the node is
			 * still up.  If the node was part of any
			 * mounts, we need to kick it out of the cluster
			 * to force fencing.
			 */
			/* XXX actually check for mounts */
			if (1) {
				log_error("kill node %d - ocfs2_controld PROCDOWN",
					  addr->nodeid);
				kill_stack_node(addr->nodeid);
			}

			/* FALL THROUGH */

		case CPG_REASON_NODEDOWN:
			/* A nice clean failure */
			/* XXX process node leaving ocfs2_controld
			 * group.  This is not the same as
			 * processing a node leaving its mount
			 * groups.  We handle that below. */
			break;

		case CPG_REASON_NODEUP:
		case CPG_REASON_JOIN:
			log_error("Unexpected reason %d while looking at node leave event for node %d",
				  addr->reason,
				  addr->nodeid);
			break;

		default:
			log_error("Invalid reason %d while looking at node leave event for node %d",
				  addr->reason,
				  addr->nodeid);
			break;
	}
}

static void handle_node_down(struct cpg_address *addr,
			     void *user_data)
{
	struct list_head *p;
	struct cgroup *cg;

	if ((addr->reason != CPG_REASON_NODEDOWN) &&
	    (addr->reason != CPG_REASON_PROCDOWN))
		return;

	log_debug("node down %d", addr->nodeid);

	list_for_each(p, &group_list) {
		cg = list_entry(p, struct cgroup, cg_list);
		if (find_node(cg, addr->nodeid)) {
			cg->cg_node_down(addr->nodeid, cg->cg_user_data);
			pop_node(cg, addr->nodeid);
		}
	}
}

static void daemon_change(struct cgroup *cg)
{
	int i, found = 0;

	log_debug("ocfs2_controld (group \"%.*s\") confchg: members %d, left %d, joined %d",
		  cg->cg_name.length, cg->cg_name.value,
		  cg->cg_cb_member_count, cg->cg_cb_left_count,
		  cg->cg_cb_joined_count);

	for (i = 0; i < cg->cg_cb_member_count; i++) {
		if ((cg->cg_cb_members[i].nodeid == our_nodeid) &&
		    (cg->cg_cb_members[i].pid == (uint32_t)getpid())) {
		    found = 1;
		}
	}

	if (found) {
		if (!cg->cg_joined)
			cg->cg_set_cgroup(cg, cg->cg_user_data);
		cg->cg_joined = 1;
	} else
		log_error("this node is not in the ocfs2_controld confchg: %u %u",
			  our_nodeid, (uint32_t)getpid());

	/* Here we do any internal work for the daemon group changing */
	for_each_node_list(cg->cg_cb_left, cg->cg_cb_left_count,
			   handle_daemon_left, NULL);

	/*
	 * This is the pass to notify mountgroups.  It is important we do
	 * it here rather than in group_change(); we ensure the same order
	 * on all nodes.
	 */
	for_each_node_list(cg->cg_cb_left, cg->cg_cb_left_count,
			   handle_node_down, NULL);
}

static void process_configuration_change(struct cgroup *cg)
{
	memcpy(&cg->cg_members, &cg->cg_cb_members,
	       sizeof(&cg->cg_cb_members));
	cg->cg_member_count = cg->cg_cb_member_count;

	if (cg == &daemon_group)
		daemon_change(cg);
	else
		group_change(cg);
}

static struct cgroup *client_to_group(int ci)
{
	struct list_head *p;
	struct cgroup *cg;

	if (ci == daemon_group.cg_ci)
		return &daemon_group;

	list_for_each(p, &group_list) {
		cg = list_entry(p, struct cgroup, cg_list);
		if (cg->cg_ci == ci)
			return cg;
	}

	log_error("unknown client %d", ci);
	return NULL;
}

static struct cgroup *handle_to_group(cpg_handle_t handle)
{
	struct list_head *p;
	struct cgroup *cg;

	if (handle == daemon_group.cg_handle)
		return &daemon_group;

	list_for_each(p, &group_list) {
		cg = list_entry(p, struct cgroup, cg_list);
		if (cg->cg_handle == handle)
			return cg;
	}

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
	struct cgroup *cg;

	log_debug("confchg called");

	cg = handle_to_group(handle);
	if (!cg)
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

	cg->cg_cb_left_count = left_list_entries;
	cg->cg_cb_joined_count = joined_list_entries;
	cg->cg_cb_member_count = member_list_entries;

	for (i = 0; i < left_list_entries; i++)
		cg->cg_cb_left[i] = left_list[i];
	for (i = 0; i < joined_list_entries; i++)
		cg->cg_cb_joined[i] = joined_list[i];
	for (i = 0; i < member_list_entries; i++)
		cg->cg_cb_members[i] = member_list[i];

	cg->cg_got_confchg = 1;
}

static cpg_callbacks_t callbacks = {
	.cpg_deliver_fn	= deliver_cb,
	.cpg_confchg_fn	= confchg_cb,
};

static void process_cpg(int ci)
{
	cpg_error_t error;
	cpg_flow_control_state_t flow_control_state;
	struct cgroup *cg;

	cg = client_to_group(ci);
	if (!cg)
		return;

	cg->cg_got_confchg = 0;
	error = cpg_dispatch(cg->cg_handle, CPG_DISPATCH_ONE);
	if (error != CPG_OK) {
		log_error("cpg_dispatch error %d", error);
		return;
	}

	error = cpg_flow_control_state_get(cg->cg_handle,
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

	if (cg->cg_got_confchg)
		process_configuration_change(cg);
}

static void dead_cpg(int ci)
{
	struct cgroup *cg;

	if (ci == daemon_group.cg_ci) {
		log_error("cpg connection died");
		shutdown_daemon();

		/* We can't talk to cpg anymore */
		daemon_group.cg_handle = 0;
		connection_dead(ci);
	} else {
		cg = client_to_group(ci);
		if (cg)
			finalize_group(cg);
	}
}

static int start_join(struct cgroup *cg)
{
	cpg_error_t error;

	log_debug("Starting join for group \"%.*s\"",
		  cg->cg_name.length, cg->cg_name.value);
	do {
		error = cpg_join(cg->cg_handle, &cg->cg_name);
		if (error == CPG_OK) {
			log_debug("cpg_join succeeded");
			error = 0;
		} else if (error == CPG_ERR_TRY_AGAIN) {
			log_debug("cpg_join retry");
			sleep(1);
		} else
			log_error("cpg_join error %d", error);
	} while (error == CPG_ERR_TRY_AGAIN);

	return error;
}

static int start_leave(struct cgroup *cg)
{
	int i;
	cpg_error_t error;

	if (!cg->cg_handle)
		return -EINVAL;

	log_debug("leaving group \"%.*s\"",
		  cg->cg_name.length, cg->cg_name.value);

	for (i = 0; i < 10; i++) {
		error = cpg_leave(cg->cg_handle,
				  &cg->cg_name);
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

	if (error == CPG_OK)
		return 0;
	else if (error == CPG_ERR_TRY_AGAIN)
		return -EAGAIN;
	else
		return -EIO;
}


static int init_group(struct cgroup *cg, const char *name)
{
	cpg_error_t error;

	cg->cg_name.length = snprintf(cg->cg_name.value,
				      CPG_MAX_NAME_LENGTH,
				      "ocfs2:%s", name);
	if (cg->cg_name.length >= CPG_MAX_NAME_LENGTH) {
		log_error("Group name \"%s\" is too long", name);
		error = -ENAMETOOLONG;
		goto out;
	}

	error = cpg_initialize(&cg->cg_handle, &callbacks);
	if (error != CPG_OK) {
		log_error("cpg_initialize error %d", error);
		goto out;
	}

	cpg_fd_get(cg->cg_handle, &cg->cg_fd);
	cg->cg_ci = connection_add(cg->cg_fd, process_cpg, dead_cpg);
	if (cg->cg_ci < 0) {
		error = cg->cg_ci;
		log_error("Unable to add cpg client: %s", strerror(-error));
		goto out_finalize;
	}

	error = start_join(cg);
	if (error)
		goto out_close;

	return 0;

out_close:
	connection_dead(cg->cg_fd);

out_finalize:
	cpg_finalize(cg->cg_handle);
	cg->cg_handle = 0;

out:
	return error;
}

int group_leave(struct cgroup *cg)
{
	if (!cg->cg_joined) {
		log_error("Unable to leave unjoined group %.*s",
			  cg->cg_name.length, cg->cg_name.value);
		return -EINVAL;
	}

	return start_leave(cg);
}

int group_join(const char *name,
	       void (*set_cgroup)(struct cgroup *cg, void *user_data),
	       void (*node_down)(int nodeid, void *user_data),
	       void *user_data)
{
	int rc;
	struct cgroup *cg;

	cg = malloc(sizeof(struct cgroup));
	if (!cg) {
		log_error("Unable to allocate cgroup structure");
		rc = -ENOMEM;
		goto out;
	}

	memset(cg, 0, sizeof(struct cgroup));
	INIT_LIST_HEAD(&cg->cg_nodes);

	cg->cg_set_cgroup = set_cgroup;
	cg->cg_node_down = node_down;
	cg->cg_user_data = user_data;

	rc = init_group(cg, name);
	if (rc)
		free(cg);
	else
		list_add(&cg->cg_list, &group_list);

out:
	return rc;
}

static void daemon_set_cgroup(struct cgroup *cg, void *user_data)
{
	void (*daemon_joined)(int first) = user_data;

	if (cg != &daemon_group) {
		log_error("Somehow, daemon_set_cgroup is called on a different group!");
		return;
	}

	daemon_joined(daemon_group.cg_member_count == 1);
}

int setup_cpg(void (*daemon_joined)(int first))
{
	cpg_error_t error;

	INIT_LIST_HEAD(&group_list);
	daemon_group.cg_set_cgroup = daemon_set_cgroup;
	daemon_group.cg_user_data = daemon_joined;
	error = init_group(&daemon_group, "controld");

	return error;
}

void exit_cpg(void)
{
	if (!daemon_group.cg_handle)
		return;

	start_leave(&daemon_group);

	log_debug("closing cpg connection");
	cpg_finalize(daemon_group.cg_handle);
}
