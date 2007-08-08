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
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>

#include "o2cb.h"
#include "ocfs2_controld.h"
#include "ocfs2_controld_internal.h"


enum mountgroup_state {
	MG_CREATED		= 1 << 0,
	MG_JOIN_SENT		= 1 << 1,
	MG_JOIN_START		= 1 << 2,
	MG_JOIN_START_DONE	= 1 << 3,
#define MG_JOINING	(MG_JOIN_SENT | MG_JOIN_START | MG_JOIN_START_DONE)
	MG_JOINED		= 1 << 4,
	MG_MOUNTED		= 1 << 5,
#define MG_MEMBER	(MG_JOINED | MG_MOUNTED)
	MG_LEAVE_SENT		= 1 << 6,
	MG_LEAVE_START		= 1 << 7,
	MG_LEAVE_START_DONE	= 1 << 8,
#define MG_LEAVING	(MG_LEAVE_SENT | MG_LEAVE_START | MG_LEAVE_START_DONE)
	MG_DEAD			= 1 << 9,
};

struct list_head mounts;

static void fill_error(struct mountgroup *mg, int error, char *errfmt, ...)
{
	int rc;
	va_list args;

	if (mg->error)
		return;

	mg->error = error;

	va_start(args, errfmt);
	rc = vsnprintf(mg->error_msg, sizeof(mg->error_msg), errfmt, args);
	va_end(args);

	if (rc >= sizeof(mg->error_msg)) {
		log_debug("Error message truncated");
		mg->error_msg[sizeof(mg->error_msg) - 1] = '\0';
	}
}

static int mg_statep(struct mountgroup *mg, enum mountgroup_state test,
		     enum mountgroup_state allowed)
{
	if (mg->state == test)
		return 1;

	if (allowed) {
		if (!(mg->state & allowed))
			log_error("mountgroup %s is in state %d, testing for %d, allowed %d",
				  mg->uuid, mg->state, test, allowed);
	}

	return 0;
}

static int mg_joining(struct mountgroup *mg)
{
	return mg_statep(mg, MG_JOINING, 0);
}

static int mg_leaving(struct mountgroup *mg)
{
	return mg_statep(mg, MG_LEAVING, 0);
}

static void notify_mount_client(struct mountgroup *mg)
{
	int error = mg->error;
	char *error_msg = "OK";

	if (error) {
		if (mg->error_msg[0]) {
			error_msg = mg->error_msg;
		} else
			error_msg = strerror(-error);
		mg->error = 0;
	}

	log_group(mg, "notify_mount_client sending %d \"%s\"", -error,
		  error_msg);

	error = send_message(mg->mount_client_fd, CM_STATUS, error,
			     error_msg);
	if (error)
		log_error("Unable to notify client, send_message failed with %d: %s",
			  -error, strerror(-error));
	else
		mg->mount_client_notified = 1;

	/*
	 * XXX If we failed to notify the client, what can we do?  I'm
	 * guessing that our main loop will get POLLHUP of some sort.
	 */
}

static int create_mountpoint(struct mountgroup *mg, const char *mountpoint,
			     int ci)
{
	int rc = -ENOMEM;
	struct mountpoint *mp;

	mp = malloc(sizeof(struct mountpoint));
	if (mp) {
		memset(mp, 0, sizeof(struct mountpoint));
		strncpy(mp->mountpoint, mountpoint, sizeof(mp->mountpoint));
		mp->client = ci;
		list_add(&mp->list, &mg->mountpoints);
		rc = 0;
	}

	return rc;
}

static struct mountpoint *find_mountpoint(struct mountgroup *mg,
					  const char *mountpoint, int ci)
{
	struct mountpoint *mp;
	struct list_head *p;
	int found = 0;

	list_for_each(p, &mg->mountpoints) {
		mp = list_entry(p, struct mountpoint, list);
		if (ci && (mp->client != ci))
			continue;
		if (!strcmp(mp->mountpoint, mountpoint)) {
			found = 1;
			break;
		}
	}

	return found ? mp : NULL;
}

static void remove_failed_mountpoint(struct mountgroup *mg,
				     const char *mountpoint, int ci)
{
	struct mountpoint *mp;

	mp = find_mountpoint(mg, mountpoint, ci);
	if (mp) {
		list_del(&mp->list);
		free(mp);
	}
	
	assert(mp);
}

static void add_another_mountpoint(struct mountgroup *mg,
				   const char *mountpoint,
				   const char *device, int ci)
{
	log_group(mg, "add_another_mountpoint %s device %s ci %d",
		  mountpoint, device, ci);

	if (strcmp(mg->device, device)) {
		fill_error(mg, -EINVAL,
			   "Trying to mount fs %s on device %s, but it already is mounted from device %s",
			   mg->uuid, mg->device, device);
		goto out;
	}

	if (find_mountpoint(mg, mountpoint, 0)) {
		fill_error(mg, -EBUSY,
			   "Filesystem %s is already mounted on %s",
			   mg->uuid, mountpoint);
		goto out;
	}

	if (mg->mount_client || mg->mount_client_fd || !mg->kernel_mount_done) {
		fill_error(mg, -EBUSY, "Another mount is in process");
		goto out;
	}

	if (create_mountpoint(mg, mountpoint, ci)) {
		fill_error(mg, -ENOMEM,
			   "Unable to allocate mountpoint structure");
		goto out;
	}

	mg->mount_client = ci;

	/*
	 * This special error is returned to mount.ocfs2 to tell it that
	 * the mount is a secondary one and no additional work is required.
	 * It will just call mount(2).
	 */
	fill_error(mg, -EALREADY, "Kernel mounted, go ahead");

out:
	return;
}

struct mountgroup *find_mg(const char *uuid)
{
	struct list_head *p;
	struct mountgroup *mg;

	list_for_each(p, &mounts) {
		mg = list_entry(p, struct mountgroup, list);
		if ((strlen(mg->uuid) == strlen(uuid)) &&
		    !strncmp(mg->uuid, uuid, strlen(uuid)))
			return mg;
	}
	return NULL;
}

static struct mountgroup *create_mg(const char *uuid, const char *mountpoint,
				    int ci)
{
	struct mountgroup *mg = NULL;

	mg = malloc(sizeof(struct mountgroup));
	if (!mg)
		goto out;

	memset(mg, 0, sizeof(struct mountgroup));
	INIT_LIST_HEAD(&mg->members);
	INIT_LIST_HEAD(&mg->mountpoints);
	mg->state = MG_CREATED;
	strncpy(mg->uuid, uuid, sizeof(mg->uuid));

	if (create_mountpoint(mg, mountpoint, ci)) {
		free(mg);
		mg = NULL;
	}

out:
	return mg;
}

int do_mount(int ci, int fd, const char *fstype, const char *uuid,
	     const char *cluster, const char *device,
	     const char *mountpoint, struct mountgroup **mg_ret)
{
	int rc = 0;
	struct mountgroup mg_error = { /* Until we have a real one */
		.error		= 0,
	};
	struct mountgroup *mg = &mg_error;

	log_debug("mount: MOUNT %s %s %s %s %s",
		  fstype, uuid, cluster, device, mountpoint);

	if (strcmp(fstype, "ocfs2")) {
		fill_error(mg, -EINVAL, "Unsupported fstype: %s", fstype);
		goto out;
	}

	if (!strlen(cluster) || (strlen(cluster) != strlen(clustername)) ||
	    strcmp(cluster, clustername)) {
		fill_error(mg, -EINVAL,
			   "Request for mount in cluster %s but we belong to %s",
			  cluster, clustername);
		goto out;
	}

	if (strlen(uuid) > MAXNAME) {
		fill_error(mg, -ENAMETOOLONG, "UUID too long: %s", uuid);
		goto out;
	}

	mg = find_mg(uuid);
	if (mg) {
		add_another_mountpoint(mg, mountpoint, device, ci);
		goto out;
	}

	/* Here we stop using &mg_error and start using our real one */
	mg = create_mg(uuid, mountpoint, ci);
	if (!mg) {
		mg = &mg_error;  /* Well, almost */
		fill_error(mg, -ENOMEM,
			   "Unable to allocate mountgroup structure");
		goto out;
	}

	mg->mount_client = ci;
	strncpy(mg->type, fstype, sizeof(mg->type));
	strncpy(mg->cluster, cluster, sizeof(mg->cluster));
	strncpy(mg->device, device, sizeof(mg->device));
	list_add(&mg->list, &mounts);

	rc = group_join(gh, (char *)uuid);
	if (rc) {
		fill_error(mg, -errno, "Unable to start group join: %s",
			   strerror(errno));

		/*
		 * Remove the mountpoint so we can free the mountgroup
		 * at the bottom.
		 */
		remove_failed_mountpoint(mg, mountpoint, ci);
		goto out;
	}

	mg->state = MG_JOIN_SENT;

	*mg_ret = mg;
	log_group(mg, "mount successfully started");

out:
	/*
	 * Only reply on error.  If we're doing OK, the reply is delayed
	 * until join completes (notify_mount_client()).
	 */
	if (mg->error) {
		rc = mg->error;
		send_message(fd, CM_STATUS, mg->error, mg->error_msg);

		/* -EALREADY magic is sent, clear it */
		if (mg->error == -EALREADY)
			mg->error = 0;
		else {
			log_error("mount: %s", mg->error_msg);

			if ((mg != &mg_error) &&
			    list_empty(&mg->mountpoints)) {
				log_debug("mount: freeing failed mountgroup");
				list_del(&mg->list);
				free(mg);
			}
		}
	}

	log_debug("do_mount returns %d", rc);
	return rc;
}

int do_mount_result(struct mountgroup *mg, int ci, int another,
		    const char *fstype, const char *uuid,
		    const char *errcode, const char *mountpoint)
{
	int rc = 0;
	int reply = 1;
	char *ptr = NULL;
	long err;

	log_debug("mount: MRESULT %s %s %s %s",
		  fstype, uuid, errcode, mountpoint);

	assert(mg->mount_client == ci);
	assert(!mg->error);

	if (strcmp(fstype, "ocfs2")) {
		fill_error(mg, -EINVAL, "Unsupported fstype: %s", fstype);
		goto out;
	}

	if (strlen(uuid) > MAXNAME) {
		fill_error(mg, -ENAMETOOLONG, "UUID too long: %s", uuid);
		goto out;
	}

	if (strcmp(uuid, mg->uuid)) {
		fill_error(mg, -EINVAL,
			   "UUID %s does not match mountgroup %s", uuid,
			   mg->uuid);
		goto out;
	}

	/* XXX Check that mountpoint is valid */

	err = strtol(errcode, &ptr, 10);
	if (ptr && *ptr != '\0') {
		fill_error(mg, -EINVAL, "Invalid error code string: %s",
			   errcode);
		goto out;
	}
	if ((err == LONG_MIN) || (err == LONG_MAX) || (err < INT_MIN) ||
	    (err > INT_MAX)) {
		fill_error(mg, -ERANGE, "Error code %ld out of range", err);
		goto out;
	}

	if (another) {
		if (err) {
			remove_failed_mountpoint(mg, mountpoint, ci);
			assert(!list_empty(&mg->mountpoints));
		}
		/*
		 * rc is zero, we're responding to mount.ocfs2 that we
		 * got the message.  There's nothing else to do.
		 */
		goto out;
	}

	mg->kernel_mount_done = 1;
	mg->kernel_mount_error = err;

	if (!err) {
		/* Everyone's happy */
		mg->mount_client = 0;
		mg->mount_client_fd = 0;

		goto out;
	}

	/*
	 * We're failing an initial mount.  We keep mount_client_fd around
	 * both to send the result of the LEAVE as well as to keep other
	 * clients from trying to race us.  We don't reply to the client
	 * until the LEAVE has completed.
	 */
	reply = 0;
	remove_failed_mountpoint(mg, mountpoint, ci);
	assert(list_empty(&mg->mountpoints));

	/* We shouldn't get to MRESULT unless we're a member, but... */
	if (!mg_statep(mg, MG_MEMBER, MG_MEMBER)) {
		mg->group_leave_on_finish = 1;
		goto out;
	}

	if (group_leave(gh, mg->uuid))
		fill_error(mg, -errno, "Unable to start group leave: %s",
			   strerror(errno));
	else
		mg->state = MG_LEAVE_SENT;

out:
	if (reply)
		send_message(mg->mount_client_fd, CM_STATUS, mg->error,
			     mg->error ? mg->error_msg : "OK");

	return rc;
}

int do_unmount(int ci, int fd, const char *fstype, const char *uuid,
	       const char *mountpoint)
{
	int rc = 0;
	int reply = 1;
	struct mountgroup mg_error = {
		.error = 0,
	};
	struct mountgroup *mg;
	struct mountpoint *mp;

	log_debug("unmount: UMOUNT %s %s %s",
		  fstype, uuid, mountpoint);

	if (strcmp(fstype, "ocfs2")) {
		fill_error(&mg_error, -EINVAL, "Unsupported fstype: %s",
			   fstype);
		goto out;
	}

	if (strlen(uuid) > MAXNAME) {
		fill_error(&mg_error, -ENAMETOOLONG, "UUID too long: %s",
			   uuid);
		goto out;
	}

	/* Once we have our mg, we're done with &mg_error */
	mg = find_mg(uuid);
	if (!mg) {
		fill_error(&mg_error, -ENOENT, "Unknown uuid %s", uuid);
		goto out;
	}

	/* We shouldn't find the mg if the uuid isn't mounted *somewhere* */
	assert(!list_empty(&mg->mountpoints));

	mp = find_mountpoint(mg, mountpoint, 0);
	if (!mp) {
		fill_error(&mg_error, -ENOENT,
			   "Filesystem %s is not mounted on %s", uuid,
			   mountpoint);
		goto out;
	}

	/* XXX Do we check kernel_mount_done? */

	log_group(mg, "removing mountpoint %s", mountpoint);
	list_del(&mp->list);
	free(mp);

	if (!list_empty(&mg->mountpoints)) {
		log_group(mg, "mounts still remain");
		goto out;
	}

	/*
	 * We're clearing the last mount.  We must leave the group before
	 * we let umount.ocfs2 complete.  Thus, we'll let
	 * notify_mount_client() handle the rest.
	 */

	reply = 0;

	/*
	 * We shouldn't be allowing another client to connect before
	 * we get to MG_MEMBER, but let's be safe
	 */
	if (!mg_statep(mg, MG_MEMBER, MG_MEMBER)) {
		mg->group_leave_on_finish = 1;
		goto out;
	}

	if (group_leave(gh, mg->uuid)) {
		/* We spoke too soon! */
		/* XXX How can a client clean this up? */
		reply = 1;
		fill_error(&mg_error, -errno, "Unable to leave group: %s",
			   strerror(errno));
	} else
		mg->state = MG_LEAVE_SENT;

out:
	if (reply)
		send_message(fd, CM_STATUS, mg_error.error,
			     mg_error.error ? mg_error.error_msg : "OK");

	return rc;
}

static struct mg_member *find_memb_nodeid(struct mountgroup *mg, int nodeid)
{
	struct list_head *p;
	struct mg_member *memb;

	list_for_each(p, &mg->members) {
		memb = list_entry(p, struct mg_member, list);
		if (memb->nodeid == nodeid)
			return memb;
	}
	return NULL;
}

#define MEMBER_LINK_FORMAT	"/sys/kernel/config/cluster/%s/region/%s/%s"
#define MEMBER_TARGET_FORMAT	"/sys/kernel/config/cluster/%s/node/%s"
static int drop_member(struct mountgroup *mg, struct mg_member *memb)
{
	int rc;
	char link[PATH_MAX+1];

	/* 
	 * XXX Can we just remove here, or should we wait until
	 * do_finish()?  I think we can just remove them
	 */

	list_del(&memb->list);
	memb->gone_event = mg->start_event_nr;
	memb->gone_type = mg->start_type;
	mg->memb_count--;

	snprintf(link, PATH_MAX, MEMBER_LINK_FORMAT, clustername, mg->uuid,
		 memb->name);
	rc = rmdir(link);
	if (rc)
		log_error("rmdir of %s failed: %d", link, errno);

	free(memb);

	return rc;
}

static int add_member(struct mountgroup *mg, int nodeid)
{
	int rc;
	struct list_head *p;
	char *node_name;
	struct mg_member *memb, *test, *target = NULL;
	char link[PATH_MAX+1], nodepath[PATH_MAX+1];

	memb = malloc(sizeof(struct mg_member));
	if (!memb) {
		rc = -errno;
		goto out;
	}

	memset(memb, 0, sizeof(struct mg_member));
	memb->nodeid = nodeid;
	node_name = nodeid2name(nodeid);
	if (!node_name) {
		log_error("Unable to determine name for node %d", nodeid);
		rc = -EINVAL;
		goto out_free;
	}

	strncpy(memb->name, node_name, NAME_MAX);
	mg->memb_count++;

	list_for_each(p, &mg->members) {
		test = list_entry(p, struct mg_member, list);
		if (memb->nodeid < test->nodeid) {
			target = test;
			break;
		}
	}

	snprintf(link, PATH_MAX, MEMBER_LINK_FORMAT, clustername, mg->uuid,
		 memb->name);
	snprintf(nodepath, PATH_MAX, MEMBER_TARGET_FORMAT, clustername,
		 memb->name);

	rc = symlink(nodepath, link);
	if (rc) {
		rc = -errno;
		goto out_free;
	}

	if (target)
		list_add_tail(&memb->list, &target->list);
	else
		list_add_tail(&memb->list, &mg->members);

out_free:
	if (rc)
		free(memb);

out:
	return rc;
}

static int is_member(struct mountgroup *mg, int nodeid)
{
	return find_memb_nodeid(mg, nodeid) != NULL;
}

static int path_exists(const char *path)
{
	struct stat buf;

	if (stat(path, &buf) < 0) {
		if (errno != ENOENT)
			log_error("%s: stat failed: %d", path, errno);
		return 0;
	}
	return 1;
}

static int create_path(char *path)
{
	mode_t old_umask;
	int rv;

	old_umask = umask(0022);
	rv = mkdir(path, 0777);
	umask(old_umask);

	if (rv < 0) {
		rv = -errno;
		log_error("%s: mkdir failed: %d", path, -rv);
		if (-rv == EEXIST)
			rv = 0;
	}
	return rv;
}

#define REGION_FORMAT "/sys/kernel/config/cluster/%s/heartbeat/%s"
static int initialize_region(struct mountgroup *mg)
{
	int rc = 0;
	char path[PATH_MAX+1];

	snprintf(path, PATH_MAX, REGION_FORMAT, clustername, mg->uuid);

	if (!path_exists(path)) {
		rc = create_path(path);
		if (rc) {
			fill_error(mg, -rc, "Unable to create region %s",
				   mg->uuid);
			mg->group_leave_on_finish = 1;
		}
	}

	return rc;
}

static int drop_region(struct mountgroup *mg)
{
	int rc = 0;
	char path[PATH_MAX+1];

	snprintf(path, PATH_MAX, REGION_FORMAT, clustername, mg->uuid);

	if (path_exists(path)) {
		rc = rmdir(path);
		if (rc) {
			rc = -errno;
			fill_error(mg, -rc, "Unable to remove region %s",
				   mg->uuid);
		}
	}

	return rc;
}

static void down_members(struct mountgroup *mg, int member_count,
			 int *nodeids)
{
	int found, rc, i;
	struct list_head *p, *t;
	struct mg_member *memb;

	list_for_each_safe(p, t, &mg->members) {
		memb = list_entry(p, struct mg_member, list);
		found = 0;
		for (i = 0; i < member_count; i++) {
			if (memb->nodeid == nodeids[i]) {
				found = 1;
				break;
			}
		}

		if (found)
			continue;

		if (mg->start_type == GROUP_NODE_JOIN) {
			log_error("down_members: Somehow we got a member gone (%d) during a JOIN!",
				  memb->nodeid);
			/* Continue anyway, it's gone */
		}

		rc = drop_member(mg, memb);
		/*
		 * I don't think we care that drop_member failed, even
		 * during join.  Yes, we probably don't like that ocfs2
		 * won't get a notification if drop_member() failed, but
		 * there's nothing we can do.  Let's just ignore rc.
		 */
	}
}

static void up_members(struct mountgroup *mg, int member_count,
		       int *nodeids)
{
	int i, rc;

	for (i = 0; i < member_count; i++) {
		if (is_member(mg, nodeids[i]))
			continue;

		if (mg->start_type == GROUP_NODE_LEAVE) {
			log_error("up_members: Somehow we got at member added (%d) during a LEAVE!",
				  nodeids[i]);
			/* Continue anyway */
		}

		if ((nodeids[i] == our_nodeid) &&
		    (!mg_statep(mg, MG_JOIN_START,
				MG_MEMBER | MG_LEAVE_START)))
			log_error("up_members: we got ourselves up in a join event we didn't expect! Group is %s ",
				  mg->uuid);

		rc = add_member(mg, nodeids[i]);
		if (rc && mg_joining(mg)) {
			fill_error(mg, -rc, "Unable to join group %s",
				   mg->uuid);
			mg->group_leave_on_finish = 1;
		}
	}
}

void do_stop(struct mountgroup *mg)
{
	/*
	 * As far as I can tell, we don't have to do anything here.  Later
	 * we might want to freeze ocfs2, but currently it handles its own
	 * thang.
	 */
	log_group(mg, "do_stop() called");

	group_stop_done(gh, mg->uuid);
}

/*
 * The ocfs2 membership scheme makes this pretty simple.  We can just 
 * go ahead and modify the group in o2cb.  After all members have called
 * start_done, the mounting node's do_finish() can notify mount.ocfs2.
 */
void do_start(struct mountgroup *mg, int type, int member_count,
	      int *nodeids)
{
	if (mg_statep(mg, MG_JOIN_SENT, MG_MEMBER | MG_LEAVE_SENT))
		mg->state = MG_JOIN_START;
	else if (mg_statep(mg, MG_LEAVE_SENT, MG_MEMBER | MG_JOIN_SENT))
		mg->state = MG_LEAVE_START;

	mg->start_event_nr = mg->last_start;
	mg->start_type = type;

	log_group(mg,
		  "start %d state %d type %d member_count %d",
		  mg->last_start, mg->state, type,
		  member_count);

	if (mg_joining(mg)) {
		if (initialize_region(mg))
			goto out;
	}

	down_members(mg, member_count, nodeids);
	up_members(mg, member_count, nodeids);

out:
	group_start_done(gh, mg->uuid, mg->start_event_nr);
	if (mg_statep(mg, MG_JOIN_START, MG_MEMBER | MG_LEAVE_START))
		mg->state = MG_JOIN_START_DONE;
	else if (mg_statep(mg, MG_LEAVE_START, MG_MEMBER | MG_JOIN_START))
		mg->state = MG_LEAVE_START_DONE;
}

void do_finish(struct mountgroup *mg)
{
	log_group(mg, "finish called");

	if (mg_statep(mg, MG_JOIN_START_DONE, MG_MEMBER)) {
		mg->state = MG_JOINED;
		if (!mg->error) {
			assert(!mg->group_leave_on_finish);
			notify_mount_client(mg);
		} else {
			/*
			 * We had a problem joining the group.  We're going
			 * to leave it, and we don't want to notify
			 * mount.ocfs2 until we've done that.  It will
			 * happen in do_terminate().
			 */
			assert(mg->group_leave_on_finish);
		}
	}

	/*
	 * We do this when we determine it's time to leave but we're
	 * processing another node's join/leave events.  Here we know they
	 * are done, so we can call leave.
	 *
	 * We trust that if a node is both (MG_MEMBER && ->leave_on_finish),
	 * the notify_mount_client() above will have sent mount.ocfs2 an
	 * error.
	 */
	if (mg_statep(mg, MG_MEMBER, MG_MEMBER) &&
	    mg->group_leave_on_finish)  {
		log_group(mg, "leaving group after delay for join to finish");
		if (group_leave(gh, mg->uuid))
			log_error("group_leave(%s) failed: %s",
				  mg->uuid, strerror(errno));
		else
			mg->state = MG_LEAVE_SENT;
		mg->group_leave_on_finish = 0;
	}
}

void do_terminate(struct mountgroup *mg)
{
	log_group(mg, "termination of our unmount leave");

	if (!mg_statep(mg, MG_LEAVE_START_DONE, MG_LEAVE_START_DONE))
		log_error("terminate called from state %d for group %s",
			  mg->state, mg->uuid);

	mg->state = MG_DEAD;

	if (mg->mount_client)
		notify_mount_client(mg);

	/*
	 * A successful mount means that do_unmount() must have cleared
	 * the mountpoint.  A failed mount means that do_mount_result()
	 * cleared the mountpoint.  Either way, the list of mountpoints
	 * had better be empty by the time we've left the group.
	 */
	assert(list_empty(&mg->mountpoints));

	/*
	 * Drop all members from our local region, as we don't care about
	 * them anymore.
	 */
	down_members(mg, 0, NULL);
	assert(list_empty(&mg->members));

	if (drop_region(mg))
		log_error("Error removing region %s", mg->uuid);

	list_del(&mg->list);
	free(mg);
}
