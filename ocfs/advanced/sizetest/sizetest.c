/*
 * sizetest.c
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

static void print_ocfs_extent_map()
{
    ocfs_extent_map i;
    SHOW_SIZEOF(ocfs_extent_map, i);
}

static void print_ocfs_offset_map()
{
    ocfs_offset_map i;
    SHOW_SIZEOF(ocfs_offset_map, i);
    SHOW_OFFSET(length, i);
    SHOW_OFFSET(log_disk_off, i);
    SHOW_OFFSET(actual_disk_off, i);
}

static void print_ocfs_io_runs()
{
    ocfs_io_runs i;
    SHOW_SIZEOF(ocfs_io_runs, i);
}

static void print_ocfs_inode_offsets()
{
    ocfs_inode i;
    SHOW_SIZEOF(ocfs_inode, i);

    if (show_all)
    	SHOW_OFFSET(main_res, i);
}
 
static void print_ocfs_dlm_reply_master_offsets()
{
    ocfs_dlm_reply_master d;
    SHOW_SIZEOF(ocfs_dlm_reply_master, d);
}

static void print_ocfs_dlm_msg_offsets()
{
    ocfs_dlm_msg d;
    SHOW_SIZEOF(ocfs_dlm_msg, d);
}

static void print_ocfs_cleanup_record_offsets()
{
    ocfs_cleanup_record c;
    SHOW_SIZEOF(ocfs_cleanup_record, c);
}

static void print_ocfs_global_ctxt_offsets()
{
    ocfs_global_ctxt c;

    SHOW_SIZEOF(ocfs_global_ctxt, c);

    if (show_all) {
        SHOW_OFFSET(obj_id, c);
        SHOW_OFFSET(res, c);
        SHOW_OFFSET(osb_next, c);
        SHOW_OFFSET(oin_cache, c);
        SHOW_OFFSET(ofile_cache, c);
        SHOW_OFFSET(fe_cache, c);
        SHOW_OFFSET(lockres_cache, c);
        SHOW_OFFSET(flags, c);
        SHOW_OFFSET(node_name, c);
        SHOW_OFFSET(cluster_name, c);
        SHOW_OFFSET(comm_info, c);
        SHOW_OFFSET(hbm, c);
    }
}

static void print_ocfs_super_offsets()
{
    ocfs_super s;

    SHOW_SIZEOF(ocfs_super, s);

    if (show_all) {
        SHOW_OFFSET(obj_id, s);
        SHOW_OFFSET(osb_res, s);
        SHOW_OFFSET(osb_next, s);
        SHOW_OFFSET(osb_id, s);
        SHOW_OFFSET(complete, s);
        SHOW_OFFSET(dlm_task, s);
        SHOW_OFFSET(osb_flags, s);
        SHOW_OFFSET(file_open_cnt, s);
        SHOW_OFFSET(publ_map, s);
        SHOW_OFFSET(root_sect_node, s);
        SHOW_OFFSET(cache_lock_list, s);
        SHOW_OFFSET(sb, s);
        SHOW_OFFSET(oin_root_dir, s);
        SHOW_OFFSET(vol_layout, s);
        SHOW_OFFSET(vol_node_map, s);
        SHOW_OFFSET(node_cfg_info, s);
        SHOW_OFFSET(cfg_seq_num, s);
        SHOW_OFFSET(cfg_initialized, s);
        SHOW_OFFSET(num_cfg_nodes, s);
        SHOW_OFFSET(node_num, s);
        SHOW_OFFSET(hbm, s);
        SHOW_OFFSET(hbt, s);
        SHOW_OFFSET(log_disk_off, s);
        SHOW_OFFSET(log_meta_disk_off, s);
        SHOW_OFFSET(log_file_size, s);
        SHOW_OFFSET(sect_size, s);
        SHOW_OFFSET(needs_flush, s);
        SHOW_OFFSET(commit_cache_exec, s);
        SHOW_OFFSET(map_lock, s);
        SHOW_OFFSET(metadata_map, s);
        SHOW_OFFSET(trans_map, s);
        SHOW_OFFSET(cluster_bitmap, s);
        SHOW_OFFSET(max_dir_node_ent, s);
        SHOW_OFFSET(vol_state, s);
        SHOW_OFFSET(curr_trans_id, s);
        SHOW_OFFSET(trans_in_progress, s);
        SHOW_OFFSET(log_lock, s);
        SHOW_OFFSET(recovery_lock, s);
        SHOW_OFFSET(node_recovering, s);
        SHOW_OFFSET(vol_alloc_lock, s);
        SHOW_OFFSET(lock_timer, s);
        SHOW_OFFSET(lock_stop, s);
        SHOW_OFFSET(lock_event, s);
        SHOW_OFFSET(cache_fs, s);
    }
}

static void print_ocfs_lock_res_offsets()
{
    ocfs_lock_res l;

    SHOW_SIZEOF(ocfs_lock_res, l);

    if (show_all) {
        SHOW_OFFSET(signature, l);
        SHOW_OFFSET(lock_type, l);		
//        SHOW_OFFSET(ref_cnt, l);		
        SHOW_OFFSET(master_node_num, l);
        SHOW_OFFSET(last_upd_seq_num, l);
        SHOW_OFFSET(last_lock_upd, l);
        SHOW_OFFSET(sector_num, l);
        SHOW_OFFSET(oin_openmap, l);
        SHOW_OFFSET(in_use, l);
        SHOW_OFFSET(thread_id, l);
        SHOW_OFFSET(cache_list, l);
        SHOW_OFFSET(in_cache_list, l);
        SHOW_OFFSET(lock_state, l);
        SHOW_OFFSET(oin, l);
        SHOW_OFFSET(lock_mutex, l);
        SHOW_OFFSET(voted_event, l);
        SHOW_OFFSET(req_vote_map, l);
        SHOW_OFFSET(got_vote_map, l);
        SHOW_OFFSET(vote_status, l);
        SHOW_OFFSET(last_write_time, l);
        SHOW_OFFSET(last_read_time, l);
        SHOW_OFFSET(writer_node_num, l);
        SHOW_OFFSET(reader_node_num, l);
    }
}

static void print_superops_offsets()
{
    struct super_operations s;

    SHOW_SIZEOF(struct super_operations, s);

    if (show_all) {
        SHOW_OFFSET(read_inode, s);
        SHOW_OFFSET(read_inode2, s);
        SHOW_OFFSET(dirty_inode, s);
        SHOW_OFFSET(write_inode, s);
        SHOW_OFFSET(put_inode, s);
        SHOW_OFFSET(delete_inode, s);
        SHOW_OFFSET(put_super, s);
        SHOW_OFFSET(write_super, s);
        SHOW_OFFSET(write_super_lockfs, s);
        SHOW_OFFSET(unlockfs, s);
        SHOW_OFFSET(statfs, s);
        SHOW_OFFSET(remount_fs, s);
        SHOW_OFFSET(clear_inode, s);
        SHOW_OFFSET(umount_begin, s);
    }
}
 
static void print_super_offsets()
{
    struct super_block s;

    SHOW_SIZEOF(struct super_block, s);

    if (show_all) {
        SHOW_OFFSET(s_op, s);
        SHOW_OFFSET(s_list, s);
        SHOW_OFFSET(s_dev, s);
        SHOW_OFFSET(s_blocksize, s);
        SHOW_OFFSET(s_blocksize_bits, s);
        SHOW_OFFSET(s_dirt, s);
        SHOW_OFFSET(s_maxbytes, s);
        SHOW_OFFSET(s_type, s);
        SHOW_OFFSET(s_op, s);
        SHOW_OFFSET(dq_op, s);
        SHOW_OFFSET(s_flags, s);
        SHOW_OFFSET(s_magic, s);
        SHOW_OFFSET(s_root, s);
        SHOW_OFFSET(s_umount, s);
        SHOW_OFFSET(s_lock, s);
        SHOW_OFFSET(s_count, s);
        SHOW_OFFSET(s_active, s);
        SHOW_OFFSET(s_dirty, s);
        SHOW_OFFSET(s_locked_inodes, s);
        SHOW_OFFSET(s_files, s);
        SHOW_OFFSET(s_bdev, s);
        SHOW_OFFSET(s_instances, s);
        SHOW_OFFSET(s_dquot, s);
        SHOW_OFFSET(u, s);
    }
}
 
static void print_filp_offsets()
{
    struct file f;

    SHOW_SIZEOF(struct file, f);

    if (show_all) {
        SHOW_OFFSET(f_list, f);
        SHOW_OFFSET(f_dentry, f);
        SHOW_OFFSET(f_vfsmnt, f);
        SHOW_OFFSET(f_op, f);
        SHOW_OFFSET(f_count, f);
        SHOW_OFFSET(f_flags, f);
        SHOW_OFFSET(f_mode, f);
        SHOW_OFFSET(f_pos, f);
        SHOW_OFFSET(f_reada, f);
        SHOW_OFFSET(f_ramax, f);
        SHOW_OFFSET(f_raend, f);
        SHOW_OFFSET(f_ralen, f);
        SHOW_OFFSET(f_rawin, f);
        SHOW_OFFSET(f_owner, f);
        SHOW_OFFSET(f_uid, f);
        SHOW_OFFSET(f_gid, f);
        SHOW_OFFSET(f_error, f);
        SHOW_OFFSET(f_version, f);
        SHOW_OFFSET(private_data, f);
        //SHOW_OFFSET(f_iobuf, f);
        //SHOW_OFFSET(f_iobuf_lock, f);
    }
}

static void print_inode_offsets()
{
    struct inode ino;

    SHOW_SIZEOF(struct inode, ino);

    if (show_all) {
        SHOW_OFFSET(i_hash, ino);
        SHOW_OFFSET(i_list, ino);
        SHOW_OFFSET(i_dentry, ino);
        SHOW_OFFSET(i_dirty_buffers, ino);
        //SHOW_OFFSET(i_dirty_data_buffers, ino);
        SHOW_OFFSET(i_ino, ino);
        SHOW_OFFSET(i_count, ino);
        SHOW_OFFSET(i_dev, ino);
        SHOW_OFFSET(i_mode, ino);
        SHOW_OFFSET(i_nlink, ino);
        SHOW_OFFSET(i_uid, ino);
        SHOW_OFFSET(i_gid, ino);
        SHOW_OFFSET(i_rdev, ino);
        SHOW_OFFSET(i_size, ino);
        SHOW_OFFSET(i_atime, ino);
        SHOW_OFFSET(i_mtime, ino);
        SHOW_OFFSET(i_ctime, ino);
        SHOW_OFFSET(i_blksize, ino);
        SHOW_OFFSET(i_blocks, ino);
        SHOW_OFFSET(i_version, ino);
        SHOW_OFFSET(i_bytes, ino);
        SHOW_OFFSET(i_sem, ino);
#ifndef __LP64__
        SHOW_OFFSET(i_truncate_sem, ino);
#endif
        SHOW_OFFSET(i_zombie, ino);
        SHOW_OFFSET(i_op, ino);
        SHOW_OFFSET(i_fop, ino);    
        SHOW_OFFSET(i_sb, ino);
        SHOW_OFFSET(i_wait, ino);
        SHOW_OFFSET(i_flock, ino);
        SHOW_OFFSET(i_mapping, ino);
        SHOW_OFFSET(i_data, ino);    
        SHOW_OFFSET(i_dquot, ino);
        //SHOW_OFFSET(i_devices, ino);
        SHOW_OFFSET(i_pipe, ino);
        SHOW_OFFSET(i_bdev, ino);
        SHOW_OFFSET(i_cdev, ino);
        SHOW_OFFSET(i_dnotify_mask, ino); 
        SHOW_OFFSET(i_dnotify, ino); 
        SHOW_OFFSET(i_state, ino);
        SHOW_OFFSET(i_flags, ino);
        SHOW_OFFSET(i_sock, ino);
        SHOW_OFFSET(i_writecount, ino);
        SHOW_OFFSET(i_attr_flags, ino);
        SHOW_OFFSET(i_generation, ino);
        SHOW_OFFSET(u.generic_ip, ino);
    }
}

static void print_dentry_offsets()
{
    struct qstr qs;
    struct dentry den;

    SHOW_SIZEOF(struct dentry, den);

    if (show_all) {
        SHOW_OFFSET(d_count, den);
        SHOW_OFFSET(d_flags, den);
        SHOW_OFFSET(d_inode, den);
        SHOW_OFFSET(d_parent, den);
        SHOW_OFFSET(d_hash, den);
        SHOW_OFFSET(d_lru, den);
        SHOW_OFFSET(d_child, den);
        SHOW_OFFSET(d_subdirs, den);    
        SHOW_OFFSET(d_alias, den);
        SHOW_OFFSET(d_mounted, den);
        SHOW_OFFSET(d_name, den);
        SHOW_OFFSET(d_time, den);
        SHOW_OFFSET(d_op, den);
        SHOW_OFFSET(d_sb, den);    
        SHOW_OFFSET(d_vfs_flags, den);
        SHOW_OFFSET(d_fsdata, den);
        SHOW_OFFSET(d_iname, den); 
        SHOW_SIZEOF(struct qstr, qs);
        SHOW_OFFSET(name, qs);
        SHOW_OFFSET(len, qs);
        SHOW_OFFSET(hash, qs);
    }
}

void usage(void)
{
	printf("usage: sizetest [all]\n");
	return ;
}


static void print_ocfs_alloc_ext()
{
	ocfs_alloc_ext s;
	SHOW_SIZEOF(ocfs_alloc_ext, s);
	SHOW_OFFSET(file_off, s);			
	SHOW_OFFSET(num_bytes, s);		
	SHOW_OFFSET(disk_off, s);			
}


static void print_ocfs_publish()
{
	ocfs_publish s;
	SHOW_SIZEOF(ocfs_publish, s);
	SHOW_OFFSET(time, s);                     
	SHOW_OFFSET(vote, s);                     
	SHOW_OFFSET(dirty, s);                     
	SHOW_OFFSET(vote_type, s);                  
	SHOW_OFFSET(vote_map, s);                   
	SHOW_OFFSET(publ_seq_num, s);               
	SHOW_OFFSET(dir_ent, s);                    
	SHOW_OFFSET(hbm, s);
	SHOW_OFFSET(comm_seq_num, s);		
}


static void print_ocfs_vote()
{
	ocfs_vote s;
	SHOW_SIZEOF(ocfs_vote, s);
	SHOW_OFFSET(vote, s);   
	SHOW_OFFSET(vote_seq_num, s);              
	SHOW_OFFSET(dir_ent, s);                   
	SHOW_OFFSET(open_handle, s);                
}


static void print_ocfs_file_entry()
{
	ocfs_file_entry s;
	SHOW_SIZEOF(ocfs_file_entry, s);
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
	SHOW_OFFSET(extents, s);
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
}


static void print_ocfs_index_node()
{
	ocfs_index_node s;
	SHOW_SIZEOF(ocfs_index_node, s);
	SHOW_OFFSET(down_ptr, s);
	SHOW_OFFSET(file_ent_ptr, s);
	SHOW_OFFSET(name_len, s);
	SHOW_OFFSET(name, s);
}


static void print_ocfs_index_hdr()
{
	ocfs_index_hdr s;
	SHOW_SIZEOF(ocfs_index_hdr, s);
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
}


static void print_ocfs_dir_node()
{
	ocfs_dir_node s;
	SHOW_SIZEOF(ocfs_dir_node, s);
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
}


static void print_ocfs_vol_node_map()
{
	ocfs_vol_node_map s;
	SHOW_SIZEOF(ocfs_vol_node_map, s);
	SHOW_OFFSET(time, s);
	SHOW_OFFSET(scan_time, s);
	SHOW_OFFSET(scan_rate, s);
	SHOW_OFFSET(miss_cnt, s);
	SHOW_OFFSET(dismount, s);
	SHOW_OFFSET(largest_seq_num, s);
}


static void print_ocfs_vol_layout()
{
	ocfs_vol_layout s;
	SHOW_SIZEOF(ocfs_vol_layout, s);
	SHOW_OFFSET(start_off, s);
	SHOW_OFFSET(num_nodes, s);
	SHOW_OFFSET(cluster_size, s);
	SHOW_OFFSET(mount_point, s);
	SHOW_OFFSET(vol_id, s);
	SHOW_OFFSET(label, s);
	SHOW_OFFSET(label_len, s);
	SHOW_OFFSET(size, s);
	SHOW_OFFSET(root_start_off, s);
	SHOW_OFFSET(serial_num, s);
	SHOW_OFFSET(root_size, s);
	SHOW_OFFSET(publ_sect_off, s);
	SHOW_OFFSET(vote_sect_off, s);
	SHOW_OFFSET(root_bitmap_off, s);
	SHOW_OFFSET(root_bitmap_size, s);
	SHOW_OFFSET(data_start_off, s);
	SHOW_OFFSET(num_clusters, s);
	SHOW_OFFSET(root_int_off, s);
	SHOW_OFFSET(dir_node_size, s);
	SHOW_OFFSET(file_node_size, s);
	SHOW_OFFSET(bitmap_off, s);
	SHOW_OFFSET(node_cfg_off, s);
	SHOW_OFFSET(node_cfg_size, s);
	SHOW_OFFSET(new_cfg_off, s);
	SHOW_OFFSET(prot_bits, s);
	SHOW_OFFSET(uid, s);
	SHOW_OFFSET(gid, s);
}


static void print_ocfs_extent_group()
{
	ocfs_extent_group s;
	SHOW_SIZEOF(ocfs_extent_group, s);
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
	SHOW_OFFSET(extents, s);	
}


static void print_ocfs_bitmap_lock()
{
	ocfs_bitmap_lock s;
	SHOW_SIZEOF(ocfs_bitmap_lock, s);
	SHOW_OFFSET(disk_lock, s);
	SHOW_OFFSET(used_bits, s);
}


static void print_ocfs_vol_disk_hdr()
{
	ocfs_vol_disk_hdr s;
	SHOW_SIZEOF(ocfs_vol_disk_hdr, s);
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
}


static void print_ocfs_disk_lock()
{
	ocfs_disk_lock s;
	SHOW_SIZEOF(ocfs_disk_lock, s);
	SHOW_OFFSET(curr_master, s);			
	SHOW_OFFSET(file_lock, s);				
	SHOW_OFFSET(last_write_time, s);			
	SHOW_OFFSET(last_read_time, s);			
	SHOW_OFFSET(writer_node_num, s);			
	SHOW_OFFSET(reader_node_num, s);			
	SHOW_OFFSET(oin_node_map, s);			
	SHOW_OFFSET(dlock_seq_num, s);			
}


static void print_ocfs_vol_label()
{
	ocfs_vol_label s;
	SHOW_SIZEOF(ocfs_vol_label, s);
	SHOW_OFFSET(disk_lock, s);                
	SHOW_OFFSET(label, s);            
	SHOW_OFFSET(label_len, s);                           
	SHOW_OFFSET(vol_id, s);           
	SHOW_OFFSET(vol_id_len, s);                          
	SHOW_OFFSET(cluster_name, s);  
	SHOW_OFFSET(cluster_name_len, s);                    
}


static void print_ocfs_ipc_config_info()
{
	ocfs_ipc_config_info s;
	SHOW_SIZEOF(ocfs_ipc_config_info, s);
	SHOW_OFFSET(type, s);					
	SHOW_OFFSET(ip_addr, s);
	SHOW_OFFSET(ip_port, s);					
	SHOW_OFFSET(ip_mask, s);
}


static void print_ocfs_guid()
{
	ocfs_guid s;
	SHOW_SIZEOF(ocfs_guid, s);
	SHOW_OFFSET(id.host_id, s);
	SHOW_OFFSET(id.mac_id, s);
}


static void print_ocfs_disk_node_config_info()
{
	ocfs_disk_node_config_info s;
	SHOW_SIZEOF(ocfs_disk_node_config_info, s);
	SHOW_OFFSET(disk_lock, s);			
	SHOW_OFFSET(node_name, s);
	SHOW_OFFSET(guid, s);					
	SHOW_OFFSET(ipc_config, s);		
}


static void print_ocfs_node_config_hdr()
{
	ocfs_node_config_hdr s;
	SHOW_SIZEOF(ocfs_node_config_hdr, s);
	SHOW_OFFSET(disk_lock, s);			
	SHOW_OFFSET(signature, s);		
	SHOW_OFFSET(version, s);					
	SHOW_OFFSET(num_nodes, s);				
	SHOW_OFFSET(last_node, s);				
	SHOW_OFFSET(cfg_seq_num, s);				
}


static void print_ocfs_cdsl()
{
	ocfs_cdsl s;
	SHOW_SIZEOF(ocfs_cdsl, s);
	SHOW_OFFSET(name, s);
	SHOW_OFFSET(flags, s);
	SHOW_OFFSET(operation, s);
}


static void print_ocfs_free_bitmap()
{
	ocfs_free_bitmap s;
	SHOW_SIZEOF(ocfs_free_bitmap, s);
	SHOW_OFFSET(length, s);
	SHOW_OFFSET(file_off, s);
	SHOW_OFFSET(type, s);
	SHOW_OFFSET(node_num, s);
}


static void print_ocfs_free_extent_log()
{
	ocfs_free_extent_log s;
	SHOW_SIZEOF(ocfs_free_extent_log, s);
	SHOW_OFFSET(index, s);
	SHOW_OFFSET(disk_off, s);
}


static void print_ocfs_free_log()
{
	ocfs_free_log s;
	SHOW_SIZEOF(ocfs_free_log, s);
	SHOW_OFFSET(num_free_upds, s); 
	SHOW_OFFSET(free_bitmap, s);
}


static void print_ocfs_delete_log()
{
	ocfs_delete_log s;
	SHOW_SIZEOF(ocfs_delete_log, s);
	SHOW_OFFSET(node_num, s);
	SHOW_OFFSET(ent_del, s);
	SHOW_OFFSET(parent_dirnode_off, s);
	SHOW_OFFSET(flags, s);
}


static void print_ocfs_recovery_log()
{
	ocfs_recovery_log s;
	SHOW_SIZEOF(ocfs_recovery_log, s);
	SHOW_OFFSET(node_num, s);
}


static void print_ocfs_alloc_log()
{
	ocfs_alloc_log s;
	SHOW_SIZEOF(ocfs_alloc_log, s);
	SHOW_OFFSET(length, s);
	SHOW_OFFSET(file_off, s);
	SHOW_OFFSET(type, s);
	SHOW_OFFSET(node_num, s);
}


static void print_ocfs_dir_log()
{
	ocfs_dir_log s;
	SHOW_SIZEOF(ocfs_dir_log, s);
	SHOW_OFFSET(orig_off, s);
	SHOW_OFFSET(saved_off, s);
	SHOW_OFFSET(length, s);
}


static void print_ocfs_lock_update()
{
	ocfs_lock_update s;
	SHOW_SIZEOF(ocfs_lock_update, s);
	SHOW_OFFSET(orig_off, s);
	SHOW_OFFSET(new_off, s);
}


static void print_ocfs_lock_log()
{
	ocfs_lock_log s;
	SHOW_SIZEOF(ocfs_lock_log, s);
	SHOW_OFFSET(num_lock_upds, s);
	SHOW_OFFSET(lock_upd, s);
}


static void print_ocfs_bcast_rel_log()
{
	ocfs_bcast_rel_log s;
	SHOW_SIZEOF(ocfs_bcast_rel_log, s);
	SHOW_OFFSET(lock_id, s);
}


static void print_ocfs_cleanup_record()
{
	ocfs_cleanup_record s;
	SHOW_SIZEOF(ocfs_cleanup_record, s);
	SHOW_OFFSET(log_id, s);
	SHOW_OFFSET(log_type, s);
	SHOW_OFFSET(rec.lock, s);
	SHOW_OFFSET(rec.alloc, s);
	SHOW_OFFSET(rec.bcast, s);
	SHOW_OFFSET(rec.del, s);
	SHOW_OFFSET(rec.free, s);
}


static void print_ocfs_log_record()
{
	ocfs_log_record s;
	SHOW_SIZEOF(ocfs_log_record, s);
	SHOW_OFFSET(log_id, s);
	SHOW_OFFSET(log_type, s);
	SHOW_OFFSET(rec.dir, s);
	SHOW_OFFSET(rec.alloc, s);
	SHOW_OFFSET(rec.recovery, s);
	SHOW_OFFSET(rec.bcast, s);
	SHOW_OFFSET(rec.del, s);
	SHOW_OFFSET(rec.extent, s);
}


static void print_ocfs_dlm_msg_hdr()
{
	ocfs_dlm_msg_hdr s;
	SHOW_SIZEOF(ocfs_dlm_msg_hdr, s);
	SHOW_OFFSET(lock_id, s);
	SHOW_OFFSET(flags, s);
	SHOW_OFFSET(lock_seq_num, s);
	SHOW_OFFSET(open_handle, s);
}

static void print_ocfs_dlm_reply_master()
{
	ocfs_dlm_reply_master s;
	SHOW_SIZEOF(ocfs_dlm_reply_master, s);
	SHOW_OFFSET(h, s);
	SHOW_OFFSET(status, s);
}


static void print_ocfs_dlm_disk_vote_reply()
{
	ocfs_dlm_disk_vote_reply s;
	SHOW_SIZEOF(ocfs_dlm_disk_vote_reply, s);
	SHOW_OFFSET(h, s);
	SHOW_OFFSET(status, s);
}


static void print_ocfs_dlm_msg()
{
	ocfs_dlm_msg s;
	SHOW_SIZEOF(ocfs_dlm_msg, s);
	SHOW_OFFSET(magic, s);
	SHOW_OFFSET(msg_len, s);
	SHOW_OFFSET(vol_id, s);
	SHOW_OFFSET(src_node, s);
	SHOW_OFFSET(dst_node, s);
	SHOW_OFFSET(msg_type, s);
	SHOW_OFFSET(check_sum, s);
	SHOW_OFFSET(msg_buf, s);
}


static void print_ocfs_recv_ctxt()
{
	ocfs_recv_ctxt s;
	SHOW_SIZEOF(ocfs_recv_ctxt, s);
	SHOW_OFFSET(msg_len, s);
	SHOW_OFFSET(msg, s);
	SHOW_OFFSET(status, s);
#ifdef LINUX_2_5
	SHOW_OFFSET(ipc_wq, s);
#else
	SHOW_OFFSET(ipc_tq, s);
#endif
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

    print_inode_offsets();
    print_dentry_offsets();
    print_filp_offsets();
    print_super_offsets();
    print_superops_offsets();
    print_ocfs_lock_res_offsets();
    print_ocfs_super_offsets();
    print_ocfs_global_ctxt_offsets();
    print_ocfs_cleanup_record_offsets();
    print_ocfs_dlm_reply_master_offsets();
    print_ocfs_dlm_msg_offsets();
    print_ocfs_inode_offsets();
    print_ocfs_alloc_ext();
    print_ocfs_io_runs();
    print_ocfs_offset_map();
    print_ocfs_log_record();
    print_ocfs_vol_disk_hdr();
    print_ocfs_file_entry();
    print_ocfs_extent_group();
    print_ocfs_alloc_ext();
    print_ocfs_publish();
    print_ocfs_vote();
    print_ocfs_file_entry();
    print_ocfs_index_node();
    print_ocfs_index_hdr();
    print_ocfs_dir_node();
    print_ocfs_vol_node_map();
    print_ocfs_vol_layout();
    print_ocfs_extent_group();
    print_ocfs_bitmap_lock();
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
    print_ocfs_lock_update();
    print_ocfs_lock_log();
    print_ocfs_bcast_rel_log();
    print_ocfs_cleanup_record();
    print_ocfs_log_record();
    print_ocfs_dlm_msg_hdr();
    print_ocfs_dlm_reply_master();
    print_ocfs_dlm_disk_vote_reply();
    print_ocfs_dlm_msg();
    print_ocfs_recv_ctxt();
    print_ocfs_extent_map();
    return 0;
}
