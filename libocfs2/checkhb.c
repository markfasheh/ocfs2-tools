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

/*
 * ocfs2_check_heartbeat() check if the device is mounted on the
 * cluster or not
 *
 * Return:
 *  mounted_flags set to ==>
 * 	OCFS2_MF_MOUNTED		if mounted locally
 * 	OCFS2_MF_ISROOT
 * 	OCFS2_MF_READONLY
 * 	OCFS2_MF_SWAP
 * 	OCFS2_MF_MOUNTED_CLUSTER	if mounted on cluster
 *  node_names set to ==>
 *  	Slots of live nodes set to name
 *  notify ==>
 *      Called for every step of progress (because this takes a few
 *      seconds).  Can be NULL.  States are:
 *              OCFS2_CHB_START
 *              OCFS2_CHB_WAITING  (N times)
 *              OCFS2_CHB_COMPLETE
 *  user_data ==>
 *      User data pointer for the notify function.
 */
errcode_t ocfs2_check_heartbeat(char *device, int *mount_flags,
                                char **node_names,
                                ocfs2_chb_notify notify,
                                void *user_data)
{
	ocfs2_filesys *fs = NULL;
	char *buf = NULL;
	int buflen = 0;
	__u64 pub_times[OCFS2_NODE_MAP_MAX_NODES];
	int node_stats[OCFS2_NODE_MAP_MAX_NODES];
	char *p;
	errcode_t ret;
	int64_t blkno, blksize;
	uint16_t num_nodes;
	uint32_t blksz_bits;
	uint64_t dlm_blkno;
	int i;
	int wait_time;
	char *dlm = ocfs2_system_inode_names[DLM_SYSTEM_INODE];

	memset(&fs, 0, sizeof(ocfs2_filesys));
	memset(pub_times, 0, (sizeof(__u64) * OCFS2_NODE_MAP_MAX_NODES));
	memset(node_stats, 0, (sizeof(int) * OCFS2_NODE_MAP_MAX_NODES));

	/* is it locally mounted */
	*mount_flags = 0;
	ret = ocfs2_check_mount_point(device, mount_flags, NULL, 0);
	if (ret)
		goto bail;

	/* open	fs */
	blksize = 0;
	blkno = 0;
	ret = ocfs2_open(device, OCFS2_FLAG_RO, blkno, blksize, &fs);
	if (ret)
		goto bail;

	blksize = fs->fs_blocksize;
	num_nodes = OCFS2_RAW_SB(fs->fs_super)->s_max_nodes;
	blksz_bits = OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;

	/* get the dlm blkno */
	ret = ocfs2_lookup(fs, fs->fs_sysdir_blkno, dlm, strlen(dlm),
			   NULL, &dlm_blkno);
	if (ret)
		goto bail;

	/* read dlm file */
	ret = ocfs2_read_whole_file(fs, dlm_blkno, &buf, &buflen);
	if (ret)
		goto bail;

	/* init publish times */
	p = buf + ((2 + 4 + num_nodes) << blksz_bits); /* start of publish */
	ocfs2_detect_live_nodes (fs, p, pub_times, node_stats, 1);

	if (buflen && buf) {
		ocfs2_free (&buf);
		buflen = 0;
	}

	if (notify)
		notify(OCFS2_CHB_START,
		       "Checking heart beat on volume ",
		       user_data);

	/* wait */
	wait_time = 1;
	wait_time = (wait_time ? wait_time : 1);
	for (i = 0; i < OCFS2_HBT_WAIT; ++i) {
		if (notify)
			notify(OCFS2_CHB_WAITING, ".", user_data);
		sleep(wait_time);
	}

	if (notify)
		notify(OCFS2_CHB_COMPLETE,
		       "\r                                                \r",
		       user_data);
  
	/* read dlm file again */
	ret = ocfs2_read_whole_file(fs, dlm_blkno, &buf, &buflen);
	if (ret)
		goto bail;

	/* detect live nodes */
	p = buf + ((2 + 4 + num_nodes) << blksz_bits); /* start of publish */
	ocfs2_detect_live_nodes (fs, p, pub_times, node_stats, 0);

	/* fill mount_flags */
	for (i = 0; i < num_nodes; ++i) {
		if (node_stats[i]) {
			*mount_flags |= OCFS2_MF_MOUNTED_CLUSTER;
			break;
		}
	}

	/* fill node_names */
	if (node_names) {
		p = buf + (2 << blksz_bits);	/* start of node cfg */
		ocfs2_live_node_names(fs, p, node_stats, node_names);
	}

bail:
	if (fs)
		ocfs2_close(fs);

	if (buflen && buf)
		ocfs2_free (&buf);

	return ret;
}

/*
 * ocfs2_detect_live_nodes()
 *
 */
void ocfs2_detect_live_nodes (ocfs2_filesys *fs, char *pub_buf, uint64_t *pub_times,
			      int *node_stats, int first_time)
{
	char *p;
	ocfs_publish *publish;
	int i;
	uint16_t num_nodes = OCFS2_RAW_SB(fs->fs_super)->s_max_nodes;
	uint32_t blksz_bits = OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;


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
			node_stats[i] = 1;
		}
		p += (1 << blksz_bits);
	}

bail:
	return ;
}

/*
 * ocfs2_live_node_names()
 *
 */
void ocfs2_live_node_names(ocfs2_filesys *fs, char *node_buf, int *node_stats,
			   char **node_names)
{
	char *p;
	int i;
	ocfs_node_config_info *node;
	uint16_t num_nodes = OCFS2_RAW_SB(fs->fs_super)->s_max_nodes;
	uint32_t blksz_bits = OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;

	p = node_buf;
	for (i = 0; i < num_nodes; ++i) {
		if (node_stats[i]) {
			node = (ocfs_node_config_info *)p;
			node_names[i] = strdup(node->node_name);
		}
		p += (1 << blksz_bits);
	}

	return ;
}
