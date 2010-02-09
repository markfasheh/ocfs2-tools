/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * capabilities.c
 *
 * Read dlmfs capabilities.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include "o2dlm/o2dlm.h"

#define CAPABILITIES_FILE	"/sys/module/ocfs2_dlmfs/parameters/capabilities"

static ssize_t read_single_line_file(const char *file, char *line,
				     size_t count)
{
	ssize_t ret = 0;
	FILE *f;

	f = fopen(file, "r");
	if (f) {
		if (fgets(line, count, f))
			ret = strlen(line);
		fclose(f);
	} else
		ret = -errno;

	return ret;
}

static ssize_t o2dlm_read_capabilities(char *line, size_t count)
{
	ssize_t got;

	got = read_single_line_file(CAPABILITIES_FILE, line, count);
	if ((got > 0) && (line[got - 1] == '\n')) {
		got--;
		line[got] = '\0';
	}
	return got;
}

static errcode_t o2dlm_has_capability(const char *cap_name, int *found)
{
	char line[PATH_MAX];
	char *p;
	ssize_t got;

	got = o2dlm_read_capabilities(line, PATH_MAX);
	if (got == -ENOENT) {
		got = 0;
		line[0] = '\0';
	}
	if (got < 0)
		return O2DLM_ET_SERVICE_UNAVAILABLE;

	*found = 0;
	p = strstr(line, cap_name);
	if (p) {
		p += strlen(cap_name);
		if (!*p || (*p == ' '))
			*found = 1;
	}
	return 0;
}

errcode_t o2dlm_supports_bast(int *supports)
{
	return o2dlm_has_capability("bast", supports);
}

errcode_t o2dlm_supports_stackglue(int *supports)
{
	return o2dlm_has_capability("stackglue", supports);
}

#ifdef DEBUG_EXE
int main(int argc, char *argv[])
{
	errcode_t ret;
	int i, rc = 0;
	char **caps;

	initialize_o2dl_error_table();

	ret = o2dlm_supports_bast(&i);
	if (!ret)
		fprintf(stdout, "bast: %s\n", i ? "yes" : "no");
	else {
		rc = 1;
		com_err("debug_capabilities", ret,
			"while testing bast capability");
	}

	ret = o2dlm_supports_stackglue(&i);
	if (!ret)
		fprintf(stdout, "stackglue: %s\n", i ? "yes" : "no");
	else {
		rc = 1;
		com_err("debug_capabilities", ret,
			"while testing stackglue capability");
	}

	ret = o2dlm_has_capability("invalid", &i);
	if (!ret)
		fprintf(stdout, "invalid: %s\n", i ? "yes" : "no");
	else {
		rc = 1;
		com_err("debug_capabilities", ret,
			"while testing invalid capability");
	}

	return ret;
}
#endif
