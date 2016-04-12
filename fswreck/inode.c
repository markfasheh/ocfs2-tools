/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
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
			INODE_NZ_DTIME, INODE_SIZE, INODE_CLUSTERS, INODE_COUNT,
			INODE_BLOCK_ECC, INODE_VALID_FLAG
 *
 * Inode link not connected error: INODE_NOT_CONNECTED
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
			blkno, (uint64_t)di->i_blkno, ((uint64_t)di->i_blkno + 100));
		di->i_blkno += 100;
		break;
	case INODE_NZ_DTIME:
		fprintf(stdout, "INODE_NZ_DTIME: "
			"Corrupt inode#%"PRIu64", change i_dtime from %"PRIu64
			" to 100\n", blkno, (uint64_t)di->i_dtime);
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
			 blkno, (uint64_t)di->i_size, ((uint64_t)di->i_size + 100));
		di->i_size += 100;
		break;
	case INODE_SPARSE_SIZE:
		fprintf(stdout, "INODE_SPARSE_SIZE: "
			"Corrupt inode#%"PRIu64", change i_size "
			"from %"PRIu64" to %u\n",
			 blkno, (uint64_t)di->i_size, fs->fs_clustersize);
		di->i_size = fs->fs_clustersize;
		break;
	case INODE_CLUSTERS:
		fprintf(stdout, "INODE_CLUSTERS: "
			"Corrupt inode#%"PRIu64", change i_clusters"
			" from %u to 0\n", blkno, di->i_clusters);
		di->i_clusters = 0;
		break;
	case INODE_SPARSE_CLUSTERS:
		fprintf(stdout, "INODE_SPARSE_CLUSTERS: "
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
	case INODE_BLOCK_ECC:
		fprintf(stdout, "INODE_BLOCK_ECC: "
			"Corrupte inode#%"PRIu64", set both i_check.bc_crc32e"
			"=%u and i_check.bc_ecc=%u to 0x1234\n",
			blkno, di->i_check.bc_crc32e, di->i_check.bc_ecc);
		di->i_check.bc_crc32e = 0x1234;
		di->i_check.bc_ecc = 0x1234;
		break;
	case INODE_VALID_FLAG:
		fprintf(stdout, "INODE_VALID_FLAG: "
			"Corrupt inode#%"PRIu64", clear inode valid flag\n",
			blkno);
		di->i_flags &= ~OCFS2_VALID_FL;
		break;
	case REFCOUNT_FLAG_INVALID:
		di->i_dyn_features |= OCFS2_HAS_REFCOUNT_FL;
		fprintf(stdout, "REFCOUNT_FLAG_INVALD: "
			"Corrupt inode#%"PRIu64", add refcount feature\n",
			blkno);
		break;
	case REFCOUNT_LOC_INVALID:
		di->i_refcount_loc = 100;
		fprintf(stdout, "REFCOUNT_LOC_INVALID: "
			"Create an inode#%"PRIu64","
			"whose i_refcount_loc has been messed up.\n",
			blkno);
		break;
	default:
		FSWRK_FATAL("Invalid type[%d]\n", type);
	}

	if (type != INODE_BLOCK_ECC)
		ret = ocfs2_write_inode(fs, blkno, buf);
	else
		ret = ocfs2_write_inode_without_meta_ecc(fs, blkno, buf);

	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	if (buf)
		ocfs2_free(&buf);
	return;
}

void mess_up_inode_field(ocfs2_filesys *fs, enum fsck_type type, uint64_t blkno)
{
	errcode_t ret;
	uint64_t tmpblkno;
	uint32_t clusters = 10;

	char *buf = NULL;
	struct ocfs2_dinode *di;

	create_file(fs, blkno, &tmpblkno);

	if ((type == INODE_SPARSE_SIZE) || (type == INODE_SPARSE_CLUSTERS)) {

		if (!ocfs2_sparse_alloc(OCFS2_RAW_SB(fs->fs_super)))
			FSWRK_FATAL("should specfiy a sparse file supported "
				    "volume to do this corruption\n");

		ret = ocfs2_malloc_block(fs->fs_io, &buf);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);

		ret = ocfs2_read_inode(fs, tmpblkno, buf);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);

		di = (struct ocfs2_dinode *)buf;

		di->i_size = fs->fs_clustersize * 2;

		ret = ocfs2_write_inode(fs, tmpblkno, buf);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);

		if (buf)
			ocfs2_free(&buf);
	}

	if ((type == INODE_CLUSTERS) || (type == INODE_SPARSE_CLUSTERS) ||
	    (type == INODE_SPARSE_SIZE)) {
		ret = ocfs2_extend_allocation(fs, tmpblkno, clusters);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);
	}

	if (type == REFCOUNT_FLAG_INVALID &&
	    ocfs2_refcount_tree(OCFS2_RAW_SB(fs->fs_super)))
		FSWRK_FATAL("should specfiy a norefcount volume\n");
	if (type == REFCOUNT_LOC_INVALID &&
	    !ocfs2_refcount_tree(OCFS2_RAW_SB(fs->fs_super)))
		FSWRK_FATAL("Should specify a refcount supported volume\n");

	damage_inode(fs, tmpblkno, type);
	return;
}

void mess_up_inode_not_connected(ocfs2_filesys *fs, enum fsck_type type,
				 uint64_t blkno)
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

void mess_up_inode_orphaned(ocfs2_filesys *fs, enum fsck_type type,
			    uint16_t slotnum)
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

void mess_up_inode_alloc(ocfs2_filesys *fs, enum fsck_type type,
			 uint16_t slotnum)
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

void mess_up_inline_flag(ocfs2_filesys *fs, enum fsck_type type, uint64_t blkno)
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

void mess_up_inline_inode(ocfs2_filesys *fs, enum fsck_type type, uint64_t blkno)
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
		max_inline_sz =
			ocfs2_max_inline_data_with_xattr(fs->fs_blocksize, di);

		if (!(di->i_dyn_features & OCFS2_INLINE_DATA_FL))
			di->i_dyn_features |= OCFS2_INLINE_DATA_FL;

		switch (type) {
		case INLINE_DATA_COUNT_INVALID:
			di->id2.i_data.id_count = 0;
			fprintf(stdout, "INLINE_DATA_COUNT_INVALID: "
				"Create an inlined inode#%"PRIu64"(%s),"
				"whose id_count has been messed up.\n",
				inline_blkno, file_type);
			break;
		case INODE_INLINE_SIZE:
			di->i_size = max_inline_sz + 1;
			fprintf(stdout, "INODE_INLINE_SIZE: "
				"Create an inlined inode#%"PRIu64"(%s),"
				"whose i_size has been messed up.\n",
				inline_blkno, file_type);
			break;
		case INODE_INLINE_CLUSTERS:
			di->i_clusters = 1;
			fprintf(stdout, "INODE_INLINE_CLUSTERS: "
				"Create an inlined inode#%"PRIu64"(%s),"
				"whose i_clusters has been messed up.\n",
				inline_blkno, file_type);
			break;

		default:
			FSWRK_FATAL("Invalid type[%d]\n", type);
		}

		ret = ocfs2_write_inode(fs, inline_blkno, buf);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);
	}

	if (buf)
		ocfs2_free(&buf);

	return;
}

void mess_up_dup_clusters(ocfs2_filesys *fs, enum fsck_type type,
			  uint64_t blkno)
{
	errcode_t err;
	char *buf = NULL;
	uint64_t inode1_blkno, inode2_blkno;
	struct ocfs2_dinode *di1, *di2;
	struct ocfs2_extent_list *el1, *el2;

	err = ocfs2_malloc_blocks(fs->fs_io, 2, &buf);
	if (err)
		FSWRK_COM_FATAL(progname, err);

	create_file(fs, blkno, &inode1_blkno);

	di1 = (struct ocfs2_dinode *)buf;
	err = ocfs2_read_inode(fs, inode1_blkno, (char *)di1);
	if (err)
		FSWRK_COM_FATAL(progname, err);

	if (ocfs2_support_inline_data(OCFS2_RAW_SB(fs->fs_super))) {
		if (di1->i_dyn_features & OCFS2_INLINE_DATA_FL) {
			di1->i_dyn_features &= ~OCFS2_INLINE_DATA_FL;
			err = ocfs2_write_inode(fs, inode1_blkno, (char *)di1);
			if (err)
				FSWRK_COM_FATAL(progname, err);
		}
	}

	if (type != DUP_CLUSTERS_SYSFILE_CLONE) {

		create_file(fs, blkno, &inode2_blkno);
		di2 = (struct ocfs2_dinode *)(buf + fs->fs_blocksize);
		err = ocfs2_read_inode(fs, inode2_blkno, (char *)di2);
		if (err)
			FSWRK_COM_FATAL(progname, err);

		if (ocfs2_support_inline_data(OCFS2_RAW_SB(fs->fs_super))) {
			if (di2->i_dyn_features & OCFS2_INLINE_DATA_FL) {
				di2->i_dyn_features &= ~OCFS2_INLINE_DATA_FL;
				err = ocfs2_write_inode(fs, inode2_blkno,
							(char *)di2);
				if (err)
					FSWRK_COM_FATAL(progname, err);
			}
		}

		err = ocfs2_extend_allocation(fs, inode2_blkno, 1);
		if (err)
			FSWRK_COM_FATAL(progname, err);

		/* Re-read the inode with the allocation */
		err = ocfs2_read_inode(fs, inode2_blkno, (char *)di2);
		if (err)
			FSWRK_COM_FATAL(progname, err);

		/* Set i_size to non-zero so that the allocation is valid */
		di2->i_size = fs->fs_clustersize;
		err = ocfs2_write_inode(fs, inode2_blkno, (char *)di2);
		if (err)
			FSWRK_COM_FATAL(progname, err);

		if (type == DUP_CLUSTERS_CLONE)
			fprintf(stdout, "DUP_CLUSTERS_CLONE: "
				"Create two inodes #%"PRIu64" and #%"PRIu64
				" by allocating same cluster to them.\n",
				inode1_blkno, inode2_blkno);
		else
			fprintf(stdout, "DUP_CLUSTERS_DELETE: "
				"Create two inodes #%"PRIu64" and #%"PRIu64
				" by allocating same cluster to them.\n",
				inode1_blkno, inode2_blkno);
	} else {
		/* Here use journal file*/
		err = ocfs2_lookup_system_inode(fs, JOURNAL_SYSTEM_INODE, 0,
						&inode2_blkno);
		if (err)
			FSWRK_COM_FATAL(progname, err);

		di2 = (struct ocfs2_dinode *)(buf + fs->fs_blocksize);
		err = ocfs2_read_inode(fs, inode2_blkno, (char *)di2);
		if (err)
			FSWRK_COM_FATAL(progname, err);

		if (di2->id2.i_list.l_tree_depth)
			FSWRK_FATAL("Journal inode has non-zero tree "
				    "depth.  fswreck can't use it for "
				    "DUP_CLUSTERS_SYSFILE_CLONE\n");

		fprintf(stdout, "DUP_CLUSTERS_SYSFILE_CLONE: "
			"Allocate same cluster to journal file "
			"#%"PRIu64" and regular file #%"PRIu64".\n",
			inode1_blkno, inode2_blkno);
	}

	el1 = &(di1->id2.i_list);
	el2 = &(di2->id2.i_list);


	el1->l_next_free_rec = 1;
	el1->l_recs[0] = el2->l_recs[0];

	di1->i_size =
		ocfs2_clusters_to_bytes(fs, el1->l_recs[0].e_leaf_clusters);
	di1->i_clusters = di2->i_clusters;

	err = ocfs2_write_inode(fs, inode1_blkno, (char *)di1);
	if (err)
		FSWRK_COM_FATAL(progname, err);

	ocfs2_free(&buf);
}
