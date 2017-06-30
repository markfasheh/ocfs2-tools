/*
 * chain.h
 *
 * Function prototypes, macros, etc. for related 'C' files
 *
 * Copyright (C) 2006 Oracle.  All rights reserved.
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301 USA.
 *
 */

#ifndef __CHAIN_H__
#define __CHAIN_H__

void mess_up_chains_list(ocfs2_filesys *fs, enum fsck_type type,
			 uint16_t slotnum);
void mess_up_chains_rec(ocfs2_filesys *fs, enum fsck_type type,
			uint16_t slotnum);
void mess_up_chains_inode(ocfs2_filesys *fs, enum fsck_type type,
			  uint16_t slotnum);
void mess_up_chains_group(ocfs2_filesys *fs, enum fsck_type type,
			  uint16_t slotnum);
void mess_up_chains_group_magic(ocfs2_filesys *fs, enum fsck_type type,
				uint16_t slotnum);
void mess_up_chains_cpg(ocfs2_filesys *fs, enum fsck_type type,
			uint16_t slotnum);
void mess_up_superblock_clusters_excess(ocfs2_filesys *fs, enum fsck_type type,
					uint16_t slotnum);
void mess_up_superblock_clusters_lack(ocfs2_filesys *fs, enum fsck_type type,
				      uint16_t slotnum);

#endif		/* __CHAIN_H__ */
