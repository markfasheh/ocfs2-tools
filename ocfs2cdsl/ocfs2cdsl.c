/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * ocfs2cdsl.c
 *
 * OCFS2 CDSL utility
 *
 * Copyright (C) 2004, 2005 Oracle.  All rights reserved.
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
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/utsname.h>
#include <sys/sysmacros.h>

#include <glib.h>


#define OCFS_MAGIC 0xa156f7eb

#define CDSL_BASE  ".cluster"

#define PROC_OCFS2 "/proc/fs/ocfs2"


typedef enum {
	CDSL_TYPE_HOSTNAME,
	CDSL_TYPE_MACH,
	CDSL_TYPE_OS,
	CDSL_TYPE_NODENUM,
	CDSL_TYPE_SYS,
	CDSL_TYPE_UID,
	CDSL_TYPE_GID,
	CDSL_TYPE_UNKNOWN
} CDSLType;

static const char * const cdsl_names[] = {
	[CDSL_TYPE_HOSTNAME]	"hostname",
	[CDSL_TYPE_MACH]	"mach",
	[CDSL_TYPE_OS]		"os",
	[CDSL_TYPE_NODENUM]	"mach",
	[CDSL_TYPE_SYS]		"sys",
	[CDSL_TYPE_UID]		"uid",
	[CDSL_TYPE_GID]		"gid",
	[CDSL_TYPE_UNKNOWN]	NULL
};


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
static CDSLType cdsl_type_from_string(const char *str);
static char *cdsl_path_expand(State *s);
static char *cdsl_target(State *s, const char *path);
static void delete(State *s, const char *path);
static char *get_node_num(State *s);


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
		fprintf(stderr, "%s: couldn't statfs %s: %s\n",
			s->progname, s->dirname, g_strerror(errno));
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

	if (exists && !s->copy) {
		if (s->force) {
			delete(s, s->fullname);
			exists = FALSE;
		}
		else {
			fprintf(stderr, "%s: %s already exists, but copy (-c) "
					"or force (-f) not given\n",
				s->progname, s->fullname);
			exit(1);
		}
	}

	path = s->dirname + strlen(fsroot) + 1;

	if (exists) {
		cdsl_path = g_build_filename(fsroot, cdsl_path_expand(s),
					     path, NULL);

		cmd = g_strdup_printf("mkdir -p %s", g_shell_quote(cdsl_path));

		if (s->verbose || s->dry_run)
			printf("%s\n", cmd);

		if (!s->dry_run) {
			if (!g_spawn_command_line_sync(cmd, NULL, &cmd_err,
						       &ret, &error)) {
				fprintf(stderr, "%s: Couldn't mkdir: %s\n",
					s->progname, error->message);
				exit(1);
			}

			if (ret != 0) {
				fprintf(stderr, "%s: mkdir error: %s\n",
					s->progname, cmd_err);
				exit(1);
			}
		}

		g_free(cmd);

		cdsl_full = g_build_filename(cdsl_path, s->filename, NULL);

		if (g_file_test(cdsl_full, G_FILE_TEST_EXISTS)) {
			if (s->force)
				delete(s, cdsl_full);
			else {
				fprintf(stderr, "%s: CDSL already exists. "
						"To replace, use the force "
						"(-f) option\n",
					s->progname);
				exit(1);
			}
		}

		if (s->verbose || s->dry_run)
			printf("mv %s %s\n", s->fullname, cdsl_full);

		if (!s->dry_run) {
			if (rename(s->fullname, cdsl_full) != 0) {
				fprintf(stderr, "%s: could not rename %s: %s\n",
					s->progname, s->filename,
					g_strerror(errno));
				exit(1);
			}
		}

		g_free (cdsl_full);
		g_free (cdsl_path);
	}

	target = g_build_filename(cdsl_target(s, path), s->filename, NULL);

	if (s->verbose || s->dry_run)
		printf("ln -s %s %s\n", target, s->fullname);

	if (!s->dry_run) {
		if (symlink(target, s->fullname) != 0) {
			fprintf(stderr, "%s: could not symlink %s to %s: %s\n",
				s->progname, target, s->fullname,
				g_strerror(errno));
			exit(1);
		}
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
			type = cdsl_type_from_string(optarg);
			if (type == CDSL_TYPE_UNKNOWN) {
				fprintf(stderr, "%s: '%s' not a recognized "
						"type\n",
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

	if (show_version) {
		version(progname);
		exit(0);
	}

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
	const char * const *name;

	fprintf(stderr, "Usage: %s [-cfnqvV] [-t", progname);

	for (name = cdsl_names; *name; name++)
		fprintf(stderr, " %s", *name);

	fprintf(stderr, "] [filename]\n");
	exit(1);
}

static void
version(const char *progname)
{
	fprintf(stderr, "%s %s\n", progname, VERSION);
}

static CDSLType
cdsl_type_from_string(const char *str)
{
	const char * const *name;
	CDSLType type;

	for (name = cdsl_names, type = CDSL_TYPE_HOSTNAME; *name;
	     name++, type++)
		if (strcmp(str, *name) == 0)
			break;

	return type;
}

static char *
get_ocfs2_root(const char *path)
{
	struct mntent *mnt;
	FILE *fp;
	int len;
	char *found = NULL, *found_type = NULL;
	char *ret = NULL;

	fp = setmntent (_PATH_MOUNTED, "r");

	if (fp == NULL)
		return NULL;

	while ((mnt = getmntent(fp))) {
		len = strlen(mnt->mnt_dir);

		if (strncmp(mnt->mnt_dir, path, len) == 0) {
			if (path[len] == '/' || path[len] == '\0') {
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

	return ret;
}

static char *
cdsl_path_expand(State *s)
{
	const char *prefix;
	char *val, *ret;;
	struct utsname buf;

	uname(&buf);

	switch(s->type) {
	case CDSL_TYPE_HOSTNAME:
		val = g_strdup(buf.nodename);
		break;
	case CDSL_TYPE_MACH:
		val = g_strdup(buf.machine);
		break;
	case CDSL_TYPE_OS:
		val = g_strdup(buf.sysname);
		break;
	case CDSL_TYPE_NODENUM:
		val = get_node_num(s);
		break;
	case CDSL_TYPE_SYS:
		val = g_strdup_printf("%s_%s", buf.machine, buf.sysname);
		break;
	case CDSL_TYPE_UID:
		val = g_strdup_printf("%lu", (unsigned long)getuid());
		break;
	case CDSL_TYPE_GID:
		val = g_strdup_printf("%lu", (unsigned long)getgid());
		break;
	case CDSL_TYPE_UNKNOWN:
	default:
		g_assert_not_reached();
		val = NULL;
		break;
	}

	prefix = cdsl_names[s->type];

	ret = g_build_filename(CDSL_BASE, prefix, val, NULL);

	g_free(val);

	return ret;
}

static char *
cdsl_target(State *s, const char *path)
{
	const char *type;
	char *val, *ret;
	GString *prefix;
	char **parts;
	int i;

	type = cdsl_names[s->type];

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

	if (s->verbose || s->dry_run)
		printf("%s\n", cmd);

	if (!s->dry_run) {
		if (!g_spawn_command_line_sync(cmd, NULL, &cmd_err, &ret,
					       &error)) {
			fprintf(stderr, "%s: Couldn't rm: %s\n", s->progname,
				error->message);
			exit(1);
		}

		if (ret != 0) {
			fprintf(stderr, "%s: rm error: %s\n", s->progname,
				cmd_err);
			exit(1);
		}
	}

	g_free(cmd);
}

static char *
get_node_num(State *s)
{
	struct stat sbuf;
	char *dev, *path, buf[20];
	FILE *f;
	int i;

	if (stat(s->dirname, &sbuf) != 0) {
		fprintf(stderr, "%s: couldn't stat %s: %s\n",
			s->progname, s->dirname, g_strerror(errno));
		exit(1);
	}

	dev = g_strdup_printf("%u_%u", major(sbuf.st_dev), minor(sbuf.st_dev));
	path = g_build_filename(PROC_OCFS2, dev, "nodenum", NULL);
	g_free(dev);

	f = fopen(path, "r");

	if (f == NULL) {
		fprintf(stderr, "%s: could not open %s: %s\n",
			s->progname, path, g_strerror(errno));
		exit(1);
	}

	if (fgets(buf, sizeof(buf), f) == NULL) {
		fprintf(stderr, "%s: could not read node number: %s\n",
			s->progname, g_strerror(errno));
		exit(1);
	}

	fclose(f);

	g_free(path);

	for (i = 0; i < sizeof(buf); i++) {
		if (buf[i] < '0' || buf[i] > '9') {
		    	buf[i] = '\0';
			break;
		}
	}

	if (buf[0] == '\0') {
		fprintf(stderr, "%s: invalid node number: %s\n",
			s->progname, g_strerror(errno));
		exit(1);
	}

	return g_strdup(buf);
}
