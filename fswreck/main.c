/*
 * main.c
 *
 * entry point for fswrk
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
 * Authors: Sunil Mushran
 */

#include <main.h>

#define MAX_CORRUPT		6

char *progname = NULL;
char *device = NULL;
uint16_t nodenum = 0;
int corrupt[MAX_CORRUPT];

struct corrupt_funcs {
	void (*func) (ocfs2_filesys *fs, int code, uint16_t nodenum);
};

struct corrupt_funcs cf[] = {
	{ NULL },		/* 0 */
	{ NULL },		/* 1 */
	{ NULL },		/* 2 */
	{ &corrupt_chains },	/* 3 */
	{ &corrupt_chains },	/* 4 */
	{ &corrupt_chains },	/* 5 */
	{ &corrupt_chains }	/* 6 */
};

/*
 * usage()
 *
 */
static void usage (char *progname)
{
	g_print ("Usage: %s [OPTION]... [DEVICE]\n", progname);
	g_print ("	-c <corrupt code>\n");
	g_print ("	-n <node number>\n");
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
 * handle_signal()
 *
 */
static void handle_signal (int sig)
{
	switch (sig) {
	case SIGTERM:
	case SIGINT:
		exit(1);
	}

	return ;
}					/* handle_signal */


/*
 * read_options()
 *
 */
static int read_options(int argc, char **argv)
{
	int c;
	int ind;

	progname = basename(argv[0]);

	if (argc < 2) {
		usage(progname);
		return 1;
	}

	while(1) {
		c = getopt(argc, argv, "c:n:");
		if (c == -1)
			break;

		switch (c) {
		case 'c':	/* corrupt */
			ind = strtoul(optarg, NULL, 0);
			if (ind <= MAX_CORRUPT)
				corrupt[ind] = 1;
			else {
				printf("booo\n");
				return -1;
			}
			break;

		case 'n':	/* nodenum */
			nodenum = strtoul(optarg, NULL, 0);
			break;

		default:
			break;
		}
	}

	if (optind < argc && argv[optind])
		device = argv[optind];

	return 0;
}

/*
 * main()
 *
 */
int main (int argc, char **argv)
{
	ocfs2_filesys *fs = NULL;
	errcode_t ret = 0;
	int i;

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

	memset(corrupt, 0, sizeof(corrupt));

	if (read_options(argc, argv))
		goto bail;

	if (!device) {
		usage(progname);
		goto bail;
	}

	print_version (progname);

	ret = ocfs2_open(device, OCFS2_FLAG_RW, 0, 0, &fs);
	if (ret) {
		com_err(progname, ret, "while opening \"%s\"", device);
		goto bail;
	}

	for (i = 1; i <= MAX_CORRUPT; ++i) {
		if (corrupt[i]) {
			if (cf[i].func)
				cf[i].func(fs, i, nodenum);
			else
				printf("Unimplemented corrupt code = %d\n", i);
		}
	}

bail:
	if (fs) {
		ret = ocfs2_close(fs);
		if (ret)
			com_err(progname, ret, "while closing \"%s\"", device);
	}

	return 0;
}					/* main */
