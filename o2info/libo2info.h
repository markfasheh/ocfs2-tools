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

int o2info_get_fs_features(ocfs2_filesys *fs, struct o2info_fs_features *ofs);
int o2info_get_volinfo(ocfs2_filesys *fs, struct o2info_volinfo *vf);
int o2info_get_mkfs(ocfs2_filesys *fs, struct o2info_mkfs *oms);

#endif
