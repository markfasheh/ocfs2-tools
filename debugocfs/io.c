/*
 * io.c
 *
 * provides the actual file I/O support in debugocfs and
 * libdebugocfs for specific filesystem structures
 *
 * Copyright (C) 2002 Oracle Corporation.  All rights reserved.
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
 * Author: Kurt Hackel
 */

#include "debugocfs.h"

extern char *optarg;
extern int optind, opterr, optopt;
extern int filenum;
extern user_args args;
extern __u32 OcfsDebugCtxt;
extern __u32 OcfsDebugLevel;

int myread(int file, char *buf, __u32 len)
{
	int ret = 0;
	char *p = buf;
	__u32 remlen = len;

	while(remlen) {
		ret = read(file, p, remlen);
		if (ret == -1) {
			printf("Failed to read: %s\n", strerror(errno));
			goto bail;
		}
		remlen -= ret;
		p += ret;
	}

bail:
	return ret;
}

int mywrite(int file, char *buf, __u32 len)
{
	int ret = 0;
	char *p = buf;
	__u32 remlen = len;

	while(remlen) {
		ret = write(file, p, remlen);
		if (ret == -1) {
			printf("Failed to write: %s\n", strerror(errno));
			goto bail;
		}
		remlen -= ret;
		p += ret;
	}

bail:
	return ret;
}

loff_t myseek64(int fd, loff_t off, int whence)
{
    loff_t res;

    if ((res = lseek64(fd, off, whence) == -1))
    {
	printf("Failed to lseek to %lld!\n", off);
    }
    return res;
}

void read_publish_sector(int fd, ocfs_publish * ps, __u64 offset)
{
    myseek64(fd, offset, SEEK_SET);
    read(fd, ps, 512);
}

int write_publish_sector(int fd, ocfs_publish * ps, __u64 offset)
{
    int ret;
    void *sector;

    sector = malloc_aligned(512);
    memset(sector, 1, 512);
    memcpy(sector, ps, sizeof(ocfs_publish));
    myseek64(fd, offset, SEEK_SET);
    ret = write(fd, sector, 512);
    free_aligned(sector);
    return ret;
}


void read_vote_sector(int fd, ocfs_vote * vs, __u64 offset)
{
    myseek64(fd, offset, SEEK_SET);
    read(fd, vs, 512);
}

int write_vote_sector(int fd, ocfs_vote * vs, __u64 offset)
{
    int ret;
    void *sector;

    sector = malloc_aligned(512);
    memset(sector, 1, 512);
    memcpy(sector, vs, sizeof(ocfs_vote));
    myseek64(fd, offset, SEEK_SET);
    ret = write(fd, sector, 512);
    free_aligned(sector);
    return ret;
}

void read_system_file(int fd, ocfs_vol_disk_hdr * v, int fileid,
		      ocfs_file_entry * fe)
{
    __u64 diskOffset = (fileid * 512) + v->internal_off;

    myseek64(fd, diskOffset, SEEK_SET);
    read(fd, fe, 512);
}

int write_system_file(int fd, ocfs_vol_disk_hdr * v, int fileid,
		      ocfs_file_entry * fe)
{
    int ret;
    void *sector;
    __u64 diskOffset = (fileid * 512) + v->internal_off;

    sector = malloc_aligned(512);
    memset(sector, 1, 512);
    memcpy(sector, fe, sizeof(ocfs_file_entry));
    myseek64(fd, diskOffset, SEEK_SET);
    ret = write(fd, sector, 512);
    free_aligned(sector);
    return ret;
}

void read_vol_disk_header(int fd, ocfs_vol_disk_hdr * v)
{
    myseek64(fd, 0, SEEK_SET);
    //read(fd, v, sizeof(ocfs_vol_disk_hdr));
    read(fd, v, 512);
}

int write_vol_disk_header(int fd, ocfs_vol_disk_hdr * v)
{
    int ret;
    void *sector;

    sector = malloc_aligned(512);
    memset(sector, 1, 512);
    memcpy(sector, v, sizeof(ocfs_vol_disk_hdr));
    myseek64(fd, 0, SEEK_SET);
    ret = write(fd, sector, 512);
    free_aligned(sector);
    return ret;
}

void read_vol_label(int fd, ocfs_vol_label * v)
{
    myseek64(fd, 512, SEEK_SET);
    //read(fd, v, sizeof(ocfs_vol_label));
    read(fd, v, 512);
}

int write_vol_label(int fd, ocfs_vol_label * v)
{
    int ret;
    void *sector;

    sector = malloc_aligned(512);
    memset(sector, 1, 512);
    memcpy(sector, v, sizeof(ocfs_vol_label));
    myseek64(fd, 512, SEEK_SET);
    ret = write(fd, sector, 512);
    free_aligned(sector);
    return ret;
}

void read_extent(int fd, ocfs_extent_group * e, __u64 offset)
{
    myseek64(fd, offset, SEEK_SET);
    if (read(fd, e, 512) != 512)
        printf("hmm... short read for extent\n");
}


void read_dir_node(int fd, ocfs_dir_node * d, __u64 offset)
{
    int err;

    myseek64(fd, offset, SEEK_SET);
    err = read(fd, d, DIR_NODE_SIZE);
    if (err == -1)
	    fprintf(stderr, "read_dir_node: %s\n", strerror(errno));
    else if (err != DIR_NODE_SIZE)
	    printf("hmm... short read for dir node\n");
}

int write_dir_node_header(int fd, ocfs_dir_node * d, __u64 offset)
{
    int ret;
    void *sector;

    sector = malloc_aligned(512);
    memset(sector, 1, 512);
    memcpy(sector, d, sizeof(ocfs_dir_node));
    myseek64(fd, offset, SEEK_SET);
    ret = write(fd, sector, 512);
    free_aligned(sector);
    return ret;
}

int write_file_entry(int fd, ocfs_file_entry * f, __u64 offset)
{
    int ret;
    void *sector;

    sector = malloc_aligned(512);
    memset(sector, 1, 512);
    memcpy(sector, f, sizeof(ocfs_file_entry));
    myseek64(fd, offset, SEEK_SET);
    ret = write(fd, sector, 512);
    free_aligned(sector);
    return ret;
}


void read_cdsl_data(int fd, void *data, __u64 offset)
{
    int len = sizeof(__u64) * MAX_NODES;

    myseek64(fd, offset, SEEK_SET);
    if (read(fd, data, len) != len)
	printf("hmm... short read for cdsl data\n");
}
