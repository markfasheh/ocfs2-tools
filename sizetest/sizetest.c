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


#undef offsetof
#define offsetof(TYPE, MEMBER) ((unsigned int) &((TYPE *)0)->MEMBER)
#define ssizeof(TYPE, MEMBER) ((unsigned int) sizeof(((TYPE *)0)->MEMBER))

#define START_TYPE(TYPE) do { \
    printf("[off]\t%- 20s\t[size]\n", #TYPE); \
} while (0)

#define SHOW_OFFSET(TYPE, MEMBER) do { \
    printf("0x%03X\t%- 20s\t+0x%02X\n", offsetof(TYPE, MEMBER), #MEMBER, ssizeof(TYPE, MEMBER)); \
} while (0)

#define END_TYPE(TYPE) do {\
    printf("\t%- 20s\t0x%03X\n", "Total", sizeof(*((TYPE *)0))); \
} while (0)

static void print_ocfs2_extent_rec(void)
{
	START_TYPE(ocfs2_extent_rec);

	SHOW_OFFSET(struct ocfs2_extent_rec, e_cpos);
	SHOW_OFFSET(struct ocfs2_extent_rec, e_int_clusters);
	SHOW_OFFSET(struct ocfs2_extent_rec, e_blkno);

	END_TYPE(struct ocfs2_extent_rec);
	printf("\n");
}

static void print_ocfs2_chain_rec(void)
{
	START_TYPE(ocfs2_chain_rec);

	SHOW_OFFSET(struct ocfs2_chain_rec, c_free);
	SHOW_OFFSET(struct ocfs2_chain_rec, c_total);
	SHOW_OFFSET(struct ocfs2_chain_rec, c_blkno);
	
        END_TYPE(struct ocfs2_chain_rec);
        printf("\n");
}

static void print_ocfs2_extent_list(void)
{
	START_TYPE(ocfs2_extent_list);

	SHOW_OFFSET(struct ocfs2_extent_list, l_tree_depth);
	SHOW_OFFSET(struct ocfs2_extent_list, l_count);
	SHOW_OFFSET(struct ocfs2_extent_list, l_next_free_rec);
	SHOW_OFFSET(struct ocfs2_extent_list, l_reserved1);
	SHOW_OFFSET(struct ocfs2_extent_list, l_reserved2);
	SHOW_OFFSET(struct ocfs2_extent_list, l_recs);
	
        END_TYPE(struct ocfs2_extent_list);
        printf("\n");
}

static void print_ocfs2_chain_list(void)
{
	START_TYPE(ocfs2_chain_list);

	SHOW_OFFSET(struct ocfs2_chain_list, cl_cpg);
	SHOW_OFFSET(struct ocfs2_chain_list, cl_bpc);
	SHOW_OFFSET(struct ocfs2_chain_list, cl_count);
	SHOW_OFFSET(struct ocfs2_chain_list, cl_next_free_rec);
	SHOW_OFFSET(struct ocfs2_chain_list, cl_reserved1);
	SHOW_OFFSET(struct ocfs2_chain_list, cl_recs);
	
        END_TYPE(struct ocfs2_chain_list);
        printf("\n");
}

static void print_ocfs2_extent_block(void)
{
	START_TYPE(ocfs2_extent_block);

	SHOW_OFFSET(struct ocfs2_extent_block, h_signature);
	SHOW_OFFSET(struct ocfs2_extent_block, h_reserved1);
	SHOW_OFFSET(struct ocfs2_extent_block, h_suballoc_slot);
	SHOW_OFFSET(struct ocfs2_extent_block, h_suballoc_bit);
	SHOW_OFFSET(struct ocfs2_extent_block, h_fs_generation);
	SHOW_OFFSET(struct ocfs2_extent_block, h_blkno);
	SHOW_OFFSET(struct ocfs2_extent_block, h_reserved3);
	SHOW_OFFSET(struct ocfs2_extent_block, h_next_leaf_blk);
	SHOW_OFFSET(struct ocfs2_extent_block, h_list);
	
        END_TYPE(struct ocfs2_extent_block);
        printf("\n");
}

static void print_ocfs2_super_block(void)
{
	START_TYPE(ocfs2_super_block);

	SHOW_OFFSET(struct ocfs2_super_block, s_major_rev_level);
	SHOW_OFFSET(struct ocfs2_super_block, s_minor_rev_level);
	SHOW_OFFSET(struct ocfs2_super_block, s_mnt_count);
	SHOW_OFFSET(struct ocfs2_super_block, s_max_mnt_count);
	SHOW_OFFSET(struct ocfs2_super_block, s_state);
	SHOW_OFFSET(struct ocfs2_super_block, s_errors);
	SHOW_OFFSET(struct ocfs2_super_block, s_checkinterval);
	SHOW_OFFSET(struct ocfs2_super_block, s_lastcheck);
	SHOW_OFFSET(struct ocfs2_super_block, s_creator_os);
	SHOW_OFFSET(struct ocfs2_super_block, s_feature_compat);
	SHOW_OFFSET(struct ocfs2_super_block, s_feature_incompat);
	SHOW_OFFSET(struct ocfs2_super_block, s_feature_ro_compat);
	SHOW_OFFSET(struct ocfs2_super_block, s_root_blkno);
	SHOW_OFFSET(struct ocfs2_super_block, s_system_dir_blkno);
	SHOW_OFFSET(struct ocfs2_super_block, s_blocksize_bits);
	SHOW_OFFSET(struct ocfs2_super_block, s_clustersize_bits);
	SHOW_OFFSET(struct ocfs2_super_block, s_max_slots);
	SHOW_OFFSET(struct ocfs2_super_block, s_tunefs_flag);
	SHOW_OFFSET(struct ocfs2_super_block, s_reserved1);
	SHOW_OFFSET(struct ocfs2_super_block, s_first_cluster_group);
	SHOW_OFFSET(struct ocfs2_super_block, s_label);
	SHOW_OFFSET(struct ocfs2_super_block, s_uuid);
	
        END_TYPE(struct ocfs2_super_block);
        printf("\n");
}

static void print_ocfs2_local_alloc(void)
{
	START_TYPE(ocfs2_local_alloc);

	SHOW_OFFSET(struct ocfs2_local_alloc, la_bm_off);
	SHOW_OFFSET(struct ocfs2_local_alloc, la_size);
	SHOW_OFFSET(struct ocfs2_local_alloc, la_reserved1);
	SHOW_OFFSET(struct ocfs2_local_alloc, la_reserved2);
	SHOW_OFFSET(struct ocfs2_local_alloc, la_bitmap);
	
        END_TYPE(struct ocfs2_local_alloc);
        printf("\n");
}

static void print_ocfs2_dinode(void)
{
	START_TYPE(ocfs2_dinode);

	SHOW_OFFSET(struct ocfs2_dinode, i_signature);
	SHOW_OFFSET(struct ocfs2_dinode, i_generation);
	SHOW_OFFSET(struct ocfs2_dinode, i_suballoc_slot);
	SHOW_OFFSET(struct ocfs2_dinode, i_suballoc_bit);
	SHOW_OFFSET(struct ocfs2_dinode, i_reserved0);
	SHOW_OFFSET(struct ocfs2_dinode, i_clusters);
	SHOW_OFFSET(struct ocfs2_dinode, i_uid);
	SHOW_OFFSET(struct ocfs2_dinode, i_gid);
	SHOW_OFFSET(struct ocfs2_dinode, i_size);
	SHOW_OFFSET(struct ocfs2_dinode, i_mode);
	SHOW_OFFSET(struct ocfs2_dinode, i_links_count);
	SHOW_OFFSET(struct ocfs2_dinode, i_flags);
	SHOW_OFFSET(struct ocfs2_dinode, i_atime);
	SHOW_OFFSET(struct ocfs2_dinode, i_ctime);
	SHOW_OFFSET(struct ocfs2_dinode, i_mtime);
	SHOW_OFFSET(struct ocfs2_dinode, i_dtime);
	SHOW_OFFSET(struct ocfs2_dinode, i_blkno);
	SHOW_OFFSET(struct ocfs2_dinode, i_last_eb_blk);
	SHOW_OFFSET(struct ocfs2_dinode, i_fs_generation);
	SHOW_OFFSET(struct ocfs2_dinode, i_atime_nsec);
	SHOW_OFFSET(struct ocfs2_dinode, i_ctime_nsec);
	SHOW_OFFSET(struct ocfs2_dinode, i_mtime_nsec);
	SHOW_OFFSET(struct ocfs2_dinode, i_attr);
	SHOW_OFFSET(struct ocfs2_dinode, i_dyn_features);
	SHOW_OFFSET(struct ocfs2_dinode, i_reserved2);

	SHOW_OFFSET(struct ocfs2_dinode, id1.i_pad1);
	SHOW_OFFSET(struct ocfs2_dinode, id1.dev1.i_rdev);
	SHOW_OFFSET(struct ocfs2_dinode, id1.bitmap1.i_used);
	SHOW_OFFSET(struct ocfs2_dinode, id1.bitmap1.i_total);
	SHOW_OFFSET(struct ocfs2_dinode, id1.journal1.ij_flags);
	SHOW_OFFSET(struct ocfs2_dinode, id1.journal1.ij_pad);

	SHOW_OFFSET(struct ocfs2_dinode, id2.i_super);
	SHOW_OFFSET(struct ocfs2_dinode, id2.i_lab);
	SHOW_OFFSET(struct ocfs2_dinode, id2.i_chain);
	SHOW_OFFSET(struct ocfs2_dinode, id2.i_list);
	SHOW_OFFSET(struct ocfs2_dinode, id2.i_symlink);
	
        END_TYPE(struct ocfs2_dinode);
        printf("\n");
}

static void print_ocfs2_dir_entry(void)
{
	START_TYPE(struct ocfs2_dir_entry);

	SHOW_OFFSET(struct ocfs2_dir_entry, inode);
	SHOW_OFFSET(struct ocfs2_dir_entry, rec_len);
	SHOW_OFFSET(struct ocfs2_dir_entry, name_len);
	SHOW_OFFSET(struct ocfs2_dir_entry, file_type);
	SHOW_OFFSET(struct ocfs2_dir_entry, name);
	
        END_TYPE(struct ocfs2_dir_entry);
        printf("\n");
}

static void print_ocfs2_group_desc(void)
{
	START_TYPE(ocfs2_group_desc);

	SHOW_OFFSET(struct ocfs2_group_desc, bg_signature);
	SHOW_OFFSET(struct ocfs2_group_desc, bg_size);
	SHOW_OFFSET(struct ocfs2_group_desc, bg_bits);
	SHOW_OFFSET(struct ocfs2_group_desc, bg_free_bits_count);
	SHOW_OFFSET(struct ocfs2_group_desc, bg_chain);
	SHOW_OFFSET(struct ocfs2_group_desc, bg_generation);
	SHOW_OFFSET(struct ocfs2_group_desc, bg_reserved1);
	SHOW_OFFSET(struct ocfs2_group_desc, bg_next_group);
	SHOW_OFFSET(struct ocfs2_group_desc, bg_parent_dinode);
	SHOW_OFFSET(struct ocfs2_group_desc, bg_blkno);
	SHOW_OFFSET(struct ocfs2_group_desc, bg_reserved2);
	SHOW_OFFSET(struct ocfs2_group_desc, bg_bitmap);
	
        END_TYPE(struct ocfs2_group_desc);
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
