/*
 * ocfsgenmisc.c
 *
 * Miscellaneous.
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

#ifdef __KERNEL__
#include <ocfs.h>
#else
#include <libocfs.h>
#endif

__u32 disk_timeo = 0;

/* Tracing */
#define OCFS_DEBUG_CONTEXT    OCFS_DEBUG_CONTEXT_MISC

/*  Global Sequence number for error log. */

__u32 OcfsErrorLogSequence = 0;

extern spinlock_t ocfs_inode_lock;

/*
 * ocfs_create_meta_log_files()
 *
 */
int ocfs_create_meta_log_files (ocfs_super * osb)
{
	int status = 0;
	__u64 fileSize = 0;
	__u64 allocSize = 0;
	__u64 log_disk_off = 0;
	__u32 logFileId;

	LOG_ENTRY ();

	logFileId = (OCFS_FILE_VOL_LOG_FILE + osb->node_num);

	status = ocfs_get_system_file_size (osb, logFileId, &fileSize, &allocSize);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto bail;
	}

	if (allocSize != 0) {
		log_disk_off = ocfs_file_to_disk_off (osb, (OCFS_FILE_VOL_LOG_FILE +
						       osb->node_num), 0);
		if (log_disk_off == 0) {
			LOG_ERROR_STATUS(status = -EFAIL);
			goto bail;
		}

		osb->log_disk_off = log_disk_off;

		log_disk_off = ocfs_file_to_disk_off (osb, (OCFS_FILE_VOL_META_DATA +
						       osb->node_num), 0);
		if (log_disk_off == 0) {
			LOG_ERROR_STATUS(status = -EFAIL);
			goto bail;
		}
		osb->log_meta_disk_off = log_disk_off;
		goto bail;
	}

	status = ocfs_extend_system_file (osb, (OCFS_FILE_VOL_LOG_FILE +
					 osb->node_num), (ONE_MEGA_BYTE * 10), NULL);
	if (status < 0) {
		/* if that fails, fall back to smaller, for fragmented fs */
		status = ocfs_extend_system_file (osb, (OCFS_FILE_VOL_LOG_FILE +
					osb->node_num), 128 * 1024, NULL);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto bail;
		}
	}

	ocfs_extend_system_file (osb, (OCFS_FILE_VOL_LOG_FILE + osb->node_num), 0, NULL);

	log_disk_off = ocfs_file_to_disk_off (osb, (OCFS_FILE_VOL_LOG_FILE +
					       osb->node_num), 0);
	if (log_disk_off == 0) {
		LOG_ERROR_STATUS(status = -EFAIL);
		goto bail;
	}

	osb->log_disk_off = log_disk_off;

	status = ocfs_extend_system_file (osb, (OCFS_FILE_VOL_META_DATA +
					 osb->node_num), ONE_MEGA_BYTE, NULL);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto bail;
	}

	ocfs_extend_system_file (osb, (OCFS_FILE_VOL_META_DATA + osb->node_num), 0, NULL);

	log_disk_off = ocfs_file_to_disk_off (osb, (OCFS_FILE_VOL_META_DATA +
					       osb->node_num), 0);
	if (log_disk_off == 0) {
		LOG_ERROR_STATUS(status = -EFAIL);
		goto bail;
	}

	osb->log_meta_disk_off = log_disk_off;

      bail:
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_create_meta_log_files */

/*
 * ocfs_create_new_oin()
 *
 * Create a new oin.
 */
int ocfs_create_new_oin (ocfs_inode ** Returnedoin,
		  __u64 * alloc_size,
		  __u64 * EndOfFile, struct file *FileObject, ocfs_super * osb)
{
	int status = 0;
	ocfs_inode *oin = NULL;

	LOG_ENTRY ();

	/* Don't do OCFS_ASSERT for FileObject, as it is OK if FileObject is NULL */

	OCFS_ASSERT (osb);

	oin = ocfs_allocate_oin ();
	*Returnedoin = oin;

	if (oin == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto finally;
	}

	ocfs_init_sem (&(oin->main_res));
	ocfs_init_sem (&(oin->paging_io_res));
	OCFS_SET_FLAG (oin->oin_flags, OCFS_INITIALIZED_MAIN_RESOURCE);

	/*  Initialize the alloc size value here, file size will come later in i_size */
	oin->alloc_size = *(alloc_size);

	/* Insert the pointer to osb in the oin and also Initialize the OFile list  */
	oin->osb = osb;
	INIT_LIST_HEAD (&(oin->next_ofile));

      finally:

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_create_new_oin */

/* ocfs_create_root_dir_node()
 *
 */
int ocfs_create_root_dir_node (ocfs_super * osb)
{
	int status = 0, tempstat;
	ocfs_dir_node *NewDirNode = NULL;
	__u64 bitmapOffset, numClustersAlloc, fileOffset = 0;
	__u32 size, i;
	ocfs_vol_disk_hdr *volDiskHdr = NULL;
	ocfs_lock_res *LockResource = NULL;
	bool lock_acq = false;
	char *buf = NULL;
	ocfs_file_entry *fe = NULL;
	ocfs_file_entry *sys_fe = NULL;

	LOG_ENTRY ();

	fe = ocfs_allocate_file_entry ();
	if (fe == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto bail;
	}

	/* Acquire volume Lock ...  */
	status = ocfs_acquire_lock (osb, OCFS_VOLUME_LOCK_OFFSET,
				    OCFS_DLM_EXCLUSIVE_LOCK, FLAG_FILE_CREATE,
				    &LockResource, fe);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto bail;
	} else
		lock_acq = true;

	NewDirNode = ocfs_allocate_dirnode();
	if (NewDirNode == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto bail;
	}

	size = (ONE_MEGA_BYTE > osb->vol_layout.cluster_size) ?
	    ONE_MEGA_BYTE : osb->vol_layout.cluster_size;

	status = ocfs_find_contiguous_space_from_bitmap (osb, size,
					&bitmapOffset, &numClustersAlloc, false);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto bail;
	}

	osb->vol_layout.root_int_off = (bitmapOffset *
					osb->vol_layout.cluster_size) +
					osb->vol_layout.data_start_off;

	sys_fe = ocfs_allocate_file_entry ();
	if (sys_fe == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto bail;
	}

	if ((buf = ocfs_malloc (OCFS_MAX_FILENAME_LENGTH)) == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto bail;
	}

	/*  Create Files in root for directory, file node allocations */
	/*  Create the dir alloc file now that we have a internal */
	for (i = 0; i < OCFS_MAXIMUM_NODES; i++) {
		ocfs_init_system_file (osb, OCFS_FILE_VOL_META_DATA + i, buf, sys_fe);
		ocfs_init_system_file (osb, OCFS_FILE_VOL_LOG_FILE + i, buf, sys_fe);
		ocfs_init_system_file (osb, OCFS_FILE_DIR_ALLOC + i, buf, sys_fe);
		ocfs_init_system_file (osb, OCFS_FILE_DIR_ALLOC_BITMAP + i, buf, sys_fe);
		ocfs_init_system_file (osb, OCFS_FILE_FILE_ALLOC + i, buf, sys_fe);
		ocfs_init_system_file (osb, OCFS_FILE_FILE_ALLOC_BITMAP + i, buf, sys_fe);
		ocfs_init_system_file (osb, LOG_FILE_BASE_ID + i, buf, sys_fe);
		ocfs_init_system_file (osb, CLEANUP_FILE_BASE_ID + i, buf, sys_fe);
#ifdef LOCAL_ALLOC
		ocfs_init_system_file (osb, OCFS_VOL_BITMAP_FILE + (2*i), buf, sys_fe);
#endif 
	}

	status = ocfs_alloc_node_block (osb, osb->vol_layout.dir_node_size,
				 &bitmapOffset, &fileOffset,
				 &numClustersAlloc, osb->node_num,
				 DISK_ALLOC_DIR_NODE);
	if (status < 0) {
		LOG_ERROR_STATUS (status = -EFAIL);
		goto bail;
	}

	osb->vol_layout.root_start_off = bitmapOffset;

	ocfs_initialize_dir_node (osb, NewDirNode, bitmapOffset, fileOffset,
				  osb->node_num);
	NewDirNode->dir_node_flags |= DIR_NODE_FLAG_ROOT;

	status = ocfs_write_dir_node (osb, NewDirNode, -1);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto bail;
	}

	/*  Read the first sector bytes from the target device */
	size = osb->sect_size;
	status = ocfs_read_disk_ex (osb, (void **) &volDiskHdr, size, size, 0);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto bail;
	}

	volDiskHdr->root_off = osb->vol_layout.root_start_off;
	volDiskHdr->internal_off = osb->vol_layout.root_int_off;

	osb->blk_zero_write = true;
	status = ocfs_write_disk (osb, (__s8 *) volDiskHdr, size, 0);
	osb->blk_zero_write = false;
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto bail;
	}

      bail:
	if (status < 0) {
		LOG_ERROR_STR ("Disabling Volume");
		osb->vol_state = VOLUME_DISABLED;
	}

	/*  Release Volume Lock */
	if (lock_acq) {
		tempstat = ocfs_release_lock (osb, OCFS_VOLUME_LOCK_OFFSET,
				OCFS_DLM_EXCLUSIVE_LOCK, 0, LockResource, fe);
		if (tempstat < 0) {
			LOG_ERROR_STATUS (tempstat);
			osb->vol_state = VOLUME_DISABLED;
		}
	}
	ocfs_release_dirnode (NewDirNode);
	ocfs_safefree (volDiskHdr);
	ocfs_safefree (buf);
	ocfs_release_file_entry (sys_fe);
	ocfs_release_file_entry (fe);
	ocfs_put_lockres (LockResource);
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_create_root_dir_node */

/* ocfs_create_root_oin()
 *
 */
int ocfs_create_root_oin (ocfs_super * osb)
{
	int status = 0;
	int tmpstat;
	__u64 allocSize = 0;
	__u64 endofFile = 0;
	ocfs_inode *oin = NULL;
	ocfs_vol_disk_hdr *volDiskHdr = NULL;
	ocfs_lock_res *LockResource = NULL;
	bool vol_locked = false;
	ocfs_file_entry *fe = NULL;

	LOG_ENTRY ();

	fe = ocfs_allocate_file_entry ();
	if (fe == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto finally;
	}

	if (osb->vol_layout.root_start_off == 0) {
		status = ocfs_wait_for_disk_lock_release (osb, OCFS_VOLUME_LOCK_OFFSET,
						 10000, OCFS_DLM_NO_LOCK);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}

		status = ocfs_acquire_lock (osb, OCFS_VOLUME_LOCK_OFFSET,
				     OCFS_DLM_EXCLUSIVE_LOCK, FLAG_FILE_CREATE,
				     &LockResource, fe);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		} else
			vol_locked = true;

		status = ocfs_read_disk_ex (osb, (void **) &volDiskHdr,
					OCFS_SECTOR_SIZE, OCFS_SECTOR_SIZE, 0);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}

		if (vol_locked) {
			status = ocfs_release_lock (osb, OCFS_VOLUME_LOCK_OFFSET,
						    OCFS_DLM_EXCLUSIVE_LOCK, 0,
						    LockResource, fe);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				osb->vol_state = VOLUME_DISABLED;
			}
			vol_locked = false;
		}

		if (volDiskHdr->root_off != 0) {
//			ocfs_sleep (3000);
			ocfs_wait_for_disk_lock_release (osb,
							OCFS_VOLUME_LOCK_OFFSET,
							30000, OCFS_DLM_NO_LOCK);
			osb->vol_layout.root_start_off = volDiskHdr->root_off;
			osb->vol_layout.root_int_off = volDiskHdr->internal_off;
		}

		status = ocfs_create_root_dir_node (osb);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}
		/*  if it fails, Release the memory for the OFile we allocated above */
	} else {
		status = ocfs_create_meta_log_files (osb);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}
	}

	/* Create the root directory oin. This is done either here or in */
	/* FindNewoin's if it fails, Release the memory for the OFile we */
	/* allocated above */
	status = ocfs_create_new_oin (&oin, &allocSize, &endofFile, NULL, osb);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	/*  This is for root . */
	status = ocfs_initialize_oin (oin, osb,
			   OCFS_OIN_DIRECTORY | OCFS_OIN_ROOT_DIRECTORY, NULL,
			   0, osb->vol_layout.root_start_off);
	if (status < 0) {
		if (status != -EINTR)
			LOG_ERROR_STATUS (status);
		goto finally;
	}

	// oin->Parentoin = NULL; /*  Root has no parent */

	/*  Set the Rootdirectories root Dir Node */

	osb->oin_root_dir = oin;

	oin->dir_disk_off = osb->vol_layout.root_start_off;

      finally:
	if (status < 0 && oin)
		ocfs_release_oin (oin, true);

	if (vol_locked) {
		tmpstat = ocfs_release_lock (osb, OCFS_VOLUME_LOCK_OFFSET,
					     OCFS_DLM_EXCLUSIVE_LOCK, 0,
					     LockResource, fe);
		if (tmpstat < 0) {
			LOG_ERROR_STATUS (tmpstat);
			osb->vol_state = VOLUME_DISABLED;
		}
	}

	ocfs_safefree (volDiskHdr);
	ocfs_release_file_entry (fe);
	ocfs_put_lockres (LockResource);
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_create_root_oin */



/*
 * ocfs_delete_all_extent_maps()
 *
 */
void ocfs_delete_all_extent_maps (ocfs_inode * oin)
{
	__u32 RunsInExtentMap = 0, ExtentMapIndex, ByteCount = 0;
	__s64 Vbo;
	__s64 Lbo;

	LOG_ENTRY ();

	RunsInExtentMap = ocfs_extent_map_get_count (&oin->map);

	for (ExtentMapIndex = 0; ExtentMapIndex < RunsInExtentMap;
	     ExtentMapIndex++) {
		if (ocfs_get_next_extent_map_entry
		    (oin->osb, &oin->map, ExtentMapIndex, &Vbo, &Lbo,
		     &ByteCount)) {
			ocfs_remove_extent_map_entry (oin->osb, &oin->map, Vbo,
						  ByteCount);
		}
	}

	LOG_EXIT ();
	return;
}				/* ocfs_delete_all_extent_maps */


#ifndef USERSPACE_TOOL
/*
 * ocfs_release_oin()
 *
 */
void ocfs_release_oin (ocfs_inode * oin, bool need_lock)
{
	ocfs_lock_res *lockres = NULL;
	struct inode *inode;
	__u64 savedOffset = 0;

	LOG_ENTRY_ARGS ("oin=%p, lock=%s\n", oin, need_lock? "yes" : "no");

	if (!oin || !oin->osb)
		goto bail;

	OCFS_ASSERT(IS_VALID_OIN(oin));

	lockres = oin->lock_res;

	if (lockres != NULL) {
		ocfs_get_lockres (lockres);
		ocfs_acquire_lockres (lockres);
		if (lockres->oin == oin)
			lockres->oin = NULL;
		ocfs_release_lockres (lockres);
	}

	inode = (struct inode *) oin->inode;

	if (inode) {
		savedOffset = oin->file_disk_off;
		SET_INODE_OIN (inode, NULL);
		SET_INODE_OFFSET (inode, savedOffset);
		LOG_TRACE_ARGS ("inode oin cleared / flags: %d / offset: %u.%u\n",
			inode->i_flags, savedOffset);
	}

	if (inode) {
		if (need_lock)
			spin_lock (&ocfs_inode_lock);
		oin->inode = NULL;
		if (atomic_read(&inode->i_count) > 1)
			atomic_dec(&inode->i_count);
		if (need_lock)
			spin_unlock (&ocfs_inode_lock);
	}

	ocfs_extent_map_destroy (&oin->map);
	ocfs_extent_map_init (&oin->map);

	/*  Delete the ocfs_sem objects */
	if (oin->oin_flags & OCFS_INITIALIZED_MAIN_RESOURCE) {
		ocfs_del_sem (&(oin->main_res));
		OCFS_CLEAR_FLAG (oin->oin_flags, OCFS_INITIALIZED_MAIN_RESOURCE);
	}
	if (oin->oin_flags & OCFS_INITIALIZED_PAGING_IO_RESOURCE) {
		ocfs_del_sem (&(oin->paging_io_res));
		OCFS_CLEAR_FLAG (oin->oin_flags,
			       OCFS_INITIALIZED_PAGING_IO_RESOURCE);
	}

	memset (oin, 0, sizeof(ocfs_inode));
#ifdef OCFS_MEM_DBG
	ocfs_dbg_slab_free (OcfsGlobalCtxt.oin_cache, oin);
#else
	kmem_cache_free (OcfsGlobalCtxt.oin_cache, oin);
#endif
	oin = NULL;
	ocfs_put_lockres (lockres);

	ocfs_put_lockres (lockres);
bail:
	LOG_EXIT ();
	return;
}				/* ocfs_release_oin */
#endif /* !USERSPACE_TOOL */

/*
 * ocfs_initialize_osb()
 *
 */
int ocfs_initialize_osb (ocfs_super * osb, ocfs_vol_disk_hdr * vdh,
			 ocfs_vol_label * vol_label, __u32 sect_size)
{
	int status = 0;
	ocfs_publish *publish = NULL;
	__u32 bitmap_len, length;
	void *bitmap_buf, *buffer = NULL;
	__u64 offset;
	ocfs_vol_layout *vol_layout;

	LOG_ENTRY ();

	if (osb == NULL) {
		LOG_ERROR_STATUS(status = -EFAIL);
		goto finally;
	}

	OCFS_CLEAR_FLAG (osb->osb_flags, OCFS_OSB_FLAGS_SHUTDOWN);

	vol_layout = &(osb->vol_layout);

	vol_layout->cluster_size = (__u32) (vdh->cluster_size);
	osb->obj_id.type = OCFS_TYPE_OSB;
	osb->obj_id.size = sizeof (ocfs_super);

	snprintf(osb->dev_str, sizeof(osb->dev_str), "%u,%u",
		 MAJOR(osb->sb->s_dev), MINOR(osb->sb->s_dev));

	ocfs_init_sem (&(osb->osb_res));
	ocfs_init_sem (&(osb->map_lock));
	ocfs_init_sem (&(osb->log_lock));
	ocfs_init_sem (&(osb->recovery_lock));
#ifdef PARANOID_LOCKS
	ocfs_init_sem (&(osb->dir_alloc_lock));
	ocfs_init_sem (&(osb->file_alloc_lock));
#endif
	ocfs_init_sem (&(osb->vol_alloc_lock));

	init_MUTEX (&(osb->cfg_lock));
	init_MUTEX (&(osb->comm_lock));
	init_MUTEX (&(osb->trans_lock));

#ifdef __LP64__
#define HASHBITS	11
#else
#define HASHBITS	12
#endif
	if (!ocfs_hash_create (&(osb->root_sect_node), HASHBITS)) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto bail;
	}

	osb->node_recovering = OCFS_INVALID_NODE_NUM;
	osb->needs_flush = false;
	osb->commit_cache_exec = false;
	osb->log_disk_off = 0;
	osb->log_meta_disk_off = 0;
	osb->trans_in_progress = false;
	osb->last_disk_seq = ULONGLONG_MAX;

	init_MUTEX (&(osb->publish_lock));
	atomic_set (&osb->node_req_vote, 0);

	init_waitqueue_head (&osb->nm_init_event);
	atomic_set (&osb->nm_init, 0);

	ocfs_extent_map_init (&osb->metadata_map);
	ocfs_extent_map_init (&osb->trans_map);

	INIT_LIST_HEAD (&(osb->cache_lock_list));
	osb->sect_size = sect_size;
	osb->oin_root_dir = NULL;
	osb->node_num = OCFS_INVALID_NODE_NUM;

	memcpy (vol_layout->mount_point, vdh->mount_point, strlen (vdh->mount_point));
	vol_layout->serial_num = vdh->serial_num;
	vol_layout->size = (__u64) (vdh->device_size);
	vol_layout->start_off = vdh->start_off;
	vol_layout->bitmap_off = (__u64) vdh->bitmap_off;
	vol_layout->publ_sect_off = vdh->publ_off;
	vol_layout->vote_sect_off = vdh->vote_off;
	vol_layout->root_bitmap_off = vdh->root_bitmap_off;
	vol_layout->root_start_off = vdh->root_off;
	vol_layout->root_int_off = vdh->internal_off;
	vol_layout->root_size = vdh->root_size;
	vol_layout->cluster_size = (__u32) vdh->cluster_size;
	vol_layout->num_nodes = (__u32) vdh->num_nodes;
	vol_layout->data_start_off = vdh->data_start_off;
	vol_layout->root_bitmap_size = vdh->root_bitmap_size;
	vol_layout->num_clusters = vdh->num_clusters;
	vol_layout->dir_node_size = vdh->dir_node_size;
	vol_layout->file_node_size = vdh->file_node_size;
	vol_layout->node_cfg_off = vdh->node_cfg_off;
	vol_layout->node_cfg_size = vdh->node_cfg_size;
	vol_layout->new_cfg_off = vdh->new_cfg_off;
	vol_layout->prot_bits = vdh->prot_bits;
	vol_layout->uid = vdh->uid;
	vol_layout->gid = vdh->gid;

	if (disk_timeo) {
		vol_layout->disk_hb = vdh->disk_hb;
		vol_layout->hb_timeo = vdh->hb_timeo;
	}

	if (!IS_VALID_DISKHB(vol_layout->disk_hb))
		vol_layout->disk_hb = OCFS_NM_HEARTBEAT_TIME;

	if (!IS_VALID_HBTIMEO(vol_layout->hb_timeo))
		vol_layout->hb_timeo = OCFS_HB_TIMEOUT;

	if (disk_timeo)
		osb->max_miss_cnt = (vdh->hb_timeo / vdh->disk_hb) + 1;
	else
		osb->max_miss_cnt = MISS_COUNT_VALUE;

	memcpy (vol_layout->vol_id, vol_label->vol_id, MAX_VOL_ID_LENGTH);

	if (vol_layout->dir_node_size == 0) 
		vol_layout->dir_node_size = OCFS_DEFAULT_DIR_NODE_SIZE;

	if (vol_layout->file_node_size == 0) 
		vol_layout->file_node_size = OCFS_DEFAULT_FILE_NODE_SIZE;

	osb->max_dir_node_ent =
		    (__u32) (vol_layout->dir_node_size / sect_size) - 2;
	bitmap_len = (__u32) vol_layout->num_clusters;

	/* In the start one sector is for Volume header and second sector */
	/* is for Global sequence Number and Directoy Entry. */
	{
		__u32 sz = OCFS_ALIGN ((bitmap_len + 7) / 8, OCFS_PAGE_SIZE);

		if ((bitmap_buf = vmalloc (sz)) == NULL) {
			LOG_ERROR_STR ("vmalloc failed");
			status = -ENOMEM;
			goto finally;
		}
	}

	ocfs_initialize_bitmap (&osb->cluster_bitmap, (__u32 *) bitmap_buf,
				bitmap_len);

	osb->prealloc_lock = 0;
	osb->data_prealloc = ocfs_malloc (IORUN_ALLOC_SIZE);
	if (!osb->data_prealloc) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto bail;
	}

	osb->md_prealloc = ocfs_malloc (IORUN_ALLOC_SIZE);
	if (!osb->md_prealloc) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto bail;
	}

	osb->cfg_len = (OCFS_MAXIMUM_NODES +
			OCFS_VOLCFG_NEWCFG_SECTORS) * sect_size;
	osb->cfg_prealloc = ocfs_malloc (osb->cfg_len);
	if (!osb->cfg_prealloc) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto bail;
	}

	osb->log_prealloc = ocfs_malloc (OCFS_ALIGN(sizeof(ocfs_cleanup_record),
						    OCFS_PAGE_SIZE));
	if (!osb->log_prealloc) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto bail;
	}

	status = ocfs_get_config (osb);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	/* Read the Publish Sector of local Node */
	offset = vol_layout->publ_sect_off + (osb->node_num * osb->sect_size);
	status = ocfs_read_force_disk_ex (osb, (void **)&publish, osb->sect_size,
					  osb->sect_size, offset);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	/*  Zero out the time stamp to write a new value */
	publish->time = 0;
	OcfsQuerySystemTime (&publish->time);

	status = ocfs_write_disk (osb, publish, osb->sect_size, offset);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	/*  Read disk for all Publish Sectors  */
	length = OCFS_MAXIMUM_NODES * osb->sect_size;
	status = ocfs_read_force_disk_ex (osb, (void **)&buffer, length, length,
					  vol_layout->publ_sect_off);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	ocfs_update_publish_map (osb, (__u8 *) buffer, true);

	/* We might need to add a variable in Global List of osb to */
	/* delay any creation, if any other node is already creating a file */

	/*  Link this osb onto the global linked list of all osb structures. */
	/*  The Global Link List is mainted for the whole driver . */
	ocfs_down_sem (&(OcfsGlobalCtxt.res), true);
	list_add_tail (&(osb->osb_next), &(OcfsGlobalCtxt.osb_next));
	ocfs_up_sem (&(OcfsGlobalCtxt.res));

	/*  Mark the fact that this osb structure is initialized. */
	OCFS_SET_FLAG (osb->osb_flags, OCFS_OSB_FLAGS_OSB_INITIALIZED);

	/* skip the frees which happen on error only */
	goto finally;

      bail:
	if (osb->root_sect_node.buckets)
		ocfs_hash_destroy (&(osb->root_sect_node), NULL);
	ocfs_safefree (osb->data_prealloc);
	ocfs_safefree (osb->md_prealloc);
	ocfs_safefree (osb->log_prealloc);
	ocfs_safefree (osb->cfg_prealloc);

      finally:
	ocfs_safefree (publish);
	ocfs_safefree (buffer);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_initialize_osb */

/*
 * ocfs_verify_volume()
 *
 */
int ocfs_verify_volume (ocfs_vol_disk_hdr * vdh)
{
	int status = 0;

	LOG_ENTRY ();

	if (vdh == NULL) {
		LOG_ERROR_STATUS (status = -EFAIL);
		goto bail;
	}

	/*  Compare the Signature with the one we read from disk  */
	if (memcmp (vdh->signature, OCFS_VOLUME_SIGNATURE,
		    strlen (OCFS_VOLUME_SIGNATURE)) != 0) {
		LOG_ERROR_STR ("Invalid volume signature");
		status = -EINVAL;
		goto bail;
	}

	/*  Check the Volume Length and the ClusterSize.  */
	if (vdh->device_size == 0) {
		LOG_ERROR_STR ("Device size cannot be zero");
		status = -EINVAL;
		goto bail;
	}

	if (vdh->cluster_size == 0) {
		LOG_ERROR_STR ("Cluster size cannot be zero");
		status = -EINVAL;
		goto bail;
	}

	if (vdh->major_version != OCFS_MAJOR_VERSION) {
		LOG_ERROR_ARGS ("Version number not compatible: %u.%u",
				vdh->major_version, vdh->minor_version);
		status = -EINVAL;
		goto bail;
	}

	/*  Verify if mount point and volume size are valid */
	/*  Read the root directory and make sure it is valid */
	/*  Check to see who else is alive. */
	/*  Kick in the NM i/f to start writing time stamps to the disk */

      bail:
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_verify_volume */

/*
 * ocfs_vol_member_reconfig()
 *
 */
int ocfs_vol_member_reconfig (ocfs_super * osb)
{
	int status = 0;

	LOG_ENTRY ();

	/* Start out with the highest multiple.... */
	osb->hbm = DISK_HBEAT_COMM_ON;

	/* Trigger the NM on this node to init the VolMap based on the info */
	/* on the disk currently and advertise to other nodes our existance. */
	ocfs_nm_heart_beat (osb, HEARTBEAT_METHOD_DISK, true);

	/* Send a mesg to force the nm on all other nodes to process this */
	/* volume, this should allow for them to detect our existance. */
//    ocfs_nm_heart_beat(osb, HEARTBEAT_METHOD_IPC, 0);

	osb->publ_map |= (1 << osb->node_num);

	/* Create the Rootdirectory oin. */
	osb->vol_state = VOLUME_INIT;

	status = ocfs_create_root_oin (osb);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}
	osb->vol_state = VOLUME_ENABLED;

      finally:

	LOG_EXIT_STATUS (status);
	return (status);
}				/* ocfs_vol_member_reconfig */

/*
 * ocfs_check_volume()
 *
 */
int ocfs_check_volume (ocfs_super * osb)
{
	int status = 0;
	__u64 offset = 0;
	__u8 *buffer = NULL;
	ocfs_publish *publish;

	LOG_ENTRY ();

	/* Read the node's publish sector */
	offset = osb->vol_layout.publ_sect_off +
		      (osb->node_num * osb->sect_size);
	status = ocfs_read_force_disk_ex (osb, (void **)&buffer, osb->sect_size,
					  osb->sect_size, offset);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	publish = (ocfs_publish *) buffer;

	if (publish->dirty) {
		ocfs_down_sem (&(osb->osb_res), true);
		status = ocfs_recover_vol (osb, osb->node_num);
		ocfs_up_sem (&(osb->osb_res));
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}
	}

      finally:
	ocfs_safefree (buffer);
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_check_volume */


/*
 * ocfs_delete_osb()
 *
 * The routine gets called from dismount or close whenever a dismount on 
 * volume is requested and the osb open count becomes 1.
 * It will remove the osb from the global list and also free up all the
 * initialized resources and fileobject.
 */
void ocfs_delete_osb (ocfs_super * osb)
{
	struct list_head null_list;

	LOG_ENTRY ();

	if (!osb)
		goto finito;

	memset (&null_list, 0, sizeof(struct list_head));
	ocfs_down_sem (&(OcfsGlobalCtxt.res), true);
	if (memcmp (&osb->osb_next, &null_list, sizeof(struct list_head)))
		list_del (&(osb->osb_next));
	ocfs_up_sem (&(OcfsGlobalCtxt.res));

	ocfs_del_sem (&(osb->osb_res));
	ocfs_del_sem (&(osb->log_lock));
	ocfs_del_sem (&(osb->recovery_lock));
	ocfs_del_sem (&(osb->map_lock));
	ocfs_extent_map_destroy (&osb->metadata_map);
	ocfs_extent_map_destroy (&osb->trans_map);
	ocfs_safefree(osb->data_prealloc);
	ocfs_safefree(osb->md_prealloc);
	ocfs_safefree(osb->cfg_prealloc);
	ocfs_safefree(osb->log_prealloc);
	memset (osb, 0, sizeof (ocfs_super));

finito:
	LOG_EXIT ();
	return;
}				/* ocfs_delete_osb */

/*
 * ocfs_commit_cache()
 *
 */
int ocfs_commit_cache (ocfs_super * osb, bool Flag)
{
	int status = 0;

	LOG_ENTRY ();

	ocfs_flush_cache (osb);

	ocfs_down_sem (&(osb->map_lock), true);

	status = ocfs_write_map_file (osb);
	if (status >= 0) {
		status = ocfs_process_log_file (osb, Flag);
		if (status < 0)
			LOG_ERROR_STATUS (status);

		status = ocfs_extend_system_file (osb,
				(OCFS_FILE_VOL_LOG_FILE + osb->node_num),
				0, NULL);
		if (status < 0)
			LOG_ERROR_STATUS (status);

		osb->log_file_size = 0;

		status = ocfs_extend_system_file (osb,
				(OCFS_FILE_VOL_META_DATA + osb->node_num),
				0, NULL);
		if (status < 0)
			LOG_ERROR_STATUS (status);

		ocfs_extent_map_destroy (&osb->metadata_map);
		ocfs_extent_map_destroy (&osb->trans_map);
		ocfs_extent_map_init (&osb->metadata_map);
		ocfs_extent_map_init (&osb->trans_map);
	}

	ocfs_up_sem (&(osb->map_lock));

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_commit_cache */

/*
 * ocfs_is_dir_empty()
 * 
 */
int ocfs_is_dir_empty (ocfs_super * osb, ocfs_dir_node * dirnode, bool * empty)
{
	ocfs_dir_node *dn = NULL;
	__u64 offset;
	int status = 0;

	LOG_ENTRY ();

	*empty = true;

	if (dirnode->num_ent_used != 0) {
		*empty = false;
		goto bail;
	}

	offset = dirnode->next_node_ptr;
	if (offset == INVALID_NODE_POINTER)
		goto bail;

	dn = ocfs_malloc (OCFS_SECTOR_SIZE);
	if (dn == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto bail;
	}

	while (1) {
		status = ocfs_read_sector (osb, dn, offset);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto bail;
		}

		if (dn->num_ent_used != 0) {
			*empty = false;
			goto bail;
		}

		offset = dn->next_node_ptr;
		if (offset == INVALID_NODE_POINTER)
			goto bail;
	}

bail:
	LOG_TRACE_ARGS("status=%d, dir=%u.%u is %s\n", status,
		       HILO(dirnode->node_disk_off),
		       (*empty ? "empty" : "not empty"));
	ocfs_safefree (dn);
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs _is_dir_empty */
