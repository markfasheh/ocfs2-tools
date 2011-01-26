/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * o2cb_scandisk.h
 *
 * Copyright (C) 2010 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License, version 2,  as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <dirent.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tools-internal/scandisk.h"
#include "o2cb/o2cb.h"

#include "ocfs2-kernel/kernel-list.h"

struct o2cb_device {
	struct list_head od_list;
	char *od_uuid;
#define O2CB_DEVICE_FOUND		0x01
#define O2CB_DEVICE_HB_STARTED		0x02
	int od_flags;
	struct o2cb_region_desc od_region;
	struct o2cb_cluster_desc od_cluster;
};

void o2cb_scandisk(struct list_head *hbdevs);
