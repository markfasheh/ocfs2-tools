/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * verbose.h
 *
 * Internal verbose output functions.
 *
 * Copyright (C) 2008 Oracle.  All rights reserved.
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

#ifndef _INTERNAL_VERBOSE_H
#define _INTERNAL_VERBOSE_H

/* Verbosity levels for verbosef/errorf/tcom_err */
#define VL_FLAG_STDOUT	0x100	/* or'd with a level, output to stdout */
enum tools_verbosity_level {
	VL_CRIT	 	= 0,	/* Don't use this!  I still haven't
				   thought of anything so critical that
				   -q should be ignored */
	VL_ERR		= 1,	/* Error messages */

/* Regular output is the same level as errors */
#define VL_OUT		(VL_ERR | VL_FLAG_STDOUT)

	VL_APP		= 2,	/* Verbose application status */
	VL_LIB		= 3, 	/* Status from shared code */
	VL_DEBUG	= 4, 	/* Debugging output */
};

/* Call this to set the program name */
void tools_setup_argv0(const char *argv0);

/* Returns the program name from argv0 */
const char *tools_progname(void);

/* Prints the tools version */
void tools_version(void);

/* Increase and decrease the verbosity level */
void tools_verbose(void);
void tools_quiet(void);

/* Sets the process interactive */
void tools_interactive(void);

/*
 * Output that honors the verbosity level.  tcom_err() is for errcode_t
 * errors.  errorf() is for all other errors.  verbosef() is for verbose
 * output.
 */
void verbosef(enum tools_verbosity_level level, const char *fmt, ...)
	__attribute__ ((format (printf, 2, 3)));
void errorf(const char *fmt, ...)
	__attribute__ ((format (printf, 1, 2)));
void tcom_err(errcode_t code, const char *fmt, ...)
	__attribute__ ((format (printf, 2, 3)));
int tools_interact(const char *fmt, ...)
	__attribute__ ((format (printf, 1, 2)));
int tools_interact_critical(const char *fmt, ...)
	__attribute__ ((format (printf, 1, 2)));

#endif  /* _INTERNAL_VERBOSE_H */
