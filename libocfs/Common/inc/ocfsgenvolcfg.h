/*
 * ocfsgenvolcfg.h
 *
 * Function prototypes for related 'C' file.
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

#ifndef _OCFSGENVOLCFG_H_
#define _OCFSGENVOLCFG_H_

typedef struct _ocfs_cfg_task
{
	struct tq_struct cfg_tq;
	ocfs_super *osb;
	__u64 lock_off;
	__u8 *buffer;
}
ocfs_cfg_task;

typedef enum _ocfs_volcfg_op
{
	OCFS_VOLCFG_ADD,
	OCFS_VOLCFG_UPD
}
ocfs_volcfg_op;

void ocfs_worker (void *Arg);

void ocfs_assert_lock_owned (unsigned long Arg);

int ocfs_add_to_disk_config (ocfs_super * osb, __u32 pref_node_num,
			     ocfs_disk_node_config_info * NodeCfgInfo);

int ocfs_write_volcfg_header (ocfs_super * osb, ocfs_volcfg_op op);

void ocfs_volcfg_gblctxt_to_disknode(ocfs_disk_node_config_info *disk);

void ocfs_volcfg_gblctxt_to_node(ocfs_node_config_info *node);

int ocfs_config_with_disk_lock (ocfs_super * osb, __u64 LockOffset, __u8 * Buffer,
				__u32 node_num, ocfs_volcfg_op op);

int ocfs_release_disk_lock (ocfs_super * osb, __u64 LockOffset);

void ocfs_cfg_worker (ocfs_super * osb);

int ocfs_disknode_to_node (ocfs_node_config_info ** CfgInfo,
		 ocfs_disk_node_config_info * NodeCfgInfo);

int ocfs_update_disk_config (ocfs_super * osb, __u32 node_num,
			     ocfs_disk_node_config_info * disk);

int ocfs_chk_update_config (ocfs_super * osb);

int ocfs_add_node_to_config (ocfs_super * osb);

int ocfs_get_config (ocfs_super * osb);

bool ocfs_has_node_config_changed (ocfs_super * osb);

int ocfs_refresh_node_config (ocfs_super * osb);

void ocfs_show_all_node_cfgs (ocfs_super * osb);

#endif				/* _OCFSGENVOLCFG_H_ */
