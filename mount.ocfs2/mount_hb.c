/*
 * mount_heartbeat.c  Common heartbeat functions
 *
 * Copyright (C) 2005 Oracle.  All rights reserved.
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
 */

#include <sys/types.h>
#include <inttypes.h>

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>

#include <ocfs2.h>
#include <ocfs2_fs.h>
#include <ocfs1_fs_compat.h>

#include "o2cb.h"
#include "mount_hb.h"

int get_uuid(char *dev, char *uuid)
{
	ocfs2_filesys *fs = NULL;
	errcode_t ret = 0;
	int i;
	char *p;
	uint8_t *s_uuid;

	ret = ocfs2_open(dev, OCFS2_FLAG_RO, 0, 0, &fs);
	if (ret)
		goto out;

	s_uuid = OCFS2_RAW_SB(fs->fs_super)->s_uuid;

	for (i = 0, p = uuid; i < 16; i++, p += 2)
		sprintf(p, "%02X", s_uuid[i]);
	*p = '\0';

	ocfs2_close(fs);

out:
	return ret;
}

static int get_ocfs2_disk_hb_params(char *group_dev, uint32_t *block_bits, uint32_t *cluster_bits,
			     uint64_t *start_block, uint32_t *num_clusters)
{
	int status = -EINVAL;
	errcode_t ret = 0;
	uint64_t blkno;
	char *buf = NULL;
	char *heartbeat_filename;
	ocfs2_dinode *di;
	ocfs2_extent_rec *rec;
	ocfs2_filesys *fs = NULL;

	ret = ocfs2_open(group_dev, OCFS2_FLAG_RO, 0, 0, &fs);
	if (ret) {
		com_err(progname, ret, "while opening the device.");
		return status;
	}

	heartbeat_filename = ocfs2_system_inodes[HEARTBEAT_SYSTEM_INODE].si_name;
	ret = ocfs2_lookup(fs, fs->fs_sysdir_blkno, heartbeat_filename,
			   strlen(heartbeat_filename),  NULL, &blkno);
	if (ret) {
		com_err(progname, ret, "while looking up the hb system inode.");
		goto leave;
	}

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret) {
		com_err(progname, ret, "while allocating a block for hb.");
		goto leave;
	}
	
	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret) {
		com_err(progname, ret, "while reading hb inode.");
		goto leave;
	}

	di = (ocfs2_dinode *)buf;
	if (di->id2.i_list.l_tree_depth || 
	    di->id2.i_list.l_next_free_rec != 1) {
		com_err(progname, 0, "when checking for contiguous hb.");
		goto leave;
	}
	rec = &(di->id2.i_list.l_recs[0]);
	
	*block_bits = OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;
	*cluster_bits = OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits;
	*start_block = rec->e_blkno;
	*num_clusters = rec->e_clusters;
	status = 0;

leave:
	if (buf)
		ocfs2_free(&buf);
	if (fs)
		ocfs2_close(fs);
	return status;
}

int start_heartbeat(char *hbuuid, char *device)
{
	int ret;
	char *cluster = NULL;
	errcode_t err;
	uint32_t block_bits, cluster_bits, num_clusters;
	uint64_t start_block, num_blocks;

	ret = get_ocfs2_disk_hb_params(device, &block_bits, &cluster_bits, 
				       &start_block, &num_clusters);
	if (ret < 0) {
		printf("hb_params failed\n");
		return ret;
	}

	num_blocks = num_clusters << cluster_bits;
	num_blocks >>= block_bits;

	/* clamp to NM_MAX_NODES */
	if (num_blocks > 254)
		num_blocks = 254;

        /* XXX: NULL cluster is a hack for right now */
	err = o2cb_create_heartbeat_region_disk(NULL,
						hbuuid,
						device,
						1 << block_bits,
						start_block,
						num_blocks);
	if (err) {
		com_err(progname, err, "while creating hb region with o2cb.");
		return -EINVAL;
	}

	return 0;
}

int stop_heartbeat(const char *hbuuid)
{
	errcode_t err;

	err = o2cb_remove_heartbeat_region_disk(NULL, hbuuid);
	if (err) {
		com_err(progname, err, "while creating hb region with o2cb.");
		return -EINVAL;
	}
	return 0;
}
