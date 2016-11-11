/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * op_query.c - query operation for tunefs
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

#define _GNU_SOURCE  /* For asprintf() */
#include <stdio.h>
#include <string.h>
#include <printf.h>

#include "ocfs2/ocfs2.h"

#include "libocfs2ne.h"

/* To get around passing arguments to printf() */
static ocfs2_filesys *query_fs;


static void tunefs_inprog_flag_in_str(uint32_t flag, char *buf, size_t len)
{
	errcode_t err;

	err = ocfs2_snprint_tunefs_flags(buf, len, flag);
	if (err)
		tcom_err(err, "while processing inprog flags");
}

static void incompat_flag_in_str(uint32_t flag, char *buf, size_t len)
{
	errcode_t err;
	ocfs2_fs_options flags = {
		.opt_incompat = flag,
	};

	err = ocfs2_snprint_feature_flags(buf, len, &flags);
	if (err)
		tcom_err(err, "while processing feature flags");
}

static void compat_flag_in_str(uint32_t flag, char *buf, size_t len)
{
	errcode_t err;
	ocfs2_fs_options flags = {
		.opt_compat = flag,
	};

	err = ocfs2_snprint_feature_flags(buf, len, &flags);
	if (err)
		tcom_err(err, "while processing feature flags");
}

static void ro_compat_flag_in_str(uint32_t flag, char *buf, size_t len)
{
	errcode_t err;
	ocfs2_fs_options flags = {
		.opt_ro_compat = flag,
	};

	err = ocfs2_snprint_feature_flags(buf, len, &flags);
	if (err)
		tcom_err(err, "while processing feature flags");
}

static int print_ulong(FILE *stream, const struct printf_info *info,
		       const void *const *args, unsigned long val)
{
	char *buffer;
	int len;

	len = asprintf(&buffer, "%lu", val);
	if (len == -1)
		return -1;

	len = fprintf(stream, "%*s", (info->left ? -info->width : info->width),
		      buffer);
	free(buffer);
	return len;
}

static int print_string(FILE *stream, const struct printf_info *info,
			const void *const *args, char *val)
{
	char *buffer;
	int len;

	len = asprintf(&buffer, "%s", val);
	if (len == -1)
		return -1;

	len = fprintf(stream, "%*s", (info->left ? -info->width : info->width),
		      buffer);
	free(buffer);
	return len;
}

static int handle_blocksize(FILE *stream, const struct printf_info *info,
			    const void *const *args)
{
	return print_ulong(stream, info, args, query_fs->fs_blocksize);
}

static int handle_clustersize(FILE *stream, const struct printf_info *info,
			      const void *const *args)
{
	return print_ulong(stream, info, args, query_fs->fs_clustersize);
}

static int handle_numslots(FILE *stream, const struct printf_info *info,
			   const void *const *args)
{
	return print_ulong(stream, info, args,
			   OCFS2_RAW_SB(query_fs->fs_super)->s_max_slots);
}

static int handle_rootdir(FILE *stream, const struct printf_info *info,
			  const void *const *args)
{
	return print_ulong(stream, info, args,
			   OCFS2_RAW_SB(query_fs->fs_super)->s_root_blkno);
}

static int handle_sysdir(FILE *stream, const struct printf_info *info,
			 const void *const *args)
{
	return print_ulong(stream, info, args,
			   OCFS2_RAW_SB(query_fs->fs_super)->s_system_dir_blkno);
}

static int handle_clustergroup(FILE *stream, const struct printf_info *info,
			       const void *const *args)
{
	return print_ulong(stream, info, args,
			   OCFS2_RAW_SB(query_fs->fs_super)->s_first_cluster_group);
}

static int handle_label(FILE *stream, const struct printf_info *info,
			const void *const *args)
{
	char label[OCFS2_MAX_VOL_LABEL_LEN + 1];

	snprintf(label, OCFS2_MAX_VOL_LABEL_LEN + 1, "%s",
		 (char *)OCFS2_RAW_SB(query_fs->fs_super)->s_label);

	return print_string(stream, info, args, label);
}

static int handle_uuid(FILE *stream, const struct printf_info *info,
		       const void *const *args)
{
	return print_string(stream, info, args, query_fs->uuid_str);
}

static int handle_flag(FILE *stream, const struct printf_info *info,
		       const void *const *args, uint32_t flag,
		       void(*flag_func)(uint32_t flag, char *buf, size_t len))
{
	char buf[PATH_MAX]; /* Should be big enough */
	int len = 0;

	*buf = '\0';
	(flag_func)(flag, buf, PATH_MAX);

	if (*buf)
		len = print_string(stream, info, args, buf);

	return len;
}

static int handle_compat(FILE *stream, const struct printf_info *info,
			 const void *const *args)
{
	int len;
	len = handle_flag(stream, info, args,
			  OCFS2_RAW_SB(query_fs->fs_super)->s_feature_compat,
			  compat_flag_in_str);
	if (!len)
		len = print_string(stream, info, args, "None");
	return len;
}

static int handle_incompat(FILE *stream, const struct printf_info *info,
			   const void *const *args)
{
	int len;

	len = handle_flag(stream, info, args,
			  OCFS2_RAW_SB(query_fs->fs_super)->s_feature_incompat,
			  incompat_flag_in_str);

	if (OCFS2_RAW_SB(query_fs->fs_super)->s_tunefs_flag)
		len += handle_flag(stream, info, args,
				   OCFS2_RAW_SB(query_fs->fs_super)->s_tunefs_flag,
				   tunefs_inprog_flag_in_str);

	if (!len)
		len = print_string(stream, info, args, "None");
	return len;
}

static int handle_ro_compat(FILE *stream, const struct printf_info *info,
			    const void *const *args)
{
	int len;

	len =  handle_flag(stream, info, args,
			   OCFS2_RAW_SB(query_fs->fs_super)->s_feature_ro_compat,
			   ro_compat_flag_in_str);
	if (!len)
		len = print_string(stream, info, args, "None");
	return len;
}

static int handle_arginfo(const struct printf_info *info, size_t n, int *types,
		int *size)
{
	return 0;
}

/*
 * \a=0x07, \b=0x08, \t=0x09, \n=0x0a, \v=0x0b, \f=0x0c, \r=0x0d
 */
static char * process_escapes(char *queryfmt)
{
	char *fmt;
	int i, j;
	int len;

	len = strlen(queryfmt);

	if (ocfs2_malloc0(len + 1, &fmt))
		return NULL;

	for(i = 0, j = 0; i < len; ) {
		if (queryfmt[i] != '\\')
			fmt[j++] = queryfmt[i++];
		else {
			switch(queryfmt[i + 1]) {
			case 'a':
				fmt[j++] = 0x07;
				break;
			case 'b':
				fmt[j++] = 0x08;
				break;
			case 't':
				fmt[j++] = 0x09;
				break;
			case 'n':
				fmt[j++] = 0x0A;
				break;
			case 'v':
				fmt[j++] = 0x0B;
				break;
			case 'f':
				fmt[j++] = 0x0C;
				break;
			case 'r':
				fmt[j++] = 0x0D;
				break;
			default:
				fmt[j++] = queryfmt[i];
				fmt[j++] = queryfmt[i + 1];
				break;
			}
			i += 2;
		}
	}

	return fmt;
}

static int query_parse_option(struct tunefs_operation *op, char *arg)
{
	int argtype;

	if (!arg) {
		errorf("No query format specified\n");
		return 1;
	}

	/*
	 * We want to make sure that there are no "standard" specifiers in
	 * the format, only our own.
	 */
	if (parse_printf_format(arg, 1, &argtype)) {
		errorf("Unknown type specifier in the query format: "
		       "\"%s\"\n",
		       arg);
		return 1;
	}

	op->to_private = arg;

	return 0;
}


/*
 * When creating printf fields for ourselves, we need to avoid the
 * standard specifiers.  All lowercase specifiers are reserved by C99.
 * Reserved uppercase specifiers are: E, F, G, A, C, S, X, L
 */
static int query_run(struct tunefs_operation *op, ocfs2_filesys *fs,
		     int flags)
{
	char *queryfmt = op->to_private;
	char *fmt;

	fmt = process_escapes(queryfmt);
	if (!fmt) {
		tcom_err(TUNEFS_ET_NO_MEMORY,
			 "while escaping the query format");
		return 1;
	}

	register_printf_function('B', handle_blocksize, handle_arginfo);
	register_printf_function('T', handle_clustersize, handle_arginfo);
	register_printf_function('N', handle_numslots, handle_arginfo);
	register_printf_function('R', handle_rootdir, handle_arginfo);
	register_printf_function('Y', handle_sysdir, handle_arginfo);
	register_printf_function('P', handle_clustergroup, handle_arginfo);

	register_printf_function('V', handle_label, handle_arginfo);
	register_printf_function('U', handle_uuid, handle_arginfo);

	register_printf_function('M', handle_compat, handle_arginfo);
	register_printf_function('H', handle_incompat, handle_arginfo);
	register_printf_function('O', handle_ro_compat, handle_arginfo);

	query_fs = fs;
	fprintf(stdout, fmt);
	query_fs = NULL;

	ocfs2_free(&fmt);

	return 0;
}

DEFINE_TUNEFS_OP(query,
		 "Usage: op_query [opts] <device> <query-format>\n",
		 TUNEFS_FLAG_RO,
		 query_parse_option,
		 query_run);

#ifdef DEBUG_EXE
int main(int argc, char *argv[])
{
	return tunefs_op_main(argc, argv, &query_op);
}
#endif
