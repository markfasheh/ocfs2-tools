/*
 * ocfscom.h
 *
 * Includes datatype typedefs among other things
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

#ifndef _OCFSCOM_H_
#define _OCFSCOM_H_

#define OCFS_POINTER_SIZE   (sizeof(void *))

#ifdef __LP64__
#define OCFS_GCC_ATTR_PACKED	__attribute__ ((packed))
#define OCFS_GCC_ATTR_ALIGNED	__attribute__ ((aligned(4)))
#define OCFS_GCC_ATTR_PACKALGN	__attribute__ ((aligned(4), packed))
#else
#define OCFS_GCC_ATTR_PACKED
#define OCFS_GCC_ATTR_ALIGNED
#define OCFS_GCC_ATTR_PACKALGN
#endif

typedef struct _ocfs_alloc_bm
{
	void *buf;
	__u32 size;
	__u32 failed;
	__u32 ok_retries;
}
ocfs_alloc_bm;

typedef struct _ocfs_sem
{
	long magic;		/* OCFS_SEM_MAGIC */
	pid_t pid;
	long count;
	struct semaphore sem;
}
ocfs_sem;

/* convenience macro */
#define ocfs_safefree(x)	\
do {				\
	if (x)			\
		ocfs_free(x);	\
	(x) = NULL;		\
} while (0)

#define ocfs_safevfree(x)	\
do {				\
	if (x)			\
		vfree(x);	\
	(x) = NULL;		\
} while (0)

#define OCFS_ASSERT(x)             do { if (!(x)) BUG(); } while (0)
#define OCFS_BREAKPOINT()          printk("DEBUG BREAKPOINT! %s, %d\n", \
                                          __FILE__, __LINE__)

/* time is in 0.1 microsecs */
#ifdef USERSPACE_TOOL

#define OcfsQuerySystemTime(t)                                          \
                          do {                                          \
				  (*t) = (time(NULL)); 			\
                          } while (0)
#else
#define OcfsQuerySystemTime(t)                                                \
                          do {                                                \
                            (*t)  = (__u64)((__u64)CURRENT_TIME * (__u64)10000000); \
                            (*t) += (__u64)((__u64)xtime.tv_usec * (__u64)10);      \
                          } while (0)
#endif

#ifdef __KERNEL__
#define ocfs_getpid()               current->pid
#endif
#ifndef __KERNEL__
#define ocfs_getpid()               getpid()
#endif

typedef struct _ocfs_extent
{
	__s64 virtual;
	__s64 physical;
	__s64 sectors;
}
ocfs_extent;

typedef struct _ocfs_extent_map
{
	spinlock_t lock;
	__u32 capacity;
	__u32 count;
	bool initialized;
	void *buf;
}
ocfs_extent_map;

typedef struct _alloc_item
{
        enum { SLAB_ITEM, KMALLOC_ITEM, VMALLOC_ITEM } type;
	void *address;
        union {
        	int length;
                void *slab;
        } u;
	struct list_head list;
	char tag[30];
}
alloc_item;

/* i_flags flag - heh yeah i know it's evil! */
#define S_OCFS_OIN_VALID          256

#define inode_data_is_oin(i)      (i->i_flags & S_OCFS_OIN_VALID)

#define SET_INODE_OFFSET(i,o)     do { \
                                      i->i_flags     &= ~S_OCFS_OIN_VALID; \
                                      i->u.generic_ip = (void *)HI(o); \
                                      i->i_ino        = LO(o); \
                                  } while (0)

#define GET_INODE_OFFSET(i)       (__u64)((((__u64)((unsigned long)i->u.generic_ip))<<32) + \
                                        ((__u64)i->i_ino))

#define SET_INODE_OIN(i,o)        do { \
                                      i->i_flags     |= S_OCFS_OIN_VALID; \
                                      i->u.generic_ip = (void *)o; \
                                  } while (0)

#define FIRST_FILE_ENTRY(dir)   ((char *) ((char *)dir)+OCFS_SECTOR_SIZE)
#define FILEENT(dir,idx)        (ocfs_file_entry *) ( ((char *)dir) + \
                                ((dir->index[idx]+1) * OCFS_SECTOR_SIZE))

#define IS_FE_DELETED(_flg)				\
	(!(_flg) ||					\
	 ((_flg) & OCFS_SYNC_FLAG_MARK_FOR_DELETION) ||	\
	 ((_flg) & OCFS_SYNC_FLAG_NAME_DELETED) ||	\
	 ((_flg) & OCFS_SYNC_FLAG_DELETED))

#endif				/*  _OCFSCOM_H_ */
