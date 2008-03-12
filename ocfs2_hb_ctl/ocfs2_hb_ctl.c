/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * ocfs2_hb_ctl.c  Utility to start / stop heartbeat on demand
 *
 * Copyright (C) 2005 Oracle.  All rights reserved.
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
 * 
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 * Authors: Mark Fasheh
 */


#define _XOPEN_SOURCE 600  /* Triggers XOPEN2K in features.h */
#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE /* Because libc really doesn't want us using O_DIRECT? */

#include <sys/types.h>
#include <sys/wait.h>
#include <inttypes.h>

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <signal.h>

#include "ocfs2/ocfs2.h"

#define DEV_PREFIX      "/dev/"
#define PROC_IDE_FORMAT "/proc/ide/%s/media"
#define IONICE_PATH	"/usr/bin/ionice"

enum hb_ctl_action {
	HB_ACTION_UNKNOWN,
	HB_ACTION_USAGE,
	HB_ACTION_START,
	HB_ACTION_STOP,
	HB_ACTION_REFINFO,
	HB_ACTION_IONICE,
};

struct hb_ctl_options {
	enum hb_ctl_action action;
	char *dev_str;
	char *uuid_str;
	int  io_prio;
	char *service;  /* The service accessing the region.  Ths is
			   usually the mountpoint, but could be a program
			   name like 'fsck.ocfs2'. Note that the service
			   is now a required argument to this program.
			   This will work even with old kernels, because
			   o2cb.init fills the hb_ctl path with /bin/true.
			   Nothing in ocfs2-tools will call this
			   incorrectly. */
};


static char *progname = "ocfs2_hb_ctl";
static struct o2cb_region_desc *region_desc = NULL;

static void block_signals(int how)
{
     sigset_t sigs;

     sigfillset(&sigs);
     sigdelset(&sigs, SIGTRAP);
     sigdelset(&sigs, SIGSEGV);
     sigprocmask(how, &sigs, NULL);
}

static void free_desc(void)
{
	if (region_desc) {
		if (region_desc->r_name)
			ocfs2_free(&region_desc->r_name);
		if (region_desc->r_device_name)
			ocfs2_free(&region_desc->r_device_name);

		ocfs2_free(&region_desc);
		region_desc = NULL;
	}
}
			   
static errcode_t get_desc(const char *dev)
{
	errcode_t err = 0;
	ocfs2_filesys *fs;

	if (region_desc)
		goto out;

	err = ocfs2_malloc0(sizeof(struct o2cb_region_desc),
			    &region_desc);
	if (err)
		goto out;

	err = ocfs2_open(dev,
			 OCFS2_FLAG_RO | OCFS2_FLAG_HEARTBEAT_DEV_OK,
			 0, 0, &fs);
	if (err)
		goto out;

	err = ocfs2_fill_heartbeat_desc(fs, region_desc);
	if (!err) {
		region_desc->r_name = strdup(region_desc->r_name);
		region_desc->r_device_name = strdup(region_desc->r_device_name);
		if (!region_desc->r_name || !region_desc->r_device_name)
			err = OCFS2_ET_NO_MEMORY;
	} else {
		region_desc->r_name = NULL;
		region_desc->r_device_name = NULL;
	}

	ocfs2_close(fs);

out:
	if (err)
		free_desc();

	return err;
}

static errcode_t get_uuid(char *dev, char *uuid)
{
	errcode_t ret;

	ret = get_desc(dev);
	if (!ret) 
		strcpy(uuid, region_desc->r_name);

	return ret;
}

static errcode_t compare_dev(const char *dev,
			     struct hb_ctl_options *hbo)
{
	errcode_t err;
	int len;
	char *device;

	if (region_desc) {
		fprintf(stderr, "We have a descriptor already!\n");
		free_desc();
	}
	
	len = strlen(DEV_PREFIX) + strlen(dev) + 1;
	device = malloc(sizeof(char) * len);
	if (!device)
		return OCFS2_ET_NO_MEMORY;
	snprintf(device, len, DEV_PREFIX "%s", dev);

	/* Any problem with getting the descriptor is NOT FOUND */
	err = OCFS2_ET_FILE_NOT_FOUND;
	if (get_desc(device))
		goto out;

	if (!strcmp(region_desc->r_name, hbo->uuid_str)) {
		hbo->dev_str = device;
		err = 0;
	} else
		free_desc();

out:
	if (err && device)
		free(device);

	return err;
}

static int as_ide_disk(const char *dev_name)
{
    FILE *f;
    int is_disk = 1;
    size_t len;
    char *proc_name;

    len = strlen(PROC_IDE_FORMAT) + strlen(dev_name);
    proc_name = (char *)malloc(sizeof(char) * len);
    if (!proc_name)
        return 0;

    snprintf(proc_name, len, PROC_IDE_FORMAT, dev_name);
    
    /* If not ide, file won't exist */
    f = fopen(proc_name, "r");
    if (f)
    {
        if (fgets(proc_name, len, f))
        {
            /* IDE devices we don't want to probe */
            if (!strncmp(proc_name, "cdrom", strlen("cdrom")) ||
                !strncmp(proc_name, "tape", strlen("tape")))
                is_disk = 0;
        }
        fclose(f);
    }

    free(proc_name);

    return is_disk;
}  /* as_ide_disk() */


/* Um, wow, this is, like, one big hardcode */
static errcode_t scan_devices(errcode_t (*func)(const char *,
						struct hb_ctl_options *),
			      struct hb_ctl_options *hbo)
{
	errcode_t err = 0;
	int rc, major, minor;
	FILE *f;
	char *buffer, *name;

	buffer = (char *)malloc(sizeof(char) * (PATH_MAX + 1));
	if (!buffer)
		return OCFS2_ET_NO_MEMORY;

	name = (char *)malloc(sizeof(char) * (PATH_MAX + 1));
	if (!name)
	{
		free(buffer);
		return OCFS2_ET_NO_MEMORY;
	}

	f = fopen("/proc/partitions", "r");
	if (!f)
	{
		rc = -errno;
		goto out_free;
	}

	err = OCFS2_ET_FILE_NOT_FOUND;
	while (1)
	{
		if ((fgets(buffer, PATH_MAX + 1, f)) == NULL)
			break;

		name[0] = '\0';
		major = minor = 0;

		/* FIXME: If this is bad, send patches */
		if (sscanf(buffer, "%d %d %*d %99[^ \t\n]",
			   &major, &minor, name) < 3)
			continue;

		if (*name && major)
		{
			if (!as_ide_disk(name))
				continue;
			
			err = func(name, hbo);
			if (!err || (err != OCFS2_ET_FILE_NOT_FOUND))
				break;
		}
	}

	fclose(f);

out_free:
	free(buffer);
	free(name);

	return err;
}  /* scan_devices() */

static errcode_t lookup_dev(struct hb_ctl_options *hbo)
{
	return scan_devices(compare_dev, hbo);
}

static errcode_t start_heartbeat(struct hb_ctl_options *hbo)
{
	errcode_t err = 0;
	struct o2cb_cluster_desc cluster = {
		.c_stack = NULL,  /* classic stack only */
	};

	if (!hbo->dev_str)
		err = lookup_dev(hbo);
	if (!err) {
		region_desc->r_persist = 1;  /* hb_ctl is for reals */
		region_desc->r_service = hbo->service;
		err = o2cb_begin_group_join(&cluster, region_desc);
		if (!err) {
			/*
			 * This is a manual start, there is no service
			 * or mountpoint being started by hb_ctl, so
			 * we assume success
			 */
			err = o2cb_complete_group_join(&cluster,
						       region_desc, 0);
		}
	}

	return err;
}

static errcode_t adjust_priority(struct hb_ctl_options *hbo)
{
	int ret, child_status;
	pid_t hb_pid, child_pid;
	char level_arg[16], pid_arg[16];

	if (access (IONICE_PATH, X_OK) != 0)
		return OCFS2_ET_NO_IONICE;

	ret = o2cb_get_hb_thread_pid (NULL, hbo->uuid_str, &hb_pid);
	if (ret != 0) 
		return ret;

	child_pid = fork ();
	if (child_pid == 0) {
		sprintf (level_arg, "-n%d", hbo->io_prio);
		sprintf (pid_arg, "-p%d", hb_pid);
		execlp (IONICE_PATH, "ionice", "-c1", level_arg, pid_arg, NULL);

		ret = errno;
		exit (ret);
	} else if (child_pid > 0) {
		ret = waitpid (child_pid, &child_status, 0);
		if (ret == 0)
			ret = WEXITSTATUS(child_status);
		else
			ret = errno;
	} else {
		ret = errno;
	}

	return ret;
}

static errcode_t stop_heartbeat(struct hb_ctl_options *hbo)
{
	errcode_t err = 0;
	struct o2cb_cluster_desc cluster = {
		.c_stack = NULL,  /* classic stack only */
	};

	if (!hbo->dev_str)
		err = lookup_dev(hbo);
	if (!err) {
		region_desc->r_persist = 1;  /* hb_ctl is for reals */
		region_desc->r_service = hbo->service;
		err = o2cb_group_leave(NULL, region_desc);
	}

	return err;
}

static errcode_t print_hb_ref_info(struct hb_ctl_options *hbo)
{
	errcode_t err;
	int num;

	err = o2cb_num_region_refs(hbo->uuid_str, &num);
	if (!err)
		printf("%s: %d refs\n", hbo->uuid_str, num);

	return err;
}

static int read_options(int argc, char **argv, struct hb_ctl_options *hbo)
{
	int c, ret;

	ret = 0;

	while(1) {
		c = getopt(argc, argv, "ISKPd:u:n:h");
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			hbo->action = HB_ACTION_USAGE;
			break;

		case 'K':
			hbo->action = HB_ACTION_STOP;
			break;

		case 'S':
			hbo->action = HB_ACTION_START;
			break;

		case 'P':
			hbo->action = HB_ACTION_IONICE;
			break;

		case 'd':
			if (optarg)
				hbo->dev_str = strdup(optarg);
			break;

		case 'u':
			if (optarg)
				hbo->uuid_str = strdup(optarg);
			break;

		case 'n':
			if (optarg)
				hbo->io_prio = atoi(optarg);
			break;

		case 'I':
			hbo->action = HB_ACTION_REFINFO;
			break;

		case '?':
		case ':':
		default:
			ret = -1;
			break;
		}
	}

	if (!ret && (optind < argc))
		hbo->service = strdup(argv[optind]);

	return ret;
}

static int process_options(struct hb_ctl_options *hbo)
{
	int ret = 0;

	switch (hbo->action) {
	case HB_ACTION_START:
		/* For start must specify exactly one of uuid or device. */
		if ((hbo->uuid_str && hbo->dev_str) ||
		    (!hbo->uuid_str && !hbo->dev_str) ||
		    !hbo->service)
			ret = -EINVAL;
		break;

	case HB_ACTION_STOP:
		/* For stop must specify exactly one of uuid or device. */
		if ((hbo->uuid_str && hbo->dev_str) ||
		    (!hbo->uuid_str && !hbo->dev_str) ||
		    !hbo->service)
			ret = -EINVAL;
		break;

	case HB_ACTION_REFINFO:
		/* Refinfo needs uuid or device */
		if ((hbo->uuid_str && hbo->dev_str) ||
		    (!hbo->uuid_str && !hbo->dev_str))
			ret = -EINVAL;
		break;

	case HB_ACTION_IONICE:
		/* ionice needs uuid and priority */
		if ((hbo->uuid_str && hbo->dev_str) ||
		    (!hbo->uuid_str && !hbo->dev_str) ||
		    hbo->io_prio < 0 || hbo->io_prio > 7)
			ret = -EINVAL;
		break;

	case HB_ACTION_UNKNOWN:
		ret = -EINVAL;
		break;

	case HB_ACTION_USAGE:
	default:
		break;
	}

	return ret;
}

static void print_usage(int err)
{
	FILE *output = err ? stderr : stdout;

	fprintf(output, "Usage: %s -S -d <device> <service>\n", progname);
	fprintf(output, "       %s -S -u <uuid> <service>\n", progname);
	fprintf(output, "       %s -K -d <device> <service>\n", progname);
	fprintf(output, "       %s -K -u <uuid> <service>\n", progname);
	fprintf(output, "       %s -I -d <device>\n", progname);
	fprintf(output, "       %s -I -u <uuid>\n", progname);
	fprintf(output, "       %s -P -d <device> [-n <io_priority>]\n", progname);
	fprintf(output, "       %s -P -u <uuid> [-n <io_priority>]\n", progname);
	fprintf(output, "       %s -h\n", progname);
}

int main(int argc, char **argv)
{
	errcode_t err = 0;
	int ret = 0;
	struct hb_ctl_options hbo = {
		.action = HB_ACTION_UNKNOWN,
	};
	char hbuuid[33];

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	initialize_ocfs_error_table();
	initialize_o2dl_error_table();
	initialize_o2cb_error_table();

	ret = read_options(argc, argv, &hbo);
	if (ret) {
		print_usage(1);
		goto bail;
	}

	ret = process_options(&hbo);
	if (ret) {
		print_usage(1);
		goto bail;
	}

	if (hbo.action == HB_ACTION_USAGE) {
		print_usage(0);
		goto bail;
	}

	err = o2cb_init();
	if (err) {
		com_err(progname, err, "Cannot initialize cluster\n");
		ret = -EINVAL;
		goto bail;
	}

	if (!hbo.uuid_str) {
		err = get_uuid(hbo.dev_str, hbuuid);
		if (err) {
			com_err(progname, err, "while reading uuid");
			ret = -EINVAL;
			goto bail;
		}

		hbo.uuid_str = hbuuid;
	}

	block_signals(SIG_BLOCK);

	switch(hbo.action) {
	case HB_ACTION_USAGE:
		ret = 0;
		print_usage(0);
		break;

	case HB_ACTION_START:
		err = start_heartbeat(&hbo);
		if (err) {
			com_err(progname, err, "while starting heartbeat");
			ret = -EINVAL;
		}
		break;

	case HB_ACTION_STOP:
		err = stop_heartbeat(&hbo);
		if (err) {
			com_err(progname, err, "while stopping heartbeat");
			ret = -EINVAL;
		}
		break;

	case HB_ACTION_IONICE:
		err = adjust_priority(&hbo);
		if (err) 
			ret = err;
		break;

	case HB_ACTION_REFINFO:
		err = print_hb_ref_info(&hbo);
		if (err) {
			com_err(progname, err, "while reading reference counts");
			ret = -EINVAL;
		}
		break;

	default:
		abort();
	}
	block_signals(SIG_UNBLOCK);

bail:
	free_desc();
	return ret ? 1 : 0;
}
