/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * feature_strings.h
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

#ifndef __FEATURE_STRING_H
#define __FEATURE_STRING_H

#include <ocfs2/ocfs2.h>

struct fs_feature_flags {
	const char *ff_str;
	/* this flag is the feature's own flag. */
	fs_options ff_own_flags;
	/*
	 * this flag includes the feature's own flag and
	 * all the other features' flag it depends on.
	 */
	fs_options ff_flags;
};

enum feature_level_indexes {
	FEATURE_LEVEL_DEFAULT = 0,
	FEATURE_LEVEL_MAX_COMPAT,
	FEATURE_LEVEL_MAX_FEATURES,
};

errcode_t parse_feature(const char *opts,
			fs_options *feature_flags,
			fs_options *reverse_flags);

int parse_feature_level(const char *typestr,
			enum feature_level_indexes *index);

int merge_feature_flags_with_level(fs_options *dest,
				   int index,
				   fs_options *feature_set,
				   fs_options *reverse_set);
#endif /* __FEATURE_STIRNG_H */
