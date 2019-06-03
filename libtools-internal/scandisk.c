/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2008 Red Hat, Inc.  All rights reserved.
**  All rights reserved.
**
**  Author: Fabio M. Di Nitto <fdinitto@redhat.com>
**
**  Original design by:
**  Joel Becker <Joel.Becker@oracle.com>
**  Fabio M. Di Nitto <fdinitto@redhat.com>
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <dirent.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>

#include "tools-internal/scandisk.h"

/** search in cache helpers **/

/*
 * match is 0 for exact match
 *          1 to see if the string is contained and return the first match
 */

static struct devnode *find_dev_by_path(struct devnode *startnode, char *path,
					int match)
{
	struct devnode *nextnode;
	struct devpath *nextpath;

	while (startnode) {
		nextnode = startnode->next;
		nextpath = startnode->devpath;
		while (nextpath) {
			if (match) {
				if (strstr(nextpath->path, path))
					return startnode;
			} else {
				if (!strcmp(nextpath->path, path))
					return startnode;
			}
			nextpath = nextpath->next;
		}
		startnode = nextnode;
	}

	return 0;
}

static struct devnode *find_dev_by_majmin(struct devnode *startnode, int maj,
					  int min)
{
	struct devnode *nextnode;

	while (startnode) {
		nextnode = startnode->next;
		if ((startnode->maj == maj) && (startnode->min == min))
			return startnode;
		startnode = nextnode;
	}

	return 0;
}

/** free the cache.. this one is easy ;) **/

/* free all the path associated to one node */
static void flush_dev_list(struct devpath *startpath)
{
	struct devpath *nextpath;

	while (startpath) {
		nextpath = startpath->next;
		free(startpath);
		startpath = nextpath;
	}

	return;
}

/* free all nodes associated with one devlist */
static void flush_dev_cache(struct devlisthead *devlisthead)
{
	struct devnode *nextnode, *startnode = devlisthead->devnode;

	while (startnode) {
		nextnode = startnode->next;
		flush_dev_list(startnode->devpath);
		free(startnode);
		startnode = nextnode;
	}

	return;
}

/** list object allocation helpers **/

/* our only certain keys in the list are maj and min
 * this function append a devnode obj to devlisthead
 * and set maj and min
 */

static struct devnode *alloc_list_obj(struct devlisthead *devlisthead, int maj,
				      int min)
{
	struct devnode *nextnode;

	nextnode = malloc(sizeof(struct devnode));
	if (!nextnode)
		return 0;

	memset(nextnode, 0, sizeof(struct devnode));

	if (!devlisthead->devnode)
		devlisthead->devnode = nextnode;
	else
		devlisthead->tail->next = nextnode;

	devlisthead->tail = nextnode;

	nextnode->maj = maj;
	nextnode->min = min;

	return nextnode;
}

/* really annoying but we have no way to know upfront how
 * many paths are linked to a certain maj/min combo.
 * Once we find a device, we know maj/min and this new path.
 * add_path_obj will add the given path to the devnode
 */
static int add_path_obj(struct devnode *startnode, const char *path)
{
	struct devpath *nextpath, *startpath;

	nextpath = malloc(sizeof(struct devpath));
	if (!nextpath)
		return 0;

	memset(nextpath, 0, sizeof(struct devpath));

	if (!startnode->devpath) {
		startnode->devpath = startpath = nextpath;
	} else {
		startpath = startnode->devpath;
		while (startpath->next)
			startpath = startpath->next;

		/* always append what we find */
		startpath->next = nextpath;
		startpath = nextpath;
	}

	strncpy(startpath->path, path, MAXPATHLEN - 1);

	return 1;
}

/* lsdev needs to add blocks in 2 conditions: if we have a real block device
 * or if have a symlink to a block device.
 * this function simply avoid duplicate code around.
 */
static int add_lsdev_block(struct devlisthead *devlisthead, struct stat *sb,
			   const char *path)
{
	int maj, min;
	struct devnode *startnode;

	maj = major(sb->st_rdev);
	min = minor(sb->st_rdev);

	startnode = find_dev_by_majmin(devlisthead->devnode, maj, min);
	if (!startnode) {
		startnode = alloc_list_obj(devlisthead, maj, min);
		if (!startnode)
			return 0;
	}

	if (!add_path_obj(startnode, path))
		return 0;

	return 1;
}

/* check if it is a device or a symlink to a device */
static int dev_is_block(struct stat *sb, char *path)
{
	if (S_ISBLK(sb->st_mode))
		return 1;

	if (S_ISLNK(sb->st_mode))
		if (!stat(path, sb))
			if (S_ISBLK(sb->st_mode))
				return 1;

	return 0;
}

/* lsdev does nothing more than ls -lR /dev
 * dives into dirs (skips hidden directories)
 * add block devices
 * parse symlinks
 *
 * ret:
 * 1 on success
 * -1 for generic errors
 * -2 -ENOMEM
 */
static int lsdev(struct devlisthead *devlisthead, const char *path)
{
	int i, n, err = 0;
	struct dirent **namelist;
	struct stat sb;
	char newpath[MAXPATHLEN];

	i = scandir(path, &namelist, 0, alphasort);
	if (i < 0)
		return -1;

	for (n = 0; n < i; n++) {
		if (namelist[n]->d_name[0] != '.') {
			snprintf(newpath, sizeof(newpath), "%s/%s", path,
				 namelist[n]->d_name);

			if (!lstat(newpath, &sb)) {
				if (S_ISDIR(sb.st_mode))
					err = lsdev(devlisthead, newpath);
				if (err < 0)
					return err;

				if (dev_is_block(&sb, newpath))
					if (!add_lsdev_block
					    (devlisthead, &sb, newpath))
						return -2;
			}
		}
		free(namelist[n]);
	}
	free(namelist);
	return 1;
}

/*
 * scan /proc/partitions and adds info into the list.
 * It's able to add nodes if those are not found in sysfs.
 *
 * ret:
 *  0 if we can't scan
 *  -2 -ENOMEM
 *  1 if everything is ok
 */

static int scanprocpart(struct devlisthead *devlisthead)
{
	char line[4096];
	FILE *fp;
	int minor, major;
	unsigned long long blkcnt;
	char device[128];
	struct devnode *startnode;

	fp = fopen("/proc/partitions", "r");
	if (!fp)
		return 0;

	while (fgets(line, sizeof(line), fp) != NULL) {

		if (strlen(line) > 128 + (22))
			continue;

		if (sscanf(line, "%4d %4d %10llu %s",
		       &major, &minor, &blkcnt, device) < 4)
			continue;

		/* careful here.. if there is no device, we are scanning the
		 * first two lines that are not useful to us
		 */
		if (!strlen(device))
			continue;

		startnode =
		    find_dev_by_majmin(devlisthead->devnode, major, minor);
		if (!startnode) {
			startnode = alloc_list_obj(devlisthead, major, minor);
			if (!startnode) {
				fclose(fp);
				return -2;
			}
		}

		startnode->procpart = 1;
		strncpy(startnode->procname, device, sizeof(startnode->procname) - 1);
	}

	fclose(fp);
	return 1;
}

/* scan /proc/mdstat and adds info to the list. At this point
 * all the devices _must_ be already in the list. We don't add anymore
 * since raids can only be assembled out of existing devices
 *
 * ret:
 * 1 if we could scan
 * 0 otherwise
 */
static int scanmdstat(struct devlisthead *devlisthead)
{
	char line[4096];
	FILE *fp;
	char device[16];
	char separator[4];
	char status[16];
	char personality[16];
	char firstdevice[16];
	char devices[4096];
	char *tmp, *next;
	struct devnode *startnode = NULL;

	fp = fopen("/proc/mdstat", "r");
	if (!fp)
		return 0;

	while (fgets(line, sizeof(line), fp) != NULL) {

		/* i like things to be absolutely clean */
		memset(device, 0, sizeof(device));
		memset(separator, 0, sizeof(separator));
		memset(status, 0, sizeof(status));
		memset(personality, 0, sizeof(personality));
		memset(firstdevice, 0, sizeof(firstdevice));
		memset(devices, 0, sizeof(devices));

		if (strlen(line) >= sizeof(line))
			continue;

		/* we only parse stuff that starts with ^md
		 * that's supposed to point to raid */
		if (!(line[0] == 'm' && line[1] == 'd'))
			continue;

		if (sscanf(line, "%s %s %s %s %s",
		       device, separator, status, personality, firstdevice) < 5)
			continue;

		/* scan only raids that are active */
		if (strcmp(status, "active"))
			continue;

		/* try to find *mdX and set the device as real raid.
		 * if we don't find the device we don't try to set the slaves */
		startnode = find_dev_by_path(devlisthead->devnode, device, 1);
		if (!startnode)
			continue;

		startnode->md = 1;

		/* trunkate the string from sdaX[Y] to sdaX and
		 * copy the whole device string over */
		tmp = strstr(firstdevice, "[");
		if (!tmp)
			continue;
		memset(tmp, 0, 1);

		tmp = strstr(line, firstdevice);
		if (!tmp)
			continue;
		strncpy(devices, tmp, sizeof(devices) - 1);

		/* if we don't find any slave (for whatever reason)
		 * keep going */
		if (!strlen(devices))
			continue;

		tmp = devices;
		while ((tmp) && ((next = strstr(tmp, " ")) || strlen(tmp))) {
			char *tmp2;

			tmp2 = strstr(tmp, "[");
			if (tmp2)
				memset(tmp2, 0, 1);

			startnode =
			    find_dev_by_path(devlisthead->devnode, tmp, 1);
			if (startnode)
				startnode->md = 2;

			tmp = next;

			if (tmp)
				tmp++;

		}
	}

	fclose(fp);
	return 1;
}

/* scanmapper parses /proc/devices to identify what maj are associated
 * with device-mapper
 *
 * ret:
 * can't fail for now
 */
static int scanmapper(struct devlisthead *devlisthead)
{
	struct devnode *startnode;
	FILE *fp;
	char line[4096];
	char major[4];
	char device[64];
	int maj, start = 0;

	fp = fopen("/proc/devices", "r");
	if (!fp)
		return 0;

	while (fgets(line, sizeof(line), fp) != NULL) {
		memset(major, 0, sizeof(major));
		memset(device, 0, sizeof(device));

		if (strlen(line) > sizeof(line))
			continue;

		if (!strncmp(line, "Block devices:", 13)) {
			start = 1;
			continue;
		}

		if (!start)
			continue;

		if (sscanf(line, "%s %s", major, device) < 2)
			continue;

		if (!strncmp(device, "device-mapper", 13)) {
			maj = atoi(major);
			startnode = devlisthead->devnode;

			while (startnode) {
				if (startnode->maj == maj)
					startnode->mapper = 1;

				startnode = startnode->next;
			}

		}

	}

	fclose(fp);
	return 1;
}

/* scanpower parses /proc/devices to identify what maj are associated
 * with powerpath devices
 */
static int scanpower(struct devlisthead *devlisthead)
{
	struct devnode *startnode;
	FILE *fp;
	char line[4096];
	char major[4];
	char device[64];
	int maj, start = 0;
	int found = 0;

	fp = fopen("/proc/devices", "r");
	if (!fp)
		return 0;

	while (fgets(line, sizeof(line), fp) != NULL) {
		memset(major, 0, 4);
		memset(device, 0, 64);

		if (strlen(line) > 4096)
			continue;

		if (!strncmp(line, "Block devices:", 13)) {
			start = 1;
			continue;
		}

		if (!start)
			continue;

		sscanf(line, "%s %s", major, device);

		if (!strncmp(device, "power", 5)) {
			found = 1;
			maj = atoi(major);
			startnode = devlisthead->devnode;

			while (startnode) {
				if (startnode->maj == maj)
					startnode->power = 1;

				startnode = startnode->next;
			}

		}

	}

	fclose(fp);
	return found;
}

/* scan through the list and execute the custom filter for each entry */
static void run_filter(struct devlisthead *devlisthead,
		       devfilter filter, void *filter_args)
{
	struct devnode *startnode = devlisthead->devnode;

	while (startnode) {
		filter(startnode, filter_args);
		startnode = startnode->next;
	}
	return;
}

/** sysfs helper functions **/

/* /sys/block/sda/dev or /sys/block/sda1/dev exists
 * the device is real and dev contains maj/min info.
 *
 * ret:
 * 1 on success and set maj/min
 * 0 if no file is found
 * -1 if we could not open the file
 */
static int sysfs_is_dev(char *path, int *maj, int *min)
{
	char newpath[MAXPATHLEN];
	struct stat sb;
	FILE *f;
	snprintf(newpath, sizeof(newpath), "%.*s/dev", (int)sizeof(newpath) - 5, path);
	if (!lstat(newpath, &sb)) {
		f = fopen(newpath, "r");
		if (f) {
			int err;

			err = fscanf(f, "%d:%d", maj, min);
			fclose(f);
			if ((err == EOF) || (err != 2))
				return -1;

			return 1;
		} else
			return -1;
	}
	return 0;
}

/* /sys/block/sda/removable tells us if a device can be ejected
 * from the system or not. This is useful for USB pendrive that are
 * both removable and disks.
 *
 * ret:
 * 1 if is removable
 * 0 if not
 * -1 if we couldn't find the file.
 */
static int sysfs_is_removable(char *path)
{
	char newpath[MAXPATHLEN];
	struct stat sb;
	int i = -1;
	FILE *f;
	snprintf(newpath, sizeof(newpath), "%.*s/removable", (int)sizeof(newpath) - 11, path);
	if (!lstat(newpath, &sb)) {
		f = fopen(newpath, "r");
		if (f) {
			int err;

			err = fscanf(f, "%d\n", &i);
			fclose(f);
			if ((err == EOF) || (err != 1))
				i = -1;
		}
	}
	return i;
}

/* we use this function to scan /sys/block/sda{,1}/{holders,slaves}
 * to know in what position of the foodchain this device is.
 * NOTE: a device can have both holders and slaves at the same time!
 * (for example an lvm volume on top of a raid device made of N real disks
 *
 * ret:
 * always return the amount of entries in the dir if successful
 * or any return value from scandir.
 */
static int sysfs_has_subdirs_entries(char *path, const char *subdir)
{
	char newpath[MAXPATHLEN];
	struct dirent **namelist;
	struct stat sb;
	int n, i, count = 0;

	snprintf(newpath, sizeof(newpath), "%s/%s", path, subdir);
	if (!lstat(newpath, &sb)) {
		if (S_ISDIR(sb.st_mode)) {
			i = scandir(newpath, &namelist, 0, alphasort);
			if (i < 0)
				return i;
			for (n = 0; n < i; n++) {
				if (namelist[n]->d_name[0] != '.')
					count++;
				free(namelist[n]);
			}
			free(namelist);
		}
	}
	return count;
}

/* this is the best approach so far to make sure a block device
 * is a disk and distinguish it from a cdrom or tape or etc.
 * What we know for sure is that a type 0 is a disk.
 * From an old piece code 0xe is an IDE disk and comes from media.
 * NOTE: we scan also for ../ that while it seems stupid, it will
 * allow to easily mark partitions as real disks.
 * (see for example /sys/block/sda/device/type and
 * /sys/block/sda1/../device/type)
 * TODO: there might be more cases to evaluate.
 *
 * ret:
 * -2 we were not able to open the file
 * -1 no path found
 *  0 we found the path but we have 0 clue on what it is
 *  1 is a disk
 */
static int sysfs_is_disk(char *path)
{
	char newpath[MAXPATHLEN];
	struct stat sb;
	int i = -1;
	FILE *f;

	snprintf(newpath, sizeof(newpath), "%.*s/device/type", (int)sizeof(newpath) - 13, path);
	if (!lstat(newpath, &sb))
		goto found;

	snprintf(newpath, sizeof(newpath), "%.*s/../device/type", (int)sizeof(newpath) - 16, path);
	if (!lstat(newpath, &sb))
		goto found;

	snprintf(newpath, sizeof(newpath), "%.*s/device/media", (int)sizeof(newpath) - 14, path);
	if (!lstat(newpath, &sb))
		goto found;

	snprintf(newpath, sizeof(newpath), "%.*s/../device/media", (int)sizeof(newpath) - 17, path);
	if (!lstat(newpath, &sb))
		goto found;

	snprintf(newpath, sizeof(newpath), "%.*s/device/devtype", (int)sizeof(newpath) - 16, path);
	if (!lstat(newpath, &sb))
		return 1;

	snprintf(newpath, sizeof(newpath), "%.*s/../device/devtype", (int)sizeof(newpath) - 19, path);
	if (!lstat(newpath, &sb))
		return 1;

	return -1;

      found:
	f = fopen(newpath, "r");
	if (f) {
		int err;

		err = fscanf(f, "%d\n", &i);
		fclose(f);

		if ((err == EOF) || (err != 1))
			return 0;

		switch (i) {
		case 0x0:	/* scsi type_disk */
		case 0xe:	/* found on ide disks from old kernels.. */
			i = 1;
			break;
		default:
			i = 0;	/* by default we have no clue */
			break;
		}
	} else
		i = -2;

	return i;
}

/* recursive function that will scan and dive into /sys/block
 * looking for devices and scanning for attributes.
 *
 * ret:
 * 1 on success
 * -1 on generic error
 * -2 -ENOMEM
 */
static int scansysfs(struct devlisthead *devlisthead, const char *path, int level, int parent_holder)
{
	struct devnode *startnode;
	int i, n, maj = -1, min = -1, has_holder;
	struct dirent **namelist;
	struct stat sb;
	char newpath[MAXPATHLEN];

	i = scandir(path, &namelist, 0, alphasort);
	if (i < 0)
		return -1;

	for (n = 0; n < i; n++) {
		if (namelist[n]->d_name[0] != '.') {
			snprintf(newpath, sizeof(newpath),
				 "%s/%s", path, namelist[n]->d_name);

			/* newer version of sysfs has symlinks follow them */
			if (!lstat(newpath, &sb) && level)
				if (S_ISLNK(sb.st_mode))
					if (!stat(newpath, &sb))
						if (S_ISBLK(sb.st_mode))
							continue;

			has_holder = parent_holder;

			if (sysfs_is_dev(newpath, &maj, &min) > 0) {
				startnode =
				    alloc_list_obj(devlisthead, maj,
						   min);
				if (!startnode)
					return -2;

				startnode->sysfsattrs.sysfs = 1;
				startnode->sysfsattrs.removable =
				    sysfs_is_removable(newpath);

				if (!parent_holder)
					has_holder =
					    sysfs_has_subdirs_entries(newpath,
								      "holders");

				startnode->sysfsattrs.holders = has_holder;

				startnode->sysfsattrs.slaves =
				    sysfs_has_subdirs_entries(newpath,
							      "slaves");
				startnode->sysfsattrs.disk =
				    sysfs_is_disk(newpath);
			}

			if (!stat(newpath, &sb) && !level) {
				if (S_ISDIR(sb.st_mode))
					if (scansysfs(devlisthead, newpath, 1, has_holder) < 0)
						return -1;
			} else if (!lstat(newpath, &sb)) {
				if (S_ISDIR(sb.st_mode))
					if (scansysfs(devlisthead, newpath, 1, has_holder) < 0)
						return -1;
			}

		}
		free(namelist[n]);
	}

	free(namelist);
	return 1;
}

/*
 * devlisthead can be null if you are at init time. pass the old one if you are
 * updating or scanning..
 *
 * timeout is used only at init time to set the cache timeout value if default
 * value is not good enough. We might extend its meaning at somepoint.
 * Anything <= 0 means that the cache does not expire.
 */

struct devlisthead *scan_for_dev(struct devlisthead *devlisthead,
				 time_t timeout,
				 devfilter filter, void *filter_args)
{
	int res;
	time_t current;

	time(&current);

	if (devlisthead) {
		if ((current - devlisthead->cache_timestamp) <
		    devlisthead->cache_timeout) {
			return devlisthead;
		}
	} else {
		devlisthead = malloc(sizeof(struct devlisthead));
		if (!devlisthead)
			return NULL;
		memset(devlisthead, 0, sizeof(struct devlisthead));
		if (timeout)
			devlisthead->cache_timeout = timeout;
		else
			devlisthead->cache_timeout = DEVCACHETIMEOUT;
	}

	flush_dev_cache(devlisthead);
	devlisthead->cache_timestamp = current;

	/* it's important we check those 3 errors and abort in case
	 * as it means that we are running out of mem,
	 */
	devlisthead->sysfs = res = scansysfs(devlisthead, SYSBLOCKPATH, 0, 0);
	if (res < -1)
		goto emergencyout;

	devlisthead->procpart = res = scanprocpart(devlisthead);
	if (res < -1)
		goto emergencyout;

	devlisthead->lsdev = res = lsdev(devlisthead, DEVPATH);
	if (res < -1)
		goto emergencyout;

	/* from now on we don't alloc mem ourselves but only add info */
	devlisthead->mdstat = scanmdstat(devlisthead);

	devlisthead->mapper = scanmapper(devlisthead);

	devlisthead->power = scanpower(devlisthead);
	if (filter)
		run_filter(devlisthead, filter, filter_args);

	return devlisthead;

      emergencyout:
	free_dev_list(devlisthead);
	return 0;
}

/* free everything we used so far */

void free_dev_list(struct devlisthead *devlisthead)
{
	if (devlisthead) {
		flush_dev_cache(devlisthead);
		free(devlisthead);
	}
	return;
}

#ifdef DEBUG_EXE
#include "ocfs2-kernel/kernel-list.h"
#include <unistd.h>

struct sd_devices {
	struct list_head sd_list;
	int sd_maj;
	int sd_min;
	char *sd_path;
};

struct scan_context {
	struct list_head devlist;
	int rescan;
};

static void add_to_list(struct list_head *device_list, struct devnode *node)
{
	struct devpath *path;
	struct sd_devices *sd;
	int add = 0;

	path = node->devpath;
	while (path) {
		if (node->mapper)
			add = !strncmp(path->path, "/dev/mapper/", 12);
		else
			add = !strncmp(path->path, "/dev/sd", 7);
		if (add) {
			sd = malloc(sizeof(struct sd_devices));
			if (sd) {
				sd->sd_maj = node->maj;
				sd->sd_min = node->min;
				sd->sd_path = strdup(path->path);
				list_add_tail(&sd->sd_list, device_list);
				break;
			}
		}
		path = path->next;
	}
}

static void filter_devices(struct devnode *node, void *user_data)
{
	struct scan_context *ctxt = user_data;

	/* No information in sysfs?  Ignore it! */
	if (!node->sysfsattrs.sysfs)
		return;

	/* Not a disk?  Ignore it! */
	if (!node->sysfsattrs.disk)
		return;

	/* It's part of some other device?  Ignore it! */
	if (node->sysfsattrs.holders)
		return;

	/*
	 * No path in /dev?  Well, udev probably hasn't gotten there. Trigger
	 * a rescan
	 */
	if (!node->devpath) {
		ctxt->rescan = 1;
		return;
	}

	add_to_list(&ctxt->devlist, node);
}

int main(int argc, char **argv)
{
	struct devlisthead *dev = NULL;
	int delay = 1;
	struct scan_context scan_ctxt, *ctxt = &scan_ctxt;
	struct list_head *pos, *pos1;
	struct sd_devices *sd;

	INIT_LIST_HEAD(&ctxt->devlist);

	do {
		ctxt->rescan = 0;
		if (delay > 5)
			break;

		if (dev) {
			list_for_each_safe(pos, pos1, &ctxt->devlist) {
				sd = list_entry(pos, struct sd_devices, sd_list);
				list_del(pos);
				free(sd);
			}
			free_dev_list(dev);
			sleep(delay);
			delay += 2;
		}

		dev = scan_for_dev(NULL, 5, filter_devices, ctxt);
		if (!dev) {
			printf("error\n");
			return -1;
		}
	} while (ctxt->rescan);


	list_for_each(pos, &ctxt->devlist) {
		sd = list_entry(pos, struct sd_devices, sd_list);
		printf("%d %d %s\n", sd->sd_maj, sd->sd_min, sd->sd_path);
	}

	free_dev_list(dev);

	return 0;
}
#endif
