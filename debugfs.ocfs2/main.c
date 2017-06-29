/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * main.c
 *
 * entry point for debugfs.ocfs2
 *
 * Copyright (C) 2004, 2007 Oracle.  All rights reserved.
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301 USA.
 *
 * Authors: Sunil Mushran, Manish Singh
 */

#include "main.h"
#include <sys/types.h>
#include <dirent.h>

#define PROMPT "debugfs: "

extern struct dbgfs_gbls gbls;

static int decodemode = 0;
static int encodemode = 0;
static int arg_ind = 0;

static int logmode = 0;
struct log_entry {
	char *mask;
	char *action;
};
static GList *loglist = NULL;

static void usage(char *progname)
{
	g_print("usage: %s -l [<logentry> ... [allow|off|deny]] ...\n", progname);
	g_print("usage: %s -d, --decode <lockres>\n", progname);
	g_print("usage: %s -e, --encode <lock type> <block num> <generation|parent>\n", progname);
	g_print("usage: %s [-f cmdfile] [-R request] [-i] [-s backup#] [-V] [-w] [-n] [-?] [device]\n", progname);
	g_print("\t-f, --file <cmdfile>\t\tExecute commands in cmdfile\n");
	g_print("\t-R, --request <command>\t\tExecute a single command\n");
	g_print("\t-s, --superblock <backup#>\tOpen the device using a backup superblock\n");
	g_print("\t-i, --image\t\t\tOpen an o2image file\n");
	g_print("\t-w, --write\t\t\tOpen in read-write mode instead of the default of read-only\n");
	g_print("\t-V, --version\t\t\tShow version\n");
	g_print("\t-n, --noprompt\t\t\tHide prompt\n");
	g_print("\t-?, --help\t\t\tShow this help\n");
	exit(0);
}

static void print_version(char *progname)
{
	fprintf(stderr, "%s %s\n", progname, VERSION);
}

static void process_one_list(GList *list, char *action)
{
	GList *tmp;
	struct log_entry *entry = NULL;


	while (list) {
		tmp = loglist;
		while (tmp) {
			entry = tmp->data;
			if (!strcasecmp(entry->mask, list->data))
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
		if (!strcasecmp(argv[i], "allow") ||
		    !strcasecmp(argv[i], "deny") ||
		    !strcasecmp(argv[i], "off")) {
			process_one_list(tmplist, argv[i]);
			g_list_free(tmplist);
			tmplist = NULL;
		} else {
			tmplist = g_list_append(tmplist, argv[i]);
		}
	}
}

static void process_decode_lockres(int argc, char **argv, int startind)
{
	int i;
	errcode_t ret;
	enum ocfs2_lock_type type;
	uint64_t blkno = 0;
	uint32_t generation = 0;
	uint64_t parent = 0;

	if (startind + 1 > argc) {
		usage(gbls.progname);
		exit(1);
	}

	for (i = startind; i < argc; ++i) {
		ret = ocfs2_decode_lockres(argv[i], &type, &blkno,
					   &generation, &parent);
		if (ret)
			continue;

		printf("Lockres:    %s\n", argv[i]);
		printf("Type:       %s\n", ocfs2_lock_type_string(type));
		if (blkno)
			printf("Block:      %"PRIu64"\n", blkno);
		if (generation)
			printf("Generation: 0x%08x\n", generation);
		if (parent)
			printf("Parent:	    %"PRIu64"\n", parent);
		printf("\n");
	}

	return ;
}

static void process_encode_lockres(int argc, char **argv, int startind)
{
	int i;
	errcode_t ret;
	enum ocfs2_lock_type type;
	uint64_t blkno;
	uint64_t extra; /* generation or parent */
	char lock[OCFS2_LOCK_ID_MAX_LEN];
	char tmp[OCFS2_LOCK_ID_MAX_LEN];

	if (startind + 3 > argc) {
		usage(gbls.progname);
		exit(1);
	}

	i = startind;

	type = ocfs2_get_lock_type(argv[i++][0]);
	blkno = strtoull(argv[i++], NULL, 0);
	extra = strtoull(argv[i++], NULL, 0);

	if (type == OCFS2_LOCK_TYPE_DENTRY) {
		ret = ocfs2_encode_lockres(type, blkno, 0, extra, tmp);
		if (!ret)
			ret = ocfs2_printable_lockres(tmp, lock, sizeof(lock));
	} else
		ret = ocfs2_encode_lockres(type, blkno, (uint32_t)extra, 0,
					   lock);
	if (ret) {
		com_err(gbls.progname, ret, "while encoding lockname");
		return ;
	}

	printf("%s\n", lock);

	return ;
}

static void get_options(int argc, char **argv, struct dbgfs_opts *opts)
{
	int c;
	char *ptr = NULL;
	static struct option long_options[] = {
		{ "file", 1, 0, 'f' },
		{ "request", 1, 0, 'R' },
		{ "version", 0, 0, 'V' },
		{ "help", 0, 0, '?' },
		{ "write", 0, 0, '?' },
		{ "log", 0, 0, 'l' },
		{ "noprompt", 0, 0, 'n' },
		{ "decode", 0, 0, 'd' },
		{ "encode", 0, 0, 'e' },
		{ "superblock", 1, 0, 's' },
		{ "image", 0, 0, 'i' },
		{ 0, 0, 0, 0}
	};

	while (1) {
		if (decodemode || encodemode || logmode)
			break;

		c = getopt_long(argc, argv, "lf:R:deV?wns:i",
				long_options, NULL);
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

		case 'R':
			opts->one_cmd = strdup(optarg);
			if (!strlen(opts->one_cmd)) {
				usage(gbls.progname);
				exit(1);
			}
			break;

		case 'd':
			decodemode++;
			break;

		case 'e':
			encodemode++;
			break;

		case 'i':
			opts->imagefile = 1;
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

		case 's':
			opts->sb_num = strtoul(optarg, &ptr, 0);
			break;

		default:
			usage(gbls.progname);
			break;
		}
	}

	if (optind < argc) {
		if (logmode)
			fill_log_list(argc, argv, optind);
		else
			opts->device = strdup(argv[optind]);
	}

	if (decodemode || encodemode)
		arg_ind = optind;

	return ;
}

static char *get_line(FILE *stream, int no_prompt)
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
			add_history(line);
		}
	}

	return line;
}

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

#define LOG_CTL_SYSFS_DIR_OLD "/sys/o2cb/logmask"
#define LOG_CTL_SYSFS_DIR "/sys/fs/o2cb/logmask"
#define LOG_CTL_SYSFS_FORMAT "%s/%s"
static int set_logmode_sysfs(const char *path, struct log_entry *entry)
{
	FILE *f;
	char *logpath;

	logpath = g_strdup_printf(LOG_CTL_SYSFS_FORMAT, path, entry->mask);
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

static int get_logmode_sysfs(const char *path, const char *name)
{
	char *logpath;
	char *current_mask = NULL;

	logpath = g_strdup_printf(LOG_CTL_SYSFS_FORMAT, path, name);
	if (g_file_get_contents(logpath, &current_mask,
				NULL, NULL)) {
		fprintf(stdout, "%s %s", name, current_mask);
	}
	g_free(logpath);

	g_free(current_mask);
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

static void run_logmode_sysfs(const char *path)
{
	GList *tmp;
	DIR *dir;
	struct dirent *d;

	if (loglist) {
		tmp = loglist;
		while (tmp) {
			if (set_logmode_sysfs(path, tmp->data))
				break;
			tmp = tmp->next;
		}
	} else {
		dir = opendir(path);
		if (dir) {
			while ((d = readdir(dir)) != NULL)
				get_logmode_sysfs(path, d->d_name);
			closedir(dir);
		}
	}
}

static void run_logmode(void)
{
	struct stat stat_buf;

	if (!stat(LOG_CTL_SYSFS_DIR, &stat_buf) &&
	    S_ISDIR(stat_buf.st_mode))
		run_logmode_sysfs(LOG_CTL_SYSFS_DIR);
        else if (!stat(LOG_CTL_SYSFS_DIR_OLD, &stat_buf) &&
	    S_ISDIR(stat_buf.st_mode))
		run_logmode_sysfs(LOG_CTL_SYSFS_DIR_OLD);
	else if (!stat(LOG_CTL_PROC, &stat_buf) &&
		 S_ISREG(stat_buf.st_mode))
		run_logmode_proc();
}

int main(int argc, char **argv)
{
	char *line;
	struct dbgfs_opts opts;
	FILE *cmd = NULL;

	initialize_o2cb_error_table();
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

	if (decodemode) {
		process_decode_lockres(argc, argv, arg_ind);
		goto bail;
	}

	if (encodemode) {
		process_encode_lockres(argc, argv, arg_ind);
		goto bail;
	}

	gbls.allow_write = opts.allow_write;
	gbls.imagefile = opts.imagefile;
	if (!opts.cmd_file)
		gbls.interactive++;

	if (opts.device) {
		if (opts.sb_num)
			line = g_strdup_printf("open %s -s %u", opts.device, opts.sb_num);
		else
			line = g_strdup_printf("open %s", opts.device);
		do_command(line);
		g_free(line);
	}

	if (opts.one_cmd) {
		do_command(opts.one_cmd);
		goto bail;
	}

	if (opts.cmd_file) {
		cmd = fopen(opts.cmd_file, "r");
		if (!cmd) {
			com_err(argv[0], errno, "'%s'", opts.cmd_file);
			goto bail;
		}
	}

	if (!opts.no_prompt)
		print_version(gbls.progname);

	while (1) {
		line = get_line(cmd, opts.no_prompt);

		if (line) {
			if (!gbls.interactive && !opts.no_prompt)
				fprintf(stdout, "%s%s\n", PROMPT, line);
			do_command(line);
			if (gbls.interactive)
				free(line);
		} else {
			printf("\n");
			raise(SIGTERM);
			exit(0);
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
}
