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

#define OPTION_STRING			"DhV"
#define LOCKFILE_NAME			"/var/run/o2cb_controld.pid"

#if 0
static int uevent_fd;
#endif
static int member_fd;

static void sigterm_handler(int sig)
{
	clear_configfs();
}

static int loop(void)
{
	struct pollfd *pollfd;
	int rv, i, maxi;

	pollfd = malloc(MAXCON * sizeof(struct pollfd));
	if (!pollfd)
		return -1;

	rv = member_fd = setup_member();
	if (rv < 0)
		goto out;
	pollfd[0].fd = member_fd;
	pollfd[0].events = POLLIN;

	maxi = 0;

	for (;;) {
		rv = poll(pollfd, maxi + 1, -1);
		if (rv == -1 && errno == EINTR)
			continue;
		if (rv < 0) {
			log_error("poll errno %d", errno);
			goto out;
		}

		for (i = 0; i <= maxi; i++) {
			if (pollfd[i].revents & POLLIN) {
				if (pollfd[i].fd == member_fd)
					process_member();
			}

			if (pollfd[i].revents & POLLHUP) {
				if (pollfd[i].fd == member_fd) {
					log_error("cluster is down, exiting");
					clear_configfs();
					exit(1);
				}
				log_debug("closing fd %d", pollfd[i].fd);
				close(pollfd[i].fd);
			}
		}
	}
	rv = 0;
 out:
	free(pollfd);
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

	signal(SIGTERM, sigterm_handler);

	set_scheduler();
	set_oom_adj(-16);

	/* if this daemon was killed and the cluster shut down, and
	   then the cluster brought back up and this daemon restarted,
	   there will be old configfs entries we need to clear out */
	clear_configfs();

#if 0
	set_ccs_options();
#endif

	return loop();
}

char *prog_name;
int daemon_debug_opt;
char daemon_debug_buf[256];

