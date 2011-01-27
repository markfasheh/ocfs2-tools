/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * op_cluster.c
 *
 * Manipulates o2cb cluster configuration
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

errcode_t o2cbtool_validate_clustername(char *clustername)
{
	char *p;
	int len;
	errcode_t ret = O2CB_ET_INVALID_CLUSTER_NAME;

	p = tools_strstrip(clustername);
	len = strlen(p);

	if (!len) {
		tcom_err(O2CB_ET_INVALID_CLUSTER_NAME, "; zero length");
		return ret;
	}

	if (len > OCFS2_CLUSTER_NAME_LEN) {
		tcom_err(ret, "; max %d characters", OCFS2_CLUSTER_NAME_LEN);
		return ret;
	}

	while(isalnum(*p++) && len--);
	if (len) {
		tcom_err(ret, "; only alpha-numeric characters allowed");
		return ret;
	}

	return 0;
}

/*
 * add-cluster <clustername>
 */
errcode_t o2cbtool_add_cluster(struct o2cb_command *cmd)
{
	O2CBCluster *cluster;
	errcode_t ret = -1;
	gchar *clustername;

	if (cmd->o_argc < 2)
		goto bail;

	cmd->o_print_usage = 0;

	clustername = cmd->o_argv[1];

	ret = o2cbtool_validate_clustername(clustername);
	if (ret)
		goto bail;

	cluster = o2cb_config_add_cluster(cmd->o_config, clustername);
	if (!cluster) {
		errorf("Cluster '%s' already exists\n", clustername);
		goto bail;
	}

	cmd->o_modified = 1;
	ret = 0;
	verbosef(VL_APP, "Added cluster '%s'\n", clustername);

bail:
	return ret;
}

/*
 * remove-cluster <clustername>
 */
errcode_t o2cbtool_remove_cluster(struct o2cb_command *cmd)
{
	errcode_t ret = -1;
	gchar *clustername;

	if (cmd->o_argc < 2 || !strlen(tools_strstrip(cmd->o_argv[1])))
		goto bail;

	cmd->o_print_usage = 0;

	clustername = cmd->o_argv[1];

	ret = o2cb_config_remove_cluster(cmd->o_config, clustername);
	if (ret) {
		errorf("Unknown cluster '%s'\n", clustername);
		goto bail;
	}

	cmd->o_modified = 1;
	ret = 0;
	verbosef(VL_APP, "Removed cluster '%s'\n", clustername);

bail:
	return ret;
}
