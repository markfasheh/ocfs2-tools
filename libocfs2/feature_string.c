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
#include "ocfs2/feature_string.h"

struct feature_level_translation {
	const char *fl_str;
	enum feature_level_indexes fl_type;
};

static struct feature_level_translation ocfs2_feature_levels_table[] = {
	{"default", FEATURE_LEVEL_DEFAULT},
	{"max-compat", FEATURE_LEVEL_MAX_COMPAT},
	{"max-features", FEATURE_LEVEL_MAX_FEATURES},
	{NULL, FEATURE_LEVEL_DEFAULT},
};

static fs_options feature_level_defaults[] = {
	{OCFS2_FEATURE_COMPAT_BACKUP_SB,
	 OCFS2_FEATURE_INCOMPAT_SPARSE_ALLOC,
	 0},  /* FEATURE_LEVEL_DEFAULT */

	{OCFS2_FEATURE_COMPAT_BACKUP_SB,
	 0,
	 0}, /* FEATURE_LEVEL_MAX_COMPAT */

	{OCFS2_FEATURE_COMPAT_BACKUP_SB,
	 OCFS2_FEATURE_INCOMPAT_SPARSE_ALLOC,
	 OCFS2_FEATURE_RO_COMPAT_UNWRITTEN}, /* FEATURE_LEVEL_MAX_FEATURES */
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
		NULL,
		{0, 0, 0},
		{0, 0, 0}
	},
};

static inline void merge_features(fs_options *features,
				  fs_options new_features)
{
	features->compat |= new_features.compat;
	features->incompat |= new_features.incompat;
	features->ro_compat |= new_features.ro_compat;
}

/* Get the feature level according to the value set by "--fs-feature-level". */
int parse_feature_level(const char *typestr,
			 enum feature_level_indexes *index)
{
	int i;

	for(i = 0; ocfs2_feature_levels_table[i].fl_str; i++) {
		if (strcmp(typestr,
			  ocfs2_feature_levels_table[i].fl_str) == 0) {
			*index = ocfs2_feature_levels_table[i].fl_type;
			break;
		}
	}

	if (!ocfs2_feature_levels_table[i].fl_str) {
		return OCFS2_ET_UNSUPP_FEATURE;
	}

	return 0;
}

static int check_feature_flags(fs_options *fs_flags,
			       fs_options *fs_r_flags)
{
	int ret = 1;

	if (fs_r_flags->compat &&
	    fs_flags->compat & fs_r_flags->compat)
		ret = 0;
	else if (fs_r_flags->incompat &&
		 fs_flags->incompat & fs_r_flags->incompat)
		ret = 0;
	else if (fs_r_flags->ro_compat &&
		 fs_flags->ro_compat & fs_r_flags->ro_compat)
		ret = 0;

	return ret;
}

/*
 * Check and Merge all the diffent features set by the user.
 *
 * index: the feature level.
 * feature_set: all the features a user set by "--fs-features".
 * reverse_set: all the features a user want to clear by "--fs-features".
 */
int merge_feature_flags_with_level(fs_options *dest,
				   int index,
				   fs_options *feature_set,
				   fs_options *reverse_set)
{
	int i;
	fs_options level_set = feature_level_defaults[index];
	/*
	 * "Check whether the user asked for a flag to be set and cleared,
	 * which is illegal. The feature_set and reverse_set are both set
	 * by "--fs-features", so they shouldn't collide with each other.
	 */
	if (!check_feature_flags(feature_set, reverse_set))
		return 0;

	/* Now combine all the features the user has set. */
	*dest = level_set;
	merge_features(dest, *feature_set);

	/*
	 * We have to remove all the features in the reverse set
	 * and other features which depend on them.
	 */
	for(i = 0; ocfs2_supported_features[i].ff_str; i++) {
		if ((reverse_set->compat &
			ocfs2_supported_features[i].ff_flags.compat) ||
		    (reverse_set->incompat &
			ocfs2_supported_features[i].ff_flags.incompat) ||
		    (reverse_set->ro_compat &
			ocfs2_supported_features[i].ff_flags.ro_compat)) {
			dest->compat &=
			~ocfs2_supported_features[i].ff_own_flags.compat;

			dest->incompat &=
			~ocfs2_supported_features[i].ff_own_flags.incompat;

			dest->ro_compat &=
			~ocfs2_supported_features[i].ff_own_flags.ro_compat;
		}
	}

	return 1;
}

/*
 * Parse the feature string.
 *
 * For those the user want to clear(with "no" in the beginning),
 * they are stored in "reverse_flags".
 *
 * For those the user want to set, they are stored in "feature_flags".
 */
errcode_t parse_feature(const char *opts,
		        fs_options *feature_flags,
		        fs_options *reverse_flags)
{
	char *options, *token, *next, *p, *arg;
	int i, reverse = 0;

	memset(feature_flags, 0, sizeof(fs_options));
	memset(reverse_flags, 0, sizeof(fs_options));

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

	return 0;
}
