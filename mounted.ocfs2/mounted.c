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

errcode_t ocfs2_full_detect(char *device);
errcode_t ocfs2_quick_detect(char *device);
void ocfs2_print_live_nodes(char **node_names, uint16_t num_nodes);
int read_options(int argc, char **argv);
void usage(char *progname);
void ocfs2_partition_list (char **dev_list);
int ocfs2_get_ocfs1_label(char *device, char *buf, int buflen);

int detect_only = 0;
char *device = NULL;
char *progname = NULL;

char *usage_string =
"usage: %s [-d] [device]\n"
"	-d detect only\n";

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

/*
 * ocfs2_full_detect()
 *
 */
errcode_t ocfs2_full_detect(char *device)
{
	errcode_t ret = 0;
	int mount_flags = 0;
	char *node_names[OCFS2_NODE_MAP_MAX_NODES];
	int i;
	ocfs2_filesys *fs = NULL;
	uint8_t *vol_label = NULL;
	uint8_t *vol_uuid = NULL;
	uint16_t num_nodes = OCFS2_NODE_MAP_MAX_NODES;

	memset(node_names, 0, sizeof(node_names));

	/* open	fs */
	ret = ocfs2_open(device, O_DIRECT | OCFS2_FLAG_RO, 0, 0, &fs);
	if (ret) {
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
		
	ret = ocfs2_check_heartbeat(device, &mount_flags, node_names);
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
	char *dev_list[255];
	char *dev = NULL;
	uint8_t *vol_label = NULL;
	ocfs1_vol_label *v1_lbl = NULL;
	char buf[512];
	int i;

	memset(dev_list, 0 , sizeof(dev_list));

	if (device)
		dev_list[0] = strdup(device);
	else
		ocfs2_partition_list(dev_list);

	printf("%-20s  %-6s  %-s\n", "Device", "Type", "Label");

	for (i = 0; i < 255 && dev_list[i]; ++i) {
		dev = dev_list[i];
		ret = ocfs2_open(dev, OCFS2_FLAG_RO, 0, 0, &fs);
		if (ret == 0 || ret == OCFS2_ET_OCFS_REV) {
			if (!ret)
				vol_label = OCFS2_RAW_SB(fs->fs_super)->s_label;
			else {
				if (!ocfs2_get_ocfs1_label(dev, buf, sizeof(buf))) {
					v1_lbl = (ocfs1_vol_label *)buf;
					vol_label = v1_lbl->label;
				} else
					vol_label = NULL;
			}
			printf("%-20s  %-6s  %-s\n", dev,
			       (!ret ? "ocfs2" : "ocfs"),
			       (vol_label ? (char *)vol_label : " "));
		}
		if (!ret)
			ocfs2_close(fs);
	}

	for (i = 0; i < 255; ++i)
		if (dev_list[i])
			ocfs2_free(&dev_list[i]);

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
void ocfs2_partition_list (char **dev_list)
{
	FILE   *proc;
	char   line[100], name[100], device[255];
	int cnt = 0;

	proc = fopen ("/proc/partitions", "r");
	if (proc == NULL)
		return;

	while (fgets (line, sizeof(line), proc) != NULL) {
		if (sscanf(line, "%*d %*d %*d %99[^ \t\n]", name) != 1)
			continue;

		snprintf(device, sizeof(device), "/dev/%s", name);
		dev_list[cnt++] = strdup(device);
	}

	fclose (proc);

	return ;
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

	if (argc < 2) {
		usage(argv[0]);
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
