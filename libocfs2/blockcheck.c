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

#include <inttypes.h>

#include "ocfs2/ocfs2.h"
#include "ocfs2/bitops.h"
#include "ocfs2/byteorder.h"

#include "blockcheck.h"



static inline unsigned int hc_hweight32(unsigned int w)
{
	unsigned int res = (w & 0x55555555) + ((w >> 1) & 0x55555555);
	res = (res & 0x33333333) + ((res >> 2) & 0x33333333);
	res = (res & 0x0F0F0F0F) + ((res >> 4) & 0x0F0F0F0F);
	res = (res & 0x00FF00FF) + ((res >> 8) & 0x00FF00FF);
	return (res & 0x0000FFFF) + ((res >> 16) & 0x0000FFFF);
}

/*
 * We use the following conventions:
 *
 * d = # data bits
 * p = # parity bits
 * c = # total code bits (d + p)
 */
static int calc_parity_bits(unsigned int d)
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
 */
static unsigned int calc_code_bit(unsigned int i)
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
	 * We compare with (b + 1) becuase we have to compare with what b
	 * would be _if_ it were bumped up by the parity bit.  Capice?
	 */
	for (p = 0; (1 << p) < (b + 1); p++)
		b++;

	return b;
}

/* XXX: Not endian safe? */
void ocfs2_hamming_encode(unsigned char *data, unsigned int d,
			  uint32_t *parity)
{
	unsigned int p = calc_parity_bits(d);
	unsigned int i, j, b;
	unsigned int plist[p];  /* XXX: We use a simple array to avoid ugly
				   bitops on *parity.  Perhaps this makes us
				   slower? */

	if (!p)
		abort();

	*parity = 0;
	memset(plist, 0, sizeof(unsigned int) * p);

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

		b = calc_code_bit(i);

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
				plist[j] ^= 1;
		}
	}

	/* Now fill *parity */
	for (j = 0; j < p; j++)
		if (plist[j])
			ocfs2_set_bit(j, parity);
}

void ocfs2_hamming_fix(unsigned char *data, unsigned int d,
		       unsigned int fix)
{
	unsigned int p = calc_parity_bits(d);
	unsigned int i, b;

	if (!p)
		abort();

	/*
	 * If the bit to fix has an hweight of 1, it's a parity bit.  One
	 * busted parity bit is its own error.  Nothing to do here.
	 */
	if (hc_hweight32(fix) == 1)
		return;

	/* See hamming_encode() for a description of 'b' */
	for (i = 0, b = 1; i < d; i++, b++)
	{
		/* Skip past parity bits */
		while (hc_hweight32(b) == 1)
			b++;

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

/*
 * table-less crc32_le() stolen from the kernel.  The kernel's slowest
 * version :-)
 *
 * RFC 3385 shows that the 802.3 crc32 (this one) has the same properties
 * and probabilities as crc32c (which iSCSI uses) for data blocks < 2^16
 * bits.  We fit.
 */

/*
 * There are multiple 16-bit CRC polynomials in common use, but this is
 * *the* standard CRC-32 polynomial, first popularized by Ethernet.
 * x^32+x^26+x^23+x^22+x^16+x^12+x^11+x^10+x^8+x^7+x^5+x^4+x^2+x^1+x^0
 */
#define CRCPOLY_LE 0xedb88320

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
	int i;
	while (len--) {
		crc ^= *p++;
		for (i = 0; i < 8; i++)
			crc = (crc >> 1) ^ ((crc & 1) ? CRCPOLY_LE : 0);
	}
	return crc;
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
 */
void ocfs2_block_check_compute(ocfs2_filesys *fs, void *data,
			       struct ocfs2_block_check *bc)
{
	uint32_t crc, ecc;

	/* XXX: Caller has already swapped the inode, so this needs
	 * endian fixup */

	memset(bc, 0, sizeof(struct ocfs2_block_check));

	/* XXX: I think crc32_le() might need bitreverse() */
	crc = crc32_le(~0, data, fs->fs_blocksize);
	ocfs2_hamming_encode(data, fs->fs_blocksize, &ecc);

	bc->bc_crc32e = crc;
	bc->bc_ecc = (uint16_t)ecc;  /* We know it's max 16 bits */
}

/*
 * This function validates existing check information.  Like _compute,
 * the function will take care of zeroing bc before calculating check codes.
 * If bc is not a pointer inside data, the caller must have zeroed any
 * inline ocfs2_block_check structures.
 */
errcode_t ocfs2_block_check_validate(ocfs2_filesys *fs, void *data,
				     struct ocfs2_block_check *bc)
{
	struct ocfs2_block_check check = *bc;
	uint32_t crc, ecc;

	/* XXX: Caller has already swapped the inode, so this needs
	 * endian fixup */

	memset(bc, 0, sizeof(struct ocfs2_block_check));

	/* Fast path - if the crc32 validates, we're good to go */
	crc = crc32_le(~0, data, fs->fs_blocksize);
	if (crc == check.bc_crc32e)
		return 0;

	/* Ok, try ECC fixups */
	ocfs2_hamming_encode(data, fs->fs_blocksize, &ecc);
	ocfs2_hamming_fix(data, fs->fs_blocksize, ecc ^ check.bc_ecc);

	/* And check the crc32 again */
	crc = crc32_le(~0, data, fs->fs_blocksize);
	if (crc == check.bc_crc32e)
		return 0;

	return OCFS2_ET_IO;
}

#ifdef DEBUG_EXE
#include <stdlib.h>

static uint64_t read_number(const char *num)
{
	uint64_t val;
	char *ptr;

	val = strtoull(num, &ptr, 0);
	if (!ptr || *ptr)
		return 0;

	return val;
}

static void print_usage(void)
{
	fprintf(stderr,
		"Usage: blockcheck <filename> <inode_num>\n");
}

extern int opterr, optind;
extern char *optarg;

int main(int argc, char *argv[])
{
	errcode_t ret;
	uint64_t blkno;
	char *filename, *buf;
	ocfs2_filesys *fs;
	struct ocfs2_dinode *di;
	struct ocfs2_block_check check;

	blkno = OCFS2_SUPER_BLOCK_BLKNO;

	initialize_ocfs_error_table();

	if (argc < 2) {
		fprintf(stderr, "Missing filename\n");
		print_usage();
		return 1;
	}
	filename = argv[1];

	if (argc > 2) {
		blkno = read_number(argv[2]);
		if (blkno < OCFS2_SUPER_BLOCK_BLKNO) {
			fprintf(stderr, "Invalid blockno: %"PRIu64"\n", blkno);
			print_usage();
			return 1;
		}
	}

	ret = ocfs2_open(filename, OCFS2_FLAG_RO, 0, 0, &fs);
	if (ret) {
		com_err(argv[0], ret,
			"while opening file \"%s\"", filename);
		goto out;
	}

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret) {
		com_err(argv[0], ret,
			"while allocating inode buffer");
		goto out_close;
	}


	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret) {
		com_err(argv[0], ret, "while reading inode %"PRIu64, blkno);
		goto out_free;
	}

	di = (struct ocfs2_dinode *)buf;

	fprintf(stdout, "OCFS2 inode %"PRIu64" on \"%s\"\n", blkno,
		filename);

	OCFS2_SET_INCOMPAT_FEATURE(OCFS2_RAW_SB(fs->fs_super),
				   OCFS2_FEATURE_INCOMPAT_META_ECC);
	ocfs2_block_check_compute(fs, buf, &check);
	fprintf(stdout, "crc32le: %"PRIu32", ecc: %"PRIu16"\n",
		check.bc_crc32e, check.bc_ecc);

out_free:
	ocfs2_free(&buf);

out_close:
	ret = ocfs2_close(fs);
	if (ret) {
		com_err(argv[0], ret,
			"while closing file \"%s\"", filename);
	}

out:
	return 0;
}

#endif
