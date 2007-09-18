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

static errcode_t ocfs2_read_slotmap (ocfs2_filesys *fs, uint8_t *node_nums)
{
	errcode_t ret = 0;
	char *slotbuf = NULL;
	int slotbuf_len;
	char *slotmap = ocfs2_system_inodes[SLOT_MAP_SYSTEM_INODE].si_name;
	uint32_t slotmap_len;
	uint64_t slotmap_blkno;
	int16_t *slots;
	int i, j;
	uint32_t num_nodes = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;

	slotmap_len = strlen(slotmap);

	ret = ocfs2_lookup(fs, fs->fs_sysdir_blkno, slotmap, slotmap_len,
			   NULL, &slotmap_blkno);
	if (ret)
		return ret;

	ret =  ocfs2_read_whole_file(fs, slotmap_blkno, &slotbuf, &slotbuf_len);
	if (!ret) {
		slots = (int16_t *)slotbuf;
		for (i = 0, j = 0; i < num_nodes; ++i)
			if (le16_to_cpu(slots[i]) != (uint16_t)OCFS2_INVALID_SLOT)
				node_nums[j++] = le16_to_cpu(slots[i]);
	}

	if (slotbuf)
		ocfs2_free(&slotbuf);

	return ret;
}

/*
 * ocfs2_check_heartbeats() check if the list of ocfs2 devices are
 * mounted on the cluster or not
 * 
 * Return:
 *  mounted_flags set to ==>
 * 	OCFS2_MF_MOUNTED		if mounted locally
 * 	OCFS2_MF_ISROOT
 * 	OCFS2_MF_READONLY
 * 	OCFS2_MF_SWAP
 * 	OCFS2_MF_BUSY
 * 	OCFS2_MF_MOUNTED_CLUSTER	if mounted on cluster
 */
errcode_t ocfs2_check_heartbeats(struct list_head *dev_list, int ignore_local)
{
	ocfs2_filesys *fs = NULL;
	errcode_t ret = 0;
	struct list_head *pos;
	ocfs2_devices *dev = NULL;
	char *device= NULL;
	int open_flags;

	list_for_each(pos, dev_list) {
		dev = list_entry(pos, ocfs2_devices, list);
		device = dev->dev_name;

		/* open	fs */
		fs = NULL;
		open_flags = OCFS2_FLAG_RO | OCFS2_FLAG_HEARTBEAT_DEV_OK;
		ret = ocfs2_open(device, open_flags, 0, 0, &fs);
		if (ret) {
			ret = 0;
			continue;
		} else
			dev->fs_type = 2;

		if (OCFS2_HAS_INCOMPAT_FEATURE(OCFS2_RAW_SB(fs->fs_super),
					  OCFS2_FEATURE_INCOMPAT_HEARTBEAT_DEV))
			dev->hb_dev = 1;

		/* is it locally mounted */
		if (!ignore_local || !dev->hb_dev) {
			ret = ocfs2_check_mount_point(device, &dev->mount_flags,
						      NULL, 0);
			if (ret)
				goto bail;
		}

		/* get label/uuid for ocfs2 */
		dev->max_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;
		memcpy(dev->label, OCFS2_RAW_SB(fs->fs_super)->s_label,
		       sizeof(dev->label));
		memcpy(dev->uuid, OCFS2_RAW_SB(fs->fs_super)->s_uuid,
		       sizeof(dev->uuid));

		ret = ocfs2_malloc((sizeof(uint8_t) * dev->max_slots),
				   &dev->node_nums);
		if (ret)
			goto bail;

		memset(dev->node_nums, OCFS2_MAX_SLOTS,
		       (sizeof(uint8_t) * dev->max_slots));

		if (dev->hb_dev)
			goto close;

		/* read slotmap to get nodes on which the volume is mounted */
		ret = ocfs2_read_slotmap(fs, dev->node_nums);
		if (ret) {
			dev->errcode = ret;
			ret = 0;
		} else {
			if (dev->node_nums[0] != OCFS2_MAX_SLOTS)
				dev->mount_flags |= OCFS2_MF_MOUNTED_CLUSTER;
		}
close:
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
	struct ocfs1_vol_label *v1_lbl;

	fd = open(device, O_RDONLY);
	if (fd == -1)
		goto bail;

	if (pread(fd, buf, sizeof(buf), 512) == -1)
		goto bail;

	v1_lbl = (struct ocfs1_vol_label *)buf;
	memcpy(label, v1_lbl->label, label_len);
	memcpy(uuid, v1_lbl->vol_id, uuid_len);

	ret = 0;
bail:
	if (fd >= 0)
		close(fd);
	return ret;
}
