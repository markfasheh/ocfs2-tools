/*
 * mounted.c
 *
 * ocfs2 mount detect utility
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
#include <ocfs2_disk_dlm.h>
#include <ocfs1_fs_compat.h>
#include <kernel-list.h>

int detect_only = 0;
char *device = NULL;
char *progname = NULL;

#define MAX_DEVNAME_LEN		100

struct _ocfs2_devices {
	struct list_head list;
	char name[MAX_DEVNAME_LEN];
};
typedef struct _ocfs2_devices ocfs2_devices;

char *usage_string =
"usage: %s [-d] [device]\n"
"	-d detect only\n";

errcode_t ocfs2_full_detect(char *device);
errcode_t ocfs2_quick_detect(char *device);
void ocfs2_print_live_nodes(char **node_names, uint16_t num_nodes);
errcode_t ocfs2_partition_list (struct list_head *dev_list);
int ocfs2_get_ocfs1_label(char *device, char *buf, int buflen);
int read_options(int argc, char **argv);
void usage(char *progname);

/*
 * main()
 *
 */
int main(int argc, char **argv)
{
	errcode_t ret = 0;

	initialize_ocfs_error_table();

	ret = read_options(argc, argv);
	if (ret)
		goto bail;

	if (!detect_only && !device) {
		usage(progname);
		ret = 1;
		goto bail;
	}

	if (detect_only)
		ret = ocfs2_quick_detect(device);
	else
		ret = ocfs2_full_detect(device);

bail:
	return ret;
}

static void chb_notify(int state, char *progress, void *user_data)
{
    fprintf(stdout, "%s", progress);
    fflush(stdout);
}

/*
 * ocfs2_full_detect()
 *
 */
errcode_t ocfs2_full_detect(char *device)
{
	errcode_t ret = 0;
	int mount_flags = 0;
	char *node_names[OCFS2_NODE_MAP_MAX_NODES];
	ocfs2_filesys *fs = NULL;
	uint8_t *vol_label = NULL;
	uint8_t *vol_uuid = NULL;
	uint16_t num_nodes = OCFS2_NODE_MAP_MAX_NODES;
	int i;

	memset(node_names, 0, sizeof(node_names));

	/* open	fs */
	ret = ocfs2_open(device, O_DIRECT | OCFS2_FLAG_RO, 0, 0, &fs);
	if (ret) {
		if (ret == OCFS2_ET_OCFS_REV)
			fprintf(stderr, "Error: %s is an ocfs volume. "
				"Use mounted.ocfs to detect heartbeat on it.\n",
				device);
		else
			com_err(progname, ret, "while opening \"%s\"", device);
		goto bail;
	}

	num_nodes = OCFS2_RAW_SB(fs->fs_super)->s_max_nodes;
	vol_label = OCFS2_RAW_SB(fs->fs_super)->s_label;
	vol_uuid = OCFS2_RAW_SB(fs->fs_super)->s_uuid;

	printf("Device: %s\n", device);
	printf("Label : %s\n", vol_label);
	printf("Id    : ");
	for (i = 0; i < 16; i++)
		printf("%02X", vol_uuid[i]);
	printf("\n");

	if (detect_only)
		goto bail;
		
	ret = ocfs2_check_heartbeat(device, &mount_flags, node_names,
                                    chb_notify, NULL);
	if (ret) {
		com_err(progname, ret, "while detecting heartbeat");
		goto bail;
	}

	if (mount_flags & (OCFS2_MF_MOUNTED | OCFS2_MF_MOUNTED_CLUSTER)) {
		printf("Nodes :");
		ocfs2_print_live_nodes(node_names, num_nodes);
	} else {
		printf("Nodes : Not mounted\n");
		goto bail;
	}

bail:
	if (fs)
		ocfs2_close(fs);

	for (i = 0; i < num_nodes; ++i)
		if (node_names[i])
			ocfs2_free (&node_names[i]);

	return ret;
}

/*
 * ocfs2_quick_detect()
 *
 */
errcode_t ocfs2_quick_detect(char *device)
{
	errcode_t ret = 0;
	ocfs2_filesys *fs = NULL;
	uint8_t *vol_label = NULL;
	ocfs1_vol_label *v1_lbl = NULL;
	struct list_head dev_list;
	struct list_head *pos;
	ocfs2_devices *dev;
	char buf[512];

	INIT_LIST_HEAD(&dev_list);

	if (device) {
		ret = ocfs2_malloc0(sizeof(ocfs2_devices), &dev);
		if (ret)
			goto bail;
		strncpy(dev->name, device, MAX_DEVNAME_LEN);
		list_add(&(dev->list), &dev_list);
	} else {
		ret = ocfs2_partition_list(&dev_list);
		if (ret) {
			com_err(progname, ret, "while reading /proc/partitions");
			goto bail;
		}
	}

	printf("%-30s  %-6s  %-s\n", "Device", "Type", "Label");

	list_for_each(pos, &(dev_list)) {
		dev = list_entry(pos, ocfs2_devices, list);
		ret = ocfs2_open(dev->name, OCFS2_FLAG_RO, 0, 0, &fs);
		if (ret == 0 || ret == OCFS2_ET_OCFS_REV) {
			if (!ret)
				vol_label = OCFS2_RAW_SB(fs->fs_super)->s_label;
			else {
				if (!ocfs2_get_ocfs1_label(dev->name, buf, sizeof(buf))) {
					v1_lbl = (ocfs1_vol_label *)buf;
					vol_label = v1_lbl->label;
				} else
					vol_label = NULL;
			}
			printf("%-30s  %-6s  %-s\n", dev->name,
			       (!ret ? "ocfs2" : "ocfs"),
			       (vol_label ? (char *)vol_label : " "));
		}
		if (!ret)
			ocfs2_close(fs);
	}

bail:
	list_for_each(pos, &(dev_list)) {
		dev = list_entry(pos, ocfs2_devices, list);
		list_del(&(dev->list));
	}

	return 0;
}

/*
 * ocfs2_print_live_nodes()
 *
 */
void ocfs2_print_live_nodes(char **node_names, uint16_t num_nodes)
{
	int i;
	char comma = '\0';

	for (i = 0; i < num_nodes; ++i) {
		if (node_names[i]) {
			printf("%c %s", comma, node_names[i]);
			comma = ',';
		}
	}
	printf("\n");

	return ;
}

/*
 * ocfs2_partition_list()
 *
 */
errcode_t ocfs2_partition_list (struct list_head *dev_list)
{
	errcode_t ret = 0;
	FILE *proc;
	char line[256];
	char name[256];
	ocfs2_devices *dev;

	proc = fopen ("/proc/partitions", "r");
	if (proc == NULL) {
		ret = OCFS2_ET_IO;
		goto bail;
	}

	while (fgets (line, sizeof(line), proc) != NULL) {
		if (sscanf(line, "%*d %*d %*d %99[^ \t\n]", name) != 1)
			continue;

		ret = ocfs2_malloc0(sizeof(ocfs2_devices), &dev);
		if (ret)
			goto bail;

		snprintf(dev->name, MAX_DEVNAME_LEN, "/dev/%s", name);
		list_add_tail(&(dev->list), dev_list);
	}

bail:
	if (proc)
		fclose(proc);

	return ret;
}

/*
 * ocfs2_get_ocfs1_label()
 *
 */
int ocfs2_get_ocfs1_label(char *device, char *buf, int buflen)
{
	int fd = -1;
	int ret = -1;
	
	fd = open(device, O_RDONLY);
	if (fd == -1)
		goto bail;

	if (pread(fd, buf, buflen, 512) == -1)
		goto bail;

	ret = 0;
bail:
	if (fd > 0)
		close(fd);
	return ret;
}

/*
 * usage()
 *
 */
void usage(char *progname)
{
	printf(usage_string, progname);
	return ;
}

/*
 * read_options()
 *
 */
int read_options(int argc, char **argv)
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
		c = getopt(argc, argv, "d");
		if (c == -1)
			break;

		switch (c) {
		case 'd':	/* detect only */
			detect_only = 1;
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
