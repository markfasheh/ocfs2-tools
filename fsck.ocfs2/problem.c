/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * problem.c
 *
 * These routines serve the same purpose as e2fsck's "fix_problem()"
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
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
 * Authors: Zach Brown
 */
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <termios.h>

#include "ocfs2.h"

#include "problem.h"
#include "util.h"

/* 
 * when a caller cares why read() failed we can bother to communicate
 * the error.  
 *
 * XXX I wonder this termios junk is really the best way.  I bet we want
 * to at least make a passing attempt to not be killed while we have
 * the terminal all messed up.
 */
static int read_a_char(int fd)
{
	struct termios orig, new;
	char c;
	ssize_t ret;

	/* turn off buffering and echoing and encourage single character
	 * reads */
	tcgetattr(0, &orig);
	new = orig;
	new.c_lflag &= ~(ICANON | ECHO);
	new.c_cc[VMIN] = 1;
	new.c_cc[VTIME] = 0;
	tcsetattr (0, TCSANOW, &new);

	ret = read(fd, &c, sizeof(c));

	tcsetattr (0, TCSANOW, &orig);

	if (ret != sizeof(c))
		return EOF;

	return c;
}

/* 
 * this checks the user's intent.  someday soon it will check command line flags
 * and have a notion of grouping, as well
 */
int should_fix(o2fsck_state *ost, unsigned flags, const char *fmt, ...)
{
	va_list ap;
	int c;

	/* paranoia for jokers that claim to default to both */
	if((flags & FIX_DEFYES) && (flags & FIX_DEFNO))
		flags &= ~(FIX_DEFYES|FIX_DEFNO);

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);

	if (!ost->ost_ask) {
		if (ost->ost_answer)
			printf("  Fixing.\n");
		else
			printf("  Ignoring\n");
		return ost->ost_answer;
	}

	printf(" Fix? ");
	if (flags & FIX_DEFYES)
		printf(" <y> ");
	else if (flags & FIX_DEFNO)
		printf(" <n> ");

	fflush(stdout);

	/* no curses, no nothin.  overly regressive? */
	while ((c = read_a_char(fileno(stdin))) != EOF) {

		printf("read '%c'\n", c);

		/* XXX control-c, we're done? */
		if (c == 3) {
			printf("cancelled!\n");
			exit(FSCK_ERROR);
		}

		/* straight answers */
		if (c == 'y' || c == 'Y')
			return 1;
		if (c == 'n' || c == 'N')
			return 0;

		/* space or CR lead to applying the optional default */
		if (c == ' ' || c == '\n') {
			if (flags & FIX_DEFYES)
				return 1;
			if (flags & FIX_DEFNO)
				return 0;
		}

		/* otherwise keep asking */
	}

	printf("input failed?\n");
	exit(FSCK_ERROR);

	return 0;
}
