/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * feature_strings.c
 *
 * Routines for analyzing a feature string.
 *
 * Copyright (C) 2007 Oracle.  All rights reserved.
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

#include "ocfs2/ocfs2.h"

struct fs_feature_flags {
	const char *ff_str;
	/* this flag is the feature's own flag. */
	ocfs2_fs_options ff_own_flags;
	/*
	 * this flag includes the feature's own flag and
	 * all the other features' flag it depends on.
	 */
	ocfs2_fs_options ff_flags;
};

struct feature_level_translation {
	const char *fl_str;
	enum ocfs2_feature_levels fl_type;
};

static struct feature_level_translation ocfs2_feature_levels_table[] = {
	{"default", OCFS2_FEATURE_LEVEL_DEFAULT},
	{"max-compat", OCFS2_FEATURE_LEVEL_MAX_COMPAT},
	{"max-features", OCFS2_FEATURE_LEVEL_MAX_FEATURES},
	{NULL, OCFS2_FEATURE_LEVEL_DEFAULT},
};

static ocfs2_fs_options feature_level_defaults[] = {
	{OCFS2_FEATURE_COMPAT_BACKUP_SB,
	 OCFS2_FEATURE_INCOMPAT_SPARSE_ALLOC,
	 OCFS2_FEATURE_RO_COMPAT_UNWRITTEN},  /* OCFS2_FEATURE_LEVEL_DEFAULT */

	{OCFS2_FEATURE_COMPAT_BACKUP_SB,
	 0,
	 0}, /* OCFS2_FEATURE_LEVEL_MAX_COMPAT */

	{OCFS2_FEATURE_COMPAT_BACKUP_SB,
	 OCFS2_FEATURE_INCOMPAT_SPARSE_ALLOC |
	 OCFS2_FEATURE_INCOMPAT_EXTENDED_SLOT_MAP,
	 OCFS2_FEATURE_RO_COMPAT_UNWRITTEN}, /* OCFS2_FEATURE_LEVEL_MAX_FEATURES */
};

static struct fs_feature_flags ocfs2_supported_features[] = {
	{
		"local",
		{0, OCFS2_FEATURE_INCOMPAT_LOCAL_MOUNT, 0},
		{0, OCFS2_FEATURE_INCOMPAT_LOCAL_MOUNT, 0},
	},
	{
		"sparse",
		{0, OCFS2_FEATURE_INCOMPAT_SPARSE_ALLOC, 0},
		{0, OCFS2_FEATURE_INCOMPAT_SPARSE_ALLOC, 0},
	},
	{
		"backup-super",
		{OCFS2_FEATURE_COMPAT_BACKUP_SB, 0, 0},
		{OCFS2_FEATURE_COMPAT_BACKUP_SB, 0, 0},
	},
	{
		"unwritten",
		{0, 0, OCFS2_FEATURE_RO_COMPAT_UNWRITTEN},
		{0, OCFS2_FEATURE_INCOMPAT_SPARSE_ALLOC,
		 OCFS2_FEATURE_RO_COMPAT_UNWRITTEN},
	},
	{
		"extended-slotmap",
		{0, OCFS2_FEATURE_INCOMPAT_EXTENDED_SLOT_MAP, 0},
		{0, OCFS2_FEATURE_INCOMPAT_EXTENDED_SLOT_MAP, 0},
	},
	{
		NULL,
		{0, 0, 0},
		{0, 0, 0}
	},
};

static inline void merge_features(ocfs2_fs_options *features,
				  ocfs2_fs_options new_features)
{
	features->opt_compat |= new_features.opt_compat;
	features->opt_incompat |= new_features.opt_incompat;
	features->opt_ro_compat |= new_features.opt_ro_compat;
}

/* Get the feature level according to the value set by "--fs-feature-level". */
errcode_t ocfs2_parse_feature_level(const char *typestr,
				    enum ocfs2_feature_levels *level)
{
	int i;

	for(i = 0; ocfs2_feature_levels_table[i].fl_str; i++) {
		if (strcmp(typestr,
			  ocfs2_feature_levels_table[i].fl_str) == 0) {
			*level = ocfs2_feature_levels_table[i].fl_type;
			break;
		}
	}

	if (!ocfs2_feature_levels_table[i].fl_str) {
		return OCFS2_ET_UNSUPP_FEATURE;
	}

	return 0;
}

static int check_feature_flags(ocfs2_fs_options *fs_flags,
			       ocfs2_fs_options *fs_r_flags)
{
	int ret = 1;

	if (fs_r_flags->opt_compat &&
	    fs_flags->opt_compat & fs_r_flags->opt_compat)
		ret = 0;
	else if (fs_r_flags->opt_incompat &&
		 fs_flags->opt_incompat & fs_r_flags->opt_incompat)
		ret = 0;
	else if (fs_r_flags->opt_ro_compat &&
		 fs_flags->opt_ro_compat & fs_r_flags->opt_ro_compat)
		ret = 0;

	return ret;
}

/*
 * If we are asked to clear a feature, we also need to clear any other
 * features that depend on it.
 */
static void ocfs2_feature_clear_deps(ocfs2_fs_options *reverse_set)
{
	int i;

	for(i = 0; ocfs2_supported_features[i].ff_str; i++) {
		if ((reverse_set->opt_compat &
			ocfs2_supported_features[i].ff_flags.opt_compat) ||
		    (reverse_set->opt_incompat &
			ocfs2_supported_features[i].ff_flags.opt_incompat) ||
		    (reverse_set->opt_ro_compat &
			ocfs2_supported_features[i].ff_flags.opt_ro_compat)) {
			merge_features(reverse_set,
				       ocfs2_supported_features[i].ff_own_flags);
		}
	}

}

/*
 * Check and Merge all the diffent features set by the user.
 *
 * index: the feature level.
 * feature_set: all the features a user set by "--fs-features".
 * reverse_set: all the features a user want to clear by "--fs-features".
 */
errcode_t ocfs2_merge_feature_flags_with_level(ocfs2_fs_options *dest,
					       int level,
					       ocfs2_fs_options *feature_set,
					       ocfs2_fs_options *reverse_set)
{
	ocfs2_fs_options level_set = feature_level_defaults[level];
	/*
	 * "Check whether the user asked for a flag to be set and cleared,
	 * which is illegal. The feature_set and reverse_set are both set
	 * by "--fs-features", so they shouldn't collide with each other.
	 */
	if (!check_feature_flags(feature_set, reverse_set))
		return OCFS2_ET_CONFLICTING_FEATURES;

	/* Now combine all the features the user has set. */
	*dest = level_set;
	merge_features(dest, *feature_set);

	/*
	 * Ensure that all dependancies are correct in the reverse set.
	 * A reverse set from ocfs2_parse_feature() will be correct, but
	 * a hand-built one might not be.
	 */
	ocfs2_feature_clear_deps(reverse_set);

	/* Now clear the reverse set from our destination */
	dest->opt_compat &= ~reverse_set->opt_compat;
	dest->opt_ro_compat &= ~reverse_set->opt_ro_compat;
	dest->opt_incompat &= ~reverse_set->opt_incompat;

	return 0;
}

/*
 * Parse the feature string.
 *
 * For those the user want to clear(with "no" in the beginning),
 * they are stored in "reverse_flags".
 *
 * For those the user want to set, they are stored in "feature_flags".
 */
errcode_t ocfs2_parse_feature(const char *opts,
			      ocfs2_fs_options *feature_flags,
			      ocfs2_fs_options *reverse_flags)
{
	char *options, *token, *next, *p, *arg;
	int i, reverse = 0;

	memset(feature_flags, 0, sizeof(ocfs2_fs_options));
	memset(reverse_flags, 0, sizeof(ocfs2_fs_options));

	options = strdup(opts);
	for (token = options; token && *token; token = next) {
		reverse = 0;
		p = strchr(token, ',');
		next = NULL;

		if (p) {
			*p = '\0';
			next = p + 1;
		}

		arg = strstr(token, "no");
		if (arg && arg == token) {
			reverse = 1;
			token += 2;
		}

		for(i = 0; ocfs2_supported_features[i].ff_str; i++) {
			if (strcmp(token,
				   ocfs2_supported_features[i].ff_str) == 0) {
				if (!reverse)
					merge_features(feature_flags,
					ocfs2_supported_features[i].ff_flags);
				else
					merge_features(reverse_flags,
					ocfs2_supported_features[i].ff_own_flags);
				break;
			}
		}
		if (!ocfs2_supported_features[i].ff_str) {
			free(options);
			return OCFS2_ET_UNSUPP_FEATURE;
		}
	}

	free(options);
	ocfs2_feature_clear_deps(reverse_flags);

	return 0;
}


#ifdef DEBUG_EXE

#include <stdio.h>
#include <getopt.h>

#include "ocfs2/ocfs2.h"

static void print_features(char *desc, ocfs2_fs_options *feature_set)
{
	int i;

	fprintf(stdout, "%s:\n", desc);

	fprintf(stdout, "COMPAT:\t\t");
	for(i = 0; ocfs2_supported_features[i].ff_str; i++)
		if (feature_set->opt_compat &
		    ocfs2_supported_features[i].ff_own_flags.opt_compat)
			fprintf(stdout, " %s",
				ocfs2_supported_features[i].ff_str);
	fprintf(stdout, "\n");
	fprintf(stdout, "RO_COMPAT:\t");
	for(i = 0; ocfs2_supported_features[i].ff_str; i++)
		if (feature_set->opt_ro_compat &
		    ocfs2_supported_features[i].ff_own_flags.opt_ro_compat)
			fprintf(stdout, " %s",
				ocfs2_supported_features[i].ff_str);
	fprintf(stdout, "\n");
	fprintf(stdout, "INCOMPAT:\t");
	for(i = 0; ocfs2_supported_features[i].ff_str; i++)
		if (feature_set->opt_incompat &
		    ocfs2_supported_features[i].ff_own_flags.opt_incompat)
			fprintf(stdout, " %s",
				ocfs2_supported_features[i].ff_str);
	fprintf(stdout, "\n\n");
}

extern int optind, optopt, opterr;
extern char *optarg;
int main(int argc, char *argv[])
{
	int c;
	enum ocfs2_feature_levels level = OCFS2_FEATURE_LEVEL_DEFAULT;
	errcode_t err;
	char *feature_string = NULL;
	char *level_string = NULL;
	ocfs2_fs_options set_features, clear_features, mkfs_features;

	initialize_ocfs_error_table();

	opterr = 0;
	memset(&set_features, 0, sizeof(ocfs2_fs_options));
	memset(&clear_features, 0, sizeof(ocfs2_fs_options));
	while ((c = getopt(argc, argv, ":l:s:")) != EOF) {
		switch (c) {
		case 'l':
			if (level_string)
				free(level_string);
			level_string = strdup(optarg);
			err = ocfs2_parse_feature_level(optarg, &level);
			if (err) {
				com_err(argv[0], err,
					"while parsing the feature level string");
				exit(1);
			}
			break;

		case 's':
			if (feature_string)
				free(feature_string);
			feature_string = strdup(optarg);
			memset(&set_features, 0, sizeof(ocfs2_fs_options));
			memset(&clear_features, 0, sizeof(ocfs2_fs_options));
			err = ocfs2_parse_feature(optarg, &set_features,
						  &clear_features);
			if (err) {
				com_err(argv[0], err,
					"while parsing the feature string");
				exit(1);
			}
			break;

		default:
			fprintf(stderr, "%s: Invalid argument: '-%c'\n",
				argv[0], optopt);
			exit(1);
			break;
		}
	}

	memset(&mkfs_features, 0, sizeof(ocfs2_fs_options));
	err = ocfs2_merge_feature_flags_with_level(&mkfs_features, level,
						   &set_features,
						   &clear_features);
	if (err) {
		com_err(argv[0], err,
			"while trying to reconcile default and specified features");
		exit(1);
	}

	print_features("\nmkfs.ocfs2 would set these features",
		       &mkfs_features);
	print_features("tunefs.ocfs2 would set these features",
		       &set_features);
	print_features("tunefs.ocfs2 would clear these features",
		       &clear_features);

	return 0;
}


#endif /* DEBUG_EXE */
