/*
 * debugocfs.h
 *
 * Function prototypes for related 'C' file.
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
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
 * Author: Kurt Hackel, Sunil Mushran
 */

#ifndef _DEBUGOCFS_H_
#define _DEBUGOCFS_H_

#define _LARGEFILE64_SOURCE
#include <libocfs.h>
#include <bindraw.h>

#define MAX_NODES  OCFS_MAXIMUM_NODES
#define MAX_SYSTEM_FILES    (CLEANUP_FILE_BASE_ID + OCFS_MAXIMUM_NODES)	/* 193? */
#define DIR_NODE_SIZE  (1024 * 128)


/* modes for find_file_entry */
enum 
{
    FIND_MODE_DIR,
    FIND_MODE_FILE,
    FIND_MODE_FILE_EXTENT,
    FIND_MODE_FILE_DATA
};

#define DEFAULT_NODE_NUMBER   (0)

typedef struct _user_args
{
    int nodenum;
    int showHeader;
    int showBitmap;
    int showPublish;
    int showVote;
    int showListing;
    int showDirent;
    int showDirentAll;
    int showFileent;
    int showFileext;
    bool no_rawbind;
    int twoFourbyte;
    int showSystemFiles;
    int suckFile;
    int publishNodes[MAX_NODES];
    int voteNodes[MAX_NODES];
    int systemFiles[MAX_SYSTEM_FILES];
    char *dirent;
    char *fileent;
    char *suckTo;
}
user_args;

typedef struct _filedata
{
    ocfs_io_runs *array;
    __u32 num;
    __u64 off;
    mode_t mode;
    uid_t user;
    gid_t group;
    unsigned int major;
    unsigned int minor;
    char *linkname;
}
filedata;

/* print functions */
void print_dir_node(void *buf);
void print_disk_lock(void *buf);
void print_vol_label(void *buf);
void print_vol_disk_header(void *buf);
void print_publish_sector(void *buf);
void print_vote_sector(void *buf);
void print_file_entry(void *buf);
void print_extent_ex(void *buf);
void print_extent(void *buf, int twolongs, bool prev_ptr_error);
void print_cdsl_offsets(void *buf);
void print_record(void *rec, int type);
void print_system_file(int fd, ocfs_vol_disk_hdr * v, int fileid);

/* helpers */
void print_synch_flags(int flags);
void print_publish_flags(int type);
void print_vote_type(int type);
void print_log_type(int type);
void print_lock_type(__u8 lock);
void print___u64_as_bitmap(__u64 x);
void print_node_pointer(__u64 ptr);

void handle_one_file_entry(int fd, ocfs_file_entry *fe, void *buf);


ocfs_super *get_fake_vcb(int fd, ocfs_vol_disk_hdr * hdr, int nodenum);
void walk_dir_nodes(int fd, __u64 offset, const char *parent, void *buf);
void find_file_entry(ocfs_super * vcb, __u64 offset, const char *parent,
		     const char *searchFor, int mode, filedata *buf);
void traverse_fe_extents(ocfs_super * vcb, ocfs_file_entry *fe);
void traverse_extent(ocfs_super * vcb, ocfs_extent_group * exthdr, int flag);

#if 0
void read_vol_disk_header(int fd, ocfs_vol_disk_hdr * v);
int write_vol_disk_header(int fd, ocfs_vol_disk_hdr * v);
void read_vote_sector(int fd, ocfs_vote * vs, __u64 offset);
void read_publish_sector(int fd, ocfs_publish * ps, __u64 offset);
void read_system_file(int fd, ocfs_vol_disk_hdr * v, int fileid,
		      ocfs_file_entry * fe);
void read_dir_node(int fd, ocfs_dir_node * d, __u64 offset);
void read_extent(int fd, ocfs_extent_group * e, __u64 offset);
void read_cdsl_data(int fd, void *data, __u64 offset);
void read_vol_label(int fd, ocfs_vol_label * v);
loff_t myseek64(int fd, loff_t off, int whence);
int suck_file(ocfs_super * vcb, const char *path, const char *file);

int write_publish_sector(int fd, ocfs_publish * ps, __u64 offset);
int write_vote_sector(int fd, ocfs_vote * vs, __u64 offset);
int write_system_file(int fd, ocfs_vol_disk_hdr * v, int fileid,
		      ocfs_file_entry * fe);
int write_vol_disk_header(int fd, ocfs_vol_disk_hdr * v);
int write_vol_label(int fd, ocfs_vol_label * v);
int write_dir_node_header(int fd, ocfs_dir_node * d, __u64 offset);
int write_file_entry(int fd, ocfs_file_entry * f, __u64 offset);


int ocfs_lookup_file_allocation(ocfs_super * VCB, ocfs_inode * FCB, __s64 Vbo, __s64 * Lbo, __u32 ByteCount, __u32 * NumIndex, void **Buffer);
int AdjustAllocation(ocfs_io_runs ** IoRuns, __u32 * ioRunSize);
bool ocfs_add_extent_map_entry(ocfs_super * VCB, ocfs_extent_map * MCB, __s64 Vbo, __s64 Lbo, __u64 ByteCount);
int ocfs_update_extent_map(ocfs_super * VCB, ocfs_extent_map * MCB, void *Buffer, __s64 * localVbo, __u64 * remainingLength, __u32 Flag);
int ocfs_read_sector(ocfs_super * vcb, void *buf, __u64 off);

ocfs_file_entry *ocfs_allocate_file_entry();
void *ocfs_linux_dbg_alloc(int Size, char *file, int line);
bool ocfs_lookup_extent_map_entry (ocfs_super * osb, ocfs_extent_map * Map, __s64 Vbo, __s64 * Lbo, __u64 * SectorCount, __u32 * Index);
bool ocfs_extent_map_lookup (ocfs_extent_map * map, __s64 virtual, __s64 * physical, __s64 * sectorcount, __u32 * index);
#endif

void usage(void);

#endif
