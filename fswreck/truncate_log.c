/*
 * truncate_log.c
 *
 * truncate log corruptions
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
 * Truncate log list error: 	DEALLOC_COUNT, DEALLOC_USED
 *
 * Truncate log rec error: 	TRUNCATE_REC_START_RANGE, TRUNCATE_REC_WRAP,
 *				TRUNCATE_REC_RANGE
 *
 */

#include "main.h"

extern char *progname;

static void create_truncate_log(ocfs2_filesys *fs, uint64_t blkno,
				uint16_t used, uint32_t clusters)
{
	errcode_t ret;
	char *buf = NULL;
	struct ocfs2_dinode *di;
	struct ocfs2_truncate_log *tl;
	uint16_t i, max;
	uint32_t found;
	uint64_t begin;

	max = ocfs2_truncate_recs_per_inode(fs->fs_blocksize);
	if (used > max)
		FSWRK_FATAL("recnum exceeds the limit of truncate log");

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	di = (struct ocfs2_dinode *)buf;

	if (!(di->i_flags & OCFS2_VALID_FL))
		FSWRK_FATAL("not a valid file");

	if (!(di->i_flags & OCFS2_DEALLOC_FL))
		FSWRK_FATAL("not a valid truncate log");
	
	tl = &di->id2.i_dealloc;

	if (tl->tl_used > 0) {
		FSWRK_WARN("truncate log#%"PRIu64" file not empty."
				"Can't create a new one.\n", blkno);
		goto bail;
	}

	used = min(used, tl->tl_count);
	tl->tl_used = used;

	for (i = 0; i < tl->tl_used; i++) {
		ret = ocfs2_new_clusters(fs, 1, clusters, &begin, &found);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);
		
		tl->tl_recs[i].t_start = 
			cpu_to_le32(ocfs2_blocks_to_clusters(fs, begin));
		tl->tl_recs[i].t_clusters = cpu_to_le32(found);
	}

	ret = ocfs2_write_inode(fs, blkno, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

bail:
	if(buf)
		ocfs2_free(&buf);
	return;
}

static void damage_truncate_log(ocfs2_filesys *fs,
				uint64_t blkno, enum fsck_type type, int recnum)
{
	errcode_t ret;
	char *buf = NULL;
	struct ocfs2_dinode *di;
	struct ocfs2_truncate_log *tl;
	struct ocfs2_truncate_rec *tr;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	di = (struct ocfs2_dinode *)buf;

	if (!(di->i_flags & OCFS2_VALID_FL))
		FSWRK_FATAL("not a valid file");

	if (!(di->i_flags & OCFS2_DEALLOC_FL))
		FSWRK_FATAL("not a valid truncate log");
	
	tl = &di->id2.i_dealloc;

	/* For TRUNCATE_REC_START_RANGE, TRUNCATE_REC_WRAP, TRUNCATE_REC_RANGE,
	 * tl_used must be greater than 0 and recnum must be less than tl_used.
	 * So check it first.
	 */
	if (type == TRUNCATE_REC_START_RANGE || type == TRUNCATE_REC_WRAP ||
		type == TRUNCATE_REC_RANGE) {
		if (tl->tl_used == 0) {
			FSWRK_WARN("truncate log#%"PRIu64" is empty, so can't"
					"corrupt it for type[%d]\n",
					blkno, type);
			goto bail;
		}
		
		if(tl->tl_used <= recnum) {
			FSWRK_WARN("truncate log#%"PRIu64" can't corrupt "
					"item[%d] corrupt it for type[%d]\n",
					blkno, recnum, type);
			goto bail;
		}
	}
	
	switch (type) {
	case DEALLOC_COUNT:
		fprintf(stdout, "DEALLOC_COUNT: "
			"Corrupt truncate log inode#%"PRIu64", change tl_count"
			" from %u to %u\n",
			blkno, tl->tl_count, (tl->tl_count + 10));
		tl->tl_count += 10;
		break;
	case DEALLOC_USED:
		fprintf(stdout, "DEALLOC_USED: "
			"Corrupt truncate log inode#%"PRIu64", change tl_used"
			" from %u to %u\n",
			blkno, tl->tl_used, (tl->tl_count + 10));
		tl->tl_used = tl->tl_count + 10;
		break;
	case TRUNCATE_REC_START_RANGE:
		tr = &tl->tl_recs[recnum];
		fprintf(stdout, "TRUNCATE_REC_START_RANGE: "
			"Corrupt truncate log inode#%"PRIu64",rec#%d "
			"change t_start from %u to %u\n",
			blkno, recnum, tr->t_start, (fs->fs_clusters + 10));
		tr->t_start = fs->fs_clusters + 10;
		break;
	case TRUNCATE_REC_WRAP:
		tr = &tl->tl_recs[recnum];
		fprintf(stdout, "TRUNCATE_REC_WRAP: "
			"Corrupt truncate log inode#%"PRIu64",rec#%d "
			"change t_start from %u to 10000\n,"
			"change t_clusters from %u to %u\n",
			blkno, recnum,
			tr->t_start,tr->t_clusters, (UINT32_MAX - 10));
		tr->t_start = 10000;
		tr->t_clusters = UINT32_MAX - 10;
		break;
	case TRUNCATE_REC_RANGE:
		tr = &tl->tl_recs[recnum];
		fprintf(stdout, "TRUNCATE_REC_RANGE: "
			"Corrupt truncate log inode#%"PRIu64",rec#%d "
			"change t_clusters from %u to %u\n",
			blkno, recnum, tr->t_clusters, (fs->fs_clusters + 10));
		tr->t_clusters = fs->fs_clusters + 10;
		break;
	default:
		FSWRK_FATAL("Unknown type = %d", type);
	}
	
	ret = ocfs2_write_inode(fs, blkno, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);
bail:
	if (buf)
		ocfs2_free(&buf);
	return;
}

static void get_truncate_log(ocfs2_filesys *fs, 
					uint16_t slotnum, uint64_t *blkno)
{
	errcode_t ret;
	char truncate_log[OCFS2_MAX_FILENAME_LEN];
	struct ocfs2_super_block *sb = OCFS2_RAW_SB(fs->fs_super);

	if (slotnum == UINT16_MAX)
		slotnum = 0;
	
	snprintf(truncate_log, sizeof(truncate_log), 	
		 ocfs2_system_inodes[TRUNCATE_LOG_SYSTEM_INODE].si_name,
		 slotnum);
	
	ret = ocfs2_lookup(fs, sb->s_system_dir_blkno, truncate_log,
			   strlen(truncate_log), NULL, blkno);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	return;
}

void mess_up_truncate_log_list(ocfs2_filesys *fs, uint16_t slotnum)
{
	uint64_t blkno;
	int i;
 	enum fsck_type types[] = { DEALLOC_COUNT, DEALLOC_USED };

	get_truncate_log(fs, slotnum, &blkno);

	for ( i = 0; i < ARRAY_ELEMENTS(types); i++) 
		damage_truncate_log(fs, blkno, types[i], i);
		
	return;
}

void mess_up_truncate_log_rec(ocfs2_filesys *fs, uint16_t slotnum)
{
	uint64_t blkno;
	int i;
 	enum fsck_type types[] = {	TRUNCATE_REC_START_RANGE,
	 				TRUNCATE_REC_WRAP,
					TRUNCATE_REC_RANGE };

	get_truncate_log(fs, slotnum, &blkno);

	create_truncate_log(fs, blkno, 10, 10);
	for ( i = 0; i < ARRAY_ELEMENTS(types); i++) 
		damage_truncate_log(fs, blkno, types[i], i);
		
	return;
}
