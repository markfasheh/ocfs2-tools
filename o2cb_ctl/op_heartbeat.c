/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * op_heartbeat.c
 *
 * Manipulates heartbeat info in the o2cb cluster configuration
 *
 * Copyright (C) 2010 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License, version 2,  as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include "o2cbtool.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

static int is_block_device(char *name)
{
	struct stat statbuf;

	if (!stat(name, &statbuf))
		if (S_ISBLK(statbuf.st_mode))
			return 1;
	return 0;
}

static int get_region(char *device, char **region)
{
	ocfs2_filesys *fs;
	errcode_t ret = 0;

	*region = '\0';

	if (!is_block_device(device)) {
		verbosef(VL_DEBUG, "'%s' is not a block device; assuming "
			 "region\n", device);
		*region = strdup(device);
		goto bail;
	}

	verbosef(VL_DEBUG, "Reading region of block device '%s'\n", device);
	ret = ocfs2_open(device, OCFS2_FLAG_RO | OCFS2_FLAG_HEARTBEAT_DEV_OK,
			 0, 0, &fs);
	if (ret) {
		tcom_err(ret, "while reading region on device '%s'", device);
		goto bail;
	}

	*region = strdup(fs->uuid_str);

	ocfs2_close(fs);

bail:
	if (!ret && !*region) {
		ret = O2CB_ET_NO_MEMORY;
		tcom_err(ret, "while copying region");
	}

	if (!ret)
		verbosef(VL_DEBUG, "Heartbeat region '%s'\n", *region);

	return ret;
}

/*
 * o2cb add-heartbeat <clustername> <region|device>
 */
errcode_t o2cbtool_add_heartbeat(struct o2cb_command *cmd)
{
	O2CBCluster *cluster;
	O2CBHeartbeat *hb;
	int ret = -1;
	gchar *clustername, *tmp, *region = '\0';

	if (cmd->o_argc < 3) {
		errorf("usage: %s %s\n", cmd->o_name, cmd->o_usage);
		goto bail;
	}

	clustername = cmd->o_argv[1];
	tmp = tools_strstrip(cmd->o_argv[2]);

	ret = get_region(tmp, &region);
	if (ret)
		goto bail;

	ret = -1;
	cluster = o2cb_config_get_cluster_by_name(cmd->o_config, clustername);
	if (!cluster) {
		errorf("Unknown cluster '%s'\n", clustername);
		goto bail;
	}

	hb = o2cb_cluster_add_heartbeat(cluster, region);
	if (!hb) {
		errorf("Heartbeat region '%s' already exists\n", region);
		goto bail;
	}

	cmd->o_modified = 1;
	ret = 0;
	verbosef(VL_APP, "Added heartbeat region '%s' to cluster '%s'\n",
		 region, clustername);

bail:
	if (region)
		free(region);
	return ret;
}

/*
 * o2cb remove-heartbeat <clustername> <region>
 */
errcode_t o2cbtool_remove_heartbeat(struct o2cb_command *cmd)
{
	O2CBCluster *cluster;
	int ret = -1;
	gchar *clustername, *tmp, *region = '\0';

	if (cmd->o_argc < 3) {
		errorf("usage: %s %s\n", cmd->o_name, cmd->o_usage);
		goto bail;
	}

	clustername = cmd->o_argv[1];
	tmp = cmd->o_argv[2];

	ret = get_region(tmp, &region);
	if (ret)
		goto bail;

	ret = -1;
	cluster = o2cb_config_get_cluster_by_name(cmd->o_config, clustername);
	if (!cluster) {
		errorf("Unknown cluster '%s'\n", clustername);
		goto bail;
	}

	ret = o2cb_cluster_remove_heartbeat(cluster, region);
	if (ret) {
		errorf("Unknown heartbeat region '%s'\n", region);
		goto bail;
	}

	cmd->o_modified = 1;
	ret = 0;
	verbosef(VL_APP, "Removed heartbeat region '%s' from cluster '%s'\n",
		 region, clustername);

bail:
	if (region)
		free(region);
	return ret;
}

/*
 * o2cb heartbeat-mode <clustername> <global|local>
 */
errcode_t o2cbtool_heartbeat_mode(struct o2cb_command *cmd)
{
	O2CBCluster *cluster;
	int ret = -1;
	gchar *clustername, *hbmode;

	if (cmd->o_argc < 3) {
		errorf("usage: %s %s\n", cmd->o_name, cmd->o_usage);
		goto bail;
	}

	clustername = cmd->o_argv[1];
	hbmode = cmd->o_argv[2];

	if (strcmp(hbmode, "global") && strcmp(hbmode, "local")) {
		errorf("usage: %s %s\n", cmd->o_name, cmd->o_usage);
		goto bail;
	}

	cluster = o2cb_config_get_cluster_by_name(cmd->o_config, clustername);
	if (!cluster) {
		errorf("Unknown cluster '%s'\n", clustername);
		goto bail;
	}

	ret = o2cb_cluster_set_heartbeat_mode(cluster, hbmode);
	if (ret) {
		errorf("Could not change heartbeat mode to '%s', ret=%d,\n",
		       hbmode, ret);
		goto bail;
	}

	cmd->o_modified = 1;
	ret = 0;
	verbosef(VL_APP, "Changed heartbeat mode in cluster '%s' to '%s'\n",
		 clustername, hbmode);

bail:
	return ret;
}
