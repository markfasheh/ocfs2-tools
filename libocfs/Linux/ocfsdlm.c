/*
 * ocfsdlm.c
 *
 * Allows one dlm thread per mounted volume instead of one
 * for all volumes.
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
#define OCFS_DEBUG_CONTEXT OCFS_DEBUG_CONTEXT_NM

/*
 * ocfs_insert_sector_node()
 *
 */
int ocfs_insert_sector_node (ocfs_super * osb, ocfs_lock_res * lock_res,
			     ocfs_lock_res ** found_lock_res)
{
	int status = 0;
	__u32 tmp;

	LOG_ENTRY_ARGS ("(0x%p, 0x%p)\n", osb, lock_res);

	if (HASHTABLE_DESTROYED (&(osb->root_sect_node))) {
		LOG_TRACE_STATUS (status = -EFAIL);
		goto bail;
	}

	if (lock_res->signature != 0x55AA) {
		LOG_ERROR_STATUS (status = -EFAIL);
		goto bail;
	}

	if (!ocfs_hash_add (&(osb->root_sect_node), &(lock_res->sector_num),
			    sizeof (__u64), lock_res, sizeof (ocfs_lock_res *),
			    (void **)found_lock_res, &tmp)) {
		LOG_ERROR_STATUS(status = -EFAIL);
		goto bail;
	}

	if (*found_lock_res) {
		ocfs_get_lockres (*found_lock_res);
		LOG_TRACE_ARGS ("isn: fres=0x%p, ref=%d, lid=%u.%u\n",
				*found_lock_res,
				atomic_read (&((*found_lock_res)->lr_ref_cnt)),
				HILO((*found_lock_res)->sector_num));
	}
	else {
		ocfs_get_lockres (lock_res);
		LOG_TRACE_ARGS ("isn: lres=0x%p, ref=%d, lid=%u.%u\n", lock_res,
				atomic_read (&lock_res->lr_ref_cnt),
				HILO(lock_res->sector_num));
	}

      bail:
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_insert_sector_node */

/*
 * ocfs_lookup_sector_node()
 *
 */
int ocfs_lookup_sector_node (ocfs_super * osb, __u64 lock_id, ocfs_lock_res ** lock_res)
{
	int status = 0;
	__u32 len = 0;

	LOG_ENTRY_ARGS ("(0x%p, %u.%u, 0x%p)\n", osb, HI (lock_id),
			LO (lock_id), lock_res);

	if (HASHTABLE_DESTROYED (&(osb->root_sect_node))) {
		status = -EFAIL;
		LOG_TRACE_STATUS (status);
		goto bail;
	}

	if (ocfs_hash_get (&(osb->root_sect_node), &(lock_id), sizeof (__u64),
			 (void **) lock_res, &len)) {
		if (len != sizeof (ocfs_lock_res *)) {
			LOG_ERROR_STATUS (status = -EFAIL);
			goto bail;
		}

		if ((*lock_res)->signature != 0x55AA) {
			LOG_ERROR_STATUS (status = -EFAIL);
			goto bail;
		}

		ocfs_get_lockres (*lock_res);
		LOG_TRACE_ARGS ("lsn: lid=%u.%u, lres=0x%p, ref=%d\n",
				HILO(lock_id), *lock_res,
				atomic_read (&((*lock_res)->lr_ref_cnt)));
	} else
		status = -ENOENT;


      bail:
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_lookup_sector_node */

/*
 * ocfs_remove_sector_node()
 *
 */
void ocfs_remove_sector_node (ocfs_super * osb, ocfs_lock_res * lock_res)
{
	LOG_ENTRY_ARGS ("(0x%p, 0x%p)\n", osb, lock_res);

	if (HASHTABLE_DESTROYED (&(osb->root_sect_node))) {
		LOG_TRACE_STATUS (-EFAIL);
		goto bail;
	}

	if (lock_res->signature != 0x55AA) {
		LOG_ERROR_STATUS (-EFAIL);
		goto bail;
	}

	LOG_TRACE_ARGS ("rsn: lres=0x%p, ref=%d, lid=%u.%u\n", lock_res,
			atomic_read (&lock_res->lr_ref_cnt),
			HILO(lock_res->sector_num));

	ocfs_hash_del (&(osb->root_sect_node), &(lock_res->sector_num),
		       sizeof (__u64));

	ocfs_put_lockres (lock_res);

      bail:
	LOG_EXIT ();
	return ;
}				/* ocfs_remove_sector_node */

/*
 * ocfs_volume_thread()
 * 
 * Called by OcfsMountVolume(). This function is executed as a kernel thread
 * for each mounted ocfs volume.
 */
#ifdef USERSPACE_TOOL
void *ocfs_volume_thread (void *arg)
#else
int  ocfs_volume_thread (void *arg)
#endif

{
	ocfs_super *osb;
	char proc[16];
	int status = 0;
	int flush_counter = 0;
	__u32 disk_hb = 0;

	LOG_ENTRY ();

	osb = (ocfs_super *) arg;

	sprintf (proc, "ocfsnm-%d", osb->osb_id);
	ocfs_daemonize (proc, strlen(proc));

#ifdef USERSPACE_TOOL
	osb->dlm_task->thread = pthread_self();
#else
	osb->dlm_task = current;
#endif

	disk_hb = osb->vol_layout.disk_hb;

	/* The delay changes based on multiplier */
	while (!(OcfsGlobalCtxt.flags & OCFS_FLAG_SHUTDOWN_VOL_THREAD) &&
	       !(osb->osb_flags & OCFS_OSB_FLAGS_BEING_DISMOUNTED)) {

		if (OcfsGlobalCtxt.hbm == 0)
			OcfsGlobalCtxt.hbm = DISK_HBEAT_NO_COMM;

		ocfs_sleep (disk_hb);

		if ((OcfsGlobalCtxt.flags & OCFS_FLAG_SHUTDOWN_VOL_THREAD) ||
		    (osb->osb_flags & OCFS_OSB_FLAGS_BEING_DISMOUNTED))
			break;

		status = ocfs_nm_thread (osb);
		if (status < 0) {
			if (osb->osb_flags & OCFS_OSB_FLAGS_BEING_DISMOUNTED)
				break;
		}

		/* do a syncdev every 2 mins currently that is 500ms per cycle */
		if (flush_counter++ == 240) {
			fsync_no_super (osb->sb->s_dev);
			flush_counter = 0;
		}
	}
	
	complete (&(osb->complete));
	LOG_EXIT_LONG (0);
#ifdef USERSPACE_TOOL
	return NULL;
#else
	return 0;
#endif
}				/* ocfs_volume_thread */
