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

#include <mkfs.h>
#include <mkfs_utils.h>

static void usage(const char *progname)
{
	fprintf(stderr, "Usage: %s [-L volume-label] [-n number-of-nodes]\n"
			"\t\t[-j journal-size] [-S volume-size] [-qvV] device\n",
			progname);
	exit(0);
}

static void version(const char *progname)
{
	fprintf(stderr, "%s %s\n", progname, VERSION);
}

static void print_tunefs_state(State *s)
{
	if (s->quiet)
		return;

	printf("Filesystem label=%s\n", s->vol_label);
	printf("Block size=%u (bits=%u)\n", s->blocksize, s->blocksize_bits);
	printf("Cluster size=%u (bits=%u)\n", s->cluster_size, s->cluster_size_bits);
	printf("Volume size=%llu (%u clusters) (%"PRIu64" blocks)\n",
	       (unsigned long long) s->volume_size_in_bytes,
	       s->volume_size_in_clusters, s->volume_size_in_blocks);
	printf("Initial number of nodes: %u\n", s->initial_nodes);
}

static State * get_tunefs_state(int argc, char **argv)
{
	char *progname;
	char *vol_label = NULL;
	unsigned int initial_nodes = 0;
	char *dummy;
	State *s;
	int c;
	int verbose = 0, quiet = 0;
	int show_version = 0;
	char *device_name;
	int ret;
	uint64_t val;
	uint64_t journal_size_in_bytes = 0;
	uint64_t volume_size_in_bytes = 0;
	uint64_t max_journal_size = 500 * ONE_MEGA_BYTE;

	static struct option long_options[] = {
		{ "label", 1, 0, 'L' },
		{ "nodes", 1, 0, 'n' },
		{ "verbose", 0, 0, 'v' },
		{ "quiet", 0, 0, 'q' },
		{ "version", 0, 0, 'V' },
		{ "journalsize", 0, 0, 'j'},
		{ "volumesize", 0, 0, 'S'},
		{ 0, 0, 0, 0}
	};

	if (argc && *argv)
		progname = basename(argv[0]);
	else
		progname = strdup("tunefs.ocfs2");

	while (1) {
		c = getopt_long(argc, argv, "L:n:j:S:vqV", long_options, 
				NULL);

		if (c == -1)
			break;

		switch (c) {
		case 'L':
			vol_label = strdup(optarg);

			if (strlen(vol_label) >= MAX_VOL_LABEL_LEN) {
				com_err(progname, 0,
					"Volume label too long: must be less "
					"than %d characters",
					MAX_VOL_LABEL_LEN);
				exit(1);
			}

			break;

		case 'n':
			initial_nodes = strtoul(optarg, &dummy, 0);

			if (initial_nodes > OCFS2_MAX_NODES || *dummy != '\0') {
				com_err(progname, 0,
					"Initial nodes must be no more than %d",
					OCFS2_MAX_NODES);
				exit(1);
			} else if (initial_nodes < 2) {
				com_err(progname, 0,
					"Initial nodes must be at least 2");
				exit(1);
			}

			break;

		case 'j':
			ret = get_number(optarg, &val);

			if (ret || 
			    val < OCFS2_MIN_JOURNAL_SIZE ||
			    val > max_journal_size) {
				com_err(progname, 0,
					"Invalid journal size %s: must be "
					"between %d and %"PRIu64" bytes",
					optarg,
					OCFS2_MIN_JOURNAL_SIZE,
					max_journal_size);
				exit(1);
			}

			journal_size_in_bytes = val;

			break;

		case 'S':
			ret = get_number(optarg, &val);

//			if (ret || val > MAX_VOL_SIZE) {
//				com_err(progname, 0,
//					"Invalid device size %s: must be "
//					"between %d and %"PRIu64" bytes",
//					optarg,
//					BOO, BOO);
//				exit(1);
//			}

			volume_size_in_bytes = val;

			break;

		case 'v':
			verbose = 1;
			break;

		case 'q':
			quiet = 1;
			break;

		case 'V':
			show_version = 1;
			break;

		default:
			usage(progname);
			break;
		}
	}

	if ((optind == argc) && !show_version)
		usage(progname);

	device_name = argv[optind];

	if (!quiet || show_version)
		version(progname);

	if (show_version)
		exit(0);

	s = malloc(sizeof(State));
	memset(s, 0, sizeof(State));

	s->progname      = progname;

	s->verbose       = verbose;
	s->quiet         = quiet;

	s->new.vol_label     = vol_label;
	s->new.initial_nodes = initial_nodes;

	s->device_name   = strdup(device_name);

	s->fd            = -1;

	s->format_time   = time(NULL);

	s->new.journal_size_in_bytes = journal_size_in_bytes;
	s->new.volume_size_in_bytes = volume_size_in_bytes;

	return s;
}


static void fill_tunefs_defaults(State *s, ocfs2_filesys *fs)
{
	size_t pagesize;

	pagesize = getpagesize();

	s->pagesize_bits = get_bits(s, pagesize);

	s->blocksize = fs->fs_blocksize;
	s->blocksize_bits = get_bits(s, s->blocksize);
	s->cluster_size = fs->fs_clustersize;
	s->cluster_size_bits = get_bits(s, s->cluster_size);
	s->initial_nodes = OCFS2_RAW_SB(fs->fs_super)->s_max_nodes;

	s->volume_size_in_clusters = fs->fs_clusters;
	s->volume_size_in_blocks = fs->fs_blocks;
	s->volume_size_in_bytes = fs->fs_clusters << s->cluster_size_bits;
	
	s->new.volume_size_in_clusters = s->new.volume_size_in_bytes >> s->cluster_size_bits;
	s->new.volume_size_in_blocks = (s->new.volume_size_in_clusters << s->cluster_size_bits) >> s->blocksize_bits;

	s->reserved_tail_size = 0;

	s->vol_label = strdup(OCFS2_RAW_SB(fs->fs_super)->s_label);

//	s->journal_size_in_bytes = figure_journal_size(s->journal_size_in_bytes, s);
}

int main(int argc, char **argv)
{
	errcode_t ret = 0;
	State *s;
	ocfs2_filesys *fs = NULL;

	initialize_ocfs_error_table();

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	s = get_tunefs_state(argc, argv);

	ret = ocfs2_open(s->device_name, OCFS2_FLAG_RO, 0, 0, &fs);
	if (ret)
		goto bail;

	fill_tunefs_defaults(s, fs);

//	adjust_volume_size(s);

	print_tunefs_state (s);

	check_32bit_blocks (s);

	if (s->new.vol_label) {
		printf("Changing volume label from %s to %s\n", s->vol_label,
		       s->new.vol_label);
		strncpy(OCFS2_RAW_SB(fs->fs_super)->s_label, s->new.vol_label, 63);
	}

	if (s->new.initial_nodes) {
		printf("Changing number of nodes from %d to %d\n",
		       s->initial_nodes, s->new.initial_nodes);
	}

	if (s->new.journal_size_in_bytes) {
		printf("Changing journal size %"PRIu64" to %"PRIu64"\n",
		       s->journal_size_in_bytes, s->new.journal_size_in_bytes);
	}

	if (s->new.volume_size_in_bytes) {
		printf("Changing volume size %"PRIu64" to %"PRIu64"\n",
		       s->volume_size_in_bytes, s->new.volume_size_in_bytes);
	}

bail:
	if (fs)
		ocfs2_close(fs);

	return ret;
}

