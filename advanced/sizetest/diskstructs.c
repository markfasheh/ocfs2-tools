/*
 * diskstructs.c
 *
 * Prints sizes and offsets of structures and its elements.
 * Useful to ensure cross platform compatibility.
 *
 * Copyright (C) 2003 Oracle Corporation.  All rights reserved.
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
 * Authors: Kurt Hackel, Sunil Mushran, Manish Singh, Wim Coekaerts
 */

#include "sizetest.h"

bool show_all = false;

void __wake_up(wait_queue_head_t *q, unsigned int mode, int nr)
{
    return;
}

static void print_ocfs_offset_map()
{
	ocfs_offset_map s;

	SHOW_SIZEOF(ocfs_offset_map, s);
	if (show_all) {
		SHOW_OFFSET(length, s);
		SHOW_OFFSET(log_disk_off, s);
		SHOW_OFFSET(actual_disk_off, s);
		printf("\n");
	}
}


static void print_ocfs_cleanup_record()
{
	ocfs_cleanup_record s;

	SHOW_SIZEOF(ocfs_cleanup_record, s);
	if (show_all) {
		SHOW_OFFSET(log_id, s);
		SHOW_OFFSET(log_type, s);;
		SHOW_OFFSET(rec.lock, s);
		SHOW_OFFSET(rec.alloc, s);
		SHOW_OFFSET(rec.bcast, s);
		SHOW_OFFSET(rec.del, s);
		SHOW_OFFSET(rec.free, s);
		printf("\n");
	}
}


void usage(void)
{
	printf("usage: diskstructs [all]\n");
	return ;
}


static void print_ocfs_alloc_ext()
{
	ocfs_alloc_ext s;

	SHOW_SIZEOF(ocfs_alloc_ext, s);
	if (show_all) {
		SHOW_OFFSET(file_off, s);
		SHOW_OFFSET(num_bytes, s);
		SHOW_OFFSET(disk_off, s);
		printf("\n");
	}
}


static void print_ocfs_publish()
{
	ocfs_publish s;
	int i;

	SHOW_SIZEOF(ocfs_publish, s);
	if (show_all) {
		SHOW_OFFSET(time, s);
		SHOW_OFFSET(vote, s);
		SHOW_OFFSET(dirty, s);
		SHOW_OFFSET(vote_type, s);
		SHOW_OFFSET(vote_map, s);
		SHOW_OFFSET(publ_seq_num, s);
		SHOW_OFFSET(dir_ent, s);
		for (i = 0; i < OCFS_MAXIMUM_NODES; ++i)
			SHOW_OFFSET(hbm[i], s);
		SHOW_OFFSET(comm_seq_num, s);
		printf("\n");
	}
}


static void print_ocfs_vote()
{
	ocfs_vote s;
	int i;

	SHOW_SIZEOF(ocfs_vote, s);
	if (show_all) {
		for (i = 0; i < OCFS_MAXIMUM_NODES; ++i)
			SHOW_OFFSET(vote[i], s);
		SHOW_OFFSET(vote_seq_num, s);
		SHOW_OFFSET(dir_ent, s);
		SHOW_OFFSET(open_handle, s);
		printf("\n");
	}
}


static void print_ocfs_file_entry()
{
	ocfs_file_entry s;
	int i;

	SHOW_SIZEOF(ocfs_file_entry, s);
	if (show_all) {
		SHOW_OFFSET(disk_lock, s);
		SHOW_OFFSET(signature, s);
		SHOW_OFFSET(local_ext, s);
		SHOW_OFFSET(next_free_ext, s);
		SHOW_OFFSET(next_del, s);
		SHOW_OFFSET(granularity, s);
		SHOW_OFFSET(filename, s);
		SHOW_OFFSET(filename_len, s);
		SHOW_OFFSET(file_size, s);
		SHOW_OFFSET(alloc_size, s);
		SHOW_OFFSET(create_time, s);
		SHOW_OFFSET(modify_time, s);
		for (i = 0; i < OCFS_MAX_FILE_ENTRY_EXTENTS; ++i)
			SHOW_OFFSET(extents[i], s);
		SHOW_OFFSET(dir_node_ptr, s);
		SHOW_OFFSET(this_sector, s);
		SHOW_OFFSET(last_ext_ptr, s);
		SHOW_OFFSET(sync_flags, s);
		SHOW_OFFSET(link_cnt, s);
		SHOW_OFFSET(attribs, s);
		SHOW_OFFSET(prot_bits, s);
		SHOW_OFFSET(uid, s);
		SHOW_OFFSET(gid, s);
		SHOW_OFFSET(dev_major, s);
		SHOW_OFFSET(dev_minor, s);
		printf("\n");
	}
}


static void print_ocfs_index_node()
{
	ocfs_index_node s;

	SHOW_SIZEOF(ocfs_index_node, s);
	if (show_all) {
		SHOW_OFFSET(down_ptr, s);
		SHOW_OFFSET(file_ent_ptr, s);
		SHOW_OFFSET(name_len, s);
		SHOW_OFFSET(name, s);
		printf("\n");
	}
}


static void print_ocfs_index_hdr()
{
	ocfs_index_hdr s;

	SHOW_SIZEOF(ocfs_index_hdr, s);
	if (show_all) {
		SHOW_OFFSET(disk_lock, s);
		SHOW_OFFSET(signature, s);
		SHOW_OFFSET(up_tree_ptr, s);
		SHOW_OFFSET(node_disk_off, s);
		SHOW_OFFSET(state, s);
		SHOW_OFFSET(down_ptr, s);
		SHOW_OFFSET(num_ents, s);
		SHOW_OFFSET(depth, s);
		SHOW_OFFSET(num_ent_used, s);
		SHOW_OFFSET(dir_node_flags, s);
		SHOW_OFFSET(sync_flags, s);
		SHOW_OFFSET(index, s);
		SHOW_OFFSET(reserved, s);
		SHOW_OFFSET(file_ent, s);
		printf("\n");
	}
}


static void print_ocfs_dir_node()
{
	ocfs_dir_node s;

	SHOW_SIZEOF(ocfs_dir_node, s);
	if (show_all) {
		SHOW_OFFSET(disk_lock, s);
		SHOW_OFFSET(signature, s);
		SHOW_OFFSET(alloc_file_off, s);
		SHOW_OFFSET(alloc_node, s);
		SHOW_OFFSET(free_node_ptr, s);
		SHOW_OFFSET(node_disk_off, s);
		SHOW_OFFSET(next_node_ptr, s);
		SHOW_OFFSET(indx_node_ptr, s);
		SHOW_OFFSET(next_del_ent_node, s);
		SHOW_OFFSET(head_del_ent_node, s);
		SHOW_OFFSET(first_del, s);
		SHOW_OFFSET(num_del, s);
		SHOW_OFFSET(num_ents, s);
		SHOW_OFFSET(depth, s);
		SHOW_OFFSET(num_ent_used, s);
		SHOW_OFFSET(dir_node_flags, s);
		SHOW_OFFSET(sync_flags, s);
		SHOW_OFFSET(index, s);
		SHOW_OFFSET(index_dirty, s);
		SHOW_OFFSET(bad_off, s);
		SHOW_OFFSET(reserved, s);
		SHOW_OFFSET(file_ent, s);
		printf("\n");
	}
}


static void print_ocfs_extent_group()
{
	ocfs_extent_group s;
	int i;

	SHOW_SIZEOF(ocfs_extent_group, s);
	if (show_all) {
		SHOW_OFFSET(signature, s);
		SHOW_OFFSET(next_free_ext, s);
		SHOW_OFFSET(curr_sect, s);
		SHOW_OFFSET(max_sects, s);
		SHOW_OFFSET(type, s);
		SHOW_OFFSET(granularity, s);
		SHOW_OFFSET(alloc_node, s);
		SHOW_OFFSET(this_ext, s);
		SHOW_OFFSET(next_data_ext, s);
		SHOW_OFFSET(alloc_file_off, s);
		SHOW_OFFSET(last_ext_ptr, s);
		SHOW_OFFSET(up_hdr_node_ptr, s);
		for (i = 0; i < OCFS_MAX_DATA_EXTENTS; ++i)
			SHOW_OFFSET(extents[i], s);
		printf("\n");
	}
}


static void print_ocfs_bitmap_lock()
{
	ocfs_bitmap_lock s;

	SHOW_SIZEOF(ocfs_bitmap_lock, s);
	if (show_all) {
		SHOW_OFFSET(disk_lock, s);
		SHOW_OFFSET(used_bits, s);
		printf("\n");
	}
}


static void print_ocfs_vol_disk_hdr()
{
	ocfs_vol_disk_hdr s;

	SHOW_SIZEOF(ocfs_vol_disk_hdr, s);
	if (show_all) {
		SHOW_OFFSET(minor_version, s);
		SHOW_OFFSET(major_version, s);
		SHOW_OFFSET(signature, s);
		SHOW_OFFSET(mount_point, s);
		SHOW_OFFSET(serial_num, s);
		SHOW_OFFSET(device_size, s);
		SHOW_OFFSET(start_off, s);
		SHOW_OFFSET(bitmap_off, s);
		SHOW_OFFSET(publ_off, s);
		SHOW_OFFSET(vote_off, s);
		SHOW_OFFSET(root_bitmap_off, s);
		SHOW_OFFSET(data_start_off, s);
		SHOW_OFFSET(root_bitmap_size, s);
		SHOW_OFFSET(root_off, s);
		SHOW_OFFSET(root_size, s);
		SHOW_OFFSET(cluster_size, s);
		SHOW_OFFSET(num_nodes, s);
		SHOW_OFFSET(num_clusters, s);
		SHOW_OFFSET(dir_node_size, s);
		SHOW_OFFSET(file_node_size, s);
		SHOW_OFFSET(internal_off, s);
		SHOW_OFFSET(node_cfg_off, s);
		SHOW_OFFSET(node_cfg_size, s);
		SHOW_OFFSET(new_cfg_off, s);
		SHOW_OFFSET(prot_bits, s);
		SHOW_OFFSET(uid, s);
		SHOW_OFFSET(gid, s);
		SHOW_OFFSET(excl_mount, s);
		printf("\n");
	}
}


static void print_ocfs_disk_lock()
{
	ocfs_disk_lock s;

	SHOW_SIZEOF(ocfs_disk_lock, s);
	if (show_all) {
		SHOW_OFFSET(curr_master, s);
		SHOW_OFFSET(file_lock, s);
		SHOW_OFFSET(last_write_time, s);
		SHOW_OFFSET(last_read_time, s);
		SHOW_OFFSET(writer_node_num, s);
		SHOW_OFFSET(reader_node_num, s);
		SHOW_OFFSET(oin_node_map, s);
		SHOW_OFFSET(dlock_seq_num, s);
		printf("\n");
	}
}


static void print_ocfs_vol_label()
{
	ocfs_vol_label s;

	SHOW_SIZEOF(ocfs_vol_label, s);
	if (show_all) {
		SHOW_OFFSET(disk_lock, s);
		SHOW_OFFSET(label, s);
		SHOW_OFFSET(label_len, s);
		SHOW_OFFSET(vol_id, s);
		SHOW_OFFSET(vol_id_len, s);
		SHOW_OFFSET(cluster_name, s);
		SHOW_OFFSET(cluster_name_len, s);
		printf("\n");
	}
}


static void print_ocfs_ipc_config_info()
{
	ocfs_ipc_config_info s;

	SHOW_SIZEOF(ocfs_ipc_config_info, s);
	if (show_all) {
		SHOW_OFFSET(type, s);
		SHOW_OFFSET(ip_addr, s);
		SHOW_OFFSET(ip_port, s);
		SHOW_OFFSET(ip_mask, s);
		printf("\n");
	}
}


static void print_ocfs_guid()
{
	ocfs_guid s;

	SHOW_SIZEOF(ocfs_guid, s);
	if (show_all) {
		SHOW_OFFSET(guid, s);
		SHOW_OFFSET(id.host_id, s);
		SHOW_OFFSET(id.mac_id, s);
		printf("\n");
	}
}


static void print_ocfs_disk_node_config_info()
{
	ocfs_disk_node_config_info s;

	SHOW_SIZEOF(ocfs_disk_node_config_info, s);
	if (show_all) {
		SHOW_OFFSET(disk_lock, s);
		SHOW_OFFSET(node_name, s);
		SHOW_OFFSET(guid, s);
		SHOW_OFFSET(ipc_config, s);
		printf("\n");
	}
}


static void print_ocfs_node_config_hdr()
{
	ocfs_node_config_hdr s;

	SHOW_SIZEOF(ocfs_node_config_hdr, s);
	if (show_all) {
		SHOW_OFFSET(disk_lock, s);
		SHOW_OFFSET(signature, s);
		SHOW_OFFSET(version, s);
		SHOW_OFFSET(num_nodes, s);
		SHOW_OFFSET(last_node, s);
		SHOW_OFFSET(cfg_seq_num, s);
		printf("\n");
	}
}


static void print_ocfs_cdsl()
{
	ocfs_cdsl s;

	SHOW_SIZEOF(ocfs_cdsl, s);
	if (show_all) {
		SHOW_OFFSET(name, s);
		SHOW_OFFSET(flags, s);
		SHOW_OFFSET(operation, s);
		printf("\n");
	}
}


static void print_ocfs_free_bitmap()
{
	ocfs_free_bitmap s;

	SHOW_SIZEOF(ocfs_free_bitmap, s);
	if (show_all) {
		SHOW_OFFSET(length, s);
		SHOW_OFFSET(file_off, s);
		SHOW_OFFSET(type, s);
		SHOW_OFFSET(node_num, s);
		printf("\n");
	}
}


static void print_ocfs_free_extent_log()
{
	ocfs_free_extent_log s;

	SHOW_SIZEOF(ocfs_free_extent_log, s);
	if (show_all) {
		SHOW_OFFSET(index, s);
		SHOW_OFFSET(disk_off, s);
		printf("\n");
	}
}


static void print_ocfs_free_log()
{
	ocfs_free_log s;
	int i;

	SHOW_SIZEOF(ocfs_free_log, s);
	if (show_all) {
		SHOW_OFFSET(num_free_upds, s);
		for (i = 0; i < FREE_LOG_SIZE; ++i)
			SHOW_OFFSET(free_bitmap[i], s);
		printf("\n");
	}
}


static void print_ocfs_delete_log()
{
	ocfs_delete_log s;

	SHOW_SIZEOF(ocfs_delete_log, s);
	if (show_all) {
		SHOW_OFFSET(node_num, s);
		SHOW_OFFSET(ent_del, s);
		SHOW_OFFSET(parent_dirnode_off, s);
		SHOW_OFFSET(flags, s);
		printf("\n");
	}
}


static void print_ocfs_recovery_log()
{
	ocfs_recovery_log s;

	SHOW_SIZEOF(ocfs_recovery_log, s);
	if (show_all) {
		SHOW_OFFSET(node_num, s);
		printf("\n");
	}
}


static void print_ocfs_alloc_log()
{
	ocfs_alloc_log s;

	SHOW_SIZEOF(ocfs_alloc_log, s);
	if (show_all) {
		SHOW_OFFSET(length, s);
		SHOW_OFFSET(file_off, s);
		SHOW_OFFSET(type, s);
		SHOW_OFFSET(node_num, s);
		printf("\n");
	}
}


static void print_ocfs_dir_log()
{
	ocfs_dir_log s;

	SHOW_SIZEOF(ocfs_dir_log, s);
	if (show_all) {
		SHOW_OFFSET(orig_off, s);
		SHOW_OFFSET(saved_off, s);
		SHOW_OFFSET(length, s);
		printf("\n");
	}
}


static void print_ocfs_lock_update()
{
	ocfs_lock_update s;

	SHOW_SIZEOF(ocfs_lock_update, s);
	if (show_all) {
		SHOW_OFFSET(orig_off, s);
		SHOW_OFFSET(new_off, s);
		printf("\n");
	}
}


static void print_ocfs_lock_log()
{
	ocfs_lock_log s;
	int i;

	SHOW_SIZEOF(ocfs_lock_log, s);
	if (show_all) {
		SHOW_OFFSET(num_lock_upds, s);
		for (i = 0; i < LOCK_UPDATE_LOG_SIZE; ++i)
			SHOW_OFFSET(lock_upd[i], s);
		printf("\n");
	}
}


static void print_ocfs_bcast_rel_log()
{
	ocfs_bcast_rel_log s;

	SHOW_SIZEOF(ocfs_bcast_rel_log, s);
	if (show_all) {
		SHOW_OFFSET(lock_id, s);
		printf("\n");
	}
}


static void print_ocfs_log_record()
{
	ocfs_log_record s;

	SHOW_SIZEOF(ocfs_log_record, s);
	if (show_all) {
		SHOW_OFFSET(log_id, s);
		SHOW_OFFSET(log_type, s);
		SHOW_OFFSET(rec.dir, s);
		SHOW_OFFSET(rec.alloc, s);
		SHOW_OFFSET(rec.recovery, s);
		SHOW_OFFSET(rec.bcast, s);
		SHOW_OFFSET(rec.del, s);
		SHOW_OFFSET(rec.extent, s);
		printf("\n");
	}
}


int main(int argc, char **argv)
{
	if (argc > 1) {
		if (!strncasecmp(*++argv, "all", 3))
			show_all = true;
		else {
			usage();
			exit (1);
		}
	}

	print_ocfs_alloc_ext();
	print_ocfs_publish();
	print_ocfs_vote();
	print_ocfs_file_entry();
	print_ocfs_index_node();
	print_ocfs_index_hdr();
	print_ocfs_dir_node();
	print_ocfs_extent_group();
	print_ocfs_bitmap_lock();

	print_ocfs_offset_map();

	print_ocfs_vol_disk_hdr();
	print_ocfs_disk_lock();
	print_ocfs_vol_label();
	print_ocfs_ipc_config_info();
	print_ocfs_guid();
	print_ocfs_disk_node_config_info();
	print_ocfs_node_config_hdr();
	print_ocfs_cdsl();

	print_ocfs_free_bitmap();
	print_ocfs_free_extent_log();
	print_ocfs_free_log();
	print_ocfs_delete_log();
	print_ocfs_recovery_log();
	print_ocfs_alloc_log();
	print_ocfs_dir_log();
	print_ocfs_lock_log();
	print_ocfs_lock_update();
	print_ocfs_bcast_rel_log();
	print_ocfs_cleanup_record();
	print_ocfs_log_record();

	return 0;
}				/* main */
