/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * checkhb.c
 *
 * ocfs2 check heartbeat function
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
#include <signal.h>

#include "ocfs2.h"
#include "ocfs2_fs.h"
#include "ocfs1_fs_compat.h"

#define FATAL_ERROR(fmt, arg...)        \
	({	fprintf(stderr, "ERROR at %s, %d: " fmt ".  EXITING!!!\n", \
			__FILE__, __LINE__, ##arg);  \
		raise (SIGTERM);     \
		exit(1); \
	})

static void ocfs2_fill_nodes_list (char *buf, uint32_t len, struct list_head *node_list)
{
	int16_t *slots = (int16_t *)buf;
	uint32_t i;
	uint32_t num_slots = (len / sizeof(uint16_t));
	ocfs2_nodes *node_blk;
	
	for (i = 0; i < num_slots; ++i) {
		if (slots[i] == -1)
			break;
		if (ocfs2_malloc0(sizeof(ocfs2_nodes), &node_blk))
			FATAL_ERROR("out of memory");
		node_blk->node_num = slots[i];
		list_add_tail(&(node_blk->list), node_list);
	}

	return ;
}

static errcode_t ocfs2_read_slotmap (ocfs2_filesys *fs, struct list_head *node_list)
{
	errcode_t ret = 0;
	char *slotbuf = NULL;
	int slotbuf_len;
	char *slotmap = ocfs2_system_inodes[SLOT_MAP_SYSTEM_INODE].si_name;
	uint32_t slotmap_len;
	uint64_t slotmap_blkno;

	slotmap_len = strlen(slotmap);

	ret = ocfs2_lookup(fs, fs->fs_sysdir_blkno, slotmap, slotmap_len,
			   NULL, &slotmap_blkno);
	if (ret)
		return ret;

	ret =  ocfs2_read_whole_file(fs, slotmap_blkno,
				     &slotbuf, &slotbuf_len);
	if (!ret) {
		ocfs2_fill_nodes_list(slotbuf, slotbuf_len, node_list);
		ocfs2_free(&slotbuf);
	}

	return ret;
}

/*
 * ocfs2_check_heartbeat() check if the device is mounted on the
 * cluster or not
 * 
 *  notify ==>
 *      Called for every step of progress (because this takes a few
 *      seconds).  Can be NULL.  States are:
 *              OCFS2_CHB_START
 *              OCFS2_CHB_WAITING  (N times)
 *              OCFS2_CHB_COMPLETE
 *  user_data ==>
 *      User data pointer for the notify function.
 *
 * Return:
 *  mounted_flags set to ==>
 * 	OCFS2_MF_MOUNTED		if mounted locally
 * 	OCFS2_MF_ISROOT
 * 	OCFS2_MF_READONLY
 * 	OCFS2_MF_SWAP
 * 	OCFS2_MF_MOUNTED_CLUSTER	if mounted on cluster
 *  nodes_list set to ==>
 *  	List of live nodes
 *
 */
errcode_t ocfs2_check_heartbeat(char *device, int *mount_flags,
			       	struct list_head *nodes_list)
{
	errcode_t ret = 0;
	struct list_head dev_list;
	struct list_head *pos1, *pos2, *pos3, *pos4;
	ocfs2_devices *dev;
	ocfs2_nodes *node;

	INIT_LIST_HEAD(&dev_list);

	if (!device)
		goto bail;

	ret = ocfs2_malloc0(sizeof(ocfs2_devices), &dev);
	if (ret)
		goto bail;
	strncpy(dev->dev_name, device, sizeof(dev->dev_name));
	dev->mount_flags = 0;
	INIT_LIST_HEAD(&(dev->node_list));
	list_add(&(dev->list), &dev_list);

	ret = ocfs2_check_heartbeats(&dev_list);
	if (ret)
		goto bail;

	list_for_each_safe(pos1, pos2, &(dev_list)) {
		dev = list_entry(pos1, ocfs2_devices, list);
		*mount_flags = dev->mount_flags;
		list_for_each_safe(pos3, pos4, &(dev->node_list)) {
			node = list_entry(pos3, ocfs2_nodes, list);
			if (nodes_list) {
				list_add_tail(&node->list, nodes_list);
			} else {
				list_del(&(node->list));
				ocfs2_free(&node);
			}
		}
		list_del(&(dev->list));
		ocfs2_free(&dev);
		break;
	}

bail:
	if (ret) {
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
	}
	return ret;
}

/*
 * ocfs2_check_heartbeats()
 *
 */
errcode_t ocfs2_check_heartbeats(struct list_head *dev_list)
{
	ocfs2_filesys *fs = NULL;
	errcode_t ret = 0;
	uint16_t num_nodes;
	struct list_head *pos;
	ocfs2_devices *dev = NULL;
	char *device= NULL;

	list_for_each(pos, dev_list) {
		dev = list_entry(pos, ocfs2_devices, list);
		device = dev->dev_name;

		INIT_LIST_HEAD(&(dev->node_list));

		/* is it locally mounted */
		ret = ocfs2_check_mount_point(device, &dev->mount_flags, NULL, 0);
		if (ret)
			goto bail;

		/* open	fs */
		fs = NULL;
		ret = ocfs2_open(device, OCFS2_FLAG_RO, 0, 0, &fs);
		if (ret) {
			ret = 0;
			continue;
		} else
			dev->fs_type = 2;

		/* get label/uuid for ocfs and ocfs2 */
		if (dev->fs_type == 2) {
			num_nodes = OCFS2_RAW_SB(fs->fs_super)->s_max_nodes;
			memcpy(dev->label, OCFS2_RAW_SB(fs->fs_super)->s_label,
			       sizeof(dev->label));
			memcpy(dev->uuid, OCFS2_RAW_SB(fs->fs_super)->s_uuid,
			       sizeof(dev->uuid));
		} else {
			num_nodes = 32;
			if (ocfs2_get_ocfs1_label(dev->dev_name,
					dev->label, sizeof(dev->label),
					dev->uuid, sizeof(dev->uuid))) {
				dev->label[0] = '\0';
				memset(dev->uuid, 0, sizeof(dev->uuid));
			}
			continue;
		}

		/* read slotmap to get nodes on which the volume is mounted */
		ret = ocfs2_read_slotmap(fs, &dev->node_list);
		if (ret) {
			dev->errcode = ret;
			ret = 0;
		} else {
			if (!list_empty(&(dev->node_list)))
				dev->mount_flags |= OCFS2_MF_MOUNTED_CLUSTER;
		}
		ocfs2_close(fs);
	}

bail:
	return ret;
}

/*
 * ocfs2_get_ocfs1_label()
 *
 */
errcode_t ocfs2_get_ocfs1_label(char *device, uint8_t *label, uint16_t label_len,
				uint8_t *uuid, uint16_t uuid_len)
{
	int fd = -1;
	int ret = OCFS2_ET_IO;
	char buf[512];
	ocfs1_vol_label *v1_lbl;

	fd = open(device, O_RDONLY);
	if (fd == -1)
		goto bail;

	if (pread(fd, buf, sizeof(buf), 512) == -1)
		goto bail;

	v1_lbl = (ocfs1_vol_label *)buf;
	memcpy(label, v1_lbl->label, label_len);
	memcpy(uuid, v1_lbl->vol_id, uuid_len);

	ret = 0;
bail:
	if (fd >= 0)
		close(fd);
	return ret;
}
