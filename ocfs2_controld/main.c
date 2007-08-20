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

#include "o2cb_client_proto.h"
#include "ocfs2_controld_internal.h"

#define OPTION_STRING			"DhVw"
#define LOCKFILE_NAME			"/var/run/ocfs2_controld.pid"

#define DEFAULT_PLOCK_RATE_LIMIT 100

struct client {
	int fd;
	char type[32];
	struct mountgroup *mg;
	int another_mount;
};

static int client_maxi;
static int client_size = 0;
static struct client *client = NULL;
static struct pollfd *pollfd = NULL;

static int cman_fd;
static int listen_fd;
static int sigpipe_fd;
static int sigpipe_write_fd;
static int groupd_fd;

extern struct list_head mounts;
extern struct list_head withdrawn_mounts;
int no_withdraw;

static void handler(int signum)
{
	log_debug("Caught signal %d\n", signum);
	if (write(sigpipe_write_fd, &signum, sizeof(signum)) < sizeof(signum))
		log_error("Problem writing signal: %s\n", strerror(-errno));
}

static int handle_signal(void)
{
	int rc, caught_sig, abortp = 0;
	static int segv_already = 0;

	rc = read(sigpipe_fd, (char *)&caught_sig, sizeof(caught_sig));
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
			if (list_empty(&mounts)) {
				log_error("Caught signal %d, exiting",
					  caught_sig);
				rc = 1;
			} else {
				log_error("Caught signal %d, but mounts exist.  Ignoring.",
					  caught_sig);
				rc = 0;
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
	return rc;
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

	sigpipe_fd = signal_pipe[0];
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

	if (rc)
		log_error("Unable to set up signal handlers");

out:
	return rc;
}

int do_read(int fd, void *buf, size_t count)
{
	int rv, off = 0;

	while (off < count) {
		rv = read(fd, buf + off, count - off);
		if (rv == 0)
			return -1;
		if (rv == -1 && errno == EINTR)
			continue;
		if (rv == -1)
			return -1;
		off += rv;
	}
	return 0;
}

int do_write(int fd, void *buf, size_t count)
{
	int rv, off = 0;

 retry:
	rv = write(fd, buf + off, count);
	if (rv == -1 && errno == EINTR)
		goto retry;
	if (rv < 0) {
		log_error("write errno %d", errno);
		return rv;
	}

	if (rv != count) {
		count -= rv;
		off += rv;
		goto retry;
	}
	return 0;
}


static int client_add(int fd)
{
	int i;

	while (1) {
		/* This fails the first time with client_size of zero */
		for (i = 0; i < client_size; i++) {
			if (client[i].fd == -1) {
				client[i].fd = fd;
				pollfd[i].fd = fd;
				pollfd[i].events = POLLIN;
				if (i > client_maxi)
					client_maxi = i;
				return i;
			}
		}

		/* We didn't find an empty slot, so allocate more. */
		client_size += MAX_CLIENTS;

		if (!client) {
			client = malloc(client_size * sizeof(struct client));
			pollfd = malloc(client_size * sizeof(struct pollfd));
		} else {
			client = realloc(client, client_size *
						 sizeof(struct client));
			pollfd = realloc(pollfd, client_size *
						 sizeof(struct pollfd));
		}
		if (!client || !pollfd)
			log_error("Can't allocate client memory.");

		for (i = client_size - MAX_CLIENTS; i < client_size; i++) {
			client[i].fd = -1;
			pollfd[i].fd = -1;
		}
	}
}

static void client_dead(int ci)
{
	log_debug("client %d fd %d dead", ci, client[ci].fd);
	close(client[ci].fd);
	client[ci].fd = -1;
	pollfd[ci].fd = -1;
	client[ci].mg = NULL;
}

static int dump_debug(int ci)
{
	int len = DUMP_SIZE;

	if (dump_wrap) {
		len = DUMP_SIZE - dump_point;
		do_write(client[ci].fd, dump_buf + dump_point, len);
		len = dump_point;
	}

	do_write(client[ci].fd, dump_buf, len);
	return 0;
}

#if 0
/* mount.gfs sends us a special fd that it will write an error message to
   if mount(2) fails.  We can monitor this fd for an error message while
   waiting for the kernel mount outside our main poll loop */

void setup_mount_error_fd(struct mountgroup *mg)
{
	struct msghdr msg;
	struct cmsghdr *cmsg;
	struct iovec vec;
	char tmp[CMSG_SPACE(sizeof(int))];
	int fd, socket = client[mg->mount_client].fd;
	char ch;
	ssize_t n;

	memset(&msg, 0, sizeof(msg));

	vec.iov_base = &ch;
	vec.iov_len = 1;
	msg.msg_iov = &vec;
	msg.msg_iovlen = 1;
	msg.msg_control = tmp;
	msg.msg_controllen = sizeof(tmp);

	n = recvmsg(socket, &msg, 0);
	if (n < 0) {
		log_group(mg, "setup_mount_error_fd recvmsg err %d errno %d",
			  n, errno);
		return;
	}
	if (n != 1) {
		log_group(mg, "setup_mount_error_fd recvmsg got %ld", (long)n);
		return;
	}

	cmsg = CMSG_FIRSTHDR(&msg);

	if (cmsg->cmsg_type != SCM_RIGHTS) {
		log_group(mg, "setup_mount_error_fd expected type %d got %d",
			  SCM_RIGHTS, cmsg->cmsg_type);
		return;
	}

	fd = (*(int *)CMSG_DATA(cmsg));
	mg->mount_error_fd = fd;

	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

	log_group(mg, "setup_mount_error_fd got fd %d", fd);
}
#endif

static int process_client(int ci)
{
	struct mountgroup *mg;
	client_message message;
	char *argv[OCFS2_CONTROLD_MAXARGS + 1];
	char buf[OCFS2_CONTROLD_MAXLINE];
	int rv, fd = client[ci].fd;

	/* receive_message ensures we have the proper number of arguments */
	rv = receive_message(fd, buf, &message, argv);
	if (rv == -EPIPE) {
		client_dead(ci);
		return 0;
	}
	if (rv < 0) {
		/* XXX: Should print better errors matching our returns */
		log_debug("client %d fd %d read error %d", ci, fd, -rv);
		return rv;
	}

	log_debug("client message %d from %d: %s", message, ci,
		  message_to_string(message));

	switch (message) {
		case CM_MOUNT:
		rv = do_mount(ci, fd, argv[0], argv[1], argv[2], argv[3],
			      argv[4], &mg);
		fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
		if (!rv || rv == -EALREADY) {
			client[ci].another_mount = rv;
			client[ci].mg = mg;
			mg->mount_client_fd = fd;
		}
		break;

		case CM_MRESULT:
		rv = do_mount_result(client[ci].mg, ci,
				     client[ci].another_mount,
				     argv[0], argv[1], argv[2], argv[3]);
		break;

		case CM_UNMOUNT:
		rv = do_unmount(ci, fd, argv[0], argv[1], argv[2]);
		if (!rv) {
			client[ci].mg = mg;
			mg->mount_client_fd = fd;
		}
		break;

		case CM_STATUS:
		log_error("Someone sent us cm_status!");
		break;

		default:
		log_error("Invalid message received");
		break;
	}
	if (daemon_debug_opt)
		dump_state();

#if 0
	} else if (!strcmp(cmd, "dump")) {
		dump_debug(ci);

	} else {
		rv = -EINVAL;
		goto reply;
	}
#endif

	return rv;
}

/*
 * THIS FUNCTION CAUSES PROBLEMS.
 *
 * bail_on_mounts() is called when we are forced to exit via a signal or
 * cman dying on us.  As such, it removes regions from o2cb but does
 * not communicate with cman.  This can cause o2cb to self-fence or cman
 * to go nuts.  But hey, if you SIGKILL the daemon, you get what you pay 
 * for.
 */
static void bail_on_mounts(void)
{
	struct list_head *p, *t;
	struct mountgroup *mg;

	list_for_each_safe(p, t, &mounts) {
		mg = list_entry(p, struct mountgroup, list);
		clean_up_mountgroup(mg);
	}
}


static int loop(void)
{
	int rv, i, f, poll_timeout = -1;

	rv = listen_fd = client_listen();
	if (rv < 0)
		goto out;
	client_add(listen_fd);

	rv = setup_sigpipe();
	if (rv < 0)
		goto out;
	client_add(sigpipe_fd);

	rv = cman_fd = setup_cman();
	if (rv < 0)
		goto out;
	client_add(cman_fd);

	rv = groupd_fd = setup_groupd();
	if (rv < 0)
		goto out;
	client_add(groupd_fd);

	log_debug("setup done");

	for (;;) {
		rv = poll(pollfd, client_maxi + 1, poll_timeout);
		if ((rv < 0) && (errno != EINTR))
			log_error("poll error %d errno %d", rv, errno);
		rv = 0;

		/* client[0] is listening for new connections */

		if (pollfd[0].revents & POLLIN) {
			f = accept(client[0].fd, NULL, NULL);
			if (f < 0)
				log_debug("accept error %d %d", f, errno);
			else
				client_add(f);
		}

		for (i = 1; i <= client_maxi; i++) {
			if (client[i].fd < 0)
				continue;

			if (pollfd[i].revents & POLLIN) {
				if (pollfd[i].fd == groupd_fd)
					process_groupd();
				else if (pollfd[i].fd == cman_fd)
					process_cman();
				else if (pollfd[i].fd == sigpipe_fd) {
					rv = handle_signal();
					if (rv)
						goto stop;
				} else
					process_client(i);
			}

			if (pollfd[i].revents & POLLHUP) {
				if (pollfd[i].fd == cman_fd) {
					log_error("cman connection died");
					goto stop;
				} else if (pollfd[i].fd == groupd_fd) {
					log_error("groupd connection died");
					goto stop;
				}
				client_dead(i);
			}
		}
	}

stop:
	if (!rv && !list_empty(&mounts))
		rv = 1;

	bail_on_mounts();

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
	printf("  -w	       Disable withdraw\n");
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

		case 'w':
			no_withdraw = 1;
			break;

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
	prog_name = argv[0];
	INIT_LIST_HEAD(&mounts);
	/* INIT_LIST_HEAD(&withdrawn_mounts); */

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

char *prog_name;
int daemon_debug_opt;
char daemon_debug_buf[1024];
char dump_buf[DUMP_SIZE];
int dump_point;
int dump_wrap;

