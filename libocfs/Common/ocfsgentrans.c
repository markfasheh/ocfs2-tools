/*
 * ocfsgentrans.c
 *
 * Logging and recovery for file system structures.
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

#ifdef __KERNEL__
#include <ocfs.h>
#else
#include <libocfs.h>
#endif


/* Tracing */
#define OCFS_DEBUG_CONTEXT      OCFS_DEBUG_CONTEXT_TRANS

/*
 * ocfs_free_disk_bitmap()
 *
 * 
 *
 * called by: ocfs_process_record()
 */
int ocfs_free_disk_bitmap (ocfs_super * osb, ocfs_cleanup_record * log_rec)
{
	int status = 0;
	__u32 num_upd;
	__u32 i;
	__u32 node_num;
	ocfs_free_log **free_dir_node = NULL;
	ocfs_free_log **free_ext_node = NULL;
	ocfs_free_log *free_vol_bits = NULL;
	ocfs_lock_res **dirnode_lockres = NULL;
	ocfs_lock_res **extnode_lockres = NULL;
	ocfs_lock_res *vol_lockres = NULL;
	ocfs_free_log *tmp_log;
	ocfs_free_log *free_log;
	__u32 tmp_indx;
	__u64 lock_id;
        ocfs_file_entry *fe = NULL;
        ocfs_bitmap_lock *bm_lock = NULL;

	LOG_ENTRY_ARGS ("(0x%p, 0x%p)\n", osb, log_rec);

#define ALLOC_BLOCK(ptr, len, err)				\
	do {							\
		(ptr) = ocfs_malloc (len);			\
		if (!(ptr)) {					\
			LOG_ERROR_STATUS ((err) = -ENOMEM);	\
			goto finally;				\
		}						\
	} while (0)

	ALLOC_BLOCK(free_dir_node,
		    OCFS_MAXIMUM_NODES * sizeof (ocfs_free_log *), status);
	ALLOC_BLOCK(free_ext_node,
		    OCFS_MAXIMUM_NODES * sizeof (ocfs_free_log *), status);
	ALLOC_BLOCK(dirnode_lockres,
		    OCFS_MAXIMUM_NODES * sizeof (ocfs_lock_res *), status);
	ALLOC_BLOCK(extnode_lockres,
		    OCFS_MAXIMUM_NODES * sizeof (ocfs_lock_res *), status);

	/* init */
	for (i = 0; i < OCFS_MAXIMUM_NODES; i++) {
		free_dir_node[i] = NULL;
		free_ext_node[i] = NULL;
	}

	free_log = &(log_rec->rec.free);

	/* alloc memory */
	num_upd = free_log->num_free_upds;
	for (i = 0; i < num_upd; i++) {
		switch (free_log->free_bitmap[i].type) {
		    case DISK_ALLOC_DIR_NODE:
			    node_num = free_log->free_bitmap[i].node_num;
			    if (free_dir_node[node_num] == NULL) {
				    free_dir_node[node_num] =
					ocfs_malloc (sizeof (ocfs_free_log));
				    if (free_dir_node[node_num] == NULL) {
					    LOG_ERROR_STATUS (status = -ENOMEM);
					    goto finally;
				    }
				    free_dir_node[node_num]->num_free_upds = 0;
			    }
			    tmp_log = free_dir_node[node_num];
			    break;

		    case DISK_ALLOC_EXTENT_NODE:
			    node_num = free_log->free_bitmap[i].node_num;
			    if (free_ext_node[node_num] == NULL) {
				    free_ext_node[node_num] =
					ocfs_malloc (sizeof (ocfs_free_log));
				    if (free_ext_node[node_num] == NULL) {
					    LOG_ERROR_STATUS (status = -ENOMEM);
					    goto finally;
				    }
				    free_ext_node[node_num]->num_free_upds = 0;
			    }
			    tmp_log = free_ext_node[node_num];
			    break;

		    case DISK_ALLOC_VOLUME:
			    if (free_vol_bits == NULL) {
				    free_vol_bits =
					ocfs_malloc (sizeof (ocfs_free_log));
				    if (free_vol_bits == NULL) {
					    LOG_ERROR_STATUS (status = -ENOMEM);
					    goto finally;
				    }
				    free_vol_bits->num_free_upds = 0;
			    }
			    tmp_log = free_vol_bits;
			    break;

		    default:
			    tmp_log = NULL;
			    break;
		}

		if (tmp_log) {
			ocfs_free_bitmap *fb1, *fb2;

			tmp_indx = tmp_log->num_free_upds;

			fb1 = &(tmp_log->free_bitmap[tmp_indx]);
			fb2 = &(free_log->free_bitmap[i]);

			fb1->length = fb2->length;
			fb1->file_off = fb2->file_off;
			fb1->type = fb2->type;
			fb1->node_num = fb2->node_num;

			tmp_log->num_free_upds++;
		}
	}

	/* Get all locks */
	if (free_vol_bits != NULL) {
		fe = ocfs_allocate_file_entry();
		if (!fe) {
			LOG_ERROR_STATUS (status = -ENOMEM);
			goto finally;
		}
		bm_lock = (ocfs_bitmap_lock *)fe;
		status = ocfs_acquire_lock (osb, OCFS_BITMAP_LOCK_OFFSET,
					    OCFS_DLM_EXCLUSIVE_LOCK,
					    FLAG_FILE_CREATE, &vol_lockres, fe);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}
	}

	lock_id = (OCFS_FILE_DIR_ALLOC_BITMAP * osb->sect_size) +
		  osb->vol_layout.root_int_off;
	for (i = 0; i < OCFS_MAXIMUM_NODES; i++, lock_id += osb->sect_size) {
		if (free_dir_node[i] != NULL) {
			status = ocfs_acquire_lock (osb, lock_id,
						    OCFS_DLM_EXCLUSIVE_LOCK,
						    FLAG_FILE_CREATE,
						    &(dirnode_lockres[i]), NULL);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto finally;
			}
		}
	}

	lock_id = (OCFS_FILE_FILE_ALLOC_BITMAP * osb->sect_size) +
		  osb->vol_layout.root_int_off;
	for (i = 0; i < OCFS_MAXIMUM_NODES; i++, lock_id += osb->sect_size) {
		if (free_ext_node[i] != NULL) {
			status = ocfs_acquire_lock (osb, lock_id,
				 		    OCFS_DLM_EXCLUSIVE_LOCK,
						    FLAG_FILE_CREATE,
						    &(extnode_lockres[i]), NULL);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto finally;
			}
		}
	}

	/* free vol block */
	if (free_vol_bits != NULL)
		ocfs_free_vol_block (osb, free_vol_bits, -1, DISK_ALLOC_VOLUME);

	/* We can potentiallly loose some allocation for dirNodes or extent */
	/* nodes but they should not be much...  */
	for (i = 0; i < OCFS_MAXIMUM_NODES; i++) {
		if (free_dir_node[i] != NULL)
			ocfs_free_vol_block (osb, free_dir_node[i], i,
					     DISK_ALLOC_DIR_NODE);

		if (free_ext_node[i] != NULL)
			ocfs_free_vol_block (osb, free_ext_node[i], i,
					     DISK_ALLOC_EXTENT_NODE);
	}

	/* release all locks */
	if (free_vol_bits != NULL) {
                bm_lock->used_bits = ocfs_count_bits(&osb->cluster_bitmap);
                status = ocfs_write_force_disk(osb, bm_lock, OCFS_SECTOR_SIZE, 
                                               OCFS_BITMAP_LOCK_OFFSET);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}
		status = ocfs_release_lock (osb, OCFS_BITMAP_LOCK_OFFSET,
					    OCFS_DLM_EXCLUSIVE_LOCK,
					    FLAG_FILE_CREATE, vol_lockres, fe);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}
	}

	lock_id = (OCFS_FILE_DIR_ALLOC_BITMAP * osb->sect_size) +
		  osb->vol_layout.root_int_off;
	for (i = 0; i < OCFS_MAXIMUM_NODES; i++, lock_id += osb->sect_size) {
		if (free_dir_node[i] != NULL) {
			status = ocfs_release_lock (osb, lock_id,
						    OCFS_DLM_EXCLUSIVE_LOCK,
						    FLAG_FILE_CREATE,
						    dirnode_lockres[i], NULL);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto finally;
			}
		}
	}

	lock_id = (OCFS_FILE_FILE_ALLOC_BITMAP * osb->sect_size) +
		  osb->vol_layout.root_int_off;
	for (i = 0; i < OCFS_MAXIMUM_NODES; i++, lock_id += osb->sect_size) {
		if (free_ext_node[i] != NULL) {
			status = ocfs_release_lock (osb, lock_id,
						    OCFS_DLM_EXCLUSIVE_LOCK,
						    FLAG_FILE_CREATE,
						    extnode_lockres[i], NULL);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto finally;
			}
		}

	}

      finally:
	for (i = 0; i < OCFS_MAXIMUM_NODES; i++) {
		if (free_dir_node[i])
			ocfs_put_lockres (dirnode_lockres[i]);
		if (free_ext_node[i])
			ocfs_put_lockres (extnode_lockres[i]);
	}
	ocfs_put_lockres (vol_lockres);

	for (i = 0; i < OCFS_MAXIMUM_NODES; i++) {
		ocfs_safefree (free_dir_node[i]);
		ocfs_safefree (free_ext_node[i]);
	}

	ocfs_safefree (free_dir_node);
	ocfs_safefree (free_ext_node);
	ocfs_safefree (dirnode_lockres);
	ocfs_safefree (extnode_lockres);

	ocfs_safefree (free_vol_bits);
	ocfs_release_file_entry(fe);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_free_disk_bitmap */

/*
 * ocfs_process_record()
 *
 *
 * called by: ocfs_process_log()
 */
int ocfs_process_record (ocfs_super * osb, void *buffer)
{
	int status = 0;
	ocfs_log_record *log_rec;
	ocfs_cleanup_record *clean_rec;
	ocfs_file_entry *fe = NULL;
	ocfs_dir_node *lock_node = NULL;
	__u8 *read_buf = NULL;
	__u32 node_num;
	__u32 index;
	ocfs_extent_group *alloc_ext;
	__u64 disk_off = 0;
	__u32 num_upd;
	__u32 i;
	__u64 lock_id;
	ocfs_lock_res *lock_res;
	ocfs_lock_res **lock_res_array = NULL;
	ocfs_lock_res *tmp_lockres;

	LOG_ENTRY_ARGS ("(0x%p, 0x%p)\n", osb, buffer);

	log_rec = (ocfs_log_record *) buffer;
	clean_rec = (ocfs_cleanup_record *) buffer;

	switch (log_rec->log_type) {
	    case LOG_TYPE_DISK_ALLOC:
	    {
		    switch (log_rec->rec.alloc.type) {
			case DISK_ALLOC_DIR_NODE:
			case DISK_ALLOC_EXTENT_NODE:
				status = ocfs_free_node_block (osb,
						   log_rec->rec.alloc.file_off,
						   log_rec->rec.alloc.length,
						   log_rec->rec.alloc.node_num,
						   log_rec->rec.alloc.type);
				break;
			default:
				break;
		    }
	    }
		    break;

	    case LOG_DELETE_NEW_ENTRY:
		    status = ocfs_get_file_entry (osb, &fe,
						  log_rec->rec.del.ent_del);
		    if (status < 0) {
			    LOG_ERROR_STATUS (status);
			    goto finally;
		    }

		    status = ocfs_get_file_entry (osb,
					  (ocfs_file_entry **) (&lock_node),
					  log_rec->rec.del.parent_dirnode_off);
		    if (status < 0) {
			    LOG_ERROR_STATUS (status);
			    goto finally;
		    }

		    node_num = log_rec->rec.del.node_num;

		    /*
		       ** Lock on directory shd be held by the node which either
		       ** died or this node...
		     */
		    status = ocfs_del_file_entry (osb, fe, lock_node);
		    if (status < 0) {
			    LOG_ERROR_STATUS (status);
			    goto finally;
		    }
		    break;

	    case LOG_DELETE_ENTRY:
		    /*
		       ** Delete the entry from the dir node it was associated
		       ** with. Now it can be reused.
		     */
		    status = ocfs_get_file_entry (osb, &fe,
						  clean_rec->rec.del.ent_del);
		    if (status < 0) {
			    LOG_ERROR_STATUS (status);
			    goto finally;
		    }

		    status = ocfs_get_file_entry (osb,
					 (ocfs_file_entry **) (&lock_node),
					 clean_rec->rec.del.parent_dirnode_off);
		    if (status < 0) {
			    LOG_ERROR_STATUS (status);
			    goto finally;
		    }

		    node_num = clean_rec->rec.del.node_num;

		    /*
		       ** Lock on directory shd be held by the node which
		       ** either died or this node...
		     */
		    status = ocfs_del_file_entry (osb, fe, lock_node);
		    if (status < 0) {
			    LOG_ERROR_STATUS (status);
			    goto finally;
		    }
		    break;

	    case LOG_MARK_DELETE_ENTRY:
		    status = ocfs_get_file_entry (osb, &fe,
						  log_rec->rec.del.ent_del);
		    if (status < 0) {
			    LOG_ERROR_STATUS (status);
			    goto finally;
		    }

		    if (log_rec->rec.del.flags & FLAG_RESET_VALID) {
			    OCFS_SET_FLAG (fe->sync_flags, OCFS_SYNC_FLAG_VALID);
			    status = ocfs_write_file_entry (osb, fe,
						      log_rec->rec.del.ent_del);
			    if (status < 0) {
				    LOG_ERROR_STATUS (status);
				    goto finally;
			    }

			    /* We are done... */
			    status = 0;
			    goto finally;
		    }

		    /*
		       ** Read in the entry to be deleted. We are doing
		       ** recovery on another node?
		       ** What if we were in abort trans for this node???
		     */
		    node_num = log_rec->rec.del.node_num;

		    /* This is recovery for a dead node */
		    if (fe->sync_flags & OCFS_SYNC_FLAG_VALID) {
			    /* No recovery needed for the entry, let it stay */
			    status = 0;
			    goto finally;
		    } else {
			    status = ocfs_delete_file_entry (osb, fe,
						log_rec->rec.del.parent_dirnode_off,
						node_num);
			    goto finally;
		    }
		    break;

	    case LOG_FREE_BITMAP:
		    status = ocfs_free_disk_bitmap (osb, buffer);
		    if (status < 0) {
			    LOG_ERROR_STATUS (status);
			    goto finally;
		    }
		    break;

	    case LOG_UPDATE_EXTENT:
		     /* Make sure we have the file lock here */
		    disk_off = log_rec->rec.extent.disk_off;
		    status = ocfs_read_disk_ex (osb, (void **)&read_buf,
				osb->sect_size, osb->sect_size, disk_off);
		    if (status < 0) {
			    LOG_ERROR_STATUS (status);
			    goto finally;
		    }

		    alloc_ext = (ocfs_extent_group *) read_buf;

		    index = log_rec->rec.extent.index;

		    alloc_ext->extents[index].file_off = 0;
		    alloc_ext->extents[index].num_bytes = 0;
		    alloc_ext->extents[index].disk_off = 0;

		    disk_off = log_rec->rec.extent.disk_off;

		    status = ocfs_write_disk (osb, read_buf,
					      (__u32) osb->sect_size, disk_off);
		    if (status < 0) {
			    LOG_ERROR_STATUS (status);
			    goto finally;
		    }
		    break;

	    case LOG_TYPE_DIR_NODE:
		    status = ocfs_recover_dir_node (osb,
						    log_rec->rec.dir.orig_off,
						    log_rec->rec.dir.saved_off);
		    if (status < 0) {
			    LOG_ERROR_STATUS (status);
			    /*
			       ** Bad one. We should disable this volume and try
			       ** and let somebody else do the recovery...
			     */
		    }
		    break;

	    case LOG_TYPE_RECOVERY:
		    /*
		       ** This node was recovering another node and died.
		       ** All locks in the function need to be recursive...
		     */
		    node_num = osb->node_recovering;

		    status = ocfs_recover_vol (osb,
					       log_rec->rec.recovery.node_num);
		    if (status < 0) {
			    LOG_ERROR_STATUS (status);
			    /*
			       ** Bad one. We should disable this volume and try
			       ** and let somebody else do the recovery...
			     */
		    }
		    osb->node_recovering = node_num;
		    break;

	    case LOG_TYPE_TRANS_START:
		    /* We are back to the record till which we needed to */
		    /* roll back. Check to ensure the file size for recovery */
		    /* log is 1 rec long */
		    status = 0;
		    break;

	    case LOG_CLEANUP_LOCK:
		    lock_res_array = ocfs_malloc (LOCK_UPDATE_LOG_SIZE *
						  sizeof (ocfs_lock_res *));
		    if (!lock_res_array) {
			    LOG_ERROR_STATUS (status = -ENOMEM);
			    goto finally;
		    }

		    num_upd = clean_rec->rec.lock.num_lock_upds;

		    for (i = 0; i < num_upd; i++) {
			    lock_id = clean_rec->rec.lock.lock_upd[i].orig_off;
			    lock_res_array[i] = NULL;

			    status = ocfs_lookup_sector_node (osb, lock_id,
							      &lock_res);
			    if (status >= 0) {
				    ocfs_remove_sector_node (osb, lock_res);
				    lock_res->sector_num =
					clean_rec->rec.lock.lock_upd[i].new_off;
				    lock_res_array[i] = lock_res;
			    } else {
				    /* We don't have the resource so don't */
				    /* bother with it */
			    }
		    }

		    for (i = 0; i < num_upd; i++) {
			    tmp_lockres = NULL;
			    lock_res = lock_res_array[i];
			    if (lock_res) {
				    /* Reinsert with new ID */
				    status = ocfs_insert_sector_node (osb, lock_res,
								      &tmp_lockres);
				    if (status < 0) {
					    LOG_ERROR_STATUS (status);
					    goto finally;
				    }
				    if (tmp_lockres)
					    LOG_ERROR_STR ("This too can happen");
				    else
					    ocfs_put_lockres (lock_res);
			    }
		    }
		    break;

	    default:
		    break;
	}

      finally:
	if (fe)
		ocfs_release_file_entry (fe);
	if (lock_node)
		ocfs_release_file_entry ((ocfs_file_entry *) lock_node);
	ocfs_safefree (read_buf);
	ocfs_safefree (lock_res_array);
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_process_record */

/*
 * ocfs_process_log()
 *
 *
 * called by: ocfs_commit_trans(), ocfs_abort_trans(), ocfs_recover_vol()
 */
int ocfs_process_log (ocfs_super * osb, __u64 trans_id, __u32 node_num, __u32 * type)
{
	int status = 0;
	__u64 file_size;
	__u64 offset;
	__u64 alloc_size;
	__u32 log_type;
	__u32 log_rec_size;
	__u32 size;
	__u32 log_file_id;
	ocfs_log_record *log_rec = NULL;
        bool use_prealloc = false;

	LOG_ENTRY_ARGS ("(0x%p, %u.%u, %u, 0x%p)\n", osb, HI (trans_id),
			LO (trans_id), node_num, type);

	log_type = *type;

	if (log_type == LOG_RECOVER) {
		log_file_id = (LOG_FILE_BASE_ID + node_num);
		log_rec_size = osb->sect_size;
	} else if (log_type == LOG_CLEANUP) {
		log_file_id = (CLEANUP_FILE_BASE_ID + node_num);
		log_rec_size = sizeof (ocfs_cleanup_record);
		log_rec_size = (__u32) OCFS_ALIGN (log_rec_size, osb->sect_size);
	} else {
		LOG_ERROR_ARGS ("logtype=%u is invalid", log_type);
		goto finally;
	}

	size = log_rec_size;
	size = (__u32) OCFS_ALIGN (size, OCFS_PAGE_SIZE);

        /* try to use prealloc log record if available */
        ocfs_down_sem (&osb->osb_res, true);
        if (! OSB_PREALLOC_LOCK_TEST(osb, OSB_LOG_LOCK))
        {
                OSB_PREALLOC_LOCK_SET(osb, OSB_LOG_LOCK);
                log_rec = (ocfs_log_record *)osb->log_prealloc;
                use_prealloc = true;
        }
        ocfs_up_sem(&osb->osb_res);

        if (log_rec == NULL)
        {
	        if ((log_rec = ocfs_malloc (size)) == NULL) {
		        LOG_ERROR_STATUS (status = -ENOMEM);
		        goto finally;
	        }
        }

	status = ocfs_get_system_file_size (osb, log_file_id, &file_size, &alloc_size);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	if (file_size <= 0) {
		if (log_type == LOG_RECOVER)
			*type = LOG_CLEANUP;
		goto finally;
	} else {
		if (log_type == LOG_RECOVER) {
			/*
			   **  This helps in bdcast recovery by having other nodes just set
			   **  the event and not process cleanup log
			 */
			status = ocfs_extend_system_file (osb,
					  (CLEANUP_FILE_BASE_ID + node_num), 0, NULL);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto finally;
			}
		}
	}

	while (file_size != 0) {
		/* Recover from the log file */
		/* Read in the last record */
		offset = file_size - log_rec_size;
		status = ocfs_read_system_file (osb, log_file_id, log_rec,
						log_rec_size, offset);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}

		status = ocfs_process_record (osb, log_rec);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}

		status = ocfs_extend_system_file (osb, log_file_id, offset, NULL);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}

		status = ocfs_get_system_file_size (osb, log_file_id,
						    &file_size, &alloc_size);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}
	}

      finally:
        ocfs_down_sem (&osb->osb_res, true);
        if (use_prealloc && OSB_PREALLOC_LOCK_TEST(osb, OSB_LOG_LOCK))
                OSB_PREALLOC_LOCK_CLEAR(osb, OSB_LOG_LOCK);
        else
                ocfs_safefree (log_rec);
        ocfs_up_sem(&osb->osb_res);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_process_log() */

/*
 * ocfs_start_trans()
 *
 *
 * called by: ocfs_create_modify_file(), ocfs_set_rename_information()
 */
int ocfs_start_trans (ocfs_super * osb)
{
	LOG_ENTRY_ARGS ("(0x%p)\n", osb);

	down (&osb->trans_lock);

	osb->curr_trans_id = osb->vol_node_map.largest_seq_num;

	if (osb->needs_flush) {
		while (osb->needs_flush)
			ocfs_sleep (100);	/* in ms */
	}

	osb->trans_in_progress = true;

	LOG_EXIT_STATUS (0);
	return 0;
}				/* ocfs_start_trans */

/*
 * ocfs_commit_trans()
 *
 *
 * called by: ocfs_create_modify_file(), ocfs_set_rename_information()
 */
int ocfs_commit_trans (ocfs_super * osb, __u64 trans_id)
{
	int status = 0;
	__u64 offset = 0;
	__u32 log_type;

	LOG_ENTRY_ARGS ("(0x%p, %u.%u)\n", osb, HI (trans_id), LO (trans_id));

	/* Log to the file for multiple transactions... */
	status = ocfs_extend_system_file (osb,
				(LOG_FILE_BASE_ID + osb->node_num), offset, NULL);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	log_type = LOG_CLEANUP;

	status = ocfs_process_log (osb, trans_id, osb->node_num, &log_type);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	status = ocfs_extend_system_file (osb,
				(CLEANUP_FILE_BASE_ID + osb->node_num), offset, NULL);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	osb->curr_trans_id = -1;

      finally:
	osb->trans_in_progress = false;
	up (&osb->trans_lock);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_commit_trans */

/*
 * ocfs_abort_trans()
 *
 *
 * called by: ocfs_create_modify_file(), ocfs_set_rename_information()
 */
int ocfs_abort_trans (ocfs_super * osb, __u64 trans_id)
{
	int status = 0;
	__u64 offset = 0;
	__u32 log_type;

	LOG_ENTRY_ARGS ("(0x%p, %u.%u)\n", osb, HI (trans_id), LO (trans_id));

	/* Read the log file and free up stf... */
	log_type = LOG_RECOVER;

	status = ocfs_process_log (osb, trans_id, osb->node_num, &log_type);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	status = ocfs_extend_system_file (osb,
				(LOG_FILE_BASE_ID + osb->node_num), offset, NULL);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	status = ocfs_extend_system_file (osb,
				(CLEANUP_FILE_BASE_ID + osb->node_num), offset, NULL);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	osb->curr_trans_id = -1;

      finally:
	osb->trans_in_progress = false;
	up (&osb->trans_lock);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_abort_trans */

/*
 * ocfs_reset_publish()
 *
 *
 * called by: ocfs_recover_vol()
 *
 * NOTE: This function is very similar to ocfs_disk_reset_voting().
 * This function should replace the other one.
 */
int ocfs_reset_publish (ocfs_super * osb, __u64 node_num)
{
	int status = 0;
	ocfs_publish *publish = NULL;
	__u64 node_publ_off;

	LOG_ENTRY_ARGS ("(0x%p, %u.%u)\n", osb, HI (node_num), LO (node_num));

	/* Read the publish sector */
	node_publ_off = osb->vol_layout.publ_sect_off +
		        (node_num * osb->sect_size);
	status = ocfs_read_disk_ex (osb, (void **)&publish, osb->sect_size,
				    osb->sect_size, node_publ_off);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	publish->dirty = false;
	publish->vote = 0;
	publish->vote_type = 0;

	/* Write the publish sector */
	status = ocfs_write_disk (osb, publish, osb->sect_size, node_publ_off);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

      finally:
	ocfs_safefree (publish);

	LOG_EXIT_STATUS (status);
	return (status);
}				/* ocfs_reset_publish */


/*
 * ocfs_recover_vol()
 *
 * called by: ocfs_process_record(), ocfs_disk_request_vote(),
 *            ocfs_acquire_lock(), ocfs_check_volume(), ocfs_nm_thread()
 */
int ocfs_recover_vol (ocfs_super * osb, __u64 node_num)
{
	int status = 0;
	int tmpstat;
	bool recovery_lock = false;
	bool lock_acq = false;
	__u64 lock_id = 0;
	__u64 file_size = 0;
	__u64 alloc_size = 0;
	ocfs_lock_res *lock_res = NULL;
	ocfs_log_record *log_rec = NULL;
	__u32 size;
	__u32 log_type;
	__u64 trans_id = 0;
	__u64 cleanup_file_size = 0;
	__u32 file_id;
	ocfs_file_entry *fe = NULL;

	LOG_ENTRY_ARGS ("(0x%p, %u.%u)\n", osb, HI (node_num), LO (node_num));

	fe = ocfs_allocate_file_entry ();
	if (fe == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto finally;
	}

	if (!IS_VALID_NODE_NUM(node_num)) {
		LOG_ERROR_STATUS (status = -EINVAL);
		goto finally;
	}

	/* Grab the local recovery resource to ensure no other thread comes */
	/* in from this node for recovery */
	ocfs_down_sem (&(osb->recovery_lock), true);
	recovery_lock = true;

	if (osb->node_recovering == node_num) {
		goto finally;
	}

	/* Now reset the publish sector to have the dirty bit not set...  */
	status = ocfs_reset_publish (osb, node_num);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	file_id = (__u32) (LOG_FILE_BASE_ID + node_num);

	/* Read in the the recovery log */
	status = ocfs_get_system_file_size (osb, file_id, &file_size,
					    &alloc_size);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	file_id = (__u32) (CLEANUP_FILE_BASE_ID + node_num);
	status = ocfs_get_system_file_size (osb, file_id, &cleanup_file_size,
					    &alloc_size);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	if ((file_size == 0) && (cleanup_file_size == 0)) {
		/* Nothing to do so ...  */
		/*
		   ** Read the log file and go back to the last checkpoint,
		   ** start of file for us. Read the logs for the transaction
		   ** being recovered and un
		 */

		osb->node_recovering = OCFS_INVALID_NODE_NUM;
		status = 0;
		goto finally;
	}

	osb->node_recovering = node_num;
	osb->vol_state = VOLUME_IN_RECOVERY;

	/*
	   ** Grab the lock on the log file for the node which needs recovery,
	   ** this ensures nobody else in the cluster process the recovery
	 */
	lock_id = ((LOG_FILE_BASE_ID + node_num) * osb->sect_size) +
		  osb->vol_layout.root_int_off;

	status = ocfs_acquire_lock (osb, lock_id, OCFS_DLM_EXCLUSIVE_LOCK,
				    FLAG_FILE_CREATE, &lock_res, fe);
	if (status < 0) {
		goto finally;
	}

	lock_acq = true;

	if (node_num != osb->node_num) {
		/*
		   ** Write a log entry indicating this node is doing recovery
		   ** for nodenum, if this node now dies during recovery.
		   ** The node doing recovery for this node will know it needs
		   ** to recover the vol for node node num too...
		 */
		size = max(sizeof (ocfs_log_record),
			   sizeof (ocfs_cleanup_record));
		size = (__u32) OCFS_ALIGN (size, OCFS_PAGE_SIZE);

		if ((log_rec = ocfs_malloc (size)) == NULL) {
			LOG_ERROR_STATUS (status = -ENOMEM);
			goto finally;
		}

		log_rec->log_id = osb->curr_trans_id;
		log_rec->log_type = LOG_TYPE_RECOVERY;
		log_rec->rec.recovery.node_num = node_num;

		/*
		   ** Log the original dirnode sector and the new cluster
		   ** where the info is stored
		 */
		status = ocfs_write_log (osb, log_rec, LOG_RECOVER);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}
	}

	log_type = LOG_RECOVER;

	status = ocfs_process_log (osb, trans_id, osb->node_num, &log_type);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	/*
	   ** If a cleanup file exists we should just reset the file size
	   ** if we aborted the transaction otherwise process the cleanup file....
	 */
	if (log_type == LOG_CLEANUP) {
		status = ocfs_process_log (osb, trans_id, osb->node_num,
					   &log_type);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}
	}

	/* Read the log file and go back to the last checkpoint, */
	/* start of file for us. Read the logs for the transaction  */
	/* being recovered and un */
	osb->node_recovering = OCFS_INVALID_NODE_NUM;

	/* The vol state migh thave to turn inti flags...  */
	osb->vol_state = VOLUME_ENABLED;

	if (recovery_lock) {
		ocfs_up_sem (&(osb->recovery_lock));
		recovery_lock = false;
	}

      finally:
	if (recovery_lock) {
		ocfs_up_sem (&(osb->recovery_lock));
		recovery_lock = false;
	}

	if (lock_acq) {
		tmpstat = ocfs_release_lock (osb, lock_id,
					     OCFS_DLM_EXCLUSIVE_LOCK,
					     FLAG_FILE_CREATE, lock_res, fe);
		if (tmpstat < 0)
			LOG_ERROR_STATUS (tmpstat);
	}
	ocfs_release_file_entry (fe);
	ocfs_put_lockres (lock_res);
	LOG_EXIT_STATUS (status);
	return (status);
}				/* ocfs_recover_vol */

/*
 * ocfs_write_log()
 *
 * called by: ocfs_recover_vol(), ocfs_del_file(), ocfs_alloc_node_block()
 */
int ocfs_write_log (ocfs_super * osb, ocfs_log_record * log_rec, __u32 type)
{
	int status = 0;
	int tmpstat;
	__s32 log_file_id = -1;
	__u64 lock_id = 0;
	__u64 file_size = 0;
	__u64 offset = 0;
	__u64 log_rec_size = 0;
	__u64 alloc_size = 0;
	ocfs_lock_res *lock_res = NULL;
	bool log_lock = false;
	bool lock_acq = false;
	ocfs_file_entry *fe = NULL;

	LOG_ENTRY_ARGS ("(0x%p, 0x%p, %u)\n", osb, log_rec, type);

	fe = ocfs_allocate_file_entry ();
	if (!fe) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto finally;
	}

	/* Get the log lock */
	ocfs_down_sem (&(osb->log_lock), true);
	log_lock = true;

	if (type == LOG_RECOVER) {
		log_file_id = (LOG_FILE_BASE_ID + osb->node_num);
		log_rec_size = osb->sect_size;
	} else if (type == LOG_CLEANUP) {
		log_file_id = (CLEANUP_FILE_BASE_ID + osb->node_num);
		log_rec_size = sizeof (ocfs_cleanup_record);
		log_rec_size = OCFS_ALIGN (log_rec_size, osb->sect_size);
	} else {
		LOG_ERROR_ARGS ("logtype=%u is invalid", type);
		goto finally;
	}

	/*
	   ** Always log to the end of the file after taking a file lock
	   ** and a log lock
	 */
	lock_id = (log_file_id * osb->sect_size) + osb->vol_layout.root_int_off;

	status = ocfs_acquire_lock (osb, lock_id, OCFS_DLM_EXCLUSIVE_LOCK,
				    FLAG_FILE_CREATE, &lock_res, fe);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	lock_acq = true;

	file_size = fe->file_size;
	alloc_size = fe->alloc_size;
	offset = file_size;

	if (alloc_size < (file_size + log_rec_size)) {
		file_size += ONE_MEGA_BYTE;
		status = ocfs_extend_system_file (osb, log_file_id, file_size, fe);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}
	}

	status = ocfs_write_system_file (osb, log_file_id, log_rec,
					 log_rec_size, offset);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	status = ocfs_extend_system_file (osb, log_file_id,
					  (offset + log_rec_size), fe);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

      finally:
	if (log_lock) {
		ocfs_up_sem (&(osb->log_lock));
		log_lock = false;
	}

	if (lock_acq) {
		tmpstat = ocfs_release_lock (osb, lock_id,
					     OCFS_DLM_EXCLUSIVE_LOCK,
					     FLAG_FILE_CREATE, lock_res, fe);
		if (tmpstat < 0)
			LOG_ERROR_STATUS (tmpstat);
	}
	ocfs_release_file_entry (fe);
	ocfs_put_lockres (lock_res);
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_write_log */

/*
 * ocfs_write_node_log()
 *
 * called by: ocfs_free_extents_for_truncate(), <ocfs_delete_file_entry(),
 *            ocfs_del_file(), ocfs_free_directory_block(),
 *            ocfs_insert_dir_node(), ocfs_free_file_extents()
 */
int ocfs_write_node_log (ocfs_super * osb, ocfs_log_record * log_rec,
			 __u32 node_num, __u32 type)
{
	int status = 0;
	int tmpstat;
	__s32 log_file_id = -1;
	__u64 lock_id = 0;
	__u64 file_size = 0;
	__u64 offset = 0;
	__u64 log_rec_size = 0;
	__u64 alloc_size = 0;
	ocfs_lock_res *lock_res = NULL;
	bool log_lock = false;
	bool lock_acq = false;
	ocfs_file_entry *fe = NULL;

	LOG_ENTRY_ARGS ("(0x%p, 0x%p, %u, %u)\n", osb, log_rec, node_num,
			type);

	fe = ocfs_allocate_file_entry ();
	if (!fe) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto finally;
	}

	/* Get the log lock */
	ocfs_down_sem (&(osb->log_lock), true);
	log_lock = true;

	if (type == LOG_RECOVER) {
		log_file_id = (LOG_FILE_BASE_ID + node_num);
		log_rec_size = osb->sect_size;
	} else if (type == LOG_CLEANUP) {
		log_file_id = (CLEANUP_FILE_BASE_ID + node_num);
		log_rec_size = sizeof (ocfs_cleanup_record);
		log_rec_size = OCFS_ALIGN (log_rec_size, osb->sect_size);
	} else {
		LOG_ERROR_ARGS ("logtype=%u is invalid", type);
		goto finally;
	}

	/* Always log to the eof after taking a file lock and a log lock */
	lock_id = (log_file_id * osb->sect_size) + osb->vol_layout.root_int_off;

	status = ocfs_acquire_lock (osb, lock_id, OCFS_DLM_EXCLUSIVE_LOCK,
				    FLAG_FILE_CREATE, &lock_res, fe);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	lock_acq = true;

	file_size = fe->file_size;
	alloc_size = fe->alloc_size;
	offset = file_size;

	if (alloc_size < (file_size + log_rec_size)) {
		file_size += ONE_MEGA_BYTE;
		status = ocfs_extend_system_file (osb, log_file_id, file_size, fe);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}
	}

	status = ocfs_write_system_file (osb, log_file_id, log_rec,
					 log_rec_size, offset);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	status = ocfs_extend_system_file (osb, log_file_id,
					  (offset + log_rec_size), fe);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

      finally:
	if (log_lock) {
		ocfs_up_sem (&(osb->log_lock));
		log_lock = false;
	}

	if (lock_acq) {
		tmpstat =
		    ocfs_release_lock (osb, lock_id, OCFS_DLM_EXCLUSIVE_LOCK,
				       FLAG_FILE_CREATE, lock_res, fe);
		if (tmpstat < 0)
			LOG_ERROR_STATUS (tmpstat);
	}
	ocfs_release_file_entry (fe);
	ocfs_put_lockres (lock_res);
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_write_node_log */
