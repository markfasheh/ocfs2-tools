/*
 * layout.h
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

#ifndef LAYOUT_H
#define LAYOUT_H

#define BLOCKS2BYTES(x)   ((x) << 9)
#define BYTES2BLOCKS(x)   ((x) >> 9)

/* 100% totally hardcoded to the ocfs version!!! */
/*

  block#    datatype                       description
  --------- ------------------------------ --------------------------------------------
  0         ocfs_vol_disk_header           *the* header                     - disk header
  1         ocfs_vol_label                 volume label, volume lock        - locks
  2         ocfs_file_entry (lock only)    bitmap lock                      - locks
  3         ocfs_file_entry (lock only)    nm lock????                      - locks
  4-7       none                           unused                           - 
  8         ocfs_node_config_hdr           autoconfig header                - autoconfig
  9         none                           unused                           - autoconfig
  10-41     ocfs_disk_node_config_info     autoconfig nodes            0-31 - autoconfig 
  42        none                           autoconfig lock (zeroed)         - autoconfig
  43        ocfs_node_config_hdr           exact copy of block#8            - autoconfig
  44-45     none                           unused                           - autoconfig
  46-77     ocfs_publish                   publish nodes               0-31 - publish
  78-109    ocfs_vote                      vote nodes                  0-31 - vote
  110-2157  bitmap                         1MB (8 million bits max)         - volume bitmap
  2158-2671 none                           514 free sectors                 - free sectors
  2672-2703 ocfs_file_entry                OCFS_VOL_MD_SYSFILE         0-31 - data_start_off
  2704-2735 ocfs_file_entry                OCFS_VOL_MD_LOG_SYSFILE     0-31 - 
  2736-2767 ocfs_file_entry                OCFS_DIR_SYSFILE            0-31 - 
  2768-2799 ocfs_file_entry                OCFS_DIR_BM_SYSFILE         0-31 - 
  2800-2831 ocfs_file_entry                OCFS_FILE_EXTENT_SYSFILE    0-31 - 
  2832-2863 ocfs_file_entry                OCFS_FILE_EXTENT_BM_SYSFILE 0-31 -
  2864-2895 ocfs_file_entry                OCFS_RECOVER_LOG_SYSFILE    0-31 - 
  2896-2927 ocfs_file_entry                OCFS_CLEANUP_LOG_SYSFILE    0-31 - 
  ####      ocfs_dir_node                  the root directory               - root_off (offset can vary)
  ####      none                           1022 free sectors                - end sectors

*/

typedef struct _ocfs_disk_structure ocfs_disk_structure;

#include "fsck.h"

enum
{
	unused = 0,
	vol_disk_header,
	vol_label_lock,
	bitmap_lock,
	nm_lock,
	node_cfg_hdr,
	node_cfg_info,
	publish_sector,
	vote_sector,
	volume_bitmap,
	free_sector,
	vol_metadata,
	vol_metadata_log,
	dir_alloc,
	dir_alloc_bitmap,
	file_alloc,
	file_alloc_bitmap,
	recover_log,
	cleanup_log,
	dir_node,
	file_entry,
	extent_header,
	extent_data,
};


struct _ocfs_disk_structure
{
	int type;
	ocfs_class *cls;
	int (*sig_match) (char *buf, int idx);
	int (*read) (int fd, char *buf, __u64 offset, int idx);
	int (*write) (int fd, char *buf, __u64 offset, int idx);
	int (*verify) (int fd, char *buf, __u64 offset, int idx, GHashTable **bad);
	int (*output) (char *buf, int idx, GHashTable *bad, FILE *f);
	int (*defaults) (char *buf, GString **out, int idx, int fld);
};

#define ANY_BLOCK  (0xffffffffUL)
typedef struct _ocfs_layout_t
{
	__u32    block;
	__u32    num_blocks;
	ocfs_disk_structure *kind; 
	char     name[50];
} ocfs_layout_t;
	
/* 
 * these are all version 1.0.0 constants 
 * I could have derived them, but what's the point?  
 * they're konstant ;-)
 * I may later include the headers for mkfs/module
 * but I'm feeling lazy now...
 */
#define OCFSCK_BITMAP_OFF           56320LLU    /* block# 110 */
#define OCFSCK_PUBLISH_OFF          23552LLU    /* block# 46  */
#define OCFSCK_VOTE_OFF             39936LLU    /* block# 78  */
#define OCFSCK_AUTOCONF_OFF         4096LLU     /* block# 8 */
#define OCFSCK_AUTOCONF_SIZE        17408LLU    /* 34 blocks */
#define OCFSCK_NEW_CFG_OFF          21504LLU    /* block# 42 */
#define OCFSCK_DATA_START_OFF       1368064LLU  /* block# 2672 */
#define OCFSCK_INTERNAL_OFF         OCFSCK_DATA_START_OFF
#define OCFSCK_ROOT_OFF 	    2416640LLU  /* block# 4720 */
#define OCFSCK_MIN_MOUNT_POINT_LEN  (strlen("/a"))
#define OCFSCK_END_SECTOR_BYTES     (1022*512)
#define OCFSCK_NON_DATA_AREA        (OCFSCK_DATA_START_OFF+OCFSCK_END_SECTOR_BYTES)
#define OCFSCK_MAX_CLUSTERS         (1024*1024*8)  /* maximum 1mb (8megabits) */


#define BM_DATA_BIT_SET         0x01	
#define BM_ALLOCATED            0x02
#define BM_SIG_BIT_1            0x04
#define BM_SIG_BIT_2            0x08
#define BM_SIG_BIT_3            0x10
#define BM_MULTIPLY_ALLOCATED   0x20
#define BM_RESERVED_1           0x40
#define BM_RESERVED_2           0x80
				
#define BM_SIG_NONE             (0)
#define BM_SIG_FILE             (BM_SIG_BIT_1)
#define BM_SIG_DIR              (BM_SIG_BIT_2)
#define BM_SIG_EXTHDR           (BM_SIG_BIT_3)
#define BM_SIG_EXTDAT           (BM_SIG_BIT_1|BM_SIG_BIT_2)
#define BM_SIG_VOL              (BM_SIG_BIT_1|BM_SIG_BIT_3)
#define BM_SIG_NODECFG          (BM_SIG_BIT_2|BM_SIG_BIT_3)
#define BM_SIG_OTHER            (BM_SIG_BIT_1|BM_SIG_BIT_2|BM_SIG_BIT_3)


#define ONE_KB                     (1024)
#define ONE_MB                     (ONE_KB*ONE_KB)

#define OCFSCK_LO_CLUSTER_SIZE     (4 * ONE_KB)
#define OCFSCK_HI_CLUSTER_SIZE     (ONE_MB)
#define OCFSCK_DIR_NODE_SIZE       (128 * ONE_KB)
#define OCFSCK_BITMAP_DATA_SIZE    (8 * ONE_MB)






#if 0

1. traverse each directory, checking each dir header and file entry signature
2. check the allocation for each file and dir, matching it back to a bit in the global bitmap
3. link any set bits with no matching file entry to lost+found data chunks
4. do interactive repairs of the header block with appropriate defaults
5. make a lock-state mode which can allow the user to change the lock state of any file entry
7. mark non set bits as set in bitmap according to space usage
8. verify if this is mounted filesystem or not

** 6. allow some (as yet undefined) editing of autoconfig sectors
** 9. clearing of bloated system files

#endif

ocfs_layout_t * find_nxt_hdr_struct(int type, int start);
ocfs_disk_structure * find_matching_struct(char *buf, int idx);

#ifndef LAYOUT_LOCAL_DEFS
extern ocfs_disk_structure dirnode_t;
extern ocfs_disk_structure fileent_t;
extern ocfs_disk_structure exthdr_t;
extern ocfs_disk_structure extdat_t;
extern ocfs_disk_structure diskhdr_t;
extern ocfs_disk_structure vollabel_t;
extern ocfs_disk_structure bmlock_t;
extern ocfs_disk_structure nmlock_t;
extern ocfs_disk_structure unused_t;
extern ocfs_disk_structure free_t;
extern ocfs_disk_structure nodecfghdr_t;
extern ocfs_disk_structure nodecfginfo_t;
extern ocfs_disk_structure publish_t;
extern ocfs_disk_structure vote_t;
extern ocfs_disk_structure volbm_t;
extern ocfs_disk_structure volmd_t;
extern ocfs_disk_structure volmdlog_t;
extern ocfs_disk_structure diralloc_t;
extern ocfs_disk_structure dirallocbm_t;
extern ocfs_disk_structure filealloc_t;
extern ocfs_disk_structure fileallocbm_t;
extern ocfs_disk_structure recover_t;
extern ocfs_disk_structure cleanup_t;
extern ocfs_layout_t ocfs_header_layout[];
extern int ocfs_header_layout_sz;
extern ocfs_layout_t ocfs_data_layout[];
extern int ocfs_data_layout_sz;
extern ocfs_layout_t ocfs_dir_layout[];
extern int ocfs_dir_layout_sz;
extern ocfs_disk_structure *ocfs_all_structures[];
extern int ocfs_all_structures_sz;
#endif /* LAYOUT_LOCAL_DEFS */

#endif /* LAYOUT_H */
