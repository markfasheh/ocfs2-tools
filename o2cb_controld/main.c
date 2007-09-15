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

#include "o2cb_controld.h"
#include "o2cb_client_proto.h"

#define OPTION_STRING			"DhV"
#define LOCKFILE_NAME			"/var/run/o2cb_controld.pid"

static int listen_fd, member_fd;
static int sigpipe_write_fd, sigpipe_fd;
struct client {
	int fd;
};

static int client_maxi;
static int client_size = 0;
static struct client *client = NULL;
static struct pollfd *pollfd = NULL;
static int live_clients = 0;


static void handler(int signum)
{
	log_debug("Caught signal %d", signum);
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
			if (live_clients) {
				log_error("Caught signal %d, but clients exist.  Ignoring.",
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

#define MAX_CLIENTS 8
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
}


static int loop(void)
{
	int rv, i;

	rv = listen_fd = client_listen(O2CB_CONTROLD_SOCK_PATH);
	if (rv < 0)
		goto out;
	client_add(listen_fd);

	rv = member_fd = setup_member();
	if (rv < 0)
		goto out;
	client_add(member_fd);

	rv = setup_sigpipe();
	if (rv)
		goto out;
	client_add(sigpipe_fd);

	for (;;) {
		rv = poll(pollfd, client_maxi + 1, -1);
		if (rv == -1 && errno == EINTR)
			continue;
		if (rv < 0) {
			log_error("poll errno %d", errno);
			goto stop;
		}

		for (i = 0; i <= client_maxi; i++) {
			if (pollfd[i].revents & POLLIN) {
				if (pollfd[i].fd == listen_fd) {
					rv = accept(listen_fd, NULL, NULL);
					if (rv < 0) {
						log_debug("accept error %d %d",
							  rv, errno);
					} else {
						client_add(rv);
						live_clients++;
					}
				} else if (pollfd[i].fd == member_fd) {
					rv = process_member();
					if (rv)
						goto stop;
				} else if (pollfd[i].fd == sigpipe_fd) {
					rv = handle_signal();
					if (rv)
						goto stop;
				}
			}

			if (pollfd[i].revents & POLLHUP) {
				if (pollfd[i].fd == member_fd) {
					log_error("cluster is down, exiting");
					goto stop;
				} else if (pollfd[i].fd == listen_fd) {
					log_error("listening fd died, exiting");
					goto stop;
				} else if (pollfd[i].fd == sigpipe_fd) {
					log_error("signal fd died, exiting");
					goto stop;
				}
				log_debug("closing fd %d", pollfd[i].fd);
				client_dead(i);
				live_clients--;
			}
		}
	}
	rv = 0;

stop:
	finalize_cluster(NULL);

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
		fprintf(stderr, "o2cb_controld is already running\n");
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
	openlog("o2cb_controld", LOG_PID, LOG_DAEMON);

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
			printf("o2cb_controld (built %s %s)\n", __DATE__, __TIME__);
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

	decode_arguments(argc, argv);

	if (!daemon_debug_opt)
		daemonize();

	set_scheduler();
	set_oom_adj(-16);

	initialize_o2cb();

	/*
	 * If this daemon was killed and the cluster shut down, and
	 * then the cluster brought back up and this daemon restarted,
	 * there will be old configfs entries we need to clear out.
	 */
	remove_stale_clusters();

	return loop();
}

char *prog_name;
int daemon_debug_opt;
char daemon_debug_buf[256];

