/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * bitops.h
 *
 * Bitmap frobbing routines for the OCFS2 userspace library.  These
 * are the inlined versions.
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License, version 2,  as published by the Free Software Foundation.
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
 * Authors: Joel Becker
 *
 *  This code is a port of e2fsprogs/lib/ext2fs/bitops.h
 *  Copyright (C) 1993, 1994, 1995, 1996 Theodore Ts'o.
 *
 *  i386 bitops operations taken from <asm/bitops.h>, Copyright 1992,
 *  Linus Torvalds.
 */



/*
 * OCFS2 bitmap manipulation routines.
 */

#if ((defined __GNUC__) && \
     (defined(__i386__) || defined(__i486__) || defined(__i586__)))

#define _OCFS2_HAVE_ASM_BITOPS_

/*
 * These are done by inline assembly for speed reasons.....
 *
 * All bitoperations return 0 if the bit was cleared before the
 * operation and != 0 if it was not.  Bit 0 is the LSB of addr; bit 32
 * is the LSB of (addr+1).
 */

/*
 * Some hacks to defeat gcc over-optimizations..
 */
struct __dummy_h { unsigned long a[100]; };
#define OCFS2_ADDR (*(struct __dummy_h *) addr)
#define OCFS2_CONST_ADDR (*(const struct __dummy_h *) addr)	

static inline int ocfs2_set_bit(int nr, void * addr)
{
	int oldbit;

	__asm__ __volatile__("btsl %2,%1\n\tsbbl %0,%0"
		:"=r" (oldbit),"=m" (OCFS2_ADDR)
		:"r" (nr));
	return oldbit;
}

static inline int ocfs2_clear_bit(int nr, void * addr)
{
	int oldbit;

	__asm__ __volatile__("btrl %2,%1\n\tsbbl %0,%0"
		:"=r" (oldbit),"=m" (OCFS2_ADDR)
		:"r" (nr));
	return oldbit;
}

static inline int ocfs2_test_bit(int nr, const void * addr)
{
	int oldbit;

	__asm__ __volatile__("btl %2,%1\n\tsbbl %0,%0"
		:"=r" (oldbit)
		:"m" (OCFS2_CONST_ADDR),"r" (nr));
	return oldbit;
}

#if 0
#define _OCFS2_HAVE_ASM_FINDBIT_

_INLINE_ int ext2fs_find_first_bit_set(void * addr, unsigned size)
{
	int d0, d1, d2;
	int res;

	if (!size)
		return 0;
	/* This looks at memory. Mark it volatile to tell gcc not to move it around */
	__asm__ __volatile__(
		"cld\n\t"			     
		"xorl %%eax,%%eax\n\t"
		"xorl %%edx,%%edx\n\t"
		"repe; scasl\n\t"
		"je 1f\n\t"
		"movl -4(%%edi),%%eax\n\t"
		"subl $4,%%edi\n\t"
		"bsfl %%eax,%%edx\n"
		"1:\tsubl %%esi,%%edi\n\t"
		"shll $3,%%edi\n\t"
		"addl %%edi,%%edx"
		:"=d" (res), "=&c" (d0), "=&D" (d1), "=&a" (d2)
		:"1" ((size + 31) >> 5), "2" (addr), "S" (addr));
	return res;
}

_INLINE_ int ext2fs_find_next_bit_set (void * addr, int size, int offset)
{
	unsigned long * p = ((unsigned long *) addr) + (offset >> 5);
	int set = 0, bit = offset & 31, res;
	
	if (bit) {
		/*
		 * Look for zero in first byte
		 */
		__asm__("bsfl %1,%0\n\t"
			"jne 1f\n\t"
			"movl $32, %0\n"
			"1:"
			: "=r" (set)
			: "r" (*p >> bit));
		if (set < (32 - bit))
			return set + offset;
		set = 32 - bit;
		p++;
	}
	/*
	 * No bit found yet, search remaining full bytes for a bit
	 */
	res = ext2fs_find_first_bit_set(p, size - 32 * (p - (unsigned long *) addr));
	return (offset + set + res);
}
#endif

#undef OCFS2_ADDR

#endif	/* i386 */

#ifdef __mc68000__

#define _OCFS2_HAVE_ASM_BITOPS_

static inline int ocfs2_set_bit(int nr,void * addr)
{
	char retval;

	__asm__ __volatile__ ("bfset %2@{%1:#1}; sne %0"
	     : "=d" (retval) : "d" (nr^7), "a" (addr));

	return retval;
}

static inline int ocfs2_clear_bit(int nr, void * addr)
{
	char retval;

	__asm__ __volatile__ ("bfclr %2@{%1:#1}; sne %0"
	     : "=d" (retval) : "d" (nr^7), "a" (addr));

	return retval;
}

static inline int ocfs2_test_bit(int nr, const void * addr)
{
	char retval;

	__asm__ __volatile__ ("bftst %2@{%1:#1}; sne %0"
	     : "=d" (retval) : "d" (nr^7), "a" (addr));

	return retval;
}

#endif /* __mc68000__ */

#ifdef __sparc__

#define _OCFS2_HAVE_ASM_BITOPS_

/*
 * Do the bitops so that we are compatible with the standard i386
 * convention.
 */

_INLINE_ int ext2fs_set_bit(int nr,void * addr)
{
#if 1
	int		mask;
	unsigned char	*ADDR = (unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	__asm__ __volatile__("ldub	[%0], %%g6\n\t"
			     "or	%%g6, %2, %%g5\n\t"
			     "stb	%%g5, [%0]\n\t"
			     "and	%%g6, %2, %0\n"
	: "=&r" (ADDR)
	: "0" (ADDR), "r" (mask)
	: "g5", "g6");
	return (int) ADDR;
#else
	int		mask, retval;
	unsigned char	*ADDR = (unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	retval = (mask & *ADDR) != 0;
	*ADDR |= mask;
	return retval;
#endif
}

_INLINE_ int ext2fs_clear_bit(int nr, void * addr)
{
#if 1
	int		mask;
	unsigned char	*ADDR = (unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	__asm__ __volatile__("ldub	[%0], %%g6\n\t"
			     "andn	%%g6, %2, %%g5\n\t"
			     "stb	%%g5, [%0]\n\t"
			     "and	%%g6, %2, %0\n"
	: "=&r" (ADDR)
	: "0" (ADDR), "r" (mask)
	: "g5", "g6");
	return (int) ADDR;
	
#else
	int		mask, retval;
	unsigned char	*ADDR = (unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	retval = (mask & *ADDR) != 0;
	*ADDR &= ~mask;
	return retval;
#endif
}

_INLINE_ int ext2fs_test_bit(int nr, const void * addr)
{
	int			mask;
	const unsigned char	*ADDR = (const unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	return ((mask & *ADDR) != 0);
}

#endif /* __sparc__ */

#if !defined(_OCFS2_HAVE_ASM_FINDBIT_)
#include <strings.h>

static inline int ocfs2_find_first_bit_set(void * addr, unsigned size)
{
	char	*cp = (unsigned char *) addr;
	int 	res = 0, d0;

	if (!size)
		return 0;

	while ((size > res) && (*cp == 0)) {
		cp++;
		res += 8;
	}
	d0 = ffs(*cp);
	if (d0 == 0)
		return size;
	
	return res + d0 - 1;
}

static inline int ocfs2_find_next_bit_set (void * addr, int size, int offset)
{
	unsigned char * p;
	int set = 0, bit = offset & 7, res = 0, d0;
	
	res = offset >> 3;
	p = ((unsigned char *) addr) + res;
	
	if (bit) {
		set = ffs(*p & ~((1 << bit) - 1));
		if (set)
			return (offset & ~7) + set - 1;
		p++;
		res += 8;
	}
	while ((size > res) && (*p == 0)) {
		p++;
		res += 8;
	}
	d0 = ffs(*p);
	if (d0 == 0)
		return size;

	return (res + d0 - 1);
}
#endif	

#ifndef _OCFS2_HAVE_ASM_BITOPS_
extern int ocfs2_set_bit(int nr,void * addr);
extern int ocfs2_clear_bit(int nr, void * addr);
extern int ext2fs_test_bit(int nr, const void * addr);
#endif


