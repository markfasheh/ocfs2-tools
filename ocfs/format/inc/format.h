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

// TODO: need to add version 2 stuff to headers
// BEGIN VERSION 2
enum {
	OCFS_VOL_BM_SYSFILE = OCFS_CLEANUP_LOG_SYSFILE+1,
	OCFS_ORPHAN_DIR_SYSFILE,
	OCFS_JOURNAL_SYSFILE
};
#define OCFS_VOL_BITMAP_FILE         (OCFS_VOL_BM_SYSFILE         * OCFS_MAXIMUM_NODES)
#define OCFS_ORPHAN_DIR              (OCFS_ORPHAN_DIR_SYSFILE     * OCFS_MAXIMUM_NODES)
#define OCFS_JOURNAL_FILE            (OCFS_JOURNAL_SYSFILE        * OCFS_MAXIMUM_NODES)

#define OCFS_JOURNAL_DEFAULT_SIZE       (8 * ONE_MEGA_BYTE)
#define  OCFS_ORPHAN_DIR_FILENAME          "OrphanDir"
#define  OCFS_JOURNAL_FILENAME         	   "JournalFile"
#define  OCFS_LOCAL_ALLOC_SIGNATURE          "LCLBMP"
#define  DIR_NODE_FLAG_ORPHAN         0x02

#define OCFS_JOURNAL_CURRENT_VERSION 1

typedef struct _ocfs_local_alloc
{
	ocfs_disk_lock disk_lock;
	__u8 signature[8];        /* "LCLBMP"                           */
	__u32 alloc_size;         /* num bits taken from main bitmap    */
	__u32 num_used;           /* num bits used (is this needed?)    */
	__u32 bitmap_start;       /* starting bit offset in main bitmap */
	__u32 node_num;           /* which node owns me                 */
	__u64 this_sector;        /* disk offset of this structure      */
	__u8 padding[176];        /* pad out to 256                     */
	__u8 bitmap[256];
}
ocfs_local_alloc;

typedef struct _ocfs_disk_node_config_info2              // CLASS
{
	ocfs_disk_lock disk_lock;                       // DISKLOCK
	__u8 node_name[MAX_NODE_NAME_LENGTH+1];         // CHAR[MAX_NODE_NAME_LENGTH+1]
	ocfs_guid guid;                                 // GUID
	ocfs_ipc_config_info ipc_config;                // IPCONFIG
	__u8 journal_version;
}
ocfs_disk_node_config_info2;                             // END CLASS

// END VERSION 2




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
	__u32 disk_hb;
	__u32 hb_timeo;
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

/* journal.c */
int ocfs_replacement_journal_create(int file, __u64 journal_off);

/* system.c */
int ocfs_init_sysfile (int file, ocfs_vol_disk_hdr *volhdr, __u32 file_id, ocfs_file_entry *fe, __u64 data);
int ocfs_create_root_directory (int file, ocfs_vol_disk_hdr *volhdr);
__u32 ocfs_alloc_from_global_bitmap (__u64 file_size, ocfs_vol_disk_hdr *volhdr);
int ocfs_update_bm_lock_stats(int file);
int ocfs_init_global_alloc_bm (__u32 num_bits, int file, ocfs_vol_disk_hdr *volhdr);
void ocfs_init_dirnode(ocfs_dir_node *dir, __u64 disk_off);

#endif /* _FORMAT_H_ */
