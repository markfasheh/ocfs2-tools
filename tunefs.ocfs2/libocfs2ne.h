/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * libocfs2ne.h
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

/*
 * Adding a capability to ocfs2ne is pretty simple.  You create a source
 * file for the method, define the method structure, and then link that
 * method structure in the appropriate place.  This comment provides some
 * generic details shared by all method types.
 *
 * If you are setting or clearing a filesystem feature, you want to create
 * a struct tunefs_feature with DEFINE_TUNEFS_FEATURE_*().  See those
 * comments for more details on feature methods.
 *
 * If you are creating a more generic operation, you create a struct
 * tunefs_operation with DEFINE_TUNEFS_OP().  See those comments for more
 * details as well.
 *
 * A method should have the following characteristics:
 *
 * - It must be idempotent.  If filesystem is already in the correct
 *   state, the method should do nothing and return success.
 *
 * - It must use tunefs_interact() before writing any changes.  If the
 *   user specified -i, tunefs_interact() will ask the user before
 *   proceeding.  Otherwise, it always returns "go ahead", so you can
 *   always call it safely.
 *
 * - It must use the verbosef() APIs unless output is the point of the
 *   operation.  Errors are reported with tcom_err() or errorf().  Verbose
 *   output uses verbosef().  This way, all output honors the -v/-q
 *   options.  Operations that create output are the exception, because
 *   the user asked for output.  For example, list_sparse really makes no
 *   sense without output.
 *
 * - It should be silent under normal operation.  The user can specify -v
 *   if they want more detail.  Operations that create output don't count
 *   here, of course.  If something may take a long time, a progress
 *   indicator of some sort is OK, but it should use verbosef(VL_OUT) so
 *   that it honors '-q'.
 */

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
	VL_LIB		= 3, 	/* libocfs2ne status */
	VL_DEBUG	= 4, 	/* Debugging output */
};

/* What to do with a feature */
enum tunefs_feature_action {
	FEATURE_NOOP	= 0,
	FEATURE_ENABLE	= 1,
	FEATURE_DISABLE = 2,
};

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

/*
 * Creating a method to enable or disable a filesystem feature is pretty
 * simple.  The source file only needs two functions - one to enable the
 * feature, one to disable it.  It is legal to provide NULL for one of the
 * functions.  ocfs2ne will report "unsupported".  Each function should be
 * idempotent and use tunefs_interact() before writing to the filesystem.
 *
 * A feature should check the flags it needs set or cleared before it can
 * enable or disable.  While ocfs2ne will make sure to run features in
 * order, the user may skip a feature in interactive mode.
 *
 * DEFINE_TUNEFS_FEATURE_COMPAT(), DEFINE_TUNEFS_FEATURE_RO_COMPAT(), or
 * DEFINE_TUNEFS_FEATURE_INCOMPAT() are used as appropriate to define the
 * tunefs_feature structure.  This links the enable and disable functions
 * to the appropriate feature bit.
 *
 * Finally, the feature structure needs to be added to ocfs2ne_features.c.
 *
 * For debugging, a DEBUG_EXE section can be added at the bottom of the
 * source file.  It just needs to pass the struct tunefs_feature to
 * tunefs_feature_main().
 */
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

struct tunefs_operation {
	char		*to_name;
	int		to_open_flags;	/* Flags for tunefs_open() */
	int		(*to_parse_option)(struct tunefs_operation *op,
					   char *arg);
	int		(*to_run)(struct tunefs_operation *op,
				  ocfs2_filesys *fs,
				  int flags);	/* The tunefs_open() flags
						   that mattered */
	void		*to_private;
	char		*to_debug_usage;	/* DEBUG_EXEC usage string  */
};

#define __TUNEFS_OP(_name, _usage, _flags, _parse, _run)		\
{									\
	.to_name		= #_name,				\
	.to_open_flags		= _flags,				\
	.to_parse_option	= _parse,				\
	.to_run			= _run,					\
	.to_debug_usage		= _usage,				\
}

/*
 * Creating a tunefs operation is only a little more complex then a
 * feature.  The source file only requires the run() function to perform
 * the operation.  If the operation needs an argument, it can provide a
 * parse_option() function.  The parse_option() is called while ocfs2ne
 * is processing options, and should provide any sanity checking of the
 * argument (eg, converting a string to a number).  The operation can use
 * op->to_private to pass the option value to run().  Again, the run()
 * function should be idempotent and use tunefs_interact() before writing
 * to the filesystem.
 *
 * DEFINE_TUNEFS_OP() is used to define the tunefs_operation structure.
 *
 * A command-line option needs to be added to ocfs2ne.c for the new
 * operation.  This just means adding a struct tunefs_option that
 * references the struct tunefs_operation.  New operations should be able
 * to use generic_handle_arg() for their option's handle() function.
 *
 * For debugging, a DEBUG_EXE section can be added at the bottom of the
 * source file.  It just needs to pass the struct tunefs_operation to
 * tunefs_op_main().  If a usage string was specified in DEFINE_TUNEFS_OP(),
 * the debugging program will use that usage string.
 */
#define DEFINE_TUNEFS_OP(_name, _usage, _flags, _parse, _run)		\
struct tunefs_operation _name##_op =					\
	__TUNEFS_OP(_name, _usage, _flags, _parse, _run)


/* Sets up argv0, signals, and output buffering */
void tunefs_init(const char *argv0);

/*
 * Filesystem changes that are sensitive to interruption should be wrapped
 * with a block/unblock pair.  The scope should be as narrow as possible,
 * so that a user can interrupt the process without it hanging.
 */
void tunefs_block_signals(void);
void tunefs_unblock_signals(void);

/* Set and clear the various tunefs in-progress bits */
errcode_t tunefs_set_in_progress(ocfs2_filesys *fs, int flag);
errcode_t tunefs_clear_in_progress(ocfs2_filesys *fs, int flag);

/* Turn a string into a number.  Supports K/M/G/T/P suffixes */
errcode_t tunefs_get_number(char *arg, uint64_t *res);

/*
 * Set all journals to new_size.  If new_size is 0, it will set all
 * journals to the size of the largest existing journal.
 */
errcode_t tunefs_set_journal_size(ocfs2_filesys *fs, uint64_t new_size);

/*
 * Send an ioctl() to a live filesystem for online operation.  If the
 * filesystem is mounted and an operation needs to be performed online,
 * tunefs_open() will have already connected to the filesystem.
 */
errcode_t tunefs_online_ioctl(ocfs2_filesys *fs, int op, void *arg);
/*
 * Online operations need to make locks, but the ocfs2_filesys an
 * operation gets may not be the one we used to access the dlm.
 */
errcode_t tunefs_dlm_lock(ocfs2_filesys *fs, const char *lockid,
			  int flags, enum o2dlm_lock_level level);
errcode_t tunefs_dlm_unlock(ocfs2_filesys *fs, char *lockid);

/* Returns the program name from argv0 */
const char *tunefs_progname(void);

/* Prints the version of ocfs2ne */
void tunefs_version(void);

/* Increase and decrease the verbosity level */
void tunefs_verbose(void);
void tunefs_quiet(void);

/* Sets the process interactive */
void tunefs_interactive(void);

/*
 * Output that honors the verbosity level.  tcom_err() is for errcode_t
 * errors.  errorf() is for all other errors.  verbosef() is for verbose
 * output.
 */
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

/*
 * A wrapper for inode scanning.  Calls func() for each valid inode.
 * filetype_mask is a mask of st_mode type bits (eg, S_IFDIR|S_IFREG).
 */
errcode_t tunefs_foreach_inode(ocfs2_filesys *fs, int filetype_mask,
			       errcode_t (*func)(ocfs2_filesys *fs,
						 struct ocfs2_dinode *di,
						 void *user_data),
			       void *user_data);

/* Functions used by the core program sources */

/* Open and cloee a filesystem */
errcode_t tunefs_open(const char *device, int flags,
		      ocfs2_filesys **ret_fs);
errcode_t tunefs_close(ocfs2_filesys *fs);

/*
 * Run a tunefs_operation with its own ocfs2_filesys.  The special
 * error TUNEFS_ET_OPERATION_FAILED means the operation itself failed.
 * It will have handled any error output.  Any other errors are from
 * tunefs_op_run() itself.
 */
errcode_t tunefs_op_run(ocfs2_filesys *master_fs,
			struct tunefs_operation *op);
/* The same, but for tunefs_feature */
errcode_t tunefs_feature_run(ocfs2_filesys *master_fs,
			     struct tunefs_feature *feat);

/*
 * For debugging programs.  These open a filesystem and call
 * tunefs_*_run() as appropriate.
 */
int tunefs_feature_main(int argc, char *argv[], struct tunefs_feature *feat);
int tunefs_op_main(int argc, char *argv[], struct tunefs_operation *op);


#endif  /* _LIBTUNEFS_H */
