/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * utils.c
 *
 * utility functions for o2info
 *
 * Copyright (C) 2010 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#define _XOPEN_SOURCE 600
#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE /* Because libc really doesn't want us using O_DIRECT? */

#include <unistd.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>

#include "ocfs2/ocfs2.h"
#include "tools-internal/verbose.h"

#include "utils.h"

errcode_t o2info_open(struct o2info_method *om, int flags)
{
	errcode_t err = 0;
	int fd, open_flags;
	ocfs2_filesys *fs = NULL;

	if (om->om_method == O2INFO_USE_LIBOCFS2) {
		open_flags = flags|OCFS2_FLAG_HEARTBEAT_DEV_OK|OCFS2_FLAG_RO;
		err = ocfs2_open(om->om_path, open_flags, 0, 0, &fs);
		if (err) {
			tcom_err(err, "while opening device %s", om->om_path);
			goto out;
		}
		om->om_fs = fs;
	} else {
		open_flags = flags | O_RDONLY;
		fd = open(om->om_path, open_flags);
		if (fd < 0) {
			err = errno;
			tcom_err(err, "while opening file %s", om->om_path);
			goto out;
		}
		om->om_fd = fd;
	}

out:
	return err;
}

errcode_t o2info_close(struct o2info_method *om)
{
	errcode_t err = 0;
	int rc = 0;

	if (om->om_method == O2INFO_USE_LIBOCFS2) {
		if (om->om_fs) {
			err = ocfs2_close(om->om_fs);
			if (err) {
				tcom_err(err, "while closing device");
				goto out;
			}
		}
	} else {
		if (om->om_fd >= 0) {
			rc = close(om->om_fd);
			if (rc < 0) {
				rc = errno;
				tcom_err(rc, "while closing fd: %d.\n",
					 om->om_fd);
				err = rc;
			}
		}
	}

out:
	return err;
}

int o2info_method(const char *path)
{
	int rc;
	struct stat st;

	rc = stat(path, &st);
	if (rc < 0) {
		tcom_err(errno, "while stating %s", path);
		goto out;
	}

	rc = O2INFO_USE_IOCTL;
	if ((S_ISBLK(st.st_mode)) || (S_ISCHR(st.st_mode)))
		rc = O2INFO_USE_LIBOCFS2;

out:
	return rc;
}
