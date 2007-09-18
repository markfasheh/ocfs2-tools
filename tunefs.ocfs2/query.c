/*
 * query.c
 *
 * ocfs2 tune utility - implements query
 *
 * Copyright (C) 2004, 2007 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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

#include <tunefs.h>
#include <printf.h>

extern ocfs2_filesys *fs_gbl;
extern ocfs2_tune_opts opts;

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
	return print_ulong(stream, info, args, fs_gbl->fs_blocksize);
}

static int handle_clustersize(FILE *stream, const struct printf_info *info,
			      const void *const *args)
{
	return print_ulong(stream, info, args, fs_gbl->fs_clustersize);
}

static int handle_numslots(FILE *stream, const struct printf_info *info,
			   const void *const *args)
{
	return print_ulong(stream, info, args,
			   OCFS2_RAW_SB(fs_gbl->fs_super)->s_max_slots);
}

static int handle_rootdir(FILE *stream, const struct printf_info *info,
			  const void *const *args)
{
	return print_ulong(stream, info, args,
			   OCFS2_RAW_SB(fs_gbl->fs_super)->s_root_blkno);
}

static int handle_sysdir(FILE *stream, const struct printf_info *info,
			 const void *const *args)
{
	return print_ulong(stream, info, args,
			   OCFS2_RAW_SB(fs_gbl->fs_super)->s_system_dir_blkno);
}

static int handle_clustergroup(FILE *stream, const struct printf_info *info,
			       const void *const *args)
{
	return print_ulong(stream, info, args,
			   OCFS2_RAW_SB(fs_gbl->fs_super)->s_first_cluster_group);
}

static int handle_label(FILE *stream, const struct printf_info *info,
			const void *const *args)
{
	char label[OCFS2_MAX_VOL_LABEL_LEN + 1];

	snprintf(label, OCFS2_MAX_VOL_LABEL_LEN + 1,
		 (char *)OCFS2_RAW_SB(fs_gbl->fs_super)->s_label);

	return print_string(stream, info, args, label);
}

static int handle_uuid(FILE *stream, const struct printf_info *info,
		       const void *const *args)
{
	char uuid[OCFS2_VOL_UUID_LEN * 2 + 1];

	uuid_unparse(OCFS2_RAW_SB(fs_gbl->fs_super)->s_uuid, uuid);

	return print_string(stream, info, args, uuid);
}

static int handle_arginfo(const struct printf_info *info, size_t n, int *types)
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

	fmt = malloc(len + 1);
	if (!fmt)
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

/*
 * avoid standard type specifiers: E, F, G, A, C, S, X, L
 */
void print_query(char *queryfmt)
{
	char *fmt;
	int argtype;

	if (parse_printf_format(queryfmt, 1, &argtype)) {
		com_err(opts.progname, 0,
			"Unknown type specifier in the query format");
		return;
	}

	fmt = process_escapes(queryfmt);
	if (!fmt) {
		com_err(opts.progname, OCFS2_ET_NO_MEMORY,
			"while processing escapes in the query format");
		return ;
	}

	register_printf_function('B', handle_blocksize, handle_arginfo);
	register_printf_function('T', handle_clustersize, handle_arginfo);
	register_printf_function('N', handle_numslots, handle_arginfo);
	register_printf_function('R', handle_rootdir, handle_arginfo);
	register_printf_function('Y', handle_sysdir, handle_arginfo);
	register_printf_function('P', handle_clustergroup, handle_arginfo);

	register_printf_function('V', handle_label, handle_arginfo);
	register_printf_function('U', handle_uuid, handle_arginfo);

	fprintf(stdout, fmt);
	free(fmt);
}
