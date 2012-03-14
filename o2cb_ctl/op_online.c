/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * op_online.c
 *
 * Onlines and Offlines the cluster
 *
 * Copyright (C) 2010, 2012 Oracle.  All rights reserved.
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

#define O2CB_SYSCONFIG_FILE		"/etc/sysconfig/o2cb"
#define O2CB_ENABLED			0
#define O2CB_STACK			1
#define O2CB_BOOTCLUSTER		2
#define O2CB_HEARTBEAT_THRESHOLD	3
#define O2CB_IDLE_TIMEOUT_MS		4
#define O2CB_KEEPALIVE_DELAY_MS		5
#define O2CB_RECONNECT_DELAY_MS		6

struct o2cb_parameters {
	char *pa_param;
	char *pa_value;
	errcode_t (*pa_setfunc)(char *, char *);
} o2cb_globals[] =
{
/* 0 */	{ "O2CB_ENABLED", NULL, NULL },
/* 1 */	{ "O2CB_STACK", NULL, NULL },
/* 2 */	{ "O2CB_BOOTCLUSTER", NULL, NULL },
/* 3 */	{ "O2CB_HEARTBEAT_THRESHOLD", NULL, o2cb_set_heartbeat_dead_threshold },
/* 4 */	{ "O2CB_IDLE_TIMEOUT_MS", NULL, o2cb_set_idle_timeout },
/* 5 */	{ "O2CB_KEEPALIVE_DELAY_MS", NULL, o2cb_set_keepalive_delay },
/* 6 */	{ "O2CB_RECONNECT_DELAY_MS", NULL, o2cb_set_reconnect_delay },
	{ NULL, NULL },
};

static errcode_t set_o2cb_cluster_attributes(char *cluster_name)
{
	errcode_t ret = 0;
	char *value;
	int i;

	for (i = 0; o2cb_globals[i].pa_param; ++i) {
		if (!o2cb_globals[i].pa_setfunc)
			continue;
		value = o2cb_globals[i].pa_value;
		ret = o2cb_globals[i].pa_setfunc(cluster_name, value);
		if (ret) {
			tcom_err(ret, ": while setting o2cb parameter %s "
				 "to %s", o2cb_globals[i].pa_param,
				 o2cb_globals[i].pa_value);
			goto bail;
		}
	}
bail:
	return ret;
}

static errcode_t validate_o2cb_sysconfig(char *cluster_name)
{
	int ret = 0;

	if (strcmp(o2cb_globals[O2CB_STACK].pa_value,
		   OCFS2_CLASSIC_CLUSTER_STACK)) {
		ret = O2CB_ET_INVALID_STACK_NAME;
		tcom_err(ret, ": the default stack in '%s' is '%s' and "
			 "not '%s'", O2CB_SYSCONFIG_FILE,
			 o2cb_globals[O2CB_STACK].pa_value,
			 OCFS2_CLASSIC_CLUSTER_STACK);
		goto bail;
	}

	if (strcmp(o2cb_globals[O2CB_BOOTCLUSTER].pa_value, cluster_name)) {
		ret = O2CB_ET_INVALID_CLUSTER_NAME;
		tcom_err(ret, ": the default cluster in '%s' is '%s' and "
			 "not '%s'", O2CB_SYSCONFIG_FILE,
			 o2cb_globals[O2CB_BOOTCLUSTER].pa_value, cluster_name);
		goto bail;
	}

bail:
	return ret;
}

static errcode_t read_o2cb_sysconfig_param(char *param, char *value)
{
	int i;

	for (i = 0; o2cb_globals[i].pa_param; ++i) {
		if (strcmp(o2cb_globals[i].pa_param, param))
			continue;
		if (o2cb_globals[i].pa_value)
			free(o2cb_globals[i].pa_value);

		o2cb_globals[i].pa_value = strdup(value);
		if (!o2cb_globals[i].pa_value)
			return O2CB_ET_NO_MEMORY;
	}

	return 0;
}

static void null_hash_and_newline(char *str)
{
	char *p = str;

	while (*p) {
		if (*p == '#' || *p == '\n') {
			*p = '\0';
			break;
		}
		++p;
	}
}

static void parse_o2cb_string(char *str, char **param, char **value)
{
	char *p, *q;

	*param = NULL;
	*value = NULL;

	p = tools_strchomp(str);

	null_hash_and_newline(p);

	if (!strlen(p))
		return ;

	q = strchr(p, '=');
	if (q) {
		*q = '\0';
		++q;
		*param = tools_strchomp(p);
		*value = tools_strchomp(q);
	}
}

static errcode_t load_o2cb_sysconfig(void)
{
	FILE *file = NULL;
	char str[4096];
	char *param, *value;
	errcode_t ret = 0;

	file = fopen(O2CB_SYSCONFIG_FILE, "r");
	if (!file) {
		ret = errno;
		tcom_err(ret, ": while opening %s", O2CB_SYSCONFIG_FILE);
		goto bail;
	}

	while (fgets(str, sizeof(str), file)) {

		parse_o2cb_string(str, &param, &value);
		if (!value || !strlen(value))
			continue;

		ret = read_o2cb_sysconfig_param(param, value);
		if (ret) {
			tcom_err(ret, ": while reading o2cb sysconfig "
				 "parameter %s", param);
			goto bail;
		}
	}

bail:
	if (file)
		fclose(file);
	return ret;
}

/*
 * online-cluster <clustername>
 */
errcode_t o2cbtool_online_cluster(struct o2cb_command *cmd)
{
	errcode_t ret = -1;
	gchar *clustername;

	o2cbtool_block_signals(SIG_BLOCK);

	if (cmd->o_argc < 2)
		goto bail;

	cmd->o_print_usage = 0;

	clustername = cmd->o_argv[1];

	ret = load_o2cb_sysconfig();
	if (ret)
		goto bail;

	ret = validate_o2cb_sysconfig(clustername);

	ret = o2cbtool_register_cluster(cmd);
	if (ret)
		goto bail;

	ret = set_o2cb_cluster_attributes(clustername);
	if (ret)
		goto bail;

	ret = o2cbtool_start_heartbeat(cmd);
	if (ret)
		goto bail;

	system("o2hbmonitor");

bail:
	o2cbtool_block_signals(SIG_UNBLOCK);
	return ret;
}

/*
 * offline-cluster <clustername>
 */
errcode_t o2cbtool_offline_cluster(struct o2cb_command *cmd)
{
	errcode_t ret = -1;
	gchar *clustername;
	struct o2cb_cluster_desc desc;

	if (cmd->o_argc < 2)
		goto bail;

	cmd->o_print_usage = 0;

	clustername = cmd->o_argv[1];

	/* Read active cluster and continue if it matches */

	ret = get_running_cluster(&desc);
	if (ret) {
		tcom_err(ret, "while discovering running cluster stack");
		goto bail;
	}

	if (strcmp(desc.c_stack, OCFS2_CLASSIC_CLUSTER_STACK)) {
		ret = O2CB_ET_INVALID_STACK_NAME;
		tcom_err(ret, ": '%s' cluster stack is not active",
			 OCFS2_CLASSIC_CLUSTER_STACK);
		goto bail;
	}

	if (strcmp(desc.c_cluster, clustername)) {
		ret = O2CB_ET_INVALID_CLUSTER_NAME;
		tcom_err(ret, ": active cluster name '%s' does not match given "
			 "'%s'", desc.c_cluster, clustername);
		goto bail;
	}

	ret = o2cbtool_stop_heartbeat(cmd);
	if (ret)
		goto bail;

	/* Ensure hb is fully stopped */

	ret = o2cbtool_unregister_cluster(cmd);
	if (ret)
		goto bail;

	system("killall -e o2hbmonitor");

bail:
	return ret;
}
