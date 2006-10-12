/*
 * local_alloc.c
 *
 * local alloc corruptions
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
 * Empty local alloc  error:	LALLOC_SIZE, LALLOC_NZ_USED, LALLOC_NZ_BM
 *
 * Local alloc bitmap error: 	LALLOC_BM_OVERRUN, LALLOC_BM_STRADDLE,
 *				LALLOC_BM_SIZE
 *
 * Local alloc used info error:	LALLOC_USED_OVERRUN, LALLOC_CLEAR
 * 
 * LALLOC_USED, LALLOC_REPAIR is recorded in fsck.ocfs2.checks.8.in,
 * but never find the solution in fsck.ocfs2 source code.
 *
 */

#include <main.h>

extern char *progname;

static inline uint32_t get_local_alloc_window_bits()
{
	/* just return a specific number for test */
	return 256;
}

static void create_local_alloc(ocfs2_filesys *fs, uint64_t blkno)
{
	errcode_t ret;
	char *buf = NULL;
	struct ocfs2_dinode *di;
	struct ocfs2_local_alloc *la;
	uint32_t la_size, found;
	uint64_t la_off;
	struct ocfs2_super_block *sb = OCFS2_RAW_SB(fs->fs_super);

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	di = (struct ocfs2_dinode *)buf;

	if (!(di->i_flags & OCFS2_VALID_FL))
		FSWRK_FATAL("not a file");

	if (!(di->i_flags & OCFS2_LOCAL_ALLOC_FL))
		FSWRK_FATAL("not a local alloc file");

	if (di->id1.bitmap1.i_total > 0) {
		FSWRK_WARN("local alloc#%"PRIu64" file not empty."
				"Can't create a new one.\n", blkno);
		goto bail;
	}

	la_size = get_local_alloc_window_bits();

	ret = ocfs2_new_clusters(fs, 1, la_size, &la_off, &found);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);
	if(la_size != found)
		FSWRK_FATAL("can't allocate enough clusters for local alloc");

	la = &(di->id2.i_lab);

	la->la_bm_off = cpu_to_le32(la_off);
	di->id1.bitmap1.i_total = cpu_to_le32(la_size);
	di->id1.bitmap1.i_used = 0;
	memset(la->la_bitmap, 0, le16_to_cpu(la->la_size));
	
	ret = ocfs2_write_inode(fs, blkno, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

bail:
	if(buf)
		ocfs2_free(&buf);
	return;
}

static void damage_local_alloc(ocfs2_filesys *fs,
				uint64_t blkno, enum fsck_type type)
{
	errcode_t ret;
	char *buf = NULL;
	struct ocfs2_dinode *di;
	struct ocfs2_local_alloc *la;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	di = (struct ocfs2_dinode *)buf;

	if (!(di->i_flags & OCFS2_VALID_FL))
		FSWRK_FATAL("not a file");

	if (!(di->i_flags & OCFS2_LOCAL_ALLOC_FL))
		FSWRK_FATAL("not a local alloc file");
	
	la = &(di->id2.i_lab);

	/* For LALLOC_BM_OVERRUN, LALLOC_BM_STRADDLE,LALLOC_BM_SIZE,
	 * LALLOC_USED_OVERRUN, LALLOC_CLEAR, i_total must be greater than 0.
	 * So check it first.
	 */
	if (type == LALLOC_BM_OVERRUN || type == LALLOC_BM_STRADDLE ||
		type == LALLOC_BM_SIZE || type == LALLOC_USED_OVERRUN)
		if (di->id1.bitmap1.i_total == 0) {
			FSWRK_WARN("local inode#%"PRIu64" is empty, so can't"
					"corrupt it for type[%d]\n",
					blkno, type);
			goto bail;
		}
	
	switch (type) {
	case LALLOC_SIZE:
	case LALLOC_CLEAR:
		if (type == LALLOC_SIZE)
			fprintf(stdout, "LALLOC_SIZE: ");
		else
			fprintf(stdout, "LALLOC_CLEAR: ");
		fprintf(stdout,	"Corrupt local alloc inode#%"PRIu64
			", change size from %u to %u\n", blkno, la->la_size,
			(ocfs2_local_alloc_size(fs->fs_blocksize) + 10));
		la->la_size = ocfs2_local_alloc_size(fs->fs_blocksize) + 10;
		break;
	case LALLOC_NZ_USED:
		di->id1.bitmap1.i_total = 0;
		di->id1.bitmap1.i_used = 10;
		fprintf(stdout, "LALLOC_NZ_USED: "
			"Corrupt local alloc inode#%"PRIu64", total = %d "
			" used =  %d\n",blkno,  di->id1.bitmap1.i_total,
			di->id1.bitmap1.i_used);
		break;
	case LALLOC_NZ_BM:
		di->id1.bitmap1.i_total = 0;
		la->la_bm_off = 100;
		fprintf(stdout, "LALLOC_NZ_BM: "
			"Corrupt local alloc inode#%"PRIu64", total = %d "
			" la_bm_off =  %d\n",blkno,  di->id1.bitmap1.i_total,
			la->la_bm_off);
		break;
	case LALLOC_BM_OVERRUN:
	case LALLOC_BM_STRADDLE:
		la->la_bm_off = fs->fs_clusters + 10;
		if (type == LALLOC_BM_OVERRUN)
			fprintf(stdout, "LALLOC_BM_OVERRUN: ");
		else
			fprintf(stdout, "LALLOC_BM_STRADDLE: ");
		fprintf(stdout, "Corrupt local alloc inode#%"PRIu64
			", la_bm_off =%u\n", blkno,  la->la_bm_off);
		break;
	case LALLOC_BM_SIZE:
		fprintf(stdout, "LALLOC_SIZE: "
			"Corrupt local alloc inode#%"PRIu64", change i_total"
			" from %u to %u\n", blkno, di->id1.bitmap1.i_total,
			(la->la_size * 8 + 10));
		di->id1.bitmap1.i_total = la->la_size * 8 + 10;
		break;
	case LALLOC_USED_OVERRUN:
		fprintf(stdout, "LALLOC_USED_OVERRUN: "
			"Corrupt local alloc inode#%"PRIu64", change i_used"
			" from %u to %u\n", blkno, di->id1.bitmap1.i_used,
			(di->id1.bitmap1.i_total + 10));
		di->id1.bitmap1.i_used = di->id1.bitmap1.i_total + 10;
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


void mess_up_local_alloc_empty(ocfs2_filesys *fs, uint16_t slotnum)
{
	errcode_t ret;
	uint64_t blkno;
	int i;
	char alloc_inode[OCFS2_MAX_FILENAME_LEN];
	struct ocfs2_super_block *sb = OCFS2_RAW_SB(fs->fs_super);
 	enum fsck_type types[] = { LALLOC_SIZE, LALLOC_NZ_USED, LALLOC_NZ_BM};
  
	if (slotnum == UINT16_MAX)
		slotnum = 0;
	
	snprintf(alloc_inode, sizeof(alloc_inode), 	
		 ocfs2_system_inodes[LOCAL_ALLOC_SYSTEM_INODE].si_name, slotnum);
	
	ret = ocfs2_lookup(fs, sb->s_system_dir_blkno, alloc_inode,
			   strlen(alloc_inode), NULL, &blkno);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	for ( i = 0; i < ARRAY_ELEMENTS(types); i++) 
		damage_local_alloc(fs, blkno, types[i]);
		
	return;
}

void mess_up_local_alloc_bitmap(ocfs2_filesys *fs, uint16_t slotnum)
{
	errcode_t ret;
	uint64_t blkno;
	int i;
	char alloc_inode[OCFS2_MAX_FILENAME_LEN];
	struct ocfs2_super_block *sb = OCFS2_RAW_SB(fs->fs_super);
 	enum fsck_type types[] = { 	LALLOC_BM_OVERRUN,
					LALLOC_BM_STRADDLE,
					LALLOC_BM_SIZE};
  
	if (slotnum == UINT16_MAX)
		slotnum = 0;
	
	snprintf(alloc_inode, sizeof(alloc_inode), 	
		 ocfs2_system_inodes[LOCAL_ALLOC_SYSTEM_INODE].si_name, slotnum);
	
	ret = ocfs2_lookup(fs, sb->s_system_dir_blkno, alloc_inode,
			   strlen(alloc_inode), NULL, &blkno);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	create_local_alloc(fs, blkno);

	for ( i = 0; i < ARRAY_ELEMENTS(types); i++) 
		damage_local_alloc(fs, blkno, types[i]);
		
	return;
}

void mess_up_local_alloc_used(ocfs2_filesys *fs, uint16_t slotnum)
{
	errcode_t ret;
	uint64_t blkno;
	int i;
	char alloc_inode[OCFS2_MAX_FILENAME_LEN];
	struct ocfs2_super_block *sb = OCFS2_RAW_SB(fs->fs_super);
 	enum fsck_type types[] = { LALLOC_USED_OVERRUN, LALLOC_CLEAR };
  
	if (slotnum == UINT16_MAX)
		slotnum = 0;
	
	snprintf(alloc_inode, sizeof(alloc_inode), 	
		 ocfs2_system_inodes[LOCAL_ALLOC_SYSTEM_INODE].si_name,slotnum);
	
	ret = ocfs2_lookup(fs, sb->s_system_dir_blkno, alloc_inode,
			   strlen(alloc_inode), NULL, &blkno);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	create_local_alloc(fs, blkno);

	for ( i = 0; i < ARRAY_ELEMENTS(types); i++) 
		damage_local_alloc(fs, blkno, types[i]);
		
	return;
}
