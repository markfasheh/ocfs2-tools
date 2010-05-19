/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * blockcheck.c
 *
 * Checksum and ECC codes for the OCFS2 userspace library.
 *
 * Copyright (C) 2006, 2008 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License, version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 *   The 802.3 CRC32 algorithm is copied from the Linux kernel, lib/crc32.c.
 *   Code was from the public domain, is now GPL, so no real copyright
 *   attribution other than "The Linux Kernel".  XXX: better text, anyone?
 */

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE

#ifdef DEBUG_EXE
# define _BSD_SOURCE  /* For timersub() */
#endif

#include <inttypes.h>

#include "ocfs2/ocfs2.h"
#include "ocfs2/bitops.h"
#include "ocfs2/byteorder.h"

#include "blockcheck.h"
#include "crc32table.h"


static inline unsigned int hc_hweight32(unsigned int w)
{
	unsigned int res = (w & 0x55555555) + ((w >> 1) & 0x55555555);
	res = (res & 0x33333333) + ((res >> 2) & 0x33333333);
	res = (res & 0x0F0F0F0F) + ((res >> 4) & 0x0F0F0F0F);
	res = (res & 0x00FF00FF) + ((res >> 8) & 0x00FF00FF);
	return (res & 0x0000FFFF) + ((res >> 16) & 0x0000FFFF);
}

/*
 * Calculate the bit offset in the hamming code buffer based on the bit's
 * offset in the data buffer.  Since the hamming code reserves all
 * power-of-two bits for parity, the data bit number and the code bit
 * number are offest by all the parity bits beforehand.
 *
 * Recall that bit numbers in hamming code are 1-based.  This function
 * takes the 0-based data bit from the caller.
 *
 * An example.  Take bit 1 of the data buffer.  1 is a power of two (2^0),
 * so it's a parity bit.  2 is a power of two (2^1), so it's a parity bit.
 * 3 is not a power of two.  So bit 1 of the data buffer ends up as bit 3
 * in the code buffer.
 *
 * The caller passes in *p if it wants to keep track of the most recent
 * number of parity bits added.  This allows the function to start the
 * calculation at the last place.
 */
static unsigned int calc_code_bit(unsigned int i, unsigned int *p_cache)
{
	unsigned int b, p = 0;

	/*
	 * Data bits are 0-based, but we're talking code bits, which
	 * are 1-based.
	 */
	b = i + 1;

	/* Use the cache if it is there */
	if (p_cache)
		p = *p_cache;
        b += p;

	/*
	 * For every power of two below our bit number, bump our bit.
	 *
	 * We compare with (b + 1) because we have to compare with what b
	 * would be _if_ it were bumped up by the parity bit.  Capice?
	 *
	 * p is set above.
	 */
	for (; (1 << p) < (b + 1); p++)
		b++;

	if (p_cache)
		*p_cache = p;

	return b;
}

/*
 * This is the low level encoder function.  It can be called across
 * multiple hunks just like the crc32 code.  'd' is the number of bits
 * _in_this_hunk_.  nr is the bit offset of this hunk.  So, if you had
 * two 512B buffers, you would do it like so:
 *
 * parity = ocfs2_hamming_encode(0, buf1, 512 * 8, 0);
 * parity = ocfs2_hamming_encode(parity, buf2, 512 * 8, 512 * 8);
 *
 * If you just have one buffer, use ocfs2_hamming_encode_block().
 */
uint32_t ocfs2_hamming_encode(uint32_t parity, void *data, unsigned int d,
			      unsigned int nr)
{
	unsigned int i, b, p = 0;

	if (!d)
		abort();

	/*
	 * b is the hamming code bit number.  Hamming code specifies a
	 * 1-based array, but C uses 0-based.  So 'i' is for C, and 'b' is
	 * for the algorithm.
	 *
	 * The i++ in the for loop is so that the start offset passed
	 * to ocfs2_find_next_bit_set() is one greater than the previously
	 * found bit.
	 */
	for (i = 0; (i = ocfs2_find_next_bit_set(data, d, i)) < d; i++)
	{
		/*
		 * i is the offset in this hunk, nr + i is the total bit
		 * offset.
		 */
		b = calc_code_bit(nr + i, &p);

		/*
		 * Data bits in the resultant code are checked by
		 * parity bits that are part of the bit number
		 * representation.  Huh?
		 *
		 * <wikipedia href="http://en.wikipedia.org/wiki/Hamming_code">
		 * In other words, the parity bit at position 2^k
		 * checks bits in positions having bit k set in
		 * their binary representation.  Conversely, for
		 * instance, bit 13, i.e. 1101(2), is checked by
		 * bits 1000(2) = 8, 0100(2)=4 and 0001(2) = 1.
		 * </wikipedia>
		 *
		 * Note that 'k' is the _code_ bit number.  'b' in
		 * our loop.
		 */
		parity ^= b;
	}

	/* While the data buffer was treated as little endian, the
	 * return value is in host endian. */
	return parity;
}

uint32_t ocfs2_hamming_encode_block(void *data, unsigned int blocksize)
{
	return ocfs2_hamming_encode(0, data, blocksize * 8, 0);
}

/*
 * Like ocfs2_hamming_encode(), this can handle hunks.  nr is the bit
 * offset of the current hunk.  If bit to be fixed is not part of the
 * current hunk, this does nothing.
 *
 * If you only have one hunk, use ocfs2_hamming_fix_block().
 */
void ocfs2_hamming_fix(void *data, unsigned int d, unsigned int nr,
		       unsigned int fix)
{
	unsigned int i, b;

	if (!d)
		abort();

	/*
	 * If the bit to fix has an hweight of 1, it's a parity bit.  One
	 * busted parity bit is its own error.  Nothing to do here.
	 */
	if (hc_hweight32(fix) == 1)
		return;

	/*
	 * nr + d is the bit right past the data hunk we're looking at.
	 * If fix after that, nothing to do
	 */
	if (fix >= calc_code_bit(nr + d, NULL))
		return;

	/*
	 * nr is the offset in the data hunk we're starting at.  Let's
	 * start b at the offset in the code buffer.  See hamming_encode()
	 * for a more detailed description of 'b'.
	 */
	b = calc_code_bit(nr, NULL);
	/* If the fix is before this hunk, nothing to do */
	if (fix < b)
		return;

	for (i = 0; i < d; i++, b++)
	{
		/* Skip past parity bits */
		while (hc_hweight32(b) == 1)
			b++;

		/*
		 * i is the offset in this data hunk.
		 * nr + i is the offset in the total data buffer.
		 * b is the offset in the total code buffer.
		 *
		 * Thus, when b == fix, bit i in the current hunk needs
		 * fixing.
		 */
		if (b == fix)
		{
			if (ocfs2_test_bit(i, data))
				ocfs2_clear_bit(i, data);
			else
				ocfs2_set_bit(i, data);
			break;
		}
	}
}

void ocfs2_hamming_fix_block(void *data, unsigned int blocksize,
			     unsigned int fix)
{
	ocfs2_hamming_fix(data, blocksize * 8, 0, fix);
}

/*
 * table-based crc32_le() stolen from the kernel.  This is the one we know
 * the filesystem is using.
 *
 * RFC 3385 shows that the 802.3 crc32 (this one) has the same properties
 * and probabilities as crc32c (which iSCSI uses) for data blocks < 2^16
 * bits.  We fit.
 */

/**
 * crc32_le() - Calculate bitwise little-endian Ethernet AUTODIN II CRC32
 * @crc - seed value for computation.  ~0 for Ethernet, sometimes 0 for
 *        other uses, or the previous crc32 value if computing incrementally.
 * @p   - pointer to buffer over which CRC is run
 * @len - length of buffer @p
 *
 */
uint32_t crc32_le(uint32_t crc, unsigned char const *p, size_t len)
{
	const uint32_t      *b =(uint32_t *)p;
	const uint32_t      *tab = crc32table_le;

#if __BYTE_ORDER == __LITTLE_ENDIAN
# define DO_CRC(x) crc = tab[ (crc ^ (x)) & 255 ] ^ (crc>>8)
#else
# define DO_CRC(x) crc = tab[ ((crc >> 24) ^ (x)) & 255] ^ (crc<<8)
#endif

	crc = cpu_to_le32(crc);
	/* Align it */
	if(((long)b)&3 && len){
		do {
			uint8_t *p = (uint8_t *)b;
			DO_CRC(*p++);
			b = (void *)p;
		} while ((--len) && ((long)b)&3 );
	}
	if(len >= 4){
		/* load data 32 bits wide, xor data 32 bits wide. */
		size_t save_len = len & 3;
	        len = len >> 2;
		--b; /* use pre increment below(*++b) for speed */
		do {
			crc ^= *++b;
			DO_CRC(0);
			DO_CRC(0);
			DO_CRC(0);
			DO_CRC(0);
		} while (--len);
		b++; /* point to next byte(s) */
		len = save_len;
	}
	/* And the last few bytes */
	if(len){
		do {
			uint8_t *p = (uint8_t *)b;
			DO_CRC(*p++);
			b = (void *)p;
		} while (--len);
	}

	return le32_to_cpu(crc);
#undef DO_CRC
}

/*
 * This function generates check information for a block.
 * data is the block to be checked.  bc is a pointer to the
 * ocfs2_block_check structure describing the crc32 and the ecc.
 *
 * bc should be a pointer inside data, as the function will
 * take care of zeroing it before calculating the check information.  If
 * bc does not point inside data, the caller must make sure any inline
 * ocfs2_block_check structures are zeroed.
 *
 * The data buffer must be in on-disk endian (little endian for ocfs2).
 * bc will be filled with little-endian values and will be ready to go to
 * disk.
 */
void ocfs2_block_check_compute(void *data, size_t blocksize,
			       struct ocfs2_block_check *bc)
{
	uint32_t crc;
	uint16_t ecc;

	memset(bc, 0, sizeof(struct ocfs2_block_check));

	crc = crc32_le(~0, data, blocksize);
	/* We know this will return max 16 bits */
	ecc = (uint16_t)ocfs2_hamming_encode_block(data, blocksize);

	bc->bc_crc32e = cpu_to_le32(crc);
	bc->bc_ecc = cpu_to_le16(ecc);  /* We know it's max 16 bits */
}

/*
 * This function validates existing check information.  Like _compute,
 * the function will take care of zeroing bc before calculating check codes.
 * If bc is not a pointer inside data, the caller must have zeroed any
 * inline ocfs2_block_check structures.
 *
 * Again, the data passed in should be the on-disk endian.
 */
errcode_t ocfs2_block_check_validate(void *data, size_t blocksize,
				     struct ocfs2_block_check *bc)
{
	errcode_t err = 0;
	struct ocfs2_block_check check;
	uint32_t crc, ecc;

	check.bc_crc32e = le32_to_cpu(bc->bc_crc32e);
	check.bc_ecc = le16_to_cpu(bc->bc_ecc);

	memset(bc, 0, sizeof(struct ocfs2_block_check));

	/* Fast path - if the crc32 validates, we're good to go */
	crc = crc32_le(~0, data, blocksize);
	if (crc == check.bc_crc32e)
		goto out;

	/* Ok, try ECC fixups */
	ecc = ocfs2_hamming_encode_block(data, blocksize);
	ocfs2_hamming_fix_block(data, blocksize, ecc ^ check.bc_ecc);

	/* And check the crc32 again */
	crc = crc32_le(~0, data, blocksize);
	if (crc == check.bc_crc32e)
		goto out;

	err = OCFS2_ET_IO;

out:
	bc->bc_crc32e = cpu_to_le32(check.bc_crc32e);
	bc->bc_ecc = cpu_to_le16(check.bc_ecc);

	return err;
}

/*
 * These are the main API.  They check the superblock flag before
 * calling the underlying operations.
 *
 * They expect the buffer to be in disk format.
 */
void ocfs2_compute_meta_ecc(ocfs2_filesys *fs, void *data,
			    struct ocfs2_block_check *bc)
{
	if (ocfs2_meta_ecc(OCFS2_RAW_SB(fs->fs_super)))
		ocfs2_block_check_compute(data, fs->fs_blocksize, bc);
}

errcode_t ocfs2_validate_meta_ecc(ocfs2_filesys *fs, void *data,
				  struct ocfs2_block_check *bc)
{
	errcode_t err = 0;

	if (ocfs2_meta_ecc(OCFS2_RAW_SB(fs->fs_super)) &&
	    !(fs->fs_flags & OCFS2_FLAG_NO_ECC_CHECKS))
		err = ocfs2_block_check_validate(data, fs->fs_blocksize, bc);

	return err;
}

#ifdef DEBUG_EXE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <assert.h>
#include <errno.h>

/*
 * The function hamming_encode_orig() is my original, tested version.  It's
 * slow.  We work from it to make a faster one.
 */

/*
 * We use the following conventions:
 *
 * d = # data bits
 * p = # parity bits
 * c = # total code bits (d + p)
 */
static int calc_parity_bits_orig(unsigned int d)
{
	unsigned int p;

	/*
	 * Bits required for Single Error Correction is as follows:
	 *
	 * d + p + 1 <= 2^p
	 *
	 * We're restricting ourselves to 31 bits of parity, that should be
	 * sufficient.
	 */
	for (p = 1; p < 32; p++)
	{
		if ((d + p + 1) <= (1 << p))
			return p;
	}

	return 0;
}

static unsigned int calc_code_bit_orig(unsigned int i)
{
	unsigned int b, p;

	/*
	 * Data bits are 0-based, but we're talking code bits, which
	 * are 1-based.
	 */
	b = i + 1;

	/*
	 * For every power of two below our bit number, bump our bit.
	 *
	 * We compare with (b + 1) because we have to compare with what b
	 * would be _if_ it were bumped up by the parity bit.  Capice?
	 */
	for (p = 0; (1 << p) < (b + 1); p++)
		b++;

	return b;
}

/*
 * Find the log base 2 of 32-bit v.
 *
 * Algorithm found on http://graphics.stanford.edu/~seander/bithacks.html,
 * by Sean Eron Anderson.  Code on the page is in the public domain unless
 * otherwise noted.
 *
 * This particular algorithm is credited to Eric Cole.
 */
static int find_highest_bit_set(unsigned int v)
{

	static const int MultiplyDeBruijnBitPosition[32] =
	{
		0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8,
		31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9
	};

	v |= v >> 1; /* first round down to power of 2 */
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v = (v >> 1) + 1;

	return MultiplyDeBruijnBitPosition[(uint32_t)(v * 0x077CB531UL) >> 27];
}

static unsigned int calc_code_bit_cheat(unsigned int i)
{
	unsigned int b, p;

	/*
	 * Data bits are 0-based, but we're talking code bits, which
	 * are 1-based.
	 */
	b = i + 1;

	/*
	 * As a cheat, we know that all bits below b's highest bit must be
	 * parity bits, so we can start there.
	 */
        p = find_highest_bit_set(b);
        b += p;

	/*
	 * For every power of two below our bit number, bump our bit.
	 *
	 * We compare with (b + 1) because we have to compare with what b
	 * would be _if_ it were bumped up by the parity bit.  Capice?
	 *
	 * We start p at 2^p because of the cheat above.
	 */
	for (p = (1 << p); p < (b + 1); p <<= 1)
		b++;

	return b;
}


/*
 * This is the low level encoder function.  It can be called across
 * multiple hunks just like the crc32 code.  'd' is the number of bits
 * _in_this_hunk_.  nr is the bit offset of this hunk.  So, if you had
 * two 512B buffers, you would do it like so:
 *
 * parity = ocfs2_hamming_encode(0, buf1, 512 * 8, 0);
 * parity = ocfs2_hamming_encode(parity, buf2, 512 * 8, 512 * 8);
 *
 * If you just have one buffer, use ocfs2_hamming_encode_block().
 */
static uint32_t hamming_encode_orig(uint32_t parity, void *data, unsigned int d,
				    unsigned int nr)
{
	unsigned int p = calc_parity_bits_orig(d);
	unsigned int i, j, b;

	if (!p)
		abort();

	/*
	 * b is the hamming code bit number.  Hamming code specifies a
	 * 1-based array, but C uses 0-based.  So 'i' is for C, and 'b' is
	 * for the algorithm.
	 *
	 * The i++ in the for loop is so that the start offset passed
	 * to ocfs2_find_next_bit_set() is one greater than the previously
	 * found bit.
	 */
	for (i = 0; (i = ocfs2_find_next_bit_set(data, d, i)) < d; i++)
	{
		/*
		 * i is the offset in this hunk, nr + i is the total bit
		 * offset.
		 */
		b = calc_code_bit_orig(nr + i);

		for (j = 0; j < p; j++)
		{
			/*
			 * Data bits in the resultant code are checked by
			 * parity bits that are part of the bit number
			 * representation.  Huh?
			 *
			 * <wikipedia href="http://en.wikipedia.org/wiki/Hamming_code">
			 * In other words, the parity bit at position 2^k
			 * checks bits in positions having bit k set in
			 * their binary representation.  Conversely, for
			 * instance, bit 13, i.e. 1101(2), is checked by
			 * bits 1000(2) = 8, 0100(2)=4 and 0001(2) = 1.
			 * </wikipedia>
			 *
			 * Note that 'k' is the _code_ bit number.  'b' in
			 * our loop.
			 */
			if (b & (1 << j))
				parity ^= (1 << j);
		}
	}

	/* While the data buffer was treated as little endian, the
	 * return value is in host endian. */
	return parity;
}

/*
 * This version uses the direct parity ^= b, but the original
 * calc_parity_bits() and calc_code_bit().
 */
static uint32_t ocfs2_hamming_encode_orig_bits(uint32_t parity, void *data,
					       unsigned int d, unsigned int nr)
{
	unsigned int p = calc_parity_bits_orig(d);
	unsigned int i, b;

	if (!p)
		abort();

	/*
	 * b is the hamming code bit number.  Hamming code specifies a
	 * 1-based array, but C uses 0-based.  So 'i' is for C, and 'b' is
	 * for the algorithm.
	 *
	 * The i++ in the for loop is so that the start offset passed
	 * to ocfs2_find_next_bit_set() is one greater than the previously
	 * found bit.
	 */
	for (i = 0; (i = ocfs2_find_next_bit_set(data, d, i)) < d; i++)
	{
		/*
		 * i is the offset in this hunk, nr + i is the total bit
		 * offset.
		 */
		b = calc_code_bit_orig(nr + i);

		/*
		 * Data bits in the resultant code are checked by
		 * parity bits that are part of the bit number
		 * representation.  Huh?
		 *
		 * <wikipedia href="http://en.wikipedia.org/wiki/Hamming_code">
		 * In other words, the parity bit at position 2^k
		 * checks bits in positions having bit k set in
		 * their binary representation.  Conversely, for
		 * instance, bit 13, i.e. 1101(2), is checked by
		 * bits 1000(2) = 8, 0100(2)=4 and 0001(2) = 1.
		 * </wikipedia>
		 *
		 * Note that 'k' is the _code_ bit number.  'b' in
		 * our loop.
		 */
		parity ^= b;
	}

	/* While the data buffer was treated as little endian, the
	 * return value is in host endian. */
	return parity;
}

/*
 * This version uses the direct parity ^= b, but the original
 * calc_code_bit()
 */
static uint32_t ocfs2_hamming_encode_orig_code_bit(uint32_t parity, void *data,
						   unsigned int d, unsigned int nr)
{
	unsigned int i, b;

	if (!d)
		abort();

	/*
	 * b is the hamming code bit number.  Hamming code specifies a
	 * 1-based array, but C uses 0-based.  So 'i' is for C, and 'b' is
	 * for the algorithm.
	 *
	 * The i++ in the for loop is so that the start offset passed
	 * to ocfs2_find_next_bit_set() is one greater than the previously
	 * found bit.
	 */
	for (i = 0; (i = ocfs2_find_next_bit_set(data, d, i)) < d; i++)
	{
		/*
		 * i is the offset in this hunk, nr + i is the total bit
		 * offset.
		 */
		b = calc_code_bit_orig(nr + i);

		/*
		 * Data bits in the resultant code are checked by
		 * parity bits that are part of the bit number
		 * representation.  Huh?
		 *
		 * <wikipedia href="http://en.wikipedia.org/wiki/Hamming_code">
		 * In other words, the parity bit at position 2^k
		 * checks bits in positions having bit k set in
		 * their binary representation.  Conversely, for
		 * instance, bit 13, i.e. 1101(2), is checked by
		 * bits 1000(2) = 8, 0100(2)=4 and 0001(2) = 1.
		 * </wikipedia>
		 *
		 * Note that 'k' is the _code_ bit number.  'b' in
		 * our loop.
		 */
		parity ^= b;
	}

	/* While the data buffer was treated as little endian, the
	 * return value is in host endian. */
	return parity;
}

/*
 * This version uses the direct parity ^= b, but the cheating
 * calc_code_bit().
 */
static uint32_t ocfs2_hamming_encode_cheat_code_bit(uint32_t parity, void *data,
						    unsigned int d, unsigned int nr)
{
	unsigned int i, b;

	if (!d)
		abort();

	/*
	 * b is the hamming code bit number.  Hamming code specifies a
	 * 1-based array, but C uses 0-based.  So 'i' is for C, and 'b' is
	 * for the algorithm.
	 *
	 * The i++ in the for loop is so that the start offset passed
	 * to ocfs2_find_next_bit_set() is one greater than the previously
	 * found bit.
	 */
	for (i = 0; (i = ocfs2_find_next_bit_set(data, d, i)) < d; i++)
	{
		/*
		 * i is the offset in this hunk, nr + i is the total bit
		 * offset.
		 */
		b = calc_code_bit_cheat(nr + i);

		/*
		 * Data bits in the resultant code are checked by
		 * parity bits that are part of the bit number
		 * representation.  Huh?
		 *
		 * <wikipedia href="http://en.wikipedia.org/wiki/Hamming_code">
		 * In other words, the parity bit at position 2^k
		 * checks bits in positions having bit k set in
		 * their binary representation.  Conversely, for
		 * instance, bit 13, i.e. 1101(2), is checked by
		 * bits 1000(2) = 8, 0100(2)=4 and 0001(2) = 1.
		 * </wikipedia>
		 *
		 * Note that 'k' is the _code_ bit number.  'b' in
		 * our loop.
		 */
		parity ^= b;
	}

	/* While the data buffer was treated as little endian, the
	 * return value is in host endian. */
	return parity;
}


struct run_context {
	char *rc_name;
	void *rc_data;
	int rc_size;
	int rc_count;
	void (*rc_func)(struct run_context *ct, int nr);
};

static void timeme(struct run_context *ct)
{
	int i;
	struct rusage start;
	struct rusage stop;
	struct timeval sys_diff, usr_diff;

	assert(!getrusage(RUSAGE_SELF, &start));

	for (i = 0; i < ct->rc_count; i++)
		ct->rc_func(ct, i);

	assert(!getrusage(RUSAGE_SELF, &stop));
	timersub(&stop.ru_utime, &start.ru_utime, &usr_diff);
	timersub(&stop.ru_stime, &start.ru_stime, &sys_diff);

	fprintf(stderr, "Time for %s: %ld.%06ld user, %ld.%06ld system\n",
		ct->rc_name, usr_diff.tv_sec, usr_diff.tv_usec,
		sys_diff.tv_sec, sys_diff.tv_usec);
}

static void crc32_func(struct run_context *ct, int nr)
{
	uint32_t crc = ~0;

	crc = crc32_le(crc, ct->rc_data, ct->rc_size);
}

static void run_crc32(char *buf, int size, int count)
{
	struct run_context ct = {
		.rc_name = "CRC32",
		.rc_data = buf,
		.rc_size = size,
		.rc_count = count,
		.rc_func = crc32_func,
	};

	timeme(&ct);
}

struct hamming_context {
	struct run_context hc_rc;
	uint32_t hc_ecc;
	int hc_ecc_valid;
	uint32_t (*hc_encode)(uint32_t parity, void *data, unsigned int d,
			      unsigned int nr);
};

#define rc_to_hc(_rc) ((struct hamming_context *)(_rc))

static void hamming_func(struct run_context *ct, int nr)
{
	uint32_t ecc = 0;
	struct hamming_context *hc = rc_to_hc(ct);

	ecc = hc->hc_encode(ecc, ct->rc_data, ct->rc_size * 8, 0);

	if (hc->hc_ecc_valid) {
		if (hc->hc_ecc != ecc) {
			fprintf(stderr,
				"Calculated ecc %"PRIu32" != saved ecc %"PRIu32"\n",
				ecc, hc->hc_ecc);
			exit(1);
		}
	} else {
		assert(!nr);
		hc->hc_ecc = ecc;
		hc->hc_ecc_valid = 1;
	};
}

static void run_hamming(char *buf, int size, int count)
{
	struct hamming_context hc = {
		.hc_rc = {
			.rc_name = "Original hamming code",
			.rc_data = buf,
			.rc_size = size,
			.rc_count = count,
			.rc_func = hamming_func,
		},
		.hc_encode = hamming_encode_orig,
	};

	timeme(&hc.hc_rc);

	hc.hc_rc.rc_name = "Current hamming code";
	hc.hc_encode = ocfs2_hamming_encode;
	timeme(&hc.hc_rc);

	hc.hc_rc.rc_name = "Parity xor with orig calc bits";
	hc.hc_encode = ocfs2_hamming_encode_orig_bits;
	timeme(&hc.hc_rc);

	hc.hc_rc.rc_name = "Parity xor with orig calc code bit";
	hc.hc_encode = ocfs2_hamming_encode_orig_code_bit;
	timeme(&hc.hc_rc);

	hc.hc_rc.rc_name = "Parity xor with cheating calc code bit";
	hc.hc_encode = ocfs2_hamming_encode_cheat_code_bit;
	timeme(&hc.hc_rc);

	hc.hc_rc.rc_name = "Current hamming code";
	hc.hc_encode = ocfs2_hamming_encode;
	timeme(&hc.hc_rc);
}

static uint64_t read_number(const char *num)
{
	uint64_t val;
	char *ptr;

	val = strtoull(num, &ptr, 0);
	if (!ptr || *ptr)
		return 0;

	return val;
}

static void get_file(char *filename, char **buf, int *size)
{
	int rc, fd, tot = 0;
	char *b;
	struct stat stat_buf;

	rc  = stat(filename, &stat_buf);
	if (rc) {
		fprintf(stderr, "Unable to stat \"%s\": %s\n", filename,
			strerror(errno));
		exit(1);
	}
	if (!S_ISREG(stat_buf.st_mode)) {
		fprintf(stderr, "File \"%s\" is not a regular file\n",
			filename);
		exit(1);
	}

	b = malloc(stat_buf.st_size * sizeof(char));
	if (!b) {
		fprintf(stderr, "Unable to allocate buffer: %s\n",
			strerror(errno));
		exit(1);
	}

	fd = open64(filename, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Unable to open \"%s\": %s\n", filename,
			strerror(errno));
		exit(1);
	}

	while (tot < stat_buf.st_size) {
		rc = read(fd, b + tot, stat_buf.st_size - tot);
		if (rc < 0) {
			fprintf(stderr, "Error reading from \"%s\": %s\n",
				filename, strerror(errno));
			exit(1);
		}
		if (!rc) {
			fprintf(stderr, "Unexpected EOF while reading from \"%s\"\n",
				filename);
			exit(1);
		}
		tot += rc;
	}

	close(fd);
	*size = stat_buf.st_size;
	*buf = b;
}

static void print_usage(void)
{
	fprintf(stderr,
		"Usage: blockcheck <filename> [<count>]\n");
}

int main(int argc, char *argv[])
{
	int size, count = 1;
	char *filename, *buf;

	initialize_ocfs_error_table();

	if (argc < 2) {
		fprintf(stderr, "Missing filename\n");
		print_usage();
		return 1;
	}
	filename = argv[1];

	if (argc > 2) {
		count = read_number(argv[2]);
		if (count < 1) {
			fprintf(stderr, "Invalid count: %d\n", count);
			print_usage();
			return 1;
		}
	}

	get_file(filename, &buf, &size);
	run_crc32(buf, size, count);
	run_hamming(buf, size, count);


#if 0
	ocfs2_block_check_compute(buf, size, &check);
	fprintf(stdout, "crc32le: %"PRIu32", ecc: %"PRIu16"\n",
		le32_to_cpu(check.bc_crc32e), le16_to_cpu(check.bc_ecc));
#endif

	free(buf);

	return 0;
}

#endif
