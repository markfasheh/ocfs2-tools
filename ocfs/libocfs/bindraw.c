
/*
 * bindraw.c
 *
 * Binds device to first available raw device
 *
 * Copyright (C) 2004, Oracle.  All rights reserved.
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
 * Authors: Sunil Mushran
 */

#include <sys/raw.h>

#include "libocfs.h"
#include "bindraw.h"

/*
 * bind_raw()
 *
 */
int bind_raw(char *blk_dev, int *raw_minor, char *raw_dev, int rdlen)
{
	int fd = 0;
	int i;
	struct raw_config_request rcs;
	struct stat statbuf;
	int ret = -1;

	memset(&statbuf, 0, sizeof(struct stat));
	if (stat(blk_dev, &statbuf) == -1) {
		LOG_ERROR("%s", strerror(errno));
		goto bail;
	}

	if (MAJOR(statbuf.st_rdev) == 0) {
		LOG_ERROR("Invalid device %s", blk_dev);
		goto bail;
	}

	if (strstr(blk_dev, "/dev/raw")) {
		strncpy(raw_dev, blk_dev, rdlen-1);
		raw_dev[rdlen-1] = '\0';
	}
	else {
		fd = open("/dev/rawctl", O_RDWR);
		if (fd == -1) {
			LOG_ERROR("Error opening /dev/rawctl.\n%s",
				  strerror(errno));
			goto bail;
		}

		for (i = 1; i < 255; ++i) {
			memset(&rcs, 0, sizeof(struct raw_config_request));
			rcs.raw_minor = i;
			if (ioctl(fd, RAW_GETBIND, &rcs) == -1)
				continue;
			if (rcs.block_major == 0)
				break;
		}

		if (i >= 255) {
			LOG_ERROR("unable to find a free raw device /dev/raw/rawXX");
			goto bail;
		}

		*raw_minor = i;
		snprintf(raw_dev, rdlen, "/dev/raw/raw%d", i);

		rcs.raw_minor = i;
		rcs.block_major = (__u64)MAJOR(statbuf.st_rdev);
		rcs.block_minor = (__u64)MINOR(statbuf.st_rdev);
		if (ioctl(fd, RAW_SETBIND, &rcs) == -1) {
			LOG_ERROR("%s", strerror(errno));
			*raw_minor = 0;
			raw_dev[0] = '\0';
			goto bail;
		}
	}

	ret = 0;
bail:
	if (fd)
		close(fd);

	return ret;
}				/* bind_raw */

/*
 * unbind_raw()
 *
 */
void unbind_raw(int raw_minor)
{
	int fd = 0;
	struct raw_config_request rcs;
	
	if (raw_minor == 0)
		goto bail;

	/* printf("Unbinding %d\n", raw_minor); */

	fd = open("/dev/rawctl", O_RDWR);
	if (fd == -1) {
		LOG_ERROR("Error opening /dev/rawctl.\n%s",
			  strerror(errno));
		goto bail;
	}

	rcs.raw_minor = raw_minor;
	rcs.block_major = 0;
	rcs.block_minor = 0;
	if (ioctl(fd, RAW_SETBIND, &rcs) == -1) {
		LOG_ERROR("%s", strerror(errno));
		goto bail;
	}

bail:
	if (fd)
		close(fd);
	return ;
}				/* unbind_raw */

static void signal_message (int sig)
{
#define LINE1 "Abnormal termination!\n"
#define LINE2 "There may be bound raw devices left lying around, please clean them up\n"
#define LINE3 "using the raw(8) command.\n"

  write(2, LINE1, sizeof(LINE1) - 1);
  write(2, LINE2, sizeof(LINE2) - 1);
  write(2, LINE3, sizeof(LINE3) - 1);

  signal(sig, SIG_DFL);

  raise(sig);
}

void init_raw_cleanup_message(void)
{
    signal (SIGHUP, signal_message);
    signal (SIGQUIT, signal_message);
    signal (SIGABRT, signal_message);
    signal (SIGBUS, signal_message);
    signal (SIGSEGV, signal_message);
}
