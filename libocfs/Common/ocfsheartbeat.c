/*
 * ocfsheartbeat.c
 *
 * Keeps track of alive nodes in the cluster.
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

#if !defined(USERSPACE_TOOL)
#include <ocfs.h>
#else
#include <libocfs.h>
#endif

/* Tracing */
#define OCFS_DEBUG_CONTEXT      OCFS_DEBUG_CONTEXT_HEARTBEAT

/*
 * ocfs_nm_heart_beat()
 *
 * @osb: ocfs super block for the volume
 * @flag: type of heart beat
 * @read_publish: if the publish sector needs to be re-read
 *
 * Updates the timestamp in the nodes publish sector.
 *
 * Returns 0 if success, < 0 if error.
 */
int ocfs_nm_heart_beat (ocfs_super * osb, __u32 flag, bool read_publish)
{
	ocfs_publish *publish = NULL;
	int status = 0;
	__u64 node_publ_off = 0;

	LOG_ENTRY_ARGS ("(0x%p, %u, %s)\n", osb, flag,
			read_publish ? "true" : "false");

	if (flag & HEARTBEAT_METHOD_DISK) {
		node_publ_off = osb->vol_layout.publ_sect_off +
			        (osb->node_num * osb->sect_size);

		if (read_publish) {
			status = ocfs_read_force_disk_ex (osb, (void **)&publish,
				  osb->sect_size, osb->sect_size, node_publ_off);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto finally;
			}
		} else {
			publish = (ocfs_publish *)
				(osb->cfg_prealloc +
				 ((OCFS_VOLCFG_NEWCFG_SECTORS + osb->node_num) *
				  osb->sect_size));
		}

		OcfsQuerySystemTime (&publish->time);

		publish->hbm[osb->node_num] = osb->hbm;

		spin_lock (&OcfsGlobalCtxt.comm_seq_lock);
		publish->comm_seq_num = OcfsGlobalCtxt.comm_seq_num;
		spin_unlock (&OcfsGlobalCtxt.comm_seq_lock);

		/* Write the current time in local node's publish sector */
		status = ocfs_write_force_disk (osb, publish, osb->sect_size,
						node_publ_off);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}
	}

	if (flag & HEARTBEAT_METHOD_IPC) {
		/* Plug this in later... */
	}

      finally:
	if (read_publish)
		ocfs_safefree (publish);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_nm_heart_beat */

/*
 * ocfs_nm_thread()
 *
 */
int ocfs_nm_thread (ocfs_super * mount_osb)
{
	int status = 0;
	__u8 *buffer = NULL;
	ocfs_super *osb = NULL;
	ocfs_publish *publish;
	ocfs_publish *publish_to_vote = NULL;
	__u32 i;
	__u32 highest_vote_node = 0;
	__u64 offset = 0;
	__u32 num_nodes = 0;
	__u32 vote_node = OCFS_INVALID_NODE_NUM;
	int ret = 0;
	ocfs_node_config_hdr *node_cfg_hdr = NULL;
	__u8 *p;
	__u64 curr_node_map;
	__u64 curr_publ_seq;

	LOG_ENTRY ();

	/* For each mounted volume reiterate the time stamp on the publish sector */
	if (!mount_osb) {
		LOG_ERROR_STATUS (status = -EFAIL);
		goto finally;
	} else
		osb = mount_osb;

	/* Ensure that the volume is valid ... */
	if (osb->obj_id.type != OCFS_TYPE_OSB)
		goto finally;

	/* ... and that it is mounted */
	if (osb->osb_flags & OCFS_OSB_FLAGS_BEING_DISMOUNTED)
		goto finally;
	if (!time_after (jiffies, osb->hbt))
		goto finally;

	if (osb->vol_state == VOLUME_MOUNTED) {
		if (osb->needs_flush) {
			ocfs_trans_in_progress(osb);
			if (osb->trans_in_progress == false) {
				ocfs_commit_cache (osb, false);
				osb->needs_flush = false;
			}
		}
	}

	/* lock publish to prevent overwrites from vote_req and vote_reset */
	down (&(osb->publish_lock));

	/* Get the Publish Sector start Offset */
	offset = osb->vol_layout.new_cfg_off;

	/* Read disk for Publish Sectors of all nodes */
	status = ocfs_read_force_disk (osb, osb->cfg_prealloc, osb->cfg_len,
				       offset);
	if (status < 0) {
		up (&(osb->publish_lock));
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	/* Update the timestamp on disk to indicate that it is alive */
	ocfs_nm_heart_beat (osb, HEARTBEAT_METHOD_DISK, false);

	/* release publish lock */
	up (&(osb->publish_lock));

	/* If another node was added to the config read and update the cfg */
	node_cfg_hdr = (ocfs_node_config_hdr *) (osb->cfg_prealloc + osb->sect_size);

	if ((osb->cfg_seq_num != node_cfg_hdr->cfg_seq_num) ||
	    (osb->num_cfg_nodes != node_cfg_hdr->num_nodes)) {
		down (&(osb->cfg_lock));
		status = ocfs_chk_update_config (osb);
		up (&(osb->cfg_lock));
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}
	}

	buffer = osb->cfg_prealloc +
			(OCFS_VOLCFG_NEWCFG_SECTORS * osb->sect_size);

	num_nodes = OCFS_MAXIMUM_NODES;

	/* Refresh the publish map */
	ocfs_update_publish_map (osb, buffer, false);

	/* send signal to mount thread to continue */
	if (atomic_read (&osb->nm_init) < OCFS_HEARTBEAT_INIT) {
		atomic_inc (&osb->nm_init);
	} else if (atomic_read (&osb->nm_init) == OCFS_HEARTBEAT_INIT) {
		wake_up (&osb->nm_init_event);
		atomic_inc (&osb->nm_init);
	}

	LOG_TRACE_ARGS ("Publish map: 0x%08x\n", LO (osb->publ_map));

	/* map of local node */
	curr_node_map = (__u64) ((__u64)1 << osb->node_num);
	curr_publ_seq = 0;

	/* Check for the highest node looking for a vote, if anybody is looking */
	for (i = 0, p = buffer; i < num_nodes; i++, p += osb->sect_size) {
		publish = (ocfs_publish *) p;

		if (publish->time == (__u64) 0)
			continue;

		if (publish->vote != FLAG_VOTE_NODE ||
		    !(publish->vote_map & curr_node_map))
			continue;

		LOG_TRACE_ARGS ("node(%u): vote=%d dirty=%d type=%u\n", i,
				publish->vote, publish->dirty, publish->vote_type);

		highest_vote_node = i;

		/* Check if the node is alive or not */
		if (IS_NODE_ALIVE (osb->publ_map, highest_vote_node, num_nodes)) {
			vote_node = highest_vote_node;
			publish_to_vote = publish;
			curr_publ_seq = publish->publ_seq_num;
		} else {
			status = ocfs_recover_vol (osb, highest_vote_node);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto finally;
			}
		}
	}

	if ((vote_node != OCFS_INVALID_NODE_NUM) &&
	    (vote_node != osb->node_num) &&
	    (curr_publ_seq != osb->last_disk_seq)) {
		publish = (ocfs_publish *)
				(buffer + (osb->node_num * osb->sect_size));
		if (publish->vote)
			publish->vote = 0;

		ocfs_process_vote (osb, publish_to_vote, vote_node);
		osb->last_disk_seq = curr_publ_seq;
	}
	osb->hbt = 50 + jiffies;

      finally:
	LOG_EXIT_STATUS (ret);
	return ret;
}				/* ocfs_nm_thread() */


/*
 * ocfs_update_publish_map()
 *
 * @osb: ocfs super block for the volume
 * @buffer: publish sectors read in the last round
 * @first_time: if true, the buffer needs to be initialized
 *
 * Reads the publish sectors and compares the timestamp of each node
 * to the one it read in the last round. As long as the timestamp keeps
 * changing, the node is marked alive. Conversely, if the timestamp does
 * not change over time, the node is marked dead. The function marks all
 * the live nodes in the publishmap.
 *
 */
void ocfs_update_publish_map (ocfs_super * osb, void *buffer, bool first_time)
{
	ocfs_publish *publish;
	ocfs_vol_node_map *node_map;
	__u64 curr_time = 0;
	__u32 i;
	__u32 num_nodes;
	__u8 *p;

	LOG_ENTRY_ARGS ("(0x%p, 0x%p, %u)\n", osb, buffer, first_time);

	num_nodes = OCFS_MAXIMUM_NODES;
	node_map = &(osb->vol_node_map);
	OcfsQuerySystemTime (&curr_time);

	/* First time thru, update buffer with timestamps for all nodes */
	if (first_time) {
		/* Read the last comm_seq_num */
		p = buffer + (osb->node_num * osb->sect_size);
		publish = (ocfs_publish *) p;
		spin_lock (&OcfsGlobalCtxt.comm_seq_lock);
		OcfsGlobalCtxt.comm_seq_num = publish->comm_seq_num + 10;
		spin_unlock (&OcfsGlobalCtxt.comm_seq_lock);
		/* Refresh local buffers */
		for (i = 0, p = (__u8 *) buffer; i < num_nodes;
		     i++, p += osb->sect_size) {
			publish = (ocfs_publish *) p;
			node_map->time[i] = publish->time;
			node_map->scan_rate[i] = publish->hbm[i];
			node_map->scan_time[i] = curr_time;
		}
		goto bail;	/* exit */
	}

	for (i = 0, p = (__u8 *) buffer; i < num_nodes;
	    				 i++, p += osb->sect_size) {
		publish = (ocfs_publish *) p;

		/* Loop if slot is unused */
		if (publish->time == (__u64) 0)
			continue;

		/* Check if the node is hung or not by comparing the disk */
		/* and memory timestamp values */
		if (node_map->time[i] == publish->time) {
			if (IS_NODE_ALIVE(osb->publ_map, i, num_nodes)) {
				if (atomic_read (&(node_map->dismount[i]))) {
					node_map->miss_cnt[i] = MISS_COUNT_VALUE;
					atomic_set (&(node_map->dismount[i]), 0);
				} else
					(node_map->miss_cnt[i])++;
				if (node_map->miss_cnt[i] > MISS_COUNT_VALUE) {
#if !defined(USERSPACE_TOOL)
					printk ("ocfs: Removing %s (node %d) "
						"from clustered device (%s)\n",
						osb->node_cfg_info[i]->node_name, i,
						osb->dev_str);
#endif
					UPDATE_PUBLISH_MAP (osb->publ_map, i,
					    OCFS_PUBLISH_CLEAR, num_nodes);
				}
			}
		} else {
#if !defined(USERSPACE_TOOL)
			if (!IS_NODE_ALIVE(osb->publ_map, i, num_nodes) &&
			    osb->node_num != i)
				printk ("ocfs: Adding %s (node %d) to "
					"clustered device (%s)\n",
				       	osb->node_cfg_info[i]->node_name, i,
					osb->dev_str);
#endif
			node_map->miss_cnt[i] = 0;
			node_map->time[i] = publish->time;
			UPDATE_PUBLISH_MAP (osb->publ_map, i, OCFS_PUBLISH_SET,
					    num_nodes);
			/* Update the multiple the other node wants us to beat */
			if ((publish->hbm[osb->node_num] != DISK_HBEAT_INVALID)
			    && (osb->hbm > publish->hbm[osb->node_num])) {
				/* Go to the lowest multiplier any of the nodes */
				/* alive want us to heartbeat with. */
				osb->hbm = publish->hbm[osb->node_num];

				if (osb->hbm == 0)
					osb->hbm = DISK_HBEAT_NO_COMM;

				if (OcfsGlobalCtxt.hbm > osb->hbm)
						OcfsGlobalCtxt.hbm = osb->hbm;

				if (OcfsGlobalCtxt.hbm == 0)
					OcfsGlobalCtxt.hbm = DISK_HBEAT_NO_COMM;
			}
		}
		node_map->scan_time[i] = curr_time;
	}

      bail:
	LOG_EXIT ();
	return;
}				/* ocfs_update_publish_map */
