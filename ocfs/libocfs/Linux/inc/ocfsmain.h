/*
 * ocfsmain.h
 *
 * Function prototypes for related 'C' file.
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
 * Authors: Neeraj Goyal, Suchit Kaura, Kurt Hackel, Sunil Mushran,
 *          Manish Singh, Wim Coekaerts
 */

#ifndef _OCFSMAIN_H_
#define _OCFSMAIN_H_

/* Module versioning */
#define LinuxVersionCode(v, p, s) (((v)<<16)+((p)<<8)+(s))

int ocfs_find_inode (struct inode *inode, unsigned long ino, void *opaque);

int ocfs_symlink_get_block (struct inode *inode,
			long iblock, struct buffer_head *bh_result, int create);

int ocfs_get_block (struct inode *inode,
		long iblock, struct buffer_head *bh_result, int create);

void ocfs_release_cached_oin (ocfs_super * osb, ocfs_inode * oin);

int ocfs_initialize_mem_lists (void);
void ocfs_free_mem_lists (void);
int ocfs_remount (struct super_block *sb, int *flags, char *data);

typedef struct _ocfs_find_inode_args
{
	__u64 offset;
	ocfs_file_entry *entry;
}
ocfs_find_inode_args;

#ifdef OCFSMAIN_PRIVATE_DECLS
static int ocfs_parse_options (char *options, __u32 * uid, __u32 * gid,
			       bool * cache, bool * reclaim_id);
static struct super_block *ocfs_read_super (struct super_block *sb, void *data,
					    int silent);
static int __init ocfs_driver_entry (void);
static void __exit ocfs_driver_exit (void);

int ocfs_read_params(void);

#ifdef OCFS_LINUX_MEM_DEBUG
static void ocfs_memcheck (void);
#endif

static void ocfs_populate_inode (struct inode *inode, ocfs_file_entry *fe,
				 umode_t mode, void *genptr);

static void ocfs_read_inode2 (struct inode *inode, void *opaque);

static void ocfs_read_inode (struct inode *inode);

static struct dentry *ocfs_lookup (struct inode *dir, struct dentry *dentry);

static int ocfs_statfs (struct super_block *sb, struct statfs *buf);

static ssize_t ocfs_file_write (struct file *filp,
		 const char *buf, size_t count, loff_t * ppos);

static ssize_t ocfs_file_read (struct file *filp, char *buf, size_t count, loff_t * ppos);

static int ocfs_readpage (struct file *file, struct page *page);

static int ocfs_writepage (struct page *page);

static int ocfs_prepare_write (struct file *file,
		    struct page *page, unsigned from, unsigned to);

static int ocfs_commit_write (struct file *file,
		   struct page *page, unsigned from, unsigned to);

static int ocfs_create_or_open_file (struct inode *inode,
			  struct inode *dir,
			  struct dentry *dentry,
			  int mode, ocfs_file ** newofile, int dev);

static int ocfs_file_open (struct inode *inode, struct file *file);

static int ocfs_mknod (struct inode *dir, struct dentry *dentry, int mode, int dev);

static int ocfs_mkdir (struct inode *dir, struct dentry *dentry, int mode);

static int ocfs_create (struct inode *dir, struct dentry *dentry, int mode);

static int ocfs_link (struct dentry *old_dentry, struct inode *dir, struct dentry *dentry);

static inline int ocfs_positive (struct dentry *dentry);

static int ocfs_empty (struct dentry *dentry);

static int ocfs_unlink (struct inode *dir, struct dentry *dentry);

static int ocfs_rename (struct inode *old_dir,
	     struct dentry *old_dentry,
	     struct inode *new_dir, struct dentry *new_dentry);

static int ocfs_symlink (struct inode *dir, struct dentry *dentry, const char *symname);

static int ocfs_file_release (struct inode *inode, struct file *file);

static int ocfs_flush (struct file *file);

static int ocfs_sync_file (struct file *file, struct dentry *dentry, int datasync);

static void ocfs_put_super (struct super_block *sb);

static int ocfs_readdir (struct file *filp, void *dirent, filldir_t filldir);

static void ocfs_put_inode (struct inode *inode);

static void ocfs_clear_inode (struct inode *inode);

static int ocfs_setattr (struct dentry *dentry, struct iattr *attr);

static int ocfs_getattr (struct dentry *dentry, struct iattr *attr);

static int ocfs_dentry_revalidate (struct dentry *dentry, int flags);

static void ocfs_set_exclusive_mount_flag (struct super_block *sb, int val);

static ssize_t ocfs_rw_direct (int rw, struct file *filp, char *buf,
			       size_t size, loff_t * offp);

#if LINUX_VERSION_CODE >= LinuxVersionCode(2,4,10)
static int ocfs_direct_IO (int rw,
#if LINUX_VERSION_CODE >= LinuxVersionCode(2,4,20) || defined(SUSE)
		struct file *filp,
#else
		struct inode *inode,
#endif
		struct kiobuf *iobuf, unsigned long blocknr, int blocksize);
#endif

#endif				/* OCFSMAIN_PRIVATE_DECLS */

#endif				/* _OCFSMAIN_H_ */
