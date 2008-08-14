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
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <signal.h>
#include <syslog.h>
#include <sched.h>

#include "ocfs2-kernel/kernel-list.h"
#include "o2cb/o2cb.h"
#include "o2cb/o2cb_client_proto.h"

#include "ocfs2_controld.h"

#define OPTION_STRING		"DhVw"
#define LOCKFILE_NAME		"/var/run/ocfs2_controld.pid"
#define NALLOC			8

#define CONTROLD_PROTOCOL_MAJOR		1
#define CONTROLD_PROTOCOL_MINOR		0
#define DAEMON_MAX_PROTOCOL_SECTION	"daemon_max_protocol"
#define DAEMON_PROTOCOL_SECTION		"daemon_protocol"
#define FS_MAX_PROTOCOL_SECTION		"ocfs2_max_protocol"
#define FS_PROTOCOL_SECTION		"ocfs2_protocol"

struct client {
	int fd;
	char type[32];
	void (*work)(int ci);
	void (*dead)(int ci);
#if 0
	struct mountgroup *mg;
	int another_mount;
#endif
};

static int client_maxi;
static int client_size = 0;
static struct client *client = NULL;
static struct pollfd *pollfd = NULL;
static int time_to_die = 0;

static int sigpipe_write_fd;

char *prog_name;
int daemon_debug_opt;
char daemon_debug_buf[1024];
char dump_buf[DUMP_SIZE];
int dump_point;
int dump_wrap;

/*
 * Protocol negotiation.
 *
 * This is the maximum protocol supported by the daemon for inter-daemon
 * communication.  The negotiated value daemon_running_proto is what the
 * daemon uses at runtime.
 *
 * All daemons must support the initial protocol, which works as follows:
 * Prior to starting CPG, daemons store two values in the local node
 * checkpoint.  The maximum daemon protocol is stored in the
 * "daemon_max_protocol" section, and the ocfs2 maximum protocol is stored
 * in the "ocfs2_max_protocol" section.  The protocols are stored in the
 * format:
 *
 *     <2-char-hex-major><space><2-char-hex-minor><null>
 *
 * These sections MUST be created before CPG is started.  Other sections
 * MUST NOT be created at this time.
 *
 * Once CPG is started, the daemon reads the "daemon_protocol" and
 * "ocfs2_protocol" sections from the daemon's global checkpoint.  The
 * values are stored as the running versions.  All interaction takes place
 * based on the running versions.  At this point, the daemon may add
 * other sections to the local node checkpoint that are part of the
 * running protocol.
 *
 * If the daemon is the first node to join the group, it sets the
 * "daemon_protocol" and "ocfs2_protocol" sections of the global checkpoint
 * to the maximum values this daemon supports.
 */
static struct ocfs2_protocol_version daemon_max_proto = {
	.pv_major	= CONTROLD_PROTOCOL_MAJOR,
	.pv_minor	= CONTROLD_PROTOCOL_MINOR,
};
static struct ocfs2_protocol_version fs_max_proto;
struct ocfs2_protocol_version daemon_running_proto;
struct ocfs2_protocol_version fs_running_proto;
struct ckpt_handle *node_handle;

void shutdown_daemon(void)
{
	time_to_die = 1;
}

static void handler(int signum)
{
	log_debug("Caught signal %d", signum);
	if (write(sigpipe_write_fd, &signum, sizeof(signum)) < sizeof(signum))
		log_error("Problem writing signal: %s", strerror(-errno));
}

static void dead_sigpipe(int ci)
{
	log_error("Error on the signal pipe");
	connection_dead(ci);
	shutdown_daemon();
}

static void handle_signal(int ci)
{
	int rc, caught_sig, abortp = 0;
	static int segv_already = 0;

	rc = read(client[ci].fd, (char *)&caught_sig, sizeof(caught_sig));
	if (rc < 0) {
		rc = -errno;
		log_error("Error reading from signal pipe: %s",
			  strerror(-rc));
		goto out;
	}

	if (rc != sizeof(caught_sig)) {
		rc = -EIO;
		log_error("Error reading from signal pipe: %s",
			  strerror(-rc));
		goto out;
	}

	switch (caught_sig) {
		case SIGQUIT:
			abortp = 1;
			/* FALL THROUGH */

		case SIGTERM:
		case SIGINT:
		case SIGHUP:
			if (have_mounts()) {
				log_error("Caught signal %d, but mounts exist.  Ignoring.",
					  caught_sig);
				rc = 0;
			} else {
				log_error("Caught signal %d, exiting",
					  caught_sig);
				rc = 1;
			}
			break;

		case SIGSEGV:
			log_error("Segmentation fault, exiting");
			rc = 1;
			if (segv_already) {
				log_error("Segmentation fault loop detected");
				abortp = 1;
			} else
				segv_already = 1;
			break;

		default:
			log_error("Caught signal %d, ignoring", caught_sig);
			rc = 0;
			break;
	}

	if (rc && abortp)
		abort();

out:
	if (rc)
		shutdown_daemon();
}

static int setup_sigpipe(void)
{
	int rc;
	int signal_pipe[2];
	struct sigaction act;

	rc = pipe(signal_pipe);
	if (rc) {
		rc = -errno;
		log_error("Unable to set up signal pipe: %s",
			  strerror(-rc));
		goto out;
	}

	sigpipe_write_fd = signal_pipe[1];

	act.sa_sigaction = NULL;
	act.sa_restorer = NULL;
	sigemptyset(&act.sa_mask);
	act.sa_handler = handler;
#ifdef SA_INTERRUPT
	act.sa_flags = SA_INTERRUPT;
#endif

	rc += sigaction(SIGTERM, &act, NULL);
	rc += sigaction(SIGINT, &act, NULL);
	rc += sigaction(SIGHUP, &act, NULL);
	rc += sigaction(SIGQUIT, &act, NULL);
	rc += sigaction(SIGSEGV, &act, NULL);
	act.sa_handler = SIG_IGN;
	rc += sigaction(SIGPIPE, &act, NULL);  /* Get EPIPE instead */

	if (rc) {
		log_error("Unable to set up signal handlers");
		goto out;
	}

	rc = connection_add(signal_pipe[0], handle_signal, dead_sigpipe);
	if (rc < 0)
		log_error("Unable to add signal pipe: %s", strerror(-rc));

out:
	return rc;
}

static int do_mount(int ci, int fd, const char *fstype, const char *uuid,
		    const char *cluster, const char *device,
		    const char *service)
{
	char *error_msg;

	if (!fstype || strcmp(fstype, OCFS2_FS_NAME)) {
		error_msg = "Invalid filesystem type";
		goto fail;
	}

	if (!validate_cluster(cluster)) {
		error_msg = "Invalid cluster name";
		goto fail;
	}

	return start_mount(ci, fd, uuid, device, service);

fail:
	return send_message(fd, CM_STATUS, EINVAL, error_msg);
}

static int do_mount_result(int ci, int fd, const char *fstype,
			   const char *uuid, const char *errcode,
			   const char *service)
{
	if (!fstype || strcmp(fstype, OCFS2_FS_NAME))
		return send_message(fd, CM_STATUS, EINVAL,
				    "Invalid filesystem type");

	return complete_mount(ci, fd, uuid, errcode, service);
}

static int do_unmount(int ci, int fd, const char *fstype, const char *uuid,
		      const char *service)
{
	if (!fstype || strcmp(fstype, OCFS2_FS_NAME))
		return send_message(fd, CM_STATUS, EINVAL,
				    "Invalid filesystem type");

	return remove_mount(ci, fd, uuid, service);
}

void connection_dead(int ci)
{
	log_debug("client %d fd %d dead", ci, client[ci].fd);
	close(client[ci].fd);
	client[ci].work = NULL;
	client[ci].fd = -1;
	pollfd[ci].fd = -1;
#if 0
	client[ci].mg = NULL;
#endif
}


static int client_alloc(void)
{
	int i;
	struct client *new_client;
	struct pollfd *new_pollfd;

	if (!client) {
		new_client = malloc(NALLOC * sizeof(struct client));
		new_pollfd = malloc(NALLOC * sizeof(struct pollfd));
	} else {
		new_client = realloc(client, (client_size + NALLOC) *
					 sizeof(struct client));
		new_pollfd = realloc(pollfd, (client_size + NALLOC) *
					 sizeof(struct pollfd));
	}
	if (!new_client || !new_pollfd) {
		log_error("Can't allocate client memory.");
		return -ENOMEM;
	}
	client = new_client;
	pollfd = new_pollfd;

	for (i = client_size; i < (client_size + NALLOC); i++) {
		client[i].work = NULL;
		client[i].dead = NULL;
		client[i].fd = -1;
		pollfd[i].fd = -1;
		pollfd[i].revents = 0;
	}

	client_size += NALLOC;

	return 0;
}

int connection_add(int fd, void (*work)(int ci), void (*dead)(int ci))
{
	int i;

	if (!client) {
		i = client_alloc();
		if (i)
			return i;
	}

	while (1) {
		for (i = 0; i < client_size; i++) {
			if (client[i].fd == -1) {
				client[i].fd = fd;
				client[i].work = work;
				client[i].dead = dead ? dead : connection_dead;
				pollfd[i].fd = fd;
				pollfd[i].events = POLLIN;
				if (i > client_maxi)
					client_maxi = i;
				return i;
			}
		}

		i = client_alloc();
		if (i)
			return i;
	}

	return -ELOOP;
}

/* 4 characters for "ITEM", 1 for the space, 1 for the '\0' */
#define DEBUG_BYTES_PER_ITEM	(OCFS2_CONTROLD_MAXLINE - 6)
static size_t debug_bytes_to_count(size_t bytes)
{
	return (bytes + DEBUG_BYTES_PER_ITEM - 1) / DEBUG_BYTES_PER_ITEM;
}

static int send_debug(int fd, const char *ptr, size_t bytes)
{
	int rc = 0;
	size_t remain = bytes;
	size_t itemlen = DEBUG_BYTES_PER_ITEM;
	char itembuf[DEBUG_BYTES_PER_ITEM + 1];

	while (remain) {
		if (itemlen > remain)
			itemlen = remain;
		memcpy(itembuf, ptr, itemlen);
		itembuf[itemlen] = '\0';
		rc = send_message(fd, CM_ITEM, itembuf);
		if (rc)
			break;

		ptr += itemlen;
		remain -= itemlen;
	}

	return rc;
}

static int dump_debug(int ci, int fd)
{
	int rc, rctmp;
	char error_msg[100];  /* Arbitrary size smaller than a message */
	int count = 0;

	if (dump_wrap)
		count += debug_bytes_to_count(DUMP_SIZE - dump_point);
	count += debug_bytes_to_count(dump_point);

	rc = send_message(fd, CM_ITEMCOUNT, count);
	if (rc) {
		snprintf(error_msg, sizeof(error_msg),
			 "Unable to send ITEMCOUNT: %s",
			 strerror(-rc));
		goto out_status;
	}

	if (dump_wrap) {
		rc = send_debug(fd, dump_buf + dump_point,
				DUMP_SIZE - dump_point);
		if (rc) {
			snprintf(error_msg, sizeof(error_msg),
				 "Unable to send ITEM: %s",
				 strerror(-rc));
			goto out_status;
		}
	}
	rc = send_debug(fd, dump_buf, dump_point);
	if (rc) {
		snprintf(error_msg, sizeof(error_msg),
			 "Unable to send ITEM: %s",
			 strerror(-rc));
		goto out_status;
	}

	strcpy(error_msg, "OK");

out_status:
	rctmp = send_message(fd, CM_STATUS, -rc, error_msg);
	if (rctmp) {
		log_error("Error sending STATUS message: %s",
			  strerror(-rc));
		if (!rc)
			rc = rctmp;
	}

	return rc;
}

static int send_filesystems(int ci, int fd, const char *fstype,
			    const char *cluster)
{
	char *error_msg;

	if (!fstype || strcmp(fstype, OCFS2_FS_NAME)) {
		error_msg = "Invalid filesystem type";
		goto fail;
	}

	if (!validate_cluster(cluster)) {
		error_msg = "Invalid cluster name";
		goto fail;
	}

	return send_mountgroups(ci, fd);

fail:
	return send_message(fd, CM_STATUS, EINVAL, error_msg);
}

static int send_clustername(int ci, int fd)
{
	int rc = 0, rctmp;
	char error_msg[100];  /* Arbitrary size smaller than a message */
	const char *cluster;

	rc = get_clustername(&cluster);
	if (rc) {
		snprintf(error_msg, sizeof(error_msg),
			 "Unable to query cluster name: %s",
			 strerror(-rc));
		goto out_status;
	}

	/* Cman only supports one cluster */
	rc = send_message(fd, CM_ITEMCOUNT, 1);
	if (rc) {
		snprintf(error_msg, sizeof(error_msg),
			 "Unable to send ITEMCOUNT: %s",
			 strerror(-rc));
		goto out_status;
	}

	rc = send_message(fd, CM_ITEM, cluster);
	if (rc) {
		snprintf(error_msg, sizeof(error_msg),
			 "Unable to send ITEM: %s",
			 strerror(-rc));
		goto out_status;
	}

	strcpy(error_msg, "OK");

out_status:
	rctmp = send_message(fd, CM_STATUS, -rc, error_msg);
	if (rctmp) {
		log_error("Error sending STATUS message: %s",
			  strerror(-rc));
		if (!rc)
			rc = rctmp;
	}

	return rc;
}

static void dead_client(int ci)
{
	dead_mounter(ci, client[ci].fd);
	connection_dead(ci);
}

static void process_client(int ci)
{
	client_message message;
	char *argv[OCFS2_CONTROLD_MAXARGS + 1];
	char buf[OCFS2_CONTROLD_MAXLINE];
	int rv, fd = client[ci].fd;

	log_debug("client msg");
	/* receive_message ensures we have the proper number of arguments */
	rv = receive_message(fd, buf, &message, argv);
	if (rv == -EPIPE) {
		dead_client(ci);
		return;
	}

	if (rv < 0) {
		/* XXX: Should print better errors matching our returns */
		log_debug("client %d fd %d read error %d", ci, fd, -rv);
		return;
	}

	log_debug("client message %d from %d: %s", message, ci,
		  message_to_string(message));

	switch (message) {
		case CM_MOUNT:
		rv = do_mount(ci, fd, argv[0], argv[1], argv[2], argv[3],
			      argv[4]);
		fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
		break;

		case CM_MRESULT:
		rv = do_mount_result(ci, fd, argv[0], argv[1], argv[2],
				     argv[3]);
		break;

		case CM_UNMOUNT:
		rv = do_unmount(ci, fd, argv[0], argv[1], argv[2]);
		break;

		case CM_LISTCLUSTERS:
		rv = send_clustername(ci, fd);
		break;

		case CM_LISTFS:
		rv = send_filesystems(ci, fd, argv[0], argv[1]);
		break;

		case CM_DUMP:
		rv = dump_debug(ci, fd);
		break;

		case CM_STATUS:
		log_error("Someone sent us cm_status!");
		break;

		default:
		log_error("Invalid message received");
		break;
	}

	return;
}

static void process_listener(int ci)
{
	int fd, i;
	fd = accept(client[ci].fd, NULL, NULL);
	if (fd < 0) {
		log_debug("accept error %d %d", fd, errno);
		return;
	}

	i = connection_add(fd, process_client, NULL);
	if (i < 0) {
		log_error("Error adding client: %s", strerror(-i));
		close(fd);
	} else
		log_debug("new client connection %d", i);
}

static void dead_listener(int ci)
{
	log_error("Error on the listening socket");
	connection_dead(ci);
	shutdown_daemon();
}

static int setup_listener(void)
{
	int fd, i;

	fd = ocfs2_client_listen();
	if (fd < 0) {
		log_error("Unable to start listening socket: %s",
			  strerror(-fd));
		return fd;
	}

	i = connection_add(fd, process_listener, dead_listener);
	if (i < 0) {
		log_error("Unable to add listening socket: %s",
			  strerror(-i));
		close(fd);
		return i;
	}

	log_debug("new listening connection %d", i);

	return 0;
}

static int proto_version_to_checkpoint(struct ocfs2_protocol_version *proto,
				       char **checkpoint_data)
{
	size_t len;
	char *buf;

	len = snprintf(NULL, 0, "%02x %02x", proto->pv_major,
		       proto->pv_minor);
	buf = malloc(sizeof(char) * (len + 1));
	if (!buf) {
		log_error("Unable to allocate memory for checkpoint data");
		return -ENOMEM;
	}

	snprintf(buf, len + 1, "%02x %02x", proto->pv_major,
		 proto->pv_minor);
	*checkpoint_data = buf;

	return 0;
}

static int checkpoint_to_proto_version(char *data, size_t data_len,
				       struct ocfs2_protocol_version *proto)
{
	long major, minor;
	char *ptr;
	struct proto_version_str {
		char major[2];
		char space;
		char minor[2];
		char null;
	} str;

	if (data_len != sizeof(struct proto_version_str)) {
		log_error("Protocol version string \"%.*s\" has incorrect "
			  "length",
			  data_len, data);
		return -EINVAL;
	}
	memcpy((char *)&str, data, data_len);

	if ((str.space != ' ') || (str.null != '\0')) {
		log_error("Protocol version string \"%.*s\" has invalid "
			  "separators",
			  data_len, data);
		return -EINVAL;
	}
	str.space = '\0';

	major = strtol(str.major, &ptr, 16);
	if (!ptr || *ptr) {
		log_error("Protocol request has bad version 0x%s 0x%s",
			  str.major, str.minor);
		return -EINVAL;
	}
	minor = strtol(str.minor, &ptr, 16);
	if (!ptr || *ptr) {
		log_error("Protocol version string has bad version 0x%s 0x%s",
			  str.major, str.minor);
		return -EINVAL;
	}

	/* The major and minor must be between 0 and 255, inclusive. */
	if ((major == LONG_MIN) || (major == LONG_MAX) ||
	    (minor == LONG_MIN) || (minor == LONG_MAX) ||
	    (major > (uint8_t)-1) || (major < 0) ||
	    (minor > (uint8_t)-1) || (minor < 0)) {
		log_error("Protocol version string has bad version 0x%s 0x%s",
			  str.major, str.minor);
		return -ERANGE;
	}

	proto->pv_major = major;
	proto->pv_minor = minor;

	return 0;
}

static int install_node_checkpoint(void)
{
	int rc;
	char *buf;
	errcode_t err;

	err = o2cb_get_max_locking_protocol(&fs_max_proto);
	if (err) {
		log_error("Error querying maximum filesystem locking "
			  "protocol: %s",
			  error_message(err));
		rc = -EIO;
		goto out;
	}

	rc = ckpt_open_this_node(&node_handle);
	if (rc)
		goto out;

	rc = proto_version_to_checkpoint(&daemon_max_proto, &buf);
	if (rc)
		goto out;

	rc = ckpt_section_store(node_handle, DAEMON_MAX_PROTOCOL_SECTION,
				buf, strlen(buf) + 1);
	free(buf);
	if (rc)
		goto out;

	rc = proto_version_to_checkpoint(&fs_max_proto, &buf);
	if (rc)
		goto out;

	rc = ckpt_section_store(node_handle, FS_MAX_PROTOCOL_SECTION,
				buf, strlen(buf) + 1);
	free(buf);

out:
	return rc;
}

static void drop_node_checkpoint(void)
{
	if (node_handle)
		ckpt_close(node_handle);
}

/*
 * If we're the only daemon running, install our maximum protocols
 * as the running values.
 */
static int install_global_checkpoint(void)
{
	int rc;
	char *buf;

	daemon_running_proto = daemon_max_proto;
	fs_running_proto = fs_max_proto;

	rc = ckpt_open_global(1);
	if (rc)
		goto out;

	rc = proto_version_to_checkpoint(&daemon_running_proto, &buf);
	if (rc)
		goto out;

	rc = ckpt_global_store(DAEMON_PROTOCOL_SECTION, buf,
			       strlen(buf) + 1);
	free(buf);
	if (rc)
		goto out;

	rc = proto_version_to_checkpoint(&fs_running_proto, &buf);
	if (rc)
		goto out;

	rc = ckpt_global_store(FS_PROTOCOL_SECTION, buf, strlen(buf) + 1);
	free(buf);

out:
	return rc;
}

/*
 * Compare the cluster's locking protocol version against our maximum.
 *
 * If the major numbers are different, they are incompatible.
 * If the cluster's minor is greater than our maximum minor, they are
 * incompatible.
 */
static int protocol_compatible(struct ocfs2_protocol_version *cluster,
			       struct ocfs2_protocol_version *our_max)
{
	if (cluster->pv_major != our_max->pv_major)
		return 0;

	if (cluster->pv_minor > our_max->pv_minor)
		return 0;

	return 1;
}

static int read_global_checkpoint(void)
{
	int rc;
	char *buf;
	size_t len;

	rc = ckpt_open_global(0);
	if (rc)
		goto out;

	rc = ckpt_global_get(DAEMON_PROTOCOL_SECTION, &buf, &len);
	if (rc)
		goto out;

	rc = checkpoint_to_proto_version(buf, len, &daemon_running_proto);
	free(buf);
	if (rc)
		goto out;
	if (!protocol_compatible(&daemon_running_proto,
				 &daemon_max_proto)) {
		log_error("Our maximum daemon protocol (%d.%d) is not "
			  "compatible with the cluster's protocol (%d.%d)",
			  daemon_max_proto.pv_major,
			  daemon_max_proto.pv_minor,
			  daemon_running_proto.pv_major,
			  daemon_running_proto.pv_minor);
		rc = -EPROTONOSUPPORT;
		goto out;
	}

	rc = ckpt_global_get(FS_PROTOCOL_SECTION, &buf, &len);
	if (rc)
		goto out;

	rc = checkpoint_to_proto_version(buf, len, &fs_running_proto);
	free(buf);
	if (rc)
		goto out;

	if (!protocol_compatible(&fs_running_proto,
				 &fs_max_proto)) {
		log_error("Our maximum fs protocol (%d.%d) is not "
			  "compatible with the cluster's protocol (%d.%d)",
			  fs_max_proto.pv_major,
			  fs_max_proto.pv_minor,
			  fs_running_proto.pv_major,
			  fs_running_proto.pv_minor);
		rc = -EPROTONOSUPPORT;
	}

out:
	return rc;
}

static void cpg_joined(int first)
{
	int rv;
	errcode_t err;

	log_debug("CPG is live, we are %s first daemon",
		  first ? "the" : "not the");

	if (first)
		rv = install_global_checkpoint();
	else
		rv = read_global_checkpoint();

	if (rv) {
		shutdown_daemon();
		return;
	}

	log_debug("Daemon protocol is %d.%d",
		  daemon_running_proto.pv_major,
		  daemon_running_proto.pv_minor);
	log_debug("fs protocol is %d.%d",
		  fs_running_proto.pv_major, fs_running_proto.pv_minor);

	log_debug("Connecting to dlm_controld");
	rv = setup_dlmcontrol();
	if (rv) {
		shutdown_daemon();
		return;
	}

	log_debug("Opening control device");
	err = o2cb_control_open(our_nodeid, &fs_running_proto);
	if (err) {
		log_error("Error opening control device: %s",
			  error_message(err));
		shutdown_daemon();
		return;
	}

	log_debug("Starting to listen for mounters");
	rv = setup_listener();
	if (rv < 0) {
		shutdown_daemon();
		return;
	}
}

static int loop(void)
{
	int rv, i, poll_timeout = -1;

	rv = setup_sigpipe();
	if (rv < 0)
		goto out;

	rv = setup_stack();
	if (rv < 0)
		goto out;

	rv = setup_ckpt();
	if (rv < 0)
		goto out;

	rv = install_node_checkpoint();
	if (rv < 0)
		goto out;

	rv = setup_cpg(cpg_joined);
	if (rv < 0)
		goto out;

	log_debug("setup done");

	for (;;) {
		rv = poll(pollfd, client_maxi + 1, poll_timeout);
		if ((rv < 0) && (errno != EINTR))
			log_error("poll error %d errno %d", rv, errno);
		rv = 0;

		for (i = 0; i <= client_maxi; i++) {
			if (client[i].fd < 0)
				continue;

			/*
			 * We handle POLLIN before POLLHUP so clients can
			 * finish what they were doing
			 */
			if (pollfd[i].revents & POLLIN) {
				client[i].work(i);
				if (time_to_die)
					goto stop;
			}

			if (pollfd[i].revents & POLLHUP) {
				client[i].dead(i);
				if (time_to_die)
					goto stop;
			}
		}
	}

stop:
	if (!rv && have_mounts())
		rv = 1;

	bail_on_mounts();

	o2cb_control_close();
	exit_dlmcontrol();
	exit_cpg();
	drop_node_checkpoint();
	exit_ckpt();
	exit_stack();

out:
	return rv;
}

static void lockfile(void)
{
	int fd, error;
	struct flock lock;
	char buf[33];

	memset(buf, 0, 33);

	fd = open(LOCKFILE_NAME, O_CREAT|O_WRONLY,
		  S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if (fd < 0) {
		fprintf(stderr, "cannot open/create lock file %s\n",
			LOCKFILE_NAME);
		exit(EXIT_FAILURE);
	}

	lock.l_type = F_WRLCK;
	lock.l_start = 0;
	lock.l_whence = SEEK_SET;
	lock.l_len = 0;

	error = fcntl(fd, F_SETLK, &lock);
	if (error) {
		fprintf(stderr, "ocfs2_controld is already running\n");
		exit(EXIT_FAILURE);
	}

	error = ftruncate(fd, 0);
	if (error) {
		fprintf(stderr, "cannot clear lock file %s\n", LOCKFILE_NAME);
		exit(EXIT_FAILURE);
	}

	sprintf(buf, "%d\n", getpid());

	error = write(fd, buf, strlen(buf));
	if (error <= 0) {
		fprintf(stderr, "cannot write lock file %s\n", LOCKFILE_NAME);
		exit(EXIT_FAILURE);
	}
}

static void daemonize(void)
{
	pid_t pid = fork();
	if (pid < 0) {
		perror("main: cannot fork");
		exit(EXIT_FAILURE);
	}
	if (pid)
		exit(EXIT_SUCCESS);
	setsid();
	chdir("/");
	umask(0);
	close(0);
	close(1);
	close(2);
	openlog("ocfs2_controld", LOG_PID, LOG_DAEMON);

	lockfile();
}

static void print_usage(void)
{
	printf("Usage:\n");
	printf("\n");
	printf("%s [options]\n", prog_name);
	printf("\n");
	printf("Options:\n");
	printf("\n");
	printf("  -D	       Enable debugging code and don't fork\n");
	printf("  -h	       Print this help, then exit\n");
	printf("  -V	       Print program version information, then exit\n");
}

static void decode_arguments(int argc, char **argv)
{
	int cont = 1;
	int optchar;

	while (cont) {
		optchar = getopt(argc, argv, OPTION_STRING);

		switch (optchar) {

		case 'D':
			daemon_debug_opt = 1;
			break;

		case 'h':
			print_usage();
			exit(EXIT_SUCCESS);
			break;

		case 'V':
			printf("ocfs2_controld (built %s %s)\n", __DATE__, __TIME__);
			/* printf("%s\n", REDHAT_COPYRIGHT); */
			exit(EXIT_SUCCESS);
			break;

		case ':':
		case '?':
			fprintf(stderr, "Please use '-h' for usage.\n");
			exit(EXIT_FAILURE);
			break;

		case EOF:
			cont = 0;
			break;

		default:
			fprintf(stderr, "unknown option: %c\n", optchar);
			exit(EXIT_FAILURE);
			break;
		};
	}
}

static void set_oom_adj(int val)
{
	FILE *fp;

	fp = fopen("/proc/self/oom_adj", "w");
	if (!fp)
		return;

	fprintf(fp, "%i", val);
	fclose(fp);
}

static void set_scheduler(void)
{
	struct sched_param sched_param;
	int rv;

	rv = sched_get_priority_max(SCHED_RR);
	if (rv != -1) {
		sched_param.sched_priority = rv;
		rv = sched_setscheduler(0, SCHED_RR, &sched_param);
		if (rv == -1)
			log_error("could not set SCHED_RR priority %d err %d",
				   sched_param.sched_priority, errno);
	} else {
		log_error("could not get maximum scheduler priority err %d",
			  errno);
	}
}

int main(int argc, char **argv)
{
	errcode_t err;
	prog_name = argv[0];
	const char *stack = NULL;

	init_mounts();

	initialize_o2cb_error_table();
	err = o2cb_init();
	if (err) {
		com_err(prog_name, err, "while trying to initialize o2cb");
		return 1;
	}

	err = o2cb_get_stack_name(&stack);
	if (err) {
		com_err(prog_name, err, "while determining the current cluster stack");
		return 1;
	}
	if (strcmp(stack, stackname)) {
		fprintf(stderr, "%s: This daemon supports the \"%s\" stack, but the \"%s\" stack is in use\n",
			prog_name, stackname, stack);
		return 1;
	}

	decode_arguments(argc, argv);

	if (!daemon_debug_opt)
		daemonize();

	set_scheduler();
	set_oom_adj(-16);

	return loop();
}

void daemon_dump_save(void)
{
	int len, i;

	len = strlen(daemon_debug_buf);

	for (i = 0; i < len; i++) {
		dump_buf[dump_point++] = daemon_debug_buf[i];

		if (dump_point == DUMP_SIZE) {
			dump_point = 0;
			dump_wrap = 1;
		}
	}
}


