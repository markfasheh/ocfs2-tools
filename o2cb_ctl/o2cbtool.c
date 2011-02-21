/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * o2cbtool.c
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

char *progname = "o2cbtool";

struct o2cb_command o2cbtool_cmds[] = {
	{
		.o_name = "add-cluster",
		.o_action = o2cbtool_add_cluster,
		.o_usage = "<clustername>",
		.o_help = "Add cluster to the config file.",
	},
	{
		.o_name = "remove-cluster",
		.o_action = o2cbtool_remove_cluster,
		.o_usage = "<clustername>",
		.o_help = "Removes cluster from the config file.",
	},
	{
		.o_name = "add-node",
		.o_action = o2cbtool_add_node,
		.o_usage = "[--ip <ip>] [--port <port>] [--number <num>] "
			"<clustername> <nodename>",
		.o_help = "Adds a node to the cluster in the config file.",
	},
	{
		.o_name = "remove-node",
		.o_action = o2cbtool_remove_node,
		.o_usage = "<clustername> <nodename>",
		.o_help = "Removes a node from the cluster in the config file.",
	},
	{
		.o_name = NULL,
		.o_action = NULL,
	},
};

#define USAGE_STR	\
	"[--config-file=path] [-h|--help] [-v|--verbose] [-V|--version] " \
	"COMMAND [ARGS]"

static void usage(void)
{
	struct o2cb_command *p = o2cbtool_cmds;

	fprintf(stderr, "usage: %s %s\n", progname, USAGE_STR);
	fprintf(stderr, "\n");
	fprintf(stderr, "The commands are:\n");
	while(p->o_name) {
		fprintf(stderr, "  %-18s  %s\n", p->o_name, p->o_help);
		++p;
	}
	fprintf(stderr, "\n");

	exit(1);
}

static void parse_options(int argc, char *argv[], struct o2cb_command **cmd)
{
	int c, show_version = 0, show_help = 0;
	char *config_file = NULL;
	static struct option long_options[] = {
		{ "config-file", 1, 0, CONFIG_FILE_OPTION },
		{ "help", 0, 0, 'h' },
		{ "verbose", 0, 0, 'v' },
		{ "version", 0, 0, 'V' },
		{ 0, 0, 0, 0 },
	};

	if (argc && *argv)
		progname = basename(argv[0]);

	while (1) {
		c = getopt_long(argc, argv, "+hvV", long_options, NULL);

		if (c == -1)
			break;

		switch (c) {
		case 'h':
			show_help = 1;
			break;

		case 'v':
			tools_verbose();
			break;

		case 'V':
			show_version = 1;
			break;

		case CONFIG_FILE_OPTION:
			config_file = optarg;
			break;

		default:
			usage();
			break;
		}
	}

	if (!config_file)
		config_file = O2CB_DEFAULT_CONFIG_FILE;

	if (show_version) {
		tools_version();
		exit(1);
	}

	if (show_help)
		usage();

	if (optind == argc)
		usage();

	verbosef(VL_APP, "Using config file '%s'\n", config_file);

	for (*cmd = &o2cbtool_cmds[0]; ((*cmd)->o_name); ++(*cmd)) {
		if (!strcmp(argv[optind], (*cmd)->o_name)) {
			(*cmd)->o_argc = argc - optind;
			(*cmd)->o_argv = &(argv[optind]);
			(*cmd)->o_config_file = config_file;
			optind = 0;	/* restart opt processing */
			return;
		}
	}

	errorf("Unknown command '%s'\n", argv[optind]);
	usage();
	exit(1);
}

int main(int argc, char **argv)
{
	errcode_t ret;
	struct o2cb_command *cmd;
	O2CBConfig *oc_config = NULL;

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	initialize_o2cb_error_table();

	tools_setup_argv0(argv[0]);

	parse_options(argc, argv, &cmd);

	ret = o2cb_config_load(cmd->o_config_file, &oc_config);
	if (ret) {
		tcom_err(ret, "while loading cluster configuration "
			 "file '%s'", cmd->o_config_file);
		goto bail;
	}

	cmd->o_config = oc_config;
	cmd->o_modified = 0;

	ret = -1;
	if (!cmd->o_action) {
		errorf("Command '%s' has not been implemented\n", cmd->o_name);
		goto bail;
	}

	ret = cmd->o_action(cmd);

	if (ret || !cmd->o_modified)
		goto bail;

	ret = o2cb_config_store(oc_config, cmd->o_config_file);
	if (ret)
		tcom_err(ret, "while storing the cluster configuration in "
			 "file '%s'", cmd->o_config_file);

bail:
	if (oc_config)
		o2cb_config_free(oc_config);

	return ret;
}
