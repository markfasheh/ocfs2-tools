/*
 * ocfs.h
 *
 * Main include file
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

#ifndef  _OCFS_H_
#define  _OCFS_H_

/* XXX Hack to avoid warning */
struct mem_dqinfo;
extern inline void mark_info_dirty(struct mem_dqinfo *info);

extern inline int generic_fls(int x);
extern inline int get_bitmask_order(unsigned int count);

#include <linux/version.h>

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,4,9)
#ifdef __i386__
extern inline void prefetch(const void *x);
#endif
#endif

#ifndef SUSE
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,18)
#ifdef __ia64__
extern inline void prefetch(const void *x);
extern inline void prefetchw(const void *x);
#endif
#endif
#else
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,21)
#ifdef __ia64__
extern inline void prefetch(const void *x);
extern inline void prefetchw(const void *x);
#endif
#endif
#endif

/*
** System header files
*/
#define   __KERNEL_SYSCALLS__
#include  <linux/types.h>
#include  <linux/config.h>
#include  <linux/module.h>
#include  <linux/init.h>
#include  <linux/kernel.h>
#include  <asm/byteorder.h>
#include  <linux/spinlock.h>
#include  <linux/slab.h>
#include  <linux/slab.h>
#include  <linux/sched.h>
#include  <linux/delay.h>
#include  <linux/wait.h>
#include  <linux/list.h>
#include  <linux/fs.h>
#include  <linux/pagemap.h>
#include  <linux/random.h>
#include  <linux/string.h>
#include  <linux/locks.h>
#include  <linux/hdreg.h>
#include  <linux/file.h>
#include  <linux/raw.h>
#include  <linux/vmalloc.h>
#include  <linux/proc_fs.h>
#include  <linux/unistd.h>
#include  <asm/uaccess.h>
#include  <linux/net.h>
#include  <net/sock.h>
#include  <linux/ctype.h>
#include  <linux/tqueue.h>
#include  <linux/inet.h>

/*
** Private header files
*/
#include  <ocfsbool.h>
#include  <ocfsconst.h>
#include  <ocfscom.h>

#include  <ocfshash.h>
#include  <ocfstrace.h>
#include  <ocfsvol.h>
#include  <ocfsdisk.h>
#include  <ocfsdef.h>
#include  <ocfstrans.h>
#include  <ocfsdlm.h>

#include  <ocfsver.h>

#include  <ocfsiosup.h>
#include  <ocfsport.h>
#include  <ocfsgenmisc.h>
#include  <ocfsgenalloc.h>
#include  <ocfsgencreate.h>
#include  <ocfsgendirnode.h>
#include  <ocfsgendlm.h>
#include  <ocfsheartbeat.h>
#include  <ocfsgennm.h>
#include  <ocfsgensysfile.h>
#include  <ocfsgentrans.h>
#include  <ocfsgenutil.h>
#include  <ocfsgenvolcfg.h>
#include  <ocfsgenvote.h>

#include  <ocfsbitmap.h>
#include  <ocfsdlmp.h>
#include  <ocfsfile.h>
#include  <ocfsioctl.h>
#include  <ocfsipc.h>
#include  <ocfsmain.h>
#include  <ocfsmount.h>
#include  <ocfsproc.h>

#endif				/* _OCFS_H_ */
