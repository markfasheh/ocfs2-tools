/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * check.c
 *
 * OCFS2 format check utility
 *
 * Copyright (C) 2005 Oracle.  All rights reserved.
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
 */

#include "mkfs.h"

#define WHOAMI "mkfs.ocfs2"

/* For ocfs2_fill_cluster_information().  Errors are to be ignored */
static void cluster_fill(char **stack_name, char **cluster_name)
{
	errcode_t err;
	char **clusters = NULL;
	const char *name = NULL;

	err = o2cb_init();
	if (err)
		return;

	err = o2cb_get_stack_name(&name);
	if (err)
		return;

	*stack_name = strdup(name);

	err = o2cb_list_clusters(&clusters);
	if (err)
		return;

	/* The first cluster is the default cluster */
	if (clusters[0])
		*cluster_name = strdup(clusters[0]);

	o2cb_free_cluster_list(clusters);
}

/* For ocfs2_fill_cluster_information().  Errors are to be ignored */
static void disk_fill(const char *device, char **stack_name,
		      char **cluster_name)
{
	errcode_t err;
	ocfs2_filesys *fs = NULL;
	struct o2cb_cluster_desc desc;

	err = ocfs2_open(device, OCFS2_FLAG_RO, 0, 0, &fs);
	if (err)
		return;

	if (!ocfs2_userspace_stack(OCFS2_RAW_SB(fs->fs_super))) {
		*stack_name = strdup("o2cb");
		goto close;
	}

	err = ocfs2_fill_cluster_desc(fs, &desc);
	if (err)
		goto close;

	*stack_name = strdup(desc.c_stack);
	*cluster_name = strdup(desc.c_cluster);

close:
	ocfs2_close(fs);
}

static int pick_one(State *s, const char *what_is_it,
		    const char *user_value, const char *o2cb_value,
		    const char *disk_value, char **ret_value)
{
	int rc = -1;

	/*
	 * First, compare o2cb and disk values.  If we get past this
	 * block (via match or override), the o2cb value takes precedence.
	 */
	if (disk_value) {
		if (o2cb_value) {
			if (strcmp(o2cb_value, disk_value)) {
				fprintf(stderr,
					"%s is configured to use %s \"%s\", but \"%s\" is currently running.\n"
					"%s will not be able to determine if the filesystem is in use.\n",
					s->device_name, what_is_it,
					disk_value, o2cb_value,
					s->progname);
				if (!s->force) {
					fprintf(stderr,
						"To skip this check, use --force or -F\n");
					goto out;
				}
				fprintf(stdout,
					"Overwrite of disk information forced\n");
			}
		}
	}

	if (user_value) {
		if (o2cb_value) {
			if (strcmp(o2cb_value, user_value)) {
				fprintf(stderr, "%s \"%s\" was requested, but \"%s\" is running.\n",
				what_is_it, user_value, o2cb_value);
				if (!s->force) {
					fprintf(stderr,
						"To skip this check, use --force or -F\n");
					goto out;
				}
				fprintf(stdout, "%s forced\n", what_is_it);
			}
		} else if (disk_value) {
			if (strcmp(disk_value, user_value)) {
				fprintf(stderr, "%s \"%s\" was requested, but %s is configured for \"%s\".\n",
					what_is_it, user_value,
					s->device_name, disk_value);
				if (!s->force) {
					fprintf(stderr,
						"To skip this check, use --force or -F\n");
					goto out;
				}
				fprintf(stderr, "%s forced\n", what_is_it);
			}
		}
		*ret_value = strdup(user_value);
	} else if (o2cb_value)
		*ret_value = strdup(o2cb_value);
	else if (disk_value)
		*ret_value = strdup(disk_value);

	rc = 0;

out:
	return rc;;
}

/*
 * Try to connect to the cluster and look at the disk to fill in default
 * cluster values.  If we can't connect, that's OK for now.  The only errors
 * are when values are missing or conflict with option arguments.
 */
int ocfs2_fill_cluster_information(State *s)
{
	int rc = -1;
	char *user_cluster_name = NULL;
	char *user_stack_name = NULL;
	char *o2cb_cluster_name = NULL;
	char *o2cb_stack_name = NULL;
	char *disk_cluster_name = NULL;
	char *disk_stack_name = NULL;

	if (s->mount == MOUNT_LOCAL)
		return 0;

	cluster_fill(&o2cb_stack_name, &o2cb_cluster_name);
	disk_fill(s->device_name, &disk_stack_name, &disk_cluster_name);
	user_stack_name = s->cluster_stack;
	user_cluster_name = s->cluster_name;

	if (pick_one(s, "cluster stack", user_stack_name, o2cb_stack_name,
		     disk_stack_name, &s->cluster_stack))
		return -1;

	if (pick_one(s, "cluster name", user_cluster_name,
		     o2cb_cluster_name, disk_cluster_name,
		     &s->cluster_name))
		return -1;

	if (s->cluster_stack) {
		if (!strcmp(s->cluster_stack, "o2cb")) {
			/*
			 * We've already checked for conflicts above.  Now
			 * clear out the stack so that fill_super knows
			 * it's a classic filesystem.
			 */
			free(s->cluster_stack);
			s->cluster_stack = NULL;
		} else if (!s->cluster_name) {
			fprintf(stderr,
				"Cluster name required for stack \"%s\".\n",
				s->cluster_stack);
			goto out;
		}
	}
	if (!s->cluster_stack && s->cluster_name) {
		/* The classic stack doesn't write a name */
		free(s->cluster_name);
		s->cluster_name = NULL;
	}
	if (s->cluster_stack)
		fprintf(stdout, "Cluster stack: %s\nCluster name: %s\n",
			s->cluster_stack, s->cluster_name);
	else
		fprintf(stdout, "Cluster stack: classic o2cb\n");

	rc = 0;

out:
	if (user_stack_name)
		free(user_stack_name);
	if (user_cluster_name)
		free(user_cluster_name);
	if (o2cb_stack_name)
		free(o2cb_stack_name);
	if (o2cb_cluster_name)
		free(o2cb_cluster_name);
	if (disk_stack_name)
		free(disk_stack_name);
	if (disk_cluster_name)
		free(disk_cluster_name);

	return rc;
}

int ocfs2_check_volume(State *s)
{
	ocfs2_filesys *fs = NULL;
	errcode_t ret;
	int mount_flags;

	if (ocfs2_fill_cluster_information(s))
		return -1;

	ret = ocfs2_check_if_mounted(s->device_name, &mount_flags);
	if (ret) {
		com_err(s->progname, ret,
			"while determining whether %s is mounted.",
			s->device_name);
		return -1;
	}

	if (mount_flags & OCFS2_MF_MOUNTED) {
		fprintf(stderr, "%s is mounted; ", s->device_name);
		if (s->force) {
			fputs("overwriting anyway. Hope /etc/mtab is "
			      "incorrect.\n", stderr);
			return 1;
		}
		fputs("will not make a ocfs2 volume here!\n", stderr);
		return -1;
	}

	if (mount_flags & OCFS2_MF_BUSY) {
		fprintf(stderr, "%s is apparently in use by the system; ",
			s->device_name);
		if (s->force) {
			fputs("format forced anyway.\n", stderr);
			return 1;
		}
		fputs("will not make a ocfs2 volume here!\n", stderr);
		return -1;
	}

	ret = ocfs2_open(s->device_name, OCFS2_FLAG_RW, 0, 0, &fs);
	if (ret) {
		if (ret == OCFS2_ET_OCFS_REV)
			fprintf(stdout, "Overwriting existing ocfs partition.\n");
		return 0;
	} else
		fprintf(stdout, "Overwriting existing ocfs2 partition.\n");

	if (ocfs2_mount_local(fs))
		goto nolock;

	if (!s->force) {
		ret = o2cb_init();
		if (ret) {
			com_err(s->progname, ret,
				"while initializing the cluster");
			return -1;
		}

		ret = ocfs2_initialize_dlm(fs, WHOAMI);
		if (ret) {
			ocfs2_close(fs);
			com_err(s->progname, ret, "while initializing the dlm");
			fprintf(stderr,
				"As this is an existing OCFS2 volume, it could be mounted on an another node in the cluster.\n"
				"However, as %s is unable to initialize the dlm, it cannot detect if the volume is in use or not.\n"
				"To skip this check, use --force or -F.\n",
				s->progname);
			return -1;
		}

		ret = ocfs2_lock_down_cluster(fs);
		if (ret) {
			ocfs2_shutdown_dlm(fs, WHOAMI);
			ocfs2_close(fs);
			com_err(s->progname, ret, "while locking the cluster");
			fprintf(stderr,
				"This volume appears to be in use in the cluster.\n");
				
			return -1;
		}

		ocfs2_release_cluster(fs);
		ocfs2_shutdown_dlm(fs, WHOAMI);
	} else {
		fprintf(stderr,
			"WARNING: Cluster check disabled.\n");
	}

nolock:
	ocfs2_close(fs);

	return 1;
}
