/*
 * defaults.h
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

#ifndef DEFAULTS_H
#define DEFAULTS_H

int get_nodecfghdr_defaults (char *buf, GString **out, int idx, int fld);
int get_nodecfginfo_defaults (char *buf, GString **out, int idx, int fld);
int get_cleanup_log_defaults (char *buf, GString **out, int idx, int fld);
int get_dir_alloc_bitmap_defaults (char *buf, GString **out, int idx, int fld);
int get_dir_alloc_defaults (char *buf, GString **out, int idx, int fld);
int get_vol_disk_header_defaults  (char *buf, GString **out, int idx, int fld);
int get_disk_lock_defaults (char *buf, GString **out, int idx, int fld);
int get_file_alloc_bitmap_defaults (char *buf, GString **out, int idx, int fld);
int get_file_alloc_defaults (char *buf, GString **out, int idx, int fld);
int get_publish_sector_defaults (char *buf, GString **out, int idx, int fld);
int get_recover_log_defaults (char *buf, GString **out, int idx, int fld);
int get_vol_metadata_defaults (char *buf, GString **out, int idx, int fld);
int get_vol_metadata_log_defaults (char *buf, GString **out, int idx, int fld);
int get_vol_label_defaults  (char *buf, GString **out, int idx, int fld);
int get_vote_sector_defaults (char *buf, GString **out, int idx, int fld);
int get_dir_node_defaults (char *buf, GString **out, int idx, int fld);
int get_file_entry_defaults (char *buf, GString **out, int idx, int fld);
int get_extent_header_defaults (char *buf, GString **out, int idx, int fld);
int get_extent_data_defaults (char *buf, GString **out, int idx, int fld);

#endif /* DEFAULTS_H */
