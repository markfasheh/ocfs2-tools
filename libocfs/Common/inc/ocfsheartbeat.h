/*
 * ocfsheartbeat.h
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

#ifndef _OCFSHEARTBEAT_H_
#define _OCFSHEARTBEAT_H_

typedef struct _ocfs_sched_vote
{
	ocfs_super * osb;
	__u32 node_num;
	struct tq_struct sv_tq;
	__u8 publish_sect[OCFS_SECTOR_SIZE];
}
ocfs_sched_vote;

void ocfs_update_publish_map (ocfs_super * osb, void *buffer, bool first_time);

int ocfs_nm_thread (ocfs_super * mount_osb);

int ocfs_nm_heart_beat (ocfs_super * osb, __u32 flag, bool read_publish);

int ocfs_schedule_vote (ocfs_super * osb, ocfs_publish * publish, __u32 node_num);

void ocfs_process_vote_worker (void * val);

#endif				/* _OCFSHEARTBEAT_H_ */
