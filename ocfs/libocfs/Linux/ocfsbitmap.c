/*
 * ocfsbitmap.c
 *
 * Bitmap infrastructure code
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

#if defined(__KERNEL__)
#include  <ocfs.h>
#else
#include  <libocfs.h>
#endif

/* Tracing */
#define OCFS_DEBUG_CONTEXT  OCFS_DEBUG_CONTEXT_PORT

#define BITCOUNT(x)     (((BX_(x)+(BX_(x)>>4)) & 0x0F0F0F0F) % 255)
#define BX_(x)          ((x) - (((x)>>1)&0x77777777) \
		             - (((x)>>2)&0x33333333) \
			     - (((x)>>3)&0x11111111))

/*
 * ocfs_initialize_bitmap()
 *
 */
void ocfs_initialize_bitmap (ocfs_alloc_bm * bitmap, void *buf, __u32 sz)
{
	LOG_ENTRY ();

	bitmap->buf = buf;
	bitmap->size = sz;
	bitmap->failed = 0;
	bitmap->ok_retries = 0;

	LOG_EXIT ();
	return;
}				/* ocfs_initialize_bitmap */

/*
 * ocfs_find_clear_bits()
 *
 * sysonly is passed # bits in bitmap that are rserved for system file space
 * in case we have a disk full.
 */
int ocfs_find_clear_bits (ocfs_alloc_bm * bitmap, __u32 numBits, __u32 offset, __u32 sysonly)
{
	__u32 next_zero, off, count, size, first_zero = -1; 
	void *buffer;

	LOG_ENTRY ();

	buffer = bitmap->buf;
	size = bitmap->size - sysonly;
	count = 0;
	off = offset;

	while ((size - off + count >= numBits) &&
	       (next_zero = find_next_zero_bit (buffer, size, off)) != size) {
                if (next_zero >= bitmap->size - sysonly)
                    break;

		if (next_zero != off) {
			first_zero = next_zero;
			off = next_zero + 1;
			count = 0;
		} else {
			off++;
			if (count == 0)
				first_zero = next_zero;
		}

		count++;

		if (count == numBits)
			goto bail;
	}
	first_zero = -1;

      bail:
	if (first_zero != -1 && first_zero > bitmap->size) {
		LOG_ERROR_ARGS("um... first_zero>bitmap->size (%d > %d)",
			       first_zero, bitmap->size);
		first_zero = -1;
	}
	LOG_EXIT_LONG (first_zero);
	return first_zero;
}				/* ocfs_find_clear_bits */

/*
 * ocfs_count_bits()
 *
 */
int ocfs_count_bits (ocfs_alloc_bm * bitmap)
{
	__u32 size, count = 0, off = 0;
	unsigned char tmp;
	__u8 *buffer;

	LOG_ENTRY ();

	buffer = bitmap->buf;

	size = (bitmap->size >> 3);

	while (off < size) {
		memcpy (&tmp, buffer, 1);
		count += BITCOUNT (tmp);
		off++;
		buffer++;
	}

	LOG_EXIT_ULONG (count);
	return count;
}				/* ocfs_count_bits */

/*
 * ocfs_set_bits()
 *
 */
void ocfs_set_bits (ocfs_alloc_bm * bitmap, __u32 start, __u32 num)
{
	LOG_ENTRY ();

	while (num--)
		set_bit (start++, bitmap->buf);

	LOG_EXIT ();
	return;
}				/* ocfs_set_bits */

/*
 * ocfs_clear_bits()
 *
 */
void ocfs_clear_bits (ocfs_alloc_bm * bitmap, __u32 start, __u32 num)
{
	LOG_ENTRY ();

	while (num--)
		clear_bit (start++, bitmap->buf);

	LOG_EXIT ();
	return;
}				/* ocfs_clear_bits */
