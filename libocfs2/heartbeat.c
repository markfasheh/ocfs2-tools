/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * heartbeat.c
 *
 * Interface the OCFS2 userspace library to the userspace heartbeat
 * functionality
 *
 * Copyright (C) 2005 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License, version 2,  as published by the Free Software Foundation.
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
 * Authors: Mark Fasheh, Zach Brown
 */

#include <string.h>
#include <inttypes.h>

#include "ocfs2/byteorder.h"
#include "ocfs2/ocfs2.h"

void ocfs2_swap_disk_heartbeat_block(struct o2hb_disk_heartbeat_block *hb)
{
	if (cpu_is_little_endian)
		return;

	hb->hb_seq        = bswap_64(hb->hb_seq);
	hb->hb_cksum      = bswap_32(hb->hb_cksum);
	hb->hb_generation = bswap_64(hb->hb_generation);
}

errcode_t ocfs2_fill_heartbeat_desc(ocfs2_filesys *fs,
				    struct o2cb_region_desc *desc)
{
	errcode_t ret;
	char *filename;
	char *buf = NULL;
	uint64_t blkno, blocks, start_block;
	uint32_t block_bits, cluster_bits;
	int sectsize, sectsize_bits;
	struct ocfs2_dinode *di;
	struct ocfs2_extent_rec *rec;

	ret = ocfs2_get_device_sectsize(fs->fs_devname, &sectsize);
	if (ret)
		goto leave;

	sectsize_bits = ffs(sectsize) - 1;

	filename = ocfs2_system_inodes[HEARTBEAT_SYSTEM_INODE].si_name;

	ret = ocfs2_lookup(fs, fs->fs_sysdir_blkno, filename,
			   strlen(filename),  NULL, &blkno);
	if (ret)
		goto leave;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		goto leave;

	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret)
		goto leave;

	di = (struct ocfs2_dinode *)buf;
	if (di->id2.i_list.l_tree_depth || 
	    di->id2.i_list.l_next_free_rec != 1) {
		ret = OCFS2_ET_BAD_HEARTBEAT_FILE;
		goto leave;
	}
	rec = &(di->id2.i_list.l_recs[0]);

	block_bits = OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;
	cluster_bits = OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits;

	if (block_bits < sectsize_bits) {
		ret = OCFS2_ET_BLOCK_SIZE_TOO_SMALL_FOR_HARDWARE;
		goto leave;
	}

	blocks = ocfs2_rec_clusters(0, rec) << cluster_bits;
	blocks >>= block_bits;

	if (blocks > O2NM_MAX_NODES)
		blocks = O2NM_MAX_NODES;

	start_block = rec->e_blkno << block_bits;
	start_block >>= sectsize_bits;

	desc->r_name			= fs->uuid_str;
	desc->r_device_name		= fs->fs_devname;
	desc->r_block_bytes		= sectsize;
	desc->r_start_block		= start_block;
	desc->r_blocks			= blocks;

leave:
	if (buf)
		ocfs2_free(&buf);

	return ret;
}

errcode_t ocfs2_start_heartbeat(ocfs2_filesys *fs)
{
	errcode_t ret;
	struct o2cb_region_desc desc;

	ret = ocfs2_fill_heartbeat_desc(fs, &desc);	
	if (ret)
		goto leave;

        /* XXX: NULL cluster is a hack for right now */
	ret = o2cb_start_heartbeat_region(NULL, &desc);

leave:
	return ret;
}

errcode_t ocfs2_stop_heartbeat(ocfs2_filesys *fs)
{
	return o2cb_stop_heartbeat_region(NULL, fs->uuid_str);
}
