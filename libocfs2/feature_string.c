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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301 USA.
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

/* Printable names for feature flags */
struct feature_name {
	const char		*fn_name;
	ocfs2_fs_options	fn_flag;	/* Only the bit for this
						   feature */
};

struct flag_name {
	const char		*fl_name;
	uint32_t		fl_flag;
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
	{OCFS2_FEATURE_COMPAT_BACKUP_SB | OCFS2_FEATURE_COMPAT_JBD2_SB,
	 OCFS2_FEATURE_INCOMPAT_SPARSE_ALLOC |
	 OCFS2_FEATURE_INCOMPAT_EXTENDED_SLOT_MAP |
	 OCFS2_FEATURE_INCOMPAT_INLINE_DATA |
	 OCFS2_FEATURE_INCOMPAT_XATTR |
	 OCFS2_FEATURE_INCOMPAT_REFCOUNT_TREE |
	 OCFS2_FEATURE_INCOMPAT_INDEXED_DIRS |
	 OCFS2_FEATURE_INCOMPAT_DISCONTIG_BG |
	 OCFS2_FEATURE_INCOMPAT_APPEND_DIO,
	 OCFS2_FEATURE_RO_COMPAT_UNWRITTEN},  /* OCFS2_FEATURE_LEVEL_DEFAULT */

	{OCFS2_FEATURE_COMPAT_BACKUP_SB | OCFS2_FEATURE_COMPAT_JBD2_SB,
	 0,
	 0}, /* OCFS2_FEATURE_LEVEL_MAX_COMPAT */

	{OCFS2_FEATURE_COMPAT_BACKUP_SB | OCFS2_FEATURE_COMPAT_JBD2_SB,
	 OCFS2_FEATURE_INCOMPAT_SPARSE_ALLOC |
	 OCFS2_FEATURE_INCOMPAT_EXTENDED_SLOT_MAP |
	 OCFS2_FEATURE_INCOMPAT_INLINE_DATA |
	 OCFS2_FEATURE_INCOMPAT_META_ECC |
	 OCFS2_FEATURE_INCOMPAT_XATTR |
	 OCFS2_FEATURE_INCOMPAT_REFCOUNT_TREE |
	 OCFS2_FEATURE_INCOMPAT_INDEXED_DIRS |
	 OCFS2_FEATURE_INCOMPAT_DISCONTIG_BG |
	 OCFS2_FEATURE_INCOMPAT_APPEND_DIO,
	 OCFS2_FEATURE_RO_COMPAT_UNWRITTEN |
	 OCFS2_FEATURE_RO_COMPAT_USRQUOTA |
	 OCFS2_FEATURE_RO_COMPAT_GRPQUOTA }, /* OCFS2_FEATURE_LEVEL_MAX_FEATURES */
};

static ocfs2_fs_options mkfstypes_features_defaults[] = {
	{OCFS2_FEATURE_COMPAT_BACKUP_SB | OCFS2_FEATURE_COMPAT_JBD2_SB,
	 OCFS2_FEATURE_INCOMPAT_SPARSE_ALLOC |
	 OCFS2_FEATURE_INCOMPAT_EXTENDED_SLOT_MAP |
	 OCFS2_FEATURE_INCOMPAT_INLINE_DATA |
	 OCFS2_FEATURE_INCOMPAT_XATTR |
	 OCFS2_FEATURE_INCOMPAT_REFCOUNT_TREE |
	 OCFS2_FEATURE_INCOMPAT_INDEXED_DIRS |
	 OCFS2_FEATURE_INCOMPAT_DISCONTIG_BG |
	 OCFS2_FEATURE_INCOMPAT_APPEND_DIO,
	 OCFS2_FEATURE_RO_COMPAT_UNWRITTEN},  /* OCFS2_MKFSTYPE_DEFAULT */

	{OCFS2_FEATURE_COMPAT_BACKUP_SB | OCFS2_FEATURE_COMPAT_JBD2_SB,
	 OCFS2_FEATURE_INCOMPAT_SPARSE_ALLOC |
	 OCFS2_FEATURE_INCOMPAT_EXTENDED_SLOT_MAP |
	 OCFS2_FEATURE_INCOMPAT_INLINE_DATA |
	 OCFS2_FEATURE_INCOMPAT_XATTR |
	 OCFS2_FEATURE_INCOMPAT_REFCOUNT_TREE |
	 OCFS2_FEATURE_INCOMPAT_INDEXED_DIRS |
	 OCFS2_FEATURE_INCOMPAT_DISCONTIG_BG |
	 OCFS2_FEATURE_INCOMPAT_APPEND_DIO,
	 OCFS2_FEATURE_RO_COMPAT_UNWRITTEN},  /* OCFS2_MKFSTYPE_DATAFILES */

	{OCFS2_FEATURE_COMPAT_BACKUP_SB | OCFS2_FEATURE_COMPAT_JBD2_SB,
	 OCFS2_FEATURE_INCOMPAT_SPARSE_ALLOC |
	 OCFS2_FEATURE_INCOMPAT_EXTENDED_SLOT_MAP |
	 OCFS2_FEATURE_INCOMPAT_INLINE_DATA |
	 OCFS2_FEATURE_INCOMPAT_XATTR |
	 OCFS2_FEATURE_INCOMPAT_REFCOUNT_TREE |
	 OCFS2_FEATURE_INCOMPAT_INDEXED_DIRS |
	 OCFS2_FEATURE_INCOMPAT_DISCONTIG_BG |
	 OCFS2_FEATURE_INCOMPAT_APPEND_DIO,
	 OCFS2_FEATURE_RO_COMPAT_UNWRITTEN},  /* OCFS2_MKFSTYPE_MAIL */

	{OCFS2_FEATURE_COMPAT_BACKUP_SB | OCFS2_FEATURE_COMPAT_JBD2_SB,
	 OCFS2_FEATURE_INCOMPAT_SPARSE_ALLOC |
	 OCFS2_FEATURE_INCOMPAT_EXTENDED_SLOT_MAP |
	 OCFS2_FEATURE_INCOMPAT_INLINE_DATA |
	 OCFS2_FEATURE_INCOMPAT_XATTR |
	 OCFS2_FEATURE_INCOMPAT_REFCOUNT_TREE |
	 OCFS2_FEATURE_INCOMPAT_INDEXED_DIRS |
	 OCFS2_FEATURE_INCOMPAT_DISCONTIG_BG |
	 OCFS2_FEATURE_INCOMPAT_APPEND_DIO,
	 OCFS2_FEATURE_RO_COMPAT_UNWRITTEN},  /* OCFS2_MKFSTYPE_VMSTORE */
};

/* These are the features we support in mkfs/tunefs via --fs-features */
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
		"inline-data",
		{0, OCFS2_FEATURE_INCOMPAT_INLINE_DATA, 0},
		{0, OCFS2_FEATURE_INCOMPAT_INLINE_DATA, 0},
	},
	{
		"metaecc",
		{0, OCFS2_FEATURE_INCOMPAT_META_ECC, 0},
		{0, OCFS2_FEATURE_INCOMPAT_META_ECC, 0},
	},
	{
		"xattr",
		{0, OCFS2_FEATURE_INCOMPAT_XATTR, 0},
		{0, OCFS2_FEATURE_INCOMPAT_XATTR, 0},
	},
	{
		"indexed-dirs",
		{0, OCFS2_FEATURE_INCOMPAT_INDEXED_DIRS, 0},
		{0, OCFS2_FEATURE_INCOMPAT_INDEXED_DIRS, 0},
	},
	{
		"usrquota",
		{0, 0, OCFS2_FEATURE_RO_COMPAT_USRQUOTA},
		{0, 0, OCFS2_FEATURE_RO_COMPAT_USRQUOTA},
	},
	{
		"grpquota",
		{0, 0, OCFS2_FEATURE_RO_COMPAT_GRPQUOTA},
		{0, 0, OCFS2_FEATURE_RO_COMPAT_GRPQUOTA},
	},
	{
		"refcount",
		{0, OCFS2_FEATURE_INCOMPAT_REFCOUNT_TREE, 0},
		{0, OCFS2_FEATURE_INCOMPAT_REFCOUNT_TREE, 0},
	},
	{
		"discontig-bg",
		{0, OCFS2_FEATURE_INCOMPAT_DISCONTIG_BG, 0},
		{0, OCFS2_FEATURE_INCOMPAT_DISCONTIG_BG, 0},
	},
	{
		"clusterinfo",
		{0, OCFS2_FEATURE_INCOMPAT_CLUSTERINFO, 0},
		{0, OCFS2_FEATURE_INCOMPAT_CLUSTERINFO, 0},
	},
	{
		"append-dio",
		{0, OCFS2_FEATURE_INCOMPAT_APPEND_DIO, 0},
		{0, OCFS2_FEATURE_INCOMPAT_APPEND_DIO, 0},
	},
	{
		"mmp",
		{0, OCFS2_FEATURE_INCOMPAT_MMP, 0},
		{0, OCFS2_FEATURE_INCOMPAT_MMP, 0},
	},
	{
		NULL,
		{0, 0, 0},
		{0, 0, 0}
	},
};

/*
 * These are the printable names of all flags in s_feature_compat,
 * s_feature_ro_compat, and s_feature_incompat.  If libocfs2 supports this
 * feature, its printable name must be here.
 *
 * These MUST be kept in sync with the flags in ocfs2_fs.h.
 */
static struct feature_name ocfs2_feature_names[] = {
	{
		.fn_name = "heartbeat-device",
		.fn_flag = {0, OCFS2_FEATURE_INCOMPAT_HEARTBEAT_DEV, 0},
	},
	{
		.fn_name = "aborted-resize",
		.fn_flag = {0, OCFS2_FEATURE_INCOMPAT_RESIZE_INPROG, 0},
	},
	{
		.fn_name = "local",
		.fn_flag = {0, OCFS2_FEATURE_INCOMPAT_LOCAL_MOUNT, 0},
	},
	{
		.fn_name = "sparse",
		.fn_flag = {0, OCFS2_FEATURE_INCOMPAT_SPARSE_ALLOC, 0},
	},
	{
		.fn_name = "extended-slotmap",
		.fn_flag = {0, OCFS2_FEATURE_INCOMPAT_EXTENDED_SLOT_MAP, 0},
	},
	{
		.fn_name = "aborted-tunefs",
		.fn_flag = {0, OCFS2_FEATURE_INCOMPAT_TUNEFS_INPROG, 0},
	},
	{
		.fn_name = "userspace-stack",
		.fn_flag = {0, OCFS2_FEATURE_INCOMPAT_USERSPACE_STACK, 0},
	},
	{
		.fn_name = "backup-super",
		.fn_flag = {OCFS2_FEATURE_COMPAT_BACKUP_SB, 0, 0},
	},
	{
		.fn_name = "unwritten",
		.fn_flag = {0, 0, OCFS2_FEATURE_RO_COMPAT_UNWRITTEN},
	},
	{
		.fn_name = "inline-data",
		.fn_flag = {0, OCFS2_FEATURE_INCOMPAT_INLINE_DATA, 0},
	},
	{
		.fn_name = "strict-journal-super",
		.fn_flag = {OCFS2_FEATURE_COMPAT_JBD2_SB, 0, 0},
	},
	{
		.fn_name = "metaecc",
		.fn_flag = {0, OCFS2_FEATURE_INCOMPAT_META_ECC, 0},
	},
	{
		.fn_name = "xattr",
		.fn_flag = {0, OCFS2_FEATURE_INCOMPAT_XATTR, 0},
	},
	{
		.fn_name = "indexed-dirs",
		.fn_flag = {0, OCFS2_FEATURE_INCOMPAT_INDEXED_DIRS, 0},
	},
	{
		.fn_name = "usrquota",
		.fn_flag = {0, 0, OCFS2_FEATURE_RO_COMPAT_USRQUOTA},
	},
	{
		.fn_name = "grpquota",
		.fn_flag = {0, 0, OCFS2_FEATURE_RO_COMPAT_GRPQUOTA},
	},
	{
		.fn_name = "refcount",
		.fn_flag = {0, OCFS2_FEATURE_INCOMPAT_REFCOUNT_TREE, 0},
	},
	{
		.fn_name = "discontig-bg",
		.fn_flag = {0, OCFS2_FEATURE_INCOMPAT_DISCONTIG_BG, 0},
	},
	{
		.fn_name = "clusterinfo",
		.fn_flag = {0, OCFS2_FEATURE_INCOMPAT_CLUSTERINFO, 0},
	},
	{
		.fn_name = "append-dio",
		.fn_flag = {0, OCFS2_FEATURE_INCOMPAT_APPEND_DIO, 0},
	},
	{
		.fn_name = "mmp",
		.fn_flag = {0, OCFS2_FEATURE_INCOMPAT_MMP, 0},
	},
	{
		.fn_name = NULL,
	},
};

/*
 * The printable names of every flag in s_tunefs_flag.  If libocfs2 supports
 * the flag, its name must be here.
 *
 * These MUST be kept in sync with the flags in ocfs2_fs.h.
 */
static struct flag_name ocfs2_tunefs_flag_names[] = {
	{
		.fl_name = "remove-slot",
		.fl_flag = OCFS2_TUNEFS_INPROG_REMOVE_SLOT,
	},
	{
		.fl_name = "dir-trailer",
		.fl_flag = OCFS2_TUNEFS_INPROG_DIR_TRAILER,
	},
	{
		.fl_name = NULL,
	},
};

/*
 * The printable names of every flag in e_flags.  If libocfs2 supports the
 * flag, its name must be here.
 *
 * These MUST be kept in sync with the flags in ocfs2_fs.h.
 */
static struct flag_name ocfs2_extent_flag_names[] = {
	{
		.fl_name = "Unwritten",
		.fl_flag = OCFS2_EXT_UNWRITTEN,
	},
	{
		.fl_name = "Refcounted",
		.fl_flag = OCFS2_EXT_REFCOUNTED,
	},
	{
		.fl_name = NULL,
	},
};

/*
 * The printable names of every flag in rf_flags.  If libocfs2 supports the
 * flag, its name must be here.
 *
 * These MUST be kept in sync with the flags in ocfs2_fs.h.
 */
static struct flag_name ocfs2_refcount_flag_names[] = {
	{
		.fl_name = "Leaf",
		.fl_flag = OCFS2_REFCOUNT_LEAF_FL,
	},
	{
		.fl_name = "Tree",
		.fl_flag = OCFS2_REFCOUNT_TREE_FL,
	},
	{
		.fl_name = NULL,
	},
};

static struct flag_name ocfs2_cluster_o2cb_flag_names[] = {
	{
		.fl_name = "Globalheartbeat",
		.fl_flag = OCFS2_CLUSTER_O2CB_GLOBAL_HEARTBEAT,
	},
	{
		.fl_name = NULL,
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

static int feature_match(ocfs2_fs_options *a, ocfs2_fs_options *b)
{
	if ((a->opt_compat & b->opt_compat) ||
	    (a->opt_incompat & b->opt_incompat) ||
	    (a->opt_ro_compat & b->opt_ro_compat))
		return 1;

	return 0;
}

errcode_t ocfs2_snprint_feature_flags(char *str, size_t size,
				      ocfs2_fs_options *flags)
{
	int i, printed;
	char *ptr = str;
	size_t remain = size;
	errcode_t err = 0;
	char *sep = " ";
	ocfs2_fs_options found = {0, 0, 0};

	for (i = 0; ocfs2_feature_names[i].fn_name; i++) {
		if (!feature_match(flags, &ocfs2_feature_names[i].fn_flag))
			continue;
		merge_features(&found, ocfs2_feature_names[i].fn_flag);

		printed = snprintf(ptr, remain, "%s%s",
				   ptr == str ? "" : sep,
				   ocfs2_feature_names[i].fn_name);
		if (printed < 0)
			err = OCFS2_ET_INTERNAL_FAILURE;
		else if (printed >= remain)
			err = OCFS2_ET_NO_SPACE;
		if (err)
			break;

		remain -= printed;
		ptr += printed;
	}

	if (!err) {
		if ((found.opt_compat != flags->opt_compat) ||
		    (found.opt_ro_compat != flags->opt_ro_compat) ||
		    (found.opt_incompat != flags->opt_incompat)) {
			printed = snprintf(ptr, remain, "%sunknown",
					   ptr == str ? "" : sep);
			if (printed < 0)
				err = OCFS2_ET_INTERNAL_FAILURE;
			else if (printed >= remain)
				err = OCFS2_ET_NO_SPACE;
		}
	}

	return err;
}

static errcode_t ocfs2_snprint_flag_names(struct flag_name *flag_names,
					  char *str, size_t size,
					  uint32_t flags)
{
	int i, printed;
	char *ptr = str;
	size_t remain = size;
	errcode_t err = 0;
	char *sep = " ";
	uint16_t found = 0;

	for (i = 0; flag_names[i].fl_name; i++) {
		if (!(flags & flag_names[i].fl_flag))
			continue;
		found |= flag_names[i].fl_flag;

		printed = snprintf(ptr, remain, "%s%s",
				   ptr == str ? "" : sep,
				   flag_names[i].fl_name);
		if (printed < 0)
			err = OCFS2_ET_INTERNAL_FAILURE;
		else if (printed >= remain)
			err = OCFS2_ET_NO_SPACE;
		if (err)
			break;

		remain -= printed;
		ptr += printed;
	}

	if (!err) {
		if (found != flags) {
			printed = snprintf(ptr, remain, "%sunknown",
					   ptr == str ? "" : sep);
			if (printed < 0)
				err = OCFS2_ET_INTERNAL_FAILURE;
			else if (printed >= remain)
				err = OCFS2_ET_NO_SPACE;
		}
	}

	return err;
}

errcode_t ocfs2_snprint_tunefs_flags(char *str, size_t size, uint16_t flags)
{
	return ocfs2_snprint_flag_names(ocfs2_tunefs_flag_names,
					str, size, (uint32_t)flags);
}

errcode_t ocfs2_snprint_extent_flags(char *str, size_t size, uint8_t flags)
{
	return ocfs2_snprint_flag_names(ocfs2_extent_flag_names,
					str, size, (uint32_t)flags);
}

errcode_t ocfs2_snprint_refcount_flags(char *str, size_t size, uint8_t flags)
{
	return ocfs2_snprint_flag_names(ocfs2_refcount_flag_names,
					str, size, (uint32_t)flags);
}

errcode_t ocfs2_snprint_cluster_o2cb_flags(char *str, size_t size,
					   uint8_t flags)
{
	return ocfs2_snprint_flag_names(ocfs2_cluster_o2cb_flag_names,
					str, size, (uint32_t)flags);
}

/*
 * If we are asked to clear a feature, we also need to clear any other
 * features that depend on it.
 */
static void ocfs2_feature_clear_deps(ocfs2_fs_options *reverse_set)
{
	int i;

	for(i = 0; ocfs2_supported_features[i].ff_str; i++) {
		if (feature_match(reverse_set,
				  &ocfs2_supported_features[i].ff_flags)) {
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
					       enum ocfs2_mkfs_types fstype,
					       int level,
					       ocfs2_fs_options *feature_set,
					       ocfs2_fs_options *reverse_set)
{
	ocfs2_fs_options level_set;

	if (level == OCFS2_FEATURE_LEVEL_DEFAULT)
		level_set = mkfstypes_features_defaults[fstype];
	else
		level_set = feature_level_defaults[level];

	/*
	 * Ensure that all dependancies are correct in the reverse set.
	 * A reverse set from ocfs2_parse_feature() will be correct, but
	 * a hand-built one might not be.
	 */
	ocfs2_feature_clear_deps(reverse_set);

	/*
	 * Check whether the user asked for a flag to be set and cleared,
	 * which is illegal. The feature_set and reverse_set are both set
	 * by "--fs-features", so they shouldn't collide with each other,
	 * but a hand-built one might have problems.
	 */
	if (!check_feature_flags(feature_set, reverse_set))
		return OCFS2_ET_CONFLICTING_FEATURES;

	/* Now combine all the features the user has set. */
	*dest = level_set;
	merge_features(dest, *feature_set);

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

	/*
	 * Check whether the user asked for a flag to be set and cleared,
	 * which is illegal. The feature_set and reverse_set are both set
	 * by "--fs-features", so they shouldn't collide with each other.
	 */
	if (!check_feature_flags(feature_flags, reverse_flags))
		return OCFS2_ET_CONFLICTING_FEATURES;

	return 0;
}

static int compare_feature_forward(const void *pa, const void *pb)
{
	int ia = *(int *)pa;
	int ib = *(int *)pb;
	struct fs_feature_flags *fa = &ocfs2_supported_features[ia];
	struct fs_feature_flags *fb = &ocfs2_supported_features[ib];

	if (feature_match(&fb->ff_flags,
			  &fa->ff_own_flags))
		return -1;
	if (feature_match(&fa->ff_flags,
			  &fb->ff_own_flags))
		return 1;
	return 0;
}

static int compare_feature_backward(const void *pa, const void *pb)
{
	return compare_feature_forward(pb, pa);
}

static void __feature_foreach(int reverse, ocfs2_fs_options *feature_set,
			      int (*func)(ocfs2_fs_options *feature,
					  void *user_data),
			      void *user_data)
{
	int i, index;
	int num_features = sizeof(ocfs2_supported_features) /
		sizeof(ocfs2_supported_features[0]);
	int indices[num_features];

	index = 0;
	for (i = 0; ocfs2_supported_features[i].ff_str; i++) {
		if (feature_match(feature_set,
				  &ocfs2_supported_features[i].ff_own_flags)) {
			indices[index] = i;
			index++;
		}
	}

	qsort(indices, index, sizeof(indices[0]),
	      reverse ? compare_feature_backward : compare_feature_forward);

	for (i = 0; i < index; i++) {
		if (func(&ocfs2_supported_features[indices[i]].ff_own_flags,
			 user_data))
			break;
	}
}

void ocfs2_feature_foreach(ocfs2_fs_options *feature_set,
			   int (*func)(ocfs2_fs_options *feature,
				       void *user_data),
			   void *user_data)
{
	__feature_foreach(0, feature_set, func, user_data);
}

void ocfs2_feature_reverse_foreach(ocfs2_fs_options *reverse_set,
				   int (*func)(ocfs2_fs_options *feature,
					       void *user_data),
				   void *user_data)
{
	__feature_foreach(1, reverse_set, func, user_data);
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
	fprintf(stdout, "\n");
}

static void printable_mkfs(ocfs2_fs_options *feature_set)
{
	errcode_t err;
	char buf[PATH_MAX];
	ocfs2_fs_options flags;

	fprintf(stdout, "Printable version of mkfs features:\n");

	memset(&flags, 0, sizeof(flags));
	flags.opt_compat = feature_set->opt_compat;
	err = ocfs2_snprint_feature_flags(buf, PATH_MAX, &flags);
	if (err)
		snprintf(buf, PATH_MAX, "An error occurred: %s",
			 error_message(err));
	fprintf(stdout, "COMPAT:\t\t%s\n", buf);

	memset(&flags, 0, sizeof(flags));
	flags.opt_ro_compat = feature_set->opt_ro_compat;
	err = ocfs2_snprint_feature_flags(buf, PATH_MAX, &flags);
	if (err)
		snprintf(buf, PATH_MAX, "An error occurred: %s",
			 error_message(err));
	fprintf(stdout, "RO_COMPAT:\t%s\n", buf);

	memset(&flags, 0, sizeof(flags));
	flags.opt_incompat = feature_set->opt_incompat;
	err = ocfs2_snprint_feature_flags(buf, PATH_MAX, &flags);
	if (err)
		snprintf(buf, PATH_MAX, "An error occurred: %s",
			 error_message(err));
	fprintf(stdout, "INCOMPAT:\t%s\n", buf);

	fprintf(stdout, "\n");
}

static void print_tunefs_flags(void)
{
	errcode_t err;
	char buf[PATH_MAX];

	fprintf(stdout, "Printable s_tunefs_flag:\n");

	err = ocfs2_snprint_tunefs_flags(buf, PATH_MAX,
					 OCFS2_TUNEFS_INPROG_REMOVE_SLOT |
					 OCFS2_TUNEFS_INPROG_DIR_TRAILER);
	if (err)
		snprintf(buf, PATH_MAX, "An error occurred: %s",
			 error_message(err));
	fprintf(stdout, "FLAGS:\t\t%s\n", buf);
	fprintf(stdout, "\n");
}

static void print_extent_flags(void)
{
	errcode_t err;
	char buf[PATH_MAX];

	fprintf(stdout, "Printable e_flags:\n");

	err = ocfs2_snprint_extent_flags(buf, PATH_MAX,
					 OCFS2_EXT_UNWRITTEN |
					 OCFS2_EXT_REFCOUNTED);
	if (err)
		snprintf(buf, PATH_MAX, "An error occurred: %s",
			 error_message(err));
	fprintf(stdout, "FLAGS:\t\t%s\n", buf);
	fprintf(stdout, "\n");
}

static void print_refcount_flags(void)
{
	errcode_t err;
	char buf[PATH_MAX];

	fprintf(stdout, "Printable rf_flags:\n");

	err = ocfs2_snprint_refcount_flags(buf, PATH_MAX,
					   OCFS2_REFCOUNT_TREE_FL |
					   OCFS2_REFCOUNT_LEAF_FL);
	if (err)
		snprintf(buf, PATH_MAX, "An error occurred: %s",
			 error_message(err));
	fprintf(stdout, "FLAGS:\t\t%s\n", buf);
	fprintf(stdout, "\n");
}

static int p_feature(ocfs2_fs_options *feature_set, void *user_data)
{
	int i;

	for (i = 0; ocfs2_supported_features[i].ff_str; i++) {
		if (feature_match(feature_set,
				  &ocfs2_supported_features[i].ff_own_flags))
			fprintf(stdout, " %s",
				ocfs2_supported_features[i].ff_str);
	}

	return 0;
}

static void print_order(int reverse, ocfs2_fs_options *feature_set)
{
	fprintf(stdout, "In this order:");
	if (reverse)
		ocfs2_feature_reverse_foreach(feature_set, p_feature, NULL);
	else
		ocfs2_feature_foreach(feature_set, p_feature, NULL);
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
	err = ocfs2_merge_feature_flags_with_level(&mkfs_features,
						   OCFS2_MKFSTYPE_DEFAULT,
						   level,
						   &set_features,
						   &clear_features);
	if (err) {
		com_err(argv[0], err,
			"while trying to reconcile default and specified features");
		exit(1);
	}

	print_features("\nmkfs.ocfs2 would set these features",
		       &mkfs_features);
	print_order(0, &mkfs_features);
	printable_mkfs(&mkfs_features);
	print_features("tunefs.ocfs2 would set these features",
		       &set_features);
	print_order(0, &set_features);
	print_features("tunefs.ocfs2 would clear these features",
		       &clear_features);
	print_order(1, &clear_features);

	print_tunefs_flags();
	print_extent_flags();
	print_refcount_flags();

	return 0;
}


#endif /* DEBUG_EXE */
