/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * o2hbmonitor.c
 *
 * Monitors o2hb
 *
 * Copyright (C) 2010 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

/*
 * This utility requires the o2hb debugfs file elapsed_time_in_ms which shows
 * the time since the o2hb heartbeat timer was last armed.  This file was added
 * in the mainline kernel via commit 43695d095dfaf266a8a940d9b07eed7f46076b49.
 *
 * This utility scans the configfs to see if the cluster is up. If not up, it
 * checks again after CONFIG_POLL_IN_SECS.
 *
 * If up, it loads the dead threshold and then scans the debugfs file,
 * elapsed_time_in_ms, of each heartbeat region. If the elapsed time is
 * greater than the warn threshold, it logs a message in syslog.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <linux/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <libgen.h>
#include <syslog.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/sem.h>

#define SYS_CONFIG_DIR			"/sys/kernel/config"
#define O2HB_CLUSTER_DIR		SYS_CONFIG_DIR"/cluster"
#define O2HB_HEARTBEAT_DIR		O2HB_CLUSTER_DIR"/%s/heartbeat"
#define O2HB_DEAD_THRESHOLD		O2HB_HEARTBEAT_DIR"/threshold"
#define O2HB_DEVICE			O2HB_HEARTBEAT_DIR"/%s/dev"

#define SYS_DEBUG_DIR			"/sys/kernel/debug"
#define O2HB_DEBUG_DIR			SYS_DEBUG_DIR"/o2hb"
#define O2HB_ELAPSED_TIME		O2HB_DEBUG_DIR"/%s/elapsed_time_in_ms"

#define DEAD_THRESHOLD_IN_MSECS(a)	(((a) - 1) * 2000)
#define WARN_THRESHOLD_PERCENT		50

#define CONFIG_POLL_IN_SECS		60
#define SLOW_POLL_IN_SECS		10
#define FAST_POLL_IN_SECS		2

#define O2HB_SEM_MAGIC_KEY		0x6F326862

char *progname;
int interactive;
int warn_threshold_percent;
int verbose;

char *cluster_name;
unsigned long dead_threshold_in_ms;
unsigned long warn_threshold_in_ms;
unsigned long poll_in_secs;

static void show_version(void)
{
	fprintf(stderr, "%s %s\n", progname, VERSION);
}

static char *do_strchomp(char *str)
{
	int len = strlen(str);
	char *p;

	if (!len)
		return str;

	p = str + len - 1;
	while ((len--) && (isspace(*p) || (*p == '\n')))
		*p-- = '\0';

	return str;
}

static int get_value(char *path, char *value, int count)
{
	int fd = -1, ret = -1;
	char *p = value;

	fd = open(path, O_RDONLY);
	if (fd > 0)
		ret = read(fd, value, count);
	if (ret > 0) {
		p += ret;
		*p = '\0';
		ret = 0;
	}

	if (!ret)
		do_strchomp(value);

	if (fd > -1)
		close(fd);
	return ret;
}

static void get_device_name(char *region, char **device)
{
	int ret;
	char val[255];
	char path[PATH_MAX];

	sprintf(path, O2HB_DEVICE, cluster_name, region);
	ret = get_value(path, val, sizeof(val));
	if (ret)
		goto bail;
	*device = strdup(val);

bail:
	return ;
}

static void process_elapsed_time(char *region, unsigned long elapsed)
{
	int warn = 0;
	char *device = NULL;

	if (elapsed >= warn_threshold_in_ms)
		warn++;

	if (!verbose && !warn)
		return;

	get_device_name(region, &device);

	if (verbose)
		fprintf(stdout, "Last ping %lu msecs ago on /dev/%s, %s\n",
		       elapsed, device, region);

	if (warn) {
		poll_in_secs = FAST_POLL_IN_SECS;
		syslog(LOG_WARNING, "Last ping %lu msecs ago on /dev/%s, %s\n",
		       elapsed, device, region);
	}

	if (device)
		free(device);
}

static int read_elapsed_time(char *region, unsigned long *elapsed)
{
	int ret;
	char val[32];
	char path[PATH_MAX];

	*elapsed = 0;

	sprintf(path, O2HB_ELAPSED_TIME, region);
	ret = get_value(path, val, sizeof(val));
	if (ret)
		goto bail;
	*elapsed = strtoul(val, NULL, 0);

	ret = 0;

bail:
	return ret;
}

static void scan_heartbeat_regions(void)
{
	int ret = -1;
	DIR *dir = NULL;
	struct dirent *ent;
	char path[PATH_MAX];
	unsigned long elapsed;

	sprintf(path, O2HB_DEBUG_DIR);

	dir = opendir(path);
	if (!dir)
		return;

	do {
		ent = readdir(dir);
		if (ent && ent->d_type == DT_DIR && strcmp(ent->d_name, ".") &&
		    strcmp(ent->d_name, "..")) {
			ret = read_elapsed_time(ent->d_name, &elapsed);
			if (!ret)
				process_elapsed_time(ent->d_name, elapsed);
		}
	} while (ent);

	if (dir)
		closedir(dir);
}

static int populate_thresholds(void)
{
	int ret;
	char val[32];
	char path[PATH_MAX];

	sprintf(path, O2HB_DEAD_THRESHOLD, cluster_name);
	ret = get_value(path, val, sizeof(val));
	if (!ret) {
		dead_threshold_in_ms =
			DEAD_THRESHOLD_IN_MSECS(strtoul(val, NULL, 0));
		warn_threshold_in_ms =
			(dead_threshold_in_ms * warn_threshold_percent / 100);
	}

	return ret;
}

static int populate_cluster(void)
{
	DIR *dir;
	struct dirent *ent;

	if (cluster_name) {
		free(cluster_name);
		cluster_name = NULL;
	}

	dir = opendir(O2HB_CLUSTER_DIR);
	if (!dir)
		return -1;

	do {
		ent = readdir(dir);
		if (ent && ent->d_type == 4 && strcmp(ent->d_name, ".") &&
		    strcmp(ent->d_name, "..")) {
			cluster_name = strdup(ent->d_name);
			break;
		}
	} while (ent);

	closedir(dir);

	if (cluster_name)
		return 0;

	return -1;
}

static int is_cluster_up(void)
{
	struct stat buf;
	int status;
	static int warn_count = 0;

	status = stat(O2HB_CLUSTER_DIR, &buf);
	if (status)
		return 0;

	status = stat(O2HB_DEBUG_DIR, &buf);
	if (status) {
		if (!(warn_count++ % 10))
			syslog(LOG_WARNING,
			       "mount debugfs at /sys/kernel/debug");
		return 0;
	}

	return 1;
}

static void monitor(void)
{
	int ret;

	while (1) {
		if (!is_cluster_up()) {
			sleep(CONFIG_POLL_IN_SECS);
			continue;
		}

		ret = populate_cluster();
		if (!ret)
			ret = populate_thresholds();
		if (ret) {
			sleep(CONFIG_POLL_IN_SECS);
			continue;
		}

		poll_in_secs = SLOW_POLL_IN_SECS;

		scan_heartbeat_regions();

		sleep(poll_in_secs);
	}
}

static int islocked(void)
{
	int semid;
	struct sembuf trylock[1] = {
		{.sem_num = 0, .sem_op = 0, .sem_flg = SEM_UNDO|IPC_NOWAIT},
	};

	semid = semget(O2HB_SEM_MAGIC_KEY, 1, 0);
	if (semid < 0)
		return 0;
	if (semop(semid, trylock, 1) < 0)
		return 1;
	return 0;
}

static int getlock(void)
{
	int semid, vals[1] = { 0 };
	struct sembuf trylock[2] = {
		{.sem_num = 0, .sem_op = 0, .sem_flg = SEM_UNDO|IPC_NOWAIT},
		{.sem_num = 0, .sem_op = 1, .sem_flg = SEM_UNDO|IPC_NOWAIT},
	};

	semid = semget(O2HB_SEM_MAGIC_KEY, 1, 0);
	if (semid < 0) {
		semid = semget(O2HB_SEM_MAGIC_KEY, 1,
			       IPC_CREAT|IPC_EXCL|S_IRUSR);
		if (semid < 0)
			goto out;
		semctl(semid, 0, SETALL, vals);
		if (semop(semid, trylock, 2) < 0)
			goto out;
		else
			return 0;
	}
	if (semop(semid, trylock, 2) < 0)
		goto out;
	return 0;
out:
	if (errno == EAGAIN) {
		syslog(LOG_WARNING, "Another instance of %s is already running."
		       " Aborting.\n", progname);
		return 1;
	}
	return 0;
}

static void usage(void)
{
	fprintf(stderr, "usage: %s [-w percent] -[ivV]\n", progname);
	fprintf(stderr, "\t -w, Warn threshold percent (default 50%%)\n");
	fprintf(stderr, "\t -i, Interactive\n");
	fprintf(stderr, "\t -v, Verbose\n");
	fprintf(stderr, "\t -V, Version\n");
	exit(1);
}

int main(int argc, char **argv)
{
	int c, ret, version = 0;

	/* init globals */
	progname = basename(argv[0]);
	interactive = 0;
	warn_threshold_percent = WARN_THRESHOLD_PERCENT;
	verbose = 0;
	cluster_name = NULL;

	while (1) {
		c = getopt(argc, argv, "w:i?hvV");
		if (c == -1)
			break;
		switch (c) {
		case 'i':
			interactive = 1;
			break;
		case 'v':
			++verbose;
			break;
		case 'w':
			warn_threshold_percent = strtoul(optarg, NULL, 0);
			if (warn_threshold_percent < 1 ||
			    warn_threshold_percent > 99)
				warn_threshold_percent = WARN_THRESHOLD_PERCENT;
			break;
		case 'V':
			version = 1;
			break;
		case '?':
		case 'h':
		default:
			usage();
			break;
		}
	}

	if (version)
		show_version();

	if (islocked()) {
		fprintf(stderr, "Another instance of %s is already running. "
		       "Aborting.\n", progname);
		return 1;
	}

	if (!interactive) {
		ret = daemon(0, verbose);
		if (ret)
			fprintf(stderr, "Unable to daemonize, %s\n",
				strerror(errno));
	}

	openlog(progname, LOG_CONS|LOG_NDELAY, LOG_DAEMON);
	ret = getlock();
	if (ret) {
		closelog();
		return ret;
	}

	syslog(LOG_INFO, "Starting\n");
	monitor();
	closelog();

	return 0;
}
