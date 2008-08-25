/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * verbose.c
 *
 * Internal routines for verbose output.
 *
 * Copyright (C) 2008 Oracle.  All rights reserved.
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

#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE /* for getopt_long and O_DIRECT */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <et/com_err.h>

#include "tools-internal/verbose.h"

static char progname[PATH_MAX] = "(Unknown)";
static int verbosity = 1;
static int interactive = 0;

void tools_setup_argv0(const char *argv0)
{
	char *pname;
	char pathtmp[PATH_MAX];

	/* This shouldn't care which basename(3) we get */
	snprintf(pathtmp, PATH_MAX, "%s", argv0);
	pname = basename(pathtmp);
	snprintf(progname, PATH_MAX, "%s", pname);
}

/* If all verbosity is turned off, make sure com_err() prints nothing. */
static void quiet_com_err(const char *prog, long errcode, const char *fmt,
			  va_list args)
{
	return;
}

void tools_verbose(void)
{
	verbosity++;
	if (verbosity == 1)
		reset_com_err_hook();
}

void tools_quiet(void)
{
	if (verbosity == 1)
		set_com_err_hook(quiet_com_err);
	verbosity--;
}

static void vfverbosef(FILE *f, int level, const char *fmt, va_list args)
{
	if (level <= verbosity)
		vfprintf(f, fmt, args);
}

static void fverbosef(FILE *f, int level, const char *fmt, ...)
	__attribute__ ((format (printf, 3, 4)));
static void fverbosef(FILE *f, int level, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vfverbosef(f, level, fmt, args);
	va_end(args);
}

void verbosef(enum tools_verbosity_level level, const char *fmt, ...)
{
	va_list args;
	FILE *f = stderr;

	if (level & VL_FLAG_STDOUT) {
		f = stdout;
		level &= ~VL_FLAG_STDOUT;
	}

	va_start(args, fmt);
	vfverbosef(f, level, fmt, args);
	va_end(args);
}

void errorf(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	fverbosef(stderr, VL_ERR, "%s: ", progname);
	vfverbosef(stderr, VL_ERR, fmt, args);
	va_end(args);
}

void tcom_err(errcode_t code, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	com_err_va(progname, code, fmt, args);
	va_end(args);
}

static int vtools_interact(enum tools_verbosity_level level,
			   const char *fmt, va_list args)
{
	char *s, buffer[NAME_MAX];

	vfverbosef(stderr, level, fmt, args);

	s = fgets(buffer, sizeof(buffer), stdin);
	if (s && *s) {
		*s = tolower(*s);
		if (*s == 'y')
			return 1;
	}

	return 0;
}

void tools_interactive(void)
{
	interactive = 1;
}

/* Pass this a question without a newline. */
int tools_interact(const char *fmt, ...)
{
	int rc;
	va_list args;

	if (!interactive)
		return 1;

	va_start(args, fmt);
	rc = vtools_interact(VL_ERR, fmt, args);
	va_end(args);

	return rc;
}

/* Only for "DON'T DO THIS WITHOUT REALLY CHECKING!" stuff */
int tools_interact_critical(const char *fmt, ...)
{
	int rc;
	va_list args;

	va_start(args, fmt);
	rc = vtools_interact(VL_CRIT, fmt, args);
	va_end(args);

	return rc;
}

void tools_version(void)
{
	verbosef(VL_ERR, "%s %s\n", progname, VERSION);
}

const char *tools_progname(void)
{
	return progname;
}

