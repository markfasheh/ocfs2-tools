/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
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
 */

#define _XOPEN_SOURCE 600  /* Triggers XOPEN2K in features.h */
#define _LARGEFILE64_SOURCE

#include "ocfs2/byteorder.h"
#include "ocfs2/ocfs2.h"

void ocfs2_swap_slot_map(struct ocfs2_slot_map *sm, int num_slots)
{
	int i;

	if (!cpu_is_big_endian)
		return;

	for (i = 0; i < num_slots; i++)
		sm->sm_slots[i] = bswap_16(sm->sm_slots[i]);
}

errcode_t ocfs2_read_slot_map(ocfs2_filesys *fs,
			      int num_slots,
			      struct ocfs2_slot_map **map_ret)
{
	errcode_t ret;
	uint64_t blkno;
	char *slot_map_buf;
	struct ocfs2_slot_map *sm;
	int bytes_needed, len;

	ret = ocfs2_lookup_system_inode(fs, SLOT_MAP_SYSTEM_INODE, 0,
					&blkno);
	if (ret)
		return ret;

	ret = ocfs2_read_whole_file(fs, blkno, &slot_map_buf, &len);
	if (ret)
		return ret;

	bytes_needed = ocfs2_blocks_in_bytes(fs,
					     num_slots * sizeof(__le16));
	if (bytes_needed > len) {
		ocfs2_free(&slot_map_buf);
		return OCFS2_ET_SHORT_READ;
	}

	sm = (struct ocfs2_slot_map *)slot_map_buf;
	ocfs2_swap_slot_map(sm, num_slots);

	*map_ret = sm;

	return 0;
}

errcode_t ocfs2_write_slot_map(ocfs2_filesys *fs,
			       int num_slots,
			       struct ocfs2_slot_map *sm)
{
	errcode_t ret, tret;
	ocfs2_cached_inode *ci;
	uint64_t blkno, bytes;
	uint32_t wrote;

	ret = ocfs2_lookup_system_inode(fs, SLOT_MAP_SYSTEM_INODE, 0,
					&blkno);
	if (ret)
		return ret;

	ret = ocfs2_read_cached_inode(fs, blkno, &ci);
	if (ret)
		return ret;

	bytes = (uint64_t)num_slots * sizeof(__le16);

	ocfs2_swap_slot_map(sm, num_slots);

	ret = ocfs2_file_write(ci, sm, bytes, 0, &wrote);
	tret = ocfs2_free_cached_inode(fs, ci);
	if (ret)
		return ret;
	if (wrote != bytes)
		return OCFS2_ET_SHORT_WRITE;

	/*
	 * The error from free_cached_inode() is only important if
	 * there were no other problems.
	 */
	if (tret)
		return tret;

	return 0;
}

errcode_t ocfs2_load_slot_map(ocfs2_filesys *fs,
			      struct ocfs2_slot_map_data **data_ret)
{
	errcode_t ret;
	int i, num_slots;
	struct ocfs2_slot_map *sm;
	struct ocfs2_slot_map_data *md;

	num_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;

	ret = ocfs2_malloc0(sizeof(struct ocfs2_slot_map_data) +
			    (sizeof(struct ocfs2_slot_data) * num_slots),
			    &md);
	if (ret)
		return ret;

	md->md_num_slots = num_slots;
	md->md_slots = (struct ocfs2_slot_data *)((char *)md + sizeof(struct ocfs2_slot_map_data));

	ret = ocfs2_read_slot_map(fs, num_slots, &sm);
	if (ret) {
		ocfs2_free(&md);
		return ret;
	}

	for (i = 0; i < num_slots; i++) {
		if (sm->sm_slots[i] != (uint16_t)OCFS2_INVALID_SLOT) {
			md->md_slots[i].sd_valid = 1;
			md->md_slots[i].sd_node_num = sm->sm_slots[i];
		}
	}

	*data_ret = md;
	return 0;
}

errcode_t ocfs2_store_slot_map(ocfs2_filesys *fs,
			       struct ocfs2_slot_map_data *md)
{
	errcode_t ret;
	struct ocfs2_slot_map *sm;
	int i, num_slots, bytes;

	num_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;
	bytes = num_slots * sizeof(__le16);

	ret = ocfs2_malloc0(bytes, &sm);
	if (ret)
		return ret;

	for (i = 0; i < num_slots; i++) {
		if (md->md_slots[i].sd_valid)
			sm->sm_slots[i] = md->md_slots[i].sd_node_num;
		else
			sm->sm_slots[i] = OCFS2_INVALID_SLOT;
	}

	ret = ocfs2_write_slot_map(fs, num_slots, sm);
	ocfs2_free(&sm);

	return ret;
}
