/*
 * sizetest.c
 *
 * ocfs2 utility to check structure sizing on various ports
 *
 * Copyright (C) 2004 Oracle Corporation.  All rights reserved.
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
 * Authors: Sunil Mushran
 */


#include <ocfs2.h>
#include <ocfs2_fs.h>

#ifdef USE_HEX
# define NUMFORMAT  "0x%x"
#else
#define NUMFORMAT  "%d"
# endif

#define SHOW_SIZEOF(x,y)  printf("sizeof("#x") = "NUMFORMAT"\n", sizeof( ##y ))

#define SHOW_OFFSET(x,y)  printf("\t"#x" = "NUMFORMAT" (%d)\n", \
				(void *)&(##y.##x)-(void *)&##y, sizeof(##y.##x))

static void print_ocfs2_extent_rec(void)
{
	ocfs2_extent_rec rec;

	SHOW_SIZEOF (ocfs2_extent_rec, rec);

	SHOW_OFFSET (e_cpos, rec);
	SHOW_OFFSET (e_clusters, rec);
	SHOW_OFFSET (e_blkno, rec);
	printf("\n");
}

static void print_ocfs2_chain_rec(void)
{
	ocfs2_chain_rec rec;

	SHOW_SIZEOF (ocfs2_chain_rec, rec);

	SHOW_OFFSET (c_free, rec);
	SHOW_OFFSET (c_total, rec);
	SHOW_OFFSET (c_blkno, rec);
	printf("\n");
}

static void print_ocfs2_extent_list(void)
{
	ocfs2_extent_list rec;

	SHOW_SIZEOF (ocfs2_extent_list, rec);

	SHOW_OFFSET (l_tree_depth, rec);
	SHOW_OFFSET (l_count, rec);
	SHOW_OFFSET (l_next_free_rec, rec);
	SHOW_OFFSET (l_reserved1, rec);
	SHOW_OFFSET (l_reserved2, rec);
	SHOW_OFFSET (l_recs, rec);
	printf("\n");
}

static void print_ocfs2_chain_list(void)
{
	ocfs2_chain_list rec;

	SHOW_SIZEOF (ocfs2_chain_list, rec);

	SHOW_OFFSET (cl_cpg, rec);
	SHOW_OFFSET (cl_bpc, rec);
	SHOW_OFFSET (cl_count, rec);
	SHOW_OFFSET (cl_next_free_rec, rec);
	SHOW_OFFSET (cl_reserved1, rec);
	SHOW_OFFSET (cl_recs, rec);
	printf("\n");
}

static void print_ocfs2_extent_block(void)
{
	ocfs2_extent_block rec;

	SHOW_SIZEOF (ocfs2_extent_block, rec);

	SHOW_OFFSET (h_signature, rec);
	SHOW_OFFSET (h_reserved1, rec);
	SHOW_OFFSET (h_suballoc_node, rec);
	SHOW_OFFSET (h_suballoc_bit, rec);
	SHOW_OFFSET (h_fs_generation, rec);
	SHOW_OFFSET (h_blkno, rec);
	SHOW_OFFSET (h_reserved3, rec);
	SHOW_OFFSET (h_next_leaf_blk, rec);
	SHOW_OFFSET (h_list, rec);
	printf("\n");
}

static void print_ocfs2_super_block(void)
{
	ocfs2_super_block rec;

	SHOW_SIZEOF (ocfs2_super_block, rec);

	SHOW_OFFSET (s_major_rev_level, rec);
	SHOW_OFFSET (s_minor_rev_level, rec);
	SHOW_OFFSET (s_mnt_count, rec);
	SHOW_OFFSET (s_max_mnt_count, rec);
	SHOW_OFFSET (s_state, rec);
	SHOW_OFFSET (s_errors, rec);
	SHOW_OFFSET (s_checkinterval, rec);
	SHOW_OFFSET (s_lastcheck, rec);
	SHOW_OFFSET (s_creator_os, rec);
	SHOW_OFFSET (s_feature_compat, rec);
	SHOW_OFFSET (s_feature_incompat, rec);
	SHOW_OFFSET (s_feature_ro_compat, rec);
	SHOW_OFFSET (s_root_blkno, rec);
	SHOW_OFFSET (s_system_dir_blkno, rec);
	SHOW_OFFSET (s_blocksize_bits, rec);
	SHOW_OFFSET (s_clustersize_bits, rec);
	SHOW_OFFSET (s_max_nodes, rec);
	SHOW_OFFSET (s_reserved1, rec);
	SHOW_OFFSET (s_reserved2, rec);
	SHOW_OFFSET (s_first_cluster_group, rec);
	SHOW_OFFSET (s_label, rec);
	SHOW_OFFSET (s_uuid, rec);
	printf("\n");
}

static void print_ocfs2_local_alloc(void)
{
	ocfs2_local_alloc rec;

	SHOW_SIZEOF (ocfs2_local_alloc, rec);

	SHOW_OFFSET (la_bm_off, rec);
	SHOW_OFFSET (la_size, rec);
	SHOW_OFFSET (la_reserved1, rec);
	SHOW_OFFSET (la_reserved2, rec);
	SHOW_OFFSET (la_bitmap, rec);
	printf("\n");
}

static void print_ocfs2_dinode(void)
{
	ocfs2_dinode rec;

	SHOW_SIZEOF (ocfs2_dinode, rec);

	SHOW_OFFSET (i_signature, rec);
	SHOW_OFFSET (i_generation, rec);
	SHOW_OFFSET (i_suballoc_node, rec);
	SHOW_OFFSET (i_suballoc_bit, rec);
	SHOW_OFFSET (i_reserved0, rec);
	SHOW_OFFSET (i_clusters, rec);
	SHOW_OFFSET (i_uid, rec);
	SHOW_OFFSET (i_gid, rec);
	SHOW_OFFSET (i_size, rec);
	SHOW_OFFSET (i_mode, rec);
	SHOW_OFFSET (i_links_count, rec);
	SHOW_OFFSET (i_flags, rec);
	SHOW_OFFSET (i_atime, rec);
	SHOW_OFFSET (i_ctime, rec);
	SHOW_OFFSET (i_mtime, rec);
	SHOW_OFFSET (i_dtime, rec);
	SHOW_OFFSET (i_blkno, rec);
	SHOW_OFFSET (i_last_eb_blk, rec);
	SHOW_OFFSET (i_fs_generation, rec);
	SHOW_OFFSET (i_reserved1, rec);
	SHOW_OFFSET (i_reserved2, rec);

	SHOW_OFFSET (id1.i_pad1, rec);
	SHOW_OFFSET (id1.dev1.i_rdev, rec);
	SHOW_OFFSET (id1.bitmap1.i_used, rec);
	SHOW_OFFSET (id1.bitmap1.i_total, rec);
	SHOW_OFFSET (id1.journal1.ij_flags, rec);
	SHOW_OFFSET (id1.journal1.ij_pad, rec);

	SHOW_OFFSET (id2.i_super, rec);
	SHOW_OFFSET (id2.i_lab, rec);
	SHOW_OFFSET (id2.i_chain, rec);
	SHOW_OFFSET (id2.i_list, rec);
	SHOW_OFFSET (id2.i_symlink, rec);
	printf("\n");
}

static void print_ocfs2_dir_entry(void)
{
	struct ocfs2_dir_entry rec;

	SHOW_SIZEOF(struct ocfs2_dir_entry, rec);

	SHOW_OFFSET (inode, rec);
	SHOW_OFFSET (rec_len, rec);
	SHOW_OFFSET (name_len, rec);
	SHOW_OFFSET (file_type, rec);
	SHOW_OFFSET (name, rec);
	printf("\n");
}

static void print_ocfs2_group_desc(void)
{
	ocfs2_group_desc rec;

	SHOW_SIZEOF (ocfs2_group_desc, rec);

	SHOW_OFFSET (bg_signature, rec);
	SHOW_OFFSET (bg_size, rec);
	SHOW_OFFSET (bg_bits, rec);
	SHOW_OFFSET (bg_free_bits_count, rec);
	SHOW_OFFSET (bg_chain, rec);
	SHOW_OFFSET (bg_generation, rec);
	SHOW_OFFSET (bg_reserved1, rec);
	SHOW_OFFSET (bg_next_group, rec);
	SHOW_OFFSET (bg_parent_dinode, rec);
	SHOW_OFFSET (bg_blkno, rec);
	SHOW_OFFSET (bg_reserved2, rec);
	SHOW_OFFSET (bg_bitmap, rec);
	printf("\n");
}

int main()
{
	print_ocfs2_extent_rec();
	print_ocfs2_chain_rec();

	print_ocfs2_extent_list();
	print_ocfs2_chain_list();

	print_ocfs2_extent_block();
	print_ocfs2_super_block();

	print_ocfs2_local_alloc();

	print_ocfs2_dinode();

	print_ocfs2_dir_entry();

	print_ocfs2_group_desc();

	return 0;
}
