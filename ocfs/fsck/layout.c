/*
 * layout.c
 *
 * ocfs file system block structure layouts
 *
 * Copyright (C) 2003 Oracle.  All rights reserved.
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
 * Authors: Kurt Hackel, Sunil Mushran
 */

#include "layout.h"

#define LAYOUT_LOCAL_DEFS
ocfs_disk_structure dirnode_t = {
	dir_node,
       	&ocfs_dir_node_class,
       	dir_node_sig_match,
       	read_dir_node,
//	write_dir_node,
	write_one_sector,
       	verify_dir_node,
       	print_dir_node,
       	get_dir_node_defaults
};
ocfs_disk_structure fileent_t = {
	file_entry,
       	&ocfs_file_entry_class,
       	file_entry_sig_match,
       	read_one_sector,
	write_one_sector,
       	verify_file_entry,
       	print_file_entry,
       	get_file_entry_defaults
};
ocfs_disk_structure exthdr_t = {
	extent_header,
       	&ocfs_extent_group_class,
       	extent_header_sig_match,
       	read_one_sector,
	write_one_sector,
       	verify_extent_header,
       	print_extent_header,
       	get_extent_header_defaults
};
ocfs_disk_structure extdat_t = {
	extent_data,
	&ocfs_extent_group_class,
       	extent_data_sig_match,
       	read_one_sector,
	write_one_sector,
       	verify_extent_data,
       	print_extent_data,
       	get_extent_data_defaults
};
ocfs_disk_structure diskhdr_t = { 
	vol_disk_header,
       	&ocfs_vol_disk_hdr_class,
       	NULL,
       	read_one_sector,
	write_one_sector,
       	verify_vol_disk_header,
       	print_vol_disk_header,
       	get_vol_disk_header_defaults 
};
ocfs_disk_structure vollabel_t = { 
	vol_label_lock,
       	&ocfs_vol_label_class,
       	NULL,
       	read_one_sector,
	write_one_sector,
       	verify_vol_label,
       	print_vol_label,
       	get_vol_label_defaults 
};
ocfs_disk_structure bmlock_t = {
	bitmap_lock,
       	&ocfs_disk_lock_class,
       	NULL,
       	read_one_sector,
	write_one_sector,
       	verify_disk_lock,
       	print_disk_lock,
       	get_disk_lock_defaults
};
ocfs_disk_structure nmlock_t = {
	nm_lock,
       	&ocfs_disk_lock_class,
       	NULL,
       	read_one_sector,
	write_one_sector,
       	verify_disk_lock,
       	print_disk_lock,
       	get_disk_lock_defaults
};
ocfs_disk_structure unused_t = {
	unused,
       	NULL,
       	NULL,
       	NULL,
	NULL,
       	NULL,
       	NULL,
       	NULL,
};
ocfs_disk_structure free_t = {
	free_sector,
       	NULL,
       	NULL,
       	NULL,
	NULL,
       	NULL,
       	NULL,
       	NULL,
};
ocfs_disk_structure nodecfghdr_t = {
	node_cfg_hdr,
       	&ocfs_node_config_hdr_class,
       	nodecfghdr_sig_match,
       	read_one_sector,
	write_one_sector,
       	verify_nodecfghdr,
       	print_nodecfghdr,
       	get_nodecfghdr_defaults
};
ocfs_disk_structure nodecfginfo_t = {
	node_cfg_info,
       	&ocfs_disk_node_config_info_class,
       	NULL,
       	read_one_sector,
	write_one_sector,
       	verify_nodecfginfo,
       	print_nodecfginfo,
       	get_nodecfginfo_defaults
};
ocfs_disk_structure publish_t = {
	publish_sector,
       	&ocfs_publish_class,
       	NULL,
       	read_one_sector,
	write_one_sector,
       	verify_publish_sector,
       	print_publish_sector,
       	get_publish_sector_defaults
};
ocfs_disk_structure vote_t = {
	vote_sector,
       	&ocfs_vote_class,
       	vote_sector_sig_match,
       	read_one_sector,
	write_one_sector,
       	verify_vote_sector,
       	print_vote_sector,
       	get_vote_sector_defaults
};
ocfs_disk_structure volbm_t = {
	volume_bitmap,
       	NULL,
       	NULL,
       	read_volume_bitmap,
	write_volume_bitmap,
       	verify_volume_bitmap,
       	print_volume_bitmap,
       	NULL
};
ocfs_disk_structure volmd_t = {
	vol_metadata,
       	&ocfs_file_entry_class,
       	file_entry_sig_match,
       	read_one_sector,
	write_one_sector,
       	verify_vol_metadata,
       	print_vol_metadata,
       	get_vol_metadata_defaults
};
ocfs_disk_structure volmdlog_t = {
	vol_metadata_log,
       	&ocfs_file_entry_class,
       	file_entry_sig_match,
       	read_one_sector,
	write_one_sector,
       	verify_vol_metadata_log,
       	print_vol_metadata_log,
       	get_vol_metadata_log_defaults
};
ocfs_disk_structure diralloc_t = {
	dir_alloc,
       	&ocfs_file_entry_class,
       	file_entry_sig_match,
       	read_one_sector,
	write_one_sector,
       	verify_dir_alloc,
       	print_dir_alloc,
       	get_dir_alloc_defaults
};
ocfs_disk_structure dirallocbm_t = {
	dir_alloc_bitmap,
       	&ocfs_file_entry_class,
       	file_entry_sig_match,
       	read_one_sector,
	write_one_sector,
       	verify_dir_alloc_bitmap,
       	print_dir_alloc_bitmap,
       	get_dir_alloc_bitmap_defaults
};
ocfs_disk_structure filealloc_t = {
	file_alloc,
       	&ocfs_file_entry_class,
       	file_entry_sig_match,
       	read_one_sector,
	write_one_sector,
       	verify_file_alloc,
       	print_file_alloc,
       	get_file_alloc_defaults
};
ocfs_disk_structure fileallocbm_t = {
	file_alloc_bitmap,
       	&ocfs_file_entry_class,
       	file_entry_sig_match,
       	read_one_sector,
	write_one_sector,
       	verify_file_alloc_bitmap,
       	print_file_alloc_bitmap,
       	get_file_alloc_bitmap_defaults
};
ocfs_disk_structure recover_t = {
	recover_log,
       	&ocfs_file_entry_class,
       	file_entry_sig_match,
       	read_one_sector,
	write_one_sector,
       	verify_recover_log,
       	print_recover_log,
       	get_recover_log_defaults
};
ocfs_disk_structure cleanup_t = {
	cleanup_log,
       	&ocfs_file_entry_class,
       	file_entry_sig_match,
       	read_one_sector,
	write_one_sector,
       	verify_cleanup_log,
       	print_cleanup_log,
       	get_cleanup_log_defaults
};

ocfs_layout_t ocfs_header_layout[] = 
{
	{ 0, 1, &diskhdr_t, "Volume Header" },
	{ 1, 1, &vollabel_t, "Volume Label" },
	{ 2, 1, &bmlock_t, "Bitmap Lock" },
	{ 3, 1, &nmlock_t, "NM Lock" },
	{ 4, 4, &unused_t, "" },
	{ 8, 1, &nodecfghdr_t, "Node Config Header" },
	{ 9, 1, &unused_t, "" },
	{ 10, 32, &nodecfginfo_t, "Node Config Info" },
	{ 42, 1, &unused_t, "" },
	{ 43, 1, &nodecfghdr_t, "Node Config Trailer" },
	{ 44, 2, &unused_t, "" },
	{ 46, 32, &publish_t, "Publish" },
	{ 78, 32, &vote_t, "Vote" },
	{ 110, 2048, &volbm_t, "Volume Bitmap File" },
	{ 2158, 514, &free_t, "Free Bitmap File" },
	{ 2672, 32, &volmd_t, "Volume Metadata File" },
	{ 2704, 32, &volmdlog_t, "Volume Metadata Logfile" },
	{ 2736, 32, &diralloc_t, "Directory Alloc File" },
	{ 2768, 32, &dirallocbm_t, "Directory Alloc Bitmap File" },
	{ 2800, 32, &filealloc_t, "Extent Alloc File" },
	{ 2832, 32, &fileallocbm_t, "Extent Alloc Bitmap File" },
	{ 2864, 32, &recover_t, "Recover File" },
	{ 2896, 32, &cleanup_t, "Cleanup File" }
};
int ocfs_header_layout_sz = sizeof(ocfs_header_layout)/sizeof(ocfs_layout_t);

ocfs_layout_t ocfs_data_layout[] = 
{
	{ ANY_BLOCK, 256, &dirnode_t, "directory node" },
	{ ANY_BLOCK, 1, &fileent_t, "file entry" },
	{ ANY_BLOCK, 1, &exthdr_t, "extent header" },
	{ ANY_BLOCK, 1, &extdat_t, "extent data" }
};
int ocfs_data_layout_sz = sizeof(ocfs_data_layout)/sizeof(ocfs_layout_t);

ocfs_layout_t ocfs_dir_layout[] = 
{
	{ 0, 1, &dirnode_t },
	{ 1, 255, &fileent_t }
};
int ocfs_dir_layout_sz = sizeof(ocfs_dir_layout)/sizeof(ocfs_layout_t);

ocfs_disk_structure *ocfs_all_structures[] = 
{
	&dirnode_t,
	&fileent_t,
	&exthdr_t,
	&extdat_t,
	&diskhdr_t,
	&publish_t,
	&vote_t,
	&volmd_t,
	&volmdlog_t,
	&diralloc_t,
	&dirallocbm_t,
	&filealloc_t,
	&fileallocbm_t,
	&recover_t,
	&cleanup_t,
	&vollabel_t,
	&bmlock_t,
	&nmlock_t,
	&nodecfghdr_t,
	&nodecfginfo_t,
	&volbm_t,
	&unused_t,
	&free_t
};
int ocfs_all_structures_sz = sizeof(ocfs_all_structures)/sizeof(ocfs_disk_structure);

/* 
     autoconfig subtypes

int get_autoconfig_header_copy_defaults (char *buf, char **out, int idx);
int get_autoconfig_header_defaults (char *buf, char **out, int idx);
int get_autoconfig_node_info_defaults (char *buf, char **out, int idx);
int autoconfig_header_copy_sig_match (char *buf, int idx);
int autoconfig_header_sig_match (char *buf, int idx);
int autoconfig_node_info_sig_match (char *buf, int idx);
int print_autoconfig_header (char *buf, int idx);
int print_autoconfig_header_copy (char *buf, int idx);
int print_autoconfig_node_info (char *buf, int idx);
int verify_autoconfig_header (int fd, char *buf, int idx);
int verify_autoconfig_header_copy (int fd, char *buf, int idx);
int verify_autoconfig_node_info (int fd, char *buf, int idx);


ocfs_disk_structure autoconfig_hdr_t = {
	autoconfig_header,
	autoconfig_header_sig_match,
	read_one_sector,
	write_one_sector,
	verify_autoconfig_header,
	print_autoconfig_header,
	get_autoconfig_header_defaults
};
ocfs_disk_structure autoconfig_node_info_t = {
	autoconfig_node_info,
	autoconfig_node_info_sig_match,
	read_one_sector,
	write_one_sector,
	verify_autoconfig_node_info,
	print_autoconfig_node_info,
	get_autoconfig_node_info_defaults
};
ocfs_disk_structure autoconfig_hdr_copy_t = {
	autoconfig_header_copy,
	autoconfig_header_copy_sig_match,
	read_one_sector,
	write_one_sector,
	verify_autoconfig_header_copy,
	print_autoconfig_header_copy,
	get_autoconfig_header_copy_defaults
};

ocfs_layout_t ocfs_autoconfig_layout[] = 
{
	{ 0, 1, autoconfig_hdr_t },
	{ 1, 1, unused_t },
	{ 2, 32, autoconfig_node_info_t },
	{ 34, 1, unused_t },
	{ 35, 1, autoconfig_hdr_copy_t }
};
int ocfs_autoconfig_layout_sz = sizeof(ocfs_autoconfig_layout)/sizeof(ocfs_layout_t);

*/       




ocfs_layout_t * find_nxt_hdr_struct(int type, int start)
{
	int i;
	ocfs_disk_structure *s;
	ocfs_layout_t *ret = NULL;

	for (i=start; i<ocfs_header_layout_sz; i++)
	{
		ret = &(ocfs_header_layout[i]);
		s = ret->kind;
		if (s->type != type)
			ret = NULL;
		else
			break;
	}
	return ret;
}
								

ocfs_disk_structure * find_matching_struct(char *buf, int idx)
{
	int i;
	ocfs_disk_structure *s;

	for (i=0; i<ocfs_all_structures_sz; i++)
	{
		s = ocfs_all_structures[i];
		if (s->sig_match == NULL)
			continue;
		if (s->sig_match(buf, idx) == 0)
		{
			return s;
		}
	}
	return NULL;
}



#undef LAYOUT_LOCAL_DEFS

