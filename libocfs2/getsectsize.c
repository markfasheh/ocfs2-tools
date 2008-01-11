/*
 * getsectsize.c --- get the sector size of a device.
 * 
 * Copyright (C) 1995, 1995 Theodore Ts'o.
 * Copyright (C) 2003 VMware, Inc.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

/* Modified for OCFS2 by Manish Singh <manish.singh@oracle.com> */

#define HAVE_UNISTD_H 1
#define HAVE_ERRNO_H 1
#define HAVE_LINUX_FD_H 1
#define HAVE_OPEN64 1

#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE

#include <stdio.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_ERRNO_H
#include <errno.h>
#endif
#include <fcntl.h>
#ifdef HAVE_LINUX_FD_H
#include <sys/ioctl.h>
#include <linux/fd.h>
#endif

#if defined(__linux__) && defined(_IO) && !defined(BLKSSZGET)
#define BLKSSZGET  _IO(0x12,104)/* get block device sector size */
#endif

#include "ocfs2/ocfs2.h"

/*
 * Returns the number of blocks in a partition
 */
errcode_t ocfs2_get_device_sectsize(const char *file, int *sectsize)
{
	int	fd;

#ifdef HAVE_OPEN64
	fd = open64(file, O_RDONLY);
#else
	fd = open(file, O_RDONLY);
#endif
	if (fd < 0)
		return errno;

#ifdef BLKSSZGET
	if (ioctl(fd, BLKSSZGET, sectsize) >= 0) {
		close(fd);
		return 0;
	}
#endif
	*sectsize = 0;
	close(fd);
	return 0;
}

#ifdef DEBUG_EXE
int main(int argc, char **argv)
{
	int     sectsize;
	int     retval;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s device\n", argv[0]);
		exit(1);
	}

	retval = ocfs2_get_device_sectsize(argv[1], &sectsize);
	if (retval) {
		com_err(argv[0], retval,
			"while calling ocfs2_get_device_sectsize");
		exit(1);
	}
	printf("Device %s has a hardware sector size of %d.\n",
	       argv[1], sectsize);
	exit(0);
}
#endif
