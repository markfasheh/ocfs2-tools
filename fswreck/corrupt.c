/*
 * corrupt.c
 *
 * corruption routines
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

#include <main.h>

extern char *progname;

/*
 * corrupt_chains()
 *
 */
void corrupt_chains(ocfs2_filesys *fs, int code, uint16_t slotnum)
{
	errcode_t ret;
	uint64_t blkno;
	struct ocfs2_super_block *sb = OCFS2_RAW_SB(fs->fs_super);
	char sysfile[40];

	switch (code) {
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
	case 8:
	case 10:
	case 11:
	case 12:
		snprintf(sysfile, sizeof(sysfile),
			 ocfs2_system_inodes[GLOBAL_BITMAP_SYSTEM_INODE].si_name);
		break;
#ifdef _LATER_
	case X:
		snprintf(sysfile, sizeof(sysfile),
			 ocfs2_system_inodes[GLOBAL_INODE_ALLOC_SYSTEM_INODE].si_name);
		break;
	case Y: 
		snprintf(sysfile, sizeof(sysfile),
			 ocfs2_system_inodes[EXTENT_ALLOC_SYSTEM_INODE].si_name, slotnum);
		break;
	case Z:
		snprintf(sysfile, sizeof(sysfile),
			 ocfs2_system_inodes[INODE_ALLOC_SYSTEM_INODE].si_name, slotnum);
		break;
#endif
	default:
		FSWRK_FATAL("Invalid code=%d", code);
	}

	ret = ocfs2_lookup(fs, sb->s_system_dir_blkno, sysfile,
			   strlen(sysfile), NULL, &blkno);
	if (ret)
		FSWRK_FATAL();

	mess_up_chains(fs, blkno, code);

	return ;
}

static void create_directory(ocfs2_filesys *fs, char *dirname, uint64_t *blkno)
{
	errcode_t ret;
	struct ocfs2_super_block *sb = OCFS2_RAW_SB(fs->fs_super);

	ret = ocfs2_lookup(fs, sb->s_root_blkno, dirname, strlen(dirname), NULL,
			   blkno);
	if (!ret)
		return;
	else if (ret != OCFS2_ET_FILE_NOT_FOUND)
		FSWRK_COM_FATAL(progname, ret);

	ret  = ocfs2_new_inode(fs, blkno, S_IFDIR | 0755);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_expand_dir(fs, *blkno, fs->fs_root_blkno);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_link(fs, fs->fs_root_blkno, dirname, *blkno, OCFS2_FT_DIR);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	return;
}

void corrupt_file(ocfs2_filesys *fs, int code, uint16_t slotnum)
{
	void (*func)(ocfs2_filesys *fs, uint64_t blkno) = NULL;
	uint64_t blkno;

	switch (code) {
	case CORRUPT_EXTENT_BLOCK:
		func = mess_up_extent_block;
		break;
	case CORRUPT_EXTENT_LIST:
		func = mess_up_extent_list;
		break;
	case CORRUPT_EXTENT_REC:
		func = mess_up_extent_record;
		break;
	case CORRUPT_INODE_FIELD:
		func = mess_up_inode_field;
		break; 
	case CORRUPT_INODE_NOT_CONNECTED:
		func = mess_up_inode_not_connected;
		break;
	case CORRUPT_SYMLINK:
		func = mess_up_symlink;
		break;
	case CORRUPT_SPECIAL_FILE:
		func = mess_up_root;
		break;
	case CORRUPT_DIR_INODE:
		func = mess_up_dir_inode;
		break;
	case CORRUPT_DIR_DOT:
		func = mess_up_dir_dot;
		break;
	case CORRUPT_DIR_ENT:
		func = mess_up_dir_ent;
		break;
	case CORRUPT_DIR_PARENT_DUP:
		func = mess_up_dir_parent_dup;
		break;
	case CORRUPT_DIR_NOT_CONNECTED:
		func = mess_up_dir_not_connected;
		break;
	default:
		FSWRK_FATAL("Invalid code=%d", code);
	}

	create_directory(fs, "tmp", &blkno);

	if (func)
		func(fs, blkno);

	return;
}

void corrupt_sys_file(ocfs2_filesys *fs, int code, uint16_t slotnum)
{
	void (*func)(ocfs2_filesys *fs, uint16_t slotnum) = NULL;

	switch (code) {
	case CORRUPT_CHAIN_LIST:
		func = mess_up_chains_list;
		break;
	case CORRUPT_CHAIN_REC:
		func = mess_up_chains_rec;
		break;
	case CORRUPT_CHAIN_INODE:
		func = mess_up_chains_inode;
		break;
	case CORRUPT_CHAIN_GROUP:
		func = mess_up_chains_group;
		break;
	case CORRUPT_CHAIN_GROUP_MAGIC:
		func = mess_up_chains_group_magic;
		break;
	case CORRUPT_CHAIN_CPG:
		func = mess_up_chains_cpg;
		break;
	case CORRUPT_SUPERBLOCK_CLUSTERS_EXCESS:
		func = mess_up_superblock_clusters_excess;
		break;
	case CORRUPT_SUPERBLOCK_CLUSTERS_LACK:
		func = mess_up_superblock_clusters_lack;
		break;
	case CORRUPT_INODE_ORPHANED:
		func = mess_up_inode_orphaned;
		break;
	case CORRUPT_INODE_ALLOC_REPAIR:
		func = mess_up_inode_alloc;
		break;
	default:
		FSWRK_FATAL("Invalid code=%d", code);
	}

	if (func)
		func(fs, slotnum);

	return;
}

void corrupt_group_desc(ocfs2_filesys *fs, int code, uint16_t slotnum)
{
	void (*func)(ocfs2_filesys *fs, uint16_t slotnum) = NULL;

	switch (code) {
	case CORRUPT_GROUP_MINOR:
		func = mess_up_group_minor;
		break;
	case CORRUPT_GROUP_GENERATION:
		func = mess_up_group_gen;
		break;
	case CORRUPT_GROUP_LIST:
		func = mess_up_group_list;
		break;
	case CORRUPT_CLUSTER_AND_GROUP_DESC:
		func = mess_up_cluster_group_desc;
		break;
	default:
		FSWRK_FATAL("Invalid code=%d", code);
	}

	if (func)
		func(fs, slotnum);

	return;
}

void corrupt_local_alloc(ocfs2_filesys *fs, int code, uint16_t slotnum)
{
	void (*func)(ocfs2_filesys *fs, uint16_t slotnum) = NULL;

	switch (code) {
	case CORRUPT_LOCAL_ALLOC_EMPTY:
		func = mess_up_local_alloc_empty;
		break;
	case CORRUPT_LOCAL_ALLOC_BITMAP: 
		func = mess_up_local_alloc_bitmap;
		break;
	case CORRUPT_LOCAL_ALLOC_USED: 
		func = mess_up_local_alloc_used;
		break;
	default:
		FSWRK_FATAL("Invalid code = %d", code);
	}

	if (func)
		func(fs, slotnum);

	return;
}

void corrupt_truncate_log(ocfs2_filesys *fs, int code, uint16_t slotnum)
{
	void (*func)(ocfs2_filesys *fs, uint16_t slotnum) = NULL;

	switch (code) {
	case CORRUPT_TRUNCATE_LOG_LIST:
		func = mess_up_truncate_log_list;
		break;
	case CORRUPT_TRUNCATE_LOG_REC:
		func = mess_up_truncate_log_rec;
		break;
	default:
		FSWRK_FATAL("Invalid code = %d", code);
	}

	if (func)
		func(fs, slotnum);

	return;
}
