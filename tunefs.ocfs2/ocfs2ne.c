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
#include <inttypes.h>
#include <getopt.h>
#include <assert.h>
#include <errno.h>

#include "ocfs2/ocfs2.h"

#include "libocfs2ne.h"


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
 *
 * If you are adding a new feature flag, do not add an option here.  It
 * should be handled by --fs-features.  Just write a tunefs_feature in
 * ocfs2ne_feature_<name>.c and add it to the list ocfs2ne_features.c.
 * If you are adding an operation, make its option something that stands on
 * its own and can use generic_handle_arg() if it needs an argument.
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
						   generic_handle_arg().
						   If set, opt_op will
						   be added to the run_list
						   when this option is
						   seen. */
	char		*opt_help;	/* Help string */
	int		opt_set;	/* Was this option seen */
	int		(*opt_handle)(struct tunefs_option *opt, char *arg);
	void		*opt_private;
};

/*
 * ocfs2ne lumps all journal options as name[=value] arguments underneath
 * '-J'.  They end up being tunefs_operations, and we link them up here.
 */
struct tunefs_journal_option {
	char			*jo_name;
	char			*jo_help;
	struct tunefs_operation	*jo_op;
};

/* Things to run */
struct tunefs_run {
	struct list_head	tr_list;
	struct tunefs_operation	*tr_op;
};


extern struct tunefs_operation list_sparse_op;
extern struct tunefs_operation query_op;
extern struct tunefs_operation reset_uuid_op;
extern struct tunefs_operation features_op;
extern struct tunefs_operation resize_volume_op;
extern struct tunefs_operation set_journal_size_op;
extern struct tunefs_operation set_journal_block32_op;
extern struct tunefs_operation set_journal_block64_op;
extern struct tunefs_operation set_label_op;
extern struct tunefs_operation set_slot_count_op;
extern struct tunefs_operation update_cluster_stack_op;
extern struct tunefs_operation cloned_volume_op;
extern struct tunefs_operation set_usrquota_sync_interval_op;
extern struct tunefs_operation set_grpquota_sync_interval_op;
extern struct tunefs_operation set_mmp_update_interval_op;

/* List of operations we're going to run */
static LIST_HEAD(tunefs_run_list);

/* Number of operations we're going to run */
static int tunefs_op_count;
/* Progress display for tunefs operations */
static struct tools_progress *tunefs_op_progress;

static struct tunefs_journal_option set_journal_size_option = {
	.jo_name	= "size",
	.jo_help	= "size=<journal-size>",
	.jo_op		= &set_journal_size_op,
};

static struct tunefs_journal_option set_journal_block64_option = {
	.jo_name        = "block64",
	.jo_help        = "block64",
	.jo_op          = &set_journal_block64_op,
};

static struct tunefs_journal_option set_journal_block32_option = {
	.jo_name        = "block32",
	.jo_help        = "block32",
	.jo_op          = &set_journal_block32_op,
};

static struct tunefs_journal_option set_journal_noblock64_option = {
	.jo_name        = "noblock64",
	.jo_help        = "noblock64",
	.jo_op          = &set_journal_block32_op,
};

static struct tunefs_journal_option set_journal_noblock32_option = {
	.jo_name        = "noblock32",
	.jo_help        = "noblock32",
	.jo_op          = &set_journal_block64_op,
};

/* The list of all supported journal options */
static struct tunefs_journal_option *tunefs_journal_options[] = {
	&set_journal_size_option,
	&set_journal_block64_option,
	&set_journal_block32_option,
	&set_journal_noblock64_option,
	&set_journal_noblock32_option,
	NULL,
};


/*
 * Operations are intended to run in the order we see them in the
 * command-line arguments.  As each option is seen, the operation is
 * added with tunefs_append_operation().
 *
 * There are two exceptions.  First, special-cased options (pretty much
 * the feature) will end up at the end because we can't process them
 * until we've seen all command-line arguments.
 *
 * Second, resize is the only user of tunefs_prepend_operation().  We want
 * to grow the filesystem *before* we do anything that might require space!
 */
static errcode_t tunefs_append_operation(struct tunefs_operation *op)
{
	errcode_t err;
	struct tunefs_run *run;

	err = ocfs2_malloc0(sizeof(struct tunefs_run), &run);
	if (!err) {
		run->tr_op = op;
		list_add_tail(&run->tr_list, &tunefs_run_list);
		tunefs_op_count++;
	}

	return err;
}

static errcode_t tunefs_prepend_operation(struct tunefs_operation *op)
{
	errcode_t err;
	struct tunefs_run *run;

	err = ocfs2_malloc0(sizeof(struct tunefs_run), &run);
	if (!err) {
		run->tr_op = op;
		list_add(&run->tr_list, &tunefs_run_list);
		tunefs_op_count++;
	}

	return err;
}


static void print_usage(int rc);
static int handle_help(struct tunefs_option *opt, char *arg)
{
	print_usage(0);
	return 1;
}

static int handle_version(struct tunefs_option *opt, char *arg)
{
	tools_version();
	exit(0);
	return 1;
}

static int handle_verbosity(struct tunefs_option *opt, char *arg)
{
	int rc = 0;

	switch (opt->opt_option.val)
	{
		case 'v':
			tools_verbose();
			break;

		case 'q':
			tools_quiet();
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
	tools_interactive();
	return 0;
}

static int handle_progress(struct tunefs_option *opt, char *arg)
{
	tools_progress_enable();
	return 0;
}

static int handle_answer(struct tunefs_option *opt, char *arg)
{
	int rc = 0;

	switch (opt->opt_option.val)
	{
		case 'y':
			tools_interactive_yes();
			break;

		case 'n':
			tools_interactive_no();
			break;

		default:
			errorf("Invalid option to handle_answer: %c\n",
			       opt->opt_option.val);
			rc = 1;
			break;
	}

	return rc;
}

/*
 * Plain operations just want to have their ->to_parse_option() called.
 * Their tunefs_option can use this function if they set opt_op to the
 * tunefs_operation.
 */
static int generic_handle_arg(struct tunefs_option *opt, char *arg)
{
	struct tunefs_operation *op = opt->opt_op;

	if (!op->to_parse_option) {
		errorf("Option \"%s\" claims it has an argument, but "
		       "operation \"%s\" isn't expecting one\n",
		       opt->opt_option.name, op->to_name);
		return 1;
	}

	return op->to_parse_option(op, arg);
}

/*
 * Store a copy of the argument on opt_private.
 *
 * For example, the multiple options setting fs_features want to save off
 * their feature string.  They use this function directly or indirectly.
 */
static int strdup_handle_arg(struct tunefs_option *opt, char *arg)
{
	char *ptr = NULL;

	if (arg) {
		ptr = strdup(arg);
		if (!ptr) {
			errorf("Unable to allocate memory while processing "
			       "options\n");
			return 1;
		}
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

static struct tunefs_journal_option *find_journal_option(char *name)
{
	int i;
	struct tunefs_journal_option *jopt;

	for (i = 0; tunefs_journal_options[i]; i++) {
		jopt = tunefs_journal_options[i];
		if (!strcmp(name, jopt->jo_name))
			return jopt;
	}

	return NULL;
}

/* derived from e2fsprogs */
static int handle_journal_arg(struct tunefs_option *opt, char *arg)
{
	errcode_t err;
	int i, rc = 0;
	char *options, *token, *next, *p, *val;
	int journal_usage = 0;
	struct tunefs_journal_option *jopt;

	if (arg) {
		options = strdup(arg);
		if (!options) {
			tcom_err(TUNEFS_ET_NO_MEMORY,
				 "while processing journal options");
			return 1;
		}
	} else
		options = NULL;

	for (token = options; token && *token; token = next) {
		p = strchr(token, ',');
		next = NULL;

		if (p) {
			*p = '\0';
			next = p + 1;
		}

		val = strchr(token, '=');

		if (val) {
			*val = '\0';
			val++;
		}

		jopt = find_journal_option(token);
		if (!jopt) {
			errorf("Unknown journal option: \"%s\"\n", token);
			journal_usage++;
			continue;
		}

		if (jopt->jo_op->to_parse_option) {
			if (jopt->jo_op->to_parse_option(jopt->jo_op, val)) {
				journal_usage++;
				continue;
			}
		} else if (val) {
			errorf("Journal option \"%s\" does not accept "
			       "arguments\n",
			       token);
			journal_usage++;
			continue;
		}

		err = tunefs_append_operation(jopt->jo_op);
		if (err) {
			tcom_err(err, "while processing journal options");
			rc = 1;
			break;
		}
	}

	if (journal_usage) {
		verbosef(VL_ERR, "Valid journal options are:\n");
		for (i = 0; tunefs_journal_options[i]; i++)
			verbosef(VL_ERR, "\t%s\n",
				 tunefs_journal_options[i]->jo_help);
		rc = 1;
	}

	free(options);
	return rc;
}

static struct tunefs_option help_option = {
	.opt_option	= {
		.name	= "help",
		.val	= 'h',
	},
	.opt_handle	= handle_help,
};

static struct tunefs_option version_option = {
	.opt_option	= {
		.name	= "version",
		.val	= 'V',
	},
	.opt_handle	= handle_version,
};

static struct tunefs_option verbose_option = {
	.opt_option	= {
		.name	= "verbose",
		.val	= 'v',
	},
	.opt_help	=
		"-v|--verbose (increases verbosity; more than one permitted)",
	.opt_handle	= handle_verbosity,
};

static struct tunefs_option quiet_option = {
	.opt_option	= {
		.name	= "quiet",
		.val	= 'q',
	},
	.opt_help	=
		"-q|--quiet (decreases verbosity; more than one permitted)",
	.opt_handle	= handle_verbosity,
};

static struct tunefs_option interactive_option = {
	.opt_option	= {
		.name	= "interactive",
		.val	= 'i',
	},
	.opt_help	= "-i|--interactive",
	.opt_handle	= handle_interactive,
};

static struct tunefs_option progress_option = {
	.opt_option	= {
		.name	= "progress",
		.val	= 'p',
	},
	.opt_help	= "-p|--progress",
	.opt_handle	= handle_progress,
};

static struct tunefs_option yes_option = {
	.opt_option	= {
		.name	= "yes",
		.val	= 'y',
	},
	.opt_help	= "-y|--yes",
	.opt_handle	= handle_answer,
};

static struct tunefs_option no_option = {
	.opt_option	= {
		.name	= "no",
		.val	= 'n',
	},
	.opt_help	= "-n|--no",
	.opt_handle	= handle_answer,
};

static struct tunefs_option query_option = {
	.opt_option	= {
		.name		= "query",
		.val		= 'Q',
		.has_arg	= 1,
	},
	.opt_help	= "-Q|--query <query-format>",
	.opt_handle	= &generic_handle_arg,
	.opt_op		= &query_op,
};

static struct tunefs_option list_sparse_option = {
	.opt_option	= {
		.name	= "list-sparse",
		.val	= CHAR_MAX,
	},
	.opt_help	= "   --list-sparse",
	.opt_op		= &list_sparse_op,
};

static struct tunefs_option reset_uuid_option = {
	.opt_option	= {
		.name		= "uuid-reset",
		.val		= 'U',
		.has_arg	= 2,
	},
	.opt_help	= "-U|--uuid-reset[=new-uuid]",
	.opt_handle	= &generic_handle_arg,
	.opt_op		= &reset_uuid_op,
};

static struct tunefs_option update_cluster_stack_option = {
	.opt_option	= {
		.name	= "update-cluster-stack",
		.val	= CHAR_MAX,
	},
	.opt_help	= "   --update-cluster-stack",
	.opt_op		= &update_cluster_stack_op,
};

static struct tunefs_option cloned_volume_option = {
	.opt_option	= {
		.name		= "cloned-volume",
		.val		= CHAR_MAX,
		.has_arg	= 2,
	},
	.opt_help	= "   --cloned-volume[=new-label]",
	.opt_op		= &cloned_volume_op,
};

static struct tunefs_option set_slot_count_option = {
	.opt_option	= {
		.name		= "node-slots",
		.val		= 'N',
		.has_arg	= 1,
	},
	.opt_help	= "-N|--node-slots <number-of-node-slots>",
	.opt_handle	= generic_handle_arg,
	.opt_op		= &set_slot_count_op,
};

static struct tunefs_option set_label_option = {
	.opt_option	= {
		.name		= "label",
		.val		= 'L',
		.has_arg	= 1,
	},
	.opt_help	= "-L|--label <label>",
	.opt_handle	= generic_handle_arg,
	.opt_op		= &set_label_op,
};

static struct tunefs_option mount_type_option = {
	.opt_option	= {
		.name		= "mount",
		.val		= 'M',
		.has_arg	= 1,
	},
	.opt_handle	= mount_type_handle_arg,
};

static struct tunefs_option backup_super_option = {
	.opt_option	= {
		.name	= "backup-super",
		.val	= CHAR_MAX,
	},
	.opt_handle	= backup_super_handle_arg,
};

static struct tunefs_option features_option = {
	.opt_option	= {
		.name		= "fs-features",
		.val		= CHAR_MAX,
		.has_arg	= 1,
	},
	.opt_help	= "   --fs-features [no]sparse,...",
	.opt_handle	= strdup_handle_arg,
};

static struct tunefs_option resize_volume_option = {
	.opt_option	= {
		.name		= "volume-size",
		.val		= 'S',
		.has_arg	= 2,
	},
	.opt_help	= "-S|--volume-size",
	.opt_handle	= strdup_handle_arg,
};

static struct tunefs_option journal_option = {
	.opt_option	= {
		.name		= "journal-options",
		.val		= 'J',
		.has_arg	= 1,
	},
	.opt_help	= "-J|--journal-options <options>",
	.opt_handle	= handle_journal_arg,
};

static struct tunefs_option set_usrquota_sync_interval_option = {
	.opt_option	= {
		.name		= "usrquota-sync-interval",
		.val		= 256,
		.has_arg	= 1,
	},
	.opt_help	= "   --usrquota-sync-interval <interval>",
	.opt_handle	= generic_handle_arg,
	.opt_op		= &set_usrquota_sync_interval_op,
};

static struct tunefs_option set_grpquota_sync_interval_option = {
	.opt_option	= {
		.name		= "grpquota-sync-interval",
		.val		= 257,
		.has_arg	= 1,
	},
	.opt_help	= "   --grpquota-sync-interval <interval>",
	.opt_handle	= generic_handle_arg,
	.opt_op		= &set_grpquota_sync_interval_op,
};

static struct tunefs_option set_mmp_update_interval_option = {
	.opt_option	= {
		.name		= "mmp-update-interval",
		.val		= 300,
		.has_arg	= 1,
	},
	.opt_help	= "   --mmp-update-interval <interval>",
	.opt_handle	= generic_handle_arg,
	.opt_op		= &set_mmp_update_interval_op,
};

/* The order here creates the order in print_usage() */
static struct tunefs_option *options[] = {
	&help_option,
	&version_option,
	&interactive_option,
	&progress_option,
	&verbose_option,
	&quiet_option,
	&set_label_option,
	&set_slot_count_option,
	&resize_volume_option,
	&reset_uuid_option,
	&journal_option,
	&query_option,
	&list_sparse_option,
	&mount_type_option,
	&backup_super_option,
	&features_option,
	&update_cluster_stack_option,
	&cloned_volume_option,
	&set_usrquota_sync_interval_option,
	&set_grpquota_sync_interval_option,
	&set_mmp_update_interval_option,
	&yes_option,
	&no_option,
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

static void print_usage(int rc)
{
	int i;
	enum tools_verbosity_level level = VL_ERR;

	if (!rc)
		level = VL_OUT;

	verbosef(level, "Usage: %s [options] <device> [new-size]\n",
		 tools_progname());
	verbosef(level, "       %s -h|--help\n", tools_progname());
	verbosef(level, "       %s -V|--version\n", tools_progname());
	verbosef(level, "[options] can be any mix of:\n");
	for (i = 0; options[i]; i++) {
		if (options[i]->opt_help)
			verbosef(level, "\t%s\n", options[i]->opt_help);
	}
	verbosef(level,
		 "[new-size] is only valid with the '-S' option\n"
		 "All sizes can be specified with K/M/G/T/P suffixes\n");
	exit(rc);
}

static errcode_t parse_feature_strings(void)
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

	if (!features)
		return 0;

	verbosef(VL_DEBUG, "Full feature string is \"%s\"\n", features);
	rc = features_op.to_parse_option(&features_op, features);
	free(features);
	if (rc)
		print_usage(1);

	return tunefs_append_operation(&features_op);
}

/*
 * We do resize_volume checks in this special-case function because the
 * new size is separated from the option flag due to historical reasons.
 *
 * If resize_volume_option.opt_set, we may or may not have arg.  A NULL
 * arg is means "fill up the LUN".  If !opt_set, arg must be NULL.
 */
static errcode_t parse_resize(const char *arg)
{
	char operation_arg[NAME_MAX];  /* Should be big enough :-) */

	if (!resize_volume_option.opt_set) {
		if (arg) {
			errorf("Too many arguments\n");
			print_usage(1);
		}

		return 0;  /* no resize options */
	}

	if (!arg)
		goto parse_option;

	/*
	 * We've stored any argument to -S on opt_private.  If there
	 * was no argument to -S, our new size is in blocks due to
	 * historical reasons.
	 *
	 * We don't have an open filesystem at this point, so we
	 * can't convert clusters<->blocks<->bytes.  So let's just tell
	 * the resize operation what unit we're talking.
	 */
	if (snprintf(operation_arg, NAME_MAX, "%s:%s",
		     resize_volume_option.opt_private ?
		     (char *)resize_volume_option.opt_private : "blocks",
		     arg) >= NAME_MAX) {
		errorf("Argument to option '--%s' is too long: %s\n",
		       resize_volume_option.opt_option.name, arg);
		print_usage(1);
	}

parse_option:
	if (resize_volume_op.to_parse_option(&resize_volume_op,
					     arg ? operation_arg : NULL))
		print_usage(1);

	/*
	 * We _prepend_ resize, because we want any other operations to
	 * have all the space they need.
	 */
	return tunefs_prepend_operation(&resize_volume_op);
}

static int build_options(char **optstring, struct option **longopts)
{
	errcode_t err;
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

	err = ocfs2_malloc0(sizeof(char) * (optstring_len + 1), &str);
	if (!err)
		err = ocfs2_malloc(sizeof(struct option) * (num_opts + 1),
				   &lopts);
	if (err) {
		rc = -ENOMEM;
		goto out;
	}

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
static errcode_t parse_options(int argc, char *argv[], char **device)
{
	int c;
	errcode_t err;
	struct option *long_options = NULL;
	char *optstring = NULL;
	struct tunefs_option *opt;

	err = build_options(&optstring, &long_options);
	if (err)
		goto out;

	opterr = 0;
	while ((c = getopt_long(argc, argv, optstring,
				long_options, NULL)) != EOF) {
		opt = NULL;
		switch (c) {
			case '?':
				if (optopt)
					errorf("Invalid option: '-%c'\n",
					       optopt);
				else
					errorf("Invalid option: '%s'\n",
					       argv[optind - 1]);
				print_usage(1);
				break;

			case ':':
				if (optopt < CHAR_MAX)
					errorf("Option '-%c' requires "
					       "an argument\n",
					       optopt);
				else
					errorf("Option '%s' requires "
					       "an argument\n",
					       argv[optind - 1]);
				print_usage(1);
				break;

			default:
				opt = find_option_by_val(c);
				if (!opt) {
					errorf("Shouldn't have gotten "
					       "here: option '-%c'\n",
					       c);
					print_usage(1);
				}
				break;
		}

		if (opt->opt_set) {
			errorf("Option '-%c' specified more than once\n",
			       c);
			print_usage(1);
		}

		opt->opt_set = 1;
		if (opt->opt_handle) {
			if (opt->opt_handle(opt, optarg))
				print_usage(1);
		}
		if (opt->opt_op) {
			err = tunefs_append_operation(opt->opt_op);
		}
	}

	err = parse_feature_strings();
	if (err)
		goto out;

	if (optind >= argc) {
		errorf("No device specified\n");
		print_usage(1);
	}

	*device = strdup(argv[optind]);
	if (!*device) {
		err = TUNEFS_ET_NO_MEMORY;
		goto out;
	}
	optind++;

	/* parse_resize() will check if we expected a size */
	if (optind < argc) {
		err = parse_resize(argv[optind]);
		optind++;
	} else
		err = parse_resize(NULL);
	if (err)
		goto out;

	if (optind < argc) {
		errorf("Too many arguments\n");
		print_usage(1);
	}

out:
	ocfs2_free(&optstring);
	ocfs2_free(&long_options);
	return err;
}


/*
 * This goes through tunefs_run_list and runs each operation in turn.
 * Once an operation has completed, it is removed from the list.  If filter
 * is non-zero, only operations that match filter are run this pass.
 */
static int run_operation_filter(ocfs2_filesys *fs, int filter)
{
	errcode_t err = 0;
	struct list_head *pos, *n;
	struct tunefs_run *run;
	struct tunefs_operation *op;

	list_for_each_safe(pos, n, &tunefs_run_list) {
		run = list_entry(pos, struct tunefs_run, tr_list);
		op = run->tr_op;

		if (filter && !(op->to_open_flags & filter))
			continue;

		list_del(&run->tr_list);
		ocfs2_free(&run);

		err = tunefs_op_run(fs, op);
		if (err) {
			if (err != TUNEFS_ET_OPERATION_FAILED) {
				tcom_err(err,
					 "while trying to perform "
					 "operation \"%s\"",
					 op->to_name);
			}
			break;
		}
		tools_progress_step(tunefs_op_progress, 1);
	}

	return err;
}

static int run_operations(const char *device)
{
	int rc;
	errcode_t tmp, err = 0;
	struct list_head *p;
	struct tunefs_run *run;
	ocfs2_filesys *fs;
	int open_flags;


	/*
	 * We have a specific order here.  If we open the filesystem and
	 * get TUNEFS_ET_CLUSTER_SKIPPED, we know that cloned_volume is
	 * involved.  We want to run that first and change our volume's
	 * UUID+label, then close and reopen the filesystem.  We should be
	 * able to continue with any other operations.
	 *
	 * Next, if we open the filesystem and * get
	 * TUNEFS_ET_INVALID_STACK_NAME, we know that update_cluster_stack
	 * is involved.  We want to run that, and again close and reopen
	 * the filesystem.  This should allow us to continue with any
	 * other operations.
	 *
	 * Next, if we get TUNEFS_ET_PERFORM_ONLINE, we have at least
	 * one operation capable of working online.  We want to run through
	 * the online capable ops before failing anything that cannot be
	 * done online.  Basically, do as much as we can.
	 *
	 * Last, anything else is run.  This is the normal state if we have
	 * a correctly configured cluster and have locked down the
	 * filesystem.  It runs in the order we added things to
	 * tunefs_run_list.
	 */
	while (!err && !list_empty(&tunefs_run_list)) {
		rc = 0;
		open_flags = 0;
		list_for_each(p, &tunefs_run_list) {
			run = list_entry(p, struct tunefs_run, tr_list);
			open_flags |= run->tr_op->to_open_flags;
		}

		err = tunefs_open(device, open_flags, &fs);
		if (err == TUNEFS_ET_CLUSTER_SKIPPED)
			rc = run_operation_filter(fs, TUNEFS_FLAG_SKIPCLUSTER);
		else if (err == TUNEFS_ET_INVALID_STACK_NAME)
			rc = run_operation_filter(fs, TUNEFS_FLAG_NOCLUSTER);
		else if (err == TUNEFS_ET_PERFORM_ONLINE)
			rc = run_operation_filter(fs, TUNEFS_FLAG_ONLINE);
		else if (!err)
			rc = run_operation_filter(fs, 0);
		else {
			tcom_err(err, "while opening device \"%s\"",
				 device);
			break;
		}

		err = 0;
		if (rc)
			err = TUNEFS_ET_OPERATION_FAILED;

		tmp = tunefs_close(fs);
		if (tmp)
			tcom_err(tmp, "while closing device \"%s\"",
				 device);
		if (!err)
			err = tmp;
	}

	return err;
}

int main(int argc, char *argv[])
{
	int rc = 1;
	errcode_t err;
	char *device;

	tunefs_init(argv[0]);

	err = parse_options(argc, argv, &device);
	if (err) {
		tcom_err(err, "while parsing options");
		goto out;
	}

	tunefs_op_progress = tools_progress_start("tunefs.ocfs2",
						  "tunefs",
						  tunefs_op_count);
	if (!tunefs_op_progress) {
		tcom_err(TUNEFS_ET_NO_MEMORY,
			 "while initializing the progress display");
		goto out;
	}

	rc = run_operations(device);

	tools_progress_stop(tunefs_op_progress);

out:
	return rc;
}
