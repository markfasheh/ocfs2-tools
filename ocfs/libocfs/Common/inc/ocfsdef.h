/*
 * ocfsdef.h
 *
 * Defines in-memory structures
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

#ifndef  _OCFSDEF_H_
#define  _OCFSDEF_H_

#define  OIN_NEEDS_VERIFICATION(a)	((a)->needs_verification)
#define  OIN_UPDATED(a)			((a)->needs_verification = false)

#define  IS_VALID_DIR_NODE(ptr)                                       \
                 (!strncmp((ptr)->signature, OCFS_DIR_NODE_SIGNATURE, \
                           strlen(OCFS_DIR_NODE_SIGNATURE)))

/* sm - ocfs 1.0 fails to set fe->sig for dirs */
#define  IS_VALID_FILE_ENTRY(ptr)     \
		(((ptr)->attribs & OCFS_ATTRIB_DIRECTORY) ||	\
		 (!strcmp((ptr)->signature, OCFS_FILE_ENTRY_SIGNATURE)))

#define  IS_VALID_EXTENT_HEADER(ptr)  \
                (!strcmp((ptr)->signature, OCFS_EXTENT_HEADER_SIGNATURE))

#define  IS_VALID_EXTENT_DATA(ptr)    \
                (!strcmp((ptr)->signature, OCFS_EXTENT_DATA_SIGNATURE))

#define  IS_VALID_NODE_NUM(node)      \
                (((node) >= 0) && ((node) < OCFS_MAXIMUM_NODES))

#define  IS_VALID_OIN(_oin)	((_oin)->obj_id.type == OCFS_TYPE_OIN)

#define  IS_VALID_OSB(_osb)	((_osb)->obj_id.type == OCFS_TYPE_OSB)

#define  IS_VALID_DISKHB(_hb)	((_hb) >= OCFS_MIN_DISKHB && (_hb) <= OCFS_MAX_DISKHB)
#define  IS_VALID_HBTIMEO(_to)	((_to) >= OCFS_MIN_HBTIMEO && (_to) <= OCFS_MAX_HBTIMEO)

#define  OCFS_GET_EXTENT(vbo, extent, k)                            \
              do {                                                  \
                for ((k) = 0; (k) < OCFS_MAX_DATA_EXTENTS; (k)++) { \
                  if((__s64)((extent)->extents[(k)].file_off +        \
                     (extent)->extents[(k)].num_bytes) > (vbo))     \
                    break;                                          \
                }                                                   \
              }  while(0)

#define  OCFS_GET_FILE_ENTRY_EXTENT(vbo, fileentry, k)                    \
              do {                                                        \
                for ((k) = 0; (k) < OCFS_MAX_FILE_ENTRY_EXTENTS; (k)++) { \
                  if((__s64)((fileentry)->extents[(k)].file_off +           \
                     (fileentry)->extents[(k)].length) > (vbo))           \
                    break;                                                \
                }                                                         \
              } while(0)

#define  CHECK_FOR_LAST_EXTENT(fileentry, k)                              \
              do {                                                        \
                for((k) = 0; (k) < OCFS_MAX_FILE_ENTRY_EXTENTS; (k)++) {  \
                  if((fileentry)->extents[(k)].disk_off == 0)             \
                    break;                                                \
                }                                                         \
                (k) = ((k) >= 1) ? ((k) - 1) : (k);                       \
              } while(0)

#ifdef LOCAL_ALLOC
#define OCFS_FILE_NUM_TO_SYSFILE_TYPE(num)   ( (num >= 0 && num < OCFS_VOL_BITMAP_FILE + OCFS_MAXIMUM_NODES) ? \
                                               num/OCFS_MAXIMUM_NODES : OCFS_INVALID_SYSFILE )
#define OCFS_SYSFILE_TYPE_TO_FILE_NUM(type,node)   ( (type > OCFS_INVALID_SYSFILE && type <= OCFS_VOL_BM_SYSFILE && \
                                                      node >=0 && node < OCFS_MAXIMUM_NODES) ? \
                                                     (type * OCFS_MAXIMUM_NODES) + node : -1 )
#else
#define OCFS_FILE_NUM_TO_SYSFILE_TYPE(num)   ( (num >= 0 && num < CLEANUP_FILE_BASE_ID + OCFS_MAXIMUM_NODES) ? \
                                               num/OCFS_MAXIMUM_NODES : OCFS_INVALID_SYSFILE )
#define OCFS_SYSFILE_TYPE_TO_FILE_NUM(type,node)   ( (type > OCFS_INVALID_SYSFILE && type <= CLEANUP_FILE_BASE_ID && \
                                                      node >=0 && node < OCFS_MAXIMUM_NODES) ? \
                                                     (type * OCFS_MAXIMUM_NODES) + node : -1 )
#endif

#define down_with_flag(_sem, _flg)	\
	do {				\
		if (!_flg) {		\
			down (_sem);	\
			_flg = true;	\
		}			\
	} while (0)

#define up_with_flag(_sem, _flg)	\
	do {				\
		if (_flg) {		\
			up (_sem);	\
			_flg = false;	\
		}			\
	} while (0)

#ifdef USERSPACE_TOOL
#define ocfs_task_interruptible(_o)     (true)
#else
#define ocfs_task_interruptible(_o)	((_o)->dlm_task != current && signal_pending(current))
#endif

#define ocfs_trans_in_progress(_o)			\
do {							\
	int _i = 0;					\
	while (((_o)->trans_in_progress) && (_i < 10)) {\
		ocfs_sleep (100);			\
		_i++;					\
	}						\
} while (0)

struct _ocfs_file;
struct _ocfs_inode;
struct _ocfs_super;

/*
** Macros
*/
#define  OCFS_SET_FLAG(flag, value)    ((flag) |= (value))
#define  OCFS_CLEAR_FLAG(flag, value)  ((flag) &= ~(value))

#define  OCFS_SECTOR_ALIGN(buf)                     \
               ((__u64)buf +                          \
                (((__u64)buf % OCFS_SECTOR_SIZE) ?    \
                 (OCFS_SECTOR_SIZE - ((__u64)buf % OCFS_SECTOR_SIZE)):0))

#define  OCFS_ALIGN(val, align)        \
               ((__u64)val  +            \
                (((__u64)val % align) ? (align - ((__u64)val % align)): 0))

/*
** Structures...
*/

#define  IS_NODE_ALIVE(pubmap, i, numnodes)  \
                                  (((pubmap) >> ((i) % (numnodes))) & 0x1)

#define  IS_VALIDBIT_SET(flags)   ((flags) & 0x1)

#define  SET_VALID_BIT(flags)     ((flags) |= 0x1)

/*
**  All structures have a type, and a size associated with it.
**  The type serves to identify the structure. The size is used for
**  consistency checking ...
*/
#define  UPDATE_PUBLISH_MAP(pubmap, num, flag, numnodes)          \
                do {                                              \
                  __u64 var = 0x1;                                  \
                  if((flag) == OCFS_PUBLISH_CLEAR)                \
                    (pubmap) &= (~(var << ((num) % (numnodes)))); \
                  else                                            \
                    (pubmap) |= (var << ((num) % (numnodes)));    \
                } while(0)

typedef struct _ocfs_obj_id
{
	__u32 type;		/* 4 byte signature to uniquely identify the struct */
	__u32 size;		/* sizeof the struct */
}
ocfs_obj_id;

typedef struct _ocfs_filldir
{
       __u8 fname[OCFS_MAX_FILENAME_LENGTH];
       loff_t pos;
       __u32 ino;
} ocfs_filldir;


/**************************************************************************
**  Each file open instance is represented by a context control block.
**  For each successful create/open request; a file object and a ocfs_file will
**  be created.
**  For open operations performed internally by the FSD, there may not
**  exist file objects; but a ocfs_file will definitely be created.
**  This structure must be quad-word aligned because it is zone allocated.
**************************************************************************/
typedef struct _ocfs_file
{
	ocfs_obj_id obj_id;
	struct _ocfs_inode *oin;	/* ptr to the assoc. ocfs_inode */
	struct list_head next_ofile;	/* all OFILEs for a ocfs_inode are linked */
	struct file *k_file;
	__u64 curr_byte_off;
	__s64 curr_dir_off;
	void *curr_dir_buf;
	ocfs_filldir filldir;
}
ocfs_file;

typedef struct _ocfs_inode ocfs_inode;
typedef struct _ocfs_super ocfs_super;
typedef struct _ocfs_superduper ocfs_superduper;
typedef struct _ocfs_io_runs ocfs_io_runs;

typedef struct _ocfs_lock_res
{
	__u32 signature;
	__u8 lock_type;		/* Support only Exclusive & Shared */
	atomic_t lr_share_cnt;	/* Used in case of Shared resources */
	atomic_t lr_ref_cnt;	/* When 0, freed */
	__u32 master_node_num;	/* Master Node */
	__u64 last_upd_seq_num;
	__u64 last_lock_upd;
	__u64 sector_num;
	__u64 oin_openmap;
	__u64 tmp_openmap;	/* oin_openmap collected over the comm */
	__u8 in_use;
	int thread_id;
	struct list_head cache_list;
	bool in_cache_list;
	__u32 lock_state;
	__u32 vote_state;		/* Is the lockres being voted on over ipcdlm */
	ocfs_inode *oin;
	spinlock_t lock_mutex;
	wait_queue_head_t voted_event;
	atomic_t voted_event_woken;
	__u64 req_vote_map;
	__u64 got_vote_map;
	__u32 vote_status;
	__u64 last_write_time;
	__u64 last_read_time;
	__u32 writer_node_num;
	__u32 reader_node_num;
}
ocfs_lock_res;

struct _ocfs_inode
{
	ocfs_obj_id obj_id;
	__s64 alloc_size;
	struct inode *inode;
	ocfs_sem main_res;
	ocfs_sem paging_io_res;
	ocfs_lock_res *lock_res;
	__u64 file_disk_off;	/* file location on the volume */
	__u64 dir_disk_off;	/* for dirs, offset to dirnode structure */
	__u64 chng_seq_num;
	__u64 parent_dirnode_off;	/* from the start of vol */
	ocfs_extent_map map;
	struct _ocfs_super *osb;	/* ocfs_inode belongs to this volume */
	__u32 oin_flags;
	struct list_head next_ofile;	/* list of all ofile(s) */
	__u32 open_hndl_cnt;
	bool needs_verification;
	bool cache_enabled;
};

typedef enum _ocfs_vol_state
{
	VOLUME_DISABLED,
	VOLUME_INIT,
	VOLUME_ENABLED,
	VOLUME_LOCKED,
	VOLUME_IN_RECOVERY,
	VOLUME_MOUNTED,
	VOLUME_BEING_DISMOUNTED,
	VOLUME_DISMOUNTED
}
ocfs_vol_state;

typedef struct _ocfs_node_config_info
{
	char node_name[MAX_NODE_NAME_LENGTH];
	ocfs_guid guid;
	ocfs_ipc_config_info ipc_config;
}
ocfs_node_config_info;

typedef struct _ocfs_dlm_stats
{
	atomic_t total;
	atomic_t okay;
	atomic_t etimedout;
	atomic_t efail;
	atomic_t eagain;
	atomic_t enoent;
	atomic_t def;
}
ocfs_dlm_stats;

typedef struct _ocfs_lock_type_stats
{
	atomic_t update_lock_state;
	atomic_t make_lock_master;
	atomic_t disk_release_lock;
	atomic_t break_cache_lock;
	atomic_t others;
}
ocfs_lock_type_stats;

/*
 * ocfs_super
 *
 * A mounted volume is represented using the following structure.
 */
struct _ocfs_super
{
	ocfs_obj_id obj_id;
	ocfs_sem osb_res;	/* resource to protect the ocfs_super */
	struct list_head osb_next;	/* list of ocfs_super(s) */
	__u32 osb_id;		/* id used by the proc interface */
	struct completion complete;
	struct task_struct *dlm_task;
	__u32 osb_flags;
	bool blk_zero_write;	/* true when blk 0 is written on first mount */
	__s64 file_open_cnt;	/* num of open files/dirs. vol cannot be dismounted if > 0 */
	__u64 publ_map;		/* each bit represents state of node */
	HASHTABLE root_sect_node;	/* lockres->sector_num hash */
	struct list_head cache_lock_list;
	struct super_block *sb;
	ocfs_inode *oin_root_dir;	/* ptr to the root dir ocfs_inode */
	ocfs_vol_layout vol_layout;
	ocfs_vol_node_map vol_node_map;
	struct semaphore cfg_lock;
	ocfs_node_config_info *node_cfg_info[OCFS_MAXIMUM_NODES];
	__u64 cfg_seq_num;
	bool cfg_initialized;
	__u32 num_cfg_nodes;
	__u32 node_num;
	bool reclaim_id;                /* reclaim the original node number*/
	__u32 max_miss_cnt;
	__u8 hbm;
	unsigned long hbt;
	__u64 log_disk_off;
	__u64 log_meta_disk_off;
	__u64 log_file_size;
	__u32 sect_size;
	bool needs_flush;
	bool commit_cache_exec;
	ocfs_sem map_lock;
	ocfs_extent_map metadata_map;
	ocfs_extent_map trans_map;
	ocfs_alloc_bm cluster_bitmap;
	__u32 max_dir_node_ent;
	ocfs_vol_state vol_state;
	__s64 curr_trans_id;
	bool trans_in_progress;
	ocfs_sem log_lock;
	ocfs_sem recovery_lock;
	__u32 node_recovering;
#ifdef PARANOID_LOCKS
	ocfs_sem dir_alloc_lock;
	ocfs_sem file_alloc_lock;
#endif
	ocfs_sem vol_alloc_lock;
	struct timer_list lock_timer;
	atomic_t lock_stop;
	wait_queue_head_t lock_event;
	atomic_t lock_event_woken;
	struct semaphore comm_lock;	/* protects ocfs_comm_process_vote_reply */
	atomic_t nm_init;
	wait_queue_head_t nm_init_event;
	bool cache_fs;
	__u32 prealloc_lock;
	ocfs_io_runs *data_prealloc;
	ocfs_io_runs *md_prealloc;
	__u8 *cfg_prealloc;
	__u32 cfg_len;
	__u8 *log_prealloc;
	struct semaphore publish_lock;  /* protects r/w to publish sector */
	atomic_t node_req_vote;		/* set when node's vote req pending */
	struct semaphore trans_lock;	/* serializes transactions */
	ocfs_dlm_stats net_reqst_stats;	/* stats of netdlm vote requests */
	ocfs_dlm_stats net_reply_stats;	/* stats of netdlm vote reponses */
	ocfs_dlm_stats dsk_reqst_stats;	/* stats of diskdlm vote requests */
	ocfs_dlm_stats dsk_reply_stats;	/* stats of diskdlm vote reponses */
	ocfs_lock_type_stats lock_type_stats;	/* stats of lock types taken */
	__u64 last_disk_seq;		/* last vote seq voted on on disk */
	char dev_str[20];		/* "major,minor" of the device */
};

enum {
    OSB_DATA_LOCK,
    OSB_MD_LOCK,
    OSB_CFG_LOCK,
    OSB_LOG_LOCK
};

#define OSB_PREALLOC_LOCK_TEST(osb, l)   (osb->prealloc_lock & (1<<l))
#define OSB_PREALLOC_LOCK_SET(osb, l)    (osb->prealloc_lock |= (1<<l))
#define OSB_PREALLOC_LOCK_CLEAR(osb, l)  (osb->prealloc_lock &= ~(1<<l))


typedef struct _ocfs_comm_info
{
	__u32 type;
	char *ip_addr;
	__u32 ip_port;
	char *ip_mask;
}
ocfs_comm_info;

typedef struct _ocfs_global_ctxt
{
	ocfs_obj_id obj_id;
	ocfs_sem res;
	struct list_head osb_next;	/* List of all volumes */
	kmem_cache_t *oin_cache;
	kmem_cache_t *ofile_cache;
	kmem_cache_t *fe_cache;
	kmem_cache_t *lockres_cache;
	kmem_cache_t *dirnode_cache;
	__u32 flags;
	__u32 pref_node_num;		/* preferred... osb has the real one */
	ocfs_guid guid;			/* uniquely identifies a node */
	char *node_name;		/* human readable node identification */
	char *cluster_name;		/* unused */
	ocfs_comm_info comm_info;	/* ip address, etc for listener */
	bool comm_info_read;		/* ipc info loaded from config file */
	wait_queue_head_t flush_event;	/* unused */
	__u8 hbm;
	spinlock_t comm_seq_lock;	/* protects comm_seq_num */
	__u64 comm_seq_num;		/* local node seq num used in ipcdlm */
#ifdef OCFS_LINUX_MEM_DEBUG
        struct list_head item_list;
#endif
	atomic_t cnt_lockres;		/* count of allocated lockres */
	ocfs_dlm_stats net_reqst_stats;	/* stats of netdlm vote requests */
	ocfs_dlm_stats net_reply_stats;	/* stats of netdlm vote reponses */
	ocfs_dlm_stats dsk_reqst_stats;	/* stats of diskdlm vote requests */
	ocfs_dlm_stats dsk_reply_stats;	/* stats of diskdlm vote reponses */
}
ocfs_global_ctxt;

struct _ocfs_io_runs
{
	__u64 disk_off;
	__u32 offset;
	__u32 byte_cnt;
};

#if defined(OCFS_LINUX_MEM_DEBUG)
# define ocfs_malloc(_s)					\
({								\
	void *m = ocfs_linux_dbg_alloc(_s, __FILE__, __LINE__);	\
	if (debug_level & OCFS_DEBUG_LEVEL_MALLOC)		\
		printk("malloc(%s,%d) = %p\n", __FILE__,	\
		       __LINE__, m);				\
	m;							\
})

# define ocfs_free(x)\
do {								\
	if (debug_level & OCFS_DEBUG_LEVEL_MALLOC)		\
		printk("free(%s,%d) = %p\n", __FILE__,	\
		       __LINE__, x);				\
	ocfs_linux_dbg_free(x);					\
} while (0)

#elif !defined(OCFS_LINUX_MEM_DEBUG)
# define ocfs_malloc(Size)     kmalloc((size_t)(Size), GFP_KERNEL)
# define ocfs_free             kfree
#endif				/* ! defined(OCFS_MEM_DBG) */


typedef struct _ocfs_ipc_ctxt
{
	ocfs_sem ipc_ctxt_res;
	__u32 dlm_msg_size;
	__u16 version;
	bool init;
	struct socket *send_sock;
	struct socket *recv_sock;
	struct completion complete;
	struct task_struct *task;
}
ocfs_ipc_ctxt;

typedef enum _ocfs_protocol
{
	OCFS_TCP = 1,
	OCFS_UDP
}
ocfs_protocol;

extern ocfs_ipc_ctxt OcfsIpcCtxt;

typedef struct _ocfs_ipc_dlm_config
{
	__u16 version;
	__u32 msg_size;
	__u32 num_recv_threads;
}
ocfs_ipc_dlm_config;

/*
** Globals ...
*/
extern ocfs_global_ctxt OcfsGlobalCtxt;

#endif				/* _OCFSDEF_H_ */
