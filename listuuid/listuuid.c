/*
 * listuuid.c
 *
 * Lists UUIDs of all the devices
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
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
 * Authors: Sunil Mushran
 */

#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE /* Because libc really doesn't want us using O_DIRECT? */

#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fd.h>
#include <string.h>
#include <sys/stat.h>

#define  OCFS2_FLAT_INCLUDES	1
#include <ocfs2.h>
#include <ocfs2_fs.h>
#include <ocfs1_fs_compat.h>
#include <kernel-list.h>

char *device = NULL;
char *progname = NULL;
int all_devices = 0;

char *usage_string =
"usage: %s [-a] [device]\n";

/*
 * ocfs2_partition_list()
 *
 */
static errcode_t ocfs2_partition_list (struct list_head *dev_list)
{
	errcode_t ret = 0;
	FILE *proc;
	char line[256];
	char name[256];
	char major[256];
	char minor[256];
	ocfs2_devices *dev;

	proc = fopen ("/proc/partitions", "r");
	if (proc == NULL) {
		ret = OCFS2_ET_IO;
		goto bail;
	}

	while (fgets (line, sizeof(line), proc) != NULL) {
		*major = *minor = *name = '\0';
		if (sscanf(line, "%*[ ]%[0-9]%*[ ]%[0-9] %*d %99[^ \t\n]",
			   major, minor, name) != 3)
			continue;

		ret = ocfs2_malloc0(sizeof(ocfs2_devices), &dev);
		if (ret)
			goto bail;

		snprintf(dev->dev_name, sizeof(dev->dev_name), "/dev/%s", name);
		dev->maj_num = strtoul(major, NULL, 0);
		dev->min_num = strtoul(minor, NULL, 0);
		list_add_tail(&(dev->list), dev_list);
	}

bail:
	if (proc)
		fclose(proc);

	return ret;
}

/*
 * ocfs2_print_uuids()
 *
 */
static void ocfs2_print_uuids(struct list_head *dev_list)
{
	ocfs2_devices *dev;
	struct list_head *pos;
	char uuid[40];
	char devstr[10];
	char *p;
	int i;

	printf("%-20s  %7s  %-32s\n", "Device", "maj,min", "UUID");
	list_for_each(pos, dev_list) {
		dev = list_entry(pos, ocfs2_devices, list);
		if (dev->fs_type == 0)
			continue;

		memset(uuid, 0, sizeof(uuid));
		for (i = 0, p = uuid; i < 16; i++, p += 2)
			sprintf(p, "%02X", dev->uuid[i]);

		sprintf(devstr, "%3d,%-d", dev->maj_num, dev->min_num);
		printf("%-20s  %-7s  %-32s\n", dev->dev_name, devstr, uuid);
	}

	return ;
}

/*
 * ocfs2_detect()
 *
 */
static errcode_t ocfs2_detect(char *device)
{
	errcode_t ret = 0;
	struct list_head dev_list;
	struct list_head *pos1, *pos2;
	ocfs2_devices *dev;
	ocfs2_filesys *fs = NULL;
	char *dev_name;

	INIT_LIST_HEAD(&(dev_list));

	if (device) {
		ret = ocfs2_malloc0(sizeof(ocfs2_devices), &dev);
		if (ret)
			goto bail;
		strncpy(dev->dev_name, device, sizeof(dev->dev_name));
		list_add(&(dev->list), &dev_list);
	} else {
		ret = ocfs2_partition_list(&dev_list);
		if (ret) {
			com_err(progname, ret, "while reading /proc/partitions");
			goto bail;
		}
	}

	list_for_each(pos1, &dev_list) {
		dev = list_entry(pos1, ocfs2_devices, list);
		dev_name = dev->dev_name;

		/* open	fs */
		fs = NULL;
		ret = ocfs2_open(dev_name, OCFS2_FLAG_RO, 0, 0, &fs);
		if (ret) {
			dev->fs_type = 0;
			ret = 0;
			continue;
		} else
			dev->fs_type = 2;

		/* get uuid for ocfs2 */
		if (dev->fs_type == 2) {
			memcpy(dev->uuid, OCFS2_RAW_SB(fs->fs_super)->s_uuid,
			       sizeof(dev->uuid));
		}

		/* close fs */
		ocfs2_close(fs);
	}

	ocfs2_print_uuids(&dev_list);

bail:

	list_for_each_safe(pos1, pos2, &(dev_list)) {
		dev = list_entry(pos1, ocfs2_devices, list);
		list_del(&(dev->list));
		ocfs2_free(&dev);
	}

	return ret;
}

/*
 * usage()
 *
 */
static void usage(char *progname)
{
	printf(usage_string, progname);
	return ;
}

/*
 * read_options()
 *
 */
static int read_options(int argc, char **argv)
{
	int ret = 0;
	int c;

	progname = basename(argv[0]);

	if (argc < 2) {
		usage(progname);
		ret = 1;
		goto bail;
	}

	while(1) {
		c = getopt(argc, argv, "a");
		if (c == -1)
			break;

		switch (c) {
		case 'a':	/* all devices */
			all_devices = 1;
			break;

		default:
			break;
		}
	}

	if (!ret && optind < argc && argv[optind])
		device = argv[optind];

bail:
	return ret;
}

/*
 * main()
 *
 */
int main(int argc, char **argv)
{
	errcode_t ret = 0;

	initialize_ocfs_error_table();

	ret = read_options (argc, argv);
	if (ret)
		goto bail;

	ret = ocfs2_detect(device);

bail:
	return ret;
}
