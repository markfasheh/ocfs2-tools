/*
 * ocfsgensysfile.h
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
 * Authors: Neeraj Goyal, Suchit Kaura, Kurt Hackel, Sunil Mushran,
 *          Manish Singh, Wim Coekaerts
 */

#ifndef _OCFSGENSYSFILE_H_
#define _OCFSGENSYSFILE_H_

int ocfs_init_system_file (ocfs_super * osb, __u32 FileId, char *filename,
			   ocfs_file_entry *fe);

int ocfs_read_system_file (ocfs_super * osb,
		__u32 FileId, void *Buffer, __u64 Length, __u64 Offset);

int ocfs_write_system_file (ocfs_super * osb,
		 __u32 FileId, void *Buffer, __u64 Length, __u64 Offset);

__u64 ocfs_file_to_disk_off (ocfs_super * osb, __u32 FileId, __u64 Offset);

int ocfs_get_system_file_size (ocfs_super * osb, __u32 FileId, __u64 * Length, __u64 * AllocSize);

int ocfs_extend_system_file (ocfs_super * osb, __u32 FileId, __u64 FileSize, ocfs_file_entry *fe);

int ocfs_find_extents_of_system_file (ocfs_super * osb,
			 __u64 file_off,
			 __u64 Length,
			 ocfs_file_entry * fe, void **Buffer, __u32 * NumEntries);

int ocfs_free_file_extents (ocfs_super * osb, ocfs_file_entry * fe, __s32 LogNodeNum);

int ocfs_write_map_file (ocfs_super * osb);

#endif				/* _OCFSGENSYSFILE_H_ */
