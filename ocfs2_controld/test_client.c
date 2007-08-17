/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
 *  This copyrighted material is made available to anyone wishing to use,
 *  modify, copy, or redistribute it subject to the terms and conditions
 *  of the GNU General Public License v.2.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <mntent.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

#include "o2cb.h"
#include "ocfs2_fs.h"
#include "ocfs2.h"
#include "o2cb_client_proto.h"



static int parse_status(char **args, int *error, char **error_msg)
{
	int rc = 0;
	long err;
	char *ptr = NULL;

	err = strtol(args[0], &ptr, 10);
	if (ptr && *ptr != '\0') {
		fprintf(stderr, "Invalid error code string: %s", args[0]);
		rc = -EINVAL;
	} else if ((err == LONG_MIN) || (err == LONG_MAX) ||
		   (err < INT_MIN) || (err > INT_MAX)) {
		fprintf(stderr, "Error code %ld out of range", err);
		rc = -ERANGE;
	} else {
		*error_msg = args[1];
		*error = err;
	}

	return rc;
}

static errcode_t fill_uuid(const char *device, char *uuid)
{
	errcode_t err;
	ocfs2_filesys *fs;
	struct o2cb_region_desc desc;

	err = ocfs2_open(device, OCFS2_FLAG_RO, 0, 0, &fs);
	if (err)
		goto out;

	err = ocfs2_fill_heartbeat_desc(fs, &desc);
	ocfs2_close(fs);

	if (!err)
		strncpy(uuid, desc.r_name, OCFS2_VOL_UUID_LEN + 1);
out:
	return err;
}

static int call_mount(int fd, const char *uuid, const char *cluster,
		      const char *device, const char *mountpoint)
{
	int rc;
	int error;
	char *error_msg;
	client_message message;
	char *argv[OCFS2_CONTROLD_MAXARGS + 1];
	char buf[OCFS2_CONTROLD_MAXLINE];

	rc = send_message(fd, CM_MOUNT, OCFS2_FS_NAME, uuid, cluster,
			  device, mountpoint);
	if (rc) {
		fprintf(stderr, "Unable to send MOUNT message: %s\n",
			strerror(-rc));
		goto out;
	}

	rc = receive_message(fd, buf, &message, argv);
	if (rc < 0) {
		fprintf(stderr, "Error reading from daemon: %s\n",
			strerror(-rc));
		goto out;
	}

	switch (message) {
		case CM_STATUS:
			rc = parse_status(argv, &error, &error_msg);
			if (rc) {
				fprintf(stderr, "Bad status message: %s\n",
					strerror(-rc));
				goto out;
			}
			if (error && (error != EALREADY)) {
				rc = -error;
				fprintf(stderr,
					"Error %d from daemon: %s\n",
					error, error_msg);
				goto out;
			}
			break;

		default:
			rc = -EINVAL;
			fprintf(stderr,
				"Unexpected message %s from daemon\n",
				message_to_string(message));
			goto out;
			break;
	}

	/* XXX Here we fake mount */
	/* rc = mount(...); */
	rc = 0;

	rc = send_message(fd, CM_MRESULT, OCFS2_FS_NAME, uuid, rc,
			  mountpoint);
	if (rc) {
		fprintf(stderr, "Unable to send MRESULT message: %s\n",
			strerror(-rc));
		goto out;
	}

	rc = receive_message(fd, buf, &message, argv);
	if (rc < 0) {
		fprintf(stderr, "Error reading from daemon: %s\n",
			strerror(-rc));
		goto out;
	}

	switch (message) {
		case CM_STATUS:
			rc = parse_status(argv, &error, &error_msg);
			if (rc) {
				fprintf(stderr, "Bad status message: %s\n",
					strerror(-rc));
				goto out;
			}
			if (error) {
				rc = -error;
				fprintf(stderr,
					"Error %d from daemon: %s\n",
					error, error_msg);
			}
			break;

		default:
			rc = -EINVAL;
			fprintf(stderr,
				"Unexpected message %s from daemon\n",
				message_to_string(message));
			break;
	}

out:
	return rc;
}

static int call_unmount(int fd, const char *uuid, const char *mountpoint)
{
	int rc = 0;
	int error;
	char *error_msg;
	client_message message;
	char *argv[OCFS2_CONTROLD_MAXARGS + 1];
	char buf[OCFS2_CONTROLD_MAXLINE];
#if 0
	errcode_t err;
	FILE *mntfile;
	struct mntent *entp;
	char device[PATH_MAX + 1];
	char uuid[OCFS2_VOL_UUID_LEN + 1];

	device[0] = '\0';

	mntfile = setmntent("/tmp/fakemtab", "r");
	if (!mntfile) {
		rc = -errno;
		fprintf(stderr, "Unable to open mtab: %s\n",
			strerror(-rc));
		goto out;
	}

	while ((entp = getmntent(mntfile)) != NULL) {
		if (strcmp(entp->mnt_type, OCFS2_FSTYPE))
			continue;
		if (strcmp(entp->mnt_type, mountpoint))
			continue;
		strncpy(device, entp->mnt_fsname, PATH_MAX);
	}
	endmntent(mntfile);

	if (!*device) {
		rc = -ENOENT;
		fprintf(stderr, "Unable to find filesystem %s\n",
			mountpoint);
		goto out;
	}

	err = fill_uuid(device, uuid);
	if (err) {
		com_err("test_client", err,
			"while trying to read uuid from %s", device);
		rc = -EIO;
		goto out;
	}
#endif

	rc = send_message(fd, CM_UNMOUNT, OCFS2_FS_NAME, uuid, mountpoint);
	if (rc) {
		fprintf(stderr, "Unable to send UNMOUNT message: %s\n",
			strerror(-rc));
		goto out;
	}

	rc = receive_message(fd, buf, &message, argv);
	if (rc < 0) {
		fprintf(stderr, "Error reading from daemon: %s\n",
			strerror(-rc));
		goto out;
	}

	switch (message) {
		case CM_STATUS:
			rc = parse_status(argv, &error, &error_msg);
			if (rc) {
				fprintf(stderr, "Bad status message: %s\n",
					strerror(-rc));
				goto out;
			}
			if (error) {
				rc = -error;
				fprintf(stderr,
					"Error %d from daemon: %s\n",
					error, error_msg);
				goto out;
			}
			break;

		default:
			rc = -EINVAL;
			fprintf(stderr,
				"Unexpected message %s from daemon\n",
				message_to_string(message));
			goto out;
			break;
	}

out:
	return rc;
}

enum {
	OP_MOUNT,
	OP_UMOUNT,
};
static int parse_options(int argc, char **argv, int *op, char ***args)
{
	int rc = 0;

	if (argc < 2) {
		fprintf(stderr, "Operation required\n");
		return -EINVAL;
	}

	if (!strcmp(argv[1], "mount")) {
		if (argc == 6) {
			*op = OP_MOUNT;
			*args = argv + 2;
		} else {
			fprintf(stderr, "Invalid number of arguments\n");
			rc = -EINVAL;
		}
	} else if (!strcmp(argv[1], "umount")) {
		if (argc == 4) {
			*op = OP_UMOUNT;
			*args = argv + 2;
		} else {
			fprintf(stderr, "Invalid number of arguments\n");
			rc = -EINVAL;
		}
	} else {
		fprintf(stderr, "Invalid operation: %s\n", argv[1]);
		rc = -EINVAL;
	}

	return rc;
}

int main(int argc, char **argv)
{
	int rc, fd, op;
	char **args;

	rc = parse_options(argc, argv, &op, &args);
	if (rc)
		goto out;

	rc = client_connect();
	if (rc < 0) {
		fprintf(stderr, "Unable to connect to ocfs2_controld: %s\n",
			strerror(-rc));
		goto out;
	}
	fd = rc;

	switch (op) {
		case OP_MOUNT:
			rc = call_mount(fd, args[0], args[1], args[2],
					args[3]);
			break;

		case OP_UMOUNT:
			rc = call_unmount(fd, args[0], args[1]);
			break;

		default:
			fprintf(stderr, "Can't get here!\n");
			rc = -ENOTSUP;
			break;
	}

	close(fd);

out:
	return rc;
}
