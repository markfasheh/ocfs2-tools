/*
 * format.h
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

#ifndef _FORMAT_H_
#define _FORMAT_H_

#include <libocfs.h>
#include <bindraw.h>
#include <sys/ioctl.h>
#include <ctype.h>
#include <pwd.h>
#include <grp.h>

#define  OCFS_MAXIMUM_NODES            32
#define  FILE_NAME_SIZE                200
#define  OCFS_MAX_DIRECTORY_ENTRIES    100
#define  OCFS_NUM_FREE_SECTORS         512             /* in sectors */
#define  OCFS_NUM_END_SECTORS          1024            /* in sectors */
#define  OCFS_MAX_BITMAP_SIZE          1024 * 1024     /* in bytes */
#define  CLEAR_DATA_BLOCK_SIZE         2048            /* in sectors */

#define  OCFS_MIN_VOL_SIZE             (200 * 1024 * 1024)	/* in bytes */

#define  OCFS_FORMAT_NAME             "mkfs.ocfs"
#define  OCFS_RESIZE_NAME             "resizeocfs"

#define  OCFS_HBT_WAIT			10

#define  OCFS_BUFFER_ALIGN(buf, secsz)  ((__u64)buf +                 \
                                         (((__u64)buf % secsz) ?      \
                                          (secsz - ((__u64)buf % secsz)) : 0))

#define PRINT_PROGRESS()					\
	if (opts.print_progress) {				\
		__u64	__p;					\
		__p = (sect_count * 100) / format_size; 	\
		if (sect_count != format_size)			\
			printf("%llu\n", __p);			\
		else						\
			printf("COMPLETE\n");			\
	}

#define PRINT_VERBOSE(f, a...)	do { if (!opts.quiet) printf(f, ##a); \
				} while (0)

#define KILO_BYTE	(1024)
#define MEGA_BYTE	(KILO_BYTE * 1024)
#define GIGA_BYTE	(MEGA_BYTE * 1024)
#define TERA_BYTE	((__u64)GIGA_BYTE * (__u64)1024)

#define MULT_FACTOR(c, f)				\
	do {						\
		switch (c) {				\
		case 'k':				\
		case 'K': (f) = KILO_BYTE; break;	\
		case 'm':				\
		case 'M': (f) = MEGA_BYTE; break;	\
		case 'g':				\
		case 'G': (f) = GIGA_BYTE; break;	\
		case 't':				\
		case 'T': (f) = TERA_BYTE; break;	\
		default : (f) = 1; break;		\
		}					\
	} while (0)

#undef MIN
#define MIN(a, b)  (((a) < (b)) ? (a) : (b))

#define safefree(a)  do { if(a) { free_aligned(a); (a) = NULL; } }while(0)
#define safeclose(a)  do { if(a) { close(a); (a) = 0; } }while(0)

typedef struct _ocfs_options
{
	__u8 device[FILE_NAME_SIZE];
	__u32 block_size;
	bool clear_data_blocks;
	bool force_op;
	gid_t gid;
	__u8 volume_label[MAX_VOL_LABEL_LEN];
	__u8 mount_point[FILE_NAME_SIZE];
	bool query_only;
	mode_t perms;
	bool quiet;
	uid_t uid;
	bool print_progress;
	__u32 slot_num;
	__u64 device_size;
	bool list_nodes;
	int convert;
}
ocfs_options;

#include <frmtport.h>

/* function prototypes */
int CheckForceFormat(int file, __u64 *publ_off, bool *ocfs_vol, __u32 sect_size);

int CheckHeartBeat(int *file, __u64 publ_off, __u32 sect_size);

int WriteVolumeHdr(int file, ocfs_vol_disk_hdr * volhdr, __u64 offset,
		   __u32 sect_size);

int WriteVolumeLabel(int file, char *volid, __u32 volidlen, __u64 offset,
		     __u32 sect_size);

int InitNodeConfHdr(int file, __u64 offset, __u32 sect_size);

int ClearSectors(int file, __u64 strtoffset, __u32 noofsects, __u32 sect_size);

int ClearBitmap(int file, ocfs_vol_disk_hdr * volhdr);

int ClearDataBlocks(int file, ocfs_vol_disk_hdr * volhdr, __u32 sect_size);

int ReadOptions(int argc, char **argv);

int InitVolumeDiskHeader(ocfs_vol_disk_hdr *volhdr, __u32 sect_size,
			 __u64 vol_size, __u64 * non_data_size);

void InitVolumeLabel(ocfs_vol_label * vollbl, __u32 sect_size, char *id,
		     __u32 id_len);

void SetNodeConfigHeader(ocfs_node_config_hdr * nodehdr);

void ShowDiskHdrVals(ocfs_vol_disk_hdr * voldiskhdr);

void HandleSignal(int sig);
#endif /* _FORMAT_H_ */
