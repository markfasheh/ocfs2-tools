/*
 * ocfsiosup.h
 *
 * Function prototypes for related 'C' file.
 *
 * Copyright (C) 2002, 2003 Oracle.  All rights reserved.
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

#ifndef _OCFSIOSUP_H_
#define _OCFSIOSUP_H_

#define ocfs_write_sector(osb, buf, off)	\
			ocfs_write_disk(osb, buf, OCFS_SECTOR_SIZE, off)

#define ocfs_read_sector(osb, buf, off)		\
			ocfs_read_disk(osb, buf, OCFS_SECTOR_SIZE, off)

int LinuxWriteForceDisk (ocfs_super * osb, void *Buffer, __u32 Length,
			 __u64 Offset, bool Cached);

int LinuxReadForceDisk (ocfs_super * osb, void *Buffer, __u32 Length,
			__u64 Offset, bool Cached);

#define ocfs_write_metadata(osb, buf, len, off)		\
 		LinuxWriteForceDisk(osb, buf, len, off, false)
 
#define ocfs_read_metadata(osb, buf, len, off)		\
 		LinuxReadForceDisk(osb, buf, len, off, false)

#if 0	
int ocfs_write_metadata (ocfs_super * osb, void *Buffer, __u32 Length,
			 __u64 Offset);

int ocfs_read_metadata (ocfs_super * osb, void *Buffer, __u32 Length,
			__u64 Offset);
#endif

int ocfs_write_force_disk (ocfs_super * osb, void *Buffer, __u32 Length,
			   __u64 Offset);

int ocfs_write_disk (ocfs_super * osb, void *Buffer, __u32 Length, __u64 Offset);

int ocfs_read_force_disk (ocfs_super * osb, void *Buffer, __u32 Length,
			  __u64 Offset);

int ocfs_read_force_disk_ex (ocfs_super * osb, void **Buffer, __u32 AllocLen,
			     __u32 ReadLen, __u64 Offset);

int ocfs_read_disk (ocfs_super * osb, void *Buffer, __u32 Length, __u64 Offset);

int ocfs_read_disk_ex (ocfs_super * osb, void **Buffer, __u32 AllocLen,
		       __u32 ReadLen, __u64 Offset);

#endif				/* _OCFSIOSUP_H_ */
