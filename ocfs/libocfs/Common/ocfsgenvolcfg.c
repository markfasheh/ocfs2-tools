/* 
 * ocfsgenvolcfg.c
 *
 * Auto configuration, namely, node number.
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
void *assert_lock (void *arg);
#endif

/* Tracing */
#define  OCFS_DEBUG_CONTEXT  OCFS_DEBUG_CONTEXT_VOLCFG

/*
 * ocfs_worker()
 *
 * This function reiterates the lock on the disk from this node once
 * it has obtained it.
 */
void ocfs_worker (void *Arg)
{
	__u32 length;
	char *buffer;
	int status;
	ocfs_super *osb;
	__u64 offset;
	ocfs_cfg_task *cfg_task;

	LOG_ENTRY ();

	cfg_task = (ocfs_cfg_task *) Arg;

	/* Obtain the volume for which we need to reiterate the lock */
	osb = cfg_task->osb;
	buffer = cfg_task->buffer;
	length = osb->sect_size;
	offset = cfg_task->lock_off;

	/* Write the sector back */
	status = ocfs_write_disk (osb, buffer, length, offset);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		/* deliberate no exit jump here */
	}

	if (atomic_read (&osb->lock_stop)) {
		LOG_TRACE_ARGS ("Last Lock written : %d\n", jiffies);
		atomic_set (&osb->lock_event_woken, 1);
		wake_up (&osb->lock_event);
	} else {
#ifdef USERSPACE_TOOL
		{
			long j = jiffies + OCFS_VOLCFG_LOCK_ITERATE;
			while (j > jiffies) {
				sched_yield();
			}
		}
#else
		mod_timer (&osb->lock_timer, jiffies + OCFS_VOLCFG_LOCK_ITERATE);
#endif
	}

	LOG_EXIT ();
	return;
}				/* ocfs_worker */


#ifdef USERSPACE_TOOL
void *assert_lock (void *arg)
{
	ocfs_cfg_task *cfg_task = (ocfs_cfg_task *) arg;
	ocfs_super *osb = cfg_task->osb;

	while (1)
	{
		ocfs_worker(arg);
		if (atomic_read (&osb->lock_event_woken))
			return NULL;
	}
}
#else
/*
 * ocfs_assert_lock_owned()
 *
 * Routine called by a timer to reiterate the disk lock.
 */
void ocfs_assert_lock_owned (unsigned long Arg)
{
	ocfs_cfg_task *cfg_task;

	LOG_ENTRY ();

	cfg_task = (ocfs_cfg_task *) Arg;

	/* initialize the task */
	INIT_TQUEUE (&(cfg_task->cfg_tq), ocfs_worker, cfg_task);

	/* submit it */
	schedule_task (&cfg_task->cfg_tq);

	LOG_EXIT ();
	return ;
}				/* ocfs_assert_lock_owned */
#endif /* USERSPACE_TOOL */


/*
 * ocfs_add_to_disk_config()
 *
 */
int ocfs_add_to_disk_config (ocfs_super * osb, __u32 pref_node_num,
			     ocfs_disk_node_config_info * new_disk_node)
{
	int status = 0;
	__u64 offset;
	ocfs_disk_node_config_info *disk_node = NULL;
	__u8 *buffer = NULL;
	__u8 *p;
	__u32 node_num;
	__u32 sect_size;
	__u32 size;

	LOG_ENTRY ();

	sect_size = osb->sect_size;

	/* Read the nodecfg info for all nodes from disk */
	size = OCFS_VOLCFG_HDR_SECTORS * sect_size;
	offset = osb->vol_layout.node_cfg_off + size;
	size = osb->vol_layout.node_cfg_size - size;
	status = ocfs_read_disk_ex (osb, (void **) &buffer, size, size, offset);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	/* Check if preferred node num is available */
	node_num = OCFS_INVALID_NODE_NUM;
	if (pref_node_num >= 0 && pref_node_num < OCFS_MAXIMUM_NODES) {
		p = buffer + (pref_node_num * sect_size);
		disk_node = (ocfs_disk_node_config_info *)p;
		if (disk_node->node_name[0] == '\0')
			node_num = pref_node_num;
	}

	/* if not, find the first available empty slot */
	if (node_num == OCFS_INVALID_NODE_NUM) {
		p = buffer;
		for (node_num = 0; node_num < OCFS_MAXIMUM_NODES; ++node_num,
		     p += sect_size) {
			disk_node = (ocfs_disk_node_config_info *) p;
			if (disk_node->node_name[0] == '\0')
				break;
		}
	}

	/* If no free slots, error out */
	if (node_num >= OCFS_MAXIMUM_NODES) {
		LOG_ERROR_STR ("Unable to allocate node number as no slots " \
			       "are available");
		status = -ENOSPC;
		goto finally;
	}

	/* Copy the new nodecfg into the memory buffer */
	p = buffer + (node_num * sect_size);
	memcpy (p, new_disk_node, sect_size);

	/* Write the new node details on disk */
	size = (node_num + OCFS_VOLCFG_HDR_SECTORS) * sect_size;
	offset = osb->vol_layout.node_cfg_off + size;
	disk_node = (ocfs_disk_node_config_info *) p;
	status = ocfs_write_disk (osb, (void *) disk_node, sect_size, offset);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	/* Update the nodecfg hdr on disk */
	status = ocfs_write_volcfg_header (osb, OCFS_VOLCFG_ADD);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

      finally:
	ocfs_safefree (buffer);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_add_to_disk_config */

/*
 * ocfs_write_volcfg_header()
 *
 */
int ocfs_write_volcfg_header (ocfs_super * osb, ocfs_volcfg_op op)
{
	int status = 0;
	ocfs_node_config_hdr *hdr;
	__u8 *buffer = NULL;
	__u64 offset;

	LOG_ENTRY ();

	/* Read the nodecfg header */
	offset = osb->vol_layout.node_cfg_off;
	status = ocfs_read_disk_ex (osb, (void **) &buffer, osb->sect_size,
				    osb->sect_size, offset);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto bail;
	}

	hdr = (ocfs_node_config_hdr *) buffer;

	if (op == OCFS_VOLCFG_ADD)
		hdr->num_nodes++;

	/* Increment the seq# to trigger other nodes to re-read node cfg */
	hdr->cfg_seq_num++;

	/* Write the nodecfg header */
	status = ocfs_write_disk (osb, (void *) hdr, osb->sect_size, offset);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto bail;
	}

	/* Write the nodecfg hdr into the second sector of newcfg. */
	/* We do so so that we can read the nodecfg hdr easily when we */
	/* read the publish sector, for e.g. in ocfs_nm_thread() */
	offset = osb->vol_layout.new_cfg_off + osb->sect_size;
	status = ocfs_write_disk (osb, (void *) hdr, osb->sect_size, offset);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto bail;
	}

      bail:
	ocfs_safefree (buffer);
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_write_volcfg_header */

/*
 * ocfs_config_with_disk_lock()
 *
 * This function tries to obtain the lock on the disk for the volume
 * specified. The logic for obtaining a disk lock is as follows :
 *
 * Read the volcfg lock sector. If it is not locked, lock it by stamping
 * ones node number. Read the same sector after OCFS_VOLCFG_LOCK_TIME.
 * If the contents have not been modified, the lock is ours. Retain the
 * lock by reiterating the lock write operation every OCFS_VOLCFG_ITERATE_TIME.
 *
 * If the volcfg lock sector is owned by someone else, wait for
 * OCFS_VOLCFG_LOCK_TIME and read the lock sector again. If the lock sector
 * is owned by the same node as before attempt to break the lock as the
 * node may have died. If however, the lock sector is now owned by someone
 * else, wait for OCFS_VOLCFG_LOCK_TIME before repeating the entire exercise
 * again.
 *
 * Returns 0 if success, < 0 if error.
 */
int ocfs_config_with_disk_lock (ocfs_super * osb, __u64 LockOffset, __u8 * Buffer,
				__u32 node_num, ocfs_volcfg_op op)
{
	int status = 0;
	char *rd_buf = NULL;
	char *lock_buf = NULL;
	bool TriedAcquire = false;
	bool BreakLock = false;
	ocfs_disk_lock *DiskLock;
	ocfs_cfg_task *cfg_task = NULL;
	__u32 sect_size;
	__u64 lock_node_num = OCFS_INVALID_NODE_NUM;
#ifdef USERSPACE_TOOL
	bool thread_started = false;
	pthread_t thread;
#endif

	LOG_ENTRY ();

	sect_size = osb->sect_size;

	/* Allocate buffer for reading the disk */
	rd_buf = ocfs_malloc (osb->sect_size);
	if (rd_buf == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto finito;
	}

	cfg_task = ocfs_malloc (sizeof (ocfs_cfg_task));
	if (cfg_task == NULL)
	{
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto finito;
	}

	lock_buf = ocfs_malloc (sect_size);
	if (lock_buf == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto finito;
	}

	/* initialize cfg_task with info reqd to reiterate the volcfg lock */
	cfg_task->osb = osb;
	cfg_task->buffer = lock_buf;
	cfg_task->lock_off = LockOffset;

#ifndef USERSPACE_TOOL
	/* Initialize the kernel timer */
	init_timer(&osb->lock_timer);
	osb->lock_timer.function = ocfs_assert_lock_owned;
	osb->lock_timer.expires = 0;
	osb->lock_timer.data = (unsigned long) cfg_task;
#endif

	init_waitqueue_head (&osb->lock_event);
	atomic_set (&osb->lock_event_woken, 0);
	atomic_set (&osb->lock_stop, 0);

	while (1) {
		/* Read the volcfg lock sector */
		status = ocfs_read_disk (osb, rd_buf, sect_size, LockOffset);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finito;
		}

		DiskLock = (ocfs_disk_lock *) rd_buf;
		lock_node_num = DISK_LOCK_CURRENT_MASTER(DiskLock);

		if (DISK_LOCK_FILE_LOCK (DiskLock) == 0 || BreakLock) {
			if (DISK_LOCK_FILE_LOCK (DiskLock) != 0)
				LOG_TRACE_STR ("Try to break node config lock");
			else
				LOG_TRACE_STR ("Lock node config");

			/* Attempt to lock volcfg */
			DiskLock = (ocfs_disk_lock *) Buffer;
			DISK_LOCK_CURRENT_MASTER (DiskLock) = osb->node_num;
			DISK_LOCK_FILE_LOCK (DiskLock) = 1;

			/* Write into volcfg lock sector... */
			status = ocfs_write_disk (osb, Buffer, sect_size,
						  LockOffset);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto finito;
			}
			TriedAcquire = true;
		}

		ocfs_sleep (OCFS_VOLCFG_LOCK_TIME);

		/* Read the volcfg lock sector again... */
		status = ocfs_read_disk (osb, rd_buf, sect_size, LockOffset);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finito;
		}

		/* If we tried to acquire and we still own it we take it... */
		if ((TriedAcquire) && (memcmp (rd_buf, Buffer, sect_size) == 0)) {
			memcpy (lock_buf, Buffer, sect_size);

#ifdef USERSPACE_TOOL
			if (!thread_started) {
     				pthread_create(&thread, NULL, assert_lock, cfg_task);
				thread_started=true;
			}
#else
			/* Set timer to reiterate lock every few jiffies */
			LOG_TRACE_ARGS ("Start Timer: %d\n", jiffies);
			osb->lock_timer.expires = jiffies +
						  OCFS_VOLCFG_LOCK_ITERATE;
			add_timer(&osb->lock_timer);
#endif

			/* Write the config info into the disk */
			DiskLock = (ocfs_disk_lock *) Buffer;
			DISK_LOCK_CURRENT_MASTER (DiskLock) =
							OCFS_INVALID_NODE_NUM;
			DISK_LOCK_FILE_LOCK (DiskLock) = 0;

			if (op == OCFS_VOLCFG_ADD)
				status = ocfs_add_to_disk_config (osb, node_num,
					(ocfs_disk_node_config_info *) Buffer);
			else if (op == OCFS_VOLCFG_UPD)
				status = ocfs_update_disk_config (osb, node_num,
					 (ocfs_disk_node_config_info *) Buffer);
			else
				status = -EFAIL;
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto finito;
			}
			break;
		} else {
			DiskLock = (ocfs_disk_lock *) rd_buf;
			if (DISK_LOCK_CURRENT_MASTER (DiskLock) == lock_node_num)
				BreakLock = true;
			else {
				LOG_TRACE_ARGS ("Node config locked by node: %d\n",
					DISK_LOCK_CURRENT_MASTER (DiskLock));
				ocfs_sleep (OCFS_VOLCFG_LOCK_TIME);
			}
		}
	}

      finito:
	ocfs_release_disk_lock (osb, LockOffset);
#ifdef USERSPACE_TOOL
	if (thread_started) {
		void *ret=NULL;
		pthread_join(thread, &ret);
	}
#endif

	ocfs_safefree (rd_buf);
	ocfs_safefree (lock_buf);
	ocfs_safefree (cfg_task);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_config_with_disk_lock */

/*
 * ocfs_release_disk_lock()
 *
 * This function Cancels the timer to reiterate we own the disk lock and
 * then frees it by writing the sector for the disk lock.
 *
 * Returns 0 if success, < 0 if error.
 */
int ocfs_release_disk_lock (ocfs_super * osb, __u64 LockOffset)
{
	int status = 0;
	__s8 *buffer = NULL;
	__u32 sect_size = osb->sect_size;

	LOG_ENTRY ();

	buffer = ocfs_malloc (sect_size);
	if (buffer == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto finally;
	}

	/* reset lock... */
	memset (buffer, 0, sect_size);

	/* Cancel the timer so that we don't reiterate the lock anymore */
	LOG_TRACE_STR ("Waiting for osb->lock_event");
	atomic_set (&osb->lock_stop, 1);
	ocfs_wait (osb->lock_event, atomic_read (&osb->lock_event_woken), 0);
	atomic_set (&osb->lock_event_woken, 0);
#ifndef USERSPACE_TOOL
	del_timer_sync(&osb->lock_timer);
#endif

	/* Release the lock */
	status = ocfs_write_disk (osb, buffer, sect_size, LockOffset);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

      finally:
	ocfs_safefree (buffer);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_release_disk_lock */

/*
 * ocfs_add_node_to_config()
 *
 */
int ocfs_add_node_to_config (ocfs_super * osb)
{
	int status = 0;
	ocfs_disk_node_config_info *disk;
	void *buffer = NULL;
	__u64 offset;
	__u32 sect_size = osb->sect_size;

	LOG_ENTRY ();

	buffer = ocfs_malloc (sect_size);
	if (buffer == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto bail;
	} else
		memset (buffer, 0, sect_size);

	disk = (ocfs_disk_node_config_info *) buffer;

	/* populate the disknodecfg info from global context */
	ocfs_volcfg_gblctxt_to_disknode (disk);

	/* Write this nodes config onto disk */
	offset = osb->vol_layout.new_cfg_off;
	status = ocfs_config_with_disk_lock (osb, offset, (__u8 *) disk,
					     OcfsGlobalCtxt.pref_node_num,
					     OCFS_VOLCFG_ADD);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto bail;
	}

	status = ocfs_chk_update_config (osb);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto bail;
	}

      bail:
	ocfs_safefree (buffer);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_add_node_to_config */

/*
 * ocfs_disknode_to_node()
 *
 */
int ocfs_disknode_to_node (ocfs_node_config_info ** node,
			  ocfs_disk_node_config_info * disk)
{
	int status = 0;

	LOG_ENTRY ();

	if (*node == NULL) {
		if ((*node = (ocfs_node_config_info *)
		     ocfs_malloc (sizeof (ocfs_node_config_info))) == NULL) {
			LOG_ERROR_STATUS (status = -ENOMEM);
			goto bail;
		}
		memset (*node, 0, sizeof (ocfs_node_config_info));
	}

	strncpy ((*node)->node_name, disk->node_name, MAX_NODE_NAME_LENGTH);

	memcpy((*node)->guid.guid, disk->guid.guid, GUID_LEN);

	(*node)->ipc_config.type = disk->ipc_config.type;
	(*node)->ipc_config.ip_port = disk->ipc_config.ip_port;
	strncpy((*node)->ipc_config.ip_addr, disk->ipc_config.ip_addr,
		MAX_IP_ADDR_LEN);
	strncpy((*node)->ipc_config.ip_mask, disk->ipc_config.ip_mask,
		MAX_IP_ADDR_LEN);

      bail:
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_disknode_to_node */

/*
 * ocfs_update_disk_config()
 *
 */
int ocfs_update_disk_config (ocfs_super * osb, __u32 node_num,
			     ocfs_disk_node_config_info * disk)
{
	int status = 0;
	__u64 offset;

	LOG_ENTRY ();

	/* Write the node details */
	offset = osb->vol_layout.node_cfg_off +
		 ((node_num + OCFS_VOLCFG_HDR_SECTORS) * osb->sect_size);
	status = ocfs_write_disk (osb, (void *) disk, osb->sect_size, offset);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	status = ocfs_write_volcfg_header (osb, OCFS_VOLCFG_UPD);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

      finally:
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_update_disk_config */

/*
 * ocfs_volcfg_gblctxt_to_disknode()
 *
 */
void ocfs_volcfg_gblctxt_to_disknode(ocfs_disk_node_config_info *disk)
{
	ocfs_ipc_config_info *ipc;
	ocfs_comm_info *g_ipc;

	LOG_ENTRY ();

	ipc = &(disk->ipc_config);
	g_ipc = &(OcfsGlobalCtxt.comm_info);

	if (OcfsGlobalCtxt.node_name)
		strncpy (disk->node_name, OcfsGlobalCtxt.node_name,
			 MAX_NODE_NAME_LENGTH);

	memcpy(disk->guid.guid, OcfsGlobalCtxt.guid.guid, GUID_LEN);

	ipc->type = g_ipc->type;
	ipc->ip_port = g_ipc->ip_port;
	if (g_ipc->ip_addr)
		strncpy (ipc->ip_addr, g_ipc->ip_addr, MAX_IP_ADDR_LEN);
	if (g_ipc->ip_mask)
		strncpy (ipc->ip_mask, g_ipc->ip_mask, MAX_IP_ADDR_LEN);

	LOG_EXIT ();
	return ;
}				/* ocfs_volcfg_gblctxt_to_disknode */

/*
 * ocfs_volcfg_gblctxt_to_node()
 *
 */
void ocfs_volcfg_gblctxt_to_node(ocfs_node_config_info *node)
{
	ocfs_ipc_config_info *ipc;
	ocfs_comm_info *g_ipc;

	LOG_ENTRY ();

	ipc = &(node->ipc_config);
	g_ipc = &(OcfsGlobalCtxt.comm_info);

	if (OcfsGlobalCtxt.node_name)
		strncpy (node->node_name, OcfsGlobalCtxt.node_name,
			 MAX_NODE_NAME_LENGTH);

	memcpy(node->guid.guid, OcfsGlobalCtxt.guid.guid, GUID_LEN);

	ipc->type = g_ipc->type;
	ipc->ip_port = g_ipc->ip_port;
	if (g_ipc->ip_addr)
		strncpy (ipc->ip_addr, g_ipc->ip_addr, MAX_IP_ADDR_LEN);
	if (g_ipc->ip_mask)
		strncpy (ipc->ip_mask, g_ipc->ip_mask, MAX_IP_ADDR_LEN);

	LOG_EXIT ();
	return ;
}				/* ocfs_volcfg_gblctxt_to_node */

/*
 * ocfs_chk_update_config()
 *
 */
int ocfs_chk_update_config (ocfs_super * osb)
{
	int status = 0;
	ocfs_node_config_hdr *hdr = NULL;
	ocfs_disk_node_config_info *disk = NULL;
	__u8 *buffer = NULL;
	__u64 offset;
	__s32 i;
	__u32 sect_size = osb->sect_size;
	__u8 *p;

	LOG_ENTRY ();

	/* Read in the config on the disk */
	offset = osb->vol_layout.node_cfg_off;
	status = ocfs_read_disk_ex (osb, (void **) &buffer,
				    (__u32)osb->vol_layout.node_cfg_size,
				    (__u32)osb->vol_layout.node_cfg_size, offset);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	/* 1st block in buffer is the NodeCfgHdr */
	hdr = (ocfs_node_config_hdr *) buffer;

	if (strncmp (hdr->signature, NODE_CONFIG_HDR_SIGN,
		     NODE_CONFIG_SIGN_LEN)) {
		LOG_ERROR_STR ("Invalid node config signature");
		status = -EINVAL;
		goto finally;
	}

	if  (hdr->version < NODE_MIN_SUPPORTED_VER ||
	     hdr->version > NODE_CONFIG_VER) {
		LOG_ERROR_ARGS ("Node config version mismatch, (%d) < minimum" \
			        " (%d) or > current (%d)", hdr->version,
			        NODE_MIN_SUPPORTED_VER, NODE_CONFIG_VER);
		status = -EINVAL;
		goto finally;
	}

	/* Exit if nodecfg on disk has remained unchanged... */
	if ((osb->cfg_initialized) && (osb->cfg_seq_num == hdr->cfg_seq_num) &&
	    (osb->num_cfg_nodes == hdr->num_nodes))
		goto finally;

	/* ... else refresh nodecfg in memory */
	p = buffer + (OCFS_VOLCFG_HDR_SECTORS * sect_size);

	/* Read the nodecfg for all possible nodes as there may be holes */
	/* i.e., node numbers need not be dolled out in sequence */
	for (i = 0; i < OCFS_MAXIMUM_NODES; i++, p += sect_size) {
		disk = (ocfs_disk_node_config_info *) p;

		if (disk->node_name[0] == '\0')
			continue;

		status = ocfs_disknode_to_node (&osb->node_cfg_info[i], disk);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}

		/* If nodenum is set, goto next node */
		if (osb->node_num != OCFS_INVALID_NODE_NUM)
			continue;

		/*
		 * If node num is not set, set it if guid matches.
		 * If guid does not match and the hostid also does not
		 * match, goto next slot.
		 * However if the guid does not natch but the hostid
		 * matches, it means that the user re-ran ocfs_uid_gen
		 * with the -r option to reclaim its node number. In
		 * this case, allow the reclaim only if the user mounts
		 * the volume with the reclaimid option. Else, error.
		 */
		if (!memcmp(&OcfsGlobalCtxt.guid.guid, disk->guid.guid,
			    GUID_LEN)) {
			osb->node_num = i;
			continue;
		}

		/* If the hostid does not match, goto next... */
		if (memcmp(&OcfsGlobalCtxt.guid.id.host_id,
			   disk->guid.id.host_id, HOSTID_LEN))
			continue;

		/* ...else allow node to reclaim the number if reclaimid set */
		if (osb->reclaim_id) {
			osb->node_num = i;
			/* Write this node's cfg with the new guid on disk */
			status = ocfs_refresh_node_config (osb);
			if (status < 0) {
				LOG_ERROR_STATUS(status);
				goto finally;
			}
		}
		else {
			LOG_ERROR_STR("Re-mount volume with the reclaimid " \
				      "option to reclaim the node number");
			status = -EFAIL;
			goto finally;
		}
	}

	osb->cfg_initialized = true;
	osb->cfg_seq_num = hdr->cfg_seq_num;
	osb->num_cfg_nodes = hdr->num_nodes;
	LOG_TRACE_ARGS ("Num of configured nodes (%u)\n", osb->num_cfg_nodes);
	IF_TRACE(ocfs_show_all_node_cfgs (osb));

      finally:
	ocfs_safefree (buffer);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_chk_update_config */

/*
 * ocfs_get_config()
 *
 */
int ocfs_get_config (ocfs_super * osb)
{
	int status = 0;

	LOG_ENTRY ();

	/* Update our config info for this volume from the disk */
	status = ocfs_chk_update_config (osb);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto bail;
	}

	if (osb->node_num == OCFS_INVALID_NODE_NUM) {
		if (osb->reclaim_id) {
			LOG_ERROR_STR ("unable to reclaim id");
			status = -EINVAL;
			goto bail;
		}
		status = ocfs_add_node_to_config (osb);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto bail;
		}
	} else {
		if (ocfs_has_node_config_changed (osb)) {
			status = ocfs_refresh_node_config (osb);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto bail;
			}
		}
	}

	LOG_TRACE_ARGS ("Node Num: %d\n", osb->node_num);

      bail:
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_get_config */

/*
 * ocfs_has_node_config_changed()
 *
 */
bool ocfs_has_node_config_changed (ocfs_super * osb)
{
	ocfs_node_config_info *node;
	ocfs_ipc_config_info *ipc;
	ocfs_comm_info *g_ipc;
	bool chg = false;

	LOG_ENTRY ();

	node = osb->node_cfg_info[osb->node_num];
	ipc = &(node->ipc_config);
	g_ipc = &(OcfsGlobalCtxt.comm_info);

	if (OcfsGlobalCtxt.node_name &&
	    strncmp (node->node_name, OcfsGlobalCtxt.node_name,
		     MAX_NODE_NAME_LENGTH))
		chg = true;

	if (!chg && ipc->type != g_ipc->type)
		chg = true;

	if (!chg && ipc->ip_port != g_ipc->ip_port)
		chg = true;

	if (!chg && g_ipc->ip_addr &&
	    strncmp (ipc->ip_addr, g_ipc->ip_addr, MAX_IP_ADDR_LEN))
		chg = true;

	if (!chg && g_ipc->ip_mask &&
	    strncmp (ipc->ip_mask, g_ipc->ip_mask, MAX_IP_ADDR_LEN))
		chg = true;

	LOG_EXIT_LONG (chg);
	return chg;
}				/* ocfs_has_node_config_changed */

/*
 * ocfs_refresh_node_config()
 *
 */
int ocfs_refresh_node_config (ocfs_super * osb)
{
	ocfs_node_config_info *node;
	ocfs_disk_node_config_info *disk;
	__u64 offset;
	__u8 *buffer = NULL;
	int status = 0;

	LOG_ENTRY ();

	buffer = ocfs_malloc (osb->sect_size);
	if (buffer == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto bail;
	}

	memset ((void *) buffer, 0, osb->sect_size);
	disk = (ocfs_disk_node_config_info *) buffer;

	/* populate the nodecfg info in disk from global context */
	ocfs_volcfg_gblctxt_to_disknode (disk);

	/* populate the nodecfg info in mem from global context */
	node = osb->node_cfg_info[osb->node_num];
	ocfs_volcfg_gblctxt_to_node (node);

	/* Update the nodecfg on disk with the new info */
	offset = osb->vol_layout.new_cfg_off;
	status = ocfs_config_with_disk_lock (osb, offset, (__u8 *) disk,
					     osb->node_num, OCFS_VOLCFG_UPD);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto bail;
	}

      bail:
	ocfs_safefree(buffer);
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_refresh_node_config */

/*
 * ocfs_show_all_node_cfgs()
 *
 */
void ocfs_show_all_node_cfgs (ocfs_super * osb)
{
	ocfs_node_config_info *node;
	__u32 i;

	for (i = 0; i < OCFS_MAXIMUM_NODES; i++) {
		node = osb->node_cfg_info[i];

		if (!node || node->node_name[0] == '\0')
			continue;

		LOG_TRACE_ARGS ("Node (%u) is (%s)\n", i, node->node_name);
		LOG_TRACE_ARGS ("ip=%s, port=%d\n", node->ipc_config.ip_addr,
				node->ipc_config.ip_port);
	}

	return;
}				/* ocfs_show_all_node_cfgs */
