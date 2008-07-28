/*
 * group.c
 *
 * group descriptor corruptions
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


/* This file will create the following errors for a group descriptor.
 *
 * Group minor field error:	GROUP_PARENT, GROUP_BLKNO, GROUP_CHAIN,
 *				GROUP_FREE_BITS
 *
 * Group generation error:	GROUP_GEN
 *
 * Group list error:		GROUP_UNEXPECTED_DESC, GROUP_EXPECTED_DESC
 *
 */
#include "main.h"

extern char *progname;

static void create_test_group_desc(ocfs2_filesys *fs, uint64_t *newblk,
				struct ocfs2_group_desc *clone)
{
	errcode_t ret;
	uint32_t n_clusters;
	char *buf = NULL;
	struct ocfs2_group_desc *bg = NULL;

	if (!clone)
		FSWRK_FATAL("Can't fake the null group descriptor");

	ret = ocfs2_new_clusters(fs, 1, 1, newblk, &n_clusters);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);
	
	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	bg = (struct ocfs2_group_desc *)buf;
	*bg = *clone;
	bg->bg_blkno = cpu_to_le64(*newblk);
	bg->bg_next_group = 0;

	ret = io_write_block(fs->fs_io, *newblk, 1, buf);
	if(ret)
		FSWRK_COM_FATAL(progname, ret);

	return;
}

static void damage_group_desc(ocfs2_filesys *fs, uint64_t blkno,
				enum fsck_type type)
{
	errcode_t ret;
	char *buf = NULL, *bufgroup = NULL, *bufgroup1 = NULL;
	struct ocfs2_dinode *di;
	struct ocfs2_chain_list *cl;
	struct ocfs2_chain_rec *cr;
	struct ocfs2_group_desc *bg = NULL, *bg1 = NULL;
	uint64_t newblk;

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
		FSWRK_WARN("No chain record found at inode#%"PRIu64
			",so can't corrupt it for type[%d].\n",
			 blkno, type);
		goto bail;
	}

	ret = ocfs2_malloc_block(fs->fs_io, &bufgroup);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);
	
	cr = cl->cl_recs;
	ret = ocfs2_read_group_desc(fs, cr->c_blkno, bufgroup);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	bg = (struct ocfs2_group_desc *)bufgroup;

	switch (type) {
	case GROUP_EXPECTED_DESC:
		fprintf(stdout, "Corrput GROUP_EXPECED_DESC: "
			"delete the group desciptor#%"PRIu64" from the chain "
			"#%d\n", bg->bg_next_group, bg->bg_chain);
		bg->bg_next_group = 0;
		break;
	case GROUP_UNEXPECTED_DESC:
		create_test_group_desc(fs, &newblk, bg);
		fprintf(stdout, "Corrput GROUP_UNEXPECED_DESC: "
			"Add a fake descriptor#%"PRIu64
			" in the chain#%d of inode#%"PRIu64"\n",
			newblk, bg->bg_chain, blkno);
		bg->bg_next_group = cpu_to_le64(newblk);
		break;
	case GROUP_GEN:
		fprintf(stdout, "Corrput GROUP_GEN: "
			"change group generation from %x to %x\n",
			bg->bg_generation, (bg->bg_generation + 10));
		bg->bg_generation += 10;
		/* crash next group correspondingly to verify fsck.ocfs2. */
		if (bg->bg_next_group) {
			ret = ocfs2_malloc_block(fs->fs_io, &bufgroup1);
			if (ret)
				FSWRK_COM_FATAL(progname, ret);
	
			ret = ocfs2_read_group_desc(fs, bg->bg_next_group,
							 bufgroup1);
			if (ret)
				FSWRK_COM_FATAL(progname, ret);

			bg1 = (struct ocfs2_group_desc *)bufgroup1;

			bg1->bg_generation += 10;
			fprintf(stdout, "Corrput GROUP_GEN: "
				"change group generation from %x to %x\n",
				bg1->bg_generation, (bg1->bg_generation + 10));

			ret = ocfs2_write_group_desc(fs, bg->bg_next_group,
							bufgroup1);
			if (ret)
				FSWRK_COM_FATAL(progname, ret);
		}
		break;
	case GROUP_PARENT:
		fprintf(stdout, "Corrput GROUP_PARENT: "
			"change group parent from %"PRIu64" to %"PRIu64"\n",
			bg->bg_parent_dinode, (bg->bg_parent_dinode + 10));
		bg->bg_parent_dinode += 10;
		break;
	case GROUP_BLKNO:
		fprintf(stdout, "Corrput GROUP_BLKNO: "
			"change group blkno from %"PRIu64" to %"PRIu64"\n",
			bg->bg_blkno, (bg->bg_blkno + 10));
		bg->bg_blkno += 10;
		break;
	case GROUP_CHAIN:
		fprintf(stdout, "Corrput GROUP_CHAIN: "
			"change group chain from %u to %u\n",
			bg->bg_chain, (bg->bg_chain + 10));
		bg->bg_chain += 10;
		break;
	case GROUP_FREE_BITS:
		fprintf(stdout, "Corrput GROUP_FREE_BITS: "
			"change group free bits from %u to %u\n",
			bg->bg_free_bits_count, (bg->bg_bits + 10));
		bg->bg_free_bits_count = bg->bg_bits + 10;
		break;
	default:
		FSWRK_FATAL("Invalid type[%d]\n", type);
	}

	ret = ocfs2_write_group_desc(fs, cr->c_blkno, bufgroup);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_write_inode(fs, blkno, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

bail:
	if(bufgroup1)
		ocfs2_free(&bufgroup1);
	if (bufgroup)
		ocfs2_free(&bufgroup);
	if (buf)
		ocfs2_free(&buf);

	return;
}

static void mess_up_group_desc(ocfs2_filesys *fs, uint16_t slotnum,
			enum fsck_type *types, int num)
{
	errcode_t ret;
	char sysfile[OCFS2_MAX_FILENAME_LEN];
	uint64_t blkno;
	int i;
	struct ocfs2_super_block *sb = OCFS2_RAW_SB(fs->fs_super);
	
	if (num <= 0)
		FSWRK_FATAL("Invalid num %d", num);

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
		damage_group_desc(fs, blkno, types[i]);

	return ;
}

void mess_up_group_minor(ocfs2_filesys *fs, uint16_t slotnum)
{
	enum fsck_type types[] = { 	GROUP_PARENT, GROUP_BLKNO,
					GROUP_CHAIN, GROUP_FREE_BITS };

	mess_up_group_desc(fs, slotnum, types, ARRAY_ELEMENTS(types));
}

void mess_up_group_gen(ocfs2_filesys *fs, uint16_t slotnum)
{

	enum fsck_type types[] = { GROUP_GEN };

	mess_up_group_desc(fs, slotnum, types, ARRAY_ELEMENTS(types));
}

void mess_up_group_list(ocfs2_filesys *fs, uint16_t slotnum)
{
	enum fsck_type types[] = { GROUP_EXPECTED_DESC, GROUP_UNEXPECTED_DESC };

	mess_up_group_desc(fs, slotnum, types, ARRAY_ELEMENTS(types));
}

/* We will allocate some clusters and corrupt the group descriptor
 * which stores the clusters and makes fsck run into error.
 */
void mess_up_cluster_group_desc(ocfs2_filesys *fs, uint16_t slotnum)
{
	errcode_t ret;
	uint32_t found, start_cluster, old_free_bits, request = 100;
	uint64_t start_blk;
	uint64_t bg_blk;
	int cpg;
	char *buf = NULL;
	struct ocfs2_group_desc *bg;

	ret = ocfs2_new_clusters(fs, 1, request, &start_blk, &found);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	start_cluster = ocfs2_blocks_to_clusters(fs, start_blk);
	cpg = ocfs2_group_bitmap_size(fs->fs_blocksize) * 8;
	bg_blk = ocfs2_which_cluster_group(fs, cpg, start_cluster);

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_read_group_desc(fs, bg_blk, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	bg = (struct ocfs2_group_desc *)buf;

	old_free_bits = bg->bg_free_bits_count;
	bg->bg_free_bits_count = bg->bg_bits + 10;

	ret = ocfs2_write_group_desc(fs, bg_blk, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	fprintf(stdout, "Corrput CLUSTER and GROUP_FREE_BITS: "
		"Allocating %u clusters and change group[%"PRIu64"]'s free bits"
		" from %u to %u\n",
		found, bg_blk, old_free_bits, bg->bg_free_bits_count);

	if (buf)
		ocfs2_free(&buf);
}
