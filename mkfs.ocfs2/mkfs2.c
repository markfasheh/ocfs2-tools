/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * mkfs.c
 *
 * OCFS2 format utility
 *
 * Copyright (C) 2004 Oracle Corporation.  All rights reserved.
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
 * Authors: Manish Singh, Kurt Hackel
 */

#include <mkfs.h>
#include <mkfs_utils.h>

static State *get_state(int argc, char **argv);
static void usage(const char *progname);
static void version(const char *progname);
extern char *optarg;
extern int optind, opterr, optopt;

SystemFileInfo system_files[] = {
	{ "bad_blocks", SFI_OTHER, 1, 0 },
	{ "global_inode_alloc", SFI_CHAIN, 1, 0 },
	{ "dlm", SFI_DLM, 1, 0 },
	{ "global_bitmap", SFI_BITMAP, 1, 0 },
	{ "orphan_dir", SFI_OTHER, 1, 1 },
	{ "extent_alloc:%04d", SFI_CHAIN, 0, 0 },
	{ "inode_alloc:%04d", SFI_CHAIN, 0, 0 },
	{ "journal:%04d", SFI_JOURNAL, 0, 0 },
	{ "local_alloc:%04d", SFI_LOCAL_ALLOC, 0, 0 }
};

int
main(int argc, char **argv)
{
	State *s;
	SystemFileDiskRecord *record[NUM_SYSTEM_INODES];
	SystemFileDiskRecord global_alloc_rec;
	SystemFileDiskRecord crap_rec;
	SystemFileDiskRecord superblock_rec;
	SystemFileDiskRecord root_dir_rec;
	SystemFileDiskRecord system_dir_rec;
	int i, j, num;
	DirData *orphan_dir;
	DirData *root_dir;
	DirData *system_dir;
	uint32_t need;
	uint64_t allocated;
	SystemFileDiskRecord *tmprec;
	char fname[SYSTEM_FILE_NAME_MAX];

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	s = get_state(argc, argv);

	open_device(s);

	fill_defaults(s);  

	adjust_volume_size(s);

	generate_uuid (s);

	create_generation(s);

	print_state (s);

	check_32bit_blocks (s);

	init_record(s, &global_alloc_rec, SFI_OTHER, 0);
	global_alloc_rec.extent_off = 0;
	global_alloc_rec.extent_len = s->volume_size_in_bytes;

	init_record(s, &superblock_rec, SFI_OTHER, 0);
	init_record(s, &root_dir_rec, SFI_OTHER, 1);
	init_record(s, &system_dir_rec, SFI_OTHER, 1);

	for (i = 0; i < NUM_SYSTEM_INODES; i++) {
		num = system_files[i].global ? 1 : s->initial_nodes;
		record[i] = do_malloc(s, sizeof(SystemFileDiskRecord) * num);

		for (j = 0; j < num; j++) {
			init_record(s, &record[i][j],
				    system_files[i].type, system_files[i].dir);
		}
	}

	root_dir = alloc_directory(s);
	system_dir = alloc_directory(s);
	orphan_dir = alloc_directory(s);

	need = (s->volume_size_in_clusters + 7) >> 3;
	need = ((need + s->cluster_size - 1) >> s->cluster_size_bits) << s->cluster_size_bits;

	if (need > BITMAP_WARNING_LEN)
		fprintf(stderr, "WARNING: bitmap is very large, consider using "
				"a larger cluster size and/or\na smaller "
				"volume\n");

	if (!s->quiet)
		printf("Creating bitmaps: ");

	tmprec = &(record[GLOBAL_BITMAP_SYSTEM_INODE][0]);
	tmprec->extent_off = 0;
	tmprec->extent_len = need;

	s->global_bm = initialize_bitmap (s, s->volume_size_in_clusters,
					  s->cluster_size_bits,
					  "global bitmap", tmprec,
					  &global_alloc_rec);

	/*
	 * Set all bits up to and including the superblock.
	 */
	alloc_bytes_from_bitmap(s, (OCFS2_SUPER_BLOCK_BLKNO + 1) << s->blocksize_bits,
				s->global_bm, &(crap_rec.extent_off),
				&(crap_rec.extent_len));

	/*
	 * Alloc a placeholder for the future global chain allocator
	 */
	alloc_from_bitmap(s, 1, s->global_bm, &(crap_rec.extent_off),
                          &(crap_rec.extent_len));

	/*
	 * Now allocate the global inode alloc group
	 */
	tmprec = &(record[GLOBAL_INODE_ALLOC_SYSTEM_INODE][0]);

	need = blocks_needed(s);
	alloc_bytes_from_bitmap(s, need << s->blocksize_bits,
                                s->global_bm,
                                &(crap_rec.extent_off),
                                &(crap_rec.extent_len));

	s->system_group =
		initialize_alloc_group(s, "system inode group", tmprec,
				       crap_rec.extent_off >> s->blocksize_bits,
				       0,
                                       crap_rec.extent_len >> s->cluster_size_bits,
				       s->cluster_size / s->blocksize);

	tmprec->group = s->system_group;
	tmprec->chain_off =
		tmprec->group->gd->bg_blkno << s->blocksize_bits;

	if (!s->quiet)
		printf("done\n");

	if (!s->quiet)
		printf("Writing superblock: ");

	superblock_rec.fe_off = (uint64_t)OCFS2_SUPER_BLOCK_BLKNO << s->blocksize_bits;

	alloc_from_bitmap (s, 1, s->global_bm,
			   &root_dir_rec.extent_off,
			   &root_dir_rec.extent_len);

	root_dir_rec.fe_off = alloc_inode(s, &root_dir_rec.suballoc_bit);
	root_dir->record = &root_dir_rec;

	add_entry_to_directory(s, root_dir, ".", root_dir_rec.fe_off, OCFS2_FT_DIR);
	add_entry_to_directory(s, root_dir, "..", root_dir_rec.fe_off, OCFS2_FT_DIR);

	need = system_dir_blocks_needed(s);
	alloc_from_bitmap (s, need, s->global_bm, &system_dir_rec.extent_off, &system_dir_rec.extent_len);
	system_dir_rec.fe_off = alloc_inode(s, &system_dir_rec.suballoc_bit);
	system_dir->record = &system_dir_rec;
	add_entry_to_directory(s, system_dir, ".", system_dir_rec.fe_off, OCFS2_FT_DIR);
	add_entry_to_directory(s, system_dir, "..", system_dir_rec.fe_off, OCFS2_FT_DIR);

	for (i = 0; i < NUM_SYSTEM_INODES; i++) {
		num = (system_files[i].global) ? 1 : s->initial_nodes;

		for (j = 0; j < num; j++) {
			record[i][j].fe_off = alloc_inode(s, &(record[i][j].suballoc_bit));
			sprintf(fname, system_files[i].name, j);
			add_entry_to_directory(s, system_dir, fname,
					       record[i][j].fe_off,
					       system_files[i].dir ? OCFS2_FT_DIR
							           : OCFS2_FT_REG_FILE);
		}
	}

	/* back when we initialized the alloc group we hadn't allocated
	 * an inode for the global allocator yet */
	tmprec = &(record[GLOBAL_INODE_ALLOC_SYSTEM_INODE][0]);
	s->system_group->gd->bg_parent_dinode = 
		cpu_to_le64(tmprec->fe_off >> s->blocksize_bits);

	tmprec = &(record[DLM_SYSTEM_INODE][0]);
	need = (AUTOCONF_BLOCKS(s->initial_nodes, 32) +
		PUBLISH_BLOCKS(s->initial_nodes, 32) +
		VOTE_BLOCKS(s->initial_nodes, 32));
	alloc_from_bitmap(s, need, s->global_bm, &tmprec->extent_off, &tmprec->extent_len);
	tmprec->file_size = need << s->blocksize_bits;

	tmprec = &record[ORPHAN_DIR_SYSTEM_INODE][0];
	orphan_dir->record = tmprec;
	alloc_from_bitmap(s, 1, s->global_bm, &tmprec->extent_off, &tmprec->extent_len);
	add_entry_to_directory(s, orphan_dir, ".", tmprec->fe_off, OCFS2_FT_DIR);
	add_entry_to_directory(s, orphan_dir, "..", system_dir_rec.fe_off, OCFS2_FT_DIR);

	tmprec = s->global_bm->bm_record;
	alloc_bytes_from_bitmap(s, tmprec->extent_len, s->global_bm,
				&(tmprec->extent_off), &allocated);

	format_leading_space(s);
	format_superblock(s, &superblock_rec, &root_dir_rec, &system_dir_rec);

	if (!s->quiet)
		printf("done\n");

	if (!s->quiet)
		printf("Writing system files: ");

	format_file(s, &root_dir_rec);
	format_file(s, &system_dir_rec);

	for (i = 0; i < NUM_SYSTEM_INODES; i++) {
		num = system_files[i].global ? 1 : s->initial_nodes;
		for (j = 0; j < num; j++) {
			tmprec = &(record[i][j]);
			if (system_files[i].type == SFI_JOURNAL) {
				alloc_bytes_from_bitmap(s, s->journal_size_in_bytes,
							s->global_bm,
							&(tmprec->extent_off),
							&(tmprec->extent_len));
				replacement_journal_create(s, tmprec->extent_off);
				tmprec->file_size = tmprec->extent_len;
			}

			format_file(s, tmprec);
		}
	}

	write_bitmap_data(s, s->global_bm);
	write_group_data(s, s->system_group);

	write_directory_data(s, root_dir);
	write_directory_data(s, system_dir);
	write_directory_data(s, orphan_dir);

	if (!s->quiet)
		printf("done\n");

	if (!s->quiet)
		printf("Writing autoconfig header: ");

	write_autoconfig_header(s, &record[DLM_SYSTEM_INODE][0]);

	if (!s->quiet)
		printf("done\n");

	close_device(s);

	if (!s->quiet)
		printf("%s successful\n\n", s->progname);

	return 0;
}

static State *
get_state(int argc, char **argv)
{
	char *progname;
	unsigned int blocksize = 0;
	unsigned int cluster_size = 0;
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
	uint64_t max_journal_size = 500 * ONE_MEGA_BYTE;

	static struct option long_options[] = {
		{ "blocksize", 1, 0, 'b' },
		{ "clustersize", 1, 0, 'c' },
		{ "label", 1, 0, 'L' },
		{ "nodes", 1, 0, 'n' },
		{ "verbose", 0, 0, 'v' },
		{ "quiet", 0, 0, 'q' },
		{ "version", 0, 0, 'V' },
		{ "journalsize", 0, 0, 'j'},
		{ 0, 0, 0, 0}
	};

	if (argc && *argv)
		progname = basename(argv[0]);
	else
		progname = strdup("mkfs.ocfs2");

	while (1) {
		c = getopt_long(argc, argv, "b:c:L:n:j:vqV", long_options, 
				NULL);

		if (c == -1)
			break;

		switch (c) {
		case 'b':
			ret = get_number(optarg, &val);

			if (ret ||
			    val < OCFS2_MIN_BLOCKSIZE ||
			    val > OCFS2_MAX_BLOCKSIZE) {
				com_err(progname, 0,
					"Invalid blocksize %s: "
					"must be between %d and %d bytes",
					optarg,
					OCFS2_MIN_BLOCKSIZE,
					OCFS2_MAX_BLOCKSIZE);
				exit(1);
			}

			blocksize = (unsigned int) val;
			break;

		case 'c':
			ret = get_number(optarg, &val);

			if (ret ||
			    val < MIN_CLUSTER_SIZE ||
			    val > MAX_CLUSTER_SIZE) {
				com_err(progname, 0,
					"Invalid cluster size %s: "
					"must be between %d and %d bytes",
					optarg,
					MIN_CLUSTER_SIZE,
					MAX_CLUSTER_SIZE);
				exit(1);
			}

			cluster_size = (unsigned int) val;
			break;

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
	optind++;

	if (optind < argc) {
		unsigned long val;

		val = strtoul(argv[optind], &dummy, 0);

		if ((*dummy) || (val > 0xffffffffUL)) {
			com_err(progname, 0, "Block count bad - %s",
				argv[optind]);
			exit(1);
		}

		optind++;
	}

	if (optind < argc)
		usage(progname);

	if (!quiet || show_version)
		version(progname);

	if (show_version)
		exit(0);

	s = malloc(sizeof(State));
	memset(s, 0, sizeof(State));

	s->progname      = progname;

	s->verbose       = verbose;
	s->quiet         = quiet;

	s->blocksize     = blocksize;
	s->cluster_size  = cluster_size;
	s->vol_label     = vol_label;
	s->initial_nodes = initial_nodes;

	s->device_name   = strdup(device_name);

	s->fd            = -1;

	s->format_time   = time(NULL);

	s->journal_size_in_bytes = journal_size_in_bytes;

	return s;
}

static void
usage(const char *progname)
{
	fprintf(stderr, "Usage: %s [-b blocksize] [-c cluster-size] [-L volume-label]\n"
			"\t[-n number-of-nodes] [-j journal-size] [-qvV] device [blocks-count]\n",
			progname);
	exit(0);
}

static void
version(const char *progname)
{
	fprintf(stderr, "%s %s\n", progname, VERSION);
}
