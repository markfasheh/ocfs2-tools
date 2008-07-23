/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * ocfs2ne.c
 *
 * ocfs2 tune utility.
 *
 * Copyright (C) 2004, 2008 Oracle.  All rights reserved.
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

#define _GNU_SOURCE /* for getopt_long and O_DIRECT */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include <getopt.h>
#include <assert.h>
#include <errno.h>

#include "ocfs2/ocfs2.h"

#include "libocfs2ne.h"
#include "libocfs2ne_err.h"


/*
 * Why do we have a list of option structures will callbacks instead of
 * a simple switch() statement?  Because the ocfs2ne option set has grown
 * over time, and there are a few operations that can be triggered by
 * more than one option.  For example, -M {cluster|local} is really just
 * clearing or setting the fs feature 'local'.
 *
 * For most argument-free operations, they'll just specify their name and
 * val.  Options with arguments will mostly use generic_handle_arg() as
 * their ->opt_handle().
 */
struct tunefs_option {
	struct option	opt_option;		/* For getopt_long().  If
						   there is no short
						   option, set .val to
						   CHAR_MAX.  A unique
						   value will be inserted
						   by the code. */
	struct tunefs_operation	*opt_op;	/* Operation associated
						   with this option.  This
						   needs to be set if the
						   option has no
						   ->opt_handle() or is
						   using
						   generic_handle_arg() */
	char		*opt_help;	/* Help string */
	int		opt_set;	/* Was this option seen */
	int		(*opt_handle)(struct tunefs_option *opt, char *arg);
	void		*opt_private;
};

/* Things to run */
struct tunefs_run {
	struct list_head	tr_list;
	struct tunefs_operation	*tr_op;
};


extern struct tunefs_operation list_sparse_op;
extern struct tunefs_operation reset_uuid_op;
extern struct tunefs_operation features_op;
extern struct tunefs_operation resize_volume_op;
extern struct tunefs_operation set_journal_size_op;
extern struct tunefs_operation set_label_op;
extern struct tunefs_operation set_slot_count_op;
extern struct tunefs_operation update_cluster_stack_op;

static LIST_HEAD(tunefs_run_list);

static int tunefs_queue_operation(struct tunefs_operation *op)
{
	struct tunefs_run *run;

	run = malloc(sizeof(struct tunefs_run));
	if (!run) {
		errorf("Unable to allocate memory while queuing an "
		       "operation\n");
		return 1;
	}

	run->tr_op = op;
	list_add_tail(&run->tr_list, &tunefs_run_list);
	return 0;
}

static void print_usage(int rc);
static int handle_help(struct tunefs_option *opt, char *arg)
{
	print_usage(0);
	return 1;
}

static int handle_version(struct tunefs_option *opt, char *arg)
{
	tunefs_version();
	exit(0);
	return 1;
}

static int handle_verbosity(struct tunefs_option *opt, char *arg)
{
	int rc = 0;

	switch (opt->opt_option.val)
	{
		case 'v':
			tunefs_verbose();
			break;

		case 'q':
			tunefs_quiet();
			break;

		default:
			errorf("Invalid option to handle_verbosity: %c\n",
			       opt->opt_option.val);
			rc = 1;
			break;
	}

	/* More than one -v or -q is valid */
	opt->opt_set = 0;
	return rc;
}

static int handle_interactive(struct tunefs_option *opt, char *arg)
{
	tunefs_interactive();
	return 0;
}

/*
 * Plain operations just want to have their ->to_parse_option() called.
 * Their tunefs_option can use this function if they set opt_op to the
 * tunefs_operation.
 */
static int generic_handle_arg(struct tunefs_option *opt, char *arg)
{
	int rc;
	struct tunefs_operation *op = opt->opt_op;

	if (!op->to_parse_option) {
		errorf("Option \"%s\" claims it has an argument, but "
		       "operation \"%s\" isn't expecting one\n",
		       opt->opt_option.name, op->to_name);
		return 1;
	}

	rc = op->to_parse_option(arg, op->to_user_data);
	if (!rc)
		rc = tunefs_queue_operation(op);

	return rc;
}

/*
 * The multiple options setting fs_features want to save off their feature
 * string.  They can use this function directly or indirectly.
 */
static int strdup_handle_arg(struct tunefs_option *opt, char *arg)
{
	char *ptr = strdup(arg);

	if (!ptr) {
		errorf("Unable to allocate memory while processing "
		       "options\n");
		return 1;
	}

	opt->opt_private = ptr;
	return 0;
}

static int mount_type_handle_arg(struct tunefs_option *opt, char *arg)
{
	int rc = 0;

	if (!arg) {
		errorf("No mount type specified\n");
		rc = 1;
	} else if (!strcmp(arg, "local"))
		rc = strdup_handle_arg(opt, "local");
	else if (!strcmp(arg, "cluster"))
		rc = strdup_handle_arg(opt, "nolocal");
	else {
		errorf("Invalid mount type: \"%s\"\n", arg);
		rc = 1;
	}

	return rc;
}

static int backup_super_handle_arg(struct tunefs_option *opt, char *arg)
{
	return strdup_handle_arg(opt, "backup-super");
}

struct tunefs_option help_option = {
	.opt_option	= {
		.name	= "help",
		.val	= 'h',
	},
	.opt_handle	= handle_help,
};

struct tunefs_option version_option = {
	.opt_option	= {
		.name	= "version",
		.val	= 'V',
	},
	.opt_handle	= handle_version,
};

struct tunefs_option verbose_option = {
	.opt_option	= {
		.name	= "verbose",
		.val	= 'v',
	},
	.opt_help	=
		"-v|--verbose (increases verbosity; more than one permitted)",
	.opt_handle	= handle_verbosity,
};

struct tunefs_option quiet_option = {
	.opt_option	= {
		.name	= "quiet",
		.val	= 'q',
	},
	.opt_help	=
		"-q|--quiet (decreases verbosity; more than one permitted)",
	.opt_handle	= handle_verbosity,
};

struct tunefs_option interactive_option = {
	.opt_option	= {
		.name	= "interactive",
		.val	= 'i',
	},
	.opt_help	= "-i|--interactive",
	.opt_handle	= handle_interactive,
};

struct tunefs_option list_sparse_option = {
	.opt_option	= {
		.name	= "list-sparse",
		.val	= CHAR_MAX,
	},
	.opt_help	= "   --list-sparse",
	.opt_op		= &list_sparse_op,
};

struct tunefs_option reset_uuid_option = {
	.opt_option	= {
		.name	= "uuid-reset",
		.val	= 'U',
	},
	.opt_help	= "-U|--uuid-reset",
	.opt_op		= &reset_uuid_op,
};

struct tunefs_option update_cluster_stack_option = {
	.opt_option	= {
		.name	= "update-cluster-stack",
		.val	= CHAR_MAX,
	},
	.opt_help	= "   --update-cluster-stack",
	.opt_op		= &update_cluster_stack_op,
};

struct tunefs_option set_slot_count_option = {
	.opt_option	= {
		.name		= "node-slots",
		.val		= 'N',
		.has_arg	= 1,
	},
	.opt_help	= "-N|--node-slots <number-of-node-slots>",
	.opt_handle	= generic_handle_arg,
	.opt_op		= &set_slot_count_op,
};

struct tunefs_option set_label_option = {
	.opt_option	= {
		.name		= "label",
		.val		= 'L',
		.has_arg	= 1,
	},
	.opt_help	= "-L|--label <label>",
	.opt_handle	= generic_handle_arg,
	.opt_op		= &set_label_op,
};

struct tunefs_option mount_type_option = {
	.opt_option	= {
		.name		= "mount",
		.val		= 'M',
		.has_arg	= 1,
	},
	.opt_handle	= mount_type_handle_arg,
};

struct tunefs_option backup_super_option = {
	.opt_option	= {
		.name	= "backup-super",
		.val	= CHAR_MAX,
	},
	.opt_handle	= backup_super_handle_arg,
};

struct tunefs_option features_option = {
	.opt_option	= {
		.name		= "fs-features",
		.val		= CHAR_MAX,
		.has_arg	= 1,
	},
	.opt_help	= "   --fs-features [no]sparse,...",
	.opt_handle	= strdup_handle_arg,
};


/* The order here creates the order in print_usage() */
static struct tunefs_option *options[] = {
	&help_option,
	&version_option,
	&interactive_option,
	&verbose_option,
	&quiet_option,
	&set_label_option,
	&set_slot_count_option,
	&reset_uuid_option,
	&list_sparse_option,
	&update_cluster_stack_option,
	&mount_type_option,
	&backup_super_option,
	&features_option,
	NULL,
};

/*
 * The options listed here all end up setting or clearing filesystem
 * features.  These options must also live in the master options array.
 * When the are processed in parse_options(), they should attach the
 * relevant feature string to opt_private.  The feature strings will be
 * processed at the end of parse_options().
 */
static struct tunefs_option *feature_options[] = {
	&mount_type_option,
	&backup_super_option,
	&features_option,
	NULL,
};

static struct tunefs_option *find_option_by_val(int val)
{
	int i;
	struct tunefs_option *opt = NULL;

	for (i = 0; options[i]; i++) {
		if (options[i]->opt_option.val == val) {
			opt = options[i];
			break;
		}
	}

	return opt;
}

static struct tunefs_option *find_opt_by_name(const char *name)
{
	int i;
	struct tunefs_option *opt = NULL;

	for (i = 0; options[i]; i++) {
		if (!strcmp(options[i]->opt_option.name, name)) {
			opt = options[i];
			break;
		}
	}

	return opt;
}

static void print_usage(int rc)
{
	int i;
	enum tunefs_verbosity_level level = VL_ERR;

	if (!rc)
		level = VL_OUT;

	verbosef(level, "Usage: %s [options] <device> [new-size]\n",
		 tunefs_progname());
	verbosef(level, "       %s -h|--help\n", tunefs_progname());
	verbosef(level, "       %s -V|--version\n", tunefs_progname());
	verbosef(level, "[options] can be any mix of:\n");
	for (i = 0; options[i]; i++) {
		if (options[i]->opt_help)
			verbosef(level, "\t%s\n", options[i]->opt_help);
	}
	verbosef(level, "[new-size] is only valid with the '-S' option\n");
	exit(rc);
}

static int parse_feature_strings(void)
{
	int i, rc;
	char *tmp, *features = NULL;
	struct tunefs_option *opt;
	size_t len, new_features_len, features_len = 0;

	for (i = 0; feature_options[i]; i++) {
		opt = feature_options[i];
		if (!opt->opt_set)
			continue;
		if (!opt->opt_private)
			continue;

		len = strlen(opt->opt_private);
		new_features_len = features_len + len;
		if (features_len)
			new_features_len++;  /* A comma to separate */
		tmp = realloc(features, new_features_len + 1);
		if (!tmp) {
			errorf("Unable to allocate memory while processing "
			       "options\n");
			return 1;
		}
		features = tmp;
		tmp = features + features_len;
		if (features_len) {
			*tmp = ',';
			tmp++;
		}
		strcpy(tmp, opt->opt_private);
		features_len = new_features_len;
	}

	verbosef(VL_APP, "Full feature string is \"%s\"\n", features);
	rc = features_op.to_parse_option(features,
					 features_op.to_user_data);
	if (!rc)
		rc = tunefs_queue_operation(&features_op);

	return rc;
}

static int build_options(char **optstring, struct option **longopts)
{
	int i, num_opts, rc = 0;
	int unprintable_counter;
	size_t optstring_len;
	char *p, *str = NULL;
	struct option *lopts = NULL;
	struct tunefs_option *opt;

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

	str = malloc(sizeof(char) * (optstring_len + 1));
	lopts = malloc(sizeof(struct option) * (num_opts + 1));
	if (!str || !lopts) {
		rc = -ENOMEM;
		goto out;
	}
	memset(str, 0, sizeof(char) * (optstring_len + 1));
	memset(lopts, 0, sizeof(struct option) * (num_opts + 1));

	p = str;
	*p = ':';
	p++;
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


extern int optind, opterr, optopt;
extern char *optarg;
static errcode_t parse_options(int argc, char *argv[])
{
	int c;
	errcode_t err;
	struct option *long_options = NULL;
	char error[PATH_MAX];
	char *optstring = NULL;
	struct tunefs_option *opt;

	err = build_options(&optstring, &long_options);
	if (err)
		goto out;

	opterr = 0;
	error[0] = '\0';
	while ((c = getopt_long(argc, argv, optstring,
				long_options, NULL)) != EOF) {
		opt = NULL;
		switch (c) {
			case '?':
				if (optopt)
					errorf("Invalid option: \'-%c\'\n",
					       optopt);
				else
					errorf("Invalid option: \'%s\'\n",
					       argv[optind - 1]);
				print_usage(1);
				break;

			case ':':
				if (optopt < CHAR_MAX)
					errorf("Option \'-%c\' requires "
					       "an argument\n",
					       optopt);
				else
					errorf("Option \'%s\' requires "
					       "an argument\n",
					       argv[optind - 1]);
				print_usage(1);
				break;

			default:
				opt = find_option_by_val(c);
				if (!opt) {
					errorf("Shouldn't have gotten "
					       "here: option \'-%c\'\n",
					       c);
					print_usage(1);
				}
				break;
		}

		if (opt->opt_set) {
			errorf("Option \'-%c\' specified more than once\n",
			       c);
			print_usage(1);
		}

		opt->opt_set = 1;
		if (opt->opt_handle) {
			if (opt->opt_handle(opt, optarg))
				print_usage(1);
		} else {
			if (tunefs_queue_operation(opt->opt_op)) {
				err = TUNEFS_ET_NO_MEMORY;
				break;
			}
		}
	}

	if (parse_feature_strings())
		print_usage(1);

out:
	return err;
}

static int run_operations(void)
{
	struct list_head *p;
	struct tunefs_run *run;

	list_for_each(p, &tunefs_run_list) {
		run = list_entry(p, struct tunefs_run, tr_list);
		verbosef(VL_OUT, "Performing \"%s\"\n",
			 run->tr_op->to_name);
	}

	return 0;
}

int main(int argc, char *argv[])
{
	int rc = 1;
	errcode_t err;

	tunefs_init(argv[0]);

	err = parse_options(argc, argv);
	if (err) {
		tcom_err(err, "while parsing options");
		goto out;
	}

	rc = run_operations();

out:
	return rc;
}
