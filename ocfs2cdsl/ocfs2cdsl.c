/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * ocfs2cdsl.c
 *
 * OCFS2 CDSL utility
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
 * Authors: Manish Singh
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mntent.h>
#include <libgen.h>
#include <getopt.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/statfs.h>
#include <sys/utsname.h>

#include <glib.h>


#define OCFS_MAGIC 0xa156f7eb

#define CDSL_BASE ".cluster"


typedef enum {
	CDSL_TYPE_HOSTNAME,
	CDSL_TYPE_MACH,
	CDSL_TYPE_OS,
	CDSL_TYPE_NODENUM
} CDSLType;


typedef struct _State State;

struct _State {
	char *progname;

	gboolean copy;
	gboolean force;
	gboolean dry_run;

	gboolean verbose;
	gboolean quiet;

	CDSLType type;

	char *filename;
	char *dirname;
	char *fullname;
};


static State *get_state (int argc, char **argv);
static void usage(const char *progname);
static void version(const char *progname);
static char *get_ocfs2_root(const char *path);
static char *cdsl_path_expand(State *s);
static char *cdsl_target(State *s, const char *path);
static void delete(State *s, const char *path);


extern char *optarg;
extern int optind, opterr, optopt;


int
main(int argc, char **argv)
{
	State *s;
	char *fsroot, *path;
	char *cmd, *cmd_err;
	int ret;
	struct statfs sbuf;
	gboolean exists;
	char *cdsl_path, *cdsl_full;
	char *target;
	GError *error = NULL;

	s = get_state(argc, argv);

	if (statfs(s->dirname, &sbuf) != 0) {
		fprintf(stderr, "%s: %s: %s\n", s->progname, s->filename,
			g_strerror(errno));
		exit(1);
	}

	if (sbuf.f_type != OCFS_MAGIC) {
		fprintf(stderr, "%s: %s is not on an ocfs2 filesystem\n",
			s->progname, s->filename);
		exit(1);
	}

	fsroot = get_ocfs2_root(s->dirname);

	if (fsroot == NULL) {
		fprintf(stderr, "%s: %s is not on an ocfs2 filesystem\n",
			s->progname, s->dirname);
		exit(1);
	}

	exists = g_file_test(s->fullname, G_FILE_TEST_EXISTS);
	if (exists) {
		if (!g_file_test(s->fullname,
				 G_FILE_TEST_IS_REGULAR | G_FILE_TEST_IS_DIR)) {
			fprintf(stderr, "%s: %s is not a file or directory\n",
				s->progname, s->fullname);
			exit(1);
		}
	}
	else if (s->copy) {
		fprintf(stderr, "%s: %s does not exist, but copy requested\n",
			s->progname, s->fullname);
		exit(1);
	}

	if (exists && !s->copy && !s->force) {
		fprintf(stderr, "%s: %s already exists, but copy (-c) or "
				"force (-f) not given\n",
			s->progname, s->fullname);
		exit(1);
	}

	path = s->dirname + strlen(fsroot) + 1;

	if (exists) {
		cdsl_path = g_build_filename(fsroot, cdsl_path_expand(s),
					     path, NULL);

		cmd = g_strdup_printf("mkdir -p %s", g_shell_quote(cdsl_path));

		if (!g_spawn_command_line_sync(cmd, NULL, &cmd_err, &ret,
					       &error)) {
			fprintf(stderr, "%s: Couldn't mkdir: %s\n", s->progname,
				error->message);
			exit(1);
		}

		if (ret != 0) {
			fprintf(stderr, "%s: mkdir error: %s\n", s->progname,
				cmd_err);
			exit(1);
		}

		g_free(cmd);

		cdsl_full = g_build_filename(cdsl_path, s->filename, NULL);

		if (g_file_test(cdsl_full, G_FILE_TEST_EXISTS)) {
			if (s->force)
				delete(s, cdsl_full);
			else {
				fprintf(stderr, "%s: CDSL already exists "
						"To replace, use the force "
						"(-f) option\n",
					s->progname);
				exit(1);
			}
		}

		if (rename(s->fullname, cdsl_full) != 0) {
			fprintf(stderr, "%s: could not rename %s: %s\n",
				s->progname, s->filename, g_strerror(errno));
			exit(1);
		}

		g_free (cdsl_full);
		g_free (cdsl_path);
	}

	target = g_build_filename(cdsl_target(s, path), s->filename, NULL);

	if (symlink(target, s->fullname) != 0) {
		fprintf(stderr, "%s: could not symlink %s to %s: %s\n",
			s->progname, target, s->fullname, g_strerror(errno));
		exit(1);
	}

	g_free(target);

	return 0;
}

static State *
get_state(int argc, char **argv)
{
	char *progname;
	gboolean copy = FALSE, force = FALSE, dry_run = FALSE;
	gboolean quiet = FALSE, verbose = FALSE, show_version = FALSE;
	CDSLType type = CDSL_TYPE_HOSTNAME;
	char *filename, *dirname, *tmp;
	State *s;
	int c;

	static struct option long_options[] = {
		{ "type", 1, 0, 't' },
		{ "copy", 0, 0, 'c' },
		{ "force", 0, 0, 'f' },
		{ "dry-run", 0, 0, 'n' },
		{ "verbose", 0, 0, 'v' },
		{ "quiet", 0, 0, 'q' },
		{ "version", 0, 0, 'V' },
		{ 0, 0, 0, 0 }
	};

	if (argc && *argv)
		progname = g_path_get_basename(argv[0]);
	else
		progname = g_strdup("ocfs2cdsl");

	while (1) {
		c = getopt_long(argc, argv, "t:acfnvqV", long_options, NULL);

		if (c == -1)
			break;

		switch (c) {
		case 't':
			if (strcmp(optarg, "hostname") == 0)
				type = CDSL_TYPE_HOSTNAME;
			else if (strcmp(optarg, "mach") == 0)
				type = CDSL_TYPE_MACH;
			else if (strcmp(optarg, "os") == 0)
				type = CDSL_TYPE_OS;
			else if (strcmp(optarg, "nodenum") == 0)
				type = CDSL_TYPE_NODENUM;
			else {
				fprintf(stderr, "%s: '%s' not a recognized "
						"type",
					progname, optarg);
				exit(1);
			}
			break;
		case 'c':
			copy = TRUE;
			break;
		case 'f':
			force = TRUE;
			break;
		case 'n':
			dry_run = TRUE;
			break;
		case 'q':
			quiet = TRUE;
			break;
		case 'v':
			verbose = TRUE;
			break;
		case 'V':
			show_version = TRUE;
			break;
		default:
			usage(progname);
			break;
		}
	}

	if ((optind == argc) && !show_version)
		usage(progname);

	filename = argv[optind++];

	tmp = g_path_get_dirname(filename);
	dirname = canonicalize_file_name(tmp);
	g_free(tmp);

	if (dirname == NULL) {
		fprintf(stderr, "%s: %s: %s\n", progname,
			g_path_get_dirname(filename),
			g_strerror(errno));
		exit(1);
	}

	if (optind < argc)
		usage(progname);

	if (show_version) {
		version(progname);
		exit(0);
	}

	s = g_new0(State, 1);

	s->progname = progname;

	s->type     = type;

	s->copy     = copy;
	s->force    = force;
	s->dry_run  = dry_run;

	s->verbose  = verbose;
	s->quiet    = quiet;

	s->dirname  = g_strdup(dirname);
	s->filename = g_path_get_basename(filename);

	s->fullname = g_build_filename(s->dirname, s->filename, NULL);

	free(dirname);

	return s;
}

static void
usage(const char *progname)
{
	fprintf(stderr, "Usage: %s [-cfnqvV] [-t hostname|mach|os|nodenum] "
		"[filename]\n", progname);
	exit(0);
}

static void
version(const char *progname)
{
	fprintf(stderr, "%s %s %s (build %s)\n", progname,
		OCFS2_BUILD_VERSION, OCFS2_BUILD_DATE, OCFS2_BUILD_MD5);
}

static char *
get_ocfs2_root(const char *path)
{
	struct mntent *mnt;
	FILE *fp;
	int len, found_len;
	char *found = NULL, *found_type = NULL;
	char *ret = NULL;

	fp = setmntent (_PATH_MOUNTED, "r");

	if (fp == NULL)
		return NULL;

	while ((mnt = getmntent(fp))) {
		len = strlen(mnt->mnt_dir);

		if (strncmp(mnt->mnt_dir, path, len) == 0) {
			if (path[len] == '/') {
				found_len = len;

				g_free(found);
				found = g_strdup(mnt->mnt_dir);

				g_free(found_type);
				found_type = g_strdup(mnt->mnt_type);
			}
		}
	}

	endmntent(fp);

	if (found_type && strcmp(found_type, "ocfs2") == 0)
		ret = g_strdup(found);

	g_free(found_type);
	g_free(found);

	ret = g_strdup("/tmp/ocfs2");
	return ret;
}

static char *
cdsl_path_expand(State *s)
{
	char *prefix, *val;
	struct utsname buf;

	uname(&buf);

	switch(s->type) {
	case CDSL_TYPE_HOSTNAME:
		prefix = "hostname";
		val = buf.nodename;
		break;
	case CDSL_TYPE_MACH:
		prefix = "mach";
		val = buf.machine;
		break;
	case CDSL_TYPE_OS:
		prefix = "os";
		val = buf.sysname;
		break;
	case CDSL_TYPE_NODENUM:
		prefix = "nodenum";
		val = "0";
		break;
	default:
		g_assert_not_reached();
		break;
	}

	return g_build_filename(CDSL_BASE, prefix, val, NULL);
}

static char *
cdsl_target(State *s, const char *path)
{
	char *type, *val, *ret;
	GString *prefix;
	char **parts;
	int i;

	switch(s->type) {
	case CDSL_TYPE_HOSTNAME:
		type = "hostname";
		break;
	case CDSL_TYPE_MACH:
		type = "mach";
		break;
	case CDSL_TYPE_OS:
		type = "os";
		break;
	case CDSL_TYPE_NODENUM:
		type = "nodenum";
		break;
	default:
		g_assert_not_reached();
		break;
	}

	val = g_strdup_printf("{%s}", type);

	prefix = g_string_new("");

	parts = g_strsplit(path, "/", -1);

	for (i = 0; parts[i] != NULL; i++)
		g_string_append(prefix, "../");

	g_strfreev(parts);

	ret = g_build_filename(prefix->str, CDSL_BASE, type, val, path, NULL);

	g_string_free(prefix, TRUE);
	g_free(val);

	return ret;
}

static void
delete(State *s, const char *path)
{
	char *cmd, *cmd_err, *quoted;
	int ret;
	GError *error = NULL;

	quoted = g_shell_quote(path);
	cmd = g_strdup_printf("rm -rf %s", quoted);
	g_free(quoted);

	if (!g_spawn_command_line_sync(cmd, NULL, &cmd_err, &ret, &error)) {
		fprintf(stderr, "%s: Couldn't rm: %s\n", s->progname,
			error->message);
		exit(1);
	}

	if (ret != 0) {
		fprintf(stderr, "%s: rm error: %s\n", s->progname, cmd_err);
		exit(1);
	}

	g_free(cmd);
}
