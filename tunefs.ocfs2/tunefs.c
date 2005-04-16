/*
 * tune.c
 *
 * ocfs2 tune utility
 *
 * Copyright (C) 2004 Oracle Corporation.  All rights reserved.
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

#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE /* Because libc really doesn't want us using O_DIRECT? */

#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fd.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <malloc.h>
#include <time.h>
#include <libgen.h>
#include <netinet/in.h>
#include <inttypes.h>
#include <ctype.h>
#include <signal.h>

#include <ocfs2.h>
#include <ocfs2_fs.h>
#include <ocfs1_fs_compat.h>
#include <kernel-list.h>

#define SYSTEM_FILE_NAME_MAX   40

typedef struct _ocfs2_tune_opts {
	uint16_t num_nodes;
	uint64_t vol_size;
	uint64_t jrnl_size;
	char *vol_label;
	char *progname;
	char *device;
	int verbose;
	int quiet;
	int prompt;
	time_t tune_time;
	int fd;
} ocfs2_tune_opts;

ocfs2_tune_opts opts;
ocfs2_filesys *fs_gbl = NULL;
int cluster_locked = 0;

static void usage(const char *progname)
{
	fprintf(stderr, "usage: %s [-L volume-label] [-N number-of-nodes]\n"
			"\t[-J journal-options] [-S volume-size] [-qvV] "
			"device\n",
			progname);
	exit(0);
}

static void version(const char *progname)
{
	fprintf(stderr, "%s %s\n", progname, VERSION);
}

static void handle_signal(int sig)
{
	switch (sig) {
	case SIGTERM:
	case SIGINT:
		printf("\nProcess Interrupted.\n");

		if (cluster_locked && fs_gbl && fs_gbl->fs_dlm_ctxt)
			ocfs2_release_cluster(fs_gbl);

		if (fs_gbl && fs_gbl->fs_dlm_ctxt)
			ocfs2_shutdown_dlm(fs_gbl);

		exit(1);
	}

	return ;
}

/* Call this with SIG_BLOCK to block and SIG_UNBLOCK to unblock */
static void block_signals(int how)
{
     sigset_t sigs;

     sigfillset(&sigs);
     sigdelset(&sigs, SIGTRAP);
     sigdelset(&sigs, SIGSEGV);
     sigprocmask(how, &sigs, (sigset_t *) 0);

     return ;
}

static int get_number(char *arg, uint64_t *res)
{
	char *ptr = NULL;
	uint64_t num;

	num = strtoull(arg, &ptr, 0);

	if ((ptr == arg) || (num == UINT64_MAX))
		return(-EINVAL);

	switch (*ptr) {
	case '\0':
		break;

	case 'g':
	case 'G':
		num *= 1024;
		/* FALL THROUGH */

	case 'm':
	case 'M':
		num *= 1024;
		/* FALL THROUGH */

	case 'k':
	case 'K':
		num *= 1024;
		/* FALL THROUGH */

	case 'b':
	case 'B':
		break;

	default:
		return -EINVAL;
	}

	*res = num;

	return 0;
}

/* derived from e2fsprogs */
static void parse_journal_opts(char *progname, const char *opts,
			       uint64_t *journal_size_in_bytes)
{
	char *options, *token, *next, *p, *arg;
	int ret, journal_usage = 0;
	uint64_t val;

	options = strdup(opts);

	for (token = options; token && *token; token = next) {
		p = strchr(token, ',');
		next = NULL;

		if (p) {
			*p = '\0';
			next = p + 1;
		}

		arg = strchr(token, '=');

		if (arg) {
			*arg = '\0';
			arg++;
		}

		if (strcmp(token, "size") == 0) {
			if (!arg) {
				journal_usage++;
				continue;
			}

			ret = get_number(arg, &val);

			if (ret ||
			    val < OCFS2_MIN_JOURNAL_SIZE ||
			    val > OCFS2_MAX_JOURNAL_SIZE) {
				com_err(progname, 0,
					"Invalid journal size: %s\nSize must "
					"be between %d and %d bytes",
					arg,
					OCFS2_MIN_JOURNAL_SIZE,
					OCFS2_MAX_JOURNAL_SIZE);
				exit(1);
			}

			*journal_size_in_bytes = val;
		} else
			journal_usage++;
	}

	if (journal_usage) {
		com_err(progname, 0,
			"Bad journal options specified. Valid journal "
			"options are:\n"
			"\tsize=<journal size>\n");
		exit(1);
	}

	free(options);
}

static void get_options(int argc, char **argv)
{
	int c;
	int show_version = 0;
	int ret;
	char *dummy;
	uint64_t val;

	static struct option long_options[] = {
		{ "label", 1, 0, 'L' },
		{ "nodes", 1, 0, 'N' },
		{ "verbose", 0, 0, 'v' },
		{ "quiet", 0, 0, 'q' },
		{ "version", 0, 0, 'V' },
		{ "journal-options", 0, 0, 'J'},
		{ "volume-size", 0, 0, 'S'},
		{ 0, 0, 0, 0}
	};

	if (argc && *argv)
		opts.progname = basename(argv[0]);
	else
		opts.progname = strdup("tunefs.ocfs2");

	opts.prompt = 1;

	while (1) {
		c = getopt_long(argc, argv, "L:N:J:S:vqVx", long_options, 
				NULL);

		if (c == -1)
			break;

		switch (c) {
		case 'L':
			opts.vol_label = strdup(optarg);

			if (strlen(opts.vol_label) >= OCFS2_MAX_VOL_LABEL_LEN) {
				com_err(opts.progname, 0,
					"Volume label too long: must be less "
					"than %d characters",
					OCFS2_MAX_VOL_LABEL_LEN);
				exit(1);
			}
			break;

		case 'N':
			opts.num_nodes = strtoul(optarg, &dummy, 0);

			if (opts.num_nodes > OCFS2_MAX_NODES ||
			    *dummy != '\0') {
				com_err(opts.progname, 0,
					"Number of nodes must be no more than %d",
					OCFS2_MAX_NODES);
				exit(1);
			} else if (opts.num_nodes < 2) {
				com_err(opts.progname, 0,
					"Initial nodes must be at least 2");
				exit(1);
			}
			break;

		case 'J':
			parse_journal_opts(opts.progname, optarg,
					   &opts.jrnl_size);
			break;

		case 'S':
			ret = get_number(optarg, &val);
			if (ret)
				exit(1);
			opts.vol_size = val;
			break;

		case 'v':
			opts.verbose = 1;
			break;

		case 'q':
			opts.quiet = 1;
			break;

		case 'V':
			show_version = 1;
			break;

		case 'x':
			opts.prompt = 0;
			break;

		default:
			usage(opts.progname);
			break;
		}
	}

	if (!opts.quiet || show_version)
		version(opts.progname);

	if (show_version)
		exit(0);

	if (optind == argc)
		usage(opts.progname);

	opts.device = strdup(argv[optind]);

	opts.tune_time = time(NULL);

	return ;
}

static errcode_t add_nodes(ocfs2_filesys *fs)
{
	errcode_t ret = 0;
	uint16_t old_num = OCFS2_RAW_SB(fs->fs_super)->s_max_nodes;
	char fname[SYSTEM_FILE_NAME_MAX];
	uint64_t blkno;
	int i, j;
	char *display_str = NULL;

	for (i = OCFS2_LAST_GLOBAL_SYSTEM_INODE + 1; i < NUM_SYSTEM_INODES; ++i) {
		for (j = old_num; j < opts.num_nodes; ++j) {
			sprintf(fname, ocfs2_system_inodes[i].si_name, j);
			asprintf(&display_str, "Adding %s...", fname);
			printf("%s", display_str);
			fflush(stdout);

			/* Goto next if file already exists */
			ret = ocfs2_lookup(fs, fs->fs_sysdir_blkno, fname,
					   strlen(fname), NULL, &blkno);
			if (!ret)
				goto next_file;

			/* create inode for system file */
			ret = ocfs2_new_system_inode(fs, &blkno,
						      ocfs2_system_inodes[i].si_mode,
						      ocfs2_system_inodes[i].si_iflags);
			if (ret)
				goto bail;

			/* Add the inode to the system dir */
			ret = ocfs2_link(fs, fs->fs_sysdir_blkno, fname, blkno,
					 OCFS2_FT_REG_FILE);
			if (!ret)
				goto next_file;
			if (ret == OCFS2_ET_DIR_NO_SPACE) {
				ret = ocfs2_expand_dir(fs, fs->fs_sysdir_blkno,
						       fs->fs_sysdir_blkno);
				if (!ret)
					ret = ocfs2_link(fs, fs->fs_sysdir_blkno,
							 fname, blkno,
							 OCFS2_FT_REG_FILE);
			} else
				goto bail;
next_file:
			if (display_str) {
				memset(display_str, ' ', strlen(display_str));
				printf("\r%s\r", display_str);
				fflush(stdout);
				free(display_str);
				display_str = NULL;
			}
		}
	}
bail:
	if (display_str) {
		free(display_str);
		printf("\n");
	}

	return ret;
}

static errcode_t get_default_journal_size(ocfs2_filesys *fs, uint64_t *jrnl_size)
{
	errcode_t ret;
	char jrnl_node0[SYSTEM_FILE_NAME_MAX];
	uint64_t blkno;
	char *buf = NULL;
	ocfs2_dinode *di;

	snprintf (jrnl_node0, sizeof(jrnl_node0),
		  ocfs2_system_inodes[JOURNAL_SYSTEM_INODE].si_name, 0);

	ret = ocfs2_lookup(fs, fs->fs_sysdir_blkno, jrnl_node0,
			   strlen(jrnl_node0), NULL, &blkno);
	if (ret)
		goto bail;


	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret)
		goto bail;

	di = (ocfs2_dinode *)buf;
	*jrnl_size = (di->i_clusters <<
		      OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits);
bail:
	if (buf)
		ocfs2_free(&buf);

	return ret;
}

static void update_volume_label(ocfs2_filesys *fs, int *changed)
{
  	memset (OCFS2_RAW_SB(fs->fs_super)->s_label, 0,
		OCFS2_MAX_VOL_LABEL_LEN);
	strncpy (OCFS2_RAW_SB(fs->fs_super)->s_label, opts.vol_label,
		 OCFS2_MAX_VOL_LABEL_LEN);

	*changed = 1;

	return ;
}

static errcode_t update_nodes(ocfs2_filesys *fs, int *changed)
{
	errcode_t ret = 0;

	block_signals(SIG_BLOCK);
	ret = add_nodes(fs);
	block_signals(SIG_UNBLOCK);
	if (ret)
		return ret;

	OCFS2_RAW_SB(fs->fs_super)->s_max_nodes = opts.num_nodes;
	*changed = 1;

	return ret;
}

static errcode_t update_journal_size(ocfs2_filesys *fs, int *changed)
{
	errcode_t ret = 0;
	char jrnl_file[40];
	uint64_t blkno;
	int i;
	uint16_t max_nodes = OCFS2_RAW_SB(fs->fs_super)->s_max_nodes;
	uint32_t num_clusters;
	char *buf = NULL;
	ocfs2_dinode *di;

	num_clusters = opts.jrnl_size >>
			OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	for (i = 0; i < max_nodes; ++i) {
		snprintf (jrnl_file, sizeof(jrnl_file),
			  ocfs2_system_inodes[JOURNAL_SYSTEM_INODE].si_name, i);

		ret = ocfs2_lookup(fs, fs->fs_sysdir_blkno, jrnl_file,
				   strlen(jrnl_file), NULL, &blkno);
		if (ret)
			goto bail;


		ret = ocfs2_read_inode(fs, blkno, buf);
		if (ret)
			goto bail;

		di = (ocfs2_dinode *)buf;
		if (num_clusters <= di->i_clusters)
			continue;

		printf("Extending %s...  ", jrnl_file);
		block_signals(SIG_BLOCK);
		ret = ocfs2_extend_allocation(fs, blkno,
					      (num_clusters - di->i_clusters));
		block_signals(SIG_UNBLOCK);
		if (ret)
			goto bail;

		ret = ocfs2_read_inode(fs, blkno, buf);
		if (ret)
			goto bail;

		di = (ocfs2_dinode *)buf;
		di->i_size = di->i_clusters <<
				OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits;
		di->i_mtime = time(NULL);

		ret = ocfs2_write_inode(fs, blkno, buf);
		if (ret)
			goto bail;
		printf("\r                                                     \r");
	}

bail:
	if (ret)
		printf("\n");

	if (buf)
		ocfs2_free(&buf);

	return ret;
}

static errcode_t update_volume_size(ocfs2_filesys *fs, int *changed)
{
	printf("TODO: update_volume_size\n");
	return 0;
}

int main(int argc, char **argv)
{
	errcode_t ret = 0;
	ocfs2_filesys *fs = NULL;
	int upd_label = 0;
	int upd_nodes = 0;
	int upd_jrnls = 0;
	int upd_vsize = 0;
	uint16_t tmp;
	uint64_t def_jrnl_size = 0;
	uint64_t num_clusters;
	uint64_t vol_size = 0;

	initialize_ocfs_error_table();
	initialize_o2dl_error_table();
	initialize_o2cb_error_table();

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	if (signal(SIGTERM, handle_signal) == SIG_ERR) {
		fprintf(stderr, "Could not set SIGTERM\n");
		exit(1);
	}

	if (signal(SIGINT, handle_signal) == SIG_ERR) {
		fprintf(stderr, "Could not set SIGINT\n");
		exit(1);
	}

	memset (&opts, 0, sizeof(opts));

	get_options(argc, argv);

	ret = ocfs2_open(opts.device, OCFS2_FLAG_RW, 0, 0, &fs);
	if (ret) {
		com_err(opts.progname, ret, " ");
		goto close;
	}
	fs_gbl = fs;

	ret = ocfs2_initialize_dlm(fs);
	if (ret) {
		com_err(opts.progname, ret, " ");
		goto close;
	}

	block_signals(SIG_BLOCK);
	ret = ocfs2_lock_down_cluster(fs);
	if (ret) {
		com_err(opts.progname, ret, " ");
		goto close;
	}
	cluster_locked = 1;
	block_signals(SIG_UNBLOCK);

//	check_32bit_blocks (s);

	/* get journal size of node 0 */
	ret = get_default_journal_size(fs, &def_jrnl_size);
	if (ret)
		goto unlock;

	/* validate volume label */
	if (opts.vol_label) {
		printf("Changing volume label from %s to %s\n",
		       OCFS2_RAW_SB(fs->fs_super)->s_label, opts.vol_label);
	}

	/* validate num nodes */
	if (opts.num_nodes) {
		tmp = OCFS2_RAW_SB(fs->fs_super)->s_max_nodes;
		if (opts.num_nodes > tmp) {
			printf("Changing number of nodes from %d to %d\n",
			       tmp, opts.num_nodes);
		} else {
			printf("ERROR: Nodes (%d) has to be larger than "
			       "configured nodes (%d)\n", opts.num_nodes, tmp);
			goto unlock;
		}

		if (!opts.jrnl_size)
			opts.jrnl_size = def_jrnl_size;
	}

	/* validate journal size */
	if (opts.jrnl_size) {
		num_clusters = (opts.jrnl_size + fs->fs_clustersize - 1) >>
				OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits;

		opts.jrnl_size = num_clusters <<
				OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits;

		if (opts.jrnl_size > def_jrnl_size)
			printf("Changing journal size %"PRIu64" to %"PRIu64"\n",
			       def_jrnl_size, opts.jrnl_size);
		else {
			if (!opts.num_nodes) {
				printf("ERROR: Journal size %"PRIu64" has to be larger "
				       "than %"PRIu64"\n", opts.jrnl_size, def_jrnl_size);
				goto unlock;
			}
		}
	}

	/* validate volume size */
	if (opts.vol_size) {
		num_clusters = (opts.vol_size + fs->fs_clustersize - 1) >>
				OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits;
		opts.vol_size = num_clusters <<
				OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits;

		vol_size = fs->fs_clusters <<
			OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits;

		printf("Changing volume size %"PRIu64" to %"PRIu64"\n",
		       vol_size, opts.vol_size);
	}

	/* Abort? */
	if (opts.prompt) {
		printf("Proceed (y/N): ");
		if (toupper(getchar()) != 'Y') {
			printf("Aborting operation.\n");
			goto unlock;
		}
	}

	/* update volume label */
	if (opts.vol_label) {
		update_volume_label(fs, &upd_label);
		if (upd_label)
			printf("Changed volume label\n");
	}

	/* update number of nodes */
	if (opts.num_nodes) {
		ret = update_nodes(fs, &upd_nodes);
		if (ret) {
			com_err(opts.progname, ret, "while updating nodes");
			goto unlock;
		}
		if (upd_nodes)
			printf("Added nodes\n");
	}

	/* update journal size */
	if (opts.jrnl_size) {
		ret = update_journal_size(fs, &upd_jrnls);
		if (ret) {
			com_err(opts.progname, ret, "while updating journal size");
			goto unlock;
		}
		if (upd_jrnls)
			printf("Resized journals\n");
	}

	/* update volume size */
	if (opts.vol_size) {
		ret = update_volume_size(fs, &upd_vsize);
		if (ret) {
			com_err(opts.progname, ret, "while updating volume size");
			goto unlock;
		}
		if (upd_vsize)
			printf("Resized volume\n");
	}

	/* write superblock */
	if (upd_label || upd_nodes || upd_vsize) {
		block_signals(SIG_BLOCK);
		ret = ocfs2_write_super(fs);
		if (ret) {
			com_err(opts.progname, ret, "while writing superblock");
			goto unlock;
		}
		block_signals(SIG_UNBLOCK);
		printf("Wrote Superblock\n");
	}

unlock:
	block_signals(SIG_BLOCK);
	if (cluster_locked && fs->fs_dlm_ctxt)
		ocfs2_release_cluster(fs);
	cluster_locked = 0;
	block_signals(SIG_UNBLOCK);

close:
	block_signals(SIG_BLOCK);
	if (fs->fs_dlm_ctxt)
		ocfs2_shutdown_dlm(fs);
	block_signals(SIG_UNBLOCK);

	if (fs)
		ocfs2_close(fs);

	return ret;
}
