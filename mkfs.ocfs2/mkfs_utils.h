/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * mkfs_utils.h
 *
 * OCFS2 format utility
 *
 * Copyright (C) 2004 Oracle Corporation.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License, version 2,  as published by the Free Software Foundation.
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
 * Authors: Manish Singh, Kurt Hackel
 */

#ifndef __MKFS_UTILS_H__
#define __MKFS_UTILS_H__

#include <mkfs.h>

int get_number(char *arg, uint64_t *res);
void fill_defaults(State *s);
int get_bits(State *s, int num);
void *do_malloc(State *s, size_t size);
void do_pwrite(State *s, const void *buf, size_t count, uint64_t offset);
AllocBitmap *initialize_bitmap(State *s, uint32_t bits, uint32_t unit_bits,
			       const char *name,
			       SystemFileDiskRecord *bm_record,
			       SystemFileDiskRecord *alloc_record);
int find_clear_bits(AllocBitmap *bitmap, uint32_t num_bits, uint32_t offset);
int alloc_bytes_from_bitmap(State *s, uint64_t bytes, AllocBitmap *bitmap,
			    uint64_t *start, uint64_t *num);
int alloc_from_bitmap(State *s, uint64_t num_bits, AllocBitmap *bitmap,
		      uint64_t *start, uint64_t *num);
uint64_t alloc_inode(State *s, uint16_t *suballoc_bit);
DirData *alloc_directory(State *s);
void add_entry_to_directory(State *s, DirData *dir, char *name,
			    uint64_t byte_off, uint8_t type);
uint32_t blocks_needed(State *s);
uint32_t system_dir_blocks_needed(State *s);
void adjust_volume_size(State *s);
void check_32bit_blocks(State *s);
void format_superblock(State *s, SystemFileDiskRecord *rec,
		       SystemFileDiskRecord *root_rec,
		       SystemFileDiskRecord *sys_rec);
void format_file(State *s, SystemFileDiskRecord *rec);
void write_metadata(State *s, SystemFileDiskRecord *rec, void *src);
void write_bitmap_data(State *s, AllocBitmap *bitmap);
void write_directory_data(State *s, DirData *dir);
void write_group_data(State *s, AllocGroup *group);
void format_leading_space(State *s);
void replacement_journal_create(State *s, uint64_t journal_off);
void open_device(State *s);
void close_device(State *s);
int initial_nodes_for_volume(uint64_t size);
void generate_uuid(State *s);
void create_generation(State *s);
void write_autoconfig_header(State *s, SystemFileDiskRecord *rec);
void init_record(State *s, SystemFileDiskRecord *rec, int type, int dir);
void print_state(State *s);
int ocfs2_clusters_per_group(int block_size,
			     int cluster_size_bits);
AllocGroup * initialize_alloc_group(State *s, char *name,
				    SystemFileDiskRecord *alloc_inode,
				    uint64_t blkno, uint16_t chain,
				    uint16_t cpg, uint16_t bpc);
uint64_t figure_journal_size(uint64_t size, State *s);
int alloc_from_group(State *s, uint16_t count, AllocGroup *group,
		     uint64_t *start_blkno, uint16_t *num_bits);
#endif
