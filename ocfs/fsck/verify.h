/*
 * verify.h
 *
 * Function prototypes, macros, etc. for related 'C' files
 *
 * Copyright (C) 2003 Oracle.  All rights reserved.
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
 * Authors: Kurt Hackel, Sunil Mushran
 */

#ifndef VERIFY_H
#define VERIFY_H

int test_member_range(ocfs_class *cl, const char *name, char *buf);
int check_outside_bounds(char *buf, int structsize);
int verify_vol_disk_header  (int fd, char *buf, __u64 offset, int idx, GHashTable **bad);
int verify_disk_lock (int fd, char *buf, __u64 offset, int idx, GHashTable **bad);
int verify_vol_label  (int fd, char *buf, __u64 offset, int idx, GHashTable **bad);
int verify_nodecfghdr (int fd, char *buf, __u64 offset, int idx, GHashTable **bad);
int verify_nodecfginfo (int fd, char *buf, __u64 offset, int idx, GHashTable **bad);
int verify_cleanup_log (int fd, char *buf, __u64 offset, int idx, GHashTable **bad);
int verify_dir_alloc (int fd, char *buf, __u64 offset, int idx, GHashTable **bad);
int verify_dir_alloc_bitmap (int fd, char *buf, __u64 offset, int idx, GHashTable **bad);
int verify_file_alloc (int fd, char *buf, __u64 offset, int idx, GHashTable **bad);
int verify_file_alloc_bitmap (int fd, char *buf, __u64 offset, int idx, GHashTable **bad);
int verify_publish_sector (int fd, char *buf, __u64 offset, int idx, GHashTable **bad);
int verify_recover_log (int fd, char *buf, __u64 offset, int idx, GHashTable **bad);
int verify_vol_metadata (int fd, char *buf, __u64 offset, int idx, GHashTable **bad);
int verify_vol_metadata_log (int fd, char *buf, __u64 offset, int idx, GHashTable **bad);
int verify_volume_bitmap (int fd, char *buf, __u64 offset, int idx, GHashTable **bad);
int verify_vote_sector (int fd, char *buf, __u64 offset, int idx, GHashTable **bad);
int verify_dir_node (int fd, char *buf, __u64 offset, int idx, GHashTable **bad);
int verify_file_entry (int fd, char *buf, __u64 offset, int idx, GHashTable **bad);
int verify_extent_group (int fd, char *buf, __u64 offset, int idx, GHashTable **bad,
			 int type, __u64 up_ptr);
int verify_extent_header (int fd, char *buf, __u64 offset, int idx, GHashTable **bad);
int verify_extent_data (int fd, char *buf, __u64 offset, int idx, GHashTable **bad);
int verify_system_file_entry (int fd, char *buf, __u64 offset, int idx, GHashTable **bad, 
			      char *fname, int type);
int load_volume_bitmap(void);

#endif /* VERIFY_H */
