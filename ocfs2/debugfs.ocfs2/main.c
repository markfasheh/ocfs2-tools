
#include <main.h>
#include <commands.h>
#include <dump.h>
#include <readfs.h>

#if 0
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <glib.h>

#include <readline/readline.h>
#include <readline/history.h>

#include "inc/commands.h"
#endif

#define PROMPT "debugfs: "

static void  usage         (char *progname);
static void  print_version (void);
static char *get_line      (void);

gboolean allow_write = FALSE;

/*
 * usage()
 *
 */
static void usage (char *progname)
{
	g_print ("Usage: %s [OPTION]... [DEVICE]\n", progname);
	g_print ("Options:\n");
	g_print ("  -V, --version  g_print version information and exit\n");
	g_print ("      --help     display this help and exit\n");
	g_print ("  -w, --write    turn on write support\n");
	exit (0);
}					/* usage */

/*
 * print_version()
 *
 */
static void print_version (void)
{
	g_print ("debugocfs version " DEBUGOCFS_VERSION "\n");
}					/* print_version */

/*
 * get_line()
 *
 */
static char * get_line (void)
{
	char *line;

	line = readline (PROMPT);

	if (line && *line)
		add_history (line);

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

	for (i = 1; i < argc; i++) {
		arg = argv[i];
		if ((strcmp (arg, "--write") == 0) ||
		    (strcmp (arg, "-w") == 0)) {
			allow_write = TRUE;
		} else if ((strcmp (arg, "--version") == 0) ||
			   (strcmp (arg, "-V") == 0)) {
			print_version ();
			exit (0);
		} else if (strcmp (arg, "--help") == 0) {
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

	print_version ();

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
			exit (0);
		}
	}

	return 0;
}					/* main */
