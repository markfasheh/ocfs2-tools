/*
 * ocfsconst.h
 *
 * constants
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

#define  OCFS_DIR_FILENAME                 "DirFile"
#define  OCFS_DIR_BITMAP_FILENAME          "DirBitMapFile"
#define  OCFS_FILE_EXTENT_FILENAME         "ExtentFile"
#define  OCFS_FILE_EXTENT_BITMAP_FILENAME  "ExtentBitMapFile"
#define  OCFS_RECOVER_LOG_FILENAME         "RecoverLogFile"
#define  OCFS_CLEANUP_LOG_FILENAME         "CleanUpLogFile"

#define  ONE_SECOND              (10 * 1000 * 1000)  /* 100 nanosec unit */
#define  ONE_MILLI_SEC           (10 * 1000)         /* 100 nanosec unit */
#define  ONE_MEGA_BYTE           (1 * 1024 * 1024)   /* in bytes */

#define  MISS_COUNT_VALUE        40
#define  MIN_MISS_COUNT_VALUE    5

/* values are in ms */
#define  OCFS_MIN_DISKHB	500
#define  OCFS_MAX_DISKHB	5000
#define  OCFS_MIN_HBTIMEO	10000
#define  OCFS_MAX_HBTIMEO	60000

#define  OCFS_DEFAULT_DIR_NODE_SIZE  (1024 * 128)
#define  OCFS_DEFAULT_FILE_NODE_SIZE (512)

#define  OCFS_PAGE_SIZE		4096

#define  ULONGLONG_MAX		(~0ULL)

/*
** The following flag values reflect the operation to be performed
**   by ocfs_create_modify_file
*/
#define  FLAG_FILE_CREATE         0x1
#define  FLAG_FILE_EXTEND         0x2
#define  FLAG_FILE_DELETE         0x4
#define  FLAG_FILE_RENAME         0x8
#define  FLAG_FILE_UPDATE         0x10
#define  FLAG_FILE_CREATE_DIR     0x40
#define  FLAG_FILE_UPDATE_OIN     0x80
#define  FLAG_FILE_RELEASE_MASTER 0x100
#define  FLAG_CHANGE_MASTER       0x400
#define  FLAG_ADD_OIN_MAP         0x800
#define  FLAG_DIR                 0x1000
#define  FLAG_DEL_NAME            0x20000
#define  FLAG_RESET_VALID         0x40000
#define  FLAG_FILE_RELEASE_CACHE  0x400000
#define  FLAG_FILE_CREATE_CDSL    0x800000
#define  FLAG_FILE_DELETE_CDSL    0x1000000
#define  FLAG_FILE_CHANGE_TO_CDSL 0x4000000
#define  FLAG_FILE_TRUNCATE       0x8000000   // kch- consider removing this and ocfs_truncate_file
#define  FLAG_FILE_ACQUIRE_LOCK   0x10000000
#define  FLAG_FILE_RELEASE_LOCK   0x20000000

enum {
    OCFS_INVALID_SYSFILE = -1,
    OCFS_VOL_MD_SYSFILE = 0,
    OCFS_VOL_MD_LOG_SYSFILE,
    OCFS_DIR_SYSFILE,
    OCFS_DIR_BM_SYSFILE,
    OCFS_FILE_EXTENT_SYSFILE,
    OCFS_FILE_EXTENT_BM_SYSFILE,
    OCFS_RECOVER_LOG_SYSFILE,
    OCFS_CLEANUP_LOG_SYSFILE,
#ifdef LOCAL_ALLOC
    OCFS_VOL_BM_SYSFILE
#endif
};
#define OCFS_FILE_VOL_META_DATA      (OCFS_VOL_MD_SYSFILE         * OCFS_MAXIMUM_NODES)
#define OCFS_FILE_VOL_LOG_FILE       (OCFS_VOL_MD_LOG_SYSFILE     * OCFS_MAXIMUM_NODES)
#define OCFS_FILE_DIR_ALLOC          (OCFS_DIR_SYSFILE            * OCFS_MAXIMUM_NODES)
#define OCFS_FILE_DIR_ALLOC_BITMAP   (OCFS_DIR_BM_SYSFILE         * OCFS_MAXIMUM_NODES)
#define OCFS_FILE_FILE_ALLOC         (OCFS_FILE_EXTENT_SYSFILE    * OCFS_MAXIMUM_NODES)
#define OCFS_FILE_FILE_ALLOC_BITMAP  (OCFS_FILE_EXTENT_BM_SYSFILE * OCFS_MAXIMUM_NODES)
#define LOG_FILE_BASE_ID             (OCFS_RECOVER_LOG_SYSFILE    * OCFS_MAXIMUM_NODES)
#define CLEANUP_FILE_BASE_ID         (OCFS_CLEANUP_LOG_SYSFILE    * OCFS_MAXIMUM_NODES)
#ifdef LOCAL_ALLOC
#define OCFS_VOL_BITMAP_FILE         (OCFS_VOL_BM_SYSFILE         * OCFS_MAXIMUM_NODES)
#endif


#define  OCFS_LOG_SECTOR_SIZE        9
#define  OCFS_SECTOR_SIZE            (1<<OCFS_LOG_SECTOR_SIZE)
#define  OCFS_MOD_SECTOR_SIZE        (OCFS_SECTOR_SIZE - 1)
#define  OCFS_MAXIMUM_NODES          32
#define  OCFS_MAX_FILENAME_LENGTH    255

#define  OCFS_VOLUME_LOCK_OFFSET     (OCFS_SECTOR_SIZE)
/* change this to some other sector, change format TODO */
#define  OCFS_BITMAP_LOCK_OFFSET     (OCFS_SECTOR_SIZE * 2)

#define  OCFS_MAX_BITMAP_SIZE          1024 * 1024

#define  HEARTBEAT_METHOD_DISK       (1)
#define  HEARTBEAT_METHOD_IPC        (2)

/*
** Extents Defines
*/
#define  OCFS_EXTENT_DATA             1
#define  OCFS_EXTENT_HEADER           2

#define  OCFS_MAX_FILE_ENTRY_EXTENTS  3
#define  OCFS_MAX_DATA_EXTENTS        18
#define  NUM_SECTORS_IN_LEAF_NODE     1

/*
** Structure signatures 
*/
#define  OCFS_TYPE_OFILE            (0x02534643)
#define  OCFS_TYPE_OIN            (0x03534643)
#define  OCFS_TYPE_OSB            (0x05534643)
#define  OCFS_TYPE_GLOBAL_DATA    (0x07534643)

#define  CACHE_LOCK_SLOT_TIME          (ONE_SECOND * 10)

#define  OCFS_DLM_NO_LOCK              (0x0)
#define  OCFS_DLM_SHARED_LOCK          (0x1)
#define  OCFS_DLM_EXCLUSIVE_LOCK       (0x2)
#define  OCFS_DLM_ENABLE_CACHE_LOCK    (0x8)

#define  OCFS_INVALID_NODE_NUM         UINT_MAX

typedef enum _ocfs_rw_mode
{
	OCFS_READ,
	OCFS_WRITE
}
ocfs_rw_mode;

#define  FLAG_ALWAYS_UPDATE_OPEN       0x1
#define  LOCK_STATE_INIT               0x2
#define  LOCK_STATE_IN_VOTING          0x4

#define  OCFS_OIN_IN_TEARDOWN                    (0x00000002)
#define  OCFS_OIN_DIRECTORY                      (0x00000008)
#define  OCFS_OIN_ROOT_DIRECTORY                 (0x00000010)
#define  OCFS_OIN_CACHE_UPDATE                   (0x00000100)
#define  OCFS_OIN_DELETE_ON_CLOSE                (0x00000200)
#define  OCFS_OIN_NEEDS_DELETION                 (0x00000400)
#define  OCFS_INITIALIZED_MAIN_RESOURCE          (0x00002000)
#define  OCFS_INITIALIZED_PAGING_IO_RESOURCE     (0x00004000)
#define  OCFS_OIN_INVALID                        (0x00008000)
#define  OCFS_OIN_IN_USE                         (0x00020000)
#define  OCFS_OIN_OPEN_FOR_DIRECTIO              (0x00100000)
#define  OCFS_OIN_OPEN_FOR_WRITE                 (0x00200000)


#define  OCFS_OSB_FLAGS_BEING_DISMOUNTED  (0x00000004)
#define  OCFS_OSB_FLAGS_SHUTDOWN          (0x00000008)
#define  OCFS_OSB_FLAGS_OSB_INITIALIZED   (0x00000020)

#define  OCFS_FLAG_GLBL_CTXT_RESOURCE_INITIALIZED (0x00000001)
#define  OCFS_FLAG_MEM_LISTS_INITIALIZED          (0x00000002)
#define  OCFS_FLAG_SHUTDOWN_VOL_THREAD            (0x00000004)

#define  DIR_NODE_FLAG_ROOT           0x1

/*
** Information on Publish sector of each node
*/
#define  DISK_HBEAT_COMM_ON           20	/* in the order of 5 secs */
#define  DISK_HBEAT_NO_COMM           4		/* in the order of 1 sec */
#define  DISK_HBEAT_INVALID           0		/* in the order of 100ms */

/*
** Information on Vote sector of each node
*/
#define  FLAG_VOTE_NODE               0x1
#define  FLAG_VOTE_OIN_UPDATED        0x2
#define  FLAG_VOTE_OIN_ALREADY_INUSE  0x4
#define  FLAG_VOTE_UPDATE_RETRY       0x8
#define  FLAG_VOTE_FILE_DEL           0x10

/*
** File Entry contains this information
*/
#define  OCFS_SYNC_FLAG_DELETED            (0)
#define  OCFS_SYNC_FLAG_VALID              (0x1)
#define  OCFS_SYNC_FLAG_CHANGE             (0x2)
#define  OCFS_SYNC_FLAG_MARK_FOR_DELETION  (0x4)
#define  OCFS_SYNC_FLAG_NAME_DELETED       (0x8)

#define  OCFS_ATTRIB_DIRECTORY             (0x1)
#define  OCFS_ATTRIB_FILE_CDSL             (0x8)
#define  OCFS_ATTRIB_CHAR                  (0x10)
#define  OCFS_ATTRIB_BLOCK                 (0x20)
#define  OCFS_ATTRIB_REG                   (0x40)
#define  OCFS_ATTRIB_FIFO                  (0x80)
#define  OCFS_ATTRIB_SYMLINK               (0x100)
#define  OCFS_ATTRIB_SOCKET                (0x200)

#define  INVALID_DIR_NODE_INDEX              -1
#define  INVALID_NODE_POINTER                -1
#define  OCFS_DIR_NODE_SIGNATURE             "DIRNV20"
#define  OCFS_FILE_ENTRY_SIGNATURE           "FIL"
#define  OCFS_EXTENT_HEADER_SIGNATURE        "EXTHDR2"
#define  OCFS_EXTENT_DATA_SIGNATURE          "EXTDAT1"

#define  MAX_IP_ADDR_LEN	32

#define  OCFS_IP_ADDR           "ip_address"
#define  OCFS_IP_PORT           "ip_port"
#define  OCFS_IP_MASK           "subnet_mask"
#define  OCFS_COMM_TYPE         "type"

#define OCFS_SEM_MAGIC         0xAFABFACE
#define OCFS_SEM_DELETED       0x0D0D0D0D

#define SHUTDOWN_SIGS   (sigmask(SIGKILL) | sigmask(SIGHUP) | \
                         sigmask(SIGINT) | sigmask(SIGQUIT))

#define EFAIL                      999

#define OCFS_MAGIC                 0xa156f7eb
#define OCFS_ROOT_INODE_NUMBER     2
#define OCFS_LINUX_MAX_FILE_SIZE   9223372036854775807LL
#define INITIAL_EXTENT_MAP_SIZE    10

#define OCFS_VOLCFG_LOCK_ITERATE	10	/* in jiffies */
#define OCFS_VOLCFG_LOCK_TIME		1000    /* in ms */
#define OCFS_VOLCFG_HDR_SECTORS		2	/* in sectors */
#define OCFS_VOLCFG_NEWCFG_SECTORS	4	/* in sectors */

#define OCFS_PUBLISH_CLEAR		0
#define OCFS_PUBLISH_SET		1

#define OCFS_NM_HEARTBEAT_TIME		500	/* in ms */
#define OCFS_HEARTBEAT_INIT             10      /* number of NM iterations to stabilize the publish map */
#define OCFS_HB_TIMEOUT			30000	/* in ms */

#ifndef O_DIRECT
#define O_DIRECT        040000
#endif

#define NOT_MOUNTED_EXCLUSIVE   (-1)

/* Four functions where dlm locks are taken */
#define OCFS_UPDATE_LOCK_STATE		1
#define OCFS_MAKE_LOCK_MASTER		2
#define OCFS_DISK_RELEASE_LOCK		3
#define OCFS_BREAK_CACHE_LOCK		4

#define IORUN_ALLOC_SIZE    (OCFS_MAX_DATA_EXTENTS * sizeof (ocfs_io_runs))
