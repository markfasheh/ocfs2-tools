/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * unix_io.c
 *
 * I/O routines for the OCFS2 userspace library.
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
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
 * Portions of this code from e2fsprogs/lib/ext2fs/unix_io.c
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001,
 *  	2002 by Theodore Ts'o.
 */

#define _XOPEN_SOURCE 600  /* Triggers ISOC99, UNIX98 in features.h */
#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE /* Because libc really doesn't want us using O_DIRECT? */

#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#ifdef __linux__
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#endif

#include "ocfs2.h"


struct _io_channel {
	char *io_name;
	int io_blksize;
	int io_flags;
	int io_error;
	int io_fd;
};


errcode_t io_open(const char *name, int flags, io_channel **channel)
{
	errcode_t ret;
	io_channel *chan = NULL;
#ifdef __linux__
	struct stat stat_buf;
	struct utsname ut;
#endif

	if (!name || !*name)
		return OCFS2_ET_BAD_DEVICE_NAME;

	ret = ocfs2_malloc0(sizeof(struct _io_channel), &chan);
	if (ret)
		return ret;

	ret = ocfs2_malloc(strlen(name)+1, &chan->io_name);
	if (ret)
		goto out_chan;
	strcpy(chan->io_name, name);
	chan->io_blksize = OCFS2_MIN_BLOCKSIZE;
	chan->io_flags = (flags & OCFS2_FLAG_RW) ? O_RDWR : O_RDONLY;
	/* FIXME: should do a "check for success, fallback to bindraw */
	chan->io_flags |= O_DIRECT;
	chan->io_error = 0;

	ret = OCFS2_ET_IO;
	chan->io_fd = open64(name, chan->io_flags);
	if (chan->io_fd < 0) {
		chan->io_error = errno;
		goto out_name;
	}

	/* Workaround from e2fsprogs */
#ifdef __linux__
#undef RLIM_INFINITY
#if (defined(__alpha__) || ((defined(__sparc__) || defined(__mips__)) && (SIZEOF_LONG == 4)))
#define RLIM_INFINITY	((unsigned long)(~0UL>>1))
#else
#define RLIM_INFINITY  (~0UL)
#endif
	/*
	 * Work around a bug in 2.4.10-2.4.18 kernels where writes to
	 * block devices are wrongly getting hit by the filesize
	 * limit.  This workaround isn't perfect, since it won't work
	 * if glibc wasn't built against 2.2 header files.  (Sigh.)
	 * 
	 */
	if ((flags & OCFS2_FLAG_RW) &&
	    (uname(&ut) == 0) &&
	    ((ut.release[0] == '2') && (ut.release[1] == '.') &&
	     (ut.release[2] == '4') && (ut.release[3] == '.') &&
	     (ut.release[4] == '1') && (ut.release[5] >= '0') &&
	     (ut.release[5] < '8')) &&
	    (fstat(chan->io_fd, &stat_buf) == 0) &&
	    (S_ISBLK(stat_buf.st_mode))) {
		struct rlimit	rlim;
		
		rlim.rlim_cur = rlim.rlim_max = (unsigned long) RLIM_INFINITY;
		setrlimit(RLIMIT_FSIZE, &rlim);
		getrlimit(RLIMIT_FSIZE, &rlim);
		if (((unsigned long) rlim.rlim_cur) <
		    ((unsigned long) rlim.rlim_max)) {
			rlim.rlim_cur = rlim.rlim_max;
			setrlimit(RLIMIT_FSIZE, &rlim);
		}
	}
#endif

	*channel = chan;
	return 0;

out_name:
	ocfs2_free(&chan->io_name);

out_chan:
	ocfs2_free(&chan);

	*channel = NULL;
	return ret;
}

errcode_t io_close(io_channel *channel)
{
	errcode_t ret = 0;

	if (close(channel->io_fd) < 0)
		ret = errno;

	ocfs2_free(&channel->io_name);
	ocfs2_free(&channel);

	return ret;
}

int io_get_error(io_channel *channel)
{
	return channel->io_error;
}

errcode_t io_set_blksize(io_channel *channel, int blksize)
{
	if (blksize % OCFS2_MIN_BLOCKSIZE)
		return OCFS2_ET_INVALID_ARGUMENT;

	if (!blksize)
		blksize = OCFS2_MIN_BLOCKSIZE;

	if (channel->io_blksize != blksize)
		channel->io_blksize = blksize;

	return 0;
}

int io_get_blksize(io_channel *channel)
{
	return channel->io_blksize;
}

errcode_t io_read_block(io_channel *channel, int64_t blkno, int count,
			char *data)
{
	int ret;
	ssize_t size, tot, rd;
	uint64_t location;

	/* -ative means count is in bytes */
	size = (count < 0) ? -count : count * channel->io_blksize;
	location = blkno * channel->io_blksize;

	tot = 0;
	while (tot < size) {
		rd = pread64(channel->io_fd, data + tot,
			     size - tot, location + tot);
		ret = OCFS2_ET_IO;
		if (rd < 0) {
			channel->io_error = errno;
			goto out;
		}

		if (!rd) 
			goto out;

		tot += rd;
	}

	ret = 0;

out:
	if (!ret && tot != size) {
		ret = OCFS2_ET_SHORT_READ;
		memset(data + tot, 0, size - tot);
	}

	return ret;
}

errcode_t io_write_block(io_channel *channel, int64_t blkno, int count,
			 const char *data)
{
	int ret;
	ssize_t size, tot, wr;
	uint64_t location;

	/* -ative means count is in bytes */
	size = (count < 0) ? -count : count * channel->io_blksize;
	location = blkno * channel->io_blksize;

	tot = 0;
	while (tot < size) {
		wr = pwrite64(channel->io_fd, data + tot,
 			      size - tot, location + tot);
		ret = OCFS2_ET_IO;
		if (wr < 0) {
			channel->io_error = errno;
			goto out;
		}

		if (!wr) 
			goto out;

		tot += wr;
	}

	ret = 0;
out:
	if (!ret && (tot != size))
		ret = OCFS2_ET_SHORT_WRITE;

	return ret;
}


#ifdef DEBUG_EXE
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <limits.h>

static int64_t read_number(const char *num)
{
	int64_t val;
	char *ptr;

	val = strtoll(num, &ptr, 0);
	if (!ptr || *ptr)
		return 0;

	return val;
}

static void dump_u32(uint32_t *val)
{
	unsigned int i;
	uint8_t *bytes = (uint8_t *)val;

	for (i = 0; i < sizeof(uint32_t); i++)
		fprintf(stdout, "%02X", bytes[i]);
}

static void dump_block(int64_t blkno, int blksize, char *buf)
{
	unsigned int i;
	uint32_t *vals = (uint32_t *)buf;

	fprintf(stdout, "Dumping block %lld (%d bytes):\n", blkno,
		blksize);

	for (i = 0; i < (blksize / sizeof(uint32_t)); i++) {
		if (!(i % 4)) {
			if (i)
				fprintf(stdout, "\n");
			fprintf(stdout, "0x%08X\t",
				i * sizeof(uint32_t));
		}
		dump_u32(&vals[i]);
		fprintf(stdout, " ");
	}
	fprintf(stdout, "\n");
}

static void print_usage(void)
{
	fprintf(stderr,
		"Usage: unix_io [-b <blkno>] [-c <count>] [-B <blksize>]\n"
	       	"               <filename>\n");
}

extern int opterr, optind;
extern char *optarg;

int main(int argc, char *argv[])
{
	errcode_t ret;
	int c;
	int64_t blkno, count, blksize;
	char *filename;
	io_channel *channel;
	char *blks;

	/* Some simple defaults */
	blksize = 512;
	blkno = 0;
	count = 1;

	initialize_ocfs_error_table();

	while((c = getopt(argc, argv, "b:c:B:")) != EOF) {
		switch (c) {
			case 'b':
				blkno = read_number(optarg);
				if (blkno < 0) {
					fprintf(stderr,
						"Invalid blkno: %s\n",
						optarg);
					print_usage();
					return 1;
				}
				break;

			case 'c':
				count = read_number(optarg);
				if (!count) {
					fprintf(stderr, 
						"Invalid count: %s\n",
						optarg);
					print_usage();
					return 1;
				}
				break;

			case 'B':
				blksize = read_number(optarg);
				if (!blksize) {
					fprintf(stderr, 
						"Invalid blksize: %s\n",
						optarg);
					print_usage();
					return 1;
				}
				break;

			default:
				print_usage();
				return 1;
				break;
		}
	}

	if (blksize % OCFS2_MIN_BLOCKSIZE) {
		fprintf(stderr, "Invalid blocksize: %lld\n", blksize);
		print_usage();
		return 1;
	}
	if (count < 0) {
		if (-count > (int64_t)INT_MAX) {
			fprintf(stderr, "Count is too large: %lld\n",
				count);
			print_usage();
			return 1;
		}
		count = -count / blksize;
	} else  {
		if ((count * blksize) > INT_MAX) {
			fprintf(stderr, "Count is too large: %lld\n",
				count);
			print_usage();
			return 1;
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "Missing filename\n");
		print_usage();
		return 1;
	}

	filename = argv[optind];

	ret = io_open(filename, OCFS2_FLAG_RO, &channel);
	if (ret) {
		com_err(argv[0], ret,
			"while opening file \"%s\"", filename);
		goto out;
	}

	ret = ocfs2_malloc_blocks(channel, (int)count, &blks);
	if (ret) {
		com_err(argv[0], ret,
			"while allocating %d blocks", count);
		goto out_channel;
	}

	ret = io_read_block(channel, blkno, (int)count, blks);
	if (ret) {
		com_err(argv[0], ret,
			"while reading %d blocks at block %lld (%s)",
			(int)count, blkno,
			strerror(io_get_error(channel)));
		goto out_blocks;
	}

	for (c = 0; c < count; c++)
		dump_block(blkno + c, blksize, blks + (c * blksize));

out_blocks:
	ocfs2_free(&blks);

out_channel:
	ret = io_close(channel);
	if (ret) {
		com_err(argv[0], ret,
			"while closing file \"%s\"", filename);
	}

out:
	return 0;
}
#endif  /* DEBUG_EXE */
