/*
 * ocfsport.c
 *
 * Linux specific utilities
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

#ifdef __KERNEL__
#include <ocfs.h>
#else  /* ! KERNEL */
#include <libocfs.h>
#endif


static int get_overlap_type (__u64 new, __u64 newend, __u64 exist, __u64 existend);
static bool OcfsCoalesceExtentMapEntry (ocfs_extent_map * map,
			    __s64 virtual, __s64 physical, __s64 sectorcount);


/* Tracing */
#define OCFS_DEBUG_CONTEXT  OCFS_DEBUG_CONTEXT_PORT

/*
 * ocfs_init_sem()
 *
 */
void ocfs_init_sem (ocfs_sem * res)
{
#ifdef USERSPACE_TOOL
#if 0
	if ((res->sem.semid = semget(IPC_PRIVATE, 1, IPC_CREAT | S_IRWXU)) == -1) {
		fprintf(stderr, "ocfs_init_sem failed, %s\n", strerror(errno));
		exit(1);	
	}
	ocfs_up_sem(res);
#endif
#else
	LOG_ENTRY_ARGS ("(0x%p)\n", res);

	memset (res, 0, sizeof (ocfs_sem));
	init_MUTEX (&(res->sem));
	res->magic = OCFS_SEM_MAGIC;
	
	LOG_EXIT ();
#endif
}				/* ocfs_init_sem */

/*
 * ocfs_down_sem()
 *
 * Counter layer atop the sem. If a process which already owns the sem,
 * attempts to re-acquire it, ocfs_down_sem() increments the
 * count by 1. If however, a different process attempts to acquire that
 * sem, it blocks waiting for the sem to be released.
 * ocfs_up_sem() decrements the count by 1, if the owning
 * process releases the sem. The sem is released when the counter hits 0.
 * NT Port leftover, we want to get rid of this as soon as possible
 */
bool ocfs_down_sem (ocfs_sem * res, bool wait)
{
	bool ret = true;
#ifdef USERSPACE_TOOL
#if 0
	struct sembuf sops;

	/* NOT implementing the recursive sem in user */
	sops.sem_num = 0;
	sops.sem_op = -1;
	sops.sem_flg = SEM_UNDO | (wait ? 0 : IPC_NOWAIT);
	if (semop(res->sem.semid, &sops, 1) == -1) {
		ret = false;
		if (errno == EAGAIN && !wait)
			ret = true;
	}
	if (ret == false)
		fprintf(stderr, "ocfs_down_sem failed, %s\n", strerror(errno));
#endif
#else
	LOG_ENTRY_ARGS ("(0x%p, %u)\n", res, wait);

	if (!res || res->magic != OCFS_SEM_MAGIC)
		BUG();

#define WAIT_TILL_ACQUIRE(a)			\
	do {					\
		down(&((a)->sem));		\
		(a)->pid = current->pid;	\
		(a)->count = 1;			\
	} while(0)

	if (res->pid == 0) {
		if (wait)
			WAIT_TILL_ACQUIRE(res);
		else {
			if (!down_trylock(&(res->sem))) {
				res->pid = current->pid;
				res->count = 1;
			}
			else
				ret = false;
		}
	} else {
		if (res->pid == current->pid) {
			res->count++;
		} else {
			if (wait)
				WAIT_TILL_ACQUIRE(res);
			else
				ret = false;
		}
	}

	LOG_EXIT_ULONG (ret);
#endif /* !USERSPACE_TOOL */

	return ret;
}				/* ocfs_down_sem */

/*
 * ocfs_up_sem()
 *
 * ocfs_up_sem() decrements the count by 1, if the owning
 * process releases the sem. The sem is released when the counter hits 0.
 * Remained of NT port, we really really do not want this nesting
 * but for now it's there, we'll clean it up
 */
void ocfs_up_sem (ocfs_sem * res)
{
#ifdef USERSPACE_TOOL
#if 0
	struct sembuf sops;
	
	/* NOT implementing the recursive sem in user */
	sops.sem_num = 0;
	sops.sem_op = 1;
	sops.sem_flg = SEM_UNDO;
	if (semop(res->sem.semid, &sops, 1) == -1) {
		fprintf(stderr, "ocfs_up_sem failed, %s\n", strerror(errno));
	}
#endif
#else
	LOG_ENTRY_ARGS ("(0x%p)\n", res);

	if (!res || res->magic != OCFS_SEM_MAGIC)
		BUG();

	if (res->count && current->pid == res->pid) {
		res->count--;
		if (!res->count) {
			res->pid = 0;
			up (&(res->sem));
		}
	}

	LOG_EXIT ();
#endif

	return;
}				/* ocfs_up_sem */

/*
 * ocfs_del_sem()
 *
 */
int ocfs_del_sem (ocfs_sem * res)
{
#ifdef USERSPACE_TOOL
#if 0
	union semun junk;
	if (semctl(res->sem.semid, 0, IPC_RMID, junk) == -1) {
		LOG_EXIT ();
		return -errno;
	}
#endif
#else
	if (res)
		res->magic = OCFS_SEM_DELETED;
#endif

	return 0;
}				/* ocfs_del_sem */


/*
 * ocfs_daemonize() 
 *
 */
void ocfs_daemonize (char *name, int len)
{
#ifndef USERSPACE_TOOL
	sigset_t tmpsig;

	daemonize ();
	reparent_to_init ();

	if (len > 0) {
		if (len > 15)
			BUG();
		strncpy (current->comm, name, len);
		current->comm[len] = '\0';
	}

	/* Block all signals except SIGKILL, SIGSTOP, SIGHUP and SIGINT */
#ifdef HAVE_NPTL
        spin_lock_irq (&current->sighand->siglock);
        tmpsig = current->blocked;
        siginitsetinv (&current->blocked, SHUTDOWN_SIGS);
        recalc_sigpending ();
        spin_unlock_irq (&current->sighand->siglock);
#else
	spin_lock_irq (&current->sigmask_lock);
	tmpsig = current->blocked;
	siginitsetinv (&current->blocked, SHUTDOWN_SIGS);
	recalc_sigpending (current);
	spin_unlock_irq (&current->sigmask_lock);
#endif
#endif	/* !USERSPACE_TOOL */

	return;
}				/* ocfs_daemonize */


/*
 * ocfs_sleep()
 *
 * The interval time is in milliseconds
 *
 * This function needs to be removed.
 * Instead call schedule_timeout() directly and handle signals.
 */
int ocfs_sleep (__u32 ms)
{
#ifdef USERSPACE_TOOL
	struct timespec req, rem;

	memset(&rem, 0, sizeof(struct timespec));
	req.tv_sec = ms / 1000;
	req.tv_nsec = (ms % 1000) * 1000000;
	while (nanosleep(&req, &rem) == -1 && errno == EINTR) {
		req.tv_sec = rem.tv_sec;
		req.tv_nsec = rem.tv_nsec;
		memset(&rem, 0, sizeof(struct timespec));
	}
#else
	__u32 numJiffies;

	LOG_ENTRY ();

	/* 10ms = 1 jiffy, minimum resolution is one jiffy */
	numJiffies = ms * HZ / 1000;
	numJiffies = (numJiffies < 1) ? 1 : numJiffies;
	set_current_state (TASK_INTERRUPTIBLE);
	numJiffies = schedule_timeout (numJiffies);
	
	LOG_EXIT ();
#endif

	return 0;
}				/* ocfs_sleep */

#ifndef USERSPACE_TOOL

#ifdef OCFS_LINUX_MEM_DEBUG
#define SUPER_VERBOSE_MEM_DEBUG  1
#endif

/*
 * ocfs_dbg_slab_alloc()
 *
 */
void *ocfs_dbg_slab_alloc (kmem_cache_t *slab, char *file, int line)
{
    void *m;
    m = kmem_cache_alloc(slab, GFP_NOFS);
#ifdef OCFS_LINUX_MEM_DEBUG
    if (m == NULL) {
        LOG_ERROR_ARGS("failed to alloc from slab = %p", slab);
    } else {
		alloc_item *new;
		new = kmalloc (sizeof (alloc_item), GFP_NOFS);
                new->type = SLAB_ITEM;
		new->address = m;
		new->u.slab = slab;
		snprintf (new->tag, 30, "%d:%s", line, file);
		new->tag[29] = '\0';
		list_add (&new->list, &OcfsGlobalCtxt.item_list);
#ifdef SUPER_VERBOSE_MEM_DEBUG
		LOG_TRACE_ARGS (" + %x (%p, '%s')\n", m, slab, new->tag);
#endif
    }
#endif
    return m;
}                               /* ocfs_dbg_slab_alloc */

/*
 * ocfs_dbg_slab_free()
 *
 */
void ocfs_dbg_slab_free (kmem_cache_t *slab, void *m)
{

#ifdef OCFS_LINUX_MEM_DEBUG
	struct list_head *iter;
	struct list_head *temp_iter;
        alloc_item *item = NULL;
        bool do_free = false;

	list_for_each_safe (iter, temp_iter, &OcfsGlobalCtxt.item_list) {
		item = list_entry (iter, alloc_item, list);

		if (item->address == m && item->type == SLAB_ITEM) {
#ifdef SUPER_VERBOSE_MEM_DEBUG
			LOG_TRACE_ARGS (" - %x (%p, '%s')\n", m, item->u.slab, item->tag);
#endif
                        list_del (&item->list);
                        do_free = true;
			break;
		}
	}

        if (do_free) {
                kmem_cache_free(slab, m);
                kfree (item);
                return;
        }
	LOG_ERROR_ARGS ("tried to free mem never allocated: %x", m);
#endif
#ifndef OCFS_LINUX_MEM_DEBUG
	kmem_cache_free(slab, m);
#endif
}				/* ocfs_dbg_slab_free */



/*
 * ocfs_linux_dbg_alloc()
 *
 */
void *ocfs_linux_dbg_alloc (int Size, char *file, int line)
{
	void *m;

	m = kmalloc (Size, GFP_NOFS);
#ifdef OCFS_LINUX_MEM_DEBUG
	if (m == NULL) {
		LOG_ERROR_ARGS ("failed! (size=%d)", Size);
	} else {
		alloc_item *new;
		new = kmalloc (sizeof (alloc_item), GFP_NOFS);
                new->type = KMALLOC_ITEM;
		new->address = m;
		new->u.length = Size;
		snprintf (new->tag, 30, "%d:%s", line, file);
		new->tag[29] = '\0';
		list_add (&new->list, &OcfsGlobalCtxt.item_list);
#ifdef SUPER_VERBOSE_MEM_DEBUG
		LOG_TRACE_ARGS (" + %x (%d, '%s')\n", m, Size, new->tag);
#endif
	}
#endif
	return m;
}				/* ocfs_linux_dbg_alloc */

/*
 * ocfs_linux_dbg_free()
 *
 */
void ocfs_linux_dbg_free (const void *Buffer)
{

#ifdef OCFS_LINUX_MEM_DEBUG
	struct list_head *iter;
	struct list_head *temp_iter;
        alloc_item *item = NULL;
        bool do_free = false;

	list_for_each_safe (iter, temp_iter, &OcfsGlobalCtxt.item_list) {
		item = list_entry (iter, alloc_item, list);

		if (item->address == Buffer && item->type == KMALLOC_ITEM) {
#ifdef SUPER_VERBOSE_MEM_DEBUG
			LOG_TRACE_ARGS (" - %x (%d, '%s')\n", Buffer,
					item->u.length, item->tag);
#endif
			list_del (&item->list);
                        do_free = true;
			break;
		}
	}
        if (do_free) {
                kfree (Buffer);
                kfree (item);
                return;
        }
	LOG_ERROR_ARGS ("tried to free mem never allocated: %x", Buffer);
#endif
#ifndef OCFS_LINUX_MEM_DEBUG
	kfree (Buffer);
#endif
}				/* ocfs_linux_dbg_free */


/*
 * ocfs_linux_get_inode_offset()
 *
 */
bool ocfs_linux_get_inode_offset (struct inode * inode, __u64 * off, ocfs_inode ** oin)
{
	if (off == NULL)
		return false;

	if (oin != NULL)
		*oin = NULL;

	if (inode_data_is_oin (inode)) {
		ocfs_inode *f = ((ocfs_inode *)inode->u.generic_ip);

		if (f == NULL) {
			LOG_ERROR_STR ("bad inode oin");
			*off = -1;
			return false;
		} else {
			OCFS_ASSERT(IS_VALID_OIN(f));
			if (oin != NULL)
				*oin = f;
			if (S_ISDIR (inode->i_mode))
				*off = f->dir_disk_off;
			else
				*off = f->file_disk_off;
		}
	} else {
		*off = GET_INODE_OFFSET (inode);
	}
	return (*off != -1);
}				/* ocfs_linux_get_inode_offset */


/*
 * ocfs_linux_get_dir_entry_offset()
 *
 */
bool ocfs_linux_get_dir_entry_offset (ocfs_super * osb, __u64 * off, __u64 parentOff,
			    struct qstr * fileName, ocfs_file_entry ** fileEntry)
{
	int status;
	ocfs_file_entry *ent;

	if (off == NULL)
		return false;

	*off = -1;
	ent = ocfs_allocate_file_entry ();
	if (ent != NULL) {
		status = ocfs_find_files_on_disk (osb, parentOff, fileName, ent, NULL);
		if (status >= 0)
			*off = ent->this_sector;

		/* if the caller wants the file entry let him free it */
		if (fileEntry)
			*fileEntry = ent;
		else
			ocfs_release_file_entry (ent);
	}
	return (*off != -1);
}				/* ocfs_linux_get_dir_entry_offset */

#endif /* !USERSPACE_TOOL */

/*
 * ocfs_flush_cache()
 *
 */
void ocfs_flush_cache (ocfs_super * osb)
{
	fsync_no_super (osb->sb->s_dev);
}				/* ocfs_flush_cache */


/*
 * ocfs_purge_cache_section()
 *
 */
bool ocfs_purge_cache_section (ocfs_inode * oin, __u64 * file_off, __u32 Length)
{
	if (oin != NULL && oin->inode != NULL) {
		fsync_inode_buffers (oin->inode);
	}
	return true;
}				/* ocfs_purge_cache_section */

#ifndef USERSPACE_TOOL
/* prefetch has been declared to allow to build in debug mode */
#ifdef DEBUG
#ifndef ARCH_HAS_PREFETCH
inline void prefetch (const void *x) {;}
#endif
#ifndef ARCH_HAS_PREFETCHW
inline void prefetchw(const void *x) {;}
#endif
#endif /* !DEBUG */
#endif /* !USERSPACE_TOOL */


/* Crazy wacky extent map stuff */
/* works ok in userland stuff too */

#define GET_EXTENT_MAP_ENTRY(map, i)    ((ocfs_extent *) ((__u8 *)map->buf + \
							  ((i) * sizeof(ocfs_extent))))

/*
 * ocfs_extent_map_init()
 *
 */
void ocfs_extent_map_init (ocfs_extent_map * map)
{
	LOG_ENTRY ();

	OCFS_ASSERT (map != NULL);
	spin_lock_init(&(map->lock));
	map->capacity = 0;
	map->count = 0;
	map->initialized = true;
	map->buf = NULL;

	LOG_EXIT ();
	return;
}				/* ocfs_extent_map_init */

/*
 * ocfs_extent_map_destroy()
 *
 */
void ocfs_extent_map_destroy (ocfs_extent_map * map)
{
	LOG_ENTRY ();

	if (!map)
		goto leave;
	
	if (map->initialized) {
                spin_lock(&(map->lock));
		#warning RACE! need to retest map->initialized here!
		map->capacity = 0;
		map->count = 0;
		ocfs_safefree (map->buf);
		map->initialized = false;
                spin_unlock(&(map->lock));
	}

leave:
	LOG_EXIT ();
	return;
}				/* ocfs_extent_map_destroy */

/*
 * ocfs_extent_map_get_count()
 *
 */
__u32 ocfs_extent_map_get_count (ocfs_extent_map * map)
{
	__u32 ret;

	LOG_ENTRY ();

	OCFS_ASSERT (map != NULL);
	#warning this locking almost has to be a bug
	spin_lock(&(map->lock));
	ret = map->count;
	spin_unlock(&(map->lock));

	LOG_EXIT_ULONG (ret);
	return ret;
}				/* ocfs_extent_map_get_count */

enum
{
	LEFT_NO_OVERLAP,
	LEFT_ADJACENT,
	LEFT_OVERLAP,
	FULLY_CONTAINED,
	FULLY_CONTAINING,
	RIGHT_OVERLAP,
	RIGHT_ADJACENT,
	RIGHT_NO_OVERLAP
};

/*
 * get_overlap_type()
 *
 */
static int get_overlap_type (__u64 new, __u64 newend, __u64 exist, __u64 existend)
{
	OCFS_ASSERT (newend > new);
	OCFS_ASSERT (existend > exist);

	if (new < exist) {
		if (newend < exist)
			return LEFT_NO_OVERLAP;
		else if (newend == exist)
			return LEFT_ADJACENT;
		else if (newend >= existend)	/* && newend > exist */
			return FULLY_CONTAINING;
		else		/* newend < existend && newend > exist */
			return LEFT_OVERLAP;
	} else if (new > exist) {
		if (new > existend)
			return RIGHT_NO_OVERLAP;
		else if (new == existend)
			return RIGHT_ADJACENT;
		else if (newend > existend)	/* && new < existend */
			return RIGHT_OVERLAP;
		else		/* newend <= existend && new < existend */
			return FULLY_CONTAINED;
	} else if (newend > existend)	/* && new == exist */
		return FULLY_CONTAINING;
	else			/* newend <= existend && new == exist */
		return FULLY_CONTAINED;
}				/* get_overlap_type */

/*
 * OcfsCoalesceExtentMapEntry()
 *
 * Must call this with spinlock already held!
 */
static bool OcfsCoalesceExtentMapEntry (ocfs_extent_map * map,
			    __s64 virtual, __s64 physical, __s64 sectorcount)
{
	ocfs_extent *tmp, *tmp2;
	int i, voverlap, loverlap, newIdx;
	bool ret = false;

	LOG_ENTRY ();

	if (!map->initialized) {
		LOG_ERROR_STR ("ExtentMap is not initialized");
		goto bail;
	}

	/* attempt to coalesce this into an existing entry */

	/* 
	 * NOTE: if we are successful in coalescing this entry with an entry from somewhere
	 *       in the list, we still need to check the rest of the list in case this entry
	 *       ends up filling one or more holes
	 *                 |---- this ----|
	 *       |-- found --|          |-- another entry --|
	 *                     |---| <--- yet another entry
	 */

	newIdx = -1;
	for (i = 0; i < map->count; i++) {
		tmp = GET_EXTENT_MAP_ENTRY (map, i);
		voverlap =
		    get_overlap_type (virtual, virtual + sectorcount,
				      tmp->virtual,
				      tmp->virtual + tmp->sectors);
		loverlap =
		    get_overlap_type (physical, physical + sectorcount,
				      tmp->physical,
				      tmp->physical + tmp->sectors);

		/* first off, if the virtual range and real range don't */
		/* overlap in the same way it definitely can't be coalesced */
		if (voverlap != loverlap)
			continue;

		switch (voverlap) {
		    case FULLY_CONTAINED:	/* already fully accounted for, done */
			    ret = true;
			    goto bail;
			    break;

		    case LEFT_ADJACENT:	/* add new left part to found entry */
			    sectorcount += tmp->sectors;
			    tmp->sectors = 0;	/* mark for deletion */
			    ret = true;
			    break;

		    case RIGHT_ADJACENT:	/* add new right part to found entry */
			    virtual = tmp->virtual;
			    physical = tmp->physical;
			    sectorcount += tmp->sectors;
			    tmp->sectors = 0;	/* mark for deletion */
			    ret = true;
			    break;

		    case FULLY_CONTAINING:	/* completely take over this entry */
			    tmp->sectors = 0;	/* mark for deletion */
			    ret = true;
			    break;

		    case LEFT_OVERLAP:	/* should begin at new physical/virtual, end at old end */
			    if ((tmp->virtual - virtual) == (tmp->physical - physical))
			    {
				    /* must be same distance from edge */
				    sectorcount =
					tmp->sectors + (tmp->virtual - virtual);
				    tmp->sectors = 0;	/* mark for deletion */
				    ret = true;
			    }
			    break;

		    case RIGHT_OVERLAP:	/* should begin at old physical/virtual, end at new end */
			    if ((virtual - tmp->virtual) ==
				(physical - tmp->physical)) {
				    sectorcount =
					virtual + sectorcount - tmp->virtual;
				    virtual = tmp->virtual;
				    physical = tmp->physical;
				    tmp->sectors = 0;	/* mark for deletion */
				    ret = true;
			    }
			    break;

		    case LEFT_NO_OVERLAP:	/* keep looking */
		    case RIGHT_NO_OVERLAP:
			    break;
		}

		if (tmp->sectors == 0) {
			if (newIdx == -1)	/* first time thru, this is where we */
						/* will put the coalesced entry */
				newIdx = i;
			else {
				/* otherwise swap the tail with the current... */
				tmp2 = GET_EXTENT_MAP_ENTRY (map, map->count - 1);
				tmp->virtual = tmp2->virtual;
				tmp->physical = tmp2->physical;
				tmp->sectors = tmp2->sectors;
				tmp2->sectors = 0;
				map->count--;	/* ...and dump the tail */
			}
		}
	}

	if (newIdx != -1) {	/* finally, stick the coalesced thing into newIdx */
		tmp = GET_EXTENT_MAP_ENTRY (map, newIdx);
		tmp->virtual = virtual;
		tmp->physical = physical;
		tmp->sectors = sectorcount;
	}

      bail:

	LOG_EXIT_ULONG (ret);
	return ret;
}				/* OcfsCoalesceExtentMapEntry */

/*
 * ocfs_extent_map_add()
 *
 */
bool ocfs_extent_map_add (ocfs_extent_map * map, __s64 virtual, __s64 physical,
			  __s64 sectorcount)
{
	ocfs_extent *tmp;
	void *newpool;
	__u32 newmax;
	bool ret = false;

	LOG_ENTRY ();

	OCFS_ASSERT (map != NULL);

	if (!map->initialized) {
		LOG_ERROR_STATUS (-EFAIL);
		goto bail;
	}
	spin_lock(&(map->lock));

	if ((ret =
	     OcfsCoalesceExtentMapEntry (map, virtual, physical,
					 sectorcount))) {
		LOG_TRACE_STR ("Successfully coalesced map entry");
		goto release_spinlock;
	}

	/* if extra allocation needed, do it now */
	if (map->count >= map->capacity) {
		/* TODO: come up with some better algorithm, */
		/* for now: first-double size, second-just one more */
		newmax =
		    (map->capacity >
		     0) ? map->capacity * 2 : INITIAL_EXTENT_MAP_SIZE;
		newpool = ocfs_malloc (newmax * sizeof (ocfs_extent));
		if (newpool == NULL && newmax != INITIAL_EXTENT_MAP_SIZE) {
			newmax = map->capacity + 1;
			newpool = ocfs_malloc (newmax * sizeof (ocfs_extent));
		}
		if (newpool == NULL) {
			LOG_ERROR_STATUS (-ENOMEM);
			goto release_spinlock;
		}
		if (map->buf && map->capacity)
			memcpy (newpool, map->buf,
				map->capacity * sizeof (ocfs_extent));
		ocfs_safefree (map->buf);
		map->buf = newpool;
		map->capacity = newmax;
	}

	tmp = GET_EXTENT_MAP_ENTRY (map, map->count);
	tmp->virtual = virtual;
	tmp->physical = physical;
	tmp->sectors = sectorcount;
	map->count++;
	ret = true;

release_spinlock:
	spin_unlock(&(map->lock));

bail:
	LOG_EXIT_ULONG (ret);
	return ret;
}				/* ocfs_extent_map_add */

/* ocfs_extent_map_remove()
 *
 */
void ocfs_extent_map_remove (ocfs_extent_map * map, __s64 virtual, __s64 sectorcount)
{
	ocfs_extent *tmp;
	__u32 i;
	int voverlap;

	LOG_ENTRY ();

	OCFS_ASSERT (map != NULL);

	if (!map->initialized)
		goto bail;
	spin_lock(&(map->lock));
	for (i = 0; i < map->count; i++) {
		tmp = GET_EXTENT_MAP_ENTRY (map, i);
		voverlap =
		    get_overlap_type (virtual, virtual + sectorcount,
				      tmp->virtual,
				      tmp->virtual + tmp->sectors);
		switch (voverlap) {
		    case FULLY_CONTAINED:
			    /* for now, don't allow splitting of entries */
			    if (virtual == tmp->virtual
				&& sectorcount == tmp->sectors) {
				    if (i != map->count - 1)
					    memcpy ((void *) tmp, (void *)
						    GET_EXTENT_MAP_ENTRY (map,
									  (map->
									   count
									   -
									   1)),
						    sizeof (ocfs_extent));
				    map->count--;
				    goto release_spinlock;
			    }
			    break;
		    default:	/* all others would be an error */
			    break;
		}
	}

release_spinlock:
	spin_unlock(&(map->lock));
bail:

	LOG_EXIT ();
	return;
}				/* ocfs_extent_map_remove */

/*
 * ocfs_extent_map_lookup()
 *
 */
bool ocfs_extent_map_lookup (ocfs_extent_map *map, __s64 virtual, __s64 *physical,
			     __s64 *sectorcount, __u32 *index)
{
	ocfs_extent *tmp;
	bool ret = false;
	__u32 idx = 0;

	LOG_ENTRY ();

	OCFS_ASSERT (map != NULL);

	if (!map->initialized) {
		LOG_ERROR_STR ("BUG! Uninitialized ExtentMap!");
		goto bail;
	}
	spin_lock(&(map->lock));

	for (idx = 0; idx < map->count; idx++) {
		__s64 hi, lo, delta;

		tmp = GET_EXTENT_MAP_ENTRY (map, idx);

		lo = tmp->virtual;
		hi = lo + tmp->sectors;
		delta = virtual - lo;

		if (virtual >= lo && virtual < hi) {
			*physical = tmp->physical + delta;
			*sectorcount = tmp->sectors - delta;
			idx++;
			ret = true;
			break;
		}
	}
	spin_unlock(&(map->lock));

bail:
	*index = idx;

	LOG_EXIT_ULONG (ret);
	return ret;
}				/* ocfs_extent_map_lookup */

/*
 * ocfs_extent_map_next_entry()
 *
 */
bool ocfs_extent_map_next_entry (ocfs_extent_map *map, __u32 runindex,
				 __s64 *virtual, __s64 *physical, __s64 *sectorcount)
{
	ocfs_extent *tmp;
	bool ret = false;

	LOG_ENTRY ();

	OCFS_ASSERT (map != NULL);

	if (!map->initialized)
		goto bail;
	spin_lock(&(map->lock));
	if (runindex >= map->count)
		goto release_spinlock;
	tmp = GET_EXTENT_MAP_ENTRY (map, runindex);
	*virtual = tmp->virtual;
	*physical = tmp->physical;
	*sectorcount = tmp->sectors;
	ret = true;

release_spinlock:
	spin_unlock(&(map->lock));
bail:

	LOG_EXIT_ULONG (ret);
	return ret;
}				/* ocfs_extent_map_next_entry */
