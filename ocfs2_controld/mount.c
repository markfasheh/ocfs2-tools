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
#include <inttypes.h>
#include <errno.h>
#include <syslog.h>
#include <time.h>

#include "ocfs2-kernel/kernel-list.h"
#include "ocfs2-kernel/sparse_endian_types.h"
#include "ocfs2-kernel/ocfs2_fs.h"
#include "o2cb/o2cb_client_proto.h"

#include "ocfs2_controld.h"


struct mountpoint {
	struct list_head	mp_list;
	char			mp_mountpoint[PATH_MAX + 1];
};

struct mountgroup {
	struct list_head	mg_list;
	struct cgroup		*mg_group;
	int			mg_leave_on_join;

	char			mg_uuid[OCFS2_VOL_UUID_LEN + 1];
	char			mg_device[PATH_MAX + 1];

	struct list_head	mg_mountpoints;
	struct mountpoint	*mg_mp_in_progress;

	/* Communication with mount/umount.ocfs2 */
	int			mg_mount_ci;
	int			mg_mount_fd;
	int			mg_mount_notified;

	/* Interaction with cpg.c */
	struct cgroup		*mg_cg;

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

	if (strlen(uuid) > OCFS2_VOL_UUID_LEN) {
		log_error("uuid too long!");
		goto out;
	}

	mg = malloc(sizeof(struct mountgroup));
	if (!mg)
		goto out;

	memset(mg, 0, sizeof(struct mountgroup));
	INIT_LIST_HEAD(&mg->mg_mountpoints);
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

static struct mountpoint *find_mountpoint(struct mountgroup *mg,
					  const char *mountpoint)
{
	struct list_head *p;
	struct mountpoint *mp;

	list_for_each(p, &mg->mg_mountpoints) {
		mp = list_entry(p, struct mountpoint, mp_list);
		if ((strlen(mp->mp_mountpoint) == strlen(mountpoint)) &&
		    !strcmp(mp->mp_mountpoint, mountpoint))
			return mp;
	}

	return NULL;
}

static void remove_mountpoint(struct mountgroup *mg,
			      const char *mountpoint)
{
	struct mountpoint *mp;

	mp = find_mountpoint(mg, mountpoint);

	if (!mp) {
		log_error("mountpoint \"%s\" not found for mountgroup \"%s\"",
			  mountpoint, mg->mg_uuid);
		return;
	}

	list_del(&mp->mp_list);

	/*
	 * We must clear the list here so that dead_mounter()
	 * knows we're in the middle of a LEAVE.
	 */
	INIT_LIST_HEAD(&mp->mp_list);

	if (list_empty(&mg->mg_mountpoints)) {
		/* Set in-progress for leave */
		mg->mg_mp_in_progress = mp;

		log_debug("time to leave group %s", mg->mg_uuid);
		if (mg->mg_group) {
			log_debug("calling LEAVE for group %s",
				  mg->mg_uuid);
			/* XXX leave the group */
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
		free(mp);
}

void hack_leave(char *uuid)
{
	struct mountgroup *mg;
	struct mountpoint *mp;

	mg = find_mg_by_uuid(uuid);
	if (!mg) {
		log_error("Unable to find mg for \"%s\"", uuid);
		return;
	}

	if (!list_empty(&mg->mg_mountpoints))
		return;

	mp = mg->mg_mp_in_progress;
	if (!mp) {
		log_error("No mp in progress for \"%s\"", uuid);
		return;
	}

	if (!mg->mg_leave_on_join) {
		log_error("leave_on_join not set on \"%s\"", uuid);
		return;
	}

	log_debug("leaving group %s", uuid);
	free(mp);
	list_del(&mg->mg_list);
	free(mg);
}

static void add_mountpoint(struct mountgroup *mg, const char *device,
			   const char *mountpoint, int ci, int fd)
{
	struct mountpoint *mp;

	log_debug("Adding mountpoint %s to device %s uuid %s",
		  mountpoint, device, mg->mg_uuid);

	if (strcmp(mg->mg_device, device)) {
		fill_error(mg, EINVAL,
			   "Trying to mount fs %s on device %s, but it is already mounted from device %s",
			   mg->mg_uuid, device, mg->mg_device);
		return;
	}

	if (find_mountpoint(mg, mountpoint)) {
		fill_error(mg, EBUSY,
			   "Filesystem %s is already mounted on %s",
			   mg->mg_uuid, mountpoint);
		return;
	}

	if (mg->mg_mp_in_progress) {
		fill_error(mg, EBUSY, "Another mount is in progress");
		return;
	}

	if ((mg->mg_mount_ci != -1) ||
	    (mg->mg_mount_fd != -1)) {
		log_error("adding a mountpoint, but ci/fd are set: %d %d",
			  mg->mg_mount_ci, mg->mg_mount_fd);
	}

	mp = malloc(sizeof(struct mountpoint));
	if (!mp) {
		fill_error(mg, ENOMEM,
			   "Unable to allocate mountpoint structure");
		return;
	}

	memset(mp, 0, sizeof(struct mountpoint));
	strncpy(mp->mp_mountpoint, mountpoint, sizeof(mp->mp_mountpoint));
	mg->mg_mount_ci = ci;
	mg->mg_mount_fd = fd;
	mg->mg_mp_in_progress = mp;

	/*
	 * This special error is returned to mount.ocfs2 when the filesystem
	 * is already mounted elsewhere.  The group is already joined, and
	 * no additional work is required from ocfs2_controld.  When
	 * mount.ocfs2 sees this error, it will just clal mount(2).
	 */
	if (!list_empty(&mg->mg_mountpoints))
		fill_error(mg, EALREADY, "Already mounted, go ahead");

	list_add(&mp->mp_list, &mg->mg_mountpoints);
}

static void finish_join(struct mountgroup *mg, struct cgroup *cg)
{
	struct mountpoint *mp;

	if (mg->mg_cg) {
		log_error("cgroup passed, but one already exists! (mg %s, existing %p, new %p)",
			  mg->mg_uuid, mg->mg_cg, cg);
		return;
	}

	mp = mg->mg_mp_in_progress;
	if (!mp) {
		log_error("No mountpoint in progress for mountgroup %s",
			  mg->mg_uuid);
		return;
	}

	if (list_empty(&mp->mp_list)) {
		if (mg->mg_leave_on_join) {
			/* XXX Start leave */
		} else {
			log_error("mountgroup %s is in the process of leaving, not joining",
				  mg->mg_uuid);
		}
		return;
	}

	if (list_empty(&mg->mg_mountpoints)) {
		log_error("No mountpoints on mountgroup %s", mg->mg_uuid);
		return;
	}

	/* Ok, we've successfully joined the group */
	mg->mg_cg = cg;
	notify_mount_client(mg);
}

static void finish_leave(struct mountgroup *mg)
{
	if (list_empty(&mg->mg_mountpoints) &&
	    mg->mg_mp_in_progress) {
		/* We're done */
		notify_mount_client(mg);

		/* This is possible due to leave_on_join */
		if (!mg->mg_cg)
			log_debug("mg_cg was NULL");

		free(mg->mg_mp_in_progress);
		list_del(&mg->mg_list);
		free(mg);
		return;
	}

	/* This leave is unexpected */

	log_error("Unexpected leave of group %s", mg->mg_uuid);
	if (!mg->mg_cg)
		log_error("No mg_cg for group %s", mg->mg_uuid);

	/* XXX Do dire things */
}

/*
 * This is called when we join or leave a group.  There are three possible
 * states.
 *
 * 1) We've asked to join a group for a new filesystem.
 *    - mg_mp_in_progress != NULL
 *    - length(mg_mountpoints) == 1
 *    - mg_cg == NULL
 *
 *    cg will be our now-joined group.
 *
 * 2) We've asked to leave a group upon the last unmount of a filesystem.
 *   - mg_mp_in_progress != NULL
 *   - mg_mountpoints is empty
 *   - mg_cg is only NULL if we had to set leave_on_join.
 *
 *   cg is NULL.  We should complete our leave.
 *
 * 3) We've dropped out of the group unexpectedly.
 *   - mg_mountpoints is not empty.
 *   - mg_cg != NULL
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

static void mount_node_down(int nodeid, void *user_data)
{
	struct mountgroup *mg = user_data;

	log_debug("Node %d has left mountgroup %s", nodeid, mg->mg_uuid);

	/* XXX Write to sysfs */
}

int start_mount(int ci, int fd, const char *uuid, const char *device,
		const char *mountpoint)
{
	int rc = 0;
	struct mountgroup mg_error = { /* Until we have a real mg */
		.mg_error	= 0,
	};
	struct mountgroup *mg = &mg_error;

	log_debug("start_mount: uuid \"%s\", device \"%s\", mountpoint \"%s\"",
		  uuid, device, mountpoint);

	if (strlen(uuid) > OCFS2_VOL_UUID_LEN) {
		fill_error(mg, ENAMETOOLONG, "UUID too long: %s", uuid);
		goto out;
	}

	mg = find_mg_by_uuid(uuid);
	if (mg) {
		add_mountpoint(mg, device, mountpoint, ci, fd);
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

	add_mountpoint(mg, device, mountpoint, ci, fd);
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
		 * Because we never started a join, mg->mg_cg is NULL.
		 * remove_mountpoint() will set up for leave_on_join, but
		 * that actually never happens.  Thus, it is safe to
		 * clear mp_in_progress.
		 */
		remove_mountpoint(mg, mountpoint);
		if (mg->mg_mp_in_progress) {
			free(mg->mg_mp_in_progress);
			mg->mg_mp_in_progress = NULL;
		} else
			log_error("First mount of %s failed a join, yet mp_in_progress was NULL", mg->mg_uuid);
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

		if (mg->mg_error == EALREADY)
			mg->mg_mount_notified = 1;
		else {
			log_error("mount: %s", mg->mg_error_msg);

			if ((mg != &mg_error) &&
			    list_empty(&mg->mg_mountpoints)) {
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
		   const char *mountpoint)
{
	int rc = 0;
	int reply = 1;
	struct mountgroup mg_error = { /* Until we have a real mg */
		.mg_error	= 0,
	};
	struct mountgroup *mg;
	struct mountpoint *mp;
	long err;
	char *ptr = NULL;

	log_debug("complete_mount: uuid \"%s\", errcode \"%s\", mountpoint \"%s\"",
		  uuid, errcode, mountpoint);

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

	if (strlen(uuid) > OCFS2_VOL_UUID_LEN) {
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

	if (!mg->mg_mp_in_progress) {
		fill_error(mg, ENOENT,
			   "No mount in progress for filesystem %s",
			   mg->mg_uuid);
		goto out;
	}

	mp = find_mountpoint(mg, mountpoint);
	if (!mp) {
		fill_error(mg, ENOENT,
			   "Unknown mountpoint %s for filesystem %s",
			   mountpoint, mg->mg_uuid);
		goto out;
	}

	if (mp != mg->mg_mp_in_progress) {
		fill_error(mg, EINVAL, "Mountpoint %s is not in progress",
			   mountpoint);
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
	 * there was an error, remove_mountpoint may reset the in-progress
	 * pointer.
	 */
	mg->mg_mp_in_progress = NULL;

	if (!err) {
		mg->mg_mount_fd = -1;
		mg->mg_mount_ci = -1;
	} else {
		/*
		 * remove_mountpoint() will kick off a leave if this was
		 * the last mountpoint.  As part of the leave, it will add
		 * reset mp_in_progress.
		 */
		remove_mountpoint(mg, mountpoint);

		/*
		 * We don't pass err onto mg->mg_error because it came
		 * from mount.ocfs2.  We actually respond with 0, as we
		 * successfully processed the MRESULT.  Unless
		 * remove_mountpoint() set mg_error.
		 */
	}

	if (mg->mg_mp_in_progress)
		reply = 0;

out:
	if (reply)
		send_message(fd, CM_STATUS, mg->mg_error,
			     mg->mg_error ? mg->mg_error_msg : "OK");

	return rc;
}

int remove_mount(int ci, int fd, const char *uuid, const char *mountpoint)
{
	int rc = 0;
	int reply = 1;
	struct mountgroup mg_error = {
		.mg_error	= 0,
	};
	struct mountgroup *mg = NULL;
	struct mountpoint *mp;

	log_debug("remove_mount: uuid \"%s\", mountpoint \"%s\"",
		  uuid, mountpoint);

	if (strlen(uuid) > OCFS2_VOL_UUID_LEN) {
		fill_error(&mg_error, ENAMETOOLONG, "UUID too long: %s",
			   uuid);
		goto out;
	}

	mg = find_mg_by_uuid(uuid);
	if (!mg) {
		fill_error(&mg_error, ENOENT, "Unknown filesystem %s",
			   uuid);
		goto out;
	}

	/* find_mg() should fail if the uuid isn't mounted *somewhere* */
	if (list_empty(&mg->mg_mountpoints))
		log_error("Mountpoint list is empty!");

	mp = find_mountpoint(mg, mountpoint);
	if (!mp) {
		fill_error(&mg_error, ENOENT,
			   "Filesystem %s is not mounted on %s", uuid,
			   mountpoint);
		goto out;
	}

	if (mg->mg_mp_in_progress) {
		fill_error(&mg_error, EBUSY,
			   "Another mount is in progress");
		goto out;;
	}

	if ((mg->mg_mount_ci != -1) ||
	    (mg->mg_mount_fd != -1)) {
		log_error("removing a mountpoint, but ci/fd are set: %d %d",
			  mg->mg_mount_ci, mg->mg_mount_fd);
	}

	remove_mountpoint(mg, mountpoint);
	if (mg->mg_mp_in_progress) {
		/*
		 * remove_mountpoint() kicked off a LEAVE.  It needs the
		 * umount.ocfs2 client connection information.  It will
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
	struct mountpoint *mp;

	/* If there's no matching mountgroup, nothing to do. */
	mg = find_mg_by_client(ci);
	if (!mg)
		return;

	mp = mg->mg_mp_in_progress;

	/* If we have nothing in progress, nothing to do. */
	if (!mp)
		return;

	mg->mg_mount_ci = -1;
	mg->mg_mount_fd = -1;

	/*
	 * If mp_list is empty, the daemon is in the process
	 * of leaving the group.  We need that to complete whether we
	 * have a client or not.
	 */
	if (list_empty(&mp->mp_list))
		return;

	/*
	 * We haven't notified the client yet.  Thus, the client can't have
	 * called mount(2).  Let's just abort this mountpoint.  If this was
	 * the last mountpoint, we'll plan to leave the group.
	 */
	if (!mg->mg_mount_notified)
		remove_mountpoint(mg, mp->mp_mountpoint);

	/*
	 * XXX
	 *
	 * This is the hard one.  If we've notified the client, we're
	 * expecting the client to call mount(2).  But the client died.
	 * We don't know if that happened, so we can't leave the group.
	 *
	 * That's not totally true, btw.  Eventually we'll be opening the
	 * sysfs file.  Once we have that info, we'll do better here.
	 */
}

void init_mounts(void)
{
	INIT_LIST_HEAD(&mounts);
}

