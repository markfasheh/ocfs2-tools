/*
 * ocfsport.h
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

#ifndef _OCFSPORT_H_
#define _OCFSPORT_H_


/* timeout structure taken from Ben's aio.c */
#ifndef USERSPACE_TOOL
typedef struct _ocfs_timeout {
	struct timer_list	timer;
	int			timed_out;
	wait_queue_head_t	wait;
}
ocfs_timeout;

static void ocfs_timeout_func(unsigned long data)
{
	ocfs_timeout *to = (ocfs_timeout *)data; 

	to->timed_out = 1;
	wake_up(&to->wait);
}

static inline void ocfs_init_timeout(ocfs_timeout *to)
{
	init_timer(&to->timer);
	to->timer.data = (unsigned long)to;
	to->timer.function = ocfs_timeout_func;
	to->timed_out = 0;
	init_waitqueue_head(&to->wait);
}

static inline void ocfs_set_timeout(ocfs_timeout *to, __u32 timeout)
{
	__u32 how_long;

	if (!timeout) {
		to->timed_out = 1;
		return ;
	}

	how_long = (timeout * HZ / 1000);
	if (how_long < 1)
		how_long = 1;

	to->timer.expires = jiffies + how_long;
	add_timer(&to->timer);
}

static inline void ocfs_clear_timeout(ocfs_timeout *to)
{
	del_timer_sync(&to->timer);
}

#define __ocfs_wait(wq, condition, timeo, ret)			\
do {								\
	ocfs_timeout __to;					\
								\
	DECLARE_WAITQUEUE(__wait, current);			\
	DECLARE_WAITQUEUE(__to_wait, current);			\
								\
	ocfs_init_timeout(&__to);				\
								\
	if (timeo) {						\
		ocfs_set_timeout(&__to, timeo);			\
		if (__to.timed_out) {				\
			ocfs_clear_timeout(&__to);		\
		}						\
	}							\
								\
	add_wait_queue(&wq, &__wait);				\
	add_wait_queue(&__to.wait, &__to_wait);			\
	do {							\
		ret = 0;					\
		set_task_state(current, TASK_INTERRUPTIBLE);	\
		if (condition)					\
			break;					\
		ret = -ETIMEDOUT;				\
		if (__to.timed_out)				\
			break;					\
		schedule();					\
		if (signal_pending(current)) {			\
			ret = -EINTR;				\
			break;					\
		}						\
	} while (1);						\
								\
	set_task_state(current, TASK_RUNNING);			\
	remove_wait_queue(&wq, &__wait);			\
	remove_wait_queue(&__to.wait, &__to_wait);		\
								\
	if (timeo)						\
		ocfs_clear_timeout(&__to);			\
								\
} while(0)

#else /* USERSPACE_TOOL */

/* userspace version not intended to be the same as above */
/* timeout is ignored, waits forever until condition */
#define __ocfs_wait(wq, condition, timeo, ret) 			\
do {								\
	struct timespec req, rem;				\
	ret = 0;						\
	memset(&rem, 0, sizeof(struct timespec));		\
	req.tv_sec = 0;						\
	req.tv_nsec = 10000000;	/* 10ms */			\
	while (!(condition) && nanosleep(&req, &rem)==-1) {	\
		req.tv_nsec=rem.tv_nsec;			\
		rem.tv_nsec = 0;				\
	}							\
	if (condition)						\
		break;						\
} while (1)

#endif  /* USERSPACE_TOOL */

#define ocfs_wait(wq, condition, timeout)			\
({								\
        int __ret = 0;						\
        if (!(condition))					\
                __ocfs_wait(wq, condition, timeout, __ret);	\
        __ret;							\
})

void ocfs_init_sem (ocfs_sem * res);

bool ocfs_down_sem (ocfs_sem * res, bool wait);

void ocfs_up_sem (ocfs_sem * res);

int ocfs_del_sem (ocfs_sem * res);

void ocfs_daemonize (char *name, int len);

int ocfs_sleep (__u32 ms);

void ocfs_extent_map_init (ocfs_extent_map * map);

void ocfs_extent_map_destroy (ocfs_extent_map * map);

__u32 ocfs_extent_map_get_count (ocfs_extent_map * map);

bool ocfs_extent_map_add (ocfs_extent_map * map, __s64 virtual,
			  __s64 physical, __s64 sectorcount);

void ocfs_extent_map_remove (ocfs_extent_map * map, __s64 virtual,
			     __s64 sectorcount);

bool ocfs_extent_map_lookup (ocfs_extent_map * map, __s64 virtual,
			     __s64 * physical, __s64 * sectorcount, __u32 * index);

bool ocfs_extent_map_next_entry (ocfs_extent_map * map, __u32 runindex,
				 __s64 * virtual, __s64 * physical,
				 __s64 * sectorcount);

void *ocfs_dbg_slab_alloc (kmem_cache_t *slab, char *file, int line);
void ocfs_dbg_slab_free (kmem_cache_t *slab, void *m);

void *ocfs_linux_dbg_alloc (int Size, char *file, int line);
void ocfs_linux_dbg_free (const void *Buffer);

bool ocfs_linux_get_inode_offset (struct inode *inode, __u64 * off,
				  ocfs_inode ** oin);
bool ocfs_linux_get_dir_entry_offset (ocfs_super * osb, __u64 * off,
				      __u64 parentOff, struct qstr * fileName,
				      ocfs_file_entry ** fileEntry);

void ocfs_flush_cache (ocfs_super * osb);

bool ocfs_purge_cache_section (ocfs_inode * oin, __u64 * file_off, __u32 Length);

#endif				/* _OCFSPORT_H_ */
