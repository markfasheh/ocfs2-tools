/*
 * fsck_type.h
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 */



#ifndef _FSCK_TYPE_H
#define _FSCK_TYPE_H
/*
 * All these fsck types are copied form fsck.ocfs2/prompt-codes.h.
 * So if fsck.ocfs2 will implement a new fsck type, we need to copy 
 * it here and add some verification case to test its work.
 */
	
enum fsck_type
{ 	EB_BLKNO = 0,
	EB_GEN,
	EB_GEN_FIX,
	EXTENT_BLKNO_UNALIGNED,
	EXTENT_CLUSTERS_OVERRUN,
	EXTENT_EB_INVALID,
	EXTENT_LIST_DEPTH,
	EXTENT_LIST_COUNT,
	EXTENT_LIST_FREE,
	EXTENT_BLKNO_RANGE,
	CHAIN_CPG,
	SUPERBLOCK_CLUSTERS_EXCESS,
	SUPERBLOCK_CLUSTERS_LACK,
	GROUP_UNEXPECTED_DESC,
	GROUP_EXPECTED_DESC,
	GROUP_GEN,
	GROUP_PARENT,
	GROUP_BLKNO,
	GROUP_CHAIN,
	GROUP_FREE_BITS,
	CHAIN_COUNT,
	CHAIN_NEXT_FREE,
	CHAIN_EMPTY,
	CHAIN_I_CLUSTERS,
	CHAIN_I_SIZE,
	CHAIN_GROUP_BITS,
	CHAIN_HEAD_LINK_RANGE,
	CHAIN_LINK_GEN,
	CHAIN_LINK_MAGIC,
	CHAIN_LINK_RANGE,
	CHAIN_BITS,
	INODE_ALLOC_REPAIR,
	INODE_SUBALLOC,
	LALLOC_SIZE,
	LALLOC_NZ_USED,
	LALLOC_NZ_BM,
	LALLOC_BM_OVERRUN,
	LALLOC_BM_SIZE,
	LALLOC_BM_STRADDLE,
	LALLOC_USED_OVERRUN,
	LALLOC_CLEAR,
	DEALLOC_COUNT,
	DEALLOC_USED,
	TRUNCATE_REC_START_RANGE,
	TRUNCATE_REC_WRAP,
	TRUNCATE_REC_RANGE,
	INODE_GEN,
	INODE_GEN_FIX,
	INODE_BLKNO,
	INODE_LINK_NOT_CONNECTED,
	ROOT_NOTDIR,
	INODE_NZ_DTIME,
	LINK_FAST_DATA,
	LINK_NULLTERM,
	LINK_SIZE,
	LINK_BLOCKS,
	DIR_ZERO,
	INODE_SIZE,
	INODE_SPARSE_SIZE,
	INODE_CLUSTERS,
	INODE_SPARSE_CLUSTERS,
	LALLOC_REPAIR,
	LALLOC_USED,
	CLUSTER_ALLOC_BIT,
	DIRENT_DOTTY_DUP,
	DIRENT_NOT_DOTTY,
	DIRENT_DOT_INODE,
	DIRENT_DOT_EXCESS,
	DIRENT_ZERO,
	DIRENT_NAME_CHARS,
	DIRENT_INODE_RANGE,
	DIRENT_INODE_FREE,
	DIRENT_TYPE,
	DIR_PARENT_DUP,
	DIRENT_DUPLICATE,
	DIRENT_LENGTH,
	ROOT_DIR_MISSING,
	LOSTFOUND_MISSING,
	DIR_NOT_CONNECTED,
	DIR_DOTDOT,
	INODE_NOT_CONNECTED,
	INODE_COUNT,
	INODE_ORPHANED,
	CLUSTER_GROUP_DESC,
	INLINE_DATA_FLAG_INVALID,
	INLINE_DATA_COUNT_INVALID,
	INODE_INLINE_SIZE,
	INODE_INLINE_CLUSTERS,
	DUPLICATE_CLUSTERS,
	NUM_FSCK_TYPE
};

/* 
 * All the fsck type can be divided into following groups
 * so that we can implement one function to implement one group.
 * Currently there are following groups:
 *
 * Extent block error: EB_BLKNO, EB_GEN, EB_GEN_FIX, EXTENT_EB_INVALID
 *
 * Extent list error: EB_LIST_DEPTH, EXTENT_LIST_COUNT, EXTENT_LIST_FREE 
 *
 * Extent record error: EXTENT_BLKNO_UNALIGNED, EXTENT_CLUSTERS_OVERRUN, 
 *			EXTENT_BLKNO_RANGE
 *
 * Chain list error:	CHAIN_COUNT, CHAIN_NEXT_FREE
 *
 * Chain record error: CHAIN_EMPTY, CHAIN_HEAD_LINK_RANGE, CHAIN_BITS, CLUSTER_ALLOC_BIT
 *
 * Chain inode error: CHAIN_I_CLUSTERS, CHAIN_I_SIZE, CHAIN_GROUP_BITS
 *
 * Chain group error: CHAIN_LINK_GEN, CHAIN_LINK_RANGE
 *
 * Chain group magic error: CHAIN_LINK_MAGIC
 *
 * Group minor field error: GROUP_PARENT, GROUP_BLKNO, GROUP_CHAIN, GROUP_FREE_BITS
 *
 * Group generation error: GROUP_GEN
 *
 * Group list error: GROUP_UNEXPECTED_DESC, GROUP_EXPECTED_DESC
 *
 * Inode field error: 	INODE_SUBALLOC, INODE_GEN, INODE_GEN_FIX,INODE_BLKNO,
			INODE_NZ_DTIME, INODE_SIZE, INODE_SPARSE_SIZE,
*			INODE_CLUSTERS, INODE_SPARSE_CLUSTERS, INODE_COUNT
 *
 * Inode link not connected error: INODE_LINK_NOT_CONNECTED 
 *
 * Inode orphaned error:	INODE_ORPHANED
 *
 * Inode alloc error:	INODE_ALLOC_REPAIR
 *
 * Empty local alloc  error:	LALLOC_SIZE, LALLOC_NZ_USED, LALLOC_NZ_BM
 *
 * Local alloc bitmap error: 	LALLOC_BM_OVERRUN, LALLOC_BM_STRADDLE,LALLOC_BM_SIZE
 *
 * Local alloc used info error:	LALLOC_USED_OVERRUN, LALLOC_CLEAR

 * LALLOC_USED, LALLOC_REPAIR is recorded in fsck.ocfs2.checks.8.in,
 * but never find the solution in fsck.ocfs2 source code.
 * 
 * Truncate log list error: 	DEALLOC_COUNT, DEALLOC_USED
 *
 * Truncate log rec error: 	TRUNCATE_REC_START_RANGE, TRUNCATE_REC_WRAP,
 *				TRUNCATE_REC_RANGE
 *
 * Special files error: ROOT_NOTDIR, ROOT_DIR_MISSING, LOSTFOUND_MISSING,
 *			DIR_DOTDOT
 * 	
 * Link file error: LINK_FAST_DATA, LINK_NULLTERM, LINK_SIZE, LINK_BLOCKS
 *
 * Directory inode error: DIR_ZERO
 *
 * Dirent dot error:	DIRENT_DOTTY_DUP, DIRENT_NOT_DOTTY, DIRENT_DOT_INODE,
 *			DIRENT_DOT_EXCESS
 *
 * Dirent field error: 	DIRENT_ZERO, DIRENT_NAME_CHARS,DIRENT_INODE_RANGE,
 *			DIRENT_INODE_FREE, DIRENT_TYPE, DIRENT_DUPLICATE,
 *			DIRENT_LENGTH
 *
 * Directory parent duplicate error: DIR_PARENT_DUP
 *
 * Directory not connected error: DIR_NOT_CONNECTED
 *
 * Inline file:	INLINE_DATA_FLAG_INVALID, INLINE_DATA_COUNT_INVALID
 *		INODE_INLINE_SIZE, INODE_INLINE_CLUSTERS
 *
 */
#endif
