/*
 * ocfsdlmp.h
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

#ifndef _OCFSDLMP_H_
#define _OCFSDLMP_H_

int ocfs_insert_sector_node (ocfs_super * osb, ocfs_lock_res * lock_res,
			     ocfs_lock_res ** found_lockres);

int ocfs_lookup_sector_node (ocfs_super * osb, __u64 lock_id, ocfs_lock_res ** lock_res);

void ocfs_remove_sector_node (ocfs_super * osb, ocfs_lock_res * lock_res);

#ifdef USERSPACE_TOOL
void *ocfs_volume_thread (void *arg);
#else
int ocfs_volume_thread (void *arg);
#endif

#endif				/* _OCFSDLMP_H_ */
