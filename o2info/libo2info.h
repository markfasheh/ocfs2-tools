/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * libo2info.h
 *
 * o2info helper library  prototypes.
 *
 * Copyright (C) 2010 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef __LIBO2INFO_H__
#define __LIBO2INFO_H__

struct o2info_fs_features {
	uint32_t compat;
	uint32_t incompat;
	uint32_t rocompat;
};

struct o2info_volinfo {
	uint32_t blocksize;
	uint32_t clustersize;
	uint32_t maxslots;
	uint8_t label[OCFS2_MAX_VOL_LABEL_LEN];
	uint8_t uuid_str[OCFS2_TEXT_UUID_LEN + 1];
	struct o2info_fs_features ofs;
};

struct o2info_mkfs {
	struct o2info_volinfo ovf;
	uint64_t journal_size;
};

struct o2info_local_freeinode {
	unsigned long total;
	unsigned long free;
};

struct o2info_freeinode {
	int slotnum;
	struct o2info_local_freeinode fi[OCFS2_MAX_SLOTS];
};

#define DEFAULT_CHUNKSIZE (1024*1024)

struct free_chunk_histogram {
	uint32_t fc_chunks[OCFS2_INFO_MAX_HIST];
	uint32_t fc_clusters[OCFS2_INFO_MAX_HIST];
};

struct o2info_freefrag {
	unsigned long chunkbytes;
	uint32_t clusters;
	uint32_t free_clusters;
	uint32_t total_chunks;
	uint32_t free_chunks;
	uint32_t free_chunks_real;
	int clustersize_bits;
	int blksize_bits;
	int chunkbits;
	uint32_t clusters_in_chunk;
	uint32_t chunks_in_group;
	uint32_t min, max, avg; /* chunksize in clusters */
	struct free_chunk_histogram histogram;
};

struct o2info_fiemap {
	uint32_t blocksize;
	uint32_t clustersize;
	uint32_t num_extents;
	uint32_t num_extents_xattr;
	uint32_t clusters;
	uint32_t shared;
	uint32_t holes;
	uint32_t unwrittens;
	uint32_t xattr;
	float frag; /* extents / clusters ratio */
	float score;
};

int o2info_get_fs_features(ocfs2_filesys *fs, struct o2info_fs_features *ofs);
int o2info_get_volinfo(ocfs2_filesys *fs, struct o2info_volinfo *vf);
int o2info_get_mkfs(ocfs2_filesys *fs, struct o2info_mkfs *oms);
int o2info_get_freeinode(ocfs2_filesys *fs, struct o2info_freeinode *ofi);
int o2info_get_freefrag(ocfs2_filesys *fs, struct o2info_freefrag *off);
int o2info_get_fiemap(int fd, int flags, struct o2info_fiemap *ofp);

#endif
