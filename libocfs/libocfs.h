/*
 * libocfs.h
 *
 * kernel dummy types, macros, etc. used by userspace tools
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
 * Author: Kurt Hackel, Sunil Mushran
 */

#ifndef _LIBOCFS_H_
#define _LIBOCFS_H_

#ifndef DUMMY_C_LOCAL_DECLS
extern long TIME_ZERO;
#endif

/* Horrific, for SuSE */
#ifdef __ia64__
#define ia64_cmpxchg(sem,ptr,old,new,size)						\
({											\
	__typeof__(ptr) _p_ = (ptr);							\
	__typeof__(new) _n_ = (new);							\
	__u64 _o_, _r_;									\
											\
	switch (size) {									\
	      case 1: _o_ = (__u8 ) (long) (old); break;				\
	      case 2: _o_ = (__u16) (long) (old); break;				\
	      case 4: _o_ = (__u32) (long) (old); break;				\
	      case 8: _o_ = (__u64) (long) (old); break;				\
	      default: break;								\
	}										\
	 __asm__ __volatile__ ("mov ar.ccv=%0;;" :: "rO"(_o_));				\
	switch (size) {									\
	      case 1:									\
		__asm__ __volatile__ ("cmpxchg1."sem" %0=[%1],%2,ar.ccv"		\
				      : "=r"(_r_) : "r"(_p_), "r"(_n_) : "memory");	\
		break;									\
											\
	      case 2:									\
		__asm__ __volatile__ ("cmpxchg2."sem" %0=[%1],%2,ar.ccv"		\
				      : "=r"(_r_) : "r"(_p_), "r"(_n_) : "memory");	\
		break;									\
											\
	      case 4:									\
		__asm__ __volatile__ ("cmpxchg4."sem" %0=[%1],%2,ar.ccv"		\
				      : "=r"(_r_) : "r"(_p_), "r"(_n_) : "memory");	\
		break;									\
											\
	      case 8:									\
		__asm__ __volatile__ ("cmpxchg8."sem" %0=[%1],%2,ar.ccv"		\
				      : "=r"(_r_) : "r"(_p_), "r"(_n_) : "memory");	\
		break;									\
											\
	      default:									\
		_r_ = 0;								\
		break;									\
	}										\
	(__typeof__(old)) _r_;								\
})

#define cmpxchg_acq(ptr,o,n)	ia64_cmpxchg("acq", (ptr), (o), (n), sizeof(*(ptr)))
#endif

/* Get large file support */
#define _GNU_SOURCE

/* plain old user stuff */
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/mman.h>

/* Special case, cause of twisty nested includes */
#ifndef _LINUX_LIST_H
#define INIT_LIST_HEAD(a)	do { } while(0)
#define list_add_tail(a,b)	do { } while(0)
#define list_del(a)		do { } while(0)
struct list_head
{
    struct list_head *next, *prev;
};
#endif

/* reqd by ocfsformat */
#undef WNOHANG
#undef WUNTRACED
#include <linux/fs.h>

#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

#include <asm/bitops.h>


/* it sucks, but i have to fake a few types to get by */
#define spin_lock_init(lock)    do { } while(0)
#define spin_lock(lock)         (void)(lock) /* Not "unused variable". */
#define spin_is_locked(lock)    (0)
#define spin_trylock(lock)      ({1; })
#define spin_unlock_wait(lock)  do { } while(0)
#define spin_unlock(lock)       do { } while(0)
#define down(x)			do { } while(0)
#define up(x)			do { } while(0)
#define init_timer(x)		do { } while(0)
#define wake_up(x)		do { } while(0)
#define max(a,b)		((a) < (b) ? (b) : (a))
#define min(a,b)		((a) < (b) ? (a) : (b))
#define CURRENT_TIME		(time(NULL))
#define init_completion(x)	do { } while(0)
#define HZ			(100)
#define jiffies                 ({ struct timeval tv; \
				   long j; \
				   gettimeofday(&tv, NULL); \
				   if (TIME_ZERO==0) \
				     TIME_ZERO = tv.tv_sec; \
				   j = (tv.tv_sec - TIME_ZERO) * HZ; \
				   if (j < 0) \
				     j = 0; \
				   j += ((tv.tv_usec) / (1000000 / HZ)); \
				   (j); })
#define time_after(a,b)		((long)(b) - (long)(a) < 0)
#define kmalloc(a, b) malloc_aligned(a)
#define vmalloc(a)    malloc_aligned(a)
#define kfree(a)      free_aligned(a)
#define vfree(a)      free_aligned(a)
#define fsync_no_super(fd)     fsync(fd)
#define printk(a, x...)    fprintf(stderr, a, ##x)
#define BUG()              fprintf(stderr, "BUG!\n")

#define OCFS_PAGE_SIZE  4096
#define IN
#define OUT
#define KERN_ERR   
#define FILE_BUFFER_SIZE  (1048576 * 2)
#define ATTR_MODE       1
#define ATTR_UID        2
#define ATTR_GID        4
#define ATTR_SIZE       8
#define ATTR_ATIME      16
#define ATTR_MTIME      32
#define ATTR_CTIME      64
#define ATTR_ATIME_SET  128
#define ATTR_MTIME_SET  256
#define ATTR_FORCE      512     /* Not a change, but a change it */
#define ATTR_ATTR_FLAG  1024
#define NODEV 0

#ifndef UINT_MAX
#define UINT_MAX	(~0U)
#endif
#ifndef ULONG_MAX
#define ULONG_MAX	(~0U)
#endif



#define DELETED_FLAGS  (OCFS_SYNC_FLAG_DELETED | \
			OCFS_SYNC_FLAG_MARK_FOR_DELETION | \
			OCFS_SYNC_FLAG_NAME_DELETED)



typedef unsigned short kdev_t;

struct buffer_head {
	int a;
};

struct address_space {
	int a;
};

struct iattr { 
    unsigned long long ia_size;
    int ia_uid;
    int ia_gid;
    int ia_mode;
    int ia_ctime;
    int ia_mtime;
    int ia_valid;
};

typedef int spinlock_t;
typedef int wait_queue_head_t;

struct completion {
	unsigned int done;
	wait_queue_head_t wait;
};

struct timer_list {
	unsigned long expires;
	unsigned long data;
	void (*function)(unsigned long);
};

struct super_block
{
    int s_dev;
    union {
	    void *generic_sbp;
    } u;
};

struct semaphore
{
    int semid;
};
struct inode
{
    void * i_mapping;
    struct list_head i_dentry;
    int i_nlink;
    struct super_block *i_sb;
    unsigned int i_flags;
    unsigned long i_ino;
    int i_mode;
    unsigned long long i_size;
    int i_uid;
    int i_gid;
    unsigned long i_blocks;
    unsigned long i_blksize;
    int i_ctime;
    int i_atime;
    int i_mtime;
    kdev_t i_rdev;
    union {
        void *generic_ip;
    } u;
};
struct qstr {
	const unsigned char * name; 
	unsigned int len;
	unsigned int hash;
};
struct dentry 
{
	struct qstr d_name;
	struct inode *d_inode;
};
typedef int kmem_cache_t;
struct tq_struct
{
    int a;
};


struct task_struct
{
    pthread_t thread;
};

union semun {
	int val;                    /* value for SETVAL */
	struct semid_ds *buf;       /* buffer for IPC_STAT, IPC_SET */
	unsigned short int *array;  /* array for GETALL, SETALL */
	struct seminfo *__buf;      /* buffer for IPC_INFO */
};

/* aligned alloc prototypes */
void *memalign(size_t boundary, size_t size);
void * malloc_aligned(int size);
void free_aligned(void *ptr);

#ifndef ATOMIC_INIT

/* atomic_t stuff */

#if !defined(_ASM_IA64_ATOMIC_H) && !defined(__ARCH_X86_64_ATOMIC__) && !defined(__ARCH_I386_ATOMIC__) && !defined(_PPC_BITOPS_H) /* yes bitops is right. RH sucks and uses stub headers which still define _ASM_PPC_ATOMIC_H_ */
typedef struct { volatile int counter; } atomic_t;
#endif

#define ATOMIC_INIT(i)  { (i) }

#define atomic_read(v)          ((v)->counter)
#define atomic_set(v,i)         (((v)->counter) = (i))

static inline void atomic_inc(atomic_t *v)
{
    v->counter++;
}

static inline int atomic_dec_and_test(atomic_t *v)
{
    v->counter--;
    return v->counter != 0;
}
#endif


#if !defined(smp_mb__before_clear_bit)

#if defined(__powerpc__) && !defined(__powerpc64__)

static __inline__ void set_bit(int nr, volatile void * addr)
{
	unsigned long old;
	unsigned long mask = 1 << (nr & 0x1f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 5);

	__asm__ __volatile__("\n\
1:	lwarx   %0,0,%3 \n\
        or      %0,%0,%2 \n\
	stwcx.  %0,0,%3 \n\
	bne-    1b"
	: "=&r" (old), "=m" (*p)
	: "r" (mask), "r" (p), "m" (*p)
	: "cc" );
}

static __inline__ void clear_bit(int nr, volatile void *addr)
{
	unsigned long old;
	unsigned long mask = 1 << (nr & 0x1f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 5);

	__asm__ __volatile__("\n\
1:	lwarx   %0,0,%3 \n\
	andc    %0,%0,%2 \n\
	stwcx.  %0,0,%3 \n\
	bne-    1b"
	: "=&r" (old), "=m" (*p)
	: "r" (mask), "r" (p), "m" (*p)
	: "cc");
}

static __inline__ int __test_and_clear_bit(int nr, volatile void *addr)
{
	unsigned long mask = 1 << (nr & 0x1f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 5);
	unsigned long old = *p;

	*p = old & ~mask;
	return (old & mask) != 0;
}

static __inline__ int test_bit(int nr, __const__ volatile void *addr)
{
	__const__ unsigned int *p = (__const__ unsigned int *) addr;

	return ((p[nr >> 5] >> (nr & 0x1f)) & 1) != 0;
}

static __inline__ int __ilog2(unsigned int x)
{
	int lz;

	asm ("cntlzw %0,%1" : "=r" (lz) : "r" (x));
	return 31 - lz;
}

static __inline__ int ffz(unsigned int x)
{
	if ((x = ~x) == 0)
		return 32;
	return __ilog2(x & -x);
}

static __inline__ unsigned long find_next_zero_bit(void * addr,
	unsigned long size, unsigned long offset)
{
	unsigned int * p = ((unsigned int *) addr) + (offset >> 5);
	unsigned int result = offset & ~31UL;
	unsigned int tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 31UL;
	if (offset) {
		tmp = *p++;
		tmp |= ~0UL >> (32-offset);
		if (size < 32)
			goto found_first;
		if (tmp != ~0U)
			goto found_middle;
		size -= 32;
		result += 32;
	}
	while (size >= 32) {
		if ((tmp = *p++) != ~0U)
			goto found_middle;
		result += 32;
		size -= 32;
	}
	if (!size)
		return result;
	tmp = *p;
found_first:
	tmp |= ~0UL << size;
	if (tmp == ~0UL)        /* Are any bits zero? */
		return result + size; /* Nope. */
found_middle:
	return result + ffz(tmp);
}

#else /* !ppc32 */
#error "Your platform doesn't provide the functions required in <asm/bitopts.h>"
#endif

#endif /* !smp_mb__before_clear_bit */


/* include all the rest from ocfs.h */
#include <ocfsbool.h>
#include <ocfscom.h>
#include <ocfsconst.h>
#include <ocfshash.h>
#include <ocfstrace.h>
#include <ocfsvol.h>
#include <ocfsdisk.h>
#include <ocfsdef.h>
#include <ocfstrans.h>
#include <ocfsdlm.h>
#include <ocfsver.h>
#include <ocfsiosup.h>
#include <ocfsport.h>
#include <ocfsgenmisc.h>
#include <ocfsgenalloc.h>
#include <ocfsgencreate.h>
#include <ocfsgendirnode.h>
#include <ocfsgendlm.h>
#include <ocfsheartbeat.h>
#include <ocfsgennm.h>
#include <ocfsgensysfile.h>
#include <ocfsgentrans.h>
#include <ocfsgenutil.h>
#include <ocfsgenvolcfg.h>
#include <ocfsgenvote.h>
#include <ocfsbitmap.h>
#include <ocfsdlmp.h>
#include <ocfsfile.h>
#include <ocfsioctl.h>
//#include <ocfsipc.h>
#include <ocfsmain.h>
#include <ocfsmount.h>
#include <ocfsproc.h>


/* Error reporting convenience function */
#define LOG_ERROR(fmt, arg...)						\
	do {								\
		fprintf(stderr, "ERROR: ");				\
		fprintf(stderr, fmt, ## arg);				\
		fprintf(stderr, ", %s, %d\n", __FILE__, __LINE__);	\
		fflush(stderr);						\
	} while (0)


/* prototypes for fake functions in libocfs.c */
void complete(struct completion *c);
void init_waitqueue_head(wait_queue_head_t *q);
void init_MUTEX (struct semaphore *sem);
void truncate_inode_pages(struct address_space *as, loff_t off);
unsigned int kdev_t_to_nr(kdev_t dev);
void init_special_inode(struct inode *inode, umode_t mode, int x);
int fsync_inode_buffers(struct inode *inode);
void d_prune_aliases(struct inode *inode);
void get_random_bytes(void *buf, int nbytes);
char *ocfs_strerror(int errnum) ;
ocfs_super *get_fake_vcb(int fd, ocfs_vol_disk_hdr * hdr, int nodenum);


#endif
