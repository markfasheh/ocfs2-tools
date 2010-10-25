/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * o2info.h
 *
 * o2info operation prototypes.
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

#ifndef __O2INFO_H__
#define __O2INFO_H__

#include <getopt.h>

#include "ocfs2/ocfs2.h"
#include "ocfs2-kernel/kernel-list.h"

enum o2info_method_type {
	O2INFO_USE_LIBOCFS2 = 1,
	O2INFO_USE_IOCTL,
	O2INFO_USE_NUMTYPES
};

struct o2info_method {
	enum o2info_method_type om_method;
	char om_path[PATH_MAX];
	union {
		ocfs2_filesys *om_fs;	/* Use libocfs2 for device */
		int om_fd;		/* Use ioctl for file */
	};
};

struct o2info_operation {
	char            *to_name;
	int             (*to_run)(struct o2info_operation *op,
				 struct o2info_method *om,
				 void *arg);
	void            *to_private;
};

struct o2info_option {
	struct option	opt_option;		/* For getopt_long().  If
						   there is no short
						   option, set .val to
						   CHAR_MAX.  A unique
						   value will be inserted
						   by the code. */
	struct o2info_operation *opt_op;

	char		*opt_help;	/* Help string */
	int		opt_set;	/* Was this option seen */
	int		(*opt_handler)(struct o2info_option *opt, char *arg);
	void		*opt_private;
};

struct o2info_op_task {
	struct list_head o2p_list;
	struct o2info_operation *o2p_task;
};

#define __O2INFO_OP(_name, _run, _private)				\
{									\
	.to_name		= #_name,				\
	.to_run			= _run,					\
	.to_private		= _private				\
}

#define DEFINE_O2INFO_OP(_name, _run, _private)				\
struct o2info_operation	_name##_op =					\
	__O2INFO_OP(_name, _run, _private)

#endif  /* __O2INFO_H__ */
