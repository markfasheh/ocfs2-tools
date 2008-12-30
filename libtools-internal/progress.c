/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * progress.c
 *
 * Internal routines progress output.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <assert.h>
#include <unistd.h>
#include <inttypes.h>

#include "ocfs2-kernel/kernel-list.h"
#include "tools-internal/progress.h"
#include "libtools-internal.h"

enum progress_length {
	PROGRESS_TRUNC,
	PROGRESS_SHORT,
	PROGRESS_LONG,
};

#define TRUNC_LEN 3
#define PERCENTAGE_LEN 5
#define SPINNER_LEN 2

struct tools_progress {
	struct list_head p_list;
	enum progress_length p_len;
	char *p_long_name;
	unsigned int p_long_name_len;
	char *p_short_name;
	unsigned int p_short_name_len;
	uint64_t p_current;
	uint64_t p_count;
	unsigned int p_percent;
	int p_spinner_pos;
};

#define PROGRESS_OPEN "["
#define PROGRESS_SEP " > "
#define PROGRESS_CLOSE "]"
#define PROGRESS_ELIPS "... "

static const char spinner[] = "\\|/-";
static char nextline;

static LIST_HEAD(progresses);

/* When did we last update the progress */
static unsigned int last_tick;

/* Are we displaying progress statistics */
static int progress_on = 0;

/* A fake progress structure to pass around when we're disabled */
static struct tools_progress disabled_prog;

/*
 * A buffer for storing the current progress output.  That way, we can
 * replay it.
 *
 * If the terminal is 80 characters or less, or we can't allocate an
 * appropriately sized progbuf, we use a static one.  The extra 2 characters
 * are for nextline and the NUL.
 */
#define DEFAULT_WIDTH 80
#define PROGBUF_EXTRA 2
static char static_progbuf[DEFAULT_WIDTH + PROGBUF_EXTRA];
static char *progbuf = static_progbuf;
static unsigned int progbuf_len = DEFAULT_WIDTH + PROGBUF_EXTRA;

/*
 * If we've updated the progress within the last 1/8th of a second, there
 * is no point in doing it again.  Tick algorithm stolen from e2fsck.
 */
static int check_tick(void)
{
	unsigned int tick;
	struct timeval tv;

	gettimeofday(&tv, NULL);
	tick = (tv.tv_sec << 3) + (tv.tv_usec / (1000000 / 8));
	if (tick == last_tick)
		return 0;

	last_tick = tick;

	return 1;
}

static unsigned int calc_percent(uint64_t num, uint64_t dem)
{
	double percent = ((double)num) / ((double)dem);

	return (unsigned int)((100.0 * percent) + 0.5);
}

/* If the visual percentage hasn't change, there's no point in updating. */
static int check_percent(struct tools_progress *prog)
{
	unsigned int new_percent;

	/* An unbounded progress always steps */
	if (!prog->p_count)
		return 1;

	if (prog->p_current >= prog->p_count)
		prog->p_current = prog->p_count;

	new_percent = calc_percent(prog->p_current, prog->p_count);

	if (new_percent == prog->p_percent)
		return 0;

	prog->p_percent = new_percent;
	return 1;
}

static void step_spinner(struct tools_progress *prog)
{
	prog->p_spinner_pos = (prog->p_spinner_pos + 1) & 3;
}

static void progress_length_reset(void)
{
	struct list_head *p;
	struct tools_progress *prog;

	list_for_each(p, &progresses) {
		prog = list_entry(p, struct tools_progress, p_list);
		prog->p_len = PROGRESS_LONG;
	}
}

static size_t length_one_prog(struct tools_progress *prog)
{
	size_t len = 0;

	switch (prog->p_len) {
		case PROGRESS_LONG:
			len += prog->p_long_name_len;
			break;
		case PROGRESS_SHORT:
			len += prog->p_short_name_len;
			break;
		case PROGRESS_TRUNC:
			len += TRUNC_LEN;
			break;
		default:
			assert(0);
			break;
	}

	if (prog->p_count)
		len += PERCENTAGE_LEN;
	else
		len += SPINNER_LEN;

	return len;
}

static unsigned int progress_length_check(void)
{
	unsigned int len = 0;
	int first = 1;
	struct list_head *p;
	struct tools_progress *prog;

	assert(!list_empty(&progresses));

	list_for_each(p, &progresses) {
		prog = list_entry(p, struct tools_progress, p_list);

		if (first) {
			len += strlen(PROGRESS_OPEN);
			first = 0;
		} else
			len += strlen(PROGRESS_SEP);

		len += length_one_prog(prog);
	}
	len += strlen(PROGRESS_CLOSE);

	return len;
}

static int progress_length_shrink(void)
{
	struct list_head *p;
	struct tools_progress *prog = NULL;
	enum progress_length len = PROGRESS_LONG;

	/*
	 * We start from the longest length.  We lower that max length
	 * if we see a shorter one.  When we then see the boundary (a
	 * longer length after a shorter one), we break out.
	 */
	list_for_each(p, &progresses) {
		prog = list_entry(p, struct tools_progress, p_list);
		if (len > prog->p_len)
			len = prog->p_len;
		else if (len < prog->p_len)
			break;
		prog = NULL;
	}

	/*
	 * If there was no boundary, all progresses had the same length.
	 * shrink the first one.
	 */
	if (!prog)
		prog = list_entry(progresses.next, struct tools_progress,
				  p_list);

	/*
	 * If the one we want to shrink already is at PROGRESS_TRUNC, we
	 * can shrink no more.  Return false.
	 */
	if (prog->p_len == PROGRESS_TRUNC)
		return 0;

	prog->p_len--;
	return 1;
}


static unsigned int check_display(void)
{
	char *cols = getenv("COLUMNS");
	char *tmpbuf;
	unsigned int tmp, columns = DEFAULT_WIDTH;

	if (cols) {
		tmp = atoi(cols);
		if (tmp)
			columns = tmp;
	}

	tmp = columns + PROGBUF_EXTRA;
	/* Do we need more space for this width? */
	if (tmp > progbuf_len) {
		tmpbuf = malloc(sizeof(char) * tmp);
		if (tmpbuf) {
			progbuf_len = tmp;
			memset(tmpbuf, 0, tmp);
			if (progbuf != static_progbuf)
				free(progbuf);
			progbuf = tmpbuf;
			/*
			 * We just grew the buffer, so try long progress
			 * output again.
			 */
			progress_length_reset();
		} else {
			/*
			 * We couldn't allocate enough space, so report
			 * what we can actually use.
			 */
			columns = progbuf_len - PROGBUF_EXTRA;
		}
	}

	return columns;
}

static size_t print_one_prog(struct tools_progress *prog, char *buf,
			     size_t len)
{
	int offset = 0;
	size_t ret;

	switch (prog->p_len) {
		case PROGRESS_LONG:
			ret = snprintf(buf + offset, len - offset,
				       "%s", prog->p_long_name);
			break;
		case PROGRESS_SHORT:
			ret = snprintf(buf + offset, len - offset,
				       "%s", prog->p_short_name);
			break;
		case PROGRESS_TRUNC:
			ret = snprintf(buf + offset, len - offset,
				       "%.*s", TRUNC_LEN,
				       prog->p_short_name);
			break;
		default:
			assert(0);
			break;
	}
	offset += ret;

	if (prog->p_count)
		ret = snprintf(buf + offset, len - offset,
			       " %3u%%", prog->p_percent);
	else
		ret = snprintf(buf + offset, len - offset, " %c",
			       spinner[prog->p_spinner_pos & 3]);
	offset += ret;

	return offset;
}

static void print_trailer(char *buf, size_t len)
{
	size_t ret;
	unsigned int offset = 0;

	ret = snprintf(buf + offset, len - offset, "%s",
		       PROGRESS_CLOSE);
	offset += ret;
	assert(offset <= len);
	ret = snprintf(buf + offset, len - offset, "%c", nextline);
	assert(ret < (len - offset));
}

static void progress_printf(unsigned int columns)
{
	unsigned int offset = 0;
	size_t ret;
	int first = 1;
	struct list_head *p;
	struct tools_progress *prog = NULL;

	if (list_empty(&progresses))
		return;

	list_for_each(p, &progresses) {
		prog = list_entry(p, struct tools_progress, p_list);

		if (first) {
			ret = snprintf(progbuf + offset,
				       columns - offset,
				       "%s", PROGRESS_OPEN);
			first = 0;
		} else
			ret = snprintf(progbuf + offset,
				       columns - offset,
				       "%s", PROGRESS_SEP);
		offset += ret;

		offset += print_one_prog(prog, progbuf + offset,
					 columns - offset);
		assert(offset < columns);
	}

	/*
	 * From here on out, we use progbuf_len instead of columns.  Our
	 * earlier calculations should have gotten this right.
	 */
	assert(offset < columns);
	print_trailer(progbuf + offset, progbuf_len - offset);
}

static void truncate_printf(unsigned int columns)
{
	struct tools_progress *last =
		list_entry(progresses.prev, struct tools_progress, p_list);
	size_t ret, len = length_one_prog(last);
	unsigned int offset = 0;

	if ((len + strlen(PROGRESS_CLOSE) + strlen(PROGRESS_ELIPS)) <=
	    columns) {
		ret = snprintf(progbuf + offset, columns - offset, "%s",
			       PROGRESS_ELIPS);
		offset += ret;
		ret = print_one_prog(last, progbuf + offset,
				     columns - offset);
		offset += ret;
		assert(offset < columns);
		print_trailer(progbuf + offset, progbuf_len - offset);
	} else {
		/* Give up, no progress */
		progbuf[0] = '\0';
	}
}

static void progress_compute(void)
{
	unsigned int columns = check_display();
	int truncate = 0;

	while (progress_length_check() > columns) {
		truncate = !progress_length_shrink();
		if (truncate)
			break;
	}

	if (truncate)
		truncate_printf(columns);
	else
		progress_printf(columns);
}

static void progress_clear(void)
{
	unsigned int columns = check_display();

	memset(progbuf, ' ', columns);
	snprintf(progbuf + columns, progbuf_len - columns, "%c", nextline);
}

static void progress_write(void)
{
	printf("%s", progbuf);
}

static void tools_progress_free(struct tools_progress *prog)
{
	if (prog->p_long_name)
		free(prog->p_long_name);
	if (prog->p_short_name)
		free(prog->p_short_name);
	free(prog);
}

static struct tools_progress *tools_progress_alloc(const char *long_name,
						   const char *short_name,
						   uint64_t count)
{
	struct tools_progress *prog;

	prog = malloc(sizeof(struct tools_progress));
	if (!prog)
		goto out;

	memset(prog, 0, sizeof(struct tools_progress));
	prog->p_long_name = strdup(long_name ? long_name : "");
	prog->p_short_name = strdup(short_name ? short_name : long_name);
	if (!prog->p_long_name || !prog->p_short_name) {
		tools_progress_free(prog);
		prog = NULL;
		goto out;
	}

	prog->p_long_name_len = strlen(prog->p_long_name);
	prog->p_short_name_len = strlen(prog->p_short_name);
	prog->p_count = count;

out:
	return prog;
}


/*
 * API for libtools-internal only
 */

void tools_progress_clear(void)
{
	if (!progress_on)
		return;

	if (list_empty(&progresses))
		return;

	/*
	 * We only need to wipe the line if are doing terminal-based
	 * progress.
	 */
	if (nextline != '\r')
		return;

	progress_clear();
	progress_write();
}

void tools_progress_restore(void)
{
	if (!progress_on)
		return;

	/* Same here */
	if (list_empty(&progresses))
		return;
	if (nextline != '\r')
		return;

	progress_compute();
	progress_write();
}

int tools_progress_enabled(void)
{
	return progress_on;
}


/*
 * Public API
 */

void tools_progress_enable(void)
{
	progress_on = 1;

	if (!list_empty(&progresses))
		return;

	if (isatty(STDOUT_FILENO))
		nextline = '\r';
	else
		nextline = '\n';

}

void tools_progress_disable(void)
{
	progress_on = 0;
}

struct tools_progress *tools_progress_start(const char *long_name,
					    const char *short_name,
					    uint64_t count)
{
	struct tools_progress *prog;

	if (!progress_on)
		return &disabled_prog;

	prog = tools_progress_alloc(long_name, short_name, count);
	if (!prog)
		goto out;

	list_add_tail(&prog->p_list, &progresses);
	tools_progress_clear();
	progress_length_reset();
	progress_compute();
	progress_write();

out:
	return prog;
}

void tools_progress_step(struct tools_progress *prog, unsigned int step)
{
	if (prog == &disabled_prog)
		return;

	prog->p_current += step;

	if (!check_percent(prog))
		return;
	if (!check_tick() && (prog->p_percent != 100) &&
	    (!prog->p_count || (prog->p_percent != 0)))
		return;

	if (!prog->p_count)
		step_spinner(prog);

	progress_compute();
	progress_write();
}

void tools_progress_stop(struct tools_progress *prog)
{
	if (prog == &disabled_prog)
		return;

	tools_progress_clear();

	list_del(&prog->p_list);
	tools_progress_free(prog);

	if (!list_empty(&progresses)) {
		progress_length_reset();
		tools_progress_restore();
	}
}

#ifdef DEBUG_EXE
#include <time.h>

static int run_steps(const char *ln, const char *sn, int count,
		     int (*func)(void))
{
	int i, ret = 0;
	struct tools_progress *prog;
	struct timespec ts = {
		.tv_nsec = 100000000,
	};

	prog = tools_progress_start(ln, sn, count > 0 ? count : 0);
	if (!prog)
		return 1;

	if (count < 0)
		count = -count;
	for (i = 0; i < count; i++) {
		if (func)
			ret = func();
		if (ret)
			break;
		tools_progress_step(prog, 1);
		nanosleep(&ts, NULL);
	}
	tools_progress_stop(prog);

	return ret;
}

static int middle(void)
{
	static int try = 0;
	char lbuf[100], sbuf[100];

	try++;
	snprintf(lbuf, 100, "This is middle %d", try);
	snprintf(sbuf, 100, "middle%d", try);
	return run_steps(lbuf, sbuf, -7, NULL);
}

static int outer(void)
{
	static int try = 0;
	char lbuf[100], sbuf[100];

	try++;
	snprintf(lbuf, 100, "This is outer %d", try);
	snprintf(sbuf, 100, "outer%d", try);
	return run_steps(lbuf, sbuf, 10, middle);
}

int main(int argc, char *argv[])
{
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
	tools_progress_enable();
	return run_steps("This is a test", "thisis", 5, outer);
}
#endif
