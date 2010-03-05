/*
 * mounted.c
 *
 * ocfs2 mount detect utility
 *
 * Copyright (C) 2004, 2005 Oracle.  All rights reserved.
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
#include <dirent.h>
#include <ctype.h>

#include <uuid/uuid.h>

#include "ocfs2-kernel/kernel-list.h"
#include "ocfs2/ocfs2.h"

static int quick_detect = 0;
static char *device = NULL;
static char *progname = NULL;

static char *usage_string =
"usage: %s [-d] [-f] [device]\n"
"	-d quick detect\n"
"	-f full detect\n";

static void ocfs2_print_nodes(ocfs2_devices *dev, char **names,
				unsigned int length_of_names)
{
	int i, start = 1;
	unsigned int node_num;
        struct ocfs2_slot_map_data *map = dev->map;

	for (i = 0; i < map->md_num_slots; i++) {
		if (!map->md_slots[i].sd_valid)
			continue;

		if (start)
			start = 0;
		else
			printf(", ");

		node_num = map->md_slots[i].sd_node_num;

		if (node_num >= length_of_names)
			printf("Unknown");
		else if (names && names[node_num] && *(names[node_num]))
			printf("%s", names[node_num]);
		else
			printf("%d", node_num);
	}
}


static void ocfs2_print_full_detect(struct list_head *dev_list)
{
	ocfs2_devices *dev;
	struct list_head *pos;
	char **node_names = NULL;
	char **cluster_names = NULL;
	char *nodes[O2NM_MAX_NODES];
	int i = 0;
	uint16_t num;

	memset(nodes, 0, sizeof(nodes));

	o2cb_list_clusters(&cluster_names);

	if (cluster_names && *cluster_names) {
		o2cb_list_nodes(*cluster_names, &node_names);

		/* sort the names according to the node number */
		while(node_names && node_names[i] && *(node_names[i])) {
			if (o2cb_get_node_num(*cluster_names, node_names[i],
					      &num))
				break;
			if (num >= O2NM_MAX_NODES)
				break;
			nodes[num] = node_names[i];
			++i;
		}
	}

	printf("%-20s  %-5s  %s\n", "Device", "FS", "Nodes");
	list_for_each(pos, dev_list) {
		dev = list_entry(pos, ocfs2_devices, list);
		if (dev->fs_type == 0)
			continue;

		printf("%-20s  %-5s  ", dev->dev_name, "ocfs2");

		if (dev->errcode) {
			fflush(stdout);
			com_err("Unknown", dev->errcode, " ");
		} else {
			if (dev->hb_dev)
				printf("Heartbeat device");
			else if (dev->mount_flags & OCFS2_MF_MOUNTED_CLUSTER)
				ocfs2_print_nodes(dev, nodes, O2NM_MAX_NODES);
			else
				printf("Not mounted");
			printf("\n");
		}
	}

	if (node_names)
		o2cb_free_nodes_list(node_names);

	if (cluster_names)
		o2cb_free_cluster_list(cluster_names);
}


static void ocfs2_print_quick_detect(struct list_head *dev_list)
{
	ocfs2_devices *dev;
	struct list_head *pos;
	char uuid[OCFS2_VOL_UUID_LEN * 2 + 1];
	int i;
	char *p;

	printf("%-20s  %-5s  %-32s  %-s\n", "Device", "FS", "UUID", "Label");
	list_for_each(pos, dev_list) {
		dev = list_entry(pos, ocfs2_devices, list);
		if (dev->fs_type == 0)
			continue;

		for (i = 0, p = uuid; i < OCFS2_VOL_UUID_LEN; i++) {
			snprintf(p, 3, "%02X", dev->uuid[i]);
			p += 2;
		}

		printf("%-20s  %-5s  %-32s  %-s\n", dev->dev_name,
		       (dev->fs_type == 2 ? "ocfs2" : "ocfs"), uuid,
		       dev->label);
	}
}

static void scan_dir_for_dev(char *dirname, dev_t devno, char **devname)
{
	DIR *dir;
	struct dirent *dp;
	char path[PATH_MAX];
	int dirlen;
	struct stat st;

	dir = opendir(dirname);
	if (dir == NULL)
		return;
	dirlen = strlen(dirname) + 2;
	while ((dp = readdir(dir)) != 0) {
		if (dirlen + strlen(dp->d_name) >= sizeof(path))
			continue;

		if (dp->d_name[0] == '.' &&
		    ((dp->d_name[1] == 0) ||
		     ((dp->d_name[1] == '.') && (dp->d_name[2] == 0))))
			continue;

		sprintf(path, "%s/%s", dirname, dp->d_name);
		if (stat(path, &st) < 0)
			continue;

		if (S_ISBLK(st.st_mode) && st.st_rdev == devno) {
			*devname = strdup(path);
			break;
		}
	}
	closedir(dir);
	return;
}

static errcode_t ocfs2_partition_list (struct list_head *dev_list)
{
	errcode_t ret = 0;
	FILE *proc;
	char line[256];
	char name[256];
	char *devname = NULL;
	int major, minor;
	ocfs2_devices *dev;

	proc = fopen ("/proc/partitions", "r");
	if (proc == NULL) {
		ret = OCFS2_ET_IO;
		goto bail;
	}

	while (fgets (line, sizeof(line), proc) != NULL) {
		if (sscanf(line, "%d %d %*d %99[^ \t\n]",
			   &major, &minor, name) != 3)
			continue;

		ret = ocfs2_malloc0(sizeof(ocfs2_devices), &dev);
		if (ret)
			goto bail;

		/* Try to translate private device-mapper dm-<N> names
		 * to standard /dev/mapper/<name>.
		 */
		if (!strncmp(name, "dm-", 3) && isdigit(name[3])) {
			devname = NULL;
			scan_dir_for_dev("/dev/mapper",
					 makedev(major, minor), &devname);
			if (devname) {
				snprintf(dev->dev_name, sizeof(dev->dev_name),
					 "%s", devname);
				free(devname);
			} else
				snprintf(dev->dev_name, sizeof(dev->dev_name),
					 "/dev/%s", name);

		} else {
			snprintf(dev->dev_name, sizeof(dev->dev_name),
				 "/dev/%s", name);
		}

		list_add_tail(&(dev->list), dev_list);
	}

bail:
	if (proc)
		fclose(proc);

	return ret;
}


static void usage(char *progname)
{
	printf(usage_string, progname);
}


static int read_options(int argc, char **argv)
{
	int ret = 0;
	int c;

	progname = argv[0];

	if (argc < 2) {
		usage(progname);
		ret = 1;					  
		goto bail;
	}

	while(1) {
		c = getopt(argc, argv, "df");
		if (c == -1)
			break;

		switch (c) {
		case 'd':	/* quick detect*/
			quick_detect = 1;
			break;

		case 'f':	/* full detect*/
			quick_detect = 0;
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


static errcode_t ocfs2_detect(char *device, int quick_detect)
{
	errcode_t ret = 0;
	struct list_head dev_list;
	struct list_head *pos1, *pos2;
	ocfs2_devices *dev;

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

	ret = ocfs2_check_heartbeats(&dev_list, 1);
	if (ret) {
		com_err(progname, ret, "while detecting heartbeat");
		goto bail;
	}

	if (quick_detect)
		ocfs2_print_quick_detect(&dev_list);
	else
		ocfs2_print_full_detect(&dev_list);

bail:
	list_for_each_safe(pos1, pos2, &(dev_list)) {
		dev = list_entry(pos1, ocfs2_devices, list);
		if (dev->map)
			ocfs2_free(&dev->map);
		list_del(&(dev->list));
		ocfs2_free(&dev);
	}

	return ret;
}


int main(int argc, char **argv)
{
	errcode_t ret = 0;

	initialize_ocfs_error_table();
	initialize_o2dl_error_table();
	initialize_o2cb_error_table();

	ret = read_options(argc, argv);
	if (ret)
		goto bail;

	o2cb_init();

	ret = ocfs2_detect(device, quick_detect);

bail:
	return ret;
}
