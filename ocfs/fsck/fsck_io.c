/*
 * fsck_io.c
 *
 * provides the actual file I/O support in ocfs file system
 * check utility
 *
 * Copyright (C) 2003, 2004 Oracle.  All rights reserved.
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

#include "fsck.h"

void *mem_alloc(int len)
{
	void *buf;

	if ((buf = malloc_aligned(len)) == NULL)
		LOG_ERROR("unable to allocate %d bytes of memory", len);

	return buf;
}				/* mem_alloc */


loff_t myseek64(int fd, loff_t off, int whence)
{
	loff_t ret;

	if ((ret = lseek64(fd, off, whence) == -1))
		LOG_ERROR("lseek() %s", strerror(errno));

	return ret;
}				/* myseek64 */

/*
 * myread()
 *
 */
int myread(int file, char *buf, __u32 len)
{
	int ret;

	if ((ret = read(file, buf, len)) == -1)
		LOG_ERROR("read() %s", strerror(errno));

	return ret;
}				/* myread */


/*
 * mywrite()
 *
 */
int mywrite(int file, char *buf, __u32 len)
{
	int ret;

	if ((ret = write(file, buf, len)) == -1)
		LOG_ERROR("write() %s", strerror(errno));

	return ret;
}				/* mywrite */

/*
 * myopen()
 *
 */
int myopen(char *path, int flags)
{
    int file;
    mode_t oldmode;

    oldmode = umask(0000);
    file = open(path, flags, 0777);
    umask(oldmode);

    return file;
}				/* myopen */


/*
 * myclose()
 *
 */
void myclose(int file)
{
    if (file)
	close(file);
}				/* myclose */


void read_system_file(int fd, ocfs_vol_disk_hdr * v, int fileid,
		      ocfs_file_entry * fe)
{
    __u64 diskOffset = (fileid * OCFS_SECTOR_SIZE) + v->internal_off;

    myseek64(fd, diskOffset, SEEK_SET);
    read(fd, fe, OCFS_SECTOR_SIZE);
}				/* read_system_file */


int write_system_file(int fd, ocfs_vol_disk_hdr * v, int fileid,
		      ocfs_file_entry * fe)
{
    int ret;
    void *sector;
    __u64 diskOffset = (fileid * OCFS_SECTOR_SIZE) + v->internal_off;

    sector = malloc_aligned(OCFS_SECTOR_SIZE);
    memset(sector, 0, OCFS_SECTOR_SIZE);
    memcpy(sector, fe, sizeof(ocfs_file_entry));
    myseek64(fd, diskOffset, SEEK_SET);
    ret = write(fd, sector, OCFS_SECTOR_SIZE);
    free(sector);
    return ret;
}				/* write_system_file */


void read_cdsl_data(int fd, void *data, __u64 offset)
{
	int len = sizeof(__u64) * MAX_NODES;
	int rdlen;

	myseek64(fd, offset, SEEK_SET);
	if ((rdlen = read(fd, data, len)) != len)
		LOG_ERROR("short read for cdsl data... %d instead of %d bytes",
			  rdlen, len);
}				/* read_cdsl_data */


int read_one_sector(int fd, char *buf, __u64 offset, int idx)
{
	int ret;

	myseek64(fd, offset, SEEK_SET);
	if ((ret = read(fd, buf, OCFS_SECTOR_SIZE)) == -1)
		LOG_ERROR("read() %s", strerror(errno));
	return ret;
}				/* read_one_sector */


int write_one_sector(int fd, char *buf, __u64 offset, int idx)
{
	int ret;

	myseek64(fd, offset, SEEK_SET);
	if ((ret = write(fd, buf, OCFS_SECTOR_SIZE)) == -1)
		LOG_ERROR("write() %s", strerror(errno));
	return ret;
}				/* write_one_sector */


int read_dir_node(int fd, char *buf, __u64 offset, int idx)
{
	int rdlen;

	myseek64(fd, offset, SEEK_SET);

	if ((rdlen = read(fd, buf, DIR_NODE_SIZE)) != DIR_NODE_SIZE) {
		if (rdlen == -1)
			LOG_ERROR("read() %s\n", strerror(errno));
		else
			LOG_ERROR("short read for dirnode... %d instead "
				  "of %d bytes", rdlen, DIR_NODE_SIZE);
	}
    	return 0;
}				/* read_dir_node */


int write_dir_node (int fd, char *buf, __u64 offset, int idx)
{
	int ret;
	myseek64(fd, offset, SEEK_SET);
	if ((ret = write(fd, buf, DIR_NODE_SIZE)) == -1)
		LOG_ERROR("write() %s", strerror(errno));
	return ret;
}				/* write_dir_node */


int read_volume_bitmap (int fd, char *buf, __u64 offset, int idx)
{
	int rdlen;
	
	myseek64(fd, offset, SEEK_SET);
	if ((rdlen = read(fd, buf, VOL_BITMAP_BYTES)) != VOL_BITMAP_BYTES)
		LOG_ERROR("short read for volume bitmap... %d instead of %d bytes",
			  rdlen, VOL_BITMAP_BYTES);
	return 0;
}				/* read_volume_bitmap */


int write_volume_bitmap (int fd, char *buf, __u64 offset, int idx)
{
	int ret;
	myseek64(fd, offset, SEEK_SET);
	if ((ret = write(fd, buf, VOL_BITMAP_BYTES)) == -1)
		LOG_ERROR("write() %s", strerror(errno));
	return ret;
}				/* write_volume_bitmap */
