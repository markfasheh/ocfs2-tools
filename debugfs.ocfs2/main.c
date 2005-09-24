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
#include <sys/types.h>
#include <dirent.h>

#define PROMPT "debugfs: "

extern dbgfs_gbls gbls;

static int logmode = 0;
struct log_entry {
	char *mask;
	char *action;
};
static GList *loglist = NULL;

/*
 * usage()
 *
 */
static void usage (char *progname)
{
	g_print ("usage: %s -l [<logentry> ... [allow|off|deny]] ...\n", progname);
	g_print ("usage: %s [-f cmdfile] [-V] [-w] [-n] [-?] [device]\n", progname);
	g_print ("\t-f, --file <cmdfile>\tExecute commands in cmdfile\n");
	g_print ("\t-w, --write\t\tOpen in read-write mode instead of the default of read-only\n");
	g_print ("\t-V, --version\t\tShow version\n");
	g_print ("\t-n, --noprompt\t\tHide prompt\n");
	g_print ("\t-?, --help\t\tShow this help\n");
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

static void process_one_list(GList *list, char *action)
{
	GList *tmp;
	struct log_entry *entry;


	while (list) {
		tmp = loglist;
		while (tmp) {
			entry = tmp->data;
			if (!strcmp(entry->mask, list->data))
				break;
			tmp = tmp->next;
		}

		if (tmp) {
			entry->action = action;
		} else {
			entry = g_new(struct log_entry, 1);
			entry->action = action;
			entry->mask = list->data;
			loglist = g_list_append(loglist, entry);
		}

		list = list->next;
	}
}

static void fill_log_list(int argc, char **argv, int startind)
{
	int i;
	GList *tmplist = NULL;

	for (i = startind; i < argc; i++) {
		if (!strcmp(argv[i], "allow") ||
		    !strcmp(argv[i], "deny") ||
		    !strcmp(argv[i], "off")) {
			process_one_list(tmplist, argv[i]);
			g_list_free(tmplist);
			tmplist = NULL;
		} else {
			tmplist = g_list_append(tmplist, argv[i]);
		}
	}
}

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
		{ "log", 0, 0, 'l' },
		{ "noprompt", 0, 0, 'n' },
		{ 0, 0, 0, 0}
	};

	while (1) {
		c = getopt_long(argc, argv, "lf:V?wn", long_options, NULL);
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

		case 'l':
			logmode++;
			break;

		case 'w':
			opts->allow_write = 1;
			break;

		case 'n':
			opts->no_prompt = 1;
			break;

		case '?':
			print_version(gbls.progname);
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

	if (optind < argc) {
		if (!logmode)
			opts->device = strdup(argv[optind]);
		else
			fill_log_list(argc, argv, optind);
	}

	return ;
}

/*
 * get_line()
 *
 */
static char * get_line (FILE *stream, int no_prompt)
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
		if (no_prompt)
			line = readline(NULL);
		else
			line = readline(PROMPT);

		if (line && *line) {
			g_strchug(line);
			add_history (line);
		}
	}

	return line;
}					/* get_line */

#define LOG_CTL_PROC "/proc/fs/ocfs2_nodemanager/log_mask"
static int set_logmode_proc(struct log_entry *entry)
{
	FILE *f;

	f = fopen(LOG_CTL_PROC, "w");
	if (!f) {
		fprintf(stderr, "%s: Unable to open \"%s\": %s\n",
			gbls.progname, LOG_CTL_PROC, strerror(errno));
		return 1;
	}
	fprintf(f, "%s %s\n", entry->mask, entry->action);
	fclose(f);

	return 0;
}

#define LOG_CTL_SYSFS_DIR "/sys/o2cb/logmask"
#define LOG_CTL_SYSFS_FORMAT LOG_CTL_SYSFS_DIR "/%s"
static int set_logmode_sysfs(struct log_entry *entry)
{
	FILE *f;
	char *logpath;

	logpath = g_strdup_printf(LOG_CTL_SYSFS_FORMAT, entry->mask);
	f = fopen(logpath, "w");
	g_free(logpath);
	if (!f) {
		fprintf(stderr,
			"%s: Unable to write log mask \"%s\": %s\n",
			gbls.progname, entry->mask, strerror(errno));
		return 1;
	}
	fprintf(f, "%s\n", entry->action);
	fclose(f);

	return 0;
}

static int get_logmode_sysfs(const char *name)
{
	char *logpath;
	char *current_mask;

	logpath = g_strdup_printf(LOG_CTL_SYSFS_FORMAT, name);
	if (g_file_get_contents(logpath, &current_mask,
				NULL, NULL)) {
		fprintf(stdout, "%s %s", name, current_mask);
	}
	g_free(logpath);

	return 0;
}

static void run_logmode_proc(void)
{
	GList *tmp;
	char *current_mask;

	if (loglist) {
		tmp = loglist;
		while (tmp) {
			if (set_logmode_proc(tmp->data))
				break;
			tmp = tmp->next;
		}
	} else {
		if (g_file_get_contents(LOG_CTL_PROC, &current_mask,
					NULL, NULL)) {
			fprintf(stdout, "%s", current_mask);
		}
	}
}

static void run_logmode_sysfs(void)
{
	GList *tmp;
	DIR *dir;
	struct dirent *d;

	if (loglist) {
		tmp = loglist;
		while (tmp) {
			if (set_logmode_sysfs(tmp->data))
				break;
			tmp = tmp->next;
		}
	} else {
		dir = opendir(LOG_CTL_SYSFS_DIR);
		if (dir) {
			while ((d = readdir(dir)) != NULL)
				get_logmode_sysfs(d->d_name);
			closedir(dir);
		}
	}
}

static void run_logmode(void)
{
	struct stat stat_buf;

	if (!stat(LOG_CTL_SYSFS_DIR, &stat_buf) &&
	    S_ISDIR(stat_buf.st_mode))
		run_logmode_sysfs();
	else if (!stat(LOG_CTL_PROC, &stat_buf) &&
		 S_ISREG(stat_buf.st_mode))
		run_logmode_proc();
}


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

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	memset(&opts, 0, sizeof(opts));
	memset(&gbls, 0, sizeof(gbls));

	gbls.progname = basename(argv[0]);

	get_options(argc, argv, &opts);
	if (logmode) {
		run_logmode();
		goto bail;
	}

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
		line = get_line(cmd, opts.no_prompt);

		if (line) {
			if (!gbls.interactive && !opts.no_prompt)
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
