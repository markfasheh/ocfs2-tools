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

errcode_t get_uuid(char *dev, char *uuid)
{
	ocfs2_filesys *fs = NULL;
	errcode_t ret;

	ret = ocfs2_open(dev, OCFS2_FLAG_RO, 0, 0, &fs);
	if (ret)
		goto out;

	strcpy(uuid, fs->uuid_str);

	ocfs2_close(fs);

out:
	return ret;
}

errcode_t start_heartbeat(char *device)
{
	errcode_t err;
	ocfs2_filesys *fs = NULL;

	err = ocfs2_open(device, OCFS2_FLAG_RO, 0, 0, &fs);
	if (err)
		goto bail;

	err = ocfs2_start_heartbeat(fs);

bail:
	if (fs)
		ocfs2_close(fs);

	return err;
}

errcode_t stop_heartbeat(const char *hbuuid)
{
	errcode_t err;

	err = o2cb_remove_heartbeat_region_disk(NULL, hbuuid);

	return err;
}
