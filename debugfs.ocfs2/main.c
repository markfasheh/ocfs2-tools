/*
 * main.c
 *
 * entry point for debugfs.ocfs2
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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
 * Authors: Sunil Mushran, Manish Singh
 */

#include <main.h>

#define PROMPT "debugfs: "

extern dbgfs_gbls gbls;

/*
 * usage()
 *
 */
static void usage (char *progname)
{
	g_print ("Usage: %s [OPTION]... [DEVICE]\n", progname);
	g_print ("Options:\n");
	g_print ("  -f, --file <cmd_file>	Execute commands in cmd_file\n");
	g_print ("  -V, --version		Display version\n");
	g_print ("  -?, --help			Display this help\n");
	g_print ("  -w, --write			Enable writes\n");
	exit (0);
}					/* usage */

/*
 * print_version()
 *
 */
static void print_version (char *progname)
{
	fprintf(stderr, "%s %s\n", progname, VERSION);
}					/* print_version */

/*
 * get_options()
 *
 */
static void get_options(int argc, char **argv, dbgfs_opts *opts)
{
	int c;
	static struct option long_options[] = {
		{ "file", 1, 0, 'f' },
		{ "version", 0, 0, 'V' },
		{ "help", 0, 0, '?' },
		{ "write", 0, 0, '?' },
		{ 0, 0, 0, 0}
	};

	while (1) {
		c = getopt_long(argc, argv, "f:V?w", long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'f':
			opts->cmd_file = strdup(optarg);
			if (!strlen(opts->cmd_file)) {
				usage(gbls.progname);
				exit(1);
			}
			break;

		case 'w':
			opts->allow_write = 1;
			break;

		case 'h':
			usage(gbls.progname);
			exit(0);
			break;

		case 'V':
			print_version(gbls.progname);
			exit(0);
			break;

		default:
			usage(gbls.progname);
			break;
		}
	}

	if (optind < argc)
		opts->device = strdup(argv[optind]);

	return ;
}

/*
 * get_line()
 *
 */
static char * get_line (FILE *stream)
{
	char *line;
	static char buf[1024];
	int i;

	if (stream) {
		while (1) {
			if (!fgets(buf, sizeof(buf), stream))
				return NULL;
			line = buf;
			i = strlen(line);
			if (i)
				buf[i - 1] = '\0';
			g_strchug(line);
			if (strlen(line))
				break;
		}
	} else {
		line = readline (PROMPT);

		if (line && *line) {
			g_strchug(line);
			add_history (line);
		}
	}

	return line;
}					/* get_line */

/*
 * main()
 *
 */
int main (int argc, char **argv)
{
	char *line;
	dbgfs_opts opts;
	FILE *cmd = NULL;

	initialize_ocfs_error_table();

#define INSTALL_SIGNAL(sig)					\
	do {							\
		if (signal(sig, handle_signal) == SIG_ERR) {	\
		    printf("Could not set " #sig "\n");		\
		    goto bail;					\
		}						\
	} while (0)

	INSTALL_SIGNAL(SIGTERM);
	INSTALL_SIGNAL(SIGINT);

	memset(&opts, 0, sizeof(opts));
	memset(&gbls, 0, sizeof(gbls));

	gbls.progname = basename(argv[0]);

	get_options(argc, argv, &opts);
	gbls.allow_write = opts.allow_write;
	if (!opts.cmd_file)
		gbls.interactive++;

	print_version (gbls.progname);

	if (opts.device) {
		line = g_strdup_printf ("open %s", opts.device);
		do_command (line);
		g_free (line);
	}

	if (opts.cmd_file) {
		cmd = fopen(opts.cmd_file, "r");
		if (!cmd) {
			com_err(argv[0], errno, "'%s'", opts.cmd_file);
			goto bail;
		}
	}

	while (1) {
		line = get_line(cmd);

		if (line) {
			if (!gbls.interactive)
				fprintf (stdout, "%s%s\n", PROMPT, line);
			do_command (line);
			if (gbls.interactive)
				free (line);
		} else {
			printf ("\n");
			raise (SIGTERM);
			exit (0);
		}
	}

bail:
	if (cmd)
		fclose(cmd);
	if (opts.cmd_file)
		free(opts.cmd_file);
	if (opts.device)
		free(opts.device);
	return 0;
}					/* main */
