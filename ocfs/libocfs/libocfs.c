/*
 * libocfs.c
 *
 * provides dummy functions for userspace tools
 * to allow ocfs kernel module source to work in userspace
 *
 * Copyright (C) 2002, 2003 Oracle Corporation.  All rights reserved.
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

#define DUMMY_C_LOCAL_DECLS
#include <libocfs.h>
#undef DUMMY_C_LOCAL_DECLS
#include <string.h>

extern char *optarg;
extern int optind, opterr, optopt;

long TIME_ZERO = 0;
__u32 osb_id;
__u32 mount_cnt;
ocfs_ipc_ctxt OcfsIpcCtxt;

#if 0
ocfs_global_ctxt OcfsGlobalCtxt = {
	.pref_node_num = -1,
	.node_name = "userspacetool",
	.comm_info = {OCFS_UDP, "0.0.0.0", OCFS_IPC_DEFAULT_PORT, NULL}
};
#endif
extern ocfs_global_ctxt OcfsGlobalCtxt;

bool ocfs_linux_get_inode_offset (struct inode * inode, __u64 * off, ocfs_inode ** oin)
{
    return false;
}

bool ocfs_linux_get_dir_entry_offset (ocfs_super * osb, __u64 * off, __u64 parentOff,
				struct qstr * fileName, ocfs_file_entry ** fileEntry)
{
    return false;
}

void complete(struct completion *c)
{
}

void ocfs_release_cached_oin (ocfs_super * osb, ocfs_inode * oin)
{
}

void init_waitqueue_head(wait_queue_head_t *q)
{
}

void init_MUTEX (struct semaphore *sem)
{
}


void truncate_inode_pages(struct address_space *as, loff_t off)
{
}

unsigned int kdev_t_to_nr(kdev_t dev)
{
	return 0;
}

void init_special_inode(struct inode *inode, umode_t mode, int x)
{
}

int fsync_inode_buffers(struct inode *inode)
{
	return 0;
}

void d_prune_aliases(struct inode *inode)
{

}

void get_random_bytes(void *buf, int nbytes)
{
	FILE *f;

	f = fopen("/dev/random", "r");
	if (!f) {
		fprintf(stderr, "get_random_bytes: cannot open /dev/random\n");
		exit(1);
	}
	if (fread(buf, 1, nbytes, f) != nbytes) {
		fprintf(stderr, "get_random_bytes: cannot read /dev/random\n");
		fclose(f);
		exit(1);
	}
	fclose(f);
}

char *ocfs_strerror(int errnum) 
{
	return strerror(errnum);
}

void *ocfs_linux_dbg_alloc(int Size, char *file, int line)
{
    return malloc_aligned(Size);
}
void ocfs_linux_dbg_free (const void *Buffer)
{
    free_aligned((void *)Buffer);
}

int ocfs_write_disk(ocfs_super * vcb, void *buf, __u32 len, __u64 off)
{
    return ocfs_write_force_disk(vcb, buf, len, off);
}

int LinuxWriteForceDisk(ocfs_super * vcb, void *buf, __u32 len, __u64 off,
			bool cached)
{
    int fd;
    
    fd = (int) vcb->sb->s_dev;

    lseek64(fd, off, SEEK_SET);

    if (write(fd, buf, len) == -1) {
	    fprintf(stderr, "fd is %d\n", fd);
	    fprintf(stderr, "LinuxWriteForceDisk: Could not write: %s\n", strerror(errno));
	    return(-EFAIL);
    }
    
    return 0;
}

int ocfs_write_force_disk(ocfs_super * vcb, void *buf, __u32 len, __u64 off)
{
    return LinuxWriteForceDisk(vcb, buf, len, off, false);
}
#if 0
int ocfs_write_metadata(ocfs_super * vcb, void *buf, __u32 len, __u64 off)
{
    return ocfs_write_force_disk(vcb, buf, len, off);
}
#endif

int ocfs_read_force_disk_ex (ocfs_super * osb, void **Buffer, __u32 AllocLen,
			     __u32 ReadLen, __u64 Offset)
{
	return ocfs_read_disk_ex(osb, Buffer, AllocLen, ReadLen, Offset);
}
int ocfs_read_disk_ex (ocfs_super * osb, void **Buffer, __u32 AllocLen,
		       __u32 ReadLen, __u64 Offset) 
{
	if (!Buffer)
		return -EINVAL;
	if (!*Buffer) {
		*Buffer = malloc_aligned(AllocLen);
		if (!*Buffer)
			return -ENOMEM;
	}
	return ocfs_read_disk(osb, *Buffer, ReadLen, Offset);
}


int ocfs_read_disk(ocfs_super * vcb, void *buf, __u32 len, __u64 off)
{
    int fd;

    fd = (int) vcb->sb->s_dev;

    lseek64(fd, off, SEEK_SET);

    if (read(fd, buf, len) == -1) {
	    fprintf(stderr, "ocfs_read_disk: Could not read: %s\n", strerror(errno));
	    return(-EFAIL);
    }

    return (0);
}

int LinuxReadForceDisk(ocfs_super * VCB, void *Buffer, __u32 Length,
		       __u64 Offset, bool Cached)
{
    return ocfs_read_disk(VCB, Buffer, Length, Offset);
}
int ocfs_read_force_disk(ocfs_super * VCB, void *Buffer, __u32 Length, __u64 Offset)
{
    return ocfs_read_disk(VCB, Buffer, Length, Offset);
}
#if 0
int ocfs_read_metadata(ocfs_super * VCB, void *Buffer, __u32 Length, __u64 Offset)
{
    return ocfs_read_disk(VCB, Buffer, Length, Offset);
}
#endif


ocfs_super *get_fake_vcb(int fd, ocfs_vol_disk_hdr * hdr, int nodenum)
{
    ocfs_super *vcb;

    /* fake a few VCB values */
    vcb = (ocfs_super *) malloc_aligned(sizeof(ocfs_super));
    if (!vcb)
	    return NULL;
    vcb->sb = malloc_aligned(sizeof(struct super_block));
    if (!vcb->sb) {
	    free_aligned(vcb);
	    return NULL;
    }

    vcb->vol_layout.root_start_off = hdr->root_off;
    vcb->vol_layout.root_int_off = hdr->internal_off;
    vcb->vol_layout.cluster_size = hdr->cluster_size;
    vcb->vol_layout.data_start_off = hdr->data_start_off;
    vcb->vol_layout.node_cfg_off = hdr->node_cfg_off;
    vcb->vol_layout.node_cfg_size = hdr->node_cfg_size;

    vcb->sect_size = 512;
    vcb->curr_trans_id = 0;
    vcb->sb->s_dev = fd;
    vcb->node_num = nodenum;

    return vcb;
}


void * malloc_aligned(int size)
{
	return memalign(512, size);
}

void free_aligned(void *ptr)
{
	if (ptr)
		free(ptr);
}
