/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * utils.c
 *
 * Utility functions
 *
 * Copyright (C) 2010 Oracle.  All rights reserved.
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
#include <ctype.h>

#include "tools-internal/utils.h"
#include "libtools-internal.h"

/*
 * Public API
 */

char *tools_strchomp(char *str)
{
	int len = strlen(str);
	char *p;

	if (!len)
		return str;

	p = str + len - 1;;
	while (isspace(*p) && len--)
		*p-- = '\0';

	return str;
}

char *tools_strchug(char *str)
{
	int len = strlen(str);
	char *p = str;

	if (!len)
		return str;

	while (isspace(*p) && len--)
		p++;

	if (len)
		memmove(str, p, len);

	str[len] = '\0';

	return str;
}

#ifdef DEBUG_EXE

typedef char * (*test_func)(char *str);
static void do_test(char **s, test_func f)
{
	int i;
	char tmp[100];

	for (i = 0; s[i]; ++i) {
		strncpy(tmp, s[i], sizeof(tmp));
		printf("before:>%s< ", tmp);
		*(f)(tmp);
		printf("after:>%s<\n", tmp);
	}
}

int main(int argc, char *argv[])
{
	char *m[] = { "xxx", "xxx  \t", "xxx\n", "xx  x\n ", NULL };
	char *u[] = { "xxx", "  \txxx", "\nxxx", " \nx xx", NULL };

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	printf("Testing tools_strchomp():\n");
	do_test(m, &tools_strchomp);

	printf("\nTesting tools_strchug():\n");
	do_test(u, &tools_strchug);

	return 0;
}
#endif
