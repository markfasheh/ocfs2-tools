/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * o2cbtool.h
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <signal.h>
#include <ctype.h>

#include <glib.h>

#include "jiterator.h"
#include "o2cb_config.h"

#include "o2cb/o2cb.h"
#include "ocfs2/ocfs2.h"
#include "tools-internal/verbose.h"
#include "tools-internal/utils.h"

#define O2CB_DEFAULT_CONFIG_FILE	"/etc/ocfs2/cluster.conf"
#define O2CB_DEFAULT_IP_PORT		7777

struct o2cb_command {
	int o_modified;
	int o_argc;
	char **o_argv;
	char *o_name;
	char *o_help;
	char *o_usage;
	char *o_config_file;
	O2CBConfig *o_config;
	errcode_t (*o_action)(struct o2cb_command *);
};

enum {
	CONFIG_FILE_OPTION = CHAR_MAX + 1,
	IP_OPTION,
	PORT_OPTION,
	NODENUM_OPTION,
};

errcode_t o2cbtool_validate_clustername(char *clustername);

errcode_t o2cbtool_add_cluster(struct o2cb_command *cmd);
errcode_t o2cbtool_remove_cluster(struct o2cb_command *cmd);

errcode_t o2cbtool_add_node(struct o2cb_command *cmd);
errcode_t o2cbtool_remove_node(struct o2cb_command *cmd);
