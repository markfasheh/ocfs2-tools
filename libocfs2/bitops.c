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
 * Authors: Joel Becker
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

int ocfs2_test_bit(int nr, const void * addr)
{
	int			mask;
	const unsigned char	*ADDR = (const unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	return ((mask & *ADDR) != 0);
}

#endif	/* !_OCFS2_HAVE_ASM_BITOPS_ */

#if !defined(_OCFS2_HAVE_ASM_FINDBIT_)
#include <strings.h>

int ocfs2_find_first_bit_set(void *addr, int size)
{
	unsigned char	*cp = (unsigned char *) addr;
	unsigned int 	res = 0;
	int		d0;
	unsigned char	tilde = ~0;
	unsigned int	mask = 0U | tilde;

	if (!size)
		return 0;

	while ((size > res) && (*cp == 0)) {
		cp++;
		res += 8;
	}
	if (res >= size)
		return size;
	if ((res + 8) > size)
		mask >>= 8 - (size - res);
	d0 = ffs(*cp & mask);
	if (d0 == 0)
		return size;
	
	return res + d0 - 1;
}

int ocfs2_find_first_bit_clear(void *addr, int size)
{
	unsigned char	*cp = (unsigned char *) addr;
	unsigned int	res = 0;
	int		d0;
	unsigned char	tilde = ~0;
	unsigned int	mask = 0U | tilde;

	if (!size)
		return 0;

	while ((size > res) && (*cp == tilde)) {
		cp++;
		res += 8;
	}
	if (res >= size)
		return size;
	if ((res + 8) > size)
		mask >>= 8 - (size - res);
	d0 = ffs(~(*cp & mask));
	if (d0 == 0)
		return size;
	
	return res + d0 - 1;
}

int ocfs2_find_next_bit_set(void *addr, int size, int offset)
{
	unsigned char * p;
	int set = 0, d0;
	unsigned int	bit = offset & 7, res = 0;
	unsigned char	tilde = ~0;
	unsigned int	mask = 0U | tilde;
	
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

int ocfs2_find_next_bit_clear(void *addr, int size, int offset)
{
	unsigned char * p;
	int set = 0, d0;
	unsigned int	bit = offset & 7, res = 0;
	unsigned char tilde = ~0;
	unsigned int	mask = 0U | tilde;
	
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
#endif	


#ifdef DEBUG_EXE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
	char bitmap[8 * sizeof(unsigned long)];
	int ret;
	int size = sizeof(bitmap) * 8;

	/* Test an arbitrary size (not byte bounded) */
	memset(bitmap, 0, sizeof(bitmap));
	ocfs2_set_bit(size - 1, bitmap);
	ret = ocfs2_find_first_bit_set(bitmap, size - 3);
	fprintf(stdout, "Pass1: first set %d (%s)\n", ret,
		(ret == (size - 3)) ? "correct" : "incorrect");

	memset(bitmap, 0xFF, sizeof(bitmap));
	ocfs2_clear_bit(size - 1, bitmap);
	ret = ocfs2_find_first_bit_clear(bitmap, size - 3);
	fprintf(stdout, "Pass1: first clear %d (%s)\n", ret,
		(ret == (size - 3)) ? "correct" : "incorrect");

	/* XXX add more tests */
	return 0;
}
#endif

