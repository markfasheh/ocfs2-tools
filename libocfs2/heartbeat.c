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

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE

#define __USE_MISC
#include <string.h>
#include <inttypes.h>

#include "ocfs2.h"

static errcode_t ocfs2_get_heartbeat_params(ocfs2_filesys *fs,
					    uint32_t *block_bits,
					    uint32_t *cluster_bits,
					    uint64_t *start_block,
					    uint64_t *num_blocks)
{
	errcode_t ret;
	char *filename;
	char *buf = NULL;
	uint64_t blkno, blocks;
	ocfs2_dinode *di;
	ocfs2_extent_rec *rec;

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

	di = (ocfs2_dinode *)buf;
	if (di->id2.i_list.l_tree_depth || 
	    di->id2.i_list.l_next_free_rec != 1) {
		ret = OCFS2_ET_BAD_HEARTBEAT_FILE;
		goto leave;
	}
	rec = &(di->id2.i_list.l_recs[0]);

	*block_bits = OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;
	*cluster_bits = OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits;
	*start_block = rec->e_blkno;

	blocks = rec->e_clusters << *cluster_bits;
	blocks >>= *block_bits;
	*num_blocks = blocks;

leave:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

errcode_t ocfs2_start_heartbeat(ocfs2_filesys *fs)
{
	errcode_t ret;
	uint32_t block_bits, cluster_bits;
	uint64_t start_block, num_blocks;

	ret = ocfs2_get_heartbeat_params(fs, &block_bits, &cluster_bits,
					 &start_block, &num_blocks);
	if (ret)
		goto leave;

	/* clamp to NM_MAX_NODES */
	if (num_blocks > 254)
		num_blocks = 254;

        /* XXX: NULL cluster is a hack for right now */
	ret = o2cb_create_heartbeat_region_disk(NULL,
						fs->uuid_str,
						fs->fs_devname,
						1 << block_bits,
						start_block,
						num_blocks);

leave:
	return ret;
}

errcode_t ocfs2_stop_heartbeat(ocfs2_filesys *fs)
{
	errcode_t ret;

	ret = o2cb_remove_heartbeat_region_disk(NULL, fs->uuid_str);

	return ret;
}
