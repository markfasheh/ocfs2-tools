/*
 * frmtport.h
 *
 * Function prototypes for related 'C' file.
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
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

#ifndef _FRMTPORT_H_
#define _FRMTPORT_H_

/* function prototypes */
void *MemAlloc(__u32 size);

int OpenDisk(char *device);

int GetDiskGeometry(int file, __u64 * vollength, __u32 * sect_size);

int SetSeek(int file, __u64 offset);

int Read(int file, __u32 size, char *buf);

int Write(int file, __u32 size, char *buf);

int GenerateVolumeID(char *volid, int volidlen);

void version(char *progname);

void usage(char *progname);

int ValidateOptions(void);

int GetRandom(char *randbuf, int randlen);

int ReadPublish(int file, __u64 publ_off, __u32 sect_size, void **buf);

uid_t get_uid(char *id);

gid_t get_gid(char *id);

int read_sectors(int file, __u64 strtoffset, __u32 noofsects, __u32 sect_size,
		 void *buf);

int write_sectors(int file, __u64 strtoffset, __u32 noofsects, __u32 sect_size,
		  void *buf);

int validate_volume_size(__u64 given_vol_size, __u64 actual_vol_size);

void num_to_str(__u64 num, char *numstr);

int is_ocfs_volume(int file, ocfs_vol_disk_hdr *volhdr, bool *ocfs_vol,
		   __u32 sect_size);

int check_heart_beat(int *file, char *device, ocfs_vol_disk_hdr *volhdr,
		     __u32 *nodemap, __u32 sect_size);

int get_node_names(int file, ocfs_vol_disk_hdr *volhdr, char **node_names,
		   __u32 sect_size);

void print_node_names(char **node_names, __u32 nodemap);

#endif /* _FRMTPORT_H_ */
