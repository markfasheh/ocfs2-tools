/*
 * ocfsgendlm.c
 *
 * Distributed lock manager. Requests and processes lock votes.
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
extern __u32 comm_voting;
#else
#include <libocfs.h>
#endif

#define WAIT_FOR_VOTE_INCREMENT  200

/* Tracing */
#define OCFS_DEBUG_CONTEXT OCFS_DEBUG_CONTEXT_DLM

/*
 * ocfs_insert_cache_link()
 *
 */
int ocfs_insert_cache_link (ocfs_super * osb, ocfs_lock_res * lockres)
{
	int status = 0;

	LOG_ENTRY ();

	lockres->in_cache_list = true;

	list_add_tail (&(lockres->cache_list), &(osb->cache_lock_list));

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_insert_cache_link */

/*
 * ocfs_update_lock_state()
 *
 */
int ocfs_update_lock_state (ocfs_super * osb, ocfs_lock_res * lockres,
			    __u32 flags, bool *disk_vote)
{
	__u32 votemap;
	int status = 0;
	int tmpstat;
	__u64 lockseqno = 0;
	unsigned long jif;

	LOG_ENTRY_ARGS ("(0x%p, 0x%p, %u)\n", osb, lockres, flags);

	ocfs_acquire_lockres (lockres);
	votemap = (1 << lockres->master_node_num);
	flags |= FLAG_FILE_ACQUIRE_LOCK;

	ocfs_compute_lock_type_stats (&(osb->lock_type_stats),
				      OCFS_UPDATE_LOCK_STATE);

#ifndef USERSPACE_TOOL
	if (comm_voting && !*disk_vote) {
		LOG_TRACE_STR ("Network vote");
		jif = jiffies;
		status = ocfs_send_dlm_request_msg (osb, lockres->sector_num,
				lockres->lock_type, flags, lockres, votemap);
		if (status >= 0) {
			status = lockres->vote_status;
			if (status >= 0)
				goto vote_success;
			else
				goto finito;
		} else if (status == -ETIMEDOUT) {
			LOG_TRACE_STR ("Network voting timed out");
		}
		lockres->vote_state = 0;
	}
#endif

	LOG_TRACE_STR ("Disk vote");
	*disk_vote = true;
	jif = jiffies;
	status = ocfs_request_vote (osb, lockres->sector_num,
			lockres->lock_type, flags, votemap, &lockseqno);
	if (status < 0) {
		if (status != -EAGAIN)
			LOG_ERROR_STATUS (status);
		goto finito;
	}

	status = ocfs_wait_for_vote (osb, lockres->sector_num,
				     lockres->lock_type, flags, votemap, 5000,
				     lockseqno, lockres);
	if (status < 0) {
		if (status != -EAGAIN)
			LOG_ERROR_STATUS (status);
		goto finito;
	}

#ifndef USERSPACE_TOOL
      vote_success:
#endif
	jif = jiffies - jif;
	LOG_TRACE_ARGS ("Lock time: %u\n", jif);

	if (flags & FLAG_CHANGE_MASTER)
		lockres->master_node_num = osb->node_num;

      finito:
	if (*disk_vote) {
		tmpstat = ocfs_reset_voting (osb, lockres->sector_num,
					     lockres->lock_type, votemap);
		if (tmpstat < 0)
			LOG_ERROR_STATUS (tmpstat);
	}
	ocfs_release_lockres (lockres);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_update_lock_state */

/*
 * ocfs_disk_request_vote()
 *
 */
int ocfs_disk_request_vote (ocfs_super * osb, __u64 lock_id, __u32 lock_type,
			    __u32 flags, __u64 vote_map, __u64 * lock_seq_num)
{
	int status = 0;
	__u64 offset = 0;
	__u64 pub_off;
	__u32 size = 0;
	__u32 numnodes = 0;
	__u32 i;
	__u8 *buffer = NULL;
	ocfs_publish *pubsect = NULL;
	__u64 largestseqno = 0;
	__u64 pubmap = 0;
	__u8 *p;
	__u32 wait;
	bool publish_flag = false;
	__u32 disk_hb = osb->vol_layout.disk_hb;

	LOG_ENTRY_ARGS ("(osb=0x%p, id=%u.%u, ty=%u, fl=%u, vm=0x%08x)\n", osb,
		 HI(lock_id), LO(lock_id), lock_type, flags, LO(vote_map));

	LOG_TRACE_ARGS ("osb=0x%p, id=%u.%u, ty=%u, fl=%u, vm=0x%08x\n",
		osb, HI(lock_id), LO(lock_id), lock_type, flags, LO(vote_map));

	pubmap = osb->publ_map;
	offset = osb->vol_layout.publ_sect_off;
	numnodes = OCFS_MAXIMUM_NODES;
	size = (numnodes * osb->sect_size);

	/* take lock to prevent overwrites by vote_reset and nm thread */
	down_with_flag (&(osb->publish_lock), publish_flag);

	/* Read the Publish Sector of all nodes */
	status = ocfs_read_disk_ex (osb, (void **)&buffer, size, size, offset);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	for (i = 0, p = buffer; i < numnodes; i++, p += osb->sect_size) {
		pubsect = (ocfs_publish *) p;
		if (pubsect->time == (__u64) 0)
			continue;
		if (pubsect->publ_seq_num <= largestseqno)
			continue;
		largestseqno = pubsect->publ_seq_num;
		if (pubsect->dirty) {
			up_with_flag (&(osb->publish_lock), publish_flag);
			if (!IS_NODE_ALIVE (pubmap, i, numnodes)) {
				LOG_TRACE_ARGS ("ocfs_recover_vol(%u)\n", i);
				ocfs_recover_vol (osb, i);
			} else {
				get_random_bytes(&wait, sizeof(wait));
				wait %= 200;
				wait += disk_hb;
				LOG_TRACE_ARGS ("wait: %d\n", wait);
				ocfs_sleep (wait);
			}
			status = -EAGAIN;
			goto finally;
		}
	}

	/* Increment the largest sequence number by one & */
	/* write it in its own Publish Sector and set the Dirty Bit */
	pubsect = (ocfs_publish *) (buffer + (osb->node_num * osb->sect_size));
	largestseqno++;
	LOG_TRACE_ARGS ("largestseqno : %u.%u\n", HI(largestseqno), LO(largestseqno));
	pubsect->publ_seq_num = largestseqno;
	pubsect->dirty = true;
	pubsect->vote = FLAG_VOTE_NODE;
	pubsect->vote_map = vote_map;
	pubsect->vote_type = flags;
	pubsect->dir_ent = lock_id;

	pub_off = osb->vol_layout.publ_sect_off +
			(osb->node_num * osb->sect_size);

	status = ocfs_write_disk (osb, pubsect, osb->sect_size, pub_off);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	atomic_set (&osb->node_req_vote, 1);

	*lock_seq_num = largestseqno;

      finally:
	up_with_flag (&(osb->publish_lock), publish_flag);
	ocfs_safefree (buffer);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_disk_request_vote */

/*
 * ocfs_wait_for_disk_lock_release()
 *
 * @osb: ocfs super block for the volume
 * @offset:
 * @time_to_wait:
 * @lock_type: lowest level to which a lock must deprecate for us to break out.
 *
 * Returns 0 of success, < 0 if error.
 */
int ocfs_wait_for_disk_lock_release (ocfs_super * osb, __u64 offset,
				     __u32 time_to_wait, __u32 lock_type)
{
	int status = -ETIMEDOUT;
	int tmpstat = -ETIMEDOUT;
	__u32 timewaited = 0;
	ocfs_file_entry *fe = NULL;

	LOG_ENTRY ();

	/* Create a sepearate thread which should  set the event of the */
	/* resource after N retries. */

	fe = ocfs_allocate_file_entry ();
	if (fe == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto finally;
	}

	while (time_to_wait > timewaited) {
		memset (fe, 0, sizeof (ocfs_file_entry));
		tmpstat = ocfs_read_force_disk (osb, fe, osb->sect_size, offset);
		if (tmpstat < 0) {
			LOG_ERROR_STATUS (status = tmpstat);
			goto finally;
		}

		/* This will always be zero when the first Node comes up after reboot */
		/* (for volume lock) */
		if ((DISK_LOCK_CURRENT_MASTER (fe) == OCFS_INVALID_NODE_NUM) ||
		    (DISK_LOCK_CURRENT_MASTER (fe) == osb->node_num)) {
			status = 0;
			goto finally;
		}

		if (!IS_NODE_ALIVE (osb->publ_map,
				    DISK_LOCK_CURRENT_MASTER (fe),
				    OCFS_MAXIMUM_NODES)) {
//			LOG_TRACE_ARGS ("ocfs_recover_vol(%u)\n",
//					DISK_LOCK_CURRENT_MASTER (fe));
//			ocfs_recover_vol(osb, DISK_LOCK_CURRENT_MASTER(fe));

			/* Reset the lock as not owned and return success?? */
			/* This needs to be under some sort of cluster wide lock */
			DISK_LOCK_CURRENT_MASTER (fe) = OCFS_INVALID_NODE_NUM;
			DISK_LOCK_FILE_LOCK (fe) = OCFS_DLM_NO_LOCK;
			status = 0;
			goto finally;
		}

		/* If we are here in the code it means the local node is not the master */
		if (DISK_LOCK_FILE_LOCK (fe) <= lock_type) {
			status = 0;
			goto finally;
		} else
			ocfs_sleep (WAIT_FOR_VOTE_INCREMENT);
		timewaited += WAIT_FOR_VOTE_INCREMENT;
	}

      finally:
	ocfs_release_file_entry (fe);
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_wait_for_disk_lock_release */

/*
 * ocfs_wait_for_lock_release()
 *
 */
int ocfs_wait_for_lock_release (ocfs_super * osb, __u64 offset, __u32 time_to_wait,
				ocfs_lock_res * lockres, __u32 lock_type)
{
	int status = -ETIMEDOUT;
	int tmpstat = -ETIMEDOUT;
	__u32 timewaited = 0;
	ocfs_file_entry *fe = NULL;
	__u32 length = 0;

	LOG_ENTRY_ARGS ("(0x%p, %u.%u, %u, 0x%p, %u)\n", osb,
			HI (offset), LO (offset), time_to_wait,
			lockres, lock_type);

	fe = ocfs_allocate_file_entry ();
	if (fe == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto finally;
	}

	while (time_to_wait > timewaited) {
		memset (fe, 0, sizeof (ocfs_file_entry));
		length = osb->sect_size;
		tmpstat = ocfs_read_disk (osb, fe, length, offset);
		if (tmpstat < 0) {
			LOG_ERROR_STATUS (status = tmpstat);
			goto finally;
		}

		if ((DISK_LOCK_CURRENT_MASTER (fe) == OCFS_INVALID_NODE_NUM) ||
		    (DISK_LOCK_CURRENT_MASTER (fe) == osb->node_num)) {
			status = 0;
			goto finally;
		}

		if (!IS_NODE_ALIVE (osb->publ_map, DISK_LOCK_CURRENT_MASTER(fe),
				    OCFS_MAXIMUM_NODES)) {
//			LOG_ERROR_ARGS ("ocfs_recover_vol(%u)",
//					DISK_LOCK_CURRENT_MASTER (fe));
//			ocfs_recover_vol(osb, DISK_LOCK_CURRENT_MASTER(fe));

			/* Reset the lock as not owned and return success?? */
			/* This needs to be under some sort of cluster wide lock, */
			DISK_LOCK_CURRENT_MASTER (fe) = OCFS_INVALID_NODE_NUM;
			DISK_LOCK_FILE_LOCK (fe) = OCFS_DLM_NO_LOCK;
			status = 0;
			goto finally;
		}

		/* The local node is not the master */
		if (DISK_LOCK_FILE_LOCK (fe) >= OCFS_DLM_ENABLE_CACHE_LOCK) {
			lockres->lock_type = DISK_LOCK_FILE_LOCK (fe);
			lockres->master_node_num = DISK_LOCK_CURRENT_MASTER (fe);
			status = ocfs_break_cache_lock (osb, lockres, fe);
			if (status < 0) {
				if (status != -EINTR)
					LOG_ERROR_STATUS (status);
				goto finally;
			}
			DISK_LOCK_FILE_LOCK (fe) = lockres->lock_type;
			DISK_LOCK_CURRENT_MASTER (fe) = lockres->master_node_num;
		}

		if (DISK_LOCK_FILE_LOCK (fe) <= lock_type) {
			status = 0;
			goto finally;
		} else
			ocfs_sleep (WAIT_FOR_VOTE_INCREMENT);
		timewaited += WAIT_FOR_VOTE_INCREMENT;
	}

	LOG_TRACE_ARGS("probing the node %d for possible stale lock, lockid=%u.%u\n",
		       lockres->master_node_num, HILO (lockres->sector_num));

	status = ocfs_update_master_on_open (osb, lockres);
	if (status >= 0) {
		tmpstat = ocfs_read_disk (osb, fe, osb->sect_size, offset);
		if (tmpstat < 0) {
			LOG_ERROR_STATUS(tmpstat);
			status = tmpstat;
		}
		if (DISK_LOCK_FILE_LOCK(fe) < lockres->lock_type) {
			LOG_TRACE_STR("stale lock was found and corrected!");
		}
	}

      finally:
	if (status == -ETIMEDOUT) {
		LOG_ERROR_ARGS ("WARNING: timeout lockid=%u.%u, master=%u, "
				"type=%u\n", HILO (lockres->sector_num),
			       	lockres->master_node_num, lockres->lock_type);
	}

	if (lockres && status >= 0) {
		ocfs_acquire_lockres (lockres);
		lockres->lock_type = DISK_LOCK_FILE_LOCK (fe);
		lockres->master_node_num = DISK_LOCK_CURRENT_MASTER (fe);
		lockres->oin_openmap = DISK_LOCK_OIN_MAP (fe);
		lockres->last_lock_upd = DISK_LOCK_LAST_WRITE (fe);
		lockres->lock_type = DISK_LOCK_FILE_LOCK(fe);
		ocfs_release_lockres (lockres);
	}

	ocfs_release_file_entry (fe);
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_wait_for_lock_release */

/*
 * ocfs_get_vote_on_disk()
 *
 */
int ocfs_get_vote_on_disk (ocfs_super * osb, __u64 lock_id, __u32 lock_type,
			   __u32 flags, __u64 * got_vote_map, __u64 vote_map,
			   __u64 lock_seq_num, __u64 * oin_open_map)
{
	int status = 0;
	__u32 length = 0;
	__u32 i;
	__u32 numnodes;
	__u8 *buffer = NULL;
	ocfs_vote *vote;
	__u8 *p;

	LOG_ENTRY_ARGS ("(lockid=%u.%u, locktype=%u, votemap=0x%08x)\n",
			HI (lock_id), LO (lock_id), lock_type, LO (vote_map));

	numnodes = OCFS_MAXIMUM_NODES;

	/* Read the vote sectors of all the nodes */
	length = numnodes * osb->sect_size;
	status = ocfs_read_disk_ex (osb, (void **)&buffer, length, length,
				    osb->vol_layout.vote_sect_off);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	for (i = 0, p = buffer; i < numnodes; i++, p += osb->sect_size) {
		vote = (ocfs_vote *) p;

		/* A node we were asking to vote is dead */
		if ((IS_NODE_ALIVE (vote_map, i, numnodes)) &&
		    !(IS_NODE_ALIVE (osb->publ_map, i, numnodes))) {
			if (flags & FLAG_FILE_UPDATE_OIN) {
				(*got_vote_map) |= 1 << i;
			} else {
				status = -EAGAIN;
				goto finally;
			}
		}

		if (!IS_NODE_ALIVE (vote_map, i, numnodes) ||
		    !IS_NODE_ALIVE (osb->publ_map, i, numnodes) ||
		    vote->vote_seq_num != lock_seq_num ||
		    vote->dir_ent != lock_id)
			continue;

		/* A node we were asking to vote is alive */
		if (vote->vote[osb->node_num] == FLAG_VOTE_NODE) {
			(*got_vote_map) |= 1 << i;
			if (flags & FLAG_FILE_EXTEND || flags & FLAG_FILE_UPDATE) {
				(*oin_open_map) |= (vote->open_handle << i);
			}
		} else if (vote->vote[osb->node_num] == FLAG_VOTE_OIN_ALREADY_INUSE) {
			(*got_vote_map) |= 1 << i;
			status = -EFAIL;
			if (flags & FLAG_FILE_DELETE) {
				status = -EBUSY;
			}
			goto finally;
		} else if (vote->vote[osb->node_num] == FLAG_VOTE_OIN_UPDATED) {
			(*got_vote_map) |= 1 << i;
		} else if (vote->vote[osb->node_num] == FLAG_VOTE_UPDATE_RETRY) {
			status = -EAGAIN;
			goto finally;
		} else if (vote->vote[osb->node_num] == FLAG_VOTE_FILE_DEL) {
			status = -ENOENT;
			goto finally;
		}
	}

      finally:
	ocfs_safefree (buffer);
	LOG_EXIT_STATUS (status);
	return (status);
}				/* ocfs_get_vote_on_disk */

/*
 * ocfs_disk_reset_voting()
 *
 */
int ocfs_disk_reset_voting (ocfs_super * osb, __u64 lock_id, __u32 lock_type)
{
	int status = 0;
	ocfs_publish *pubsect = NULL;
	__u64 offset = 0;

	LOG_ENTRY_ARGS ("(0x%p, %u.%u, %u)\n", osb, HI (lock_id),
			LO (lock_id), lock_type);

	/* take lock to prevent publish overwrites by vote_req and nm thread */
	down (&(osb->publish_lock));

	/* Read node's publish sector */
	offset = osb->vol_layout.publ_sect_off +
		 (osb->node_num * osb->sect_size);
	status = ocfs_read_disk_ex (osb, (void *)&pubsect, osb->sect_size,
				    osb->sect_size, offset);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	pubsect->dirty = false;
	pubsect->vote = 0;
	pubsect->vote_type = 0;
	pubsect->vote_map = 0;
	pubsect->dir_ent = 0;

	/* Write it back */
	status = ocfs_write_disk (osb, pubsect, osb->sect_size, offset);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	atomic_set (&osb->node_req_vote, 0);

      finally:
	up (&(osb->publish_lock));
	ocfs_safefree (pubsect);
	LOG_EXIT_STATUS (status);
	return (status);
}				/* ocfs_disk_reset_voting */

/*
 * ocfs_wait_for_vote()
 *
 */
int ocfs_wait_for_vote (ocfs_super * osb, __u64 lock_id, __u32 lock_type, __u32 flags,
			__u64 vote_map, __u32 time_to_wait, __u64 lock_seq_num,
			ocfs_lock_res * lockres)
{
	int status = -EAGAIN;
	__u32 timewaited = 0;
	__u64 gotvotemap = 0;
	__u64 fileopenmap = 0;

	LOG_ENTRY_ARGS ("(osb=0x%p, id=%u.%u, type=%u, flg=%u, map=0x%x, "
		       	"seq=%u.%u)\n", osb, HILO (lock_id), lock_type,
		       	flags, LO (vote_map), HILO (lock_seq_num));

	while (time_to_wait > timewaited) {
		ocfs_sleep (WAIT_FOR_VOTE_INCREMENT);

		if (!atomic_read (&osb->node_req_vote)) {
			status = -EAGAIN;
			goto bail;
		}

		status = ocfs_get_vote_on_disk (osb, lock_id, lock_type, flags,
				&gotvotemap, vote_map, lock_seq_num, &fileopenmap);
		if (status < 0) {
			if (status != -EAGAIN)
				LOG_ERROR_STATUS (status);
			goto bail;
		}

		if (vote_map == gotvotemap) {
			if ((flags & FLAG_FILE_EXTEND) || (flags & FLAG_FILE_UPDATE))
				lockres->oin_openmap = fileopenmap;
			status = 0;
			goto bail;
		}
		timewaited += WAIT_FOR_VOTE_INCREMENT;
	}

      bail:
	ocfs_compute_dlm_stats ((timewaited >= time_to_wait ? -ETIMEDOUT : 0),
			       	status, &(OcfsGlobalCtxt.dsk_reqst_stats));
	ocfs_compute_dlm_stats ((timewaited >= time_to_wait ? -ETIMEDOUT : 0),
			       	status, &(osb->dsk_reqst_stats));

	LOG_TRACE_ARGS ("disk vote id=%u.%u, seq=%u.%u, map=0x%x, "
		       	"flags=0x%x, type=0x%x, status=%d, timeo=%d\n",
		       	HILO(lock_id), HILO(lock_seq_num), LO(vote_map),
		       	flags, lock_type, status,
			(timewaited >= time_to_wait ? -ETIMEDOUT : 0));

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_wait_for_vote */

/*
 * ocfs_reset_voting()
 *
 */
int ocfs_reset_voting (ocfs_super * osb, __u64 lock_id, __u32 lock_type,
		       __u64 vote_map)
{
	int status;

	LOG_ENTRY ();

	status = ocfs_disk_reset_voting (osb, lock_id, lock_type);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_reset_voting */

/*
 * ocfs_request_vote()
 *
 */
int ocfs_request_vote (ocfs_super * osb, __u64 lock_id, __u32 lock_type, __u32 flags,
		       __u64 vote_map, __u64 * lock_seq_num)
{
	int status;

	LOG_ENTRY ();

	status = ocfs_disk_request_vote (osb, lock_id, lock_type, flags,
					 vote_map, lock_seq_num);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_request_vote */

#ifndef USERSPACE_TOOL
/*
 * ocfs_init_dlm_msg()
 *
 */
void ocfs_init_dlm_msg (ocfs_super * osb, ocfs_dlm_msg * dlm_msg, __u32 msg_len)
{
	LOG_ENTRY ();

	dlm_msg->magic = OCFS_DLM_MSG_MAGIC;
	dlm_msg->msg_len = msg_len;

	memcpy (dlm_msg->vol_id, osb->vol_layout.vol_id, MAX_VOL_ID_LENGTH);

	dlm_msg->src_node = osb->node_num;

	LOG_EXIT ();
	return;
}				/* ocfs_init_dlm_msg */

/*
 * ocfs_send_dlm_request_msg()
 *
 */
int ocfs_send_dlm_request_msg (ocfs_super * osb, __u64 lock_id, __u32 lock_type,
			       __u32 flags, ocfs_lock_res * lockres,
			       __u64 vote_map)
{
	int status = 0;
	ocfs_dlm_msg *dlm_msg = NULL;
	__u32 msg_len;
	ocfs_dlm_msg_hdr *req;

	LOG_ENTRY_ARGS ("(osb=0x%p, id:%u.%u, ty=%u, fl=%u, vm=0x%08x)\n", osb,
			HI(lock_id), LO(lock_id), lock_type, flags, LO(vote_map));

	msg_len = sizeof (ocfs_dlm_msg) - 1 + sizeof (ocfs_dlm_req_master);

	dlm_msg = ocfs_malloc (msg_len);
	if (dlm_msg == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto finally;
	}

	ocfs_acquire_lockres (lockres);
	lockres->vote_state = LOCK_STATE_IN_VOTING;
	lockres->req_vote_map = vote_map;
	lockres->got_vote_map = 0;
	lockres->tmp_openmap = 0;
	spin_lock (&OcfsGlobalCtxt.comm_seq_lock);
	OcfsGlobalCtxt.comm_seq_num++;
	lockres->last_upd_seq_num = OcfsGlobalCtxt.comm_seq_num;
	spin_unlock (&OcfsGlobalCtxt.comm_seq_lock);
	ocfs_release_lockres (lockres);

	ocfs_init_dlm_msg (osb, dlm_msg, msg_len);

	dlm_msg->msg_type = OCFS_VOTE_REQUEST;

	req = (ocfs_dlm_msg_hdr *) dlm_msg->msg_buf;
	req->lock_id = lock_id;
	req->flags = flags;
	req->lock_seq_num = lockres->last_upd_seq_num;

	LOG_TRACE_ARGS ("ocfs: vote request lockid=%u.%u, seq=%u.%u, map=0x%08x\n",
	       HILO(req->lock_id), HILO(req->lock_seq_num), LO(vote_map));

	ocfs_send_bcast (osb, vote_map, dlm_msg);
	status = ocfs_wait (lockres->voted_event,
			    atomic_read (&lockres->voted_event_woken), 3000);
	atomic_set (&lockres->voted_event_woken, 0);
 
	if (status == -ETIMEDOUT) {
		LOG_TRACE_ARGS ("timedout seq=%u.%u\n", HILO(req->lock_seq_num));
	}

	ocfs_compute_dlm_stats (status, lockres->vote_status,
			       	&(OcfsGlobalCtxt.net_reqst_stats));

	ocfs_compute_dlm_stats (status, lockres->vote_status,
			       	&(osb->net_reqst_stats));

      finally:
	ocfs_safefree (dlm_msg);
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_send_dlm_request_msg */
#endif /* USERSPACE_TOOL */

/*
 * ocfs_make_lock_master()
 *
 */
int ocfs_make_lock_master (ocfs_super * osb, __u64 lock_id, __u32 lock_type,
			   __u32 flags, ocfs_lock_res * lockres,
			   ocfs_file_entry * fe, bool *disk_vote)
{
	__u64 vote_map = 0;
	__u64 lockseqnum = 0;
	int status = 0;
	int tmpstat;
	unsigned long jif;

	LOG_ENTRY ();

	ocfs_acquire_lockres (lockres);
	vote_map = osb->publ_map;

	if (((flags & FLAG_FILE_DELETE) || (flags & FLAG_FILE_RENAME)) &&
	    (!(flags & FLAG_DIR)) &&
	    (DISK_LOCK_CURRENT_MASTER (fe) == osb->node_num)) {
		vote_map = DISK_LOCK_OIN_MAP (fe);
		vote_map &= osb->publ_map;	/* remove all dead nodes */
	}

	vote_map &= ~(1 << osb->node_num);

	if (vote_map == 0) {
		/* As this is the only node alive, make it master of the lock */
		if (lockres->lock_type <= lock_type)
			lockres->lock_type = (__u8) lock_type;
		lockres->master_node_num = osb->node_num;

		status = ocfs_update_disk_lock (osb, lockres,
				DLOCK_FLAG_MASTER | DLOCK_FLAG_LOCK, fe);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto bail;
		}
		goto bail;
	}

	ocfs_compute_lock_type_stats (&(osb->lock_type_stats),
				      OCFS_MAKE_LOCK_MASTER);

	flags |= FLAG_FILE_ACQUIRE_LOCK;

#ifndef USERSPACE_TOOL
	if (comm_voting && !*disk_vote) {
		LOG_TRACE_STR ("Network vote");
		jif = jiffies;
		status = ocfs_send_dlm_request_msg (osb, lock_id, lock_type,
						    flags, lockres, vote_map);
		if (status >= 0) {
			status = lockres->vote_status;
			if (status >= 0)
				goto vote_success;
			else
				goto bail;
		} else if (status == -ETIMEDOUT) {
			LOG_TRACE_STR ("Network voting timed out");
		}
		lockres->vote_state = 0;
	}
#endif

	LOG_TRACE_STR ("Disk vote");
	*disk_vote = true;
	jif = jiffies;
	status = ocfs_request_vote (osb, lock_id, lock_type, flags, vote_map,
				    &lockseqnum);
	if (status < 0) {
		if (status != -EAGAIN)
			LOG_ERROR_STATUS (status);
		goto bail;
	}

	status = ocfs_wait_for_vote (osb, lock_id, lock_type, flags, vote_map,
				     5000, lockseqnum, lockres);
	if (status < 0) {
		if (status != -EAGAIN)
			LOG_ERROR_STATUS (status);
		goto bail;
	}

#ifndef USERSPACE_TOOL
      vote_success:
#endif
	jif = jiffies - jif;
	LOG_TRACE_ARGS ("Lock time: %u\n", jif);

	/* Make this node the master of this lock */
	if (lockres->lock_type <= lock_type)
		lockres->lock_type = (__u8) lock_type;

	lockres->master_node_num = osb->node_num;

	/* Write that we now are the master to the disk */
	status = ocfs_update_disk_lock (osb, lockres,
		 DLOCK_FLAG_MASTER | DLOCK_FLAG_LOCK | DLOCK_FLAG_OPEN_MAP, fe);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto bail;
	}

      bail:
	if (*disk_vote) {
		tmpstat = ocfs_reset_voting (osb, lock_id, lock_type, vote_map);
		if (tmpstat < 0)
			LOG_ERROR_STATUS (tmpstat);
	}
	ocfs_release_lockres (lockres);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_make_lock_master */

/*
 * ocfs_acquire_lockres_ex()
 *
 * @lockres: lockres to acquire
 * @timeout: timeout in ms, 0 == no timeout
 */
int ocfs_acquire_lockres_ex (ocfs_lock_res * lockres, __u32 timeout)
{
	int mypid;
	unsigned long jif = 0;
	int status = 0;
	int cnt = 0;

	LOG_ENTRY_ARGS ("(0x%p, %d)\n", lockres, timeout);

	mypid = ocfs_getpid ();

	if (timeout)
		jif = jiffies + (timeout * HZ / 1000);

	while (1) {
		spin_lock (&lockres->lock_mutex);

		if (lockres->in_use) {
			if (lockres->thread_id != mypid) {
				spin_unlock (&lockres->lock_mutex);
				if (jif && jif < jiffies) {
					LOG_TRACE_ARGS ("lockpid=%d, newpid=%d,"
						" timedout\n",
						lockres->thread_id, mypid);
					status = -ETIMEDOUT;
					goto bail;
				}

				if (++cnt == 10) {
					LOG_TRACE_ARGS ("lockpid=%d, newpid=%d\n",
						lockres->thread_id, mypid);
					cnt = 0;
				}
				ocfs_sleep (OCFS_NM_HEARTBEAT_TIME / 10);
			}
			else {
				lockres->in_use++;
				spin_unlock (&lockres->lock_mutex);
				break;
			}
		} else {
			lockres->in_use = 1;
			lockres->thread_id = mypid;
			spin_unlock (&lockres->lock_mutex);
			break;
		}
	}

      bail:
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_acquire_lockres_ex */

/*
 * ocfs_release_lockres()
 *
 */
void ocfs_release_lockres (ocfs_lock_res * lockres)
{
	LOG_ENTRY_ARGS ("(0x%p)\n", lockres);

	spin_lock (&lockres->lock_mutex);
	if (lockres->in_use == 0) {
		LOG_TRACE_ARGS("Releasing lockres with inuse 0: 0x%p\n",
			       lockres);
		lockres->thread_id = 0;
		lockres->in_use = 0;
	} else {
		lockres->in_use--;
		if (lockres->in_use == 0) {
			lockres->thread_id = 0;
		}
	}
	spin_unlock (&lockres->lock_mutex);

	LOG_EXIT ();
	return;
}				/* ocfs_release_lockres */


/*
 * ocfs_update_disk_lock()
 *
 */
int ocfs_update_disk_lock (ocfs_super * osb, ocfs_lock_res * lockres,
			   __u32 flags, ocfs_file_entry * fe)
{
	int status = 0;
	__u64 offset = 0;
	ocfs_file_entry *tmp_fe = NULL;

	LOG_ENTRY ();

	offset = lockres->sector_num;
	if (fe == NULL)
		status = ocfs_get_file_entry (osb, &tmp_fe, lockres->sector_num);
	else {
		tmp_fe = fe;
		status = ocfs_read_disk (osb, (void *) tmp_fe,
					 (__u32) osb->sect_size, offset);
	}
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	if (flags & DLOCK_FLAG_MASTER)
		DISK_LOCK_CURRENT_MASTER (tmp_fe) = lockres->master_node_num;

	if (flags & DLOCK_FLAG_LOCK)
		DISK_LOCK_FILE_LOCK (tmp_fe) = lockres->lock_type;

	if (flags & DLOCK_FLAG_OPEN_MAP)
		DISK_LOCK_OIN_MAP (tmp_fe) = lockres->oin_openmap;

	if (flags & DLOCK_FLAG_SEQ_NUM)
		DISK_LOCK_SEQNUM (tmp_fe) = lockres->last_upd_seq_num;

	status = ocfs_write_disk (osb, tmp_fe, osb->sect_size, offset);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

      finally:
	if ((tmp_fe != fe))
		ocfs_release_file_entry (tmp_fe);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_update_disk_lock */


#define ACQUIRE_WITH_FLAG(lock, flag)			\
	do {						\
		if (!(flag)) {				\
			ocfs_acquire_lockres(lock);	\
			(flag) = true;			\
		}					\
	} while (0)

#define RELEASE_WITH_FLAG(lock, flag)			\
	do {						\
		if (flag) {				\
			ocfs_release_lockres(lock);	\
			(flag) = false;			\
		}					\
	} while (0)

/*
 * ocfs_update_master_on_open()
 *
 */
int ocfs_update_master_on_open (ocfs_super * osb, ocfs_lock_res * lockres)
{
	int status = -EAGAIN;
	bool disk_vote = false;
	bool lock_acq = false;

	LOG_ENTRY ();

	ocfs_get_lockres(lockres);

	while (status == -EAGAIN) {
		if (!IS_NODE_ALIVE (osb->publ_map, lockres->master_node_num,
				    OCFS_MAXIMUM_NODES)) {
			LOG_TRACE_ARGS ("Master (%u) dead, lockid %u.%u\n",
				lockres->master_node_num, HILO (lockres->sector_num));
			status = 0;
			goto bail;
		}

		ACQUIRE_WITH_FLAG(lockres, lock_acq);

		if (lockres->master_node_num == osb->node_num) {
			LOG_TRACE_ARGS ("Added node to map 0x%08x, lockid %u.%u\n",
			     LO (lockres->oin_openmap), HILO (lockres->sector_num));

			lockres->oin_openmap |= (1 << osb->node_num);
			status = ocfs_update_disk_lock (osb, lockres,
						DLOCK_FLAG_OPEN_MAP, NULL);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto bail;
			}
		} else {
			status = ocfs_update_lock_state (osb, lockres,
						 FLAG_ADD_OIN_MAP, &disk_vote);
			if (status < 0) {
				if (status != -EAGAIN)
					LOG_ERROR_STATUS (status);
				RELEASE_WITH_FLAG(lockres, lock_acq);
				if (status == -EAGAIN) {
					ocfs_sleep (500);
					if (ocfs_task_interruptible (osb)) {
						LOG_TRACE_ARGS("interrupted... "
							"lockid=%u.%u\n",
							HILO(lockres->sector_num));
						status = -EINTR;
						goto bail;
					}
					continue;
				}
				goto bail;
			}
		}
	}

      bail:
	RELEASE_WITH_FLAG(lockres, lock_acq);
	ocfs_put_lockres(lockres);
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_update_master_on_open */

/*
 * ocfs_init_lockres()
 *
 */
void ocfs_init_lockres (ocfs_super * osb, ocfs_lock_res * lockres, __u64 lock_id)
{
	LOG_ENTRY_ARGS ("(0x%p, 0x%p, %u.%u)\n", osb, lockres,
			HI (lock_id), LO (lock_id));

	lockres->signature = 0x55AA;
	lockres->lock_type = OCFS_DLM_NO_LOCK;
	lockres->master_node_num = OCFS_INVALID_NODE_NUM;
	lockres->last_upd_seq_num = 0;
	lockres->oin_openmap = 0;
	lockres->sector_num = lock_id;
	lockres->in_use = 0;
	lockres->oin = NULL;
	lockres->lock_state = 0;
	lockres->vote_state = 0;
	lockres->in_cache_list = false;

#ifndef USERSPACE_TOOL
	spin_lock_init (&lockres->lock_mutex);
	init_waitqueue_head (&lockres->voted_event);
#endif
	atomic_set (&lockres->voted_event_woken, 0);
	atomic_set (&lockres->lr_ref_cnt, 0);
	atomic_set (&lockres->lr_share_cnt, 0);

	/* For read/write caching */
	lockres->last_read_time = 0;
	lockres->last_write_time = 0;
	lockres->writer_node_num = OCFS_INVALID_NODE_NUM;
	lockres->reader_node_num = OCFS_INVALID_NODE_NUM;

	LOG_EXIT ();
	return;
}				/* ocfs_init_lockres */

/*
 * ocfs_create_update_lock()
 *
 */
int ocfs_create_update_lock (ocfs_super * osb, ocfs_inode * oin, __u64 lock_id,
			     __u32 flags)
{
	int status = 0;
	ocfs_lock_res *lockres = NULL;
	ocfs_lock_res *tmp_lockres = NULL;
	bool is_dir = false;

	LOG_ENTRY_ARGS ("(0x%p, 0x%p, %u.%u, %u)\n", osb, oin,
			HI (lock_id), LO (lock_id), flags);

	is_dir = (flags & OCFS_OIN_DIRECTORY) ? true : false;

	/* Check the lock state on the disk / in our resource map */
	status = ocfs_lookup_sector_node (osb, lock_id, &lockres);
	if (status >= 0) {
		ocfs_acquire_lockres (lockres);
		if (lockres->oin) {
			if (lockres->oin->obj_id.type != OCFS_TYPE_OIN) {
				ocfs_release_lockres (lockres);
				LOG_ERROR_STATUS (status = -EFAIL);
				goto bail;
			} else {
				ocfs_put_lockres (lockres->oin->lock_res);
				lockres->oin->lock_res = NULL;
			}
		}
		lockres->oin = oin;
		oin->oin_flags |= flags;
		oin->lock_res = lockres;
		ocfs_get_lockres (lockres);
		ocfs_release_lockres (lockres);

		status = ocfs_wait_for_lock_release (osb, lock_id, 30000, lockres,
				(is_dir ? OCFS_DLM_EXCLUSIVE_LOCK : OCFS_DLM_NO_LOCK));
		if (status < 0) {
			if (status != -EINTR)
				LOG_ERROR_STATUS (status);
			goto bail;
		}
	} else {
		/* Create a resource and insert in the hash */
		lockres = ocfs_allocate_lockres();
		if (lockres == NULL) {
			LOG_ERROR_STATUS (status = -ENOMEM);
			goto bail;
		}

		ocfs_init_lockres (osb, lockres, lock_id);

		ocfs_get_lockres (lockres);

		status = ocfs_wait_for_lock_release (osb, lock_id, 30000, lockres,
				(is_dir ? OCFS_DLM_EXCLUSIVE_LOCK : OCFS_DLM_NO_LOCK));
		if (status < 0) {
			if (status != -EINTR)
				LOG_ERROR_STATUS (status);
			goto bail;
		}
		
		status = ocfs_check_for_stale_lock(osb, lockres, NULL, lock_id);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto bail;
		}

		status = ocfs_insert_sector_node (osb, lockres, &tmp_lockres);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto bail;
		}

		/* Check if another process added a lockres after our lookup */
		if (tmp_lockres) {
			/* If so, put the recently allocated lockres */
			ocfs_put_lockres (lockres);
			lockres = tmp_lockres;
			status = ocfs_wait_for_lock_release (osb, lock_id, 30000, lockres,
					(is_dir ? OCFS_DLM_EXCLUSIVE_LOCK : OCFS_DLM_NO_LOCK));
			if (status < 0) {
				if (status != -EINTR)
					LOG_ERROR_STATUS (status);
				goto bail;
			}
		} else {
			if (flags & OCFS_OIN_CACHE_UPDATE) {
				status = ocfs_insert_cache_link (osb, lockres);
				if (status < 0) {
					LOG_ERROR_STR ("Lock up volume");
					goto bail;
				}
			}
		}
	}

	ocfs_acquire_lockres (lockres);

	lockres->oin = oin;
	oin->oin_flags |= flags;
	if (oin->lock_res != lockres) {
		ocfs_put_lockres (oin->lock_res);
		oin->lock_res = lockres;
		ocfs_get_lockres (lockres);
	}

	LOG_TRACE_ARGS ("MasterNode=%d, ThisNode=%d\n",
			lockres->master_node_num, osb->node_num);

	if ((!is_dir) && (lockres->master_node_num != OCFS_INVALID_NODE_NUM) &&
	    ((!IS_NODE_ALIVE (lockres->oin_openmap, osb->node_num, OCFS_MAXIMUM_NODES)) ||
	     (lockres->lock_state & FLAG_ALWAYS_UPDATE_OPEN))) {
		/* Send a message to master so that he can send the oin update to */
		/* this node also. If u are the master then update File_entry */
		/* and set the bit that this node has a open */
		status = ocfs_update_master_on_open (osb, lockres);
		if (status < 0 && status != -EINTR)
			LOG_ERROR_STATUS (status);
	}

	ocfs_release_lockres (lockres);

      bail:
	ocfs_put_lockres(lockres);
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_create_update_lock */


/*
 * ocfs_get_x_for_del()
 *
 */
int ocfs_get_x_for_del (ocfs_super * osb, __u64 lock_id, __u32 lock_type, __u32 flags,
			ocfs_lock_res * lockres, ocfs_file_entry * fe)
{
	int status = -EFAIL;
	bool disk_vote = false;

	LOG_ENTRY_ARGS ("(lockid=%u.%u, locktype=%u)\n", HI (lock_id),
			LO (lock_id), lock_type);

	while (1) {
		ocfs_acquire_lockres (lockres);
		/* If I am master and I am the only one in the oin node map */
		/* update the disk */
		status = ocfs_make_lock_master (osb, lock_id, lock_type, flags,
						lockres, fe, &disk_vote);
		if (status >= 0) {
			ocfs_release_lockres (lockres);
			status = 0;
			goto finally;
		} else if (status == -EAGAIN) {
			ocfs_release_lockres (lockres);
			ocfs_sleep (500);
			if (ocfs_task_interruptible (osb)) {
				LOG_TRACE_ARGS("interrupted... lockid=%u.%u\n",
					       HILO(lock_id));
				status = -EINTR;
				goto finally;
			}
			status = ocfs_disk_update_resource (osb, lockres, fe, 0);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				osb->vol_state = VOLUME_DISABLED;
				goto finally;
			}
			continue;
		} else {
			ocfs_release_lockres (lockres);
			if (status != -EBUSY)
				LOG_ERROR_STATUS (status);
			goto finally;
		}
	}

      finally:
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_get_x_for_del */


/*
 * ocfs_try_exclusive_lock()
 *
 */
int ocfs_try_exclusive_lock(ocfs_super *osb, ocfs_lock_res *lockres, __u32 flags,
			    __u32 updated, ocfs_file_entry *fe, __u64 lock_id,
			    __u32 lock_type)
{
    int status = 0;
    bool lockres_acq = false;
    bool make_lock_master;
    bool disk_vote = false;

    LOG_ENTRY_ARGS ("(osb=0x%p, lres=0x%p, fl=%u, up=%u, fe=0x%p, "
		    "id=%u.%u ty=%u)\n", osb, lockres, flags, updated, fe,
		    HILO(lock_id), lock_type);

    ocfs_get_lockres(lockres);
    while (1) {
	ACQUIRE_WITH_FLAG(lockres, lockres_acq);
   
	if (lockres->master_node_num != osb->node_num || !updated) {
		status = ocfs_read_file_entry (osb, fe, lock_id);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}
		lockres->master_node_num = DISK_LOCK_CURRENT_MASTER (fe);
		lockres->lock_type = DISK_LOCK_FILE_LOCK (fe);
		lockres->oin_openmap = DISK_LOCK_OIN_MAP (fe);
		updated = true;
	}
            
        if (lockres->master_node_num == osb->node_num) {
            if ((flags & FLAG_FILE_DELETE) || (flags & FLAG_FILE_RENAME)) {
                status = ocfs_get_x_for_del (osb, lock_id, lock_type, flags,
					     lockres, fe);
		if (status < 0) {
			if (status != -EINTR)
				LOG_ERROR_STATUS (status);
		}
                goto finally;
            }
#if 0
            if (!updated) {
                if (lockres->master_node_num != DISK_LOCK_CURRENT_MASTER (fe)) {
                    lockres->master_node_num = DISK_LOCK_CURRENT_MASTER (fe);
                    lockres->lock_type = DISK_LOCK_FILE_LOCK (fe);
                    lockres->oin_openmap = DISK_LOCK_OIN_MAP (fe);
                    RELEASE_WITH_FLAG(lockres, lockres_acq);
                    continue;
                }
            }
#endif
            DISK_LOCK_CURRENT_MASTER (fe) = osb->node_num;
                
            if (DISK_LOCK_FILE_LOCK (fe) < OCFS_DLM_EXCLUSIVE_LOCK) {
                DISK_LOCK_FILE_LOCK (fe) = lock_type;
                    
                if (lock_type == OCFS_DLM_ENABLE_CACHE_LOCK) {
                    status = ocfs_write_force_disk (osb, fe, osb->sect_size, lock_id);
                    if (status < 0) {
                        LOG_ERROR_STATUS (status);
                        goto finally;
                    }
                }
            }
                                            
            status = ocfs_write_file_entry (osb, fe, lock_id);
            if (status < 0) {
                LOG_ERROR_STATUS (status);
                goto finally;
            }

            /* We got the lock */
            lockres->lock_type = lock_type;
            status = 0;
            goto finally;
        } else {
            make_lock_master = false;
#if 0
            lockres->master_node_num = DISK_LOCK_CURRENT_MASTER (fe);
            lockres->lock_type = DISK_LOCK_FILE_LOCK (fe);
            lockres->oin_openmap = DISK_LOCK_OIN_MAP (fe);
#endif
            if (lockres->master_node_num != OCFS_INVALID_NODE_NUM) {
                if (!IS_VALID_NODE_NUM (lockres->master_node_num)) {
		    status = -EINVAL;
		    LOG_ERROR_ARGS ("node=%d, status = %d", lockres->master_node_num, status);
                    goto finally;
                }
            }
    
            if (lockres->master_node_num == OCFS_INVALID_NODE_NUM) {
                make_lock_master = true;
            } else if (!IS_NODE_ALIVE (osb->publ_map, lockres->master_node_num,
				       OCFS_MAXIMUM_NODES)) {
                make_lock_master = true;
                RELEASE_WITH_FLAG(lockres, lockres_acq); 
                        
                LOG_TRACE_ARGS ("ocfs_recover_vol(%d)\n",
				lockres->master_node_num);
                status = ocfs_recover_vol (osb, lockres->master_node_num);
                if (status < 0) {
                    LOG_ERROR_STATUS (status);
                    goto finally;
                }
                ACQUIRE_WITH_FLAG(lockres, lockres_acq); 
            }

            if (make_lock_master) {
                /*
		 * I am not master, master is dead or not there.
                 * If lock was owned we need to do recovery
                 * otherwise we need to arbitrate for the lock
		 */

//                RELEASE_WITH_FLAG(lockres, lockres_acq); 

                status = ocfs_make_lock_master (osb, lock_id, lock_type, flags,
						lockres, fe, &disk_vote);
                if (status >= 0) {
//                    RELEASE_WITH_FLAG(lockres, lockres_acq); 
                            
                    if (lock_type == OCFS_DLM_ENABLE_CACHE_LOCK) {
                        DISK_LOCK_FILE_LOCK (fe) = lock_type;
                        status = ocfs_write_force_disk (osb, fe, osb->sect_size,
							lock_id);
                        if (status < 0) {
                            LOG_ERROR_STATUS (status);
                            goto finally;
                        }
                    }
        
                    DISK_LOCK_CURRENT_MASTER (fe) = osb->node_num;
                    DISK_LOCK_FILE_LOCK (fe) = lock_type;
                      
                    status = ocfs_write_file_entry (osb, fe, lock_id);
                    if (status < 0) {
                        LOG_ERROR_STATUS (status);
                        goto finally;
                    }
                                
                    /* We got the lock */
                    status = 0;
                    goto finally;
                } else if (status == -EAGAIN) {
                    RELEASE_WITH_FLAG(lockres, lockres_acq); 
		    ocfs_sleep (500);
		    if (ocfs_task_interruptible (osb)) {
			LOG_TRACE_ARGS("interrupted... lockid=%u.%u\n",
				       HILO(lock_id));
			status = -EINTR;
			goto finally;
		    }
#if 0
                    status = ocfs_disk_update_resource (osb, lockres, fe, 0);
                    if (status < 0) {
                        LOG_ERROR_STATUS (status);
                        osb->vol_state = VOLUME_DISABLED;
                        goto finally;
                    }
#endif
		    updated = false;
                    continue;
                } else {
                    RELEASE_WITH_FLAG(lockres, lockres_acq); 
                    goto finally;
                }
            } else /* !make_lock_master */ {
                /*
		 * MasterNode is alive and it is not this node
                 * If the lock is acquired already by the master
                 * wait for release else change master.
		 */
   
		if (lockres->lock_type <= OCFS_DLM_SHARED_LOCK) {
                    if ((flags & FLAG_FILE_DELETE) ||
			(flags & FLAG_FILE_RENAME)) {
                        status = ocfs_get_x_for_del (osb, lock_id, lock_type,
						     flags, lockres, fe);
                        RELEASE_WITH_FLAG(lockres, lockres_acq); 
			if (status < 0) {
				if (status != -EINTR && status != -EBUSY)
					LOG_ERROR_STATUS (status);
			}
                        goto finally;
                    }

                    /* Change Lock Master */
                    status = ocfs_update_lock_state (osb, lockres,
						FLAG_CHANGE_MASTER, &disk_vote);
//                    RELEASE_WITH_FLAG(lockres, lockres_acq); 

                    if (status < 0) {
			RELEASE_WITH_FLAG(lockres, lockres_acq); 
			if (status == -EAGAIN) {
				ocfs_sleep (500);
				if (ocfs_task_interruptible (osb)) {
					LOG_TRACE_ARGS("interrupted... "
						"lockid=%u.%u\n", HILO(lock_id));
					status = -EINTR;
					goto finally;
				}
				continue;
			}
                        goto finally;
                    }
                                
		    status = ocfs_read_file_entry (osb, fe, lock_id);
                    if (status < 0) {
                        LOG_ERROR_STATUS (status);
                        goto finally;
                    }
        
                    DISK_LOCK_CURRENT_MASTER (fe) = osb->node_num;
                    DISK_LOCK_FILE_LOCK (fe) = lock_type;
                    status = ocfs_write_file_entry (osb, fe, lock_id);
                    if (status < 0) {
                        LOG_ERROR_STATUS (status);
                        goto finally;
                    }
       
                    /* Update our state... */
                    lockres-> master_node_num = DISK_LOCK_CURRENT_MASTER (fe);
                    lockres->lock_type = DISK_LOCK_FILE_LOCK (fe);
                    lockres-> oin_openmap = DISK_LOCK_OIN_MAP (fe);
		    RELEASE_WITH_FLAG(lockres, lockres_acq);
                    goto finally;
                } else {
                    /* Wait for lock release */
                    RELEASE_WITH_FLAG(lockres, lockres_acq); 

                    status = ocfs_wait_for_lock_release (osb, lock_id, 30000, lockres,
			((flags & FLAG_DIR) ? OCFS_DLM_SHARED_LOCK : OCFS_DLM_NO_LOCK));
                    if (status < 0) {
			if (status == -ETIMEDOUT) {
				LOG_TRACE_ARGS("lock %u.%u, level %d, not being "
					"freed by node %u\n", HILO(lock_id),
				       	lockres->lock_type, lockres->master_node_num);
				continue;
			} else if (status == -EINTR)
				goto finally;
                        else
                            goto finally;
                    }
                    /* Try and acquire the lock again */
                    continue;
                }
            } /* make_lock_master */
        } /* master_node_num */
    } /* while */
    
finally: 
    RELEASE_WITH_FLAG(lockres, lockres_acq); 
    ocfs_put_lockres(lockres);
    LOG_EXIT_STATUS (status);
    return status;
}				/* ocfs_try_exclusive_lock */

/*
 * ocfs_acquire_lock()
 *
 */
int ocfs_acquire_lock (ocfs_super * osb, __u64 lock_id, __u32 lock_type, __u32 flags,
		       ocfs_lock_res ** lockres, ocfs_file_entry * lock_fe)
{
	int status = -EFAIL;
	bool lockres_acq = false;
	ocfs_file_entry *disklock = NULL;
	__u32 updated = 0;

	LOG_ENTRY_ARGS ("(0x%p, %u.%u, %u, %u, 0x%p, 0x%p)\n", osb,
			HI (lock_id), LO (lock_id), lock_type, flags, lockres,
			lock_fe);

	if (lock_fe)
		disklock = lock_fe;
	else {
		disklock = ocfs_allocate_file_entry ();
		if (disklock == NULL) {
			LOG_ERROR_STATUS (status = -ENOMEM);
			goto finally;
		}
	}

	status = ocfs_find_update_res (osb, lock_id, lockres, disklock,
				       &updated, 0);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	switch (lock_type) {
	    case OCFS_DLM_SHARED_LOCK:
		    if (!(flags & FLAG_DIR)) {
			    status = 0;
			    goto finally;
		    }

		    ocfs_acquire_lockres (*lockres);
		    lockres_acq = true;

		    if ((*lockres)->lock_type == OCFS_DLM_NO_LOCK) {
			    (*lockres)->lock_type = OCFS_DLM_SHARED_LOCK;
		    }
		    if (((*lockres)->lock_type == OCFS_DLM_ENABLE_CACHE_LOCK) &&
			((*lockres)->master_node_num != osb->node_num)) {
			    status = ocfs_break_cache_lock (osb, *lockres, disklock);
			    if (status < 0) {
				    if (status != -EINTR)
					LOG_ERROR_STATUS (status);
				    goto finally;
			    }
		    }

		    atomic_inc (&((*lockres)->lr_share_cnt));

		    if (lockres_acq) {
			    ocfs_release_lockres (*lockres);
			    lockres_acq = false;
		    }

		    status = 0;
		    goto finally;
		    break;

	    case OCFS_DLM_EXCLUSIVE_LOCK:
	    case OCFS_DLM_ENABLE_CACHE_LOCK:
		    /* This will be called for vol, allocation, file and */
		    /* directory from create modify */
		    status = ocfs_try_exclusive_lock(osb, *lockres, flags,
						     updated, disklock, lock_id,
						     lock_type);
		    if (status < 0) {
			if (status != -EINTR && status != -EBUSY)
				LOG_ERROR_STATUS (status);
			goto finally;
		    }
		    break;

	    default:
		    break;
	}

      finally:
	if ((lock_fe == NULL))
		ocfs_release_file_entry (disklock);
	if (lockres_acq)
		ocfs_release_lockres (*lockres);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_acquire_lock */

/*
 * ocfs_disk_release_lock()
 *
 */
int ocfs_disk_release_lock (ocfs_super * osb, __u64 lock_id, __u32 lock_type,
			    __u32 flags, ocfs_lock_res * lockres, ocfs_file_entry *fe)
{
	__u32 votemap = 0;
	__u32 tempmap = 0;
	__u32 i;
	int status = 0;
	int tmpstat;
	__u64 lockseqno;
	bool cachelock = false;
	bool disk_vote = false;
	bool fe_alloc = false;
	unsigned long jif;
	bool disk_reset = true;

	LOG_ENTRY_ARGS ("(0x%p, %u.%u, %u, %u, 0x%p)\n", osb, HI (lock_id),
			LO (lock_id), lock_type, flags, lockres);

//	ocfs_acquire_lockres (lockres);

	if (fe == NULL) {
		status = ocfs_get_file_entry (osb, &fe, lock_id);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finito;
		}
		fe_alloc = true;
	}

	if (!IS_VALID_NODE_NUM (DISK_LOCK_CURRENT_MASTER (fe))) {
		LOG_ERROR_STATUS(status = -EINVAL);
		goto finito;
	}

	if (DISK_LOCK_CURRENT_MASTER (fe) != osb->node_num) {
		LOG_ERROR_ARGS ("Current master is NOT this NODE (%d)",
				DISK_LOCK_CURRENT_MASTER (fe));
		status = 0;
		goto finito;
	}

	for (i = 0; i < OCFS_MAXIMUM_NODES; i++) {
		if (((1 << i) & DISK_LOCK_OIN_MAP (fe)) &&
		    IS_NODE_ALIVE (osb->publ_map, i, OCFS_MAXIMUM_NODES)) {
			votemap |= (1 << i);
		}
	}

	/* Send an update to all nodes alive, can be optimized later TODO */
	if ((flags & FLAG_FILE_RENAME) || (flags & FLAG_FILE_DELETE))
		votemap = (__u32) (osb->publ_map);

	/* TODO: figure out how to properly handle inode updates w/no oin */
	votemap = (__u32) (osb->publ_map);	// temporary hack, forces broadcast

	/* remove current node from the votemap */
	tempmap = (1 << osb->node_num);
	votemap &= (~tempmap);
	jif = jiffies;

	if (votemap == 0)
		goto finally;

	if (!(flags & FLAG_FILE_UPDATE_OIN) && !(flags & FLAG_FILE_DELETE))
		goto finally;

	ocfs_compute_lock_type_stats (&(osb->lock_type_stats),
				      OCFS_DISK_RELEASE_LOCK);

	flags |= FLAG_FILE_RELEASE_LOCK;
	status = -EAGAIN;
	while (status == -EAGAIN) {
#ifndef USERSPACE_TOOL
		if (comm_voting && !disk_vote) {
			LOG_TRACE_STR ("Network vote");
			status = ocfs_send_dlm_request_msg (osb, lock_id, lock_type,
						    	flags, lockres, votemap);
			if (status >= 0) {
				status = lockres->vote_status;
				if (status >= 0) {
					goto finally;
				} else if (status == -EAGAIN) {
					goto loop;
				} else {
					LOG_ERROR_STATUS (status);
					goto finito;
				}
			} else if (status == -ETIMEDOUT) {
				LOG_TRACE_STR ("Network voting timed out");
			}
			lockres->vote_state = 0;
		}
#endif
	
		LOG_TRACE_STR ("Disk vote");
		disk_vote = true;
		jif = jiffies;

		disk_reset = false;
		status = ocfs_request_vote (osb, lock_id, lock_type, flags,
					    votemap, &lockseqno);
		if (status < 0) {
			if (status == -EAGAIN) {
				if ((flags & FLAG_FILE_UPDATE_OIN)) {
					// ?????
				}
				goto reset;
			}
			LOG_ERROR_STATUS (status);
			goto finito;
		}

		status = ocfs_wait_for_vote (osb, lock_id, lock_type,
					     FLAG_FILE_UPDATE_OIN, votemap,
					     5000, lockseqno, lockres);
		if (status < 0) {
			if (status == -EAGAIN)
				goto reset;
			LOG_ERROR_STATUS (status);
			goto finito;
		}

              reset:
		tmpstat = ocfs_reset_voting (osb, lock_id, lock_type,
					     DISK_LOCK_OIN_MAP (fe));
		if (tmpstat < 0) {
			LOG_ERROR_STATUS (status = tmpstat);
			goto finito;
		}

		disk_reset = true;

		if (status != -EAGAIN)
			break;

#ifndef USERSPACE_TOOL
	      loop:
#endif
		LOG_TRACE_ARGS ("id=%u.%u\n", HILO(lock_id));
		ocfs_sleep (500);
	}

      finally:
	jif = jiffies - jif;
	LOG_TRACE_ARGS ("Lock time: %u\n", jif);

	if (disk_vote && !disk_reset) {
		tmpstat = ocfs_reset_voting (osb, lock_id, lock_type,
					    DISK_LOCK_OIN_MAP (fe));
		if (tmpstat < 0)
			LOG_ERROR_STATUS (tmpstat);
	}

	if (flags & FLAG_FILE_RELEASE_MASTER)
		DISK_LOCK_CURRENT_MASTER (fe) = OCFS_INVALID_NODE_NUM;

	if ((DISK_LOCK_FILE_LOCK (fe) == OCFS_DLM_ENABLE_CACHE_LOCK) &&
	    (DISK_LOCK_CURRENT_MASTER (fe) == osb->node_num))
		cachelock = true;
	else
		DISK_LOCK_FILE_LOCK (fe) = OCFS_DLM_NO_LOCK;

	/* Reset the lock on the disk */
	if (!cachelock) {
		tmpstat = ocfs_write_file_entry (osb, fe, lock_id);
		if (tmpstat < 0)
			LOG_ERROR_STATUS (tmpstat);
	}

      finito:
	if (fe_alloc)
		ocfs_release_file_entry (fe);
//	ocfs_release_lockres (lockres);
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_disk_release_lock */

/*
 * ocfs_release_lock()
 *
 */
int ocfs_release_lock (ocfs_super * osb, __u64 lock_id, __u32 lock_type, __u32 flags,
		       ocfs_lock_res * lockres, ocfs_file_entry *fe)
{
	int status = 0;
	bool lock_acq = false;

	LOG_ENTRY_ARGS ("(0x%p, %u.%u, %u, %u, 0x%p)\n", osb, HI (lock_id),
			LO (lock_id), lock_type, flags, lockres);

	ocfs_acquire_lockres (lockres);
	lock_acq = true;

	switch (lock_type) {
	    case OCFS_DLM_SHARED_LOCK:
		    if (atomic_dec_and_test (&lockres->lr_share_cnt)) {
			    if (lockres->lock_type == OCFS_DLM_SHARED_LOCK)
				    lockres->lock_type = OCFS_DLM_NO_LOCK;
		    }
		    status = 0;
		    goto finally;

	    case OCFS_DLM_EXCLUSIVE_LOCK:
		    break;
	}
	/*
	 * Change flags based on which kind of lock we are releasing
	 * For directory we need special handling of oin updates when the release
	 * is for XBcast
	 * For file we need to update oin's
	 * For Shared we need to update the lock state locally only
	 */

	/* OcfsRelease */

	/* CommReleaseLock */
	if (flags & FLAG_FILE_DELETE) {
		lockres->lock_type = OCFS_DLM_NO_LOCK;
		lockres->master_node_num = OCFS_INVALID_NODE_NUM;
		status = 0;
		goto do_release_lock;
	}
//    if(lock_id != OCFS_BITMAP_LOCK_OFFSET)
	{
		if ((lockres->lock_type == OCFS_DLM_ENABLE_CACHE_LOCK) &&
		    (lockres->master_node_num == osb->node_num)) {
			status = 0;
			goto finally;
		}
	}

	if (lock_id == OCFS_BITMAP_LOCK_OFFSET) {
		LOG_TRACE_ARGS ("Bitmap lock state is (%d)\n",
				lockres->lock_type);
	}

	lockres->lock_type = OCFS_DLM_NO_LOCK;
	if (flags & FLAG_FILE_RELEASE_MASTER)
		lockres->master_node_num = OCFS_INVALID_NODE_NUM;

do_release_lock:
//	if (lock_acq) {
//		ocfs_release_lockres (lockres);
//		lock_acq = false;
//	}

	status = ocfs_disk_release_lock (osb, lock_id, lock_type, flags,
					 lockres, fe);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

finally:
	if (lock_acq)
		ocfs_release_lockres (lockres);

	LOG_EXIT_STATUS (status);
	return (status);
}				/* ocfs_release_lock */

/*
 * ocfs_init_dlm()
 *
 */
int ocfs_init_dlm (void)
{
	LOG_ENTRY ();

	OcfsIpcCtxt.init = false;

	LOG_EXIT_STATUS (0);
	return 0;
}				/* ocfs_init_dlm */


/*
 * ocfs_add_lock_to_recovery()
 *
 */
int ocfs_add_lock_to_recovery (void)
{
	return 0;
}				/* ocfs_add_lock_to_recovery */

/*
 * ocfs_create_log_extent_map()
 *
 */
int ocfs_create_log_extent_map (ocfs_super * osb,
		    ocfs_io_runs ** PTransRuns,
		    __u32 * PNumTransRuns, __u64 diskOffset, __u64 ByteCount)
{
	int status = 0;
	__s64 tempVbo = 0;
	__s64 tempLbo = 0;
	__u32 tempSize = 0;
	__u32 numDataRuns = 0;
	__u32 numTransRuns;
	__u32 i;
	__u32 numMetaDataRuns = 0;
	__u32 ioRunSize;
	ocfs_io_runs *IoDataRuns = NULL;
	ocfs_io_runs *IoMetaDataRuns = NULL;
	ocfs_io_runs *IoTransRuns = NULL;
	ocfs_io_runs *TransRuns = NULL;
	__u64 file_size;
	__u64 remainingLength;
	bool bRet;
	__u32 RunsInExtentMap = 0;
	__u32 ExtentMapIndex;
	__u32 length;
	__s64 diskOffsetToFind = 0;
	__s64 foundFileOffset = 0;
	__s64 foundDiskOffset = 0;

	LOG_ENTRY ();

	ioRunSize = (OCFS_MAX_DATA_EXTENTS * sizeof (ocfs_io_runs));

	IoTransRuns = ocfs_malloc (ioRunSize);
	if (IoTransRuns == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto bail;
	}

	IoMetaDataRuns = ocfs_malloc (ioRunSize);
	if (IoMetaDataRuns == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto bail;
	}

	IoDataRuns = ocfs_malloc (ioRunSize);
	if (IoDataRuns == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto bail;
	}

	TransRuns = ocfs_malloc (ioRunSize);
	if (TransRuns == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto bail;
	}

	diskOffsetToFind = diskOffset;
	remainingLength = ByteCount;

	ocfs_down_sem (&(osb->map_lock), true);

	RunsInExtentMap = ocfs_extent_map_get_count (&osb->metadata_map);

	for (ExtentMapIndex = 0; ExtentMapIndex < RunsInExtentMap;
	     ExtentMapIndex++) {
		__u32 tempLen;

		if (!ocfs_get_next_extent_map_entry
		    (osb, &osb->metadata_map, ExtentMapIndex, &foundFileOffset,
		     &foundDiskOffset, &tempLen))
			continue;

		length = tempLen;

		if (foundDiskOffset >= (diskOffsetToFind + remainingLength))
			break;

		if (diskOffsetToFind >= (foundDiskOffset + length)) {
			/* This meta data run is before the relevant stf */
			continue;
		} else {
			if ((diskOffsetToFind >= foundDiskOffset) &&
			    ((diskOffsetToFind + remainingLength) <=
			     (foundDiskOffset + length))) {
				/* It is only metadata */
				IoMetaDataRuns[numMetaDataRuns].offset =
				    diskOffsetToFind;
				IoMetaDataRuns[numMetaDataRuns].disk_off =
				    diskOffsetToFind;
				IoMetaDataRuns[numMetaDataRuns].byte_cnt =
				    remainingLength;
				remainingLength -=
				    IoMetaDataRuns[numMetaDataRuns].byte_cnt;
				diskOffsetToFind +=
				    IoMetaDataRuns[numMetaDataRuns].byte_cnt;
				numMetaDataRuns++;
				break;
			} else if ((diskOffsetToFind < foundDiskOffset) &&
				   ((diskOffsetToFind + remainingLength) >
				    foundDiskOffset)) {
				/* We have a data run and a metadata run */
				IoDataRuns[numDataRuns].offset =
				    diskOffsetToFind;
				IoDataRuns[numDataRuns].disk_off =
				    diskOffsetToFind;
				IoDataRuns[numDataRuns].byte_cnt =
				    foundDiskOffset - diskOffsetToFind;
				remainingLength -=
				    IoDataRuns[numDataRuns].byte_cnt;
				diskOffsetToFind +=
				    IoDataRuns[numDataRuns].byte_cnt;
				numDataRuns++;

				IoMetaDataRuns[numMetaDataRuns].offset =
				    foundDiskOffset;
				IoMetaDataRuns[numMetaDataRuns].disk_off =
				    foundDiskOffset;
				IoMetaDataRuns[numMetaDataRuns].byte_cnt =
				    (remainingLength >
				     length) ? length : remainingLength;

				remainingLength -=
				    IoMetaDataRuns[numMetaDataRuns].byte_cnt;
				diskOffsetToFind +=
				    IoMetaDataRuns[numMetaDataRuns].byte_cnt;
				numMetaDataRuns++;
				if (remainingLength > 0)
					continue;
				else
					break;
			} else if ((diskOffsetToFind >= foundDiskOffset) &&
				   ((diskOffsetToFind + remainingLength) >
				    (foundDiskOffset + length))) {
				/* Meta data and as yet unknown data */
				IoMetaDataRuns[numMetaDataRuns].offset =
				    diskOffsetToFind;
				IoMetaDataRuns[numMetaDataRuns].disk_off =
				    diskOffsetToFind;
				IoMetaDataRuns[numMetaDataRuns].byte_cnt =
				    length - (diskOffsetToFind -
					      foundDiskOffset);
				remainingLength -=
				    IoMetaDataRuns[numMetaDataRuns].byte_cnt;
				diskOffsetToFind +=
				    IoMetaDataRuns[numMetaDataRuns].byte_cnt;
				numMetaDataRuns++;
				continue;
			}
		}
	}

	ocfs_up_sem (&(osb->map_lock));

	numTransRuns = *PNumTransRuns = 0;

	/* Create new extent map from real runs */

	for (i = 0; i < numMetaDataRuns; i++) {
		if (osb->log_disk_off == 0)
			ocfs_create_meta_log_files (osb);

		file_size = osb->log_file_size;

		if (file_size > (10 * ONE_MEGA_BYTE))
			LOG_ERROR_ARGS ("file_size=%d.%d", HI(file_size),
					LO(file_size));

		tempVbo = IoMetaDataRuns[i].disk_off;	/* Actual Disk Offset */
		tempLbo = file_size + osb->log_disk_off;	/* Log file disk Offset */
		tempSize = IoMetaDataRuns[i].byte_cnt;	/* Lenght of run */

		osb->log_file_size = (file_size + tempSize);

		/* Add the Extent to extent map list */
		ocfs_down_sem (&(osb->map_lock), true);
		LOG_TRACE_STR ("Acquired map_lock");

		bRet =
		    ocfs_add_extent_map_entry (osb, &osb->trans_map, tempVbo,
					   tempLbo, tempSize);
		if (!bRet) {
			ocfs_remove_extent_map_entry (osb, &osb->trans_map, tempVbo,
						  tempSize);
			bRet =
			    ocfs_add_extent_map_entry (osb, &osb->trans_map,
						   tempVbo, tempLbo, tempSize);
		}

		ocfs_up_sem (&(osb->map_lock));
		LOG_TRACE_STR ("Released map_lock");

		if (!bRet) {
			LOG_ERROR_STATUS(status = -EFAIL);
			goto bail;
		}

		TransRuns[numTransRuns].offset = tempVbo;
		TransRuns[numTransRuns].disk_off = tempLbo;
		TransRuns[numTransRuns].byte_cnt = tempSize;
		numTransRuns++;
	}

	file_size = osb->log_file_size;

	if (file_size > (10 * ONE_MEGA_BYTE))
		LOG_ERROR_ARGS ("file_size=%d.%d", HI(file_size), LO(file_size));

	if (file_size >= (2 * ONE_MEGA_BYTE))
		osb->needs_flush = true;

	*PNumTransRuns = numTransRuns;
	*PTransRuns = TransRuns;

	ocfs_safefree (IoTransRuns);
	ocfs_safefree (IoMetaDataRuns);
	ocfs_safefree (IoDataRuns);

      bail:
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_create_log_extent_map */

/*
 * ocfs_lookup_cache_link()
 *
 */
int ocfs_lookup_cache_link (ocfs_super * osb, __u8 * buf, __u64 actual_disk_off,
			    __u64 length)
{
	int status = 0;
#ifndef USERSPACE_TOOL
	ocfs_file_entry *fe = NULL;
	ocfs_lock_res *lockres = NULL;
	__u64 offset = 0;
	struct list_head *entry;
	struct list_head *tmp_entry;
	ocfs_inode *oin;
#endif

	LOG_ENTRY ();

#ifndef USERSPACE_TOOL
	offset = actual_disk_off;

	list_for_each_safe (entry, tmp_entry, &(osb->cache_lock_list)) {
		lockres = list_entry (entry, ocfs_lock_res, cache_list);
		if (lockres == NULL) {
			LOG_ERROR_STATUS(status = -EFAIL);
			goto bail;
		}

		ocfs_acquire_lockres (lockres);

		if ((lockres->sector_num >= actual_disk_off) &&
		    (lockres->sector_num < (actual_disk_off + length))) {
			LOG_TRACE_ARGS ("ocfs_lookup_cache_link has a valid "
				"entry in cache link for disk offset %u.%u\n",
				HI (lockres->sector_num), LO (lockres->sector_num));

			/* Change Lock type */
			fe = (ocfs_file_entry *)
				((__u8 *) buf + (lockres->sector_num - actual_disk_off));

			/* Flush */
			if (lockres->oin != NULL) {
				oin = lockres->oin;
				oin->cache_enabled = false;

				/* If the Open Handle Count is zero , then release the */
				/* lock and no need to flush as the data must already */
				/* be flushed */

				if (!(oin->oin_flags & OCFS_OIN_DIRECTORY)) {
					if (oin->open_hndl_cnt == 0)
						lockres->lock_type =
						    DISK_LOCK_FILE_LOCK (fe) =
						    OCFS_DLM_NO_LOCK;
					else
						ocfs_flush_cache (osb);
				}
			} else {
				/* Release the lock, as there will be no open */
				/* handle if there is no oin, and so we don't */
				/* need to keep the lock state to caching */
				lockres->lock_type = DISK_LOCK_FILE_LOCK (fe) =
								OCFS_DLM_NO_LOCK;
			}
			lockres->in_cache_list = false;
			list_del (entry);
		}
		ocfs_release_lockres (lockres);
	}
      bail:
#endif /* USERSPACE_TOOL */

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_lookup_cache_link */

/*
 * ocfs_process_log_file()
 * 
 * This is recovery. It will read the log file based on trans extent map and
 * do the actual disk writes of meta data at right disk offset.
 */
int ocfs_process_log_file (ocfs_super * osb, bool flag)
{
	int status = 0;
	__u8 *meta_data_buf = NULL;
	__u8 *tmp_buf = NULL;
	__u32 size;
	__u32 i = 0;
	ocfs_offset_map *map_buf;
	__u64 file_size;
	__u64 meta_file_size;
	__u64 meta_alloc_size;
	__u64 offset;

	LOG_ENTRY ();

	meta_alloc_size = 0;
	status = ocfs_get_system_file_size (osb,
					    (OCFS_FILE_VOL_META_DATA + osb->node_num),
					    &meta_file_size, &meta_alloc_size);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	size = OCFS_ALIGN (meta_file_size, osb->vol_layout.cluster_size);
	meta_data_buf = ocfs_malloc (size);
	if (meta_data_buf == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto finally;
	}

	status = ocfs_read_system_file (osb,
					(OCFS_FILE_VOL_META_DATA + osb->node_num),
					meta_data_buf, size, 0);
	if (status < 0) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto finally;
	}

	file_size = osb->log_file_size;

	size = 0;
	i = 0;

	while (meta_file_size != 0) {
		map_buf = (ocfs_offset_map *)
				(meta_data_buf + (i * sizeof (ocfs_offset_map)));

		if ((map_buf->length % OCFS_SECTOR_SIZE) ||
		    (map_buf->actual_disk_off % OCFS_SECTOR_SIZE)) {
			LOG_ERROR_STR ("length or actual_disk_off is not aligned");
		}

		if (size < map_buf->length) {
			ocfs_safefree (tmp_buf);
			size = OCFS_ALIGN (map_buf->length, osb->sect_size);
			tmp_buf = ocfs_malloc (size);
			if (tmp_buf == NULL) {
				LOG_ERROR_STATUS (status = -ENOMEM);
				goto finally;
			}
		}

		offset = map_buf->log_disk_off;

		status = ocfs_read_force_disk (osb, tmp_buf, map_buf->length,
					       offset);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}

		offset = map_buf->actual_disk_off;

		if (flag) {
			status = ocfs_lookup_cache_link (osb, tmp_buf,
					map_buf->actual_disk_off, map_buf->length);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto finally;
			}
		}

		status = ocfs_write_force_disk (osb, tmp_buf, map_buf->length,
						offset);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}

		meta_file_size -= sizeof (ocfs_offset_map);
		i++;
	}

      finally:
	ocfs_safefree (meta_data_buf);
	ocfs_safefree (tmp_buf);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_process_log_file */


/*
 * ocfs_break_cache_lock()
 *
 */
int ocfs_break_cache_lock (ocfs_super * osb, ocfs_lock_res * lockres,
			   ocfs_file_entry *fe)
{
	int status;
	int tmpstat;
	__u32 votemap;
	__u64 lockseqno = 0;
	bool disk_vote = false;
	unsigned long jif;
	bool disk_reset = true;
	__u32 flags = FLAG_FILE_RELEASE_CACHE | FLAG_FILE_ACQUIRE_LOCK;

	LOG_ENTRY_ARGS ("(osb=0x%p, lres=0x%p, fe=0x%p)\n", osb, lockres, fe);

	ocfs_acquire_lockres (lockres);

	/* Ask the node with cache to flush and revert to write thru on this file */
	votemap = (1 << lockres->master_node_num);

	ocfs_compute_lock_type_stats (&(osb->lock_type_stats),
				      OCFS_BREAK_CACHE_LOCK);

	jif = jiffies;
	status = -EAGAIN;
	while (status == -EAGAIN) {
		if (!IS_NODE_ALIVE (osb->publ_map, lockres->master_node_num,
				    OCFS_MAXIMUM_NODES)) {
			LOG_TRACE_ARGS ("Master (%u) is dead, lockid %u.%u\n",
				lockres->master_node_num, lockres->sector_num);
			/* TODO recovery needs to be done here .....and then become master */
			status = 0;
			goto finally;
		}

#ifndef USERSPACE_TOOL  
		if (comm_voting && !disk_vote) {
			LOG_TRACE_STR ("Network vote");
			jif = jiffies;
			status = ocfs_send_dlm_request_msg (osb, lockres->sector_num,
						    	lockres->lock_type, flags,
						    	lockres, votemap);
			if (status >= 0) {
				status = lockres->vote_status;
				if (status >= 0) {
					lockres->lock_type = OCFS_DLM_NO_LOCK;
					goto finally;
				} else if (status == -EAGAIN) {
					goto loop;
				} else {
					LOG_ERROR_STATUS (status);
					goto finito;
				}
			} else if (status == -ETIMEDOUT) {
				LOG_TRACE_STR ("Network voting timed out");
			}
			lockres->vote_state = 0;
		}
#endif

		LOG_TRACE_STR ("Disk vote");
		disk_vote = true;
		jif = jiffies;
		disk_reset = false;
		status = ocfs_request_vote (osb, lockres->sector_num,
					    lockres->lock_type, flags,
					    votemap, &lockseqno);
		if (status < 0) {
			if (status == -EAGAIN)
				goto reset;
			LOG_ERROR_STATUS (status);
			goto finally;
		}

		status = ocfs_wait_for_vote (osb, lockres->sector_num,
					     lockres->lock_type, flags,
					     votemap, 15000, lockseqno, lockres);
		if (status < 0) {
			if (status == -EAGAIN)
				goto reset;
			LOG_ERROR_STATUS (status);
			goto finally;
		}

              reset:
		tmpstat = ocfs_reset_voting (osb, lockres->sector_num,
					    lockres->lock_type, votemap);
		if (tmpstat < 0) {
			LOG_ERROR_STATUS (status = tmpstat);
			goto finito;
		}

		disk_reset = true;

		if (status != -EAGAIN)
			break;

#ifndef USERSPACE_TOOL
	      loop:
#endif
		LOG_TRACE_ARGS ("id=%u.%u\n", HILO(lockres->sector_num));
		ocfs_sleep (500);

		if (ocfs_task_interruptible (osb)) {
			LOG_TRACE_ARGS("interrupted.... lockid=%u.%u\n",
				       HILO(lockres->sector_num));
			status = -EINTR;
			goto finito;
		}
	}

	lockres->lock_type = (__u8) OCFS_DLM_NO_LOCK;

      finally:
	jif = jiffies - jif;
	LOG_TRACE_ARGS ("Lock time: %u\n", jif);

	if (disk_vote && !disk_reset) {
		tmpstat = ocfs_reset_voting (osb, lockres->sector_num,
					    lockres->lock_type, votemap);
		if (tmpstat < 0)
			LOG_ERROR_STATUS (tmpstat);
	}

      finito:
	ocfs_release_lockres (lockres);
	LOG_EXIT_STATUS (status);
	return (status);
}				/* ocfs_break_cache_lock */
