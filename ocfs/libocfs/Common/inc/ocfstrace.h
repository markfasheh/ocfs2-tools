/*
 * ocfstrace.h
 *
 * Trace related macros
 *
 * Copyright (C) 2002, 2003 Oracle.  All rights reserved.
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
 * Authors: Kurt Hackel, Sunil Mushran, Manish Singh, Wim Coekaerts
 */

#ifndef  _OCFSTRACE_H_
#define  _OCFSTRACE_H_

extern __u32 debug_context;
extern __u32 debug_level;
extern __u32 debug_exclude;

#define HI(val)            ((unsigned long)(((val) >> 16) >> 16))
#define LO(val)            ((unsigned long)((val) & 0x00000000FFFFFFFFUL))

#define HILO(val)	   HI(val), LO(val)

/* Tracing Levels */
#define OCFS_DEBUG_LEVEL_ERROR         0x00000001
#define OCFS_DEBUG_LEVEL_TRACE         0x00000002

#define OCFS_DEBUG_LEVEL_ENTRY         0x00000010
#define OCFS_DEBUG_LEVEL_EXIT          0x00000020

#define OCFS_DEBUG_LEVEL_TIMING        0x00000100
#define OCFS_DEBUG_LEVEL_STACK         0x00000200

#define OCFS_DEBUG_LEVEL_LOCKRES       0x00001000	/* get/put lockres */
#define OCFS_DEBUG_LEVEL_MALLOC        0x00002000	/* malloc/free lockres */

/* Tracing Contexts */
#define OCFS_DEBUG_CONTEXT_INIT        0x00000001	/* ocfsgeninit.c,ocfsmain.c */
#define OCFS_DEBUG_CONTEXT_MEM         0x00000002	/* ocfs_memcheck() in ocfsmain.c */

#define OCFS_DEBUG_CONTEXT_NM          0x00000010	/* ocfsgennm.c */
#define OCFS_DEBUG_CONTEXT_DLM         0x00000020	/* ocfsgendlm.c */
#define OCFS_DEBUG_CONTEXT_VOTE        0x00000040	/* ocfsgenvote.c */
#define OCFS_DEBUG_CONTEXT_IPC         0x00000080	/* ocfsipc.c */

#define OCFS_DEBUG_CONTEXT_VOLCFG      0x00000100	/* ocfsgenvolcfg.c */
#define OCFS_DEBUG_CONTEXT_HEARTBEAT   0x00000200	/* ocfsgenheartbeat.c */

#define OCFS_DEBUG_CONTEXT_MOUNT       0x00001000	/* ocfsmount.c */
#define OCFS_DEBUG_CONTEXT_SHUTDOWN    0x00002000	/* ocfsgenshutdn.c */
#define OCFS_DEBUG_CONTEXT_CREATE      0x00004000	/* gencreate.c, create.c ?? */
#define OCFS_DEBUG_CONTEXT_CLOSE       0x00008000	/* genclose.c, ocfsclose.c */

#define OCFS_DEBUG_CONTEXT_EXTENT      0x00010000	/* ocfsgenalloc.c */
#define OCFS_DEBUG_CONTEXT_DIRINFO     0x00020000	/* ocfsgendirnode.c */
#define OCFS_DEBUG_CONTEXT_FILEINFO    0x00040000	/* ocfsfile.c */
#define OCFS_DEBUG_CONTEXT_TRANS       0x00080000	/* ocfsgentrans.c */

#define OCFS_DEBUG_CONTEXT_DISKIO      0x00100000	/* ocfsgenio.c */
#define OCFS_DEBUG_CONTEXT_MISC        0x00200000	/* ocfsgenmisc.c */

#define OCFS_DEBUG_CONTEXT_UTIL        0x01000000	/* ocfsgenutil.c */
#define OCFS_DEBUG_CONTEXT_HASH        0x02000000	/* ocfshash.h */
#define OCFS_DEBUG_CONTEXT_PORT        0x08000000	/* ocfsport.c */

#define OCFS_DEBUG_CONTEXT_IOCTL       0x10000000	/* ocfsioctl.c */
#define OCFS_DEBUG_CONTEXT_PROC        0x20000000	/* ocfsproc.c */
#define OCFS_DEBUG_CONTEXT_IOSUP       0x40000000	/* ocfsiosup.c */


#ifndef OCFS_DBG_TIMING
# define DECL_U8_ARRAY(__t, __s)
# define INIT_U8_ARRAY(__s)
# define PRINT_STRING(__t)		printk("\n");
# define PRINT_ENTRY(__t)	\
		printk("(%d) ENTRY: %s", ocfs_getpid (), __FUNCTION__)
#else
# define DECL_U8_ARRAY(__t, __s)		__u8 (__t)[(__s)]
# define INIT_U8_ARRAY(__s)		*(__s) = '\0'
# define PRINT_STRING(__t)		printk("%s\n", (__t))
# define PRINT_ENTRY(__t)	\
		printk("(%d) %sENTRY: %s", ocfs_getpid (), (__t), __FUNCTION__)
#endif

#ifndef OCFS_DBG_TIMING
# define GET_STACK(s)
#else
# define GET_STACK(s)							\
	IF_LEVEL(OCFS_DEBUG_LEVEL_STACK) {				\
		__s32 esp;						\
		__asm__ __volatile__("andl %%esp,%0" : "=r" (esp) : 	\
				     "0" (8191));			\
		esp -= sizeof(struct task_struct);			\
		sprintf((s), "[%ld] ", esp);				\
	}
#endif

/* privately used macros */
#define IF_LEVEL(level)	\
	if ((debug_context & OCFS_DEBUG_CONTEXT) && (debug_level & level) && \
	    ocfs_getpid()!=debug_exclude)
				

#ifndef OCFS_DBG_TIMING
# define ENTRY_TIMING_DECLS
# define GET_TIMING(s, hi, lo)
#else
# define ENTRY_TIMING_DECLS	__u32 _HI = 0, _LO = 0
# define GET_TIMING(s, hi, lo)					\
	do {							\
		IF_LEVEL(OCFS_DEBUG_LEVEL_TIMING) {		\
			__u32 _lo, _hi;				\
			rdtsc (_lo, _hi);			\
			if ((s) == NULL) {			\
				(hi) = _hi; (lo) = _lo;		\
			} else {				\
				__u64 _b, _e;			\
				_b = hi; _b <<= 32; _b |= lo;	\
				_e = _hi; _e <<= 32; _e |= _lo;	\
				_e -= _b; 			\
				sprintf((s), " => [%u.%u]",	\
					HI(_e), LO(_e));	\
			}					\
		}						\
	} while (0)
#endif

/* IF macro */
#define IF_TRACE(func)						\
	do {							\
		if ((debug_context & OCFS_DEBUG_CONTEXT) &&	\
		    (debug_level & OCFS_DEBUG_LEVEL_TRACE))	\
			func;					\
	} while (0)

/* TRACE disabled. ERROR macros are never disabled. */

static inline void eat_value_int(int val)
{
	return;
}

static inline void eat_value_long(long val)
{
	return;
}

static inline void eat_value_ulong(unsigned long val)
{
	return;
}

static inline void eat_value_ptr(void *val)
{
	return;
}

#if !defined(TRACE)
# define  LOG_ENTRY()
# define  LOG_EXIT()
# define  LOG_EXIT_STATUS(val)			eat_value_int(val)
# define  LOG_EXIT_LONG(val)			eat_value_long(val)
# define  LOG_EXIT_ULONG(val)			eat_value_ulong(val)
# define  LOG_EXIT_PTR(val)			eat_value_ptr(val)
# define  LOG_TRACE_STR(str)
# define  LOG_TRACE_STATUS(val)			eat_value_int(val)
# define  LOG_ENTRY_ARGS(fmt, arg...)
# define  LOG_EXIT_ARGS(fmt, arg...)
# define  LOG_TRACE_ARGS(fmt, arg...)
#endif				/* !defined(TRACE) */

/* TRACE enabled */
#if defined(TRACE)

/* ENTRY macros */
/* LOG_ENTRY_ARGS()
 *
 * Note: The macro expects the args to be terminated by a newline.
 */
#define LOG_ENTRY_ARGS(fmt, arg...)					\
	ENTRY_TIMING_DECLS;						\
	do {								\
		DECL_U8_ARRAY(_t, 16);					\
		INIT_U8_ARRAY(_t);					\
		GET_STACK(_t);						\
		GET_TIMING(NULL, _HI, _LO);				\
		IF_LEVEL(OCFS_DEBUG_LEVEL_ENTRY) {			\
			PRINT_ENTRY(_t);				\
			if (fmt==NULL)					\
				printk("() \n");			\
			else						\
				printk(fmt, ##arg);			\
		}							\
	} while (0)

#define LOG_ENTRY()            LOG_ENTRY_ARGS(NULL)



/* EXIT macros */
/* LOG_EXIT_ARGS()
 *
 */
#define LOG_EXIT_ARGS(fmt, arg...)					\
	do {								\
		IF_LEVEL(OCFS_DEBUG_LEVEL_EXIT) {			\
			DECL_U8_ARRAY(_t, 50);				\
			INIT_U8_ARRAY(_t);				\
			GET_TIMING(_t, _HI, _LO);			\
			printk("(%d) EXIT : %s() %s",			\
			       ocfs_getpid (), __FUNCTION__, 		\
			       (fmt==NULL ? "" : "= "));		\
			if (fmt!=NULL)					\
				printk(fmt, ## arg);			\
			PRINT_STRING(_t);				\
		}							\
	}  while (0)

#define LOG_EXIT()             LOG_EXIT_ARGS(NULL)
#define LOG_EXIT_STATUS(val)   LOG_EXIT_ARGS("%d ", val)
#define LOG_EXIT_LONG(val)     LOG_EXIT_ARGS("%d ", val)
#define LOG_EXIT_ULONG(val)    LOG_EXIT_ARGS("%u ", val)
#define LOG_EXIT_PTR(val)      LOG_EXIT_ARGS("0x%p ", val)


/* TRACE macros */
/* LOG_TRACE_ARGS()
 *
 * Note: The macro expects the args to be terminated by a newline.
 */
#define LOG_TRACE_ARGS(fmt, arg...)					\
	do {								\
		IF_LEVEL(OCFS_DEBUG_LEVEL_TRACE) {			\
			printk("(%d) TRACE: %s(%d) ", ocfs_getpid (),	\
			       __FUNCTION__, __LINE__);			\
			printk(fmt, ## arg);				\
		}							\
	} while (0)

#define LOG_TRACE_STR(str)     LOG_TRACE_ARGS("%s\n", str)
#define LOG_TRACE_STATUS(val)  LOG_TRACE_ARGS("%d\n", val);

#endif				/* TRACE */



/* ERROR macros are not compiled out */
/* LOG_ERROR_ARGS()
 *
 * Note: The macro expects the args to be terminated by a newline.
 */
#define LOG_ERROR_ARGS(fmt, arg...)					\
	do {								\
		printk(KERN_ERR "(%d) ERROR: ", ocfs_getpid ());        \
		printk(fmt, ## arg);					\
		printk(", %s, %d\n", __FILE__, __LINE__);		\
	} while (0)

#define LOG_ERROR_STR(str)     LOG_ERROR_ARGS("%s", str)
#define LOG_ERROR_STATUS(st)   LOG_ERROR_ARGS("status = %d", st)

#endif				/* _OCFSTRACE_H_ */
