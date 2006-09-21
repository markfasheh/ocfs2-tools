/*
 * chain.c
 *
 * chain group corruptions
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
 * mess_up_chains()
 *
 */
void mess_up_chains(ocfs2_filesys *fs, uint64_t blkno, int code)
{
	errcode_t ret;
	char *buf = NULL;
	struct ocfs2_dinode *di;
	struct ocfs2_chain_list *cl;
	struct ocfs2_chain_rec *cr;
	int i;
	uint32_t tmp1, tmp2;
	uint64_t tmpblk;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	di = (struct ocfs2_dinode *)buf;

	if (!(di->i_flags & OCFS2_BITMAP_FL))
		FSWRK_FATAL("not a bitmap");

	if (!(di->i_flags & OCFS2_CHAIN_FL))
		FSWRK_FATAL("not a chain group");

	cl = &(di->id2.i_chain);

	if (!cl->cl_next_free_rec) {
		fprintf(stdout, "No chains found at block#%"PRIu64"\n", blkno);
		goto bail;
	}

	switch (code) {
	case 3: /* delink the last chain from the inode */
		fprintf(stdout, "Corrupt #%02d: Delink group descriptor "
			"in block#%"PRIu64"\n", code, blkno);

		i = cl->cl_next_free_rec - 1;
		cr = &(cl->cl_recs[i]);
		fprintf(stdout, "Delinking ind=%d, block#=%"PRIu64", "
			"free=%u, total=%u\n", i, cr->c_blkno,
			cr->c_free, cr->c_total);
		cr->c_free = 12345;
		cr->c_total = 67890;
		cr->c_blkno = ocfs2_clusters_to_blocks(fs, fs->fs_super->i_clusters);
		cr->c_blkno += 1; /* 1 more block than the fs size */
		cl->cl_next_free_rec = i;
		break;

	case 4: /* corrupt cl_count */
		fprintf(stdout, "Corrupt #%02d: Modified cl_count "
			"in block#%"PRIu64" from %u to %u\n", code, blkno,
			cl->cl_count, (cl->cl_count + 100));
		cl->cl_count += 100;
		break;

	case 5: /* corrupt cl_next_free_rec */
		fprintf(stdout, "Corrupt #%02d: Modified cl_next_free_rec "
			"in block#%"PRIu64" from %u to %u\n", code, blkno,
			cl->cl_next_free_rec, (cl->cl_next_free_rec + 10));
		cl->cl_next_free_rec += 10;
		break;

	case 7: /* corrupt id1.bitmap1.i_total/i_used */
		fprintf(stdout, "Corrupt #%02d: Modified bitmap total "
			"in block#%"PRIu64" from %u to %u\n", code, blkno,
			di->id1.bitmap1.i_total, di->id1.bitmap1.i_total + 10);
		fprintf(stdout, "Corrupt #%02d: Modified bitmap used "
			"in block#%"PRIu64" from %u to %u\n", code, blkno,
			di->id1.bitmap1.i_used, 0);
		di->id1.bitmap1.i_total += 10;
		di->id1.bitmap1.i_used = 0;
		break;

	case 8: /* Corrupt c_blkno of the first record with a number larger than volume size */
		cr = &(cl->cl_recs[0]);
		tmpblk = ocfs2_clusters_to_blocks(fs, fs->fs_super->i_clusters);
		tmpblk++; /* 1 more block than the fs size */

		fprintf(stdout, "Corrupt #%02d: Modified c_blkno in "
			"block#%"PRIu64" from %"PRIu64" to %"PRIu64"\n",
			code, blkno, cr->c_blkno, tmpblk);

		cr->c_blkno = tmpblk;
		break;

	case 10: /* Corrupt c_blkno of the first record with an unaligned number */
		cr = &(cl->cl_recs[0]);
		tmpblk = 1234567;

		fprintf(stdout, "Corrupt #%02d: Modified c_blkno in "
			"block#%"PRIu64" from %"PRIu64" to %"PRIu64"\n",
			code, blkno, cr->c_blkno, tmpblk);

		cr->c_blkno = tmpblk;
		break;

	case 11: /* Corrupt c_blkno of the first record with 0 */
		cr = &(cl->cl_recs[0]);
		tmpblk = 0;

		fprintf(stdout, "Corrupt #%02d: Modified c_blkno in "
			"block#%"PRIu64" from %"PRIu64" to %"PRIu64"\n",
			code, blkno, cr->c_blkno, tmpblk);

		cr->c_blkno = tmpblk;
		break;

	case 12: /* corrupt c_total and c_free of the first record */
		cr = &(cl->cl_recs[0]);
		tmp1 = (cr->c_total >= 100) ? (cr->c_total - 100) : 0;
		tmp2 = (cr->c_free >= 100) ? (cr->c_free - 100) : 0;

		fprintf(stdout, "Corrupt #%02d: Modified c_total "
			"in block#%"PRIu64" for chain ind=%d from %u to %u\n",
			code, blkno, 0, cr->c_total, tmp1);
		fprintf(stdout, "Corrupt #%02d: Modified c_free "
			"in block#%"PRIu64" for chain ind=%d from %u to %u\n",
			code, blkno, 0, cr->c_free, tmp2);

		cr->c_total = tmp1;
		cr->c_free = tmp2;
		break;

	default:
		FSWRK_FATAL("Invalid code=%d", code);
	}

	ret = ocfs2_write_inode(fs, blkno, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	fprintf(stdout, "Corrupt #%02d: Finito\n", code);

bail:
	if (buf)
		ocfs2_free(&buf);

	return ;
}

static void mess_up_sys_file(ocfs2_filesys *fs, uint64_t blkno,
				enum fsck_type type)
{
	errcode_t ret;
	char *buf = NULL, *bufgroup = NULL;
	struct ocfs2_dinode *di;
	struct ocfs2_chain_list *cl;
	struct ocfs2_chain_rec *cr;
	uint64_t oldblkno;
	struct ocfs2_group_desc *bg = NULL;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	di = (struct ocfs2_dinode *)buf;

	if (!(di->i_flags & OCFS2_BITMAP_FL))
		FSWRK_COM_FATAL(progname, ret);

	if (!(di->i_flags & OCFS2_CHAIN_FL))
		FSWRK_COM_FATAL(progname, ret);

	cl = &(di->id2.i_chain);

	/* for CHAIN_EMPTY, CHAIN_HEAD_LINK_RANGE, CHAIN_LINK_RANGE,
	 * CHAIN_BITS, CHAIN_LINK_GEN, CHAIN_LINK_MAGIC,
	 * we need to corrupt some chain rec, so check it first.
	 */
	if (type == CHAIN_EMPTY || type == CHAIN_HEAD_LINK_RANGE ||
		type == CHAIN_LINK_RANGE || type == CHAIN_BITS 	||
		type == CHAIN_LINK_GEN	 || type == CHAIN_LINK_MAGIC)
		if (!cl->cl_next_free_rec) {
			FSWRK_WARN("No chain record found at block#%"PRIu64
				",so can't corrupt it for type[%d].\n",
				 blkno, type);
			goto bail;
		}

	switch (type) {
	case CHAIN_COUNT:
		fprintf(stdout, "Corrupt CHAIN_COUNT: "
			"Modified cl_count "
			"in block#%"PRIu64" from %u to %u\n",  blkno,
			cl->cl_count, (cl->cl_count + 100));
		cl->cl_count += 100;
		break;
	case CHAIN_NEXT_FREE:
		fprintf(stdout, "Corrupt CHAIN_NEXT_FREE:"
			" Modified cl_next_free_rec "
			"in block#%"PRIu64" from %u to %u\n", blkno,
			cl->cl_next_free_rec, (cl->cl_count + 10));
		cl->cl_next_free_rec = cl->cl_count + 10;
		break;
	case CHAIN_EMPTY:
		cr = cl->cl_recs;
		fprintf(stdout, "Corrupt CHAIN_EMPTY:"
			" Modified e_blkno "
			"in block#%"PRIu64" from %"PRIu64" to 0\n",
			 blkno,	cr->c_blkno);
		cr->c_blkno = 0;
		break;
	case CHAIN_I_CLUSTERS:
		fprintf(stdout, "Corrupt CHAIN_I_CLUSTERS:"
			"change i_clusters in block#%"PRIu64" from %u to %u\n",
			 blkno, di->i_clusters, (di->i_clusters + 10));
		di->i_clusters += 10;
		break;
	case CHAIN_I_SIZE:
		fprintf(stdout, "Corrupt CHAIN_I_SIZE:"
			"change i_size "
			"in block#%"PRIu64" from %"PRIu64" to %"PRIu64"\n",
			 blkno, di->i_size, (di->i_size + 10));
		di->i_size += 10;
		break;
	case CHAIN_GROUP_BITS:
		fprintf(stdout, "Corrupt CHAIN_GROUP_BITS:"
			"change i_used of bitmap "
			"in block#%"PRIu64" from %u to %u\n", blkno, 
			di->id1.bitmap1.i_used, (di->id1.bitmap1.i_used + 10));
		di->id1.bitmap1.i_used += 10;
		break;
	case CHAIN_HEAD_LINK_RANGE:
		cr = cl->cl_recs;
		oldblkno = cr->c_blkno;
		cr->c_blkno = 
			ocfs2_clusters_to_blocks(fs, fs->fs_clusters) + 10;
		fprintf(stdout, "Corrupt CHAIN_HEAD_LINK_RANGE:"
			"change  "
			"in block#%"PRIu64" from %"PRIu64" to %"PRIu64"\n",
			 blkno, oldblkno, cr->c_blkno);
		break;
	case CHAIN_LINK_GEN:
	case CHAIN_LINK_MAGIC:
	case CHAIN_LINK_RANGE:
		ret = ocfs2_malloc_block(fs->fs_io, &bufgroup);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);
		
		bg = (struct ocfs2_group_desc *)bufgroup;
		cr = cl->cl_recs;

		ret = ocfs2_read_group_desc(fs, cr->c_blkno, (char *)bg);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);

		if (type == CHAIN_LINK_GEN) {
			fprintf(stdout, "Corrupt CHAIN_LINK_GEN: "
				"change generation num from %u to 0x1234\n",
				bg->bg_generation);
			bg->bg_generation = 0x1234;
		} else if (type == CHAIN_LINK_MAGIC) {
			fprintf(stdout, "Corrupt CHAIN_LINK_MAGIC: "
				"change signature to '1234'\n");
			sprintf((char *)bg->bg_signature,"1234");
		} else {
			oldblkno = bg->bg_next_group;
			bg->bg_next_group = 
			    ocfs2_clusters_to_blocks(fs, fs->fs_clusters) + 10;
			fprintf(stdout, "Corrupt CHAIN_LINK_RANGE: "
				"change next group from %"PRIu64" to %"PRIu64
				" \n", oldblkno, bg->bg_next_group);
		}
		
		ret = ocfs2_write_group_desc(fs, cr->c_blkno, (char *)bg);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);
		break;	
	case CHAIN_BITS:
		cr = cl->cl_recs;
		fprintf(stdout, "Corrupt CHAIN_BITS:"
			"change inode#%"PRIu64" c_total from %u to %u\n",
			 blkno, cr->c_total, (cr->c_total + 10));
		cr->c_total += 10; 
		break;
	default:
		FSWRK_FATAL("Unknown fsck_type[%d]\n", type);
	}

	ret = ocfs2_write_inode(fs, blkno, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

bail:
	if (bufgroup)
		ocfs2_free(&bufgroup);
	if (buf)
		ocfs2_free(&buf);

	return ;
}

static void mess_up_sys_chains(ocfs2_filesys *fs, uint16_t slotnum,
			enum fsck_type *types, int num)
{
	errcode_t ret;
	char sysfile[OCFS2_MAX_FILENAME_LEN];
	uint64_t blkno;
	int i;
	struct ocfs2_super_block *sb = OCFS2_RAW_SB(fs->fs_super);
	
	if (num <= 0)
		FSWRK_FATAL("Invalid num = %d\n", num);
	
	if (slotnum == UINT16_MAX)
		snprintf(sysfile, sizeof(sysfile),
		ocfs2_system_inodes[GLOBAL_BITMAP_SYSTEM_INODE].si_name);
	else
		snprintf(sysfile, sizeof(sysfile),
			ocfs2_system_inodes[INODE_ALLOC_SYSTEM_INODE].si_name,
			slotnum);

	ret = ocfs2_lookup(fs, sb->s_system_dir_blkno, sysfile,
				   strlen(sysfile), NULL, &blkno);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);
	
	for(i = 0; i < num; i++) 
		mess_up_sys_file(fs, blkno, types[i]);

	return ;
}

void mess_up_chains_list(ocfs2_filesys *fs,  uint16_t slotnum)
{
	
	enum fsck_type types[] = { 	CHAIN_COUNT,
					CHAIN_NEXT_FREE };
	int numtypes = sizeof(types) / sizeof(enum fsck_type);

	mess_up_sys_chains(fs, slotnum, types, numtypes);
}

void mess_up_chains_rec(ocfs2_filesys *fs,   uint16_t slotnum)
{
	
	enum fsck_type types[] = { 	CHAIN_EMPTY,
					CHAIN_HEAD_LINK_RANGE,
					CHAIN_BITS };
	int numtypes = sizeof(types) / sizeof(enum fsck_type);

	mess_up_sys_chains(fs, slotnum, types, numtypes);
}

void mess_up_chains_inode(ocfs2_filesys *fs, uint16_t slotnum)
{
	enum fsck_type types[] = { 	CHAIN_I_CLUSTERS, 
					CHAIN_I_SIZE,
					CHAIN_GROUP_BITS };
	int numtypes = sizeof(types) / sizeof(enum fsck_type);

	mess_up_sys_chains(fs, slotnum, types, numtypes);
}

void mess_up_chains_group(ocfs2_filesys *fs, uint16_t slotnum)
{
	enum fsck_type types[] = { 	CHAIN_LINK_GEN,
					CHAIN_LINK_RANGE };
	int numtypes = sizeof(types) / sizeof(enum fsck_type);

	mess_up_sys_chains(fs, slotnum, types, numtypes);
}

void mess_up_chains_group_magic(ocfs2_filesys *fs, uint16_t slotnum)
{
	enum fsck_type types[] = { CHAIN_LINK_MAGIC };
	int numtypes = sizeof(types) / sizeof(enum fsck_type);

	mess_up_sys_chains(fs, slotnum, types, numtypes);
}
