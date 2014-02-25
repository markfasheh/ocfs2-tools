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

int is_classic_stack(char *stack_name)
{
	return !strcmp(stack_name, OCFS2_CLASSIC_CLUSTER_STACK);
}

/* For ocfs2_fill_cluster_information().  Errors are to be ignored */
void cluster_fill(char **stack_name, char **cluster_name, uint8_t *stack_flags)
{
	errcode_t err;
	struct o2cb_cluster_desc cluster;

	*stack_name = NULL;
	*cluster_name = NULL;
	*stack_flags = 0;

	err = o2cb_init();
	if (err)
		return;

	err = o2cb_running_cluster_desc(&cluster);
	if (err)
		return;

	if (cluster.c_stack) {
		/*
		 * These were allocated by o2cb_running_cluster_desc(),
		 * the caller will free them.
		 */
		*stack_name = cluster.c_stack;
		*cluster_name = cluster.c_cluster;
		*stack_flags = cluster.c_flags;
	}
}

/* For ocfs2_fill_cluster_information().  Errors are to be ignored */
static void disk_fill(const char *device, char **stack_name,
		      char **cluster_name, uint8_t *stack_flags)
{
	errcode_t err;
	ocfs2_filesys *fs = NULL;
	struct o2cb_cluster_desc desc;

	*stack_name = NULL;
	*cluster_name = NULL;
	*stack_flags = 0;

	err = ocfs2_open(device, OCFS2_FLAG_RO, 0, 0, &fs);
	if (err)
		return;

	if (!ocfs2_clusterinfo_valid(OCFS2_RAW_SB(fs->fs_super)))
		goto close;

	err = ocfs2_fill_cluster_desc(fs, &desc);
	if (err)
		goto close;

	*stack_name = strdup(desc.c_stack);
	*cluster_name = strdup(desc.c_cluster);
	*stack_flags = desc.c_flags;

close:
	ocfs2_close(fs);
}

static int check_cluster_compatibility(State *s, char *active, char *other,
				       const char *other_desc)
{
	int ret = -1;

	if (strlen(other) && strlen(active) && strcmp(active, other)) {
		fprintf(stderr,
			"%s cluster (%s) does not match the active cluster "
			"(%s).\n%s will not be able to determine if this "
			"operation can be done safely.\n",
			other_desc, other, active, s->progname);
		if (!s->force) {
			fprintf(stderr, "To skip this check, use --force or -F\n");
			goto out;
		}
		fprintf(stdout, "Format is forced.\n");
	}

	ret = 0;
out:
	return ret;
}

/*
 * Try to connect to the cluster and look at the disk to fill in default
 * cluster values.  If we can't connect, that's OK for now.  The only errors
 * are when values are missing or conflict with option arguments.
 *
 * This function assumes that each set of cluster stack values (stack and
 * cluster name) are either both set or both unset. As in, if the user
 * specifies a cluster stack, he must specify the cluster name too.
 */
int ocfs2_fill_cluster_information(State *s)
{
	char *user_cluster_name, *user_stack_name, user_value[100];
	char *o2cb_cluster_name, *o2cb_stack_name, o2cb_value[100];
	char *disk_cluster_name, *disk_stack_name, disk_value[100];
	uint8_t user_stack_flags, o2cb_stack_flags, disk_stack_flags;
	int clusterinfo = 0, userspace = 0;
	int ret = -1;

	if (s->mount == MOUNT_LOCAL)
		return 0;

	*user_value = *o2cb_value = *disk_value = '\0';

	/* get currently active cluster stack */
	cluster_fill(&o2cb_stack_name, &o2cb_cluster_name, &o2cb_stack_flags);

	/* get cluster stack configured on disk */
	disk_fill(s->device_name, &disk_stack_name, &disk_cluster_name,
		  &disk_stack_flags);

	/* cluster stack as provided by the user */
	user_stack_name = s->cluster_stack;
	user_cluster_name = s->cluster_name;
	user_stack_flags = s->stack_flags;

	s->cluster_stack = s->cluster_name = NULL;
	s->stack_flags = 0;

	/*
	 * If the user specifies global heartbeat, while we can assume o2cb
	 * stack, we still need to find the cluster name.
	 */
	if (s->global_heartbeat && !user_stack_name) {
		if (!o2cb_stack_name) {
			com_err(s->progname, 0, "Global heartbeat cannot be "
				"enabled without either starting the o2cb "
				"cluster stack or providing the cluster stack "
				"info.");
			goto out;
		}
		if (strcmp(o2cb_stack_name, OCFS2_CLASSIC_CLUSTER_STACK)) {
			com_err(s->progname, 0, "Global heartbeat is "
				"incompatible with the active cluster stack "
				"\"%s\".\n", o2cb_stack_name);
			goto out;
		}

		user_stack_name = strdup(o2cb_stack_name);
		user_cluster_name = strdup(o2cb_cluster_name);
		user_stack_flags |= OCFS2_CLUSTER_O2CB_GLOBAL_HEARTBEAT;
	}

	/* User specifically asked for clusterinfo */
	if (s->feature_flags.opt_incompat & OCFS2_FEATURE_INCOMPAT_CLUSTERINFO)
		clusterinfo++;

	/* User specifically asked for usersapce stack */
	if (s->feature_flags.opt_incompat &
	    OCFS2_FEATURE_INCOMPAT_USERSPACE_STACK)
		userspace++;

	/* merge - easier to compare */
	if (user_stack_name && user_cluster_name) {
		snprintf(user_value, sizeof(user_value), "%s,%s,%d",
			 user_stack_name, user_cluster_name, user_stack_flags);
	}
	if (o2cb_stack_name && o2cb_cluster_name) {
		snprintf(o2cb_value, sizeof(o2cb_value), "%s,%s,%d",
			 o2cb_stack_name, o2cb_cluster_name, o2cb_stack_flags);
	}
	if (disk_stack_name && disk_cluster_name) {
		snprintf(disk_value, sizeof(disk_value), "%s,%s,%d",
			 disk_stack_name, disk_cluster_name, disk_stack_flags);
	}

	/* if disk and o2cb are not the same, continue only if force set */
	if (check_cluster_compatibility(s, o2cb_value, disk_value, "On disk"))
		goto out;

	/* if user and o2cb are not the same, continue only if force set */
	if (check_cluster_compatibility(s, o2cb_value, user_value,
					"User requested"))
		goto out;

	if (strlen(user_value)) {
		s->cluster_stack = strdup(user_stack_name);
		s->cluster_name = strdup(user_cluster_name);
		s->stack_flags = user_stack_flags;
	} else if (strlen(o2cb_value)) {
		s->cluster_stack = strdup(o2cb_stack_name);
		s->cluster_name = strdup(o2cb_cluster_name);
		s->stack_flags = o2cb_stack_flags;
	} else if (strlen(disk_value)) {
		s->cluster_stack = strdup(disk_stack_name);
		s->cluster_name = strdup(disk_cluster_name);
		s->stack_flags = disk_stack_flags;
	} else { /* default */
		if (clusterinfo || userspace) {
			fprintf(stderr, "The clusterinfo or userspace stack "
				"features cannot be enabled. Please rerun with "
				"the cluster stack details or after starting "
				"the cluster stack.\n");
			goto out;
		}
	}

	/*
	 * If it is the o2cb stack and the user has not specifically asked
	 * for the clusterinfo feature, then go default.
	 */
	if (!strlen(user_value) && s->cluster_stack) {
		if (is_classic_stack(s->cluster_stack) && !clusterinfo &&
		    !s->stack_flags) {
			free(s->cluster_stack);
			free(s->cluster_name);
			s->cluster_stack = s->cluster_name = NULL;
			s->stack_flags = 0;
		}
	}

	if (s->cluster_stack) {
		fprintf(stdout,
			"Cluster stack: %s\n"
			"Cluster name: %s\n"
			"Stack Flags: 0x%x\n"
			"NOTE: Feature extended slot map may be enabled\n",
			s->cluster_stack, s->cluster_name, s->stack_flags);
	} else
		fprintf(stdout, "Cluster stack: classic o2cb\n");

	ret = 0;

out:
	free(user_cluster_name);
	free(user_stack_name);
	free(o2cb_cluster_name);
	free(o2cb_stack_name);
	free(disk_cluster_name);
	free(disk_stack_name);
	return ret;
}

int ocfs2_check_volume(State *s)
{
	ocfs2_filesys *fs = NULL;
	errcode_t ret;
	int mount_flags;

	if (s->dry_run) {
		fprintf(stdout, "Dry run\n");
		return 0;
	}

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
	if ((ret == OCFS2_ET_UNSUPP_FEATURE) ||
	    (ret == OCFS2_ET_RO_UNSUPP_FEATURE)) {
		com_err(s->progname, ret, "while opening device \"%s\"",
			s->device_name);
		if (!s->force) {
			fprintf(stderr,
				"As this is an existing OCFS2 volume, it could be mounted on an another node in the cluster.\n"
				"However, as %s is unable to read the superblock, it cannot detect if the volume is in use or not.\n"
				"To skip this check, use --force or -F.\n",
				s->progname);
			return -1;
		} else {
			fprintf(stderr,
				"WARNING: Cluster check disabled.\n");
			return 1;
		}
	} else if (ret) {
		if (ret == OCFS2_ET_OCFS_REV)
			fprintf(stdout, "Overwriting existing ocfs partition.\n");
		return 0;
	} else
		fprintf(stdout, "Overwriting existing ocfs2 partition.\n");

	if (ocfs2_mount_local(fs))
		goto nolock;

	if (!s->force) {
		if (s->cluster_stack) {
			ret = o2cb_setup_stack(s->cluster_stack);
			if (ret) {
				com_err(s->progname, ret,
					"while setting up stack\n");
				return -1;
			}
		}

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
