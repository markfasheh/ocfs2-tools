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

#include "ocfs2.h"


#define CDSL_BASE       ".cluster"
#define CDSL_COMMON_DIR "common"

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
	[CDSL_TYPE_HOSTNAME] = "hostname",
	[CDSL_TYPE_MACH]     = "mach",
	[CDSL_TYPE_OS]       = "os",
	[CDSL_TYPE_NODENUM]  = "nodenum",
	[CDSL_TYPE_SYS]      = "sys",
	[CDSL_TYPE_UID]      = "uid",
	[CDSL_TYPE_GID]      = "gid",
	[CDSL_TYPE_UNKNOWN]  = NULL
};


typedef struct _State State;

struct _State {
	char *progname;

	gboolean copy;
	gboolean local;
	gboolean no_common;

	gboolean force;
	gboolean dry_run;

	gboolean verbose;
	gboolean quiet;

	CDSLType type;

	char *dirname;
	char *quoted_dirname;

	char *filename;
	char *quoted_filename;

	char *fullname;
	char *quoted_fullname;
};


static State *get_state (int argc, char **argv);
static void usage(const char *progname);
static void version(const char *progname);
static void run_command(State *s, const char *cmd);
static char *verify_ocfs2(State *s);
static char *get_ocfs2_root(const char *path);
static CDSLType cdsl_type_from_string(const char *str);
static char *cdsl_common_path(State *s);
static char *cdsl_path_expand(State *s);
static char *cdsl_source_directory(State *s, const char *fsroot,
				   const char *path);
static char *cdsl_target(State *s, const char *path);
static gboolean cdsl_match(State *s, const char *path, const char *cdsl);
static void copy(State *s, const char *src, const char *dest);
static void delete(State *s, const char *path);
static char *get_node_num(State *s);
static void create_directory(State *s, const char *path);
static void make_common_file(State *s, const char *fsroot, const char *path);
static void copy_common_file(State *s, const char *fsroot, const char *path,
			     const char *dir);


extern char *optarg;
extern int optind, opterr, optopt;


int
main(int argc, char **argv)
{
	State *s;
	char *fsroot, *path;
	char *filename, *dir;
	gboolean exists;
	char *target, *quoted_target;

	s = get_state(argc, argv);

	fsroot = verify_ocfs2(s);

	path = s->dirname + strlen(fsroot);

	if (!(fsroot[0] == '/' && fsroot[1] == '\0'))
		path += 1;

	dir = cdsl_target(s, path);
	target = g_build_filename(dir, s->filename, NULL);
	g_free(dir);

	quoted_target = g_shell_quote(target);

	if (g_file_test(s->fullname, G_FILE_TEST_IS_SYMLINK)) {
		if (!s->copy && cdsl_match(s, s->fullname, target)) {
			dir = cdsl_source_directory(s, fsroot, path);
			filename = g_build_filename(dir, s->filename, NULL);

			if (!g_file_test(filename, G_FILE_TEST_EXISTS))
				copy_common_file(s, fsroot, path, dir);

			g_free(filename);
			g_free(dir);

			exit(1);
		}			 
		else {
			com_err(s->progname, 0,
				"%s is already a symbolic link",
				s->quoted_fullname);
			exit(1);
		}
	}

	exists = g_file_test(s->fullname, G_FILE_TEST_EXISTS);

	if (exists) {
		if (!g_file_test(s->fullname,
				 G_FILE_TEST_IS_REGULAR | G_FILE_TEST_IS_DIR)) {
			com_err(s->progname, 0,
				"%s is not a file or a directory",
				s->quoted_fullname);
			exit(1);
		}
	}
	else if (s->copy) {
		com_err(s->progname, 0, "%s does not exist, no file to copy",
			s->quoted_fullname);
		exit(1);
	}

	if (exists && !s->copy) {
		if (s->force) {
			delete(s, s->fullname);
			exists = FALSE;
		}
		else {
			com_err(s->progname, 0,
				"%s already exists, but copy not requested or "
				"force (-f) not given",
				s->quoted_fullname);
			exit(1);
		}
	}

	if (exists)
		make_common_file(s, fsroot, path);
	else {
		dir = cdsl_source_directory(s, fsroot, path);

		if (!s->no_common)
			copy_common_file(s, fsroot, path, dir);

		g_free(dir);
	}

	if (s->verbose || s->dry_run)
		printf("ln -s %s %s\n", quoted_target, s->quoted_fullname);

	if (!s->dry_run) {
		if (symlink(target, s->fullname) != 0) {
			com_err(s->progname, errno,
				"could not symlink %s to %s",
				quoted_target, s->quoted_fullname);
			exit(1);
		}
	}

	g_free(quoted_target);
	g_free(target);

	return 0;
}

static State *
get_state(int argc, char **argv)
{
	char *progname;
	gboolean copy = FALSE, local = FALSE, no_common = FALSE;
	gboolean force = FALSE, dry_run = FALSE;
	gboolean quiet = FALSE, verbose = FALSE, show_version = FALSE;
	CDSLType type = CDSL_TYPE_HOSTNAME;
	char *filename, *dirname, *tmp;
	State *s;
	int c;

	static struct option long_options[] = {
		{ "type", 1, 0, 't' },
		{ "copy", 0, 0, 'c' },
		{ "local", 0, 0, 'L' },
		{ "no-common", 0, 0, 'N' },
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
		c = getopt_long(argc, argv, "t:acLNfnvqV", long_options, NULL);

		if (c == -1)
			break;

		switch (c) {
		case 't':
			type = cdsl_type_from_string(optarg);
			if (type == CDSL_TYPE_UNKNOWN) {
				com_err(progname, 0,
					"'%s' not a recognized type\n",
					optarg);
				exit(1);
			}
			break;
		case 'c':
			copy = TRUE;
			break;
		case 'L':
			local = TRUE;
			break;
		case 'N':
			no_common = TRUE;
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
		com_err(progname, errno, g_path_get_dirname(filename));
		exit(1);
	}

	if (optind < argc)
		usage(progname);

	if (local)
		copy = TRUE;

	s = g_new0(State, 1);

	s->progname  = progname;

	s->type      = type;

	s->copy      = copy;
	s->local     = local;
	s->no_common = no_common;

	s->force     = force;
	s->dry_run   = dry_run;

	s->verbose   = verbose;
	s->quiet     = quiet;

	s->dirname   = g_strdup(dirname);
	s->filename  = g_path_get_basename(filename);

	s->fullname  = g_build_filename(s->dirname, s->filename, NULL);

	free(dirname);

	s->quoted_dirname  = g_shell_quote(s->dirname);
	s->quoted_filename = g_shell_quote(s->filename);
	s->quoted_fullname = g_shell_quote(s->fullname);

	return s;
}

static void
usage(const char *progname)
{
	const char * const *name;

	fprintf(stderr, "Usage: %s [-cLfnqvV] [-t", progname);

	for (name = cdsl_names; *name; name++)
		fprintf(stderr, " %s", *name);

	fprintf(stderr, "] filename\n");
	exit(1);
}

static void
version(const char *progname)
{
	printf("%s %s\n", progname, VERSION);
}

static void
run_command(State *s, const char *cmd)
{
	char *name, *space, *cmd_err;
	int len, ret;
	GError *error = NULL;

	space = strchr(cmd, ' ');

	if (space) {
		len = space - cmd;  	
		name = g_new(char, space - cmd);
		strncpy(name, cmd, len);
		name[len] = '\0';
	}
	else
		name = g_strdup(cmd);

	if (s->verbose || s->dry_run)
		printf("%s\n", cmd);

	if (!s->dry_run) {
		if (!g_spawn_command_line_sync(cmd, NULL, &cmd_err,
					       &ret, &error)) {
			com_err(s->progname, 0,
				"could not run %s: %s",
				name, error->message);
				exit(1);
		}

		if (ret != 0) {
			com_err(s->progname, 0, "%s error: %s", name, cmd_err);
			exit(1);
		}

		g_free(cmd_err);
	}
}

static char *
verify_ocfs2(State *s)
{
	struct statfs sbuf;
	char *fsroot;

	if (statfs(s->dirname, &sbuf) != 0) {
		com_err(s->progname, errno, "could not statfs %s",
			s->quoted_dirname);
		exit(1);
	}

	if (sbuf.f_type != OCFS2_SUPER_MAGIC) {
		com_err(s->progname, 0, "%s is not on an ocfs2 filesystem",
			s->quoted_fullname);
		exit(1);
	}

	fsroot = get_ocfs2_root(s->dirname);

	if (fsroot == NULL) {
		com_err(s->progname, 0, "%s is not on an ocfs2 filesystem",
			s->quoted_fullname);
		exit(1);
	}

	return fsroot;
}

static char *
get_ocfs2_root(const char *path)
{
	struct mntent *mnt;
	FILE *fp;
	int len, found_len = 0;
	char *found = NULL, *found_type = NULL;
	char *ret = NULL;

	fp = setmntent (_PATH_MOUNTED, "r");

	if (fp == NULL)
		return NULL;

	while ((mnt = getmntent(fp))) {
		len = strlen(mnt->mnt_dir);

		if (strncmp(mnt->mnt_dir, path, len) == 0) {
			if (len > found_len && (len == 1 ||
						path[len] == '/' ||
						path[len] == '\0')) {
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

	return ret;
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
cdsl_common_path(State *s)
{
	const char *prefix;

	g_assert(s->type < CDSL_TYPE_UNKNOWN);

	prefix = cdsl_names[s->type];

	return g_build_filename(CDSL_BASE, CDSL_COMMON_DIR, prefix, NULL);
}

static char *
cdsl_path_expand(State *s)
{
	const char *prefix;
	char *val, *ret;
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
cdsl_source_directory(State *s, const char *fsroot, const char *path)
{
	char *prefix, *dir;

	prefix = cdsl_path_expand(s);
	dir = g_build_filename(fsroot, prefix, path, NULL);
	g_free(prefix);

	create_directory(s, dir);

	return dir;
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

static gboolean
cdsl_match(State *s, const char *path, const char *cdsl)
{
	char buf[PATH_MAX];
	int n;

	n = readlink(path, buf, sizeof(buf) - 1);

	if (n < 0) {
		com_err(s->progname, errno, "readlink");
		exit(1);
	}

	buf[n] = '\0';

	return strcmp(buf, cdsl) == 0;
}

static void
copy(State *s, const char *src, const char *dest)
{
	char *cmd, *quoted_src, *quoted_dest;

	quoted_src = g_shell_quote(src);
	quoted_dest = g_shell_quote(dest);

	cmd = g_strdup_printf("cp -a %s %s", quoted_src, quoted_dest);

	g_free(quoted_src);
	g_free(quoted_dest);

	run_command(s, cmd);
	g_free(cmd);
}

static void
delete(State *s, const char *path)
{
	char *cmd, *quoted;

	quoted = g_shell_quote(path);
	cmd = g_strdup_printf("rm -rf %s", quoted);
	g_free(quoted);

	run_command(s, cmd);
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
		com_err(s->progname, errno, "could not stat %s",
			s->quoted_dirname);
		exit(1);
	}

	dev = g_strdup_printf("%u_%u", major(sbuf.st_dev), minor(sbuf.st_dev));
	path = g_build_filename(PROC_OCFS2, dev, "nodenum", NULL);
	g_free(dev);

	f = fopen(path, "r");

	if (f == NULL) {
		com_err(s->progname, errno, "could not open %s", path);
		exit(1);
	}

	if (fgets(buf, sizeof(buf), f) == NULL) {
		com_err(s->progname, errno, "could not read node number");
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
		com_err(s->progname, 0, "could not read node number");
		exit(1);
	}

	return g_strdup(buf);
}

static void
create_directory(State *s, const char *path)
{
	char *cmd, *quoted;

	quoted = g_shell_quote(path);
	cmd = g_strdup_printf("mkdir -p %s", quoted);
	g_free(quoted);

	run_command(s, cmd);
	g_free(cmd);
}

static void
make_common_file(State *s, const char *fsroot, const char *path)
{
	char *prefix, *cdsl_path, *cdsl_full, *quoted, *dir;

	if (s->local)
		prefix = cdsl_path_expand(s);
	else
		prefix = cdsl_common_path(s);

	cdsl_path = g_build_filename(fsroot, prefix, path, NULL);
	g_free(prefix);

	create_directory(s, cdsl_path);

	cdsl_full = g_build_filename(cdsl_path, s->filename, NULL);
	g_free (cdsl_path);

	if (g_file_test(cdsl_full, G_FILE_TEST_EXISTS)) {
		if (s->force)
			delete(s, cdsl_full);
		else {
			com_err(s->progname, 0,
				"CDSL already exists. To replace, use the "
				"force (-f) option");
				exit(1);
		}
	}

	if (s->verbose || s->dry_run) {
		quoted = g_shell_quote(cdsl_full);
		printf("mv %s %s\n", s->quoted_fullname, quoted);
		g_free(quoted);
	}

	if (!s->dry_run) {
		if (rename(s->fullname, cdsl_full) != 0) {
			com_err(s->progname, errno, "could not rename %s",
				s->quoted_fullname);
			exit(1);
		}
	}

	if (!s->local) {
		dir = cdsl_source_directory(s, fsroot, path);
		copy(s, cdsl_full, dir);
		g_free(dir);
	}
	
	g_free (cdsl_full);
}

static void
copy_common_file(State *s, const char *fsroot, const char *path,
		 const char *dir)
{
	char *prefix, *filename;

	prefix = cdsl_common_path(s);
	filename = g_build_filename(fsroot, prefix, path, s->filename, NULL);
	g_free(prefix);

	if (g_file_test(filename, G_FILE_TEST_EXISTS))
		copy(s, filename, dir);

	g_free(filename);
}
