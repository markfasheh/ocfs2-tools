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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301 USA.
 */

#define _XOPEN_SOURCE 600  /* Triggers XOPEN2K in features.h */
#define _LARGEFILE64_SOURCE
#define _DEFAULT_SOURCE

#include "ocfs2/byteorder.h"
#include "ocfs2/ocfs2.h"

/* Just so we can pass it about */
union ocfs2_slot_map_wrapper {
	struct ocfs2_slot_map *mw_map;
	struct ocfs2_slot_map_extended *mw_map_extended;
};

void ocfs2_swap_slot_map(struct ocfs2_slot_map *sm, int num_slots)
{
	int i;

	if (!cpu_is_big_endian)
		return;

	for (i = 0; i < num_slots; i++)
		sm->sm_slots[i] = bswap_16(sm->sm_slots[i]);
}

void ocfs2_swap_slot_map_extended(struct ocfs2_slot_map_extended *se,
				  int num_slots)
{
	int i;

	if (!cpu_is_big_endian)
		return;

	for (i = 0; i < num_slots; i++)
		se->se_slots[i].es_node_num =
			bswap_32(se->se_slots[i].es_node_num);
}

static errcode_t __ocfs2_read_slot_map(ocfs2_filesys *fs,
				       int num_slots,
				       union ocfs2_slot_map_wrapper *wrap)
{
	errcode_t ret;
	uint64_t blkno;
	char *slot_map_buf;
	struct ocfs2_slot_map *sm;
	struct ocfs2_slot_map_extended *se;
	int bytes_needed, len;
	int extended = ocfs2_uses_extended_slot_map(OCFS2_RAW_SB(fs->fs_super));

	ret = ocfs2_lookup_system_inode(fs, SLOT_MAP_SYSTEM_INODE, 0,
					&blkno);
	if (ret)
		return ret;

	ret = ocfs2_read_whole_file(fs, blkno, &slot_map_buf, &len);
	if (ret)
		return ret;

	if (extended)
		bytes_needed =
			num_slots * sizeof(struct ocfs2_extended_slot);
	else
		bytes_needed = num_slots * sizeof(__le16);

	if (bytes_needed > len) {
		ocfs2_free(&slot_map_buf);
		return OCFS2_ET_SHORT_READ;
	}

	if (extended) {
		se = (struct ocfs2_slot_map_extended *)slot_map_buf;
		ocfs2_swap_slot_map_extended(se, num_slots);
		wrap->mw_map_extended = se;
	} else {
		sm = (struct ocfs2_slot_map *)slot_map_buf;
		ocfs2_swap_slot_map(sm, num_slots);
		wrap->mw_map = sm;
	}

	return 0;
}

errcode_t ocfs2_read_slot_map(ocfs2_filesys *fs,
			      int num_slots,
			      struct ocfs2_slot_map **map_ret)
{
	errcode_t ret;
	union ocfs2_slot_map_wrapper wrap = {
		.mw_map = NULL,
	};

	ret = __ocfs2_read_slot_map(fs, num_slots, &wrap);
	if (ret)
		return ret;

	*map_ret = wrap.mw_map;
	return 0;
}

errcode_t ocfs2_read_slot_map_extended(ocfs2_filesys *fs,
				       int num_slots,
				       struct ocfs2_slot_map_extended **map_ret)
{
	errcode_t ret;
	union ocfs2_slot_map_wrapper wrap = {
		.mw_map_extended = NULL,
	};

	ret = __ocfs2_read_slot_map(fs, num_slots, &wrap);
	if (ret)
		return ret;

	*map_ret = wrap.mw_map_extended;
	return 0;
}

static errcode_t __ocfs2_write_slot_map(ocfs2_filesys *fs,
					int num_slots,
					union ocfs2_slot_map_wrapper *wrap)
{
	errcode_t ret, tret;
	ocfs2_cached_inode *ci = NULL;
	uint64_t blkno;
	unsigned int size, bytes, wrote, blocks;
	void *buf = NULL;
	int extended = ocfs2_uses_extended_slot_map(OCFS2_RAW_SB(fs->fs_super));

	ret = ocfs2_lookup_system_inode(fs, SLOT_MAP_SYSTEM_INODE, 0,
					&blkno);
	if (ret)
		goto out;

	if (extended)
		size = num_slots * sizeof(struct ocfs2_extended_slot);
	else
		size = num_slots * sizeof(__le16);

	blocks = ocfs2_blocks_in_bytes(fs, size);
	bytes = blocks << OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;
	ret = ocfs2_malloc_blocks(fs->fs_io, blocks, &buf);
	if (ret)
		goto out;
	memset(buf, 0, bytes);

	if (extended) {
		memcpy(buf, wrap->mw_map_extended, size);
		ocfs2_swap_slot_map_extended(buf, num_slots);
	} else {
		memcpy(buf, wrap->mw_map, size);
		ocfs2_swap_slot_map(buf, num_slots);
	}

	ret = ocfs2_read_cached_inode(fs, blkno, &ci);
	if (ret)
		goto out;

	ret = ocfs2_file_write(ci, buf, bytes, 0, &wrote);
	if (ret)
		goto out;

	/*
	 * This is wacky.  We have to write a block (bytes), but &wrote
	 * might return only i_size (size).  Handle both.
	 */
	if ((wrote != bytes) && (wrote != size))
		ret = OCFS2_ET_SHORT_WRITE;

out:
	if (ci) {
		tret = ocfs2_free_cached_inode(fs, ci);
		/*
		 * The error from free_cached_inode() is only important if
		 * there were no other problems.
		 */
		if (!ret)
			ret = tret;
	}
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

errcode_t ocfs2_write_slot_map(ocfs2_filesys *fs,
			       int num_slots,
			       struct ocfs2_slot_map *sm)
{
	union ocfs2_slot_map_wrapper wrap = {
		.mw_map = sm,
	};

	return __ocfs2_write_slot_map(fs, num_slots, &wrap);
}

errcode_t ocfs2_write_slot_map_extended(ocfs2_filesys *fs,
					int num_slots,
					struct ocfs2_slot_map_extended *se)
{
	union ocfs2_slot_map_wrapper wrap = {
		.mw_map_extended = se,
	};

	return __ocfs2_write_slot_map(fs, num_slots, &wrap);
}

static void ocfs2_slot_map_to_data(ocfs2_filesys *fs,
				   struct ocfs2_slot_map_data *md,
				   union ocfs2_slot_map_wrapper *wrap)
{
	int i;
	struct ocfs2_slot_map *sm = wrap->mw_map;
	struct ocfs2_slot_map_extended *se = wrap->mw_map_extended;
	int extended = ocfs2_uses_extended_slot_map(OCFS2_RAW_SB(fs->fs_super));

	for (i = 0; i < md->md_num_slots; i++) {
		if (extended) {
			if (se->se_slots[i].es_valid) {
				md->md_slots[i].sd_valid = 1;
				md->md_slots[i].sd_node_num = se->se_slots[i].es_node_num;
			} else
				md->md_slots[i].sd_valid = 0;
		} else {
			if (sm->sm_slots[i] != (uint16_t)OCFS2_INVALID_SLOT) {
				md->md_slots[i].sd_valid = 1;
				md->md_slots[i].sd_node_num = sm->sm_slots[i];
			} else
				md->md_slots[i].sd_valid = 0;
		}
	}
}

static void ocfs2_slot_data_to_map(ocfs2_filesys *fs,
				   struct ocfs2_slot_map_data *md,
				   union ocfs2_slot_map_wrapper *wrap)
{
	int i;
	struct ocfs2_slot_map *sm = wrap->mw_map;
	struct ocfs2_slot_map_extended *se = wrap->mw_map_extended;
	int extended = ocfs2_uses_extended_slot_map(OCFS2_RAW_SB(fs->fs_super));

	for (i = 0; i < md->md_num_slots; i++) {
		if (extended) {
			if (md->md_slots[i].sd_valid) {
				se->se_slots[i].es_valid = 1;
				se->se_slots[i].es_node_num = md->md_slots[i].sd_node_num;
			} else
				se->se_slots[i].es_valid = 0;
		} else {
			if (md->md_slots[i].sd_valid)
				sm->sm_slots[i] = (uint16_t)md->md_slots[i].sd_node_num;
			else
				sm->sm_slots[i] = (uint16_t)OCFS2_INVALID_SLOT;
		}
	}
}

static errcode_t ocfs2_alloc_slot_map_data(int num_slots,
					   struct ocfs2_slot_map_data **md_ret)
{
	errcode_t ret;
	struct ocfs2_slot_map_data *md;

	ret = ocfs2_malloc0(sizeof(struct ocfs2_slot_map_data) +
			    (sizeof(struct ocfs2_slot_data) * num_slots),
			    &md);
	if (ret)
		return ret;

	md->md_num_slots = num_slots;
	md->md_slots = (struct ocfs2_slot_data *)((char *)md + sizeof(struct ocfs2_slot_map_data));

	*md_ret = md;
	return 0;
}

errcode_t ocfs2_load_slot_map(ocfs2_filesys *fs,
			      struct ocfs2_slot_map_data **data_ret)
{
	errcode_t ret;
	int num_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;;
	struct ocfs2_slot_map_data *md;
	union ocfs2_slot_map_wrapper wrap = {
		.mw_map = NULL,
	};

	ret = ocfs2_alloc_slot_map_data(num_slots, &md);
	if (ret)
		return ret;

	ret = __ocfs2_read_slot_map(fs, num_slots, &wrap);
	if (ret) {
		ocfs2_free(&md);
		return ret;
	}

	ocfs2_slot_map_to_data(fs, md, &wrap);

	*data_ret = md;
	return 0;
}

errcode_t ocfs2_store_slot_map(ocfs2_filesys *fs,
			       struct ocfs2_slot_map_data *md)
{
	errcode_t ret;
	char *slot_map_buf;
	int bytes;
	int extended = ocfs2_uses_extended_slot_map(OCFS2_RAW_SB(fs->fs_super));
	union ocfs2_slot_map_wrapper wrap;

	if (extended)
		bytes = md->md_num_slots * sizeof(struct ocfs2_extended_slot);
	else
		bytes = md->md_num_slots * sizeof(__le16);

	ret = ocfs2_malloc0(bytes, &slot_map_buf);
	if (ret)
		return ret;
	wrap.mw_map = (struct ocfs2_slot_map *)slot_map_buf;

	ocfs2_slot_data_to_map(fs, md, &wrap);
	ret = __ocfs2_write_slot_map(fs, md->md_num_slots, &wrap);

	ocfs2_free(&slot_map_buf);

	return ret;
}

struct slotmap_format {
	int extended;
	int needed_slots;
	int actual_slots;
	unsigned int needed_bytes;
	ocfs2_cached_inode *ci;
};

static errcode_t ocfs2_size_slot_map(ocfs2_filesys *fs,
				     struct slotmap_format *sf)
{
	errcode_t ret ;
	struct ocfs2_dinode *di;
	unsigned int clusters;
	uint64_t new_size;
	uint64_t blkno;

	di = sf->ci->ci_inode;
	blkno = sf->ci->ci_blkno;

	clusters = sf->needed_bytes + fs->fs_clustersize - 1;
	clusters = clusters >> OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits;
	/* Zero slots not allowed - even local mounts have a slot */
	if (!clusters) {
		ret = OCFS2_ET_INTERNAL_FAILURE;
		goto out;
	}

	/*
	 * We ensure that slotmaps are formatted to the end of the
	 * allocation.  If the allocation hasn't changed, we don't have
	 * anything to do.
	 */
	if (clusters == di->i_clusters) {
		ret = 0;
		goto out;
	}

	if (clusters > di->i_clusters) {
		ret = ocfs2_extend_allocation(fs, blkno,
					      (clusters - di->i_clusters));
		if (ret)
			goto out;

		/* We don't cache in the library right now, so any
		 * work done in extend_allocation won't be reflected
		 * in our now stale copy. */
		ocfs2_free_cached_inode(fs, sf->ci);
		ret = ocfs2_read_cached_inode(fs, blkno, &sf->ci);
		if (ret) {
			sf->ci = NULL;
			goto out;
		}
		di = sf->ci->ci_inode;
	} else if (clusters < di->i_clusters) {
		new_size = clusters <<
				OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits;
		ret = ocfs2_truncate(fs, blkno, new_size);
		if (ret)
			goto out;

		ocfs2_free_cached_inode(fs, sf->ci);
		ret = ocfs2_read_cached_inode(fs, blkno, &sf->ci);
		if (ret) {
			sf->ci = NULL;
			goto out;
		}
		di = sf->ci->ci_inode;
	}

	/*
	 * Now that we've adjusted the allocation, write out the
	 * correct i_size.  By design, the slot map's i_size encompasses
	 * the full allocation.
	 */
	di->i_size = di->i_clusters <<
			OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits;
	di->i_mtime = time(NULL);

	ret = ocfs2_write_inode(fs, blkno, (char *)di);
	if (ret)
		goto out;

out:
	return ret;
}

errcode_t ocfs2_format_slot_map(ocfs2_filesys *fs)
{
	errcode_t ret;
	uint64_t blkno;
	struct slotmap_format sf;
	struct ocfs2_slot_map_data *md = NULL;

	ret = ocfs2_lookup_system_inode(fs, SLOT_MAP_SYSTEM_INODE, 0,
					&blkno);
	if (ret)
		goto out;

	ret = ocfs2_read_cached_inode(fs, blkno, &sf.ci);
	if (ret)
		goto out;

	/* verify it is a system file */
	if (!(sf.ci->ci_inode->i_flags & OCFS2_VALID_FL) ||
	    !(sf.ci->ci_inode->i_flags & OCFS2_SYSTEM_FL)) {
		ret = OCFS2_ET_INTERNAL_FAILURE;
		goto out;
	}

	sf.extended = ocfs2_uses_extended_slot_map(OCFS2_RAW_SB(fs->fs_super));
	sf.needed_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;
	if (!sf.extended && (sf.needed_slots > OCFS2_MAX_SLOTS)) {
		ret = OCFS2_ET_TOO_MANY_SLOTS;
		goto out;
	}

	if (sf.extended)
		sf.needed_bytes = sf.needed_slots * sizeof(struct ocfs2_extended_slot);
	else
		sf.needed_bytes = sf.needed_slots * sizeof(__le16);

	ret = ocfs2_size_slot_map(fs, &sf);
	if (ret)
		goto out;

	if (sf.extended)
		sf.actual_slots = sf.ci->ci_inode->i_size / sizeof(struct ocfs2_extended_slot);
	else
		sf.actual_slots = sf.ci->ci_inode->i_size / sizeof(__le16);

	/* This returns an empty map that covers the entire allocation */
	ret = ocfs2_alloc_slot_map_data(sf.actual_slots, &md);
	if (ret)
		return ret;

	ret = ocfs2_store_slot_map(fs, md);

out:
	if (sf.ci)
		ocfs2_free_cached_inode(fs, sf.ci);
	if (md)
		ocfs2_free(&md);

	return ret;
}
