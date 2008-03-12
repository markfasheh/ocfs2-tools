/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * umount.ocfs2.c  Unounts ocfs2 volume
 *
 * Copyright (C) 2007 Oracle.  All rights reserved.
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

#include "mount.ocfs2.h"
#include "o2cb/o2cb.h"

/* Do not try to include <linux/mount.h> -- lots of errors */
#if !defined(MNT_DETACH)
# define MNT_DETACH 2
#endif

int verbose = 0;
int mount_quiet = 0;
char *progname = NULL;

static int nomtab = 0;

struct mount_options {
	char *dir;
	char *dev;
	int flags;
};

static void handle_signal(int sig)
{
	switch (sig) {
	case SIGTERM:
	case SIGINT:
		printf("\numount interrupted\n");
		exit(1);
	}
}

static void read_options(int argc, char **argv, struct mount_options *mo)
{
	int c;

	progname = basename(argv[0]);

	if (argc < 2)
		return;

	while(1) {
		c = getopt(argc, argv, "flvn");
		if (c == -1)
			break;

		switch (c) {
		case 'v':
			++verbose;
			break;

		case 'n':
			++nomtab;
			break;

		case 'f':
			/*
			 * Do nothing, as we don't support MNT_FORCE.  We
			 * need to handle this option for autofs
			 */
			break;

		case 'l':
			mo->flags |= MNT_DETACH;
			break;

		default:
			break;
		}
	}

	if (optind < argc && argv[optind])
		mo->dir = xstrdup(argv[optind]);
}

static int process_options(struct mount_options *mo)
{
	struct mntentchn *mc;

	if (!mo->dir) {
		com_err(progname, OCFS2_ET_INVALID_ARGUMENT, "no mountpoint specified");
		return -1;
	}

	/*
	 * We need the device to read heartbeat information, etc.  Find
	 * the *last* entry matching our mo->dir.  This may be a mountpoint
	 * or a device, so we try a mountpoint first (the usual case).
	 */
	mc = getmntdirbackward(mo->dir, NULL);
	if (mc) {
		mo->dev = xstrdup(mc->m.mnt_fsname);
	} else {
		mc = getmntdevbackward(mo->dir, NULL);
		if (!mc) {
			fprintf(stderr, "Unable to find %s in mount list\n",
				mo->dir);
			return -1;
		}
		mo->dev = mo->dir;
		mo->dir = xstrdup(mc->m.mnt_dir);
	}

	return 0;
}

static int check_dev_readonly(const char *dev, int *dev_ro)
{
	int fd;
	int ret;

	fd = open(dev, O_RDONLY);
	if (fd < 0)
		return errno;

	ret = ioctl(fd, BLKROGET, dev_ro);
	if (ret < 0)
		return errno;

	close(fd);

	return 0;
}

int main(int argc, char **argv)
{
	int rc;
	errcode_t ret = 0;
	struct mount_options mo;
	ocfs2_filesys *fs = NULL;
	int clustered = 1;

	initialize_ocfs_error_table();
	initialize_o2dl_error_table();
	initialize_o2cb_error_table();

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	if (signal(SIGTERM, handle_signal) == SIG_ERR) {
		fprintf(stderr, "Could not set SIGTERM\n");
		exit(1);
	}

	if (signal(SIGINT, handle_signal) == SIG_ERR) {
		fprintf(stderr, "Could not set SIGINT\n");
		exit(1);
	}

	memset(&mo, 0, sizeof(mo));
	read_options (argc, argv, &mo);

	ret = process_options(&mo);
	if (ret)
		goto bail;

	ret = ocfs2_open(mo.dev, OCFS2_FLAG_RO, 0, 0, &fs); //O_EXCL?
	if (ret) {
		com_err(progname, ret, "while opening device %s", mo.dev);
		goto bail;
	}

	clustered = (0 == ocfs2_mount_local(fs));

	if (verbose)
		printf("device=%s\n", mo.dev);

	if (clustered) {
		ret = o2cb_init();
		if (ret) {
			com_err(progname, ret, "Cannot initialize cluster");
			goto bail;
		}
	}

	block_signals (SIG_BLOCK);

	rc = -ENOSYS;
	if (mo.flags) {
		rc = umount2(mo.dir, mo.flags);
		if (rc) {
			rc = -errno;
			fprintf(stderr, "Error calling umount2(): %s",
				strerror(-rc));
			if ((rc == -ENOSYS) && verbose)
				fprintf(stdout,
					"No umount2(), trying umount()...\n");
		}
	}
	if (rc == -ENOSYS) {
		rc = umount(mo.dir);
		if (rc) {
			rc = -errno;
			fprintf(stderr, "Error unmounting %s: %s\n", mo.dir,
				strerror(-rc));
		}
	}

	if (rc)
		goto unblock;

	if (clustered)
		ocfs2_stop_heartbeat(fs);

	if (!nomtab)
		update_mtab(mo.dir, NULL);

unblock:
	block_signals (SIG_UNBLOCK);

bail:
	if (fs)
		ocfs2_close(fs);
	if (mo.dev)
		free(mo.dev);
	if (mo.dir)
		free(mo.dir);

	return ret ? 1 : 0;
}
