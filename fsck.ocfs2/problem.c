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
#include <ctype.h>
#include <signal.h>

#include "ocfs2.h"

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

/* this had better be null terminated */
static void print_wrapped(char *str)
{
	size_t left = strlen(str);
	size_t target, width = 80; /* XXX do like e2fsck */
	int i, j;

	target = width;

	while (left > 0) {
		/* skip leading space in a line */
		for(;*str && isspace(*str); left--, str++)
			; 

		if (left == 0)
			break;

		/* just dump it if there isn't enough left */
		if (left <= target) {
			printf("%s", str);
			break;
		}

		/* back up if we break mid-word */
		for (i = target - 1; i > 0 && !isspace(str[i]); i--)
			;

		/* see how enormous this broken word is */
		for (j = target - 1; j < left && !isspace(str[j]); j++)
			;

		j = j - i + 1; /* from offset to len */

		/* just include the word if it itself is longer than a line */
		if (j > target)
			i += j;

		i++; /* from offset to len */

		printf("%.*s", i, str);

		left -= i;
		str += i;

		/* only add a newline if we cleanly wrapped on a small word.
		 * otherwise where we start will depend on where we finished
		 * this crazy long line */
		target = width;
		if (i < target) 
			printf("\n");
		else
			target -= (i % width);
	}

	fflush(stdout);
}

/* 
 * this checks the user's intent.  someday soon it will check command line flags
 * and have a notion of grouping, as well.  The caller is expected to provide
 * a fully formed question that isn't terminated with a newline.
 */
int prompt(o2fsck_state *ost, unsigned flags, const char *fmt, ...)
{
	va_list ap;
	int c, ans = 0;
	static char fatal[] = " If you answer no fsck will not be able to "
			      "continue and will exit.";
	static char yes[] = " <y> ", no[] = " <n> ";
	char *output;
	size_t len, part;

	/* paranoia for jokers that claim to default to both */
	if((flags & PY) && (flags & PN))
		flags &= ~PY;

	len = vsnprintf(NULL, 0, fmt, ap);
	if (len < 0) {
		perror("vsnprintf failed when trying to bulid an output "
		       "buffer");
		exit(FSCK_ERROR);
	}

	if (flags & PF)
		len += sizeof(fatal); /* includes null */

	if (flags & (PY|PN))
		len += sizeof(yes); /* includes null */

	output = malloc(len);
	if (output == NULL) {
		perror("malloc failed when trying to bulid an output buffer");
		exit(FSCK_ERROR);
	}

	va_start(ap, fmt);
	part = vsnprintf(output, len, fmt, ap);
	va_end(ap);
	if (part < 0) {
		perror("vsnprintf failed when trying to bulid an output "
		       "buffer");
		exit(FSCK_ERROR);
	}

	if (flags & PF)
		strcat(output, fatal);

	if (!ost->ost_ask) {
		ans = ost->ost_answer ? 'y' : 'n';
	} else {
		if (flags & PY)
			strcat(output, yes);
		else if (flags & PN)
			strcat(output, no);
	}

	print_wrapped(output);
	free(output);

	/* no curses, no nothin.  overly regressive? */
	while (!ans && (c = read_a_char(fileno(stdin))) != EOF) {

		if (c == 3) {
			printf("ctl-c pressed, aborting.\n");
			exit(FSCK_ERROR);
		}

		if (c == 27) {
			printf("ESC pressed, aborting.\n");
			exit(FSCK_ERROR);
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

	if (flags & PF) {
		printf("fsck cannot continue.  Exiting.\n");
		exit(FSCK_ERROR);
	}

	return ans == 'y';
}
