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
#include <inttypes.h>

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <signal.h>

#include <ocfs2.h>
#include <ocfs2_fs.h>

#include "o2cb.h"

#define DEV_PREFIX      "/dev/"
#define PROC_IDE_FORMAT "/proc/ide/%s/media"

enum hb_ctl_action {
	HB_ACTION_UNKNOWN,
	HB_ACTION_USAGE,
	HB_ACTION_VERSION,
	HB_ACTION_START,
	HB_ACTION_STOP,
	HB_ACTION_REFINFO,
	HB_ACTION_LIST,
};

struct hb_ctl_options {
	enum hb_ctl_action action;
	int query;
	char *dev_str;
	char *uuid_str;
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

	if (!hbo->dev_str)
		err = lookup_dev(hbo);
	if (!err) {
		err = o2cb_start_heartbeat_region_perm(NULL,
						       region_desc);
	}

	return err;
}

static errcode_t stop_heartbeat(struct hb_ctl_options *hbo)
{
	errcode_t err;

	err = o2cb_stop_heartbeat_region_perm(NULL, hbo->uuid_str);

	return err;
}

static errcode_t print_hb_ref_info(struct hb_ctl_options *hbo)
{
	errcode_t err;
	int num;

	err = o2cb_num_region_refs(hbo->uuid_str, &num);
	if (!err)
	{
		fprintf(stdout, "%s: %d refs", hbo->uuid_str, num);
		if (hbo->query && num)
			fprintf(stdout, " (live)");
		fprintf(stdout, "\n");
	}

	return err;
}

static errcode_t list_dev(const char *dev,
			  struct hb_ctl_options *hbo)
{
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
	if (get_desc(device))
		goto out;

	fprintf(stdout, "%s:%s\n", region_desc->r_name, device);

	free_desc();

out:
	free(device);

	/* Always return NOT_FOUND, which means continue */
	return OCFS2_ET_FILE_NOT_FOUND;
}

static int run_list(struct hb_ctl_options *hbo)
{
	int ret = 0;
	errcode_t err;
	char hbuuid[33];

	if (hbo->dev_str) {
		err = get_uuid(hbo->dev_str, hbuuid);
		if (err) {
			com_err(progname, err,
				"while reading uuid from device \"%s\"",
				hbo->dev_str);
			ret = -EINVAL;
		} else {
			fprintf(stdout, "%s\n", hbuuid);
		}
	} else {
		err = scan_devices(list_dev, hbo);
		if (err && (err != OCFS2_ET_FILE_NOT_FOUND)) {
			com_err(progname, err,
				"while listing devices");
			ret = -EIO;
		}
	}

	return ret;
}

static void print_version(void)
{
	fprintf(stdout, "%s: version %s\n", progname, VERSION);
}

static int read_options(int argc, char **argv, struct hb_ctl_options *hbo)
{
	int c, ret;

	ret = 0;

	while(1) {
		c = getopt(argc, argv, "ISKLqd:u:hV-:");
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			hbo->action = HB_ACTION_USAGE;
			break;

		case 'V':
			hbo->action = HB_ACTION_VERSION;
			break;

		case '-':
			if (!strcmp(optarg, "version"))
				hbo->action = HB_ACTION_VERSION;
			else if (!strcmp(optarg, "help"))
				hbo->action = HB_ACTION_USAGE;
			else
			{
				fprintf(stderr,
					"%s: Invalid option: \'--%s\'\n",
					progname, optarg);
				ret = -1;
			}
			break;

		case 'K':
			hbo->action = HB_ACTION_STOP;
			break;

		case 'S':
			hbo->action = HB_ACTION_START;
			break;

		case 'L':
			hbo->action = HB_ACTION_LIST;
			break;

		case 'q':
			hbo->query = 1;
			break;

		case 'd':
			if (optarg)
				hbo->dev_str = strdup(optarg);
			break;

		case 'u':
			if (optarg)
				hbo->uuid_str = strdup(optarg);
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

	return ret;
}

static int process_options(struct hb_ctl_options *hbo)
{
	int ret = 0;

	switch (hbo->action) {
	case HB_ACTION_START:
		/* For start must specify exactly one of uuid or device. */
		if ((hbo->uuid_str && hbo->dev_str) ||
		    (!hbo->uuid_str && !hbo->dev_str))
			ret = -EINVAL;
		break;

	case HB_ACTION_STOP:
		/* For stop must specify exactly one of uuid or device. */
		if ((hbo->uuid_str && hbo->dev_str) ||
		    (!hbo->uuid_str && !hbo->dev_str))
			ret = -EINVAL;
		break;

	case HB_ACTION_REFINFO:
		/* Refinfo needs uuid or device */
		if ((hbo->uuid_str && hbo->dev_str) ||
		    (!hbo->uuid_str && !hbo->dev_str))
			ret = -EINVAL;
		break;

	case HB_ACTION_LIST:
		if (hbo->uuid_str)
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

	fprintf(output, "Usage: %s -S -d <device>\n", progname);
	fprintf(output, "       %s -S -u <uuid>\n", progname);
	fprintf(output, "       %s -K -d <device>\n", progname);
	fprintf(output, "       %s -K -u <uuid>\n", progname);
	fprintf(output, "       %s -I -d <device>\n", progname);
	fprintf(output, "       %s -I -u <uuid>\n", progname);
	fprintf(output, "       %s -L [-d <device>]\n", progname);
	fprintf(output, "       %s -h\n", progname);
	fprintf(output, "       %s -V\n", progname);
}

int main(int argc, char **argv)
{
	errcode_t err = 0;
	int ret = 0;
	char hbuuid[33];
	struct hb_ctl_options hbo = {
		HB_ACTION_UNKNOWN, 0, NULL, NULL
	};

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

	if (hbo.action == HB_ACTION_VERSION) {
		print_version();
		goto bail;
	}

	if (hbo.action == HB_ACTION_LIST) {
		ret = run_list(&hbo);
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

	case HB_ACTION_VERSION:
		ret = 0;
		print_version();
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
