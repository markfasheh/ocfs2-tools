/*
 * mounted.c
 *
 * ocfs2 mount detect utility
 *
 * Copyright (C) 2004, 2011 Oracle.  All rights reserved.
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301 USA.
 *
 */

#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE /* Because libc really doesn't want us using O_DIRECT? */

#include <sys/sysmacros.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fd.h>
#include <linux/major.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>

#include <uuid/uuid.h>

#include "ocfs2-kernel/kernel-list.h"
#include "ocfs2/ocfs2.h"
#include "ocfs2/byteorder.h"
#include "tools-internal/verbose.h"

#undef max
#define max(a,b)	((a) > (b) ? (a) : (b))
#undef min
#define min(a,b)	((a) < (b) ? (a) : (b))

static int quick_detect = 1; /* default */
static char *device = NULL;
static char *progname = NULL;

static char *usage_string =
"usage: %s [-dfv] [device]\n"
"	-d quick detect\n"
"	-f full detect\n"
"	-v verbose\n";

static void get_max_widths(struct list_head *dev_list, int *dev_width,
			   int *cluster_width)
{
	ocfs2_devices *dev;
	struct list_head *pos;

	list_for_each(pos, dev_list) {
		dev = list_entry(pos, ocfs2_devices, list);
		if (dev->fs_type != 2)
			continue;
		*dev_width = max(*dev_width, strlen(dev->dev_name));
		*cluster_width = max(*cluster_width, strlen(dev->cluster));
	}
}

static void print_nodes(ocfs2_devices *dev, char **names,
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


static void print_full_detect(struct list_head *dev_list)
{
	ocfs2_devices *dev;
	struct list_head *pos;
	char **node_names = NULL;
	char **cluster_names = NULL;
	char *nodes[O2NM_MAX_NODES];
	char flag;
	int i = 0, dev_width = 7, cluster_width = 7;
	uint16_t num;

	get_max_widths(dev_list, &dev_width, &cluster_width);

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

	printf("%-*s  %-5s  %-*s  %c  %-s\n", dev_width, "Device", "Stack",
	       cluster_width, "Cluster", 'F', "Nodes");

	list_for_each(pos, dev_list) {
		dev = list_entry(pos, ocfs2_devices, list);
		if (dev->fs_type != 2)
			continue;

		flag = ' ';
		if (!strcmp(dev->stack, OCFS2_CLASSIC_CLUSTER_STACK)) {
			if (dev->stackflags &
			    OCFS2_CLUSTER_O2CB_GLOBAL_HEARTBEAT)
				flag = 'G';
		}

		printf("%-*s  %-5s  %-*s  %c  ", dev_width, dev->dev_name,
		       dev->stack, cluster_width, dev->cluster, flag);

		if (dev->errcode) {
			fflush(stdout);
			com_err("Unknown", dev->errcode, " ");
		} else {
			if (dev->hb_dev)
				printf("Heartbeat device");
			else if (dev->mount_flags & OCFS2_MF_MOUNTED_CLUSTER)
				print_nodes(dev, nodes, O2NM_MAX_NODES);
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

static void print_quick_detect(struct list_head *dev_list)
{
	ocfs2_devices *dev;
	struct list_head *pos;
	char uuid[OCFS2_VOL_UUID_LEN * 2 + 1];
	int i, dev_width = 7, cluster_width = 7;
	char *p, flag;

	get_max_widths(dev_list, &dev_width, &cluster_width);

	printf("%-*s  %-5s  %-*s  %c  %-32s  %-s\n", dev_width, "Device",
	       "Stack", cluster_width, "Cluster", 'F', "UUID", "Label");

	list_for_each(pos, dev_list) {
		dev = list_entry(pos, ocfs2_devices, list);
		if (dev->fs_type != 2)
			continue;

		for (i = 0, p = uuid; i < OCFS2_VOL_UUID_LEN; i++) {
			snprintf(p, 3, "%02X", dev->uuid[i]);
			p += 2;
		}

		flag = ' ';
		if (!strcmp(dev->stack, OCFS2_CLASSIC_CLUSTER_STACK)) {
			if (dev->stackflags &
			    OCFS2_CLUSTER_O2CB_GLOBAL_HEARTBEAT)
				flag = 'G';
		}

		printf("%-*s  %-5s  %-*s  %c  %-32s  %-s\n", dev_width,
		       dev->dev_name, dev->stack, cluster_width, dev->cluster,
		       flag, uuid, dev->label);
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

static void free_partition_list(struct list_head *dev_list)
{
	struct list_head *pos1, *pos2;
	ocfs2_devices *dev;

	list_for_each_safe(pos1, pos2, dev_list) {
		dev = list_entry(pos1, ocfs2_devices, list);
		if (dev->map)
			ocfs2_free(&dev->map);
		list_del(&(dev->list));
		ocfs2_free(&dev);
	}
}

static void list_rm_device(struct list_head *dev_list, int major, int minor)
{
	struct list_head *pos1, *pos2;
	ocfs2_devices *dev;

	list_for_each_safe(pos1, pos2, dev_list) {
		dev = list_entry(pos1, ocfs2_devices, list);
		if ((dev->maj_num == major) && (dev->min_num == minor)) {
			if (dev->map)
				ocfs2_free(&dev->map);
			list_del(&(dev->list));
			ocfs2_free(&dev);
		}
	}
}

static int is_partition(int major, int minor)
{
	char path[PATH_MAX + 1];
	struct stat info;

	snprintf(path, sizeof(path), "/sys/dev/block/%d:%d/partition",
			major, minor);

	return !stat(path, &info);
}

static int find_whole_disk_minor(int major, int minor) {
#ifndef SCSI_BLK_MAJOR
#ifdef SCSI_DISK0_MAJOR
#ifdef SCSI_DISK8_MAJOR
#define SCSI_DISK_MAJOR(M) ((M) == SCSI_DISK0_MAJOR || \
		  ((M) >= SCSI_DISK1_MAJOR && (M) <= SCSI_DISK7_MAJOR) || \
		  ((M) >= SCSI_DISK8_MAJOR && (M) <= SCSI_DISK15_MAJOR))
#else
#define SCSI_DISK_MAJOR(M) ((M) == SCSI_DISK0_MAJOR || \
		  ((M) >= SCSI_DISK1_MAJOR && (M) <= SCSI_DISK7_MAJOR))
#endif /* defined(SCSI_DISK8_MAJOR) */
#define SCSI_BLK_MAJOR(M) (SCSI_DISK_MAJOR((M)) || (M) == SCSI_CDROM_MAJOR)
#else
#define SCSI_BLK_MAJOR(M)  ((M) == SCSI_DISK_MAJOR || (M) == SCSI_CDROM_MAJOR)
#endif /* defined(SCSI_DISK0_MAJOR) */
#endif /* defined(SCSI_BLK_MAJOR) */
	if (major == HD_MAJOR)
		return (minor - (minor%64));

	if (SCSI_BLK_MAJOR(major))
	       return (minor - (minor%16));
	/* FIXME: Catch all */
	return 0;
}

static errcode_t build_partition_list(struct list_head *dev_list, char *device)
{
	errcode_t ret = 0;
	FILE *proc = NULL;
	char line[512];
	char name[512];
	char *devname = NULL;
	int major, minor;
	ocfs2_devices *dev;
	uint64_t numblocks;

	if (device) {
		ret = ocfs2_malloc0(sizeof(ocfs2_devices), &dev);
		if (ret)
			goto bail;
		strncpy(dev->dev_name, device, sizeof(dev->dev_name));
		list_add(&(dev->list), dev_list);
		return 0;
	}

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

		/*
		 * Try to translate private device-mapper dm-<N> names
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

		/* skip devices smaller than 1M */

		if (ocfs2_get_device_size(dev->dev_name, 4096, &numblocks)) {
			verbosef(VL_DEBUG, "Unable to get size of %s\n",
				 dev->dev_name);
			ocfs2_free(&dev);
			continue;
		}


		if (numblocks <= (1024 * 1024 / 4096)) {
			verbosef(VL_DEBUG, "Skipping small device %s\n",
				 dev->dev_name);
			ocfs2_free(&dev);
			continue;
		}

		if (is_partition(major, minor)) {
			int whole_minor = find_whole_disk_minor(major, minor);
			list_rm_device(dev_list, major, whole_minor);
		}

		dev->maj_num = major;
		dev->min_num = minor;

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
		c = getopt(argc, argv, "dfv");
		if (c == -1)
			break;

		switch (c) {
		case 'd':	/* quick detect*/
			quick_detect = 1;
			break;

		case 'f':	/* full detect*/
			quick_detect = 0;
			break;

		case 'v':
			tools_verbose();
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

static ssize_t do_pread(int fd, void *buf, size_t count, off_t offset)
{
	ssize_t rd = 0, ret;

	while (1) {
		ret = pread(fd, buf + rd, count - rd, offset + rd);
		if (ret > 0)
			rd += ret;
		if (ret <= 0 || rd == count)
			break;
	}

	if (rd)
		return rd;
	return ret;
}

static void populate_sb_info(ocfs2_devices *dev, struct ocfs2_super_block *sb)
{
	uint32_t incompat;

	if (!sb)
		return;

	dev->fs_type = 2;

	memcpy(dev->label, sb->s_label, sizeof(dev->label));
	memcpy(dev->uuid, sb->s_uuid, sizeof(dev->uuid));

	incompat = le32_to_cpu(sb->s_feature_incompat);

	memcpy(dev->label, sb->s_label, sizeof(dev->label));
	memcpy(dev->uuid, sb->s_uuid, sizeof(dev->uuid));

#define CLUSTERINFO_VALID	(OCFS2_FEATURE_INCOMPAT_USERSPACE_STACK | \
				 OCFS2_FEATURE_INCOMPAT_CLUSTERINFO)

	if (incompat & OCFS2_FEATURE_INCOMPAT_LOCAL_MOUNT)
		snprintf(dev->stack, sizeof(dev->stack), "%s", "None");
	else if (incompat & CLUSTERINFO_VALID) {
		snprintf(dev->stack, sizeof(dev->stack), "%.*s",
			 OCFS2_STACK_LABEL_LEN, sb->s_cluster_info.ci_stack);
		snprintf(dev->cluster, sizeof(dev->cluster), "%.*s",
			 OCFS2_CLUSTER_NAME_LEN, sb->s_cluster_info.ci_cluster);
		dev->stackflags = sb->s_cluster_info.ci_stackflags;
	} else
		snprintf(dev->stack, sizeof(dev->stack), "%s",
			 OCFS2_CLASSIC_CLUSTER_STACK);
}

static void do_quick_detect(struct list_head *dev_list)
{
	int fd = -1, ret;
	char buf[512];
	struct ocfs2_dinode *di;
	uint32_t offset;
	struct list_head *pos;
	ocfs2_devices *dev;

	list_for_each(pos, dev_list) {
		dev = list_entry(pos, ocfs2_devices, list);

		verbosef(VL_APP, "Probing device %s\n", dev->dev_name);

		fd = open(dev->dev_name, O_RDONLY);
		if (fd < 0) {
			verbosef(VL_DEBUG, "Device %s open failed with '%s'\n",
			 	dev->dev_name, strerror(errno));
			continue;
		}

		/* ignore error but log if in verbose */
		ret = posix_fadvise(fd, 0, (1024 * 1024), POSIX_FADV_DONTNEED);
		if (ret < 0) {
			verbosef(VL_DEBUG, "Buffer cache free for device %s "
				 "failed with '%s'\n", dev->dev_name,
				 strerror(errno));
		}

		for (offset = 1; offset <= 8; offset <<= 1) {
			ret = do_pread(fd, buf, sizeof(buf), (offset * 1024));
			if (ret < sizeof(buf))
				break;
			di = (struct ocfs2_dinode *)buf;
			if (!memcmp(di->i_signature,
				    OCFS2_SUPER_BLOCK_SIGNATURE,
				    strlen(OCFS2_SUPER_BLOCK_SIGNATURE))) {
				populate_sb_info(dev, &di->id2.i_super);
				break;
			}
		}

		close(fd);
	}
}

static void do_full_detect(struct list_head *dev_list)
{
	ocfs2_check_heartbeats(dev_list, 1);
}

int main(int argc, char **argv)
{
	errcode_t ret = 0;
	struct list_head dev_list;

	initialize_ocfs_error_table();
	initialize_o2dl_error_table();
	initialize_o2cb_error_table();

	INIT_LIST_HEAD(&(dev_list));

	ret = read_options(argc, argv);
	if (ret)
		goto bail;

	o2cb_init();

	ret = build_partition_list(&dev_list, device);
	if (ret) {
		com_err(progname, ret, "while building partition list");
		goto bail;
	}

	if (quick_detect) {
		do_quick_detect(&dev_list);
		print_quick_detect(&dev_list);
	} else {
		do_full_detect(&dev_list);
		print_full_detect(&dev_list);
	}

	free_partition_list(&dev_list);

bail:
	return ret;
}
