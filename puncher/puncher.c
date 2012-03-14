/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * puncher.c
 *
 * Utility to punch holes in regular files
 *
 * Copyright (C) 2009, 2012 Oracle.  All rights reserved.
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
 */

#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE
#define _XOPEN_SOURCE 500
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>
#include <sys/ioctl.h>

#ifndef HAVE_FALLOCATE
# include <sys/syscall.h>
#endif

#ifdef HAVE_LINUX_FALLOC_H
# include <linux/falloc.h>	/* FALLOC_FL_* flags */
#endif
#ifndef FALLOC_FL_KEEP_SIZE
# define FALLOC_FL_KEEP_SIZE	0x01
#endif
#ifndef FALLOC_FL_PUNCH_HOLE
# define FALLOC_FL_PUNCH_HOLE	0x02
#endif

#ifndef SEEK_DATA
# define SEEK_DATA		3
#endif
#ifndef SEEK_HOLE
# define SEEK_HOLE		4
#endif

#ifndef EXT4_SUPER_MAGIC
# define EXT4_SUPER_MAGIC	0xEF53
#endif
#ifndef OCFS2_SUPER_MAGIC
# define OCFS2_SUPER_MAGIC	0x7461636f	/* taco */
#endif
#ifndef XFS_SB_MAGIC
# define XFS_SB_MAGIC		0x58465342      /* XFSB */
#endif

#include <ocfs2/ocfs2.h>
#include <ocfs2/bitops.h>

#include <tools-internal/progress.h>
#include <tools-internal/verbose.h>

#define ZERO_THRESHOLD		(1024 * 1024)

__u32 fs_supporting_punch[] = {
	EXT4_SUPER_MAGIC,
	OCFS2_SUPER_MAGIC,
	XFS_SB_MAGIC,
	0,
};

#define ROUND_UP(_a, _b)	(((_a) + (_b) - 1) / (_b))

struct punch_ctxt {
	int		pc_fd;
	blksize_t	pc_chunksize;
	blksize_t	pc_blocksize;
	__u64		pc_filesize;
	__u64		pc_zero_threshold;
	__u64		pc_numblocks;
	unsigned long	pc_seekdata:1,
			pc_dryrun:1;
	char		*pc_name;
	void		*pc_readbuf;
};

char *progname;

#ifndef HAVE_FALLOCATE
#ifndef __NR_fallocate
#define __NR_fallocate          324
#warning Your kernel headers dont define __NR_fallocate
#endif
static int fallocate(int fd, int mode, __off64_t offset, __off64_t len)
{
	return syscall(__NR_fallocate, fd, mode, offset, len);
}
#endif

static void usage(void)
{
	fprintf(stderr, "Usage: %s [options] filename\n", progname);
	fprintf(stderr, "Punches out unused areas of the file.\n\n");
	fprintf(stderr, "[options] are:\n");
	fprintf(stderr, "\t-h|--help\n");
	fprintf(stderr, "\t-p|--progress\n");
	fprintf(stderr, "\t-q|--quiet\n");
	fprintf(stderr, "\t-v|--verbose\n");
	fprintf(stderr, "\t--dry-run  (default)\n");
	fprintf(stderr, "\t--punch-holes\n");
	fprintf(stderr, "\t--max-compact\n");

	exit(1);
}

static void parse_opts(struct punch_ctxt *ctxt, int argc, char **argv)
{
	int c;
	static struct option long_options[] = {
		{ "max-compact", 0, 0, 1000 },
		{ "dry-run", 0, 0, 1001 },
		{ "punch-holes", 0, 0, 1002 },
		{ "progress", 0, 0, 'p' },
		{ "verbose", 0, 0, 'v' },
		{ "quiet", 0, 0, 'q' },
		{ "help", 0, 0, 'h' },
		{ 0, 0, 0, 0 },
	};

	progname = basename(argv[0]);

	ctxt->pc_zero_threshold = ZERO_THRESHOLD;
	ctxt->pc_dryrun = 1;
	tools_progress_disable();

	while (1) {
		c = getopt_long(argc, argv, "bpM?hvq", long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'b':
			ctxt->pc_blocksize = 1024;
			break;
		case 'p':
			tools_progress_enable();
			break;
		case 'v':
			tools_verbose();
			break;
		case 'q':
			tools_quiet();
			break;
		case 1000:
			ctxt->pc_zero_threshold = 0;
			break;
		case 1001:
			ctxt->pc_dryrun = 1;
			break;
		case 1002:
			ctxt->pc_dryrun = 0;
			break;
		case 'h':
		case '?':
		default:
			usage();
		}
	}

	if (argc - optind != 1)
		usage();

	ctxt->pc_name = argv[optind];
}

static ssize_t do_read(int fd, void *buf, size_t count, off_t offset)
{
	ssize_t ret;

	do {
		ret = pread(fd, buf, count, offset);
		if (ret == -1) {
			ret = errno;
			break;
		}
		if (count == ret)
			break;
		count -= ret;
		offset += ret;
		buf += ret;
	} while (1);

	return ret;
}

static int punch_hole(struct punch_ctxt *ctxt, __u64 offset, __u64 len)
{
	int ret = 0;

	verbosef(VL_OUT, "Punching hole (%llu blocks) at block offset %llu",
		 (unsigned long long)(len/ctxt->pc_blocksize),
		 (unsigned long long)(offset/ctxt->pc_blocksize));

	if (!ctxt->pc_dryrun)
		verbosef(VL_OUT, "\n");
	else {
		verbosef(VL_OUT, " (dry run)\n");
		goto bail;
	}

	ret = fallocate(ctxt->pc_fd,
			FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
			offset, len);
	if (ret < 0)
		com_err(progname, errno, "while punching a hole at offset %llu "
			"of length %llu", (unsigned long long)offset,
			(unsigned long long)len);
bail:
	return ret;
}

static int process_file_range(struct punch_ctxt *ctxt, __u64 startoff,
			      __u64 length)
{
	unsigned long chunks = length / ctxt->pc_chunksize;
	unsigned long i, punch_it = 0;
	__u64 offset = startoff;
	__u64 zero_offset = ULLONG_MAX, punch_len = 0;
	ssize_t wlen;
	int ret = 0;
	struct tools_progress *prog;

	verbosef(VL_DEBUG, "Scanning offset %llu blocks, length %llu blocks\n",
		 (unsigned long long)startoff/ctxt->pc_blocksize,
		 (unsigned long long)length/ctxt->pc_blocksize);

	prog = tools_progress_start("Punch Holes", "puncher", chunks);
	if (!prog)
		verbosef(VL_DEBUG, "unable to start progress");

	for (i = 0; i < chunks; ++i, offset += ctxt->pc_chunksize) {
		wlen = do_read(ctxt->pc_fd, ctxt->pc_readbuf, ctxt->pc_chunksize,
			       offset);
		if (wlen == -1) {
			com_err(progname, errno, "while reading file at "
				"offset %llu", (unsigned long long)offset);
			ret = errno;
			goto bail;
		}

		if (prog)
			tools_progress_step(prog, 1);

		wlen *= 8;

		ret = (ocfs2_find_first_bit_set(ctxt->pc_readbuf, wlen) == wlen);
		if (ret) {
			verbosef(VL_DEBUG, "Cluster at block offset %llu is "
				 "unused\n",
				 (unsigned long long)offset/ctxt->pc_blocksize);
			punch_len += ctxt->pc_chunksize;
			if (zero_offset == ULLONG_MAX)
				zero_offset = offset;
		} else {
			verbosef(VL_DEBUG, "Cluster at block offset %llu is "
				 "in use\n",
				 (unsigned long long)offset/ctxt->pc_blocksize);
			if (zero_offset != ULLONG_MAX)
				++punch_it;
		}
		ret = 0;

		if (i == (chunks - 1) && punch_len)
			++punch_it;

		if (punch_it && punch_len >= ctxt->pc_zero_threshold) {
			ret = punch_hole(ctxt, zero_offset, punch_len);
			if (ret)
				goto bail;
		}

		if (punch_it) {
			punch_it = 0;
			punch_len = 0;
			zero_offset = ULLONG_MAX;
		}
	}

bail:
	if (prog)
		tools_progress_stop(prog);
	return ret;
}

static int do_task(struct punch_ctxt *ctxt)
{
	int ret = 0;
	off64_t doff = 0, hoff, off;
	__u64 len;

	if (!ctxt->pc_seekdata) {
		len = ROUND_UP(ctxt->pc_filesize, ctxt->pc_chunksize);
		len *= ctxt->pc_chunksize;
		ret = process_file_range(ctxt, 0, len);
		goto bail;
	}

	while ((doff = lseek(ctxt->pc_fd, doff, SEEK_DATA)) != -1) {
		hoff = lseek(ctxt->pc_fd, doff, SEEK_HOLE);
		off = doff;
		len = hoff - doff;
		ret = process_file_range(ctxt, off, len);
		if (ret)
			goto bail;
		doff = hoff;
	}

bail:
	return ret;
}

static int open_file(struct punch_ctxt *ctxt)
{
	struct statfs fs;
	struct stat64 st;
	off64_t pos;
	int i, ret;
	__u64 num_balloc, num_bsize, num_bholes = 0;

	ret = statfs(ctxt->pc_name, &fs);
	if (ret) {
		com_err(progname, errno, "while looking up '%s'",
			ctxt->pc_name);
		return errno;
	}

	for (i = 0; fs_supporting_punch[i]; ++i) {
		if (fs.f_type == fs_supporting_punch[i])
			break;
	}

	if (!fs_supporting_punch[i]) {
		com_err(progname, EOPNOTSUPP, "; punching holes not supported "
			"by file system 0x%X", fs.f_type);
		return EOPNOTSUPP;
	}

	ctxt->pc_fd = open(ctxt->pc_name, O_RDWR | O_DIRECT);
	if (ctxt->pc_fd == -1) {
		com_err(progname, errno, "while opening file '%s'",
			ctxt->pc_name);
		return errno;
	}

	/* TODO bail out if not a regular file */

	ret = fstat64(ctxt->pc_fd, &st);
	if (ret) {
		com_err(progname, errno, "while stat-ing file");
		return errno;
	}

	if (!ctxt->pc_blocksize)
		ctxt->pc_blocksize = fs.f_bsize;	/* blocksize */
	ctxt->pc_filesize = st.st_size;
	ctxt->pc_chunksize = st.st_blksize;		/* allocation size? */
	ctxt->pc_numblocks = ROUND_UP(st.st_blocks * 512, ctxt->pc_blocksize);

	ret = posix_memalign(&ctxt->pc_readbuf, ctxt->pc_blocksize,
			     ctxt->pc_chunksize);
	if (!ctxt->pc_readbuf) {
		com_err(progname, errno, "while allocating %lu bytes",
			(unsigned long)ctxt->pc_chunksize);
		return errno;
	}

	num_bsize = ROUND_UP(st.st_size, ctxt->pc_blocksize);
	num_balloc = ctxt->pc_numblocks;
	if (num_bsize > num_balloc)
		num_bholes = num_bsize - num_balloc;

	/* Test support for seek_data/hole */
	pos = lseek(ctxt->pc_fd, 0, SEEK_DATA);
	if (pos != -1)
		pos = lseek(ctxt->pc_fd, 0, SEEK_HOLE);
	if (pos >= 0) {
		ctxt->pc_seekdata = 1;
		verbosef(VL_DEBUG, "Kernel supports llseek(2) "
			 "extensions SEEK_HOLE and/or SEEK_DATA.\n");
	} else {
		verbosef(VL_DEBUG, "Kernel does not support llseek(2) "
			 "extensions SEEK_HOLE and/or SEEK_DATA.\n");
	}

	verbosef(VL_OUT, "Size in blocks %llu, allocated %llu, "
		 "holes %llu (blocksize %lu)\n",
		 (unsigned long long)num_bsize,
		 (unsigned long long)num_balloc,
		 (unsigned long long)num_bholes,
		 (unsigned long)ctxt->pc_blocksize);

	verbosef(VL_DEBUG, "Cluster size %lu\n",
		 (unsigned long)ctxt->pc_chunksize);

	return 0;
}

static void close_file(struct punch_ctxt *ctxt)
{
	struct stat64 st;
	int ret;
	__u64 numblocks;

	if (ctxt->pc_readbuf)
		free(ctxt->pc_readbuf);

	if (ctxt->pc_fd == -1)
		return ;

	ret = fstat64(ctxt->pc_fd, &st);
	if (ret)
		return ;

	numblocks = ROUND_UP(st.st_blocks * 512, ctxt->pc_blocksize);

	verbosef(VL_OUT, "Allocated blocks reduced from %llu to %llu (%u%)\n",
		 ctxt->pc_numblocks, numblocks,
		 ((ctxt->pc_numblocks - numblocks) * 100 / ctxt->pc_numblocks));

	return;
}

/*
 * open file
 * read allocated blocks in chunks
 * look for runs of zeroed chunks
 * if size > threshold, punch_hole
 *
 */
int main(int argc, char **argv)
{
	struct punch_ctxt pc, *ctxt = &pc;
	int ret = 0;

	initialize_ocfs_error_table();

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	memset(ctxt, 0, sizeof(pc));

	ctxt->pc_fd = -1;

	parse_opts(ctxt, argc, argv);

	ret = open_file(ctxt);
	if (!ret)
		ret = do_task(ctxt);

	close_file(ctxt);

	ret = ret ? 1 : 0;
	return ret;
}
