/*
 * ocfsmount.c
 *
 * Mount and dismount volume
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
#else  /* ! KERNEL */
#include <libocfs.h>
#endif

/* Tracing */
#define OCFS_DEBUG_CONTEXT      OCFS_DEBUG_CONTEXT_MOUNT

static bool is_exclusive_node_alive (struct super_block *sb,
				     ocfs_vol_disk_hdr * hdr);

extern spinlock_t osb_id_lock;
extern __u32 osb_id;		/* Keeps track of next available OSB Id */
extern spinlock_t mount_cnt_lock;
extern __u32 mount_cnt;		/* Count of mounted volumes */
extern bool mount_cnt_inc;	/* true when mount_cnt is inc by 1 during first mount */

/*
 * ocfs_read_disk_header()
 *
 */
int ocfs_read_disk_header (__u8 ** buffer, struct super_block *sb)
{
	int status = 0;
#ifndef USERSPACE_TOOL
	struct buffer_head *bh = NULL;
#endif

	LOG_ENTRY ();

	if (buffer == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto leave;
	}

	/*  Read the first sector bytes from the target device */
	if ((*buffer = ocfs_malloc (1024)) == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto leave;
	}

#ifdef USERSPACE_TOOL
	lseek64(sb->s_dev, 0ULL, SEEK_SET);
	status = read(sb->s_dev, *buffer, 512);
	if (status < 512) {
		status = -EIO;
		goto leave;
	}
#else
	bh = bread (sb->s_dev, 0, 512);
	if (!bh) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto leave;
	}
	memcpy (*buffer, bh->b_data, 512);
	bforget (bh);
#endif

#ifdef USERSPACE_TOOL
	lseek64(sb->s_dev, 512ULL, SEEK_SET);
	status = read(sb->s_dev, (*buffer+512), 512);
	if (status < 512) {
		status = -EIO;
		goto leave;
	}
#else
	bh = bread (sb->s_dev, 1, 512);
	if (!bh) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto leave;
	}
	memcpy ((void *) (*buffer + 512), bh->b_data, 512);
	bforget (bh);
#endif

      leave:

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_read_disk_header */


/*
 * is_exclusive_node_alive()
 *
 */
static bool is_exclusive_node_alive (struct super_block *sb,
				     ocfs_vol_disk_hdr * hdr)
{
	bool ret = false;
	__u64 off;
	__u64 ts;
	ocfs_publish *pub;
#ifndef USERSPACE_TOOL
	struct buffer_head *bh = NULL;
#endif

	/* get the blocknum of the publish sector in question */
	off = (hdr->publ_off >> (__u64) 9);
	off += (__u64) hdr->excl_mount;

	/* get the timestamp from the publish sector */
#ifdef USERSPACE_TOOL
	pub = (ocfs_publish *)ocfs_malloc(512);
	if (pub == NULL) {
		LOG_ERROR_STR("out of memory");
		return true;
	}
	lseek64(sb->s_dev, off, SEEK_SET);
	if (read(sb->s_dev, pub, 512) != 512) {
		LOG_ERROR_STR("failed to read publish sector");
		free(pub);
		return true;
	}
	ts = pub->time;
#else
	bh = bread (sb->s_dev, (__u32) off, 512);
	if (!bh) {
		LOG_ERROR_ARGS ("failed to read block: %u\n", (__u32) off);
		return true;
	}
	pub = (ocfs_publish *) bh->b_data;
	ts = pub->time;
	bforget (bh);
#endif

	/* wait... */
	LOG_ERROR_STR ("sorry to have to do this, but you'll have to "
		"wait a bit while I check the other node...\n");
	ocfs_sleep (5000);	/* 5 seconds */

	/* get the timestamp from the publish sector */
#ifdef USERSPACE_TOOL
	lseek64(sb->s_dev, off, SEEK_SET);
	if (read(sb->s_dev, pub, 512) != 512) {
		LOG_ERROR_STR("failed to read publish sector");
		free(pub);
		return true;
	}
#else

	bh = bread (sb->s_dev, (__u32) off, 512);
	if (!bh) {
		LOG_ERROR_ARGS ("failed to read block: %u\n", (__u32) off);
		return true;
	}
	pub = (ocfs_publish *) bh->b_data;
#endif

	if (ts != pub->time) {
		/* aha! she's still there! */
		LOG_ERROR_ARGS
		    ("timestamp still changing, the node is alive!: %u.%u -> %u.%u\n",
		     HI (ts), LO (ts), HI (pub->time), LO (pub->time));
		ret = true;
	} else {
		LOG_ERROR_ARGS
		    ("timestamp NOT changing, the node is DEAD!: %u.%u -> %u.%u\n",
		     HI (ts), LO (ts), HI (pub->time), LO (pub->time));
		ret = false;
	}

#ifdef USERSPACE_TOOL
	free(pub);
#else
	bforget (bh);
#endif

	return ret;
}				/* is_exclusive_node_alive */


/*
 * ocfs_mount_volume()
 *
 */
int ocfs_mount_volume (struct super_block *sb, bool reclaim_id)
{
	int status = 0;
	ocfs_super *osb;
	__u8 *buffer = NULL;
	ocfs_vol_disk_hdr *volDiskHdr;
	ocfs_vol_label *volLabel;
	int sectsize;
#ifndef USERSPACE_TOOL
	int child_pid;
#endif

	LOG_ENTRY ();

	/* TODO: not using this yet, EVERYTHING assumes 512! */
	sectsize = OCFS_SECTOR_SIZE;

	status = ocfs_read_disk_header (&buffer, sb);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	volDiskHdr = (ocfs_vol_disk_hdr *) buffer;

	LOG_TRACE_STR ("ocfs_verify_volume...");
	status = ocfs_verify_volume (volDiskHdr);
	if (status < 0) {
		LOG_ERROR_ARGS ("Device (%u,%u) failed verification",
			       	MAJOR(sb->s_dev), MINOR(sb->s_dev));
		goto leave;
	}

	if (volDiskHdr->excl_mount != NOT_MOUNTED_EXCLUSIVE) {
		if (is_exclusive_node_alive (sb, volDiskHdr)) {
			LOG_ERROR_ARGS ("Cannot mount. Node %d has this "
					"volume mounted exclusive.\n",
					volDiskHdr->excl_mount);
			status = -EACCES;
			goto leave;
		} else {
			LOG_ERROR_ARGS ("Cannot mount. Node %d mounted this "
					"volume exclusive, but has DIED! "
					"Please recover.\n",
					volDiskHdr->excl_mount);
			status = -EACCES;
			goto leave;
		}
	}

	/* 2nd sector */
	volLabel = (ocfs_vol_label *) (buffer + sectsize);

	/* Check if the cluster name on the disk matches the one in the registry */
#ifdef ENABLE_CLUSTER_NAME_CHECK	/* TODO */
	if (OcfsGlobalCtxt.ClusterName == NULL ||
	    volLabel->ClusterNameLength < 1 ||
	    volLabel->ClusterName[0] == '\0' ||
	    memcmp (OcfsGlobalCtxt.ClusterName, volLabel->ClusterName,
		    volLabel->ClusterNameLength) != 0) {
		LOG_ERROR_ARGS
		    ("expected cluster name: '%s'  volume cluster name: '%s'\n",
		     OcfsGlobalCtxt.ClusterName, volLabel->ClusterName);
		status = -EINVAL;
		goto leave;
	}
#endif

	if ((osb = ocfs_malloc (sizeof (ocfs_super))) == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto leave;
	}
        memset(osb, 0, sizeof(ocfs_super));
        sb->u.generic_sbp = (void *)osb;
        osb->sb = sb;

	osb->reclaim_id = reclaim_id;

	status = ocfs_initialize_osb (osb, volDiskHdr, volLabel, sectsize);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}
        
	if (osb->vol_layout.root_start_off == 0 && osb->node_num != 0) {
		LOG_ERROR_ARGS("The volume must be mounted by node 0 before it can "
			       "be used and you are node %u", osb->node_num);
		status = -EINVAL;
		goto leave;
	}

	osb->sect_size = sectsize;

	spin_lock (&osb_id_lock);
	osb->osb_id = osb_id;
	if (osb_id < ULONG_MAX)
		osb_id++;
	else {
		spin_unlock (&osb_id_lock);
		LOG_ERROR_STR ("Too many volumes mounted");
		status = -ENOMEM;
		goto leave;
	}
	spin_unlock (&osb_id_lock);

	/* Launch the NM thread for the mounted volume */
	ocfs_down_sem (&(osb->osb_res), true);
#ifdef USERSPACE_TOOL
	osb->dlm_task = (struct task_struct *)ocfs_malloc(sizeof(struct task_struct));
	if (osb->dlm_task == NULL) {
		LOG_ERROR_STATUS(status = -ENOMEM); 
		goto leave;
	}
	if (pthread_create(&osb->dlm_task->thread, NULL,
       			   ocfs_volume_thread, osb) != 0) {
		LOG_ERROR_STATUS(status = -errno);
		goto leave;
	}
#else
	child_pid = kernel_thread (ocfs_volume_thread, osb,
				   CLONE_FS | CLONE_FILES | CLONE_SIGHAND);
	if (child_pid < 0) {
		LOG_ERROR_ARGS ("unable to launch ocfsnm thread (error=%d)\n",
				child_pid);
		ocfs_up_sem (&(osb->osb_res));
		status = -EFAIL;
		goto leave;
	}
#endif
	init_completion (&osb->complete);
	ocfs_up_sem (&(osb->osb_res));

#ifndef USERSPACE_TOOL
	/* Add proc entry for this volume */
	ocfs_proc_add_volume (osb);

	/* GlobalMountCount */
	spin_lock (&mount_cnt_lock);
	mount_cnt++;
	if (mount_cnt == 1) {
		/* Start the ipcdlm */
		ocfs_init_ipc_dlm (OCFS_UDP);
		OcfsIpcCtxt.init = true;
		if (mount_cnt_inc == false) {
			MOD_INC_USE_COUNT;
			mount_cnt_inc = true;
		}
	}
	spin_unlock (&mount_cnt_lock);
#endif

	/* wait for nm thread to be init */
	ocfs_wait (osb->nm_init_event,
		   (atomic_read (&osb->nm_init) >= OCFS_HEARTBEAT_INIT ), 0);

	/*  Join or Form the cluster... */
	LOG_TRACE_STR ("ocfs_vol_member_reconfig...");
	ocfs_down_sem (&(osb->osb_res), true);
	status = ocfs_vol_member_reconfig (osb);
	ocfs_up_sem (&(osb->osb_res));
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	/* Read the publish sector for this node and cleanup dirent being */
	/* modified when we crashed. */
	LOG_TRACE_STR ("ocfs_check_volume...");
	ocfs_down_sem (&(osb->osb_res), true);
	status = ocfs_check_volume (osb);
	ocfs_up_sem (&(osb->osb_res));
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	osb->vol_state = VOLUME_MOUNTED;

      leave:
	ocfs_safefree (buffer);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_mount_volume */


/*
 * lockres_hash_free_func()
 *
 */
static void lockres_hash_free_func (const void *p)
{
	/* Force remove the lockres */
	ocfs_free_lockres((ocfs_lock_res *)p);
}


/*
 * ocfs_dismount_volume()
 *
 */
int ocfs_dismount_volume (struct super_block *sb)
{
	int status = 0;
	bool AcquiredOSB = false;
	ocfs_super *osb = NULL;
	ocfs_inode *rootoin;
	__u32 nodemap;
	__u32 tempmap;
	int i;
	bool nm_killed = false;

	LOG_ENTRY_ARGS ("(0x%p)\n", sb);

	if (sb == NULL || sb->u.generic_sbp == NULL) {
		LOG_ERROR_STATUS (status = -EFAIL);
		goto leave;
	}

	osb = (ocfs_super *)(sb->u.generic_sbp);
	OCFS_ASSERT(IS_VALID_OSB(osb));
	rootoin = osb->oin_root_dir;

	ocfs_down_sem (&(osb->osb_res), true);
	AcquiredOSB = true;
	/* we shouldn't have to do the stuff below, vfs takes care of it */
#ifdef UMOUNT_CHECK 
	if (osb->file_open_cnt > 0) {
		LOG_ERROR_ARGS ("Dismount failed... file_open_cnt(%d) > 0\n",
				osb->file_open_cnt);
		LOG_ERROR_STR
		    ("WARNING!!! Need to uncomment this when file opens are correct!\n");
/*      commenting this out for now until we deal with open files properly */
		status = -EBUSY;
		goto leave;
	}
#endif
	LOG_TRACE_ARGS ("osb=0x%p rootoin=0x%p offset=%u.%u\n", osb,
			rootoin, rootoin->file_disk_off);

	fsync_no_super (sb->s_dev);

	ocfs_release_oin (rootoin, true);

	/* Destroy the Hash table */
	ocfs_hash_destroy (&(osb->root_sect_node), lockres_hash_free_func);

	/* Remove the proc element for this volume */
#ifndef USERSPACE_TOOL
	ocfs_proc_remove_volume (osb);
#endif

	/* Dismount */
	OCFS_SET_FLAG (osb->osb_flags, OCFS_OSB_FLAGS_BEING_DISMOUNTED);
	osb->vol_state = VOLUME_BEING_DISMOUNTED;

	/* Wait for this volume's NM thread to exit */
	if (osb->dlm_task) {
		LOG_TRACE_STR ("Waiting for ocfsnm to exit....");
#ifdef USERSPACE_TOOL
		pthread_join(osb->dlm_task->thread, NULL);
		ocfs_safefree(osb->dlm_task);
#else
		send_sig (SIGINT, osb->dlm_task, 0);
		wait_for_completion (&(osb->complete));
#endif
		osb->dlm_task = NULL;
		nm_killed = true;
	}

	/* create map of all active nodes except self */
	nodemap = (__u32)osb->publ_map;
	tempmap = (1 << osb->node_num);
	nodemap &= (~tempmap);

#ifndef USERSPACE_TOOL
	/* send dismount msg to all */
	if (nm_killed && OcfsIpcCtxt.task) {
		status = ocfs_send_dismount_msg (osb, (__u64)nodemap);
		if (status < 0)
			LOG_ERROR_STATUS (status);
	}

	/* decrement mount count */
	if (nm_killed) {
		spin_lock (&mount_cnt_lock);
		mount_cnt--;
		if (mount_cnt == 0) {
			/* Shutdown ocfslsnr */
			if (OcfsIpcCtxt.task) {
				LOG_TRACE_STR ("Waiting for ocfslsnr to exit....");
				send_sig (SIGINT, OcfsIpcCtxt.task, 0);
				wait_for_completion (&(OcfsIpcCtxt.complete));
				OcfsIpcCtxt.task = NULL;
			}
		}
		spin_unlock (&mount_cnt_lock);
	}
#endif

	ocfs_down_sem (&(OcfsGlobalCtxt.res), true);
	vfree (osb->cluster_bitmap.buf);
	ocfs_up_sem (&(OcfsGlobalCtxt.res));

	osb->vol_state = VOLUME_DISMOUNTED;
	if (AcquiredOSB) {
		ocfs_up_sem (&(osb->osb_res));
		AcquiredOSB = false;
	}

	if (nm_killed && osb->node_num != OCFS_INVALID_NODE_NUM)
		printk ("ocfs: Unmounting device (%s) on %s (node %d)\n",
		       	osb->dev_str,
		       	osb->node_cfg_info[osb->node_num]->node_name,
		       	osb->node_num);

	/* Free all nodecfgs */
	for (i = 0; i < OCFS_MAXIMUM_NODES; ++i) {
		ocfs_node_config_info *p;
		p = osb->node_cfg_info[i];
		ocfs_safefree (p);
	}

	ocfs_delete_osb (osb);
	ocfs_safefree (osb);
	sb->s_dev = 0;

#ifndef USERSPACE_TOOL
	spin_lock (&mount_cnt_lock);
	if (mount_cnt == 0 && atomic_read (&OcfsGlobalCtxt.cnt_lockres) == 0 &&
	    mount_cnt_inc == true) {
		MOD_DEC_USE_COUNT;
		mount_cnt_inc = false;
	}
	spin_unlock (&mount_cnt_lock);
#endif

      leave:
	if (AcquiredOSB) {
		ocfs_up_sem (&(osb->osb_res));
		AcquiredOSB = false;
	}

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_dismount_volume */
