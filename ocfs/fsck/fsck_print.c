/*
 * fsck_print.c
 *
 * print function for each structure type in ocfs
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

#include "fsck.h"


int print_dir_node (char *buf, int idx, GHashTable *bad, FILE *f)
{
	return print_class(buf, &ocfs_dir_node_class, f, bad);
}
int print_file_entry(char *buf, int idx, GHashTable *bad, FILE *f) 
{ 
	return print_class(buf, &ocfs_file_entry_class, f, bad);
}

int print_extent_header(char *buf, int idx, GHashTable *bad, FILE *f) 
{ 
	return print_class(buf, &ocfs_extent_group_class, f, bad);
}

int print_extent_data(char *buf, int idx, GHashTable *bad, FILE *f) 
{ 
	return print_class(buf, &ocfs_extent_group_class, f, bad);
}

int print_vol_disk_header(char *buf, int idx, GHashTable *bad, FILE *f) 
{ 
	return print_class(buf, &ocfs_vol_disk_hdr_class, f, bad);
}

int print_vol_label(char *buf, int idx, GHashTable *bad, FILE *f) 
{ 
	return print_class(buf, &ocfs_vol_label_class, f, bad);
}

int print_disk_lock(char *buf, int idx, GHashTable *bad, FILE *f) 
{ 
	return print_class(buf, &ocfs_disk_lock_class, f, bad);
}

int print_nodecfghdr(char *buf, int idx, GHashTable *bad, FILE *f) 
{
	return print_class(buf, &ocfs_node_config_hdr_class, f, bad);
}

int print_nodecfginfo(char *buf, int idx, GHashTable *bad, FILE *f) 
{
	return print_class(buf, &ocfs_disk_node_config_info_class, f, bad);
}

int print_publish_sector(char *buf, int idx, GHashTable *bad, FILE *f) 
{ 
	return print_class(buf, &ocfs_publish_class, f, bad);
}

int print_vote_sector(char *buf, int idx, GHashTable *bad, FILE *f) 
{ 
	return print_class(buf, &ocfs_vote_class, f, bad);
}

int print_volume_bitmap(char *buf, int idx, GHashTable *bad, FILE *f) 
{ 
	return 0; //print_class(buf, &ocfs_volume_bitmap_class, f, bad);
}

int print_vol_metadata(char *buf, int idx, GHashTable *bad, FILE *f) 
{ 
	return print_class(buf, &ocfs_file_entry_class, f, bad);
}

int print_vol_metadata_log(char *buf, int idx, GHashTable *bad, FILE *f) 
{ 
	return print_class(buf, &ocfs_file_entry_class, f, bad);
}

int print_dir_alloc(char *buf, int idx, GHashTable *bad, FILE *f) 
{ 
	return print_class(buf, &ocfs_file_entry_class, f, bad);
}

int print_dir_alloc_bitmap(char *buf, int idx, GHashTable *bad, FILE *f) 
{ 
	return print_class(buf, &ocfs_file_entry_class, f, bad);
}

int print_file_alloc(char *buf, int idx, GHashTable *bad, FILE *f) 
{ 
	return print_class(buf, &ocfs_file_entry_class, f, bad);
}

int print_file_alloc_bitmap(char *buf, int idx, GHashTable *bad, FILE *f) 
{ 
	return print_class(buf, &ocfs_file_entry_class, f, bad);
}

int print_recover_log(char *buf, int idx, GHashTable *bad, FILE *f) 
{ 
	return print_class(buf, &ocfs_file_entry_class, f, bad);
}

int print_cleanup_log(char *buf, int idx, GHashTable *bad, FILE *f) 
{ 
	return print_class(buf, &ocfs_file_entry_class, f, bad);
}
