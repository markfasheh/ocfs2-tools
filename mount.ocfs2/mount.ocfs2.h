/*
 * mount.ocfs2.h  Definitions, etc.
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
 */

#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE /* Because libc really doesn't want us using O_DIRECT? */

#include <sys/types.h>
#include <asm/types.h>
#include <inttypes.h>

#include <asm/page.h>
#include <sys/mount.h>
#include <dirent.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fd.h>
#include <string.h>
#include <sys/stat.h>

#include "fstab.h"
#include "nls.h"
#include "paths.h"
#include "realpath.h"
#include "sundries.h"
#include "xmalloc.h"
#include "mntent.h"
#include "mount_constants.h"
#include "opts.h"

#include <ocfs2.h>
#include <ocfs2_fs.h>
#include <ocfs1_fs_compat.h>
#include <kernel-list.h>

#include "bitops.h"

#include "ocfs2_nodemanager.h"
#include "ocfs2_heartbeat.h"
#include "ocfs2_tcp.h"

#define CLUSTER_FILE   "/proc/cluster/nm/.cluster"
#define GROUP_FILE     "/proc/cluster/nm/.group"
#define NODE_FILE      "/proc/cluster/nm/.node"
#define HEARTBEAT_DISK_FILE "/proc/cluster/heartbeat/.disk"
