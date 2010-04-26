/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
 *  This copyrighted material is made available to anyone wishing to use,
 *  modify, copy, or redistribute it subject to the terms and conditions
 *  of the GNU General Public License v.2.
 */


#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <unistd.h>
#include <inttypes.h>
#include <errno.h>
#include <syslog.h>
#include <time.h>

#include "ocfs2-kernel/kernel-list.h"
#include "ocfs2-kernel/sparse_endian_types.h"
#include "ocfs2-kernel/ocfs2_fs.h"
#include "o2cb/o2cb.h"
#include "o2cb/o2cb_client_proto.h"

#include "ocfs2_controld.h"

/* OCFS2_VOL_UUID_LEN is in bytes.  The hex string representation uses
 * two characters per byte */
#define OCFS2_UUID_STR_LEN	(OCFS2_VOL_UUID_LEN * 2)

struct service {
	struct list_head	ms_list;
	char			ms_service[PATH_MAX + 1];
	int			ms_additional;	/* This is a second mount */
};

struct mountgroup {
	struct list_head	mg_list;
	struct cgroup		*mg_group;
	int			mg_leave_on_join;
	int			mg_registered;

	char			mg_uuid[OCFS2_UUID_STR_LEN + 1];
	char			mg_device[PATH_MAX + 1];

	struct list_head	mg_services;
	struct service	*mg_ms_in_progress;

	/* Communication with mount.ocfs2 */
	int			mg_mount_ci;
	int			mg_mount_fd;
	int			mg_mount_notified;

	int			mg_error;
	char			mg_error_msg[128];
};


static struct list_head mounts;

static void fill_error(struct mountgroup *mg, int error, char *errfmt, ...)
{
	int rc;
	va_list args;

	/* Don't overwrite an error */
	if (mg->mg_error)
		return;

	mg->mg_error = error;
	va_start(args, errfmt);
	rc = vsnprintf(mg->mg_error_msg, sizeof(mg->mg_error_msg),
		       errfmt, args);
	va_end(args);

	if (rc >= sizeof(mg->mg_error_msg)) {
		log_debug("Error message truncated");
		mg->mg_error_msg[sizeof(mg->mg_error_msg) - 1] = '\0';
	}
}

int have_mounts(void)
{
	return !list_empty(&mounts);
}

static struct mountgroup *find_mg_by_uuid(const char *uuid)
{
	struct list_head *p;
	struct mountgroup *mg;

	list_for_each(p, &mounts) {
		mg = list_entry(p, struct mountgroup, mg_list);
		if ((strlen(mg->mg_uuid) == strlen(uuid)) &&
		    !strncmp(mg->mg_uuid, uuid, strlen(uuid)))
			return mg;
	}

	return NULL;
}

static struct mountgroup *find_mg_by_client(int ci)
{
	struct list_head *p;
	struct mountgroup *mg;

	if (ci < 0)
		return NULL;

	list_for_each(p, &mounts) {
		mg = list_entry(p, struct mountgroup, mg_list);
		if (mg->mg_mount_ci == ci)
			return mg;
	}

	return NULL;
}

static struct mountgroup *create_mg(const char *uuid, const char *device)
{
	struct mountgroup *mg = NULL;

	if (strlen(uuid) > OCFS2_UUID_STR_LEN) {
		log_error("uuid too long!");
		goto out;
	}

	mg = malloc(sizeof(struct mountgroup));
	if (!mg)
		goto out;

	memset(mg, 0, sizeof(struct mountgroup));
	INIT_LIST_HEAD(&mg->mg_services);
	mg->mg_mount_ci = -1;
	mg->mg_mount_fd = -1;
	strncpy(mg->mg_uuid, uuid, sizeof(mg->mg_uuid));
	strncpy(mg->mg_device, device, sizeof(mg->mg_device));
	list_add(&mg->mg_list, &mounts);

out:
	return mg;
}

static void notify_mount_client(struct mountgroup *mg)
{
	int error = mg->mg_error;
	char *error_msg = "OK";

	if (error) {
		if (mg->mg_error_msg[0])
			error_msg = mg->mg_error_msg;
		else
			error_msg = strerror(error);
		mg->mg_error = 0;
	}

	log_debug("notify_mount_client sending %d \"%s\"", error,
		  error_msg);

	if (mg->mg_mount_fd < 0) {
		log_debug("not sending - client went away");
		return;
	}

	error = send_message(mg->mg_mount_fd, CM_STATUS, error, error_msg);
	if (error)
		log_error("Unable to notify client, send_message failed with %d: %s",
			  -error, strerror(-error));
	else
		mg->mg_mount_notified = 1;

	/*
	 * XXX If we failed to notify the client, what can we do?  I'm
	 * guessing that our main loop will get POLLHUP and we'll clean
	 * up.
	 */
}

static struct service *find_service(struct mountgroup *mg,
					  const char *service)
{
	struct list_head *p;
	struct service *ms;

	list_for_each(p, &mg->mg_services) {
		ms = list_entry(p, struct service, ms_list);
		if ((strlen(ms->ms_service) == strlen(service)) &&
		    !strcmp(ms->ms_service, service))
			return ms;
	}

	return NULL;
}

static void remove_service(struct mountgroup *mg, const char *service)
{
	int rc;
	struct service *ms;

	ms = find_service(mg, service);
	if (!ms) {
		log_error("service \"%s\" not found for mountgroup \"%s\"",
			  service, mg->mg_uuid);
		return;
	}

	list_del(&ms->ms_list);

	/*
	 * We must clear the list here so that dead_mounter()
	 * knows we're in the middle of a LEAVE.
	 */
	INIT_LIST_HEAD(&ms->ms_list);

	if (list_empty(&mg->mg_services)) {
		/* Set in-progress for leave */
		mg->mg_ms_in_progress = ms;

		if (mg->mg_registered) {
			log_debug("Unregistering mountgroup %s",
				  mg->mg_uuid);
			rc = dlmcontrol_unregister(mg->mg_uuid);
			if (rc)
				log_error("Unable to deregister mountgroup "
					  "%s: %s",
					  mg->mg_uuid, strerror(-rc));
			mg->mg_registered = 0;
		}

		log_debug("time to leave group %s", mg->mg_uuid);
		if (mg->mg_group) {
			log_debug("calling LEAVE for group %s",
				  mg->mg_uuid);
			if (group_leave(mg->mg_group)) {
				log_error("Unable to leave group %s",
					  mg->mg_uuid);
				/* XXX what to do?  finalize?  Shutdown? */
			}
		} else {
			/*
			 * Join is in progress, let's leave when we get
			 * there.
			 */
			log_debug("Not joined %s, so set leave_on_join",
				  mg->mg_uuid);
			mg->mg_leave_on_join = 1;
		}
	} else
		free(ms);
}

static void add_service(struct mountgroup *mg, const char *device,
			   const char *service, int ci, int fd)
{
	struct service *ms;
	struct stat st1, st2;

	log_debug("Adding service \"%s\" to device \"%s\" uuid \"%s\"",
		  service, device, mg->mg_uuid);

	if (stat(mg->mg_device, &st1)) {
		fill_error(mg, errno, "Failed to stat device \"%s\": %s",
			   mg->mg_device, strerror(errno));
		return;
	}

	if (stat(device, &st2)) {
		fill_error(mg, errno, "Failed to stat device \"%s\": %s",
			   device, strerror(errno));
		return;
	}

	if (st1.st_rdev != st2.st_rdev) {
		fill_error(mg, EINVAL,
			   "Trying to mount fs \"%s\" on device \"%s\", "
			   "but it is already mounted from device \"%s\"",
			   mg->mg_uuid, device, mg->mg_device);
		return;
	}

	if (mg->mg_ms_in_progress) {
		fill_error(mg, EBUSY, "Another mount is in progress");
		return;
	}

	ms = find_service(mg, service);
	if (ms) {
		/*
		 * Real mounts use the OCFS2_FS_NAME service.  There can
		 * be more than one at a time.  All other services may
		 * only have one instance.
		 */
		if (strcmp(service, OCFS2_FS_NAME)) {
			fill_error(mg, EBUSY,
				   "Filesystem %s is already mounted on %s",
				   mg->mg_uuid, service);
			return;
		}

		/*
		 * There can be more than one real mount.  However, if an
		 * additional mount fails in sys_mount(2), we can't have
		 * complete_mount() removing the service.  We only want that
		 * to happen when it's the first mount.
		 */
		log_debug("Additional mount of %s starting for %s",
			  mg->mg_uuid, service);
		ms->ms_additional = 1;
	} else {
		ms = malloc(sizeof(struct service));
		if (!ms) {
			fill_error(mg, ENOMEM,
				   "Unable to allocate service structure");
			return;
		}
		memset(ms, 0, sizeof(struct service));
		strncpy(ms->ms_service, service, sizeof(ms->ms_service));

		/* Make sure a new service has the empty list */
		INIT_LIST_HEAD(&ms->ms_list);
	}

	if ((mg->mg_mount_ci != -1) ||
	    (mg->mg_mount_fd != -1)) {
		log_error("adding a service, but ci/fd are set: %d %d",
			  mg->mg_mount_ci, mg->mg_mount_fd);
	}
	mg->mg_mount_ci = ci;
	mg->mg_mount_fd = fd;
	mg->mg_ms_in_progress = ms;

	/*
	 * This special error is returned to mount.ocfs2 when the filesystem
	 * is already mounted elsewhere.  The group is already joined, and
	 * no additional work is required from ocfs2_controld.  When
	 * mount.ocfs2 sees this error, it will just clal mount(2).
	 */
	if (!list_empty(&mg->mg_services))
		fill_error(mg, EALREADY, "Already mounted, go ahead");

	/* new service */
	if (list_empty(&ms->ms_list))
		list_add(&ms->ms_list, &mg->mg_services);
}

static void register_result(int status, void *user_data)
{
	struct mountgroup *mg = user_data;
	struct service *ms;

	if (!mg->mg_group) {
		log_error("No cgroup (mg %s)", mg->mg_uuid);
		return;
	}

	ms = mg->mg_ms_in_progress;
	if (!ms) {
		log_error("No service in progress for mountgroup %s",
			  mg->mg_uuid);
		return;
	}

	if (status) {
		fill_error(mg, -status,
			   "Error registering mg %s with dlm_controld: %s",
			   mg->mg_uuid, strerror(-status));

		/* remove_service() will kick off a LEAVE if needed */
		remove_service(mg, ms->ms_service);
		return;
	}

	log_debug("Mountgroup %s successfully registered with dlm_controld",
		  mg->mg_uuid);
	mg->mg_registered = 1;
	notify_mount_client(mg);
}

static void finish_join(struct mountgroup *mg, struct cgroup *cg)
{
	int rc;
	struct service *ms;

	if (mg->mg_group) {
		log_error("cgroup passed, but one already exists! (mg %s, existing %p, new %p)",
			  mg->mg_uuid, mg->mg_group, cg);
		return;
	}

	ms = mg->mg_ms_in_progress;
	if (!ms) {
		log_error("No service in progress for mountgroup %s",
			  mg->mg_uuid);
		return;
	}

	if (list_empty(&ms->ms_list)) {
		if (mg->mg_leave_on_join) {
			if (group_leave(cg)) {
				log_error("Unable to leave group %s",
					  mg->mg_uuid);
				/* XXX What to do? */
			}
		} else {
			log_error("mountgroup %s is in the process of leaving, not joining",
				  mg->mg_uuid);
		}
		return;
	}

	if (list_empty(&mg->mg_services)) {
		log_error("No services on mountgroup %s", mg->mg_uuid);
		return;
	}

	/* Ok, we've successfully joined the group */
	mg->mg_group = cg;

	/* Now tell dlm_controld */
	log_debug("Registering mountgroup %s with dlm_controld",
		  mg->mg_uuid);
	rc = dlmcontrol_register(mg->mg_uuid, register_result, mg);
	if (rc) {
		fill_error(mg, -rc,
			   "Unable to register mountgroup %s with "
			   "dlm_controld: %s",
			   mg->mg_uuid, strerror(errno));
		/* remove_service() will kick off a LEAVE if needed */
		remove_service(mg, ms->ms_service);
	}
}

static void mount_node_down(int nodeid, void *user_data)
{
	struct mountgroup *mg = user_data;
	errcode_t err;

	log_debug("Node %d has left mountgroup %s", nodeid, mg->mg_uuid);

	err = o2cb_control_node_down(mg->mg_uuid, nodeid);
	if (err)
		log_debug("%s while trying to send DOWN message",
			  error_message(err));

	dlmcontrol_node_down(mg->mg_uuid, nodeid);
}

static void finish_leave(struct mountgroup *mg)
{
	struct list_head *p, *n;
	struct service *ms;
	struct timespec ts;

	if (list_empty(&mg->mg_services) &&
	    mg->mg_ms_in_progress) {
		/* We're done */
		notify_mount_client(mg);

		/* This is possible due to leave_on_join */
		if (!mg->mg_group)
			log_debug("mg_group was NULL");

		free(mg->mg_ms_in_progress);
		goto out;
	}

	/*
	 * This leave is unexpected.  If we weren't part of the group, we
	 * just cleanup our state.  However, if we were part of a group, we
	 * cannot safely continue and must die.  Fail-fast allows other
	 * nodes to make a decision about us.
	 */
	log_error("Unexpected leave of group %s", mg->mg_uuid);


	if (mg->mg_group) {
		log_error("Group %s is live, exiting", mg->mg_uuid);

		/*
		 * The _exit(2) may cause a reboot, and we want the errors
		 * to hit syslogd(8).  We can't call sync(2) which might
		 * sleep on an ocfs2 operation.  I'd say sleeping for 10ms
		 * is a good compromise.  Local syslogd(8) won't have time
		 * to write to disk, but a network syslogd(8) should get
		 * the data.
		 */
		ts.tv_sec = 0;
		ts.tv_nsec = 10000000;
		nanosleep(&ts, NULL);
		_exit(1);
	}

	log_error("No mg_group for group %s", mg->mg_uuid);

	list_for_each_safe(p, n, &mg->mg_services) {
		ms = list_entry(p, struct service, ms_list);
		list_del(&ms->ms_list);
		/* The in-progress ms may or may not be on the list */
		if (ms != mg->mg_ms_in_progress)
			free(ms);
	}
	/* So free the in-progress ms last */
	if (mg->mg_ms_in_progress)
		free(mg->mg_ms_in_progress);

	/* If we had a client attached, let it know we died */
	if (mg->mg_mount_ci != -1)
		connection_dead(mg->mg_mount_ci);

out:
	list_del(&mg->mg_list);
	free(mg);
}

/*
 * This is called when we join or leave a group.  There are three possible
 * states.
 *
 * 1) We've asked to join a group for a new filesystem.
 *    - mg_ms_in_progress != NULL
 *    - length(mg_services) == 1
 *    - mg_group == NULL
 *
 *    cg will be our now-joined group.
 *
 * 2) We've asked to leave a group upon the last unmount of a filesystem.
 *   - mg_ms_in_progress != NULL
 *   - mg_services is empty
 *   - mg_group is only NULL if we had to set leave_on_join.
 *
 *   cg is NULL.  We should complete our leave.
 *
 * 3) We've dropped out of the group unexpectedly.
 *   - mg_services is not empty.
 *   - mg_group != NULL
 *
 *   cg is NULL.  We should basically crash.  This usually is handled by
 *   closing our sysfs fd.
 */
static void mount_set_group(struct cgroup *cg, void *user_data)
{
	struct mountgroup *mg = user_data;

	if (cg)
		finish_join(mg, cg);
	else
		finish_leave(mg);
}

/*
 * THIS FUNCTION CAUSES PROBLEMS.
 *
 * bail_on_mounts() is called when we are forced to exit via a signal
 * or cluster stack dying on us.  As such, it tells ocfs2 that nodes
 * are down but not communicate with the stack or cpg.  This can cause
 * ocfs2 to self-fence or the stack to go nuts.  But hey, if you SIGKILL
 * the daemon, you get what you pay for.
 */
void bail_on_mounts(void)
{
	struct list_head *p, *n;
	struct mountgroup *mg;

	list_for_each_safe(p, n, &mounts) {
		mg = list_entry(p, struct mountgroup, mg_list);
		finish_leave(mg);
	}
}

int start_mount(int ci, int fd, const char *uuid, const char *device,
		const char *service)
{
	int rc = 0;
	struct mountgroup mg_error = { /* Until we have a real mg */
		.mg_error	= 0,
	};
	struct mountgroup *mg = &mg_error;

	log_debug("start_mount: uuid \"%s\", device \"%s\", service \"%s\"",
		  uuid, device, service);

	if (strlen(uuid) > OCFS2_UUID_STR_LEN) {
		fill_error(mg, ENAMETOOLONG, "UUID too long: %s", uuid);
		goto out;
	}

	mg = find_mg_by_uuid(uuid);
	if (mg) {
		add_service(mg, device, service, ci, fd);
		goto out;
	}

	/* Here we stop using &mg_error and start using the real one */
	mg = create_mg(uuid, device);
	if (!mg) {
		mg = &mg_error;  /* Well, almost did */
		fill_error(mg, ENOMEM,
			   "Unable to allocate mountgroup structure");
		goto out;
	}

	add_service(mg, device, service, ci, fd);
	if (mg->mg_error)
		goto out;

	/*
	 * Fire off a group join.  The cpg infrastructure will
	 * let us know when the group is joined, at which point we
	 * notify_mount_client().  If there's a failure, we notify as well.
	 */
	rc = group_join(mg->mg_uuid, mount_set_group, mount_node_down, mg);
	if (rc) {
		fill_error(mg, -rc, "Unable to start join to group %s",
			   mg->mg_uuid);

		/*
		 * Because we never started a join, mg->mg_group is NULL.
		 * remove_service() will set up for leave_on_join, but
		 * that actually never happens.  Thus, it is safe to
		 * clear ms_in_progress.
		 */
		remove_service(mg, service);
		if (mg->mg_ms_in_progress) {
			free(mg->mg_ms_in_progress);
			mg->mg_ms_in_progress = NULL;
		} else
			log_error("First mount of %s failed a join, yet ms_in_progress was NULL", mg->mg_uuid);
	}

out:
	/*
	 * Only reply on error.  If we're doing OK, the reply is delayed
	 * until join completes (notify_mount_client()).
	 *
	 * This reply includes -EALREADY, which tells the mount client that
	 * we're doing an additional mount - it can just go ahead.
	 */
	if (mg->mg_error) {
		rc = -mg->mg_error;
		send_message(fd, CM_STATUS, mg->mg_error, mg->mg_error_msg);
		mg->mg_error = 0;

		if (rc == -EALREADY)
			mg->mg_mount_notified = 1;
		else {
			log_error("mount: %s", mg->mg_error_msg);

			if ((mg != &mg_error) &&
			    list_empty(&mg->mg_services)) {
				log_debug("mount: freeing failed mountgroup");
				list_del(&mg->mg_list);
				free(mg);
			}
		}
	}

	log_debug("start_mount returns %d", rc);

	return rc;
}

int complete_mount(int ci, int fd, const char *uuid, const char *errcode,
		   const char *service)
{
	int rc = 0;
	int reply = 1;
	struct mountgroup mg_error = { /* Until we have a real mg */
		.mg_error	= 0,
	};
	struct mountgroup *mg;
	struct service *ms;
	long err;
	char *ptr = NULL;

	log_debug("complete_mount: uuid \"%s\", errcode \"%s\", service \"%s\"",
		  uuid, errcode, service);

	mg = find_mg_by_client(ci);
	if (!mg) {
		mg = &mg_error;
		fill_error(mg, EINVAL,
			   "Client is not attached to a mountgroup");
		goto out;
	}

	if (mg->mg_mount_fd != fd) {
		fill_error(mg, EINVAL,
			   "Client file descriptor does not match");
		goto out;
	}

	if (strlen(uuid) > OCFS2_UUID_STR_LEN) {
		fill_error(mg, EINVAL,
			   "UUID too long: %s", uuid);
		goto out;
	}

	if (strcmp(uuid, mg->mg_uuid)) {
		fill_error(mg, EINVAL,
			   "UUID %s does not match mountgroup %s", uuid,
			   mg->mg_uuid);
		goto out;
	}

	if (!mg->mg_ms_in_progress) {
		fill_error(mg, ENOENT,
			   "No mount in progress for filesystem %s",
			   mg->mg_uuid);
		goto out;
	}

	ms = find_service(mg, service);
	if (!ms) {
		fill_error(mg, ENOENT,
			   "Unknown service %s for filesystem %s",
			   service, mg->mg_uuid);
		goto out;
	}

	if (ms != mg->mg_ms_in_progress) {
		fill_error(mg, EINVAL, "Service %s is not in progress",
			   service);
		goto out;
	}

	err = strtol(errcode, &ptr, 10);
	if (ptr && *ptr != '\0') {
		fill_error(mg, EINVAL, "Invalid error code string: %s",
			   errcode);
		goto out;
	}
	if ((err == LONG_MIN) || (err == LONG_MAX) ||
	    (err < INT_MIN) || (err > INT_MAX)) {
		fill_error(mg, ERANGE, "Error code %ld is out of range",
			   err);
		goto out;
	}

	/*
	 * Clear the in-progress pointer and store off the reply fd.  If
	 * there was an error, remove_service may reset the in-progress
	 * pointer.
	 */
	mg->mg_ms_in_progress = NULL;

	if (ms->ms_additional) {
		/*
		 * Our additional real mount is done whether it succeeded
		 * or failed.  We only have to clear the additional state
		 * and reply OK.
		 */
		log_debug("Completed additional mount of filesystem %s, "
			  "error is %ld",
			  mg->mg_uuid, err);
		ms->ms_additional = 0;
		err = 0;
	}

	if (!err) {
		mg->mg_mount_fd = -1;
		mg->mg_mount_ci = -1;
	} else {
		/*
		 * remove_service() will kick off a leave if this was
		 * the last service.  As part of the leave, it will set
		 * ms_in_progress.
		 */
		remove_service(mg, service);

		/*
		 * We don't pass err onto mg->mg_error because it came
		 * from mount.ocfs2.  We actually respond with 0, as we
		 * successfully processed the MRESULT.  Unless
		 * remove_service() set mg_error.
		 */
	}

	if (mg->mg_ms_in_progress)
		reply = 0;

out:
	if (reply)
		send_message(fd, CM_STATUS, mg->mg_error,
			     mg->mg_error ? mg->mg_error_msg : "OK");

	return rc;
}

int remove_mount(int ci, int fd, const char *uuid, const char *service)
{
	int rc = 0;
	int reply = 1;
	struct mountgroup mg_error = {
		.mg_error	= 0,
	};
	struct mountgroup *mg = NULL;
	struct service *ms;

	log_debug("remove_mount: uuid \"%s\", service \"%s\"",
		  uuid, service);

	if (strlen(uuid) > OCFS2_UUID_STR_LEN) {
		fill_error(&mg_error, ENAMETOOLONG, "UUID too long: %s",
			   uuid);
		goto out;
	}

	mg = find_mg_by_uuid(uuid);
	if (!mg) {
		fill_error(&mg_error, ENOENT,
			   "Filesystem %s is unknown or not mounted anywhere",
			   uuid);
		goto out;
	}

	/* find_mg() should fail if the uuid isn't mounted *somewhere* */
	if (list_empty(&mg->mg_services))
		log_error("Service list is empty!");

	ms = find_service(mg, service);
	if (!ms) {
		fill_error(&mg_error, ENOENT,
			   "Service %s is not mounted on %s", uuid,
			   service);
		goto out;
	}

	if (mg->mg_ms_in_progress) {
		fill_error(&mg_error, EBUSY,
			   "Another mount is in progress");

		/*
		 * If the service we're removing has ms_additional set, it
		 * must be the filesystem service.  That means the
		 * in_progress service is an additional real mount, but
		 * the kernel is no longer mounted.
		 *
		 * As such, the in-progress service is now a new mount, and
		 * we clear the ms_addditional flag.  It will succeed or
		 * fail as a new mount.
		 */
		if (ms->ms_additional) {
			if (ms != mg->mg_ms_in_progress)
				log_error("Somehow ms_additional was set "
					  "even though the in-progress "
					  "mount isn't the filesystem "
					  "(group %s, removing %s, "
					  "in-progress %s",
					  mg->mg_uuid, ms->ms_service,
					  mg->mg_ms_in_progress->ms_service);
			ms->ms_additional = 0;
		}

		goto out;
	}

	if ((mg->mg_mount_ci != -1) ||
	    (mg->mg_mount_fd != -1)) {
		log_error("removing a service, but ci/fd are set: %d %d",
			  mg->mg_mount_ci, mg->mg_mount_fd);
	}

	remove_service(mg, service);
	if (mg->mg_ms_in_progress) {
		/*
		 * remove_service() kicked off a LEAVE.  It needs the
		 * client connection information.  It will
		 * handle replying via notify_mount_client().
		 */
		mg->mg_mount_ci = ci;
		mg->mg_mount_fd = fd;
		reply = 0;
	} else if (mg->mg_error) {
		fill_error(&mg_error, mg->mg_error, "%s", mg->mg_error_msg);
	}

out:
	if (reply)
		send_message(fd, CM_STATUS, mg_error.mg_error,
			     mg_error.mg_error ? mg_error.mg_error_msg : "OK");

	if (mg_error.mg_error)
		rc = -mg_error.mg_error;

	return rc;
}

void dead_mounter(int ci, int fd)
{
	struct mountgroup *mg;
	struct service *ms;

	/* If there's no matching mountgroup, nothing to do. */
	mg = find_mg_by_client(ci);
	if (!mg)
		return;

	ms = mg->mg_ms_in_progress;

	/* If we have nothing in progress, nothing to do. */
	if (!ms)
		return;

	log_error("Mounter for filesystem %s, service %s died", mg->mg_uuid,
		  ms->ms_service);
	mg->mg_mount_ci = -1;
	mg->mg_mount_fd = -1;

	/*
	 * If ms_list is empty, the daemon is in the process
	 * of leaving the group.  We need that to complete whether we
	 * have a client or not.
	 */
	if (list_empty(&ms->ms_list))
		return;

	/*
	 * If this was just an additional real mount, we just clear the
	 * state.
	 */
	if (ms->ms_additional) {
		log_debug("Additional mounter of filesystem %s died",
			  mg->mg_uuid);
		ms->ms_additional = 0;
		mg->mg_ms_in_progress = NULL;
		return;
	}

	/*
	 * We haven't notified the client yet.  Thus, the client can't have
	 * called mount(2).  Let's just abort this service.  If this was
	 * the last service, we'll plan to leave the group.
	 */
	if (!mg->mg_mount_notified) {
		remove_service(mg, ms->ms_service);
		return;
	}

	/*
	 * XXX
	 *
	 * This is the hard one.  If we've notified the client, we're
	 * expecting the client to call mount(2).  But the client died.
	 * We don't know if that happened, so we can't leave the group.
	 *
	 * We do know, though, that all the other in-progress operations
	 * (group join, dlmc_fs_register) must have completed, or we
	 * wouldn't have set mg_mount_notified.  Thus, we can treat it as
	 * a live mount.  Witness the power of a fully armed and
	 * operational mountgroup.
	 *
	 * We can clear the in-progress flag and allow other mounters.  If
	 * it really mounted, it can be unmounted.  If it didn't mount, the
	 * state can be torn down with ocfs2_hb_ctl.
	 *
	 * Maybe later we'll learn how to detect the mount via the kernel
	 * and tear it down ourselves.  But not right now.
	 */

	log_error("Kernel mount of filesystem %s already entered, "
		  "assuming it succeeded",
		  mg->mg_uuid);

	mg->mg_ms_in_progress = NULL;
}

int send_mountgroups(int ci, int fd)
{
	unsigned int count = 0;
	int rc = 0, rctmp;
	char error_msg[100];  /* Arbitrary size smaller than a message */
	struct list_head *p;
	struct mountgroup *mg;

	list_for_each(p, &mounts) {
		count++;
	}

	rc = send_message(fd, CM_ITEMCOUNT, count);
	if (rc) {
		snprintf(error_msg, sizeof(error_msg),
			 "Unable to send ITEMCOUNT: %s",
			 strerror(-rc));
		goto out_status;
	}

	list_for_each(p, &mounts) {
		mg = list_entry(p, struct mountgroup, mg_list);
		rc = send_message(fd, CM_ITEM, mg->mg_uuid);
		if (rc) {
			snprintf(error_msg, sizeof(error_msg),
				 "Unable to send ITEM: %s",
				 strerror(-rc));
			goto out_status;
		}
	}

	strcpy(error_msg, "OK");

out_status:
	log_debug("Sending status %d \"%s\"", -rc, error_msg);
	rctmp = send_message(fd, CM_STATUS, -rc, error_msg);
	if (rctmp) {
		log_error("Error sending STATUS message: %s",
			  strerror(-rc));
		if (!rc)
			rc = rctmp;
	}

	return rc;
}

void init_mounts(void)
{
	INIT_LIST_HEAD(&mounts);
}

