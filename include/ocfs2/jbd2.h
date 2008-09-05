/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * jbd2.h
 *
 * header file extracted from linux/include/linux/jbd2.h
 *
 * Originally written by Stephen C. Tweedie <sct@redhat.com>
 *
 * Copyright 1998-2000 Red Hat, Inc --- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * Definitions for transaction data structures for the buffer cache
 * filesystem journaling support.
 */

#ifndef _JBD2_H_
#define _JBD2_H_

/*
 * Internal structures used by the logging mechanism:
 */

#define JBD2_MAGIC_NUMBER 0xc03b3998U /* The first 4 bytes of /dev/random! */

/*
 * On-disk structures
 */

/* 
 * Descriptor block types:
 */

#define JBD2_DESCRIPTOR_BLOCK	1
#define JBD2_COMMIT_BLOCK	2
#define JBD2_SUPERBLOCK_V1	3
#define JBD2_SUPERBLOCK_V2	4
#define JBD2_REVOKE_BLOCK	5

/*
 * Standard header for all descriptor blocks:
 */
typedef struct journal_header_s
{
	__be32		h_magic;
	__be32		h_blocktype;
	__be32		h_sequence;
} journal_header_t;

/*
 * Checksum types.
 */
#define JBD2_CRC32_CHKSUM       1
#define JBD2_MD5_CHKSUM         2
#define JBD2_SHA2_CHKSUM        3

#define JBD2_CRC32_CHKSUM_SIZE  4

#define JBD2_CHECKSUM_BYTES (32 / sizeof(uint32_t))
/*
 * Commit block header for storing transactional checksums:
 */
struct commit_header {
	__be32		h_magic;
	__be32          h_blocktype;
	__be32          h_sequence;
	unsigned char   h_chksum_type;
	unsigned char   h_chksum_size;
	unsigned char 	h_padding[2];
	__be32 		h_chksum[JBD2_CHECKSUM_BYTES];
	__be64		h_commit_sec;
	__be32		h_commit_nsec;
};

/*
 * The block tag: used to describe a single buffer in the journal.
 * t_blocknr_high is only used if INCOMPAT_64BIT is set, so this
 * raw struct shouldn't be used for pointer math or sizeof() - use
 * journal_tag_bytes(journal) instead to compute this.
 */
typedef struct journal_block_tag_s
{
	__be32		t_blocknr;	/* The on-disk block number */
	__be32		t_flags;	/* See below */
	__be32		t_blocknr_high; /* most-significant high 32bits. */
} journal_block_tag_t;

#define JBD2_TAG_SIZE32 (offsetof(journal_block_tag_t, t_blocknr_high))
#define JBD2_TAG_SIZE64 (sizeof(journal_block_tag_t))

/* 
 * The revoke descriptor: used on disk to describe a series of blocks to
 * be revoked from the log 
 */
typedef struct journal_revoke_header_s
{
	journal_header_t r_header;
	int		 r_count;	/* Count of bytes used in the block */
} journal_revoke_header_t;

/* Definitions for the journal tag flags word: */
#define JBD2_FLAG_ESCAPE	1	/* on-disk block is escaped */
#define JBD2_FLAG_SAME_UUID	2	/* block has same uuid as previous */
#define JBD2_FLAG_DELETED	4	/* block deleted by this transaction */
#define JBD2_FLAG_LAST_TAG	8	/* last tag in this descriptor block */

/*
 * The journal superblock.  All fields are in big-endian byte order.
 */
typedef struct journal_superblock_s
{
/* 0x0000 */
	journal_header_t s_header;

/* 0x000C */
	/* Static information describing the journal */
	__be32	s_blocksize;		/* journal device blocksize */
	__be32	s_maxlen;		/* total blocks in journal file */
	__be32	s_first;		/* first block of log information */
	
/* 0x0018 */
	/* Dynamic information describing the current state of the log */
	__be32	s_sequence;		/* first commit ID expected in log */
	__be32	s_start;		/* blocknr of start of log */

/* 0x0020 */
	/* Error value, as set by jbd2_journal_abort(). */
	__be32	s_errno;

/* 0x0024 */
	/* Remaining fields are only valid in a version-2 superblock */
	__be32	s_feature_compat; 	/* compatible feature set */
	__be32	s_feature_incompat; 	/* incompatible feature set */
	__be32	s_feature_ro_compat; 	/* readonly-compatible feature set */
/* 0x0030 */
	__u8	s_uuid[16];		/* 128-bit uuid for journal */

/* 0x0040 */
	__be32	s_nr_users;		/* Nr of filesystems sharing log */
	
	__be32	s_dynsuper;		/* Blocknr of dynamic superblock copy*/
	
/* 0x0048 */
	__be32	s_max_transaction;	/* Limit of journal blocks per trans.*/
	__be32	s_max_trans_data;	/* Limit of data blocks per trans. */

/* 0x0050 */
	__be32	s_padding[44];

/* 0x0100 */
	__u8	s_users[16*48];		/* ids of all fs'es sharing the log */
/* 0x0400 */
} journal_superblock_t;

#define JBD2_HAS_COMPAT_FEATURE(jsb,mask)				\
	(((jsb)->s_header.h_blocktype == JBD2_SUPERBLOCK_V2) &&		\
	 ((jsb)->s_feature_compat & mask))
#define JBD2_HAS_RO_COMPAT_FEATURE(jsb,mask)				\
	(((jsb)->s_header.h_blocktype == JBD2_SUPERBLOCK_V2) &&		\
	 ((jsb)->s_feature_ro_compat & mask))
#define JBD2_HAS_INCOMPAT_FEATURE(jsb,mask)				\
	(((jsb)->s_header.h_blocktype == JBD2_SUPERBLOCK_V2) &&		\
	 ((jsb)->s_feature_incompat & mask))

#define JBD2_FEATURE_COMPAT_CHECKSUM		0x00000001

#define JBD2_FEATURE_INCOMPAT_REVOKE		0x00000001
#define JBD2_FEATURE_INCOMPAT_64BIT		0x00000002
#define JBD2_FEATURE_INCOMPAT_ASYNC_COMMIT	0x00000004

/* Features known to this kernel version: */
#define JBD2_KNOWN_COMPAT_FEATURES	0
#define JBD2_KNOWN_ROCOMPAT_FEATURES	0
#define JBD2_KNOWN_INCOMPAT_FEATURES	JBD2_FEATURE_INCOMPAT_REVOKE

static inline size_t journal_tag_bytes(journal_superblock_t *jsb)
{
	if (JBD2_HAS_INCOMPAT_FEATURE(jsb, JBD2_FEATURE_INCOMPAT_64BIT))
		return JBD2_TAG_SIZE64;
	else
		return JBD2_TAG_SIZE32;
}

#endif		/* _JBD2_H_ */
