/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * bitops.c
 *
 * Bitmap frobbing code for the OCFS2 userspace library.  See bitops.h
 * for inlined versions.
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
 *  This code is a port of e2fsprogs/lib/ext2fs/bitops.c
 *  Copyright (C) 1993, 1994, 1995, 1996 Theodore Ts'o.
 */

#include <stdio.h>
#include <sys/types.h>

#include "bitops.h"

#ifndef _OCFS2_HAVE_ASM_BITOPS_

/*
 * For the benefit of those who are trying to port Linux to another
 * architecture, here are some C-language equivalents.  You should
 * recode these in the native assmebly language, if at all possible.
 *
 * C language equivalents written by Theodore Ts'o, 9/26/92.
 * Modified by Pete A. Zaitcev 7/14/95 to be portable to big endian
 * systems, as well as non-32 bit systems.
 */

#ifdef __powerpc__
int ocfs2_set_bit(int nr,void * addr)
{
	unsigned long mask = 1 << (nr & 0x1f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 5);
	unsigned long old = *p;

	*p = old | mask;
	return (old & mask) != 0;
}
#else
int ocfs2_set_bit(int nr,void * addr)
{
	int		mask, retval;
	unsigned char	*ADDR = (unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	retval = (mask & *ADDR) != 0;
	*ADDR |= mask;
	return retval;
}
#endif

#ifdef __powerpc__
int ocfs2_clear_bit(int nr, void * addr)
{
	unsigned long mask = 1 << (nr & 0x1f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 5);
	unsigned long old = *p;

	*p = old & ~mask;
	return (old & mask) != 0;
}
#else
int ocfs2_clear_bit(int nr, void * addr)
{
	int		mask, retval;
	unsigned char	*ADDR = (unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	retval = (mask & *ADDR) != 0;
	*ADDR &= ~mask;
	return retval;
}
#endif

#ifdef __powerpc__
int ocfs2_test_bit(int nr, const void * addr)
{
	const unsigned int *p = (const unsigned int *) addr;

	return ((p[nr >> 5] >> (nr & 0x1f)) & 1) != 0;
}
#else
int ocfs2_test_bit(int nr, const void * addr)
{
	int			mask;
	const unsigned char	*ADDR = (const unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	return ((mask & *ADDR) != 0);
}
#endif

#endif	/* !_OCFS2_HAVE_ASM_BITOPS_ */

#if !defined(_OCFS2_HAVE_ASM_FINDBIT_)
#include <strings.h>

#ifdef __powerpc__
static inline int __ilog2(unsigned long x)
{
        int lz;
                                                                                                                                                         
        asm ("cntlzw %0,%1" : "=r" (lz) : "r" (x));
        return 31 - lz;
}
                                                                                                                                                         
static inline int ffz(unsigned int x)
{
        if ((x = ~x) == 0)
                return 32;
        return __ilog2(x & -x);
}
                                                                                                                                                         
static inline int __ffs(unsigned long x)
{
        return __ilog2(x & -x);
}
#endif	/* __powerpc__ */

int ocfs2_find_first_bit_set(void *addr, int size)
{
	return ocfs2_find_next_bit_set(addr, size, 0);
}

int ocfs2_find_first_bit_clear(void *addr, int size)
{
	return ocfs2_find_next_bit_clear(addr, size, 0);
}

#ifdef __powerpc__
int ocfs2_find_next_bit_set(void *addr, int size, int offset)
{
	unsigned int *p = ((unsigned int *) addr) + (offset >> 5);
	unsigned int result = offset & ~31UL;
	unsigned int tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 31UL;
	if (offset) {
		tmp = *p++;
		tmp &= ~0UL << offset;
		if (size < 32)
			goto found_first;
		if (tmp)
			goto found_middle;
		size -= 32;
		result += 32;
	}
	while (size >= 32) {
		if ((tmp = *p++) != 0)
			goto found_middle;
		result += 32;
		size -= 32;
	}
	if (!size)
		return result;
	tmp = *p;

found_first:
	tmp &= ~0UL >> (32 - size);
	if (tmp == 0UL)        /* Are any bits set? */
		return result + size; /* Nope. */
found_middle:
	return result + __ffs(tmp);
}
#else
int ocfs2_find_next_bit_set(void *addr, int size, int offset)
{
	unsigned char * p;
	int set = 0, d0;
	unsigned int	bit = offset & 7, res = 0;
	unsigned char	tilde = ~0;
	unsigned int	mask = 0U | tilde;

	/* XXX care to check for null ADDR and <= 0 for int args? */
	if (size == 0)
		return 0;
	
	res = offset >> 3;
	p = ((unsigned char *) addr) + res;
	res <<= 3;
	
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
	if (res >= size)
		return size;
	if ((res + 8) > size)
		mask >>= 8 - (size - res);
	d0 = ffs(*p & mask);
	if (d0 == 0)
		return size;

	return (res + d0 - 1);
}
#endif	/* __powerpc__ */

#ifdef __powerpc__
int ocfs2_find_next_bit_clear(void *addr, int size, int offset)
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
#else
int ocfs2_find_next_bit_clear(void *addr, int size, int offset)
{
	unsigned char * p;
	int set = 0, d0;
	unsigned int	bit = offset & 7, res = 0;
	unsigned char tilde = ~0;
	unsigned int	mask = 0U | tilde;

	if (size == 0)
		return 0;
	
	res = offset >> 3;
	p = ((unsigned char *) addr) + res;
	res <<= 3;
	
	if (bit) {
		set = ffs(~*p & ~((1 << bit) - 1) & mask);
		if (set)
			return (offset & ~7) + set - 1;
		p++;
		res += 8;
	}
	while ((size > res) && (*p == tilde)) {
		p++;
		res += 8;
	}
	if (res >= size)
		return size;
	if ((res + 8) > size)
		mask >>= 8 - (size - res);
	d0 = ffs(~(*p & mask));
	if (d0 == 0)
		return size;

	return (res + d0 - 1);
}
#endif	/* __powerpc__ */

#endif	


#ifdef DEBUG_EXE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define bit_expect(expect, which, args...) do {				\
	int _ret = ocfs2_find_##which(bitmap, args);			\
	fprintf(stdout, #which "(" #args ") = %d (expected %d: %s)\n",	\
			_ret, expect, 					\
			_ret == expect ? "correct" : "_incorrect_");	\
} while (0)

int main(int argc, char *argv[])
{
	char bitmap[8 * sizeof(unsigned long)];
	int size = sizeof(bitmap) * 8;

	/* Test an arbitrary size (not byte bounded) */
	memset(bitmap, 0, sizeof(bitmap));
	ocfs2_set_bit(size - 1, bitmap);

	bit_expect(size - 3, first_bit_set, size - 3);
	bit_expect(size - 1, first_bit_set, size);
	bit_expect(size - 1, next_bit_set, size, size - 1);
	bit_expect(size, next_bit_clear, size, size - 1);

	memset(bitmap, 0xFF, sizeof(bitmap));
	ocfs2_clear_bit(size - 1, bitmap);

	bit_expect(size - 3, first_bit_clear, size - 3);
	bit_expect(size - 1, first_bit_clear, size);
	bit_expect(size - 1, next_bit_clear, size, size - 1);
	bit_expect(size, next_bit_set, size, size - 1);

	/* XXX add more tests */
	return 0;
}
#endif

