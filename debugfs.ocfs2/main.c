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

static void  usage         (char *progname);
static void  print_version (char *progname);
static char *get_line      (void);

gboolean allow_write = FALSE;
extern dbgfs_gbls gbls;

/*
 * usage()
 *
 */
static void usage (char *progname)
{
	g_print ("Usage: %s [OPTION]... [DEVICE]\n", progname);
	g_print ("Options:\n");
	g_print ("  -V, --version  g_print version information and exit\n");
	g_print ("  -?, --help     display this help and exit\n");
	g_print ("  -w, --write    turn on write support\n");
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
 * get_line()
 *
 */
static char * get_line (void)
{
	char *line;

	line = readline (PROMPT);

	if (line && *line) {
		g_strchug(line);
		add_history (line);
	}

	return line;
}					/* get_line */

/*
 * main()
 *
 */
int main (int argc, char **argv)
{
	int i;
	char *line;
	char *device = NULL;
	char *arg;
	gboolean seen_device = FALSE;

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

	memset(&gbls, 0, sizeof(gbls));

	gbls.progname = basename(argv[0]);

	for (i = 1; i < argc; i++) {
		arg = argv[i];
		if ((strcmp (arg, "--write") == 0) ||
		    (strcmp (arg, "-w") == 0)) {
			allow_write = TRUE;
		} else if ((strcmp (arg, "--version") == 0) ||
			   (strcmp (arg, "-V") == 0)) {
			print_version (gbls.progname);
			exit (0);
		} else if ((strcmp (arg, "--help") == 0) ||
			   (strcmp (arg, "-?") == 0)) {
			usage (argv[0]);
			exit (0);
		} else if (!seen_device) {
			device = g_strdup (arg);
			seen_device = TRUE;
		} else {
			usage (argv[0]);
			exit (1);
		}
	}

	print_version (gbls.progname);

	if (device) {
		line = g_strdup_printf ("open %s", device);
		do_command (line);
		g_free (line);
	}

	while (1) {
		line = get_line ();

		if (line) {
			if (!isatty (0))
				printf ("%s\n", line);

			do_command (line);
			free (line);
		} else {
			printf ("\n");
			raise (SIGTERM);
			exit (0);
		}
	}

bail:
	return 0;
}					/* main */
