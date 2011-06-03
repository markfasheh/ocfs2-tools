/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * o2info.c
 *
 * Ocfs2 utility to gather and report fs information
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

#define _XOPEN_SOURCE 600
#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE /* Because libc really doesn't want us using O_DIRECT? */

#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <getopt.h>
#include <assert.h>

#include "ocfs2/ocfs2.h"
#include "ocfs2-kernel/ocfs2_ioctl.h"
#include "ocfs2-kernel/kernel-list.h"
#include "tools-internal/verbose.h"

#include "utils.h"

extern struct o2info_operation fs_features_op;
extern struct o2info_operation volinfo_op;
extern struct o2info_operation mkfs_op;
extern struct o2info_operation freeinode_op;
extern struct o2info_operation freefrag_op;
extern struct o2info_operation space_usage_op;

static LIST_HEAD(o2info_op_task_list);
static int o2info_op_task_count;
int cluster_coherent;

void print_usage(int rc);
static int help_handler(struct o2info_option *opt, char *arg)
{
	print_usage(0);
	exit(0);
}

static int version_handler(struct o2info_option *opt, char *arg)
{
	tools_version();
	exit(0);
}

static int coherency_handler(struct o2info_option *opt, char *arg)
{
	cluster_coherent = 1;

	return 0;
}

static struct o2info_option help_option = {
	.opt_option	= {
		.name		= "help",
		.val		= 'h',
		.has_arg	= 0,
		.flag		= NULL,
	},
	.opt_help	= NULL,
	.opt_handler	= help_handler,
	.opt_op		= NULL,
	.opt_private = NULL,
};

static struct o2info_option version_option = {
	.opt_option	= {
		.name		= "version",
		.val		= 'V',
		.has_arg	= 0,
		.flag		= NULL,
	},
	.opt_help	= NULL,
	.opt_handler	= version_handler,
	.opt_op		= NULL,
	.opt_private = NULL,
};

static struct o2info_option coherency_option = {
	.opt_option	= {
		.name		= "cluster-coherent",
		.val		= 'C',
		.has_arg	= 0,
		.flag		= NULL,
	},
	.opt_help	=
		"-C|--cluster-coherent",
	.opt_handler	= coherency_handler,
	.opt_op		= NULL,
	.opt_private = NULL,
};

static struct o2info_option fs_features_option = {
	.opt_option	= {
		.name		= "fs-features",
		.val		= CHAR_MAX,
		.has_arg	= 0,
		.flag		= NULL,
	},
	.opt_help	= "   --fs-features",
	.opt_handler	= NULL,
	.opt_op		= &fs_features_op,
	.opt_private	= NULL,
};

static struct o2info_option volinfo_option = {
	.opt_option	= {
		.name		= "volinfo",
		.val		= CHAR_MAX,
		.has_arg	= 0,
		.flag		= NULL,
	},
	.opt_help	= "   --volinfo",
	.opt_handler	= NULL,
	.opt_op		= &volinfo_op,
	.opt_private	= NULL,
};

static struct o2info_option mkfs_option = {
	.opt_option	= {
		.name		= "mkfs",
		.val		= CHAR_MAX,
		.has_arg	= 0,
		.flag		= NULL,
	},
	.opt_help	= "   --mkfs",
	.opt_handler	= NULL,
	.opt_op		= &mkfs_op,
	.opt_private	= NULL,
};

static struct o2info_option freeinode_option = {
	.opt_option	= {
		.name		= "freeinode",
		.val		= CHAR_MAX,
		.has_arg	= 0,
		.flag		= NULL,
	},
	.opt_help	= "   --freeinode",
	.opt_handler	= NULL,
	.opt_op		= &freeinode_op,
	.opt_private	= NULL,
};

static struct o2info_option freefrag_option = {
	.opt_option	= {
		.name		= "freefrag",
		.val		= CHAR_MAX,
		.has_arg	= 1,
		.flag		= NULL,
	},
	.opt_help	= "   --freefrag <chunksize in KB>",
	.opt_handler	= NULL,
	.opt_op		= &freefrag_op,
	.opt_private	= NULL,
};

static struct o2info_option space_usage_option = {
	.opt_option	= {
		.name		= "space-usage",
		.val		= CHAR_MAX,
		.has_arg	= 0,
		.flag		= NULL,
	},
	.opt_help	= "   --space-usage",
	.opt_handler	= NULL,
	.opt_op		= &space_usage_op,
	.opt_private	= NULL,
};

static struct o2info_option *options[] = {
	&help_option,
	&version_option,
	&coherency_option,
	&fs_features_option,
	&volinfo_option,
	&mkfs_option,
	&freeinode_option,
	&freefrag_option,
	&space_usage_option,
	NULL,
};

void print_usage(int rc)
{
	int i;
	enum tools_verbosity_level level = VL_ERR;

	if (!rc)
		level = VL_OUT;

	verbosef(level, "Usage: %s [options] <device or file>\n",
		 tools_progname());
	verbosef(level, "       %s -h|--help\n", tools_progname());
	verbosef(level, "       %s -V|--version\n", tools_progname());
	verbosef(level, "[options] can be followings:\n");

	for (i = 0; options[i]; i++) {
		if (options[i]->opt_help)
			verbosef(level, "\t%s\n", options[i]->opt_help);
	}

	exit(rc);
}

static int build_options(char **optstring, struct option **longopts)
{
	errcode_t err;
	int i, num_opts, rc = 0;
	int unprintable_counter;
	size_t optstring_len;
	char *p, *str = NULL;
	struct option *lopts = NULL;
	struct o2info_option *opt;

	unprintable_counter = 1;	/* Start unique at CHAR_MAX + 1*/
	optstring_len = 1;		/* For the leading ':' */
	for (i = 0; options[i]; i++) {
		opt = options[i];

		/*
		 * Any option with a val of CHAR_MAX wants an unique but
		 * unreadable ->val.  Only readable characters go into
		 * optstring.
		 */
		if (opt->opt_option.val == CHAR_MAX) {
			opt->opt_option.val =
				CHAR_MAX + unprintable_counter;
			unprintable_counter++;
			continue;
		}

		/*
		 * A given option has a single character in optstring.
		 * If it takes a mandatory argument, has_arg==1 and you add
		 * a ":" to optstring.  If it takes an optional argument,
		 * has_arg==2 and you add "::" to optstring.  Thus,
		 * 1 + has_arg is the total space needed in opstring.
		 */
		optstring_len += 1 + opt->opt_option.has_arg;
	}

	num_opts = i;

	err = ocfs2_malloc0(sizeof(char) * (optstring_len + 1), &str);
	if (!err)
		err = ocfs2_malloc(sizeof(struct option) * (num_opts + 1),
				   &lopts);
	if (err) {
		rc = -ENOMEM;
		goto out;
	}

	p = str;
	*p++ = ':';
	for (i = 0; options[i]; i++) {
		assert(p < (str + optstring_len + 1));
		opt = options[i];

		memcpy(&lopts[i], &opt->opt_option, sizeof(struct option));

		if (opt->opt_option.val >= CHAR_MAX)
			continue;

		*p = opt->opt_option.val;
		p++;
		if (opt->opt_option.has_arg > 0) {
			*p = ':';
			p++;
		}
		if (opt->opt_option.has_arg > 1) {
			*p = ':';
			p++;
		}
	}

	/*
	 * Fill last entry of options with zeros.
	 */
	memset(&lopts[i], 0, sizeof(struct option));

out:
	if (!rc) {
		*optstring = str;
		*longopts = lopts;
	} else {
		if (str)
			free(str);
		if (lopts)
			free(lopts);
	}

	return rc;
}

static struct o2info_option *find_option_by_val(int val)
{
	int i;
	struct o2info_option *opt = NULL;

	for (i = 0; options[i]; i++) {
		if (options[i]->opt_option.val == val) {
			opt = options[i];
			break;
		}
	}

	return opt;
}

static errcode_t o2info_append_task(struct o2info_operation *o2p)
{
	errcode_t err;
	struct o2info_op_task *task;

	err = ocfs2_malloc0(sizeof(struct o2info_op_task), &task);
	if (!err) {
		task->o2p_task = o2p;
		list_add_tail(&task->o2p_list, &o2info_op_task_list);
		o2info_op_task_count++;
	} else
		ocfs2_free(&task);

	return err;
}

static void o2info_free_op_task_list(void)
{
	struct o2info_op_task *task;
	struct list_head *pos, *next;

	if (list_empty(&o2info_op_task_list))
		return;

	list_for_each_safe(pos, next, &o2info_op_task_list) {
		task = list_entry(pos, struct o2info_op_task, o2p_list);
		list_del(pos);
		ocfs2_free(&task);
	}
}

extern int optind, opterr, optopt;
extern char *optarg;
static errcode_t parse_options(int argc, char *argv[], char **device_or_file)
{
	int c, lopt_idx = 0;
	errcode_t err;
	struct option *long_options = NULL;
	char error[PATH_MAX];
	char *optstring = NULL;
	struct o2info_option *opt;

	err = build_options(&optstring, &long_options);
	if (err)
		goto out;

	opterr = 0;
	error[0] = '\0';
	while ((c = getopt_long(argc, argv, optstring,
				long_options, &lopt_idx)) != EOF) {
		opt = NULL;
		switch (c) {
		case '?':
			if (optopt)
				errorf("Invalid option: '-%c'\n", optopt);
			else
				errorf("Invalid option: '%s'\n",
				       argv[optind - 1]);
			print_usage(1);
			break;

		case ':':
			if (optopt < CHAR_MAX)
				errorf("Option '-%c' requires an argument\n",
				       optopt);
			else
				errorf("Option '%s' requires an argument\n",
				       argv[optind - 1]);
			print_usage(1);
			break;

		default:
			opt = find_option_by_val(c);
			if (!opt) {
				errorf("Shouldn't have gotten here: "
				       "option '-%c'\n", c);
				print_usage(1);
			}

			if (optarg)
				opt->opt_private = (void *)optarg;

			break;
		}

		if (opt->opt_set) {
			errorf("Option '-%c' specified more than once\n",
			       c);
			print_usage(1);
		}

		opt->opt_set = 1;
		/*
		 * Handlers for simple options such as showing version,
		 * printing the usage, or specify the coherency etc.
		 */
		if (opt->opt_handler) {
			if (opt->opt_handler(opt, optarg))
				print_usage(1);
		}

		/*
		 * Real operation will be added to a list to run later.
		 */
		if (opt->opt_op) {
			opt->opt_op->to_private = opt->opt_private;
			err = o2info_append_task(opt->opt_op);
			if (err)
				goto out;
		}
	}

	if (optind == 1)
		print_usage(1);

	if (optind >= argc) {
		errorf("No device or file specified\n");
		print_usage(1);
	}

	*device_or_file = strdup(argv[optind]);
	if (!*device_or_file) {
		errorf("No memory for allocation\n");
		goto out;
	}

	optind++;

	if (optind < argc) {
		errorf("Too many arguments\n");
		print_usage(1);
	}

out:
	if (optstring)
		ocfs2_free(&optstring);

	if (long_options)
		ocfs2_free(&long_options);

	return err;
}

static errcode_t o2info_run_task(struct o2info_method *om)
{
	struct list_head *p, *n;
	struct o2info_op_task *task;

	list_for_each_safe(p, n, &o2info_op_task_list) {
		task = list_entry(p, struct o2info_op_task, o2p_list);
		task->o2p_task->to_run(task->o2p_task, om,
				       task->o2p_task->to_private);
	}

	return 0;
}

static void handle_signal(int caught_sig)
{
	int exitp = 0, abortp = 0;
	static int segv_already;

	switch (caught_sig) {
	case SIGQUIT:
		abortp = 1;
		/* FALL THROUGH */

	case SIGTERM:
	case SIGINT:
	case SIGHUP:
		errorf("Caught signal %d, exiting\n", caught_sig);
		exitp = 1;
		break;

	case SIGSEGV:
		errorf("Segmentation fault, exiting\n");
		exitp = 1;
		if (segv_already) {
			errorf("Segmentation fault loop detected\n");
			abortp = 1;
		} else
			segv_already = 1;
		break;

	default:
		errorf("Caught signal %d, ignoring\n", caught_sig);
		break;
	}

	if (!exitp)
		return;

	if (abortp)
		abort();

	exit(1);
}

static int setup_signals(void)
{
	int rc = 0;
	struct sigaction act;

	act.sa_sigaction = NULL;
	sigemptyset(&act.sa_mask);
	act.sa_handler = handle_signal;
#ifdef SA_INTERRUPT
	act.sa_flags = SA_INTERRUPT;
#endif
	rc += sigaction(SIGTERM, &act, NULL);
	rc += sigaction(SIGINT, &act, NULL);
	rc += sigaction(SIGHUP, &act, NULL);
	rc += sigaction(SIGQUIT, &act, NULL);
	rc += sigaction(SIGSEGV, &act, NULL);
	act.sa_handler = SIG_IGN;
	rc += sigaction(SIGPIPE, &act, NULL);  /* Get EPIPE instead */

	return rc;
}

static void o2info_init(const char *argv0)
{
	initialize_ocfs_error_table();

	tools_setup_argv0(argv0);

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	if (setup_signals()) {
		errorf("Unable to setup signal handling \n");
		exit(1);
	}

	cluster_coherent = 0;
}

int main(int argc, char *argv[])
{
	int rc = 0;

	char *device_or_file = NULL;
	static struct o2info_method om;

	o2info_init(argv[0]);
	parse_options(argc, argv, &device_or_file);

	rc = o2info_method(device_or_file);
	if (rc < 0)
		goto out;
	else
		om.om_method = rc;

	strncpy(om.om_path, device_or_file, PATH_MAX);

	rc = o2info_open(&om, 0);
	if (rc)
		goto out;

	rc = o2info_run_task(&om);
	if (rc)
		goto out;

	o2info_free_op_task_list();

	rc = o2info_close(&om);
out:
	if (device_or_file)
		ocfs2_free(&device_or_file);

	return rc;
}
