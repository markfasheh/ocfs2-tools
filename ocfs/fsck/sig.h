/*
 * sig.h
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

#ifndef SIG_H
#define SIG_H

int nodecfghdr_sig_match (char *buf, int idx);
int cleanup_log_sig_match (char *buf, int idx);
int dir_alloc_bitmap_sig_match (char *buf, int idx);
int dir_alloc_sig_match (char *buf, int idx);
int vol_disk_header_sig_match  (char *buf, int idx);
int disk_lock_sig_match (char *buf, int idx);
int file_alloc_bitmap_sig_match (char *buf, int idx);
int file_alloc_sig_match (char *buf, int idx);
int publish_sector_sig_match (char *buf, int idx);
int recover_log_sig_match (char *buf, int idx);
int vol_metadata_log_sig_match (char *buf, int idx);
int vol_metadata_sig_match (char *buf, int idx);
int vote_sector_sig_match (char *buf, int idx);
int dir_node_sig_match (char *buf, int idx);
int file_entry_sig_match (char *buf, int idx);
int extent_header_sig_match (char *buf, int idx);
int extent_data_sig_match (char *buf, int idx);

#endif /* SIG_H */
