/*
 * mounted.c
 *
 * ocfs2 mount detect utility
 * Detects both ocfs and ocfs2 volumes
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

int quick_detect = 0;
char *device = NULL;
char *progname = NULL;

char *usage_string =
"usage: %s [-d] [-f] [device]\n"
"	-d quick detect\n"
"	-f full detect\n";

errcode_t ocfs2_detect(char *device, int quick_detect);
void ocfs2_print_quick_detect(struct list_head *dev_list);
void ocfs2_print_full_detect(struct list_head *dev_list);
void ocfs2_print_nodes(struct list_head *node_list);
errcode_t ocfs2_partition_list (struct list_head *dev_list);
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

	ret = ocfs2_detect(device, quick_detect);

bail:
	return ret;
}

static void chb_notify(int state, char *progress, void *user_data)
{
    fprintf(stdout, "%s", progress);
    fflush(stdout);
}

/*
 * ocfs2_detect()
 *
 */
errcode_t ocfs2_detect(char *device, int quick_detect)
{
	errcode_t ret = 0;
	struct list_head dev_list;
	struct list_head *pos1, *pos2, *pos3, *pos4;
	ocfs2_nodes *node;
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

	ret = ocfs2_check_heartbeats(&dev_list, quick_detect, chb_notify, NULL);
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
		list_for_each_safe(pos3, pos4, &(dev->node_list)) {
			node = list_entry(pos3, ocfs2_nodes, list);
			list_del(&(node->list));
			ocfs2_free(&node);
		}
		list_del(&(dev->list));
		ocfs2_free(&dev);
	}

	return ret;
}

/*
 * ocfs2_print_full_detect()
 *
 */
void ocfs2_print_full_detect(struct list_head *dev_list)
{
	ocfs2_devices *dev;
	struct list_head *pos;

	printf("%-20s  %-5s  %s\n", "Device", "FS", "Nodes");
	list_for_each(pos, dev_list) {
		dev = list_entry(pos, ocfs2_devices, list);
		if (dev->fs_type == 0)
			continue;
		printf("%-20s  %-5s  ", dev->dev_name,
		       (dev->fs_type == 2 ? "ocfs2" : "ocfs"));
		if (list_empty(&(dev->node_list))) {
			printf("Not mounted\n");
			continue;
		}
		ocfs2_print_nodes(&(dev->node_list));
		printf("\n");
	}
	return ;
}


/*
 * ocfs2_print_quick_detect()
 *
 */
void ocfs2_print_quick_detect(struct list_head *dev_list)
{
	ocfs2_devices *dev;
	struct list_head *pos;
	char uuid[40];
	char *p;
	int i;

	printf("%-20s  %-5s  %-32s  %-s\n", "Device", "FS", "GUID", "Label");
	list_for_each(pos, dev_list) {
		dev = list_entry(pos, ocfs2_devices, list);
		if (dev->fs_type == 0)
			continue;

		memset(uuid, 0, sizeof(uuid));		
		for (i = 0, p = uuid; i < 16; i++, p += 2)
			sprintf(p, "%02X", dev->uuid[i]);

		printf("%-20s  %-5s  %-32s  %-s\n", dev->dev_name,
		       (dev->fs_type == 2 ? "ocfs2" : "ocfs"), uuid,
		       dev->label);
	}

	return ;
}

/*
 * ocfs2_print_nodes()
 *
 */
void ocfs2_print_nodes(struct list_head *node_list)
{
	ocfs2_nodes *node;
	struct list_head *pos;
	int begin = 1;

	list_for_each(pos, node_list) {
		node = list_entry(pos, ocfs2_nodes, list);
		if (begin) {
			printf("%s", node->node_name);
			begin = 0;
		}  else
			printf(", %s", node->node_name);
	}

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

		snprintf(dev->dev_name, sizeof(dev->dev_name), "/dev/%s", name);
		list_add_tail(&(dev->list), dev_list);
	}

bail:
	if (proc)
		fclose(proc);

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
