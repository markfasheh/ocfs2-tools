/*
 * inode.c
 *
 * inode fields corruptions
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

/* This file will create the errors for the inode.
 *
 * Inode field error: 	INODE_SUBALLOC, INODE_GEN, INODE_GEN_FIX,INODE_BLKNO,
			INODE_NZ_DTIME, INODE_SIZE, INODE_CLUSTERS, INODE_COUNT
 *
 * Inode link not connected error: INODE_LINK_NOT_CONNECTED
 *
 * Inode orphaned error:	INODE_ORPHANED
 *
 * Inode alloc error:	INODE_ALLOC_REPAIR
 *
 */

#include "main.h"

extern char *progname;

static void damage_inode(ocfs2_filesys *fs, uint64_t blkno,
				enum fsck_type type)
{
	errcode_t ret;
	char *buf = NULL;
	struct ocfs2_dinode *di;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	di = (struct ocfs2_dinode *)buf;

	if (!(di->i_flags & OCFS2_VALID_FL))
		FSWRK_FATAL("not a file");

	switch (type) {
	case INODE_GEN:
		fprintf(stdout, "INODE_GEN: "
			"Corrupt inode#%"PRIu64", change generation "
			" from %u to 0x1234\n", blkno, di->i_fs_generation);
		di->i_fs_generation = 0x1234;
		break;
	case INODE_GEN_FIX:
		fprintf(stdout, "INODE_GEN_FIX: "
			"Corrupt inode#%"PRIu64", change generation "
			" from %u to 0x1234, please answer 'n' when "
			"INODE_GEN error shows in fsck.ocfs2\n",
			blkno, di->i_fs_generation);
		di->i_fs_generation = 0x1234;
		break;
	case INODE_BLKNO:
		fprintf(stdout, "INODE_BLKNO: "
			"Corrupt inode#%"PRIu64", change i_blkno from %"PRIu64
			" to %"PRIu64"\n",
			blkno, di->i_blkno, (di->i_blkno + 10));
		di->i_blkno += 100;
		break;
	case INODE_NZ_DTIME:
		fprintf(stdout, "INODE_NZ_DTIME: "
			"Corrupt inode#%"PRIu64", change i_dtime from %"PRIu64
			" to 100\n", blkno, di->i_dtime);
		di->i_dtime = 100;
		break;
	case INODE_SUBALLOC:
		fprintf(stdout, "INODE_SUBALLOC: "
			"Corrupt inode#%"PRIu64", change i_suballoc_slot"
			" from %u to %u\n", blkno, di->i_suballoc_slot,
			(di->i_suballoc_slot + 10));
		di->i_suballoc_slot += 10;
		break;
	case INODE_SIZE:
		fprintf(stdout, "INODE_SIZE: "
			"Corrupt inode#%"PRIu64", change i_size"
			" from %"PRIu64" to %"PRIu64"\n",
			 blkno, di->i_size, (di->i_size + 100));
		di->i_size += 100;
		break;
	case INODE_CLUSTERS:
		fprintf(stdout, "INODE_CLUSTER: "
			"Corrupt inode#%"PRIu64", change i_clusters"
			" from %u to 0\n", blkno, di->i_clusters);
		di->i_clusters = 0;
		break;
	case INODE_COUNT:
		di->i_links_count = 0;
		fprintf(stdout, "INODE_COUNT: "
			"Corrupte inode#%"PRIu64", set link count to 0\n",
			blkno);
		break;
	default:
		FSWRK_FATAL("Invalid type[%d]\n", type);
	}

	ret = ocfs2_write_inode(fs, blkno, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	if (buf)
		ocfs2_free(&buf);
	return;
}

void mess_up_inode_field(ocfs2_filesys *fs, uint64_t blkno)
{
	int i;
	errcode_t ret;
	uint64_t tmpblkno;
	uint32_t clusters = 10;
	enum fsck_type types[] = {	INODE_GEN, INODE_GEN_FIX, INODE_BLKNO,
					INODE_NZ_DTIME, INODE_SUBALLOC,
					INODE_SIZE, INODE_CLUSTERS,
					INODE_COUNT};

	for (i = 0; i < ARRAY_ELEMENTS(types); i++) {
		create_file(fs, blkno, &tmpblkno);

		if (types[i] == INODE_CLUSTERS) {
			ret = ocfs2_extend_allocation(fs, tmpblkno, clusters);
			if (ret)
				FSWRK_COM_FATAL(progname, ret);
		}

		damage_inode(fs, tmpblkno, types[i]);
	}
	return;
}

void mess_up_inode_not_connected(ocfs2_filesys *fs, uint64_t blkno)
{
	errcode_t ret;
	uint64_t tmpblkno;

	ret = ocfs2_new_inode(fs, &tmpblkno, S_IFREG | 0755);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	fprintf(stdout, "INODE_NOT_CONNECTED: "
		"Create an inode#%"PRIu64" which has no links\n", tmpblkno);
	return ;
}

void mess_up_inode_orphaned(ocfs2_filesys *fs, uint16_t slotnum)
{
	errcode_t ret;
	uint64_t blkno, tmpblkno;
	char parentdir[OCFS2_MAX_FILENAME_LEN];
	struct ocfs2_super_block *sb = OCFS2_RAW_SB(fs->fs_super);

	if (slotnum == UINT16_MAX)
		slotnum = 0;
	snprintf(parentdir, sizeof(parentdir),
		 ocfs2_system_inodes[ORPHAN_DIR_SYSTEM_INODE].si_name, slotnum);

	ret = ocfs2_lookup(fs, sb->s_system_dir_blkno, parentdir,
			   strlen(parentdir), NULL, &blkno);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	create_file(fs, blkno, &tmpblkno);

	fprintf(stdout, "INODE_ORPHANED: "
		"Create an inode#%"PRIu64" under directory %s\n",
		tmpblkno, parentdir);
	return;
}

void mess_up_inode_alloc(ocfs2_filesys *fs, uint16_t slotnum)
{
	errcode_t ret;
	uint64_t tmpblkno;
	char *buf = NULL;
	struct ocfs2_dinode *di;

	ret = ocfs2_new_inode(fs, &tmpblkno, S_IFREG | 0755);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_read_inode(fs, tmpblkno, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	di = (struct ocfs2_dinode *)buf;

	di->i_flags &= ~OCFS2_VALID_FL;

	ret = ocfs2_write_inode(fs, tmpblkno, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	fprintf(stdout, "INODE_ALLOC_REPAIR: "
		"Create an inode#%"PRIu64" and invalidate it.\n", tmpblkno);

	if (buf)
		ocfs2_free(&buf);
	return;
}

void mess_up_inline_flag(ocfs2_filesys *fs, uint64_t blkno)
{

	int i;
	errcode_t ret;
	char *buf = NULL, file_type[20];
	uint64_t inline_blkno;
	struct ocfs2_dinode *di;
	struct ocfs2_super_block *osb;

	osb = OCFS2_RAW_SB(fs->fs_super);
	if (ocfs2_support_inline_data(osb))
		FSWRK_FATAL("should specfiy a noinline-data supported "
			    "volume to do this corruption\n");

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	for (i = 0; i < 2; i++) {
		if (i == 0) {
			create_file(fs, blkno, &inline_blkno);
			snprintf(file_type, 20, "%s", "Regular file");
		} else {
			create_directory(fs, blkno, &inline_blkno);
			snprintf(file_type, 20, "%s", "Diectory");
		}

		ret = ocfs2_read_inode(fs, inline_blkno, buf);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);

		di = (struct ocfs2_dinode *)buf;
		if (!(di->i_dyn_features & OCFS2_INLINE_DATA_FL)) {
			di->i_dyn_features |= OCFS2_INLINE_DATA_FL;
			ret = ocfs2_write_inode(fs, inline_blkno, buf);
			if (ret)
				FSWRK_COM_FATAL(progname, ret);
		}

		fprintf(stdout, "INLINE_DATA_FLAG_INVALID: "
			"Create an inlined inode#%"PRIu64"(%s) "
			"on a noinline-data supported volume\n",
			inline_blkno, file_type);
	}

	if (buf)
		ocfs2_free(&buf);
	return;
}

void mess_up_inline_count(ocfs2_filesys *fs, uint64_t blkno)
{
	int i;
	errcode_t ret;
	char *buf = NULL, file_type[20];
	uint64_t inline_blkno;
	struct ocfs2_dinode *di;
	uint16_t max_inline_sz;
	struct ocfs2_super_block *osb;

	osb = OCFS2_RAW_SB(fs->fs_super);
	if (!ocfs2_support_inline_data(osb))
		FSWRK_FATAL("Should specify a inline-data supported "
			    "volume to do this corruption\n");

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	for (i = 0; i < 2; i++) {
		if (i == 0) {
			create_file(fs, blkno, &inline_blkno);
			snprintf(file_type, 20, "%s", "Regular file");
		} else {
			create_directory(fs, blkno, &inline_blkno);
			snprintf(file_type, 20, "%s", "Diectroy");
		}

		ret = ocfs2_read_inode(fs, inline_blkno, buf);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);

		di = (struct ocfs2_dinode *)buf;
		max_inline_sz = ocfs2_max_inline_data(fs->fs_blocksize);

		if (!(di->i_dyn_features & OCFS2_INLINE_DATA_FL))
			di->i_dyn_features |= OCFS2_INLINE_DATA_FL;

		di->id2.i_data.id_count = 0;
		di->i_size = max_inline_sz + 1;
		di->i_clusters = 1;

		ret = ocfs2_write_inode(fs, inline_blkno, buf);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);

		fprintf(stdout, "INLINE_DATA_COUNT_INVALID: "
			"Create an inlined inode#%"PRIu64"(%s),"
			"whose id_count, i_size and i_clusters "
			"been messed up\n", inline_blkno, file_type);
	}

	if (buf)
		ocfs2_free(&buf);

	return;
}
