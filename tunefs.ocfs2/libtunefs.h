/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * libtunefs.h
 *
 * tunefs helper library prototypes.
 *
 * Copyright (C) 2004, 2008 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef _LIBTUNEFS_H
#define _LIBTUNEFS_H

#define PROGNAME "tunefs.ocfs2"

/* Flags for tunefs_open() */
#define TUNEFS_FLAG_RO		0x00
#define TUNEFS_FLAG_RW		0x01
#define TUNEFS_FLAG_ONLINE	0x02	/* Operation can run online */
#define TUNEFS_FLAG_NOCLUSTER	0x04	/* Operation does not need the
					   cluster stack */
#define TUNEFS_FLAG_ALLOCATION	0x08	/* Operation will use the
					   allocator */

/* Verbosity levels for verbosef/errorf/tcom_err */
#define VL_FLAG_STDOUT	0x100	/* or'd with a level, output to stdout */
enum tunefs_verbosity_level {
	VL_CRIT	 	= 0,	/* Don't use this!  I still haven't
				   thought of anything so critical that
				   -q should be ignored */
	VL_ERR		= 1,	/* Error messages */

/* Regular output is the same level as errors */
#define VL_OUT		(VL_ERR | VL_FLAG_STDOUT)

	VL_APP		= 2,	/* Verbose application status */
	VL_LIB		= 3, 	/* libtunefs status */
	VL_DEBUG	= 4, 	/* Debugging output */
};

/* What to do with a feature */
enum tunefs_feature_action {
	FEATURE_NOOP	= 0,
	FEATURE_ENABLE	= 1,
	FEATURE_DISABLE = 2,
};

struct tunefs_operation {
	char	*to_name;
	char	*to_usage;	/* Usage string */
	int	to_open_flags;	/* Flags for tunefs_open() */
	int	(*to_parse_option)(char *arg, void *user_data);
	int	(*to_run)(ocfs2_filesys *fs,
			  int flags,	/* The tunefs_open() flags that
					   mattered */
			  void *user_data);
	void	*to_user_data;
};

#define __TUNEFS_OP(_name, _usage, _flags, _parse, _run, _data)		\
{									\
	.to_name		= #_name,				\
	.to_usage		= _usage,				\
	.to_open_flags		= _flags,				\
	.to_parse_option	= _parse,				\
	.to_run			= _run,					\
	.to_user_data		= _data,				\
}
#define DEFINE_TUNEFS_OP(_name, _usage, _flags, _parse, _run, _data)	\
struct tunefs_operation _name##_op =					\
	__TUNEFS_OP(_name, _usage, _flags, _parse, _run, _data)

struct tunefs_feature {
	char	*tf_name;
	ocfs2_fs_options	tf_feature;	/* The feature bit is set
						   in the appropriate
						   field */
	int			tf_open_flags;	/* Flags for tunefs_open().
						   Like operations, the
						   ones that mattered are
						   passed to the enable and
						   disable functions */
	int			(*tf_enable)(ocfs2_filesys *fs, int flags);
	int			(*tf_disable)(ocfs2_filesys *fs, int flags);
	enum tunefs_feature_action	tf_action;
};

#define __TUNEFS_FEATURE(_name, _flags, _compat, _ro_compat, _incompat,	\
			 _enable, _disable)				\
{									\
	.tf_name		= #_name,				\
	.tf_open_flags		= _flags,				\
	.tf_feature		= {					\
		.opt_compat	= _compat,				\
		.opt_ro_compat	= _ro_compat,				\
		.opt_incompat	= _incompat,				\
	},								\
	.tf_enable		= _enable,				\
	.tf_disable		= _disable,				\
}
#define DEFINE_TUNEFS_FEATURE_COMPAT(_name, _bit, _flags, _enable,	\
				     _disable)				\
struct tunefs_feature _name##_feature =					\
	__TUNEFS_FEATURE(_name, _flags, _bit, 0, 0, _enable, _disable)
#define DEFINE_TUNEFS_FEATURE_RO_COMPAT(_name, _bit, _flags, _enable,	\
					_disable)			\
struct tunefs_feature _name##_feature =					\
	__TUNEFS_FEATURE(_name, _flags, 0, _bit, 0, _enable, _disable)
#define DEFINE_TUNEFS_FEATURE_INCOMPAT(_name, _bit, _flags, _enable,	\
				       _disable)			\
struct tunefs_feature _name##_feature =					\
	__TUNEFS_FEATURE(_name, _flags, 0, 0, _bit, _enable, _disable)

int tunefs_feature_main(int argc, char *argv[], struct tunefs_feature *feat);
int tunefs_main(int argc, char *argv[], struct tunefs_operation *op);

/* Handles generic option processing (-h, -v, etc), then munges argc and
 * argv to pass back to the calling application */
void tunefs_init(int *argc, char ***argv, const char *usage);
void tunefs_block_signals(void);
void tunefs_unblock_signals(void);
errcode_t tunefs_open(const char *device, int flags,
		      ocfs2_filesys **ret_fs);
errcode_t tunefs_close(ocfs2_filesys *fs);
errcode_t tunefs_set_in_progress(ocfs2_filesys *fs, int flag);
errcode_t tunefs_clear_in_progress(ocfs2_filesys *fs, int flag);
errcode_t tunefs_get_number(char *arg, uint64_t *res);

errcode_t tunefs_set_journal_size(ocfs2_filesys *fs, uint64_t new_size);
errcode_t tunefs_online_ioctl(ocfs2_filesys *fs, int op, void *arg);

void tunefs_usage(void);
void tunefs_verbose(void);
void tunefs_quiet(void);
void verbosef(enum tunefs_verbosity_level level, const char *fmt, ...)
	__attribute__ ((format (printf, 2, 3)));
void errorf(const char *fmt, ...)
	__attribute__ ((format (printf, 1, 2)));
void tcom_err(errcode_t code, const char *fmt, ...)
	__attribute__ ((format (printf, 2, 3)));
int tunefs_interact(const char *fmt, ...)
	__attribute__ ((format (printf, 1, 2)));
int tunefs_interact_critical(const char *fmt, ...)
	__attribute__ ((format (printf, 1, 2)));

#endif  /* _LIBTUNEFS_H */
