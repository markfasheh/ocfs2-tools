/*
 * libdebugocfs.h
 *
 * Function prototypes for related 'C' file.
 *
 * Copyright (C) 2002 Oracle Corporation.  All rights reserved.
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
 * Author: Kurt Hackel
 */

#ifndef _LIBDEBUGOCFS_H_
#define _LIBDEBUGOCFS_H_

#define OCFS_DBGLIB_ATTRIB_FILE_CDSL (0x1)

typedef struct _libocfs_stat
{
    char name[255];
    int current_master;
    unsigned long long size;
    unsigned long long alloc_size;
    unsigned int open_map;
    int protection;
    int uid;
    int gid;
    int dir_entries;		/* will be -1 if not a dir */
    int attribs;
    unsigned int cdsl_bitmap;
}
libocfs_stat;

typedef struct _libocfs_volinfo
{
    int major_ver;
    int minor_ver;
    char signature[128];
    char mountpoint[128];
    unsigned long long length;
    unsigned long long num_extents;
    int extent_size;
    char mounted_nodes[32];	/* hardcoded for now! */
    int protection;
    int uid;
    int gid;
}
libocfs_volinfo;

#define OCFS_DBGLIB_MAX_NODE_NAME_LENGTH 32
#define OCFS_DBGLIB_GUID_LEN             32
#define OCFS_DBGLIB_IP_ADDR_LEN          15
typedef struct _libocfs_node
{
    char name[OCFS_DBGLIB_MAX_NODE_NAME_LENGTH + 1];
    char addr[OCFS_DBGLIB_IP_ADDR_LEN + 1];
    int slot;
    char guid[OCFS_DBGLIB_GUID_LEN + 1];
}
libocfs_node;

int libocfs_readdir(const char *dev, const char *dir, int recurse,
		    GArray ** arr);
int libocfs_get_bitmap(const char *dev, char **bmap, int *numbits);
int libocfs_get_volume_info(const char *dev, libocfs_volinfo ** info);
int libocfs_is_ocfs_partition(const char *dev);
int libocfs_chown_volume(const char *dev, int protection, int uid, int gid);
int libocfs_get_node_map(const char *dev, GArray ** arr);
int libocfs_dump_file(const char *dev, const char *path, const char *file);
int libocfs_dump_file_as_node(const char *dev, const char *path,
			      const char *file, int node);

int libocfs_init_raw(void);
int libocfs_cleanup_raw(void);

#endif				/* _LIBDEBUGOCFS_H_ */
