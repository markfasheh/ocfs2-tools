/*
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

#include "ocfs2.h"
#include "ocfs2_fs.h"
#include "ocfs2_disk_dlm.h"
#include "ocfs1_fs_compat.h"


typedef struct _ocfs2_fs {
	ocfs2_filesys *fs;
	char *dev_name;
	uint64_t dlm_blkno;
	uint64_t *pub_times;
	int *live_node;
	struct list_head list;
} ocfs2_fs;


void ocfs2_detect_live_nodes (ocfs2_fs *fs_blk, char *pub_buf, int first_time);

errcode_t ocfs2_gather_times_v2(ocfs2_fs *fs_blk, struct list_head *node_list,
				int first_time);

errcode_t ocfs2_gather_times_v1(ocfs2_fs *fs_blk, struct list_head *node_list,
			       	int first_time);

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
errcode_t ocfs2_check_heartbeat(char *device, int quick_detect,
				int *mount_flags, struct list_head *nodes_list,
                                ocfs2_chb_notify notify, void *user_data)
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

	ret = ocfs2_check_heartbeats(&dev_list, quick_detect, notify, user_data);
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
errcode_t ocfs2_check_heartbeats(struct list_head *dev_list, int quick_detect,
				 ocfs2_chb_notify notify, void *user_data)
{
	ocfs2_filesys *fs = NULL;
	errcode_t ret = 0;
	uint16_t num_nodes;
	int i;
	struct list_head *pos;
	ocfs2_devices *dev = NULL;
	char *device= NULL;
	ocfs2_fs *fs_blk = NULL;
	int any_ocfs = 0;

	if (notify && !quick_detect)
		notify(OCFS2_CHB_START, "Checking heart beat on volume ", user_data);

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
			if (ret == OCFS2_ET_OCFS_REV)
				dev->fs_type = 1;
			else
				continue;
		} else
			dev->fs_type = 2;

		ret = 0;

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
		}

		if (!quick_detect) {
			ret = ocfs2_malloc0(sizeof(ocfs2_fs), &fs_blk);
			if (ret)
				goto bail;

			ret = ocfs2_malloc0((num_nodes * sizeof(uint64_t)), &fs_blk->pub_times);
			if (ret)
				goto bail;

			ret = ocfs2_malloc0((num_nodes * sizeof(int)), &fs_blk->live_node);
			if (ret)
				goto bail;

			fs_blk->fs = fs;
			fs_blk->dev_name = dev->dev_name;

			dev->private = (void *)fs_blk;

			if (dev->fs_type == 2)
				ret = ocfs2_gather_times_v2(fs_blk, NULL, 1);
			else
				ret = ocfs2_gather_times_v1(fs_blk, NULL, 1);
			if (ret)
				goto bail;
		}

		any_ocfs = 1;
	}

	if (quick_detect)
		goto bail;

	if (!any_ocfs)
		goto bail;

	/* wait */
	for (i = 0; i < OCFS2_HBT_WAIT; ++i) {
		if (notify)
			notify(OCFS2_CHB_WAITING, ".", user_data);
		sleep(1);
	}

	list_for_each(pos, dev_list) {
		dev = list_entry(pos, ocfs2_devices, list);

		if (dev->fs_type < 1)
			continue;

		fs_blk = (ocfs2_fs *) dev->private;
		fs = fs_blk->fs;

		if (dev->fs_type == 2) {
			num_nodes = OCFS2_RAW_SB(fs->fs_super)->s_max_nodes;
			ret = ocfs2_gather_times_v2(fs_blk, &dev->node_list, 0);
		} else {
			num_nodes = 32;
			ret = ocfs2_gather_times_v1(fs_blk, &dev->node_list, 0);
		}
		if (ret)
			goto bail;

		/* fill mount_flags */
		for (i = 0; i < num_nodes; ++i) {
			if (fs_blk->live_node[i]) {
				dev->mount_flags |= OCFS2_MF_MOUNTED_CLUSTER;
				break;
			}
		}
	}

	if (notify)
		notify(OCFS2_CHB_COMPLETE,
		       "\r                                                \r",
		       user_data);

bail:
	list_for_each(pos, dev_list) {
		dev = list_entry(pos, ocfs2_devices, list);
		if (dev->private) {
			fs_blk = (ocfs2_fs *) dev->private;
			if (fs_blk->fs)
				ocfs2_close(fs_blk->fs);
			ocfs2_free(&fs_blk->pub_times);
			ocfs2_free(&fs_blk->live_node);
			ocfs2_free(&fs_blk);
			dev->private = NULL;
		}
	}

	return ret;
}

/*
 * ocfs2_detect_live_nodes()
 *
 */
void ocfs2_detect_live_nodes (ocfs2_fs *fs_blk, char *pub_buf, int first_time)
{
	char *p;
	ocfs_publish *publish;
	int i;
	uint16_t num_nodes;
	uint32_t blksz_bits;
	uint64_t *pub_times = fs_blk->pub_times;
	int *live_node = fs_blk->live_node;

	if (fs_blk->fs) {
		num_nodes = OCFS2_RAW_SB(fs_blk->fs->fs_super)->s_max_nodes;
		blksz_bits = OCFS2_RAW_SB(fs_blk->fs->fs_super)->s_blocksize_bits;
	} else {
		num_nodes = 32;
		blksz_bits = 9;
	}

	if (first_time) {
		p = (char *)pub_buf;
		for (i = 0;  i < num_nodes; i++) {
			publish = (ocfs_publish *) p;
			pub_times[i] = publish->time;
			p += (1 << blksz_bits);
		}
		goto bail;	/* exit */
	}

	p = (char *)pub_buf;
	for (i = 0; i < num_nodes; i++) {
		publish = (ocfs_publish *) p;
		if (pub_times[i] != publish->time) {
			pub_times[i] = publish->time;
			live_node[i] = 1;
		}
		p += (1 << blksz_bits);
	}

bail:
	return ;
}

/*
 * ocfs2_gather_times_v2()
 *
 */
errcode_t ocfs2_gather_times_v2(ocfs2_fs *fs_blk, struct list_head *node_list,
			       	int first_time)
{
	ocfs2_filesys *fs = fs_blk->fs;
	char *dlm = ocfs2_system_inode_names[DLM_SYSTEM_INODE];
	char *buf = NULL;
	int buflen = 0;
	errcode_t ret = 0;
	char *p;
	uint16_t num_nodes = OCFS2_RAW_SB(fs->fs_super)->s_max_nodes;
	uint32_t blksz_bits = OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;
	ocfs_node_config_info *node;
	ocfs2_nodes *node_blk;
	int i;

	/* get the dlm blkno */
	if (!fs_blk->dlm_blkno) {
		ret = ocfs2_lookup(fs, fs->fs_sysdir_blkno, dlm, strlen(dlm),
				   NULL, &(fs_blk->dlm_blkno));
		if (ret)
			goto bail;
	}

	/* read dlm file */
	ret = ocfs2_read_whole_file(fs, fs_blk->dlm_blkno, &buf, &buflen);
	if (ret)
		goto bail;

	/* process publish times */
	p = buf + ((2 + 4 + num_nodes) << blksz_bits); /* start of publish */
	ocfs2_detect_live_nodes (fs_blk, p, first_time);

	/* fill node_names */
	if (!first_time && node_list) {
		p = buf + (2 << blksz_bits);	/* start of node cfg */
		for (i = 0; i < num_nodes; ++i) {
			if (fs_blk->live_node[i]) {
				node = (ocfs_node_config_info *)p;
				ret = ocfs2_malloc0(sizeof(ocfs2_nodes), &node_blk);
				if (ret)
					goto bail;
				strncpy(node_blk->node_name, node->node_name,
					sizeof(node_blk->node_name));
				node_blk->node_num = i;
				list_add_tail(&(node_blk->list), node_list);
			}
			p += (1 << blksz_bits);
		}
	}

bail:
	if (buflen && buf)
		ocfs2_free (&buf);

	return ret;
}

/*
 * ocfs2_gather_times_v1()
 *
 */
errcode_t ocfs2_gather_times_v1(ocfs2_fs *fs_blk, struct list_head *node_list,
			       	int first_time)
{
	int fd = -1;
	char *buf = NULL;
	int buflen = 0;
	errcode_t ret = 0;
	char *p;
	uint16_t num_nodes = 32;	/* v1 numnodes */
	uint32_t blksz_bits = 9;	/* v1 blksz is fixed at 512 bytes */
	ocfs1_disk_node_config_info *node;
	ocfs2_nodes *node_blk;
	int i;

	/* open device */
	fd = open(fs_blk->dev_name, O_RDONLY);
	if (fd == -1)
		goto bail;

	buflen = 2;		/* node cfg hdr */
        buflen += num_nodes;	/* node cfgs */
	buflen += 4;		/* node cfg trailer */
	buflen += num_nodes;	/* publish */
	buflen <<= blksz_bits;	/* convert to bytes */

	/* alloc mem to read cfg+publish */
	ret = ocfs2_malloc0(buflen, &buf);
	if (ret)
		goto bail;

	/* read cfg+publish */
	if (pread(fd, buf, buflen, 4096) == -1)
		goto bail;

	/* process publish times */
	p = buf + ((2 + num_nodes + 4) << blksz_bits); /* start of publish */
	ocfs2_detect_live_nodes (fs_blk, p, first_time);

	/* fill node_names */
	if (!first_time && node_list) {
		p = buf + (2 << blksz_bits);	/* start of node cfg */
		for (i = 0; i < num_nodes; ++i) {
			if (fs_blk->live_node[i]) {
				node = (ocfs1_disk_node_config_info *)p;
				ret = ocfs2_malloc0(sizeof(ocfs2_nodes), &node_blk);
				if (ret)
					goto bail;
				strncpy(node_blk->node_name, node->node_name,
					sizeof(node_blk->node_name));
				node_blk->node_num = i;
				list_add_tail(&(node_blk->list), node_list);
			}
			p += (1 << blksz_bits);
		}
	}

bail:
	if (buf)
		ocfs2_free (&buf);

	if (fd != -1)
		close(fd);

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
