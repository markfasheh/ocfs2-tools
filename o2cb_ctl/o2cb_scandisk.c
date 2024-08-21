/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * o2cb_scandisk.c
 *
 * Reads all the partitions and get the ocfs2 uuids
 *
 * Copyright (C) 2010 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License, version 2,  as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include "o2cb_scandisk.h"
#include "ocfs2/ocfs2.h"
#include "tools-internal/verbose.h"

struct hb_devices {
	struct list_head hb_list;
	char *hb_path;
};

struct scan_context {
	struct list_head sc_devlist;
	int sc_rescan;
};

static int fill_desc(char *device, struct o2cb_region_desc *reg,
		     struct o2cb_cluster_desc *cluster)
{
	ocfs2_filesys *fs;
	errcode_t ret;

	ret = ocfs2_open(device, OCFS2_FLAG_RO | OCFS2_FLAG_HEARTBEAT_DEV_OK,
			 0, 0, &fs);
	if (ret)
		return ret;

	ret = ocfs2_fill_heartbeat_desc(fs, reg);
	if (!ret)
		ret = ocfs2_fill_cluster_desc(fs, cluster);

	if (!ret) {
		/* TODO free this alloc... or not */
		reg->r_name = strdup(reg->r_name);
		reg->r_device_name = strdup(reg->r_device_name);
	}

	ocfs2_close(fs);

	return ret;
}

static int get_device_uuids(struct scan_context *ctxt, struct list_head *hbdevs)
{
	struct o2cb_device *od;
	struct list_head *pos, *pos1;
	struct hb_devices *hb;
	struct o2cb_region_desc rd;
	struct o2cb_cluster_desc cd;
	int numhbdevs = 0;

	list_for_each(pos, hbdevs) {
		++numhbdevs;
	}

	if (!numhbdevs)
		return 0;

	list_for_each(pos, &ctxt->sc_devlist) {
		hb = list_entry(pos, struct hb_devices, hb_list);

		if (fill_desc(hb->hb_path, &rd, &cd))
			continue;

		list_for_each(pos1, hbdevs) {
			od = list_entry(pos1, struct o2cb_device, od_list);
			if (od->od_flags & O2CB_DEVICE_FOUND)
				continue;
			if (strcmp(rd.r_name, od->od_uuid))
				continue;
			od->od_flags |= O2CB_DEVICE_FOUND;
			memcpy(&od->od_region, &rd, sizeof(od->od_region));
			memcpy(&od->od_cluster, &cd, sizeof(od->od_cluster));

			verbosef(VL_DEBUG, "Region %s matched to device %s\n",
				 rd.r_name, rd.r_device_name);
			--numhbdevs;
			break;
		}
		if (!numhbdevs)
			break;
	}

	return numhbdevs;
}

static void free_scan_context(struct scan_context *ctxt)
{
	struct list_head *pos, *pos1;
	struct hb_devices *hb;

	if (!ctxt)
		return ;

	list_for_each_safe(pos, pos1, &ctxt->sc_devlist) {
		hb = list_entry(pos, struct hb_devices, hb_list);
		list_del(pos);
		free(hb);
	}
}

static void add_to_list(struct list_head *device_list, struct devnode *node)
{
	struct devpath *path;
	struct hb_devices *hb;
	int add = 0;

	path = node->devpath;
	while (path) {
		if (node->mapper)
			add = !strncmp(path->path, "/dev/mapper/", 12);
		else if (node->power)
			add = !strncmp(path->path, "/dev/emcpower", 13);
		else {
			add = !strncmp(path->path, "/dev/sd", 7);
			if (!add)
				add = !strncmp(path->path, "/dev/loop", 9);
			if (!add)
				add = !strncmp(path->path, "/dev/xvd", 8);
			if (!add)
				add = !strncmp(path->path, "/dev/vd", 7);
			if (!add)
				add = !strncmp(path->path, "/dev/rbd", 8);
			if (!add)
				add = !strncmp(path->path, "/dev/drbd", 9);
			if (!add)
				add = !strncmp(path->path, "/dev/nbd", 8);
		}
		if (add) {
			hb = malloc(sizeof(struct hb_devices));
			if (hb) {
				hb->hb_path = strdup(path->path);
				list_add_tail(&hb->hb_list, device_list);
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
		ctxt->sc_rescan = 1;
		return;
	}

	add_to_list(&ctxt->sc_devlist, node);
}

void o2cb_scandisk(struct list_head *hbdevs)
{
	struct devlisthead *dev = NULL;
	int delay = 1;
	struct scan_context scan_ctxt, *ctxt = &scan_ctxt;

	INIT_LIST_HEAD(&ctxt->sc_devlist);

	do {
		ctxt->sc_rescan = 0;
		if (delay > 5)
			break;

		if (dev) {
			free_scan_context(ctxt);
			free_dev_list(dev);
			sleep(delay);
			delay += 2;
		}

		dev = scan_for_dev(NULL, 5, filter_devices, ctxt);
		if (!dev)
			goto bail;

		if (!get_device_uuids(ctxt, hbdevs))
			break;
	} while (ctxt->sc_rescan);

bail:
	free_scan_context(ctxt);
	free_dev_list(dev);
}
