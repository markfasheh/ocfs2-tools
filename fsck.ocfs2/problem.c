/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * Copyright (C) 1993-2004 by Theodore Ts'o.
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301 USA.
 *
 * --
 *
 * prompt() asks the user whether a given problem should be fixed or not.
 * "problem.c" is derived from the baroque e2fsck origins of this concept.
 *
 * XXX
 * 	The significant gap here is in persistent answers.  Often one wants
 * 	to tell fsck to stop asking the same freaking question over and over
 * 	until a different question is asked.
 */
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <termios.h>
#include <ctype.h>
#include <signal.h>

#include "ocfs2/ocfs2.h"

#include "problem.h"
#include "util.h"

/* XXX more of fsck will want this.. */
static sig_atomic_t interrupted = 0;
static void handle_sigint(int sig)
{
	interrupted = 1;
}

/* 
 * when a caller cares why read() failed we can bother to communicate
 * the error.  
 */
static int read_a_char(int fd)
{
	struct termios orig, new;
	char c;
	ssize_t ret;
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handle_sigint;
	sa.sa_flags = SA_ONESHOT; /* !SA_RESTART */
	sigaction(SIGINT, &sa, NULL);

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

	if (interrupted)
		return 3;

	if (ret != sizeof(c))
		return EOF;

	return c;
}

/* 
 * this checks the user's intent.  someday soon it will check command line flags
 * and have a notion of grouping, as well.  The caller is expected to provide
 * a fully formed question that isn't terminated with a newline.
 */
int prompt_input(o2fsck_state *ost, unsigned flags, struct prompt_code code,
		const char *fmt, ...)
{
	va_list ap;
	int c, ans = 0;
	static char yes[] = " <y> ", no[] = " <n> ";

	/* paranoia for jokers that claim to default to both */
	if((flags & PY) && (flags & PN))
		flags &= ~PY;

	printf("[%s] ", code.str);

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);

	if (!ost->ost_ask) {
		ans = ost->ost_answer ? 'y' : 'n';
	} else {
		if (flags & PY)
			printf("%s", yes);
		else if (flags & PN)
			printf("%s", no);
	}

	fflush(stdout);

	/* no curses, no nothin.  overly regressive? */
	while (!ans && (c = read_a_char(fileno(stdin))) != EOF) {

		if (c == 3) {
			printf("ctl-c pressed, aborting.\n");
			exit(FSCK_ERROR|FSCK_CANCELED);
		}

		if (c == 27) {
			printf("ESC pressed, aborting.\n");
			exit(FSCK_ERROR|FSCK_CANCELED);
		}

		c = tolower(c);

		/* space or CR lead to applying the optional default */
		if (c == ' ' || c == '\n') {
			if (flags & PY)
				c = 'y';
			else if (flags & PN)
				c = 'n';
		}

		if (c == 'y' || c == 'n') {
			ans = c;
			break;
		}

		/* otherwise keep asking */
	}

	if (!ans) {
		printf("input failed, aborting.\n");
		exit(FSCK_ERROR);
	}

	/* this is totally silly. */
	if (!ost->ost_ask) 
		printf(" %c\n", ans);
	else
		printf("%c\n", ans);

	return ans == 'y';
}
