/*
 * ocfsgennm.c
 *
 * process vote, nm thread, etc.
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
struct inode * ocfs_get_inode_from_offset(ocfs_super * osb, __u64 fileoff);
#else
#include <libocfs.h>
#endif


/* Tracing */
#define OCFS_DEBUG_CONTEXT      OCFS_DEBUG_CONTEXT_NM


/*
 * ocfs_flush_data()
 * 
 */
int ocfs_flush_data (ocfs_inode * oin)
{
	int status = 0;

	LOG_ENTRY ();

	OCFS_ASSERT(IS_VALID_OIN(oin));

	if (oin->oin_flags & OCFS_OIN_DIRECTORY)
		goto bail;

	ocfs_down_sem (&(oin->main_res), true);

	oin->cache_enabled = false;
	ocfs_flush_cache (oin->osb);

	/* Grab and release PagingIo to serialize ourselves with the lazy writer. */
	/* This will work to ensure that all IO has completed on the cached */
	/* data and we will succesfully tear away the cache section. */
	ocfs_down_sem (&(oin->paging_io_res), true);
	ocfs_up_sem (&(oin->paging_io_res));

	ocfs_purge_cache_section (oin, NULL, 0);

	ocfs_up_sem (&(oin->main_res));

      bail:
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_flush_data */

/*
 * ocfs_disk_update_resource()
 *
 * @osb: ocfs super block for the volume
 * @lock_res: lockres to be updated
 * @file_ent: corresponding file entry
 *
 * Updates the in memory lock resource from the disklock info
 * stored in the file entry on disk.
 *
 * Returns 0 if success, < 0 if error.
 */
int ocfs_disk_update_resource (ocfs_super * osb, ocfs_lock_res * lock_res,
			       ocfs_file_entry * file_ent, __u32 timeout)
{
	int status = 0;
	ocfs_file_entry *fe = NULL;

	LOG_ENTRY_ARGS ("(0x%p, 0x%p, 0x%p)\n", osb, lock_res,
			file_ent);

	if (file_ent) {
		fe = file_ent;
		status = ocfs_read_file_entry (osb, fe, lock_res->sector_num);
	} else
		status = ocfs_get_file_entry (osb, &fe, lock_res->sector_num);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	status = ocfs_acquire_lockres_ex (lock_res, timeout);
	if (status < 0) {
		LOG_TRACE_ARGS ("Timedout locking lockres for id: %u.%u\n",
			HI(lock_res->sector_num), LO(lock_res->sector_num));
		goto finally;
	}

	lock_res->lock_type = DISK_LOCK_FILE_LOCK (fe);
	lock_res->master_node_num = DISK_LOCK_CURRENT_MASTER (fe);
	lock_res->oin_openmap = DISK_LOCK_OIN_MAP (fe);

	ocfs_release_lockres (lock_res);

      finally:
	if (file_ent == NULL)
		ocfs_release_file_entry (fe);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_disk_update_resource */


int ocfs_check_for_stale_lock(ocfs_super *osb, ocfs_lock_res *lockres, ocfs_file_entry *fe, __u64 lock_id)
{
	int status = 0;
	ocfs_file_entry *tmpfe = NULL;

	LOG_ENTRY_ARGS ("(0x%p, 0x%p, 0x%p, %u.%u)\n", osb, lockres, fe,
		       	HILO(lock_id));

	if (fe == NULL) {
		tmpfe = ocfs_allocate_file_entry ();
		if (tmpfe == NULL) {
			LOG_ERROR_STATUS (status == -ENOMEM);
			goto bail;
		}
		status = ocfs_read_force_disk (osb, tmpfe, osb->sect_size, lock_id);
		if (status < 0) {
			LOG_ERROR_STATUS(status);
			goto bail;
		}
		fe = tmpfe;
	}

	/* should NEVER see am EXCLUSIVE on disk when creating lockres */
	/* indicates that a lock was held when the node died */
	if (lockres->lock_type == OCFS_DLM_EXCLUSIVE_LOCK &&
	    lockres->master_node_num == osb->node_num) {
		LOG_TRACE_ARGS("stale lock found! lockid=%u.%u\n", HILO(lock_id));
		lockres->lock_type = OCFS_DLM_NO_LOCK;
		DISK_LOCK_FILE_LOCK (fe) = OCFS_DLM_NO_LOCK;
		status = ocfs_write_force_disk (osb, fe, osb->sect_size, lock_id);
		if (status < 0) {
			LOG_ERROR_ARGS("error updating stale lockid=%u.%u\n",
				       HILO(lock_id));
		}
	}

bail:
	if (tmpfe)
		ocfs_release_file_entry(tmpfe);

	LOG_EXIT_STATUS(status);
	return status;
}

/*
 * ocfs_find_update_res()
 *
 * @osb: ocfs super block for the volume
 * @lock_id: sector number of the resource to be locked
 * @lockres: lockres of the resource
 * @fe: corresponding file entry
 * @updated: set to 1 if lockres is refreshed from disk
 *
 * Searches for the lockres for the given lockid in the hashtable.
 * If not found, it allocates a lockres for the lockid, and adds
 * it to the hashtable. If found and it's master node is not the
 * same as the current node, the lockres is refreshed from the disk.
 *
 * Returns 0 if success, < 0 if error.
 */
int ocfs_find_update_res (ocfs_super * osb, __u64 lock_id,
			  ocfs_lock_res ** lockres, ocfs_file_entry * fe,
			  __u32 * updated, __u32 timeout)
{
	int status = 0;
	ocfs_lock_res *tmp_lockres = NULL;

	LOG_ENTRY_ARGS ("(0x%p, %u.%u, 0x%p, 0x%p, 0x%p)\n", osb,
			HI (lock_id), LO (lock_id), lockres, fe,
			updated);

	status = ocfs_lookup_sector_node (osb, lock_id, lockres);
	if (status < 0) {
		*lockres = ocfs_allocate_lockres();
		if (*lockres == NULL) {
			LOG_ERROR_STATUS (status = -ENOMEM);
			goto finally;
		}

		ocfs_init_lockres (osb, *lockres, lock_id);

		ocfs_get_lockres (*lockres);

		status = ocfs_disk_update_resource (osb, *lockres, fe,
						    timeout);
		if (status < 0) {
			if (status != -ETIMEDOUT) {
				LOG_ERROR_STR ("Disabling Volume");
				osb->vol_state = VOLUME_DISABLED;
			}
			goto finally;
		}

		if (lock_id != (*lockres)->sector_num) {
			LOG_ERROR_ARGS ("lockid=%u.%u != secnum=%u.%u\n",
					HILO(lock_id),
					HILO((*lockres)->sector_num));
			status = -EFAIL;
			goto finally;
		}

		status = ocfs_check_for_stale_lock(osb, *lockres, fe, lock_id);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}
		
		if (updated)
			*updated = 1;

		status = ocfs_insert_sector_node (osb, *lockres, &tmp_lockres);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}

		if (!tmp_lockres)
			goto finally;
		else {
			ocfs_put_lockres (*lockres);
			*lockres = tmp_lockres;
		}
	}

	if (lock_id != (*lockres)->sector_num) {
		LOG_ERROR_ARGS ("lockid=%u.%u != secnum=%u.%u",
				HILO(lock_id),
				HILO((*lockres)->sector_num));
		status = -EFAIL;
		goto finally;
	}

	if ((*lockres)->master_node_num != osb->node_num) {
		status = ocfs_disk_update_resource (osb, *lockres, fe, timeout);
		if (status < 0) {
			if (status != -ETIMEDOUT) {
				LOG_ERROR_STR ("Disabling Volume");
				osb->vol_state = VOLUME_DISABLED;
			}
			goto finally;
		}
		if (updated)
			*updated = 1;
	}

      finally:
	if (status < 0)
		*lockres = NULL;
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_find_update_res */

#undef OCFS_DEBUG_CONTEXT
#define OCFS_DEBUG_CONTEXT      OCFS_DEBUG_CONTEXT_DLM
/*
 * ocfs_vote_for_del_ren()
 *
 * @osb:
 * @publish:
 * @node_num: node asking for the vote
 * @vote:
 * @lockres:
 *
 */
int ocfs_vote_for_del_ren (ocfs_super * osb, ocfs_publish * publish,
			   __u32 node_num, ocfs_vote * vote,
			   ocfs_lock_res ** lockres)
{
	int status = 0;
	__u8 voted;

	LOG_ENTRY ();

	voted = vote->vote[node_num];

	status = ocfs_common_del_ren (osb, publish->dir_ent, publish->vote_type,
				node_num, publish->publ_seq_num, &voted, lockres);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto bail;
	}

	vote->vote[node_num] = voted;

bail:
	LOG_EXIT_STATUS (status);
	return (status);
}				/* ocfs_vote_for_del_ren */

#ifndef USERSPACE_TOOL
/*
 * ocfs_get_inode_from_offset()
 *
 */
struct inode * ocfs_get_inode_from_offset(ocfs_super * osb, __u64 fileoff)
{
        int status;
        struct inode *inode = NULL;
        ocfs_file_entry *fe;
	ocfs_find_inode_args args;

	LOG_ENTRY ();
        
	status = ocfs_get_file_entry (osb, &fe, fileoff);
	if (status >= 0) {
		args.offset = fe->this_sector;
		args.entry = fe;
		inode = iget4 (osb->sb, (__u32) LO (fileoff),
			       (find_inode_t) ocfs_find_inode,
			       (void *) (&args));
                if (inode != NULL && is_bad_inode (inode)) {
		        iput (inode);
		        inode = NULL;
                }
		ocfs_release_file_entry (fe);
		fe = NULL;
	}

	LOG_EXIT_PTR (inode);
        return inode;
}				/* ocfs_get_inode_from_offset */
#endif

/*
 * ocfs_process_update_inode_request()
 *
 * @osb:
 * @lock_id:
 * @lockres:
 * @node_num: node asking for the vote
 *
 * get an inode just long enough to dump its pages
 */
int ocfs_process_update_inode_request (ocfs_super * osb, __u64 lock_id,
				       ocfs_lock_res * lockres, __u32 node_num)
{
	int status = 0;
#ifndef USERSPACE_TOOL
	struct inode *inode = NULL;
#endif

	LOG_ENTRY ();

	if (lockres && lockres->oin) {
		LOG_ERROR_STR ("should not be called if there exists an " \
			       "oin for this inode!\n");
		status = -EFAIL;
		goto bail;
	}

#ifndef USERSPACE_TOOL
	inode = ocfs_get_inode_from_offset(osb, lock_id);
	if (inode) {
		truncate_inode_pages (inode->i_mapping, 0);
		iput (inode);
		inode = NULL;
	}
#endif

	if (lockres)
		ocfs_remove_sector_node (osb, lockres);

      bail:
	LOG_EXIT ();
	return status;
}				/* ocfs_process_update_inode_request */


/*
 * ocfs_process_vote()
 *
 * @osb:
 * @publish:
 * @node_num: node asking for the vote
 *
 */
int ocfs_process_vote (ocfs_super * osb, ocfs_publish * publish, __u32 node_num)
{
	int status = 0;
	int tmpstat = 0;
	ocfs_lock_res *lockres = NULL;
	bool lockres_acq = false;
	__u32 flags;
	__u32 num_nodes;
	__u32 i;
	__u64 offset;
	ocfs_file_entry *fe = NULL;
	ocfs_vote *vote = NULL;
	ocfs_inode *oin = NULL;

	LOG_ENTRY_ARGS ("(0x%p, 0x%p, %u)\n", osb, publish, node_num);

	LOG_TRACE_ARGS ("node=%u, id=%u.%u, seq=%u.%u\n", node_num,
		HI(publish->dir_ent), LO(publish->dir_ent),
		HI(publish->publ_seq_num), LO(publish->publ_seq_num));

	num_nodes = OCFS_MAXIMUM_NODES;
	flags = publish->vote_type;

	offset = osb->vol_layout.vote_sect_off + (osb->node_num * osb->sect_size);
	status = ocfs_read_force_disk_ex (osb, (void **)&vote, osb->sect_size,
					  osb->sect_size, offset);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finito;
	}

	/* Exclusive vote for */
	status = ocfs_find_update_res (osb, publish->dir_ent, &lockres, NULL,
				       NULL, (OCFS_NM_HEARTBEAT_TIME/2));
	if (status < 0) {
		if (status == -ETIMEDOUT)
			goto finito;
		if (flags & FLAG_FILE_UPDATE_OIN) {
			status = ocfs_process_update_inode_request (osb,
					publish->dir_ent, lockres, node_num);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto finito;
			}
			vote->dir_ent = publish->dir_ent;
			vote->vote_seq_num = publish->publ_seq_num;
			vote->vote[node_num] = FLAG_VOTE_OIN_UPDATED;
		} else
			LOG_ERROR_STATUS (status);
		goto finito;
	}

	status = ocfs_acquire_lockres_ex (lockres, (OCFS_NM_HEARTBEAT_TIME/2));
	if (status < 0) {
		LOG_TRACE_ARGS ("Timedout locking lockres for id: %u.%u\n",
			       	HILO (lockres->sector_num));
		goto finally;
	} else
		lockres_acq = true;

	/* Zero out the vote for everybody, if any already set and hung */
	for (i = 0; i < num_nodes; i++)
		vote->vote[i] = 0;

	if ((flags & FLAG_FILE_DELETE) || (flags & FLAG_FILE_RENAME)) {
		status = ocfs_vote_for_del_ren (osb, publish, node_num, vote,
						&lockres);
		if (status < 0)
			LOG_ERROR_STATUS (status);
		goto finito;
	}

	if (flags & FLAG_FILE_RELEASE_CACHE) {
		ocfs_file_entry *tmp_fe = NULL;

		if (!osb->commit_cache_exec) {
			osb->needs_flush = true;
			ocfs_trans_in_progress(osb);
			if (osb->trans_in_progress == false) {
				osb->commit_cache_exec = true;
				ocfs_commit_cache (osb, true);
				osb->needs_flush = false;
				osb->commit_cache_exec = false;

				if (lockres->oin != NULL) {
					ocfs_flush_data (lockres->oin);
					lockres->lock_type = OCFS_DLM_NO_LOCK;
				}

				status = ocfs_get_file_entry (osb, &tmp_fe,
							      publish->dir_ent);
				if (status < 0) {
					LOG_ERROR_STATUS (status);
					goto finito;
				}

				/* At this stage there is nothing in disk, so */
				/* no need to update cache, as there is */
				/* nothing there */
				if (DISK_LOCK_FILE_LOCK (tmp_fe) > OCFS_DLM_NO_LOCK) {
					__u64 tmp = publish->dir_ent;

					DISK_LOCK_FILE_LOCK (tmp_fe) = OCFS_DLM_NO_LOCK;

					status = ocfs_write_force_disk (osb, tmp_fe,
									osb->sect_size, tmp);
					if (status < 0) {
						LOG_ERROR_STATUS (status);
						goto finito;
					}
					lockres->lock_type = OCFS_DLM_NO_LOCK;
				}
				ocfs_release_file_entry (tmp_fe);
				vote->vote[node_num] = FLAG_VOTE_NODE;
			} else {
				/* Ask for a retry as txn is in progress */
				vote->vote[node_num] = FLAG_VOTE_UPDATE_RETRY;
				vote->open_handle = false;
			}
			goto finito;
		}
	}

	if (publish->vote_type & FLAG_FILE_UPDATE_OIN) {
		oin = lockres->oin;
		if (oin) {
			OCFS_ASSERT(IS_VALID_OIN(oin));
			ocfs_down_sem (&(oin->main_res), true);
			oin->needs_verification = true;
			tmpstat = ocfs_verify_update_oin(osb, oin);
			if (tmpstat < 0)
				LOG_ERROR_STATUS (tmpstat);
			ocfs_up_sem (&(oin->main_res));

			vote->dir_ent = publish->dir_ent;
			vote->vote_seq_num = publish->publ_seq_num;
			vote->vote[node_num] = FLAG_VOTE_OIN_UPDATED;
		} else {
			status = ocfs_process_update_inode_request (osb,
					publish->dir_ent, lockres, node_num);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto finito;
			}
			vote->dir_ent = publish->dir_ent;
			vote->vote_seq_num = publish->publ_seq_num;
			vote->vote[node_num] = FLAG_VOTE_OIN_UPDATED;
		}
		goto finito;
	}

	if (lockres->master_node_num != OCFS_INVALID_NODE_NUM) {
		if (lockres->master_node_num == osb->node_num) {
			/* I am currently the master of the lock */
			if (flags & FLAG_CHANGE_MASTER) {
				osb->needs_flush = true;
				ocfs_trans_in_progress(osb);
				if (lockres->lock_type == OCFS_DLM_NO_LOCK) {
					__u64 tmp = publish->dir_ent;

					ocfs_commit_cache (osb, true);
					lockres->master_node_num = node_num;
					osb->needs_flush = false;

					if (lockres->oin != NULL) {
						ocfs_flush_data (lockres->oin);
						lockres->lock_type = OCFS_DLM_NO_LOCK;
					}

					status = ocfs_get_file_entry (osb, &fe,
								      publish->dir_ent);
					if (status < 0) {
						LOG_ERROR_STATUS (status);
						goto finito;
					}

					if (lockres->oin)
						DISK_LOCK_OIN_MAP (fe) |= (1 << osb->node_num);

					DISK_LOCK_CURRENT_MASTER (fe) = node_num;

					/* Write new master on the disk */
					status = ocfs_write_disk (osb, fe, osb->sect_size, tmp);
					if (status < 0) {
						LOG_ERROR_STATUS (status);
						goto finito;
					}
					lockres->master_node_num = node_num;
					vote->vote[node_num] = FLAG_VOTE_NODE;
				} else {
					/* Ask for a retry as txn is in progress */
					vote->vote[node_num] = FLAG_VOTE_UPDATE_RETRY;
					vote->open_handle = false;
				}
			} else if (flags & FLAG_ADD_OIN_MAP) {
				status = ocfs_get_file_entry (osb, &fe, publish->dir_ent);
				if (status < 0) {
					LOG_ERROR_STATUS (status);
					goto finito;
				}

				if (fe->attribs & OCFS_ATTRIB_DIRECTORY) {
					LOG_TRACE_STR("stale lock probe on directory!, respond but do nothing");
					vote->vote[node_num] = FLAG_VOTE_NODE;	
				} else if (IS_FE_DELETED(fe->sync_flags) ||
					   (!(fe->sync_flags & OCFS_SYNC_FLAG_VALID))) {
					vote->vote[node_num] = FLAG_VOTE_FILE_DEL;
					vote->open_handle = false;
				} else {
					__u64 tmp = publish->dir_ent;

					DISK_LOCK_OIN_MAP (fe) |= (1 << node_num);

					/* Write new map on the disk */
					status = ocfs_write_disk (osb, fe,
							osb->sect_size, tmp);
					if (status < 0) {
						LOG_ERROR_STATUS (status);
						goto finito;
					}

					/* Add this node to the oin map on the file entry */
					lockres->oin_openmap = DISK_LOCK_OIN_MAP (fe);
					vote->vote[node_num] = FLAG_VOTE_NODE;
				}
			}
		} else {
			/* I am not currently the master of the lock */
			if (IS_NODE_ALIVE (osb->publ_map, lockres->master_node_num,
					   OCFS_MAXIMUM_NODES)) {
				 /* We have no business voting on this lock */
				vote->vote[node_num] = FLAG_VOTE_UPDATE_RETRY;
				vote->open_handle = false;
			} else {
				/* Master Node is dead and a vote is needed */
				/* to create a new master */
				vote->open_handle = false;
				vote->vote[node_num] = FLAG_VOTE_NODE;

				if ((!(flags & FLAG_DIR)) &&
				    ((flags & FLAG_FILE_EXTEND) || (flags & FLAG_FILE_UPDATE))) {
					if (lockres->oin)
						vote->open_handle = true;
				}
			}
		}
	} else {
		/* Vote for the node */
		vote->vote[node_num] = FLAG_VOTE_NODE;
		vote->open_handle = false;

		if ((!(flags & FLAG_DIR)) &&
		    ((flags & FLAG_FILE_EXTEND) || (flags & FLAG_FILE_UPDATE))) {
			if (lockres->oin)
				vote->open_handle = true;
		}
	}

      finito:
	vote->dir_ent = publish->dir_ent;
	vote->vote_seq_num = publish->publ_seq_num;

	if (status >= 0) {
		offset = osb->vol_layout.vote_sect_off +
			 (osb->node_num * osb->sect_size);
		status = ocfs_write_disk (osb, vote, osb->sect_size, offset);
		if (status < 0)
			LOG_ERROR_STATUS (status);
		else {
			ocfs_compute_dlm_stats (0, vote->vote[node_num],
					       	&(OcfsGlobalCtxt.dsk_reply_stats));
			ocfs_compute_dlm_stats (0, vote->vote[node_num],
					       	&(osb->dsk_reply_stats));
			LOG_TRACE_ARGS ("disk reply id=%u.%u, seq=%u.%u, "
				"node=%u, vote=0x%x, status=%d\n",
				HILO(publish->dir_ent), HILO(publish->publ_seq_num),
			       	node_num, vote->vote[node_num], status);
		}
	}

finally:
	ocfs_release_file_entry (fe);

	ocfs_safefree (vote);

	if (lockres && lockres_acq)
		ocfs_release_lockres (lockres);

	ocfs_put_lockres(lockres);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_process_vote */


/*
 * ocfs_common_del_ren()
 *
 */
int ocfs_common_del_ren (ocfs_super * osb, __u64 lock_id, __u32 flags,
			 __u32 node_num, __u64 seq_num, __u8 * vote,
			 ocfs_lock_res ** lockres)
{
	int status = 0;
	__u32 retry_cnt = 0;
	bool acq_oin = false;
	ocfs_file_entry *fe = NULL;
	bool rls_oin = true;
	ocfs_inode *oin = NULL;
	ocfs_sem *oin_sem = NULL;

	LOG_ENTRY ();

	oin = (*lockres)->oin;
	if (oin) {
		ocfs_down_sem (&oin->main_res, true);
		oin->needs_verification = true;
		status = ocfs_verify_update_oin(osb, oin);
		if (status < 0) {
			if (status == -ENOENT) {
				*vote = FLAG_VOTE_NODE;
				status = 0;
			} else
				LOG_ERROR_STATUS (status);
			ocfs_up_sem (&oin->main_res);
			goto finally;
		}
		ocfs_up_sem (&oin->main_res);
	}

	/* Check for oin */
	if (oin) {
		oin_sem = &(oin->main_res);
		ocfs_down_sem (oin_sem, true);
		acq_oin = true;

		/* If OIN_IN_USE is set we should go back and retry */
		while ((oin->oin_flags & OCFS_OIN_IN_USE) && (retry_cnt < 5)) {
			if (acq_oin) {
				ocfs_up_sem (oin_sem);
				acq_oin = false;
			}

			ocfs_sleep (20);
			retry_cnt++;

			if (!acq_oin) {
				ocfs_down_sem (oin_sem, true);
				acq_oin = true;
			}
		}

/* The macro below takes into account the pre RELEASE/ACQUIRE_LOCK flag days */
/* Allows for rolling upgrade */
#define IS_RELEASE_LOCK(_f)	(((_f) & FLAG_FILE_RELEASE_LOCK) ||	\
				 !((_f) & (FLAG_FILE_ACQUIRE_LOCK | FLAG_FILE_RELEASE_LOCK)))

		if (((*lockres)->oin->open_hndl_cnt == 0) &&
		    (!(oin->oin_flags & OCFS_OIN_IN_USE))) {
			if (!(oin->oin_flags & OCFS_OIN_IN_TEARDOWN) &&
			    IS_RELEASE_LOCK(flags)) {
				if (acq_oin) {
					ocfs_up_sem (oin_sem);
					acq_oin = false;
				}

				rls_oin = false;

				if (!acq_oin) {
					ocfs_down_sem (oin_sem, true);
					acq_oin = true;
				}

				OCFS_SET_FLAG (oin->oin_flags,
					       OCFS_OIN_NEEDS_DELETION);

				if (acq_oin) {
					ocfs_up_sem (oin_sem);
					acq_oin = false;
				}

				ocfs_release_lockres (*lockres);

				if (oin && oin->inode) {
					struct inode *inode = oin->inode;
					inode->i_nlink = 0;
					d_prune_aliases (inode);
				}

				if (rls_oin) {
					ocfs_release_cached_oin (osb, oin);
					ocfs_release_oin (oin, true);
				} else {
					ocfs_down_sem (&(oin->paging_io_res),
						       true);
					ocfs_purge_cache_section (oin, NULL, 0);
					ocfs_up_sem (&(oin->paging_io_res));
				}

				if (oin && oin->inode) {
#ifndef USERSPACE_TOOL
					iput (oin->inode);
#endif
					oin->inode = NULL;
				}
				*lockres = NULL;
			}
			*vote = FLAG_VOTE_NODE;
			goto finito;
		} else {
			*vote = FLAG_VOTE_OIN_ALREADY_INUSE;
			goto finito;
		}
	} else {
#ifndef USERSPACE_TOOL
                struct inode *inode = NULL;
                if (flags & FLAG_FILE_DELETE && IS_RELEASE_LOCK(flags)) {
                        inode = ocfs_get_inode_from_offset(osb, lock_id);
                        if (inode) {
				inode->i_nlink = 0;
				d_prune_aliases (inode);
	                        iput (inode);
	                        inode = NULL;
                        }
                }
#endif
		*vote = FLAG_VOTE_NODE;
		goto finito;
	}

finito:
	if (*lockres) {
		(*lockres)->lock_state |= FLAG_ALWAYS_UPDATE_OPEN;
		(*lockres)->last_upd_seq_num = seq_num;

		if ((*lockres)->master_node_num != OCFS_INVALID_NODE_NUM) {
			if (!IS_NODE_ALIVE (osb->publ_map, (*lockres)->master_node_num,
					    OCFS_MAXIMUM_NODES)) {
				(*lockres)->master_node_num = node_num;
			}
		} else {
			(*lockres)->master_node_num = node_num;
		}

		/* Change the master if there is no lock */
		if (((*lockres)->master_node_num == osb->node_num) &&
		    ((*lockres)->lock_state <= OCFS_DLM_SHARED_LOCK)) {

			status = ocfs_get_file_entry (osb, &fe, lock_id);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto finally;
			}

			DISK_LOCK_CURRENT_MASTER (fe) = node_num;

			status = ocfs_write_disk (osb, fe, osb->sect_size, lock_id);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto finally;
			}
			(*lockres)->master_node_num = node_num;
		}
	}

finally:
	ocfs_release_file_entry (fe);

	if (acq_oin && oin_sem)
		ocfs_up_sem (oin_sem);

	LOG_EXIT_STATUS (status);
	return (status);
}				/* ocfs_common_del_ren */
