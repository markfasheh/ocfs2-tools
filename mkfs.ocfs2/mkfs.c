/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * mkfs2.c
 *
 * OCFS2 format utility
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
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

#define _LARGEFILE64_SOURCE

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <errno.h>
#include <malloc.h>
#include <time.h>
#include <libgen.h>
#include <netinet/in.h>
#include <inttypes.h>

#include "ocfs2.h"
#include "bitops.h"

/* jfs_compat.h defines these */
#undef cpu_to_be32
#undef be32_to_cpu

#include "ocfs2_disk_dlm.h"
#include "ocfs1_fs_compat.h"

typedef unsigned short kdev_t;

#include <signal.h>
#include <libgen.h>

#include "kernel-jbd.h"


#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#define BITCOUNT(x)     (((BX_(x)+(BX_(x)>>4)) & 0x0F0F0F0F) % 255)
#define BX_(x)          ((x) - (((x)>>1)&0x77777777) \
			     - (((x)>>2)&0x33333333) \
			     - (((x)>>3)&0x11111111))

#define MIN_RESERVED_TAIL_BLOCKS    8

#define LEADING_SPACE_BLOCKS    2
#define SLOP_BLOCKS             0
#define FILE_ENTRY_BLOCKS       8
#define SUPERBLOCK_BLOCKS       1
#define PUBLISH_BLOCKS(i,min)   (i<min ? min : i)
#define VOTE_BLOCKS(i,min)      (i<min ? min : i)
#define AUTOCONF_BLOCKS(i,min)  ((2+4) + (i<min ? min : i))
#define NUM_LOCAL_SYSTEM_FILES  6

#define OCFS2_OS_LINUX           0
#define OCFS2_OS_HURD            1
#define OCFS2_OS_MASIX           2
#define OCFS2_OS_FREEBSD         3
#define OCFS2_OS_LITES           4

#define OCFS2_DFL_MAX_MNT_COUNT          20
#define OCFS2_DFL_CHECKINTERVAL          0

#define SYSTEM_FILE_NAME_MAX   40

#define ONE_GB_SHIFT           30

#define BITMAP_WARNING_LEN     1572864
#define BITMAP_AUTO_MAX        786432

#define MAX_CLUSTER_SIZE       1048576
#define MIN_CLUSTER_SIZE       4096
#define AUTO_CLUSTER_SIZE      65536


enum {
	SFI_JOURNAL,
	SFI_BITMAP,
	SFI_LOCAL_ALLOC,
	SFI_DLM,
	SFI_CHAIN,
	SFI_OTHER
};


typedef struct _SystemFileInfo SystemFileInfo;

struct _SystemFileInfo {
	char *name;
	int type;
	int global;
	int dir;
};

struct BitInfo {
	uint32_t used_bits;
	uint32_t total_bits;
};

typedef struct _AllocGroup AllocGroup;
typedef struct _SystemFileDiskRecord SystemFileDiskRecord;

struct _AllocGroup {
	char *name;
	ocfs2_group_desc *gd;
	SystemFileDiskRecord *alloc_inode;
};


struct _SystemFileDiskRecord {
	uint64_t fe_off;
        uint16_t suballoc_bit;
	uint64_t extent_off;
	uint64_t extent_len;
	uint64_t file_size;

	uint64_t chain_off;
	AllocGroup *group;

	struct BitInfo bi;

	int flags;
	int links;
	int dir;
};

typedef struct _AllocBitmap AllocBitmap;

struct _AllocBitmap {
	void *buf;

	uint32_t valid_bits;
	uint32_t unit;
	uint32_t unit_bits;

	char *name;

	uint64_t fe_disk_off;

	SystemFileDiskRecord *bm_record;
	SystemFileDiskRecord *alloc_record;
};

typedef struct _DirData DirData;

struct _DirData {
	uint64_t disk_off;
	uint64_t disk_len;

	void *buf;
	int buf_len;

	int last_off;
	uint64_t fe_disk_off;

	int link_count;

	SystemFileDiskRecord *record;
};

typedef struct _State State;

struct _State {
  	char *progname;

	int verbose;
	int quiet;

	uint32_t blocksize;
	uint32_t blocksize_bits;

	uint32_t cluster_size;
	uint32_t cluster_size_bits;

	uint64_t volume_size_in_bytes;
	uint32_t volume_size_in_clusters;
	uint64_t volume_size_in_blocks;

	uint32_t pagesize_bits;

	uint64_t reserved_tail_size;

	unsigned int initial_nodes;

	char *vol_label;
	char *device_name;
	char *uuid;
	uint32_t vol_generation;

	int fd;

	time_t format_time;

	AllocBitmap *global_bm;
	AllocGroup *system_group;
};


static State *get_state(int argc, char **argv);
static int get_number(char *arg, uint64_t *res);
static void usage(const char *progname);
static void version(const char *progname);
static void fill_defaults(State *s);
static int get_bits(State *s, int num);
static void *do_malloc(State *s, size_t size);
static void do_pwrite(State *s, const void *buf, size_t count, uint64_t offset);static AllocBitmap *initialize_bitmap(State *s, uint32_t bits,
				      uint32_t unit_bits, const char *name,
				      SystemFileDiskRecord *bm_record,
				      SystemFileDiskRecord *alloc_record);
static int find_clear_bits(AllocBitmap *bitmap, uint32_t num_bits,
			   uint32_t offset);
static int alloc_bytes_from_bitmap(State *s, uint64_t bytes,
				   AllocBitmap *bitmap, uint64_t *start,
				   uint64_t *num);
static int alloc_from_bitmap(State *s, uint64_t num_bits, AllocBitmap *bitmap,
			     uint64_t *start, uint64_t *num);
static uint64_t alloc_inode(State *s, uint16_t *suballoc_bit);
static DirData *alloc_directory(State *s);
static void add_entry_to_directory(State *s, DirData *dir, char *name,
				   uint64_t byte_off, uint8_t type);
static uint32_t blocks_needed(State *s);
static uint32_t system_dir_blocks_needed(State *s);
static void adjust_volume_size(State *s);
static void format_superblock(State *s, SystemFileDiskRecord *rec,
			      SystemFileDiskRecord *root_rec,
			      SystemFileDiskRecord *sys_rec);
static void format_file(State *s, SystemFileDiskRecord *rec);
static void write_metadata(State *s, SystemFileDiskRecord *rec, void *src);
static void write_bitmap_data(State *s, AllocBitmap *bitmap);
static void write_directory_data(State *s, DirData *dir);
static void write_group_data(State *s, AllocGroup *group);
static void format_leading_space(State *s);
static void replacement_journal_create(State *s, uint64_t journal_off);
static void open_device(State *s);
static void close_device(State *s);
static int initial_nodes_for_volume(uint64_t size);
static void generate_uuid(State *s);
static void create_generation(State *s);
static void write_autoconfig_header(State *s, SystemFileDiskRecord *rec);
static void init_record(State *s, SystemFileDiskRecord *rec, int type, int dir);
static void print_state(State *s);
static int ocfs2_clusters_per_group(int block_size,
				    int cluster_size_bits);
static AllocGroup * initialize_alloc_group(State *s, char *name,
					   SystemFileDiskRecord *alloc_inode,
					   uint64_t blkno,
					   uint16_t chain, uint16_t cpg,
					   uint16_t bpc);

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
				alloc_bytes_from_bitmap(s, OCFS2_DEFAULT_JOURNAL_SIZE,
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

	static struct option long_options[] = {
		{ "blocksize", 1, 0, 'b' },
		{ "clustersize", 1, 0, 'c' },
		{ "label", 1, 0, 'L' },
		{ "nodes", 1, 0, 'n' },
		{ "verbose", 0, 0, 'v' },
		{ "quiet", 0, 0, 'q' },
		{ "version", 0, 0, 'V' },
		{ 0, 0, 0, 0}
	};

	if (argc && *argv)
		progname = basename(argv[0]);
	else
		progname = strdup("mkfs.ocfs2");

	while (1) {
		c = getopt_long(argc, argv, "b:c:L:n:vqV", long_options, NULL);

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

	return s;
}

static int
get_number(char *arg, uint64_t *res)
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

static void
usage(const char *progname)
{
	fprintf(stderr, "Usage: %s [-b blocksize] [-c cluster-size] [-L volume-label]\n"
			"\t[-n number-of-nodes] [-qvV] device [blocks-count]\n",
			progname);
	exit(0);
}

static void
version(const char *progname)
{
	fprintf(stderr, "%s %s\n", progname, VERSION);
}

static void
fill_defaults(State *s)
{
	size_t pagesize;
	errcode_t err;
	uint32_t ret;

	pagesize = getpagesize();

	s->pagesize_bits = get_bits(s, pagesize);

	if (!s->blocksize)
		s->blocksize = 4096;

	if (!s->volume_size_in_blocks) {
		err = ocfs2_get_device_size(s->device_name, s->blocksize, &ret);
		s->volume_size_in_blocks = ret;
	}

	s->volume_size_in_bytes = s->volume_size_in_blocks * s->blocksize;

	if (!s->blocksize) {
		if (s->volume_size_in_bytes <= 1024 * 1024 * 3) {
			s->blocksize = 512;
		} else {
			int shift = 30;

			while (s->blocksize > 1024) {
				if (s->volume_size_in_bytes >= 1U << shift)
					break;
				s->blocksize >>= 1;
				shift--;
			}
		}

		err = ocfs2_get_device_size(s->device_name, s->blocksize, &ret);
		s->volume_size_in_blocks = ret;

		s->volume_size_in_bytes =
			s->volume_size_in_blocks * s->blocksize;
	}

	s->blocksize_bits = get_bits(s, s->blocksize);

	if (!s->cluster_size) {
		uint32_t volume_size, cluster_size, cluster_size_bits, need;

		for (cluster_size = MIN_CLUSTER_SIZE;
		     cluster_size < AUTO_CLUSTER_SIZE;
		     cluster_size <<= 1) {
			cluster_size_bits = get_bits(s, cluster_size);

			volume_size =
				s->volume_size_in_bytes >> cluster_size_bits;

			need = (volume_size + 7) >> 3;
			need = ((need + cluster_size - 1) >>
				cluster_size_bits) << cluster_size_bits;

			if (need <= BITMAP_AUTO_MAX) 
				break;
		}

		s->cluster_size = cluster_size;
	}

	s->cluster_size_bits = get_bits(s, s->cluster_size);

	s->volume_size_in_clusters = s->volume_size_in_bytes >> s->cluster_size_bits;
	s->volume_size_in_blocks = (s->volume_size_in_clusters << s->cluster_size_bits) >> s->blocksize_bits;
	
	s->reserved_tail_size = 0;

	if (!s->initial_nodes) {
		s->initial_nodes =
			initial_nodes_for_volume(s->volume_size_in_bytes);
	}

	if (!s->vol_label) {
		  s->vol_label = strdup("");
	}
}

static int
get_bits(State *s, int num)
{
	int i, bits = 0;

	for (i = 32; i >= 0; i--) {
		if (num == (1U << i))
			bits = i;
	}

	if (bits == 0) {
		com_err(s->progname, 0,
			"Could not get bits for number %d", num);
		exit(1);
	}

	return bits;
}

static void *
do_malloc(State *s, size_t size)
{
	void *buf;

	buf = malloc(size);

	if (buf == NULL) {
		com_err(s->progname, 0,
			"Could not allocate %lu bytes of memory",
			(unsigned long)size);
		exit(1);
	}

	return buf;
}

static void
do_pwrite(State *s, const void *buf, size_t count, uint64_t offset)
{
	ssize_t ret;

	ret = pwrite64(s->fd, buf, count, offset);

	if (ret == -1) {
		com_err(s->progname, 0, "Could not write: %s",
			strerror(errno));
		exit(1);
	}
}

static AllocGroup *
initialize_alloc_group(State *s, char *name,
		       SystemFileDiskRecord *alloc_inode,
		       uint64_t blkno, uint16_t chain,
		       uint16_t cpg, uint16_t bpc)
{
	AllocGroup *group;

	group = do_malloc(s, sizeof(AllocGroup));
	memset(group, 0, sizeof(AllocGroup));

	group->gd = do_malloc(s, s->blocksize);
	memset(group->gd, 0, s->blocksize);

	strcpy(group->gd->bg_signature, OCFS2_GROUP_DESC_SIGNATURE);
	group->gd->bg_generation = cpu_to_le32(s->vol_generation);
	group->gd->bg_size = (uint32_t)ocfs2_group_bitmap_size(s->blocksize);
	group->gd->bg_bits = cpg * bpc;
	group->gd->bg_chain = chain;
	group->gd->bg_parent_dinode = alloc_inode->fe_off;
	group->gd->bg_blkno = blkno;

	/* First bit set to account for the descriptor block */
	ocfs2_set_bit(0, group->gd->bg_bitmap);
	group->gd->bg_free_bits_count = group->gd->bg_bits - 1;

	alloc_inode->bi.total_bits = group->gd->bg_bits;
	alloc_inode->bi.used_bits = alloc_inode->bi.total_bits -
		group->gd->bg_free_bits_count;
	group->alloc_inode = alloc_inode;

	group->name = strdup(name);

	return group;
}

static AllocBitmap *
initialize_bitmap(State *s, uint32_t bits, uint32_t unit_bits,
		  const char *name, SystemFileDiskRecord *bm_record,
		  SystemFileDiskRecord *alloc_record)
{
	AllocBitmap *bitmap;
	uint64_t bitmap_len = bm_record->extent_len;

	bitmap = do_malloc(s, sizeof(AllocBitmap));
	memset(bitmap, 0, sizeof(AllocBitmap));

	bitmap->buf = memalign(s->blocksize, bitmap_len);
	memset(bitmap->buf, 0, bitmap_len);

	bitmap->valid_bits = bits;
	bitmap->unit_bits = unit_bits;
	bitmap->unit = 1 << unit_bits;
	bitmap->name = strdup(name);

	bm_record->file_size = bitmap_len;
	bm_record->fe_off = 0ULL;

	bm_record->bi.used_bits = 0;
	bm_record->bi.total_bits = bits;

	alloc_record->file_size = bits << unit_bits;
	alloc_record->fe_off = 0ULL;

	bitmap->bm_record = bm_record;
	bitmap->alloc_record = alloc_record;

	return bitmap;
}

#if 0
static void
destroy_bitmap(AllocBitmap *bitmap)
{
	free(bitmap->buf);
	free(bitmap);
}
#endif

static int
find_clear_bits(AllocBitmap *bitmap, uint32_t num_bits, uint32_t offset)
{
	uint32_t next_zero, off, count = 0, size, first_zero = -1;
	void *buf;

	buf = bitmap->buf;
	size = bitmap->valid_bits;
	off = offset;

	while ((size - off + count >= num_bits) &&
	       (next_zero = ocfs2_find_next_bit_clear(buf, size, off)) != size) {
		if (next_zero >= bitmap->valid_bits)
			break;

		if (next_zero != off) {
			first_zero = next_zero;
			off = next_zero + 1;
			count = 0;
		} else {
			off++;
			if (count == 0)
				first_zero = next_zero;
		}

		count++;

		if (count == num_bits)
			goto bail;
	}

	first_zero = -1;

bail:
	if (first_zero != (uint32_t)-1 && first_zero > bitmap->valid_bits) {
		fprintf(stderr, "erf... first_zero > bitmap->valid_bits "
				"(%d > %d)", first_zero, bitmap->valid_bits);
		first_zero = -1;
	}

	return first_zero;
}

static int
alloc_bytes_from_bitmap(State *s, uint64_t bytes, AllocBitmap *bitmap,
			uint64_t *start, uint64_t *num)
{
	uint32_t num_bits = 0;

	num_bits = (bytes + bitmap->unit - 1) >> bitmap->unit_bits;

	return alloc_from_bitmap(s, num_bits, bitmap, start, num);
}

static int
alloc_from_bitmap(State *s, uint64_t num_bits, AllocBitmap *bitmap,
		  uint64_t *start, uint64_t *num)
{
	uint32_t start_bit = 0;
	void *buf;

	start_bit = find_clear_bits(bitmap, num_bits, 0);

	if (start_bit == (uint32_t)-1) {
		com_err(s->progname, 0,
			"Could not allocate %"PRIu64" bits from %s bitmap",
			num_bits, bitmap->name);
		exit(1);
	}

	*start = ((uint64_t)start_bit) << bitmap->unit_bits;
	*num = ((uint64_t)num_bits) << bitmap->unit_bits;

	buf = do_malloc(s, *num);
	memset(buf, 0, *num);

	do_pwrite(s, buf, *num, *start);

	bitmap->bm_record->bi.used_bits += num_bits;

	while (num_bits--) {
		ocfs2_set_bit(start_bit, bitmap->buf);
		start_bit++;
	}

	free(buf);

	return 0;
}

static int alloc_from_group(State *s, uint16_t count,
			    AllocGroup *group, uint64_t *start_blkno,
			    uint16_t *num_bits)
{
	uint16_t start_bit, end_bit;

	start_bit = ocfs2_find_first_bit_clear(group->gd->bg_bitmap,
					       group->gd->bg_bits);

	while (start_bit < group->gd->bg_bits) {
		end_bit = ocfs2_find_next_bit_set(group->gd->bg_bitmap,
						  group->gd->bg_bits,
						  start_bit);
		if ((end_bit - start_bit) >= count) {
			*num_bits = count;
			for (*num_bits = 0; *num_bits < count; *num_bits += 1) {
				ocfs2_set_bit(start_bit + *num_bits,
					      group->gd->bg_bitmap);
			}
			group->gd->bg_free_bits_count -= *num_bits;
			group->alloc_inode->bi.used_bits += *num_bits;
			*start_blkno = group->gd->bg_blkno + start_bit;
			return 0;
		}
		start_bit = end_bit;
	}

	com_err(s->progname, 0,
		"Could not allocate %"PRIu16" bits from %s alloc group",
		count, group->name);
	exit(1);

	return 1;
}

static uint64_t
alloc_inode(State *s, uint16_t *suballoc_bit)
{
	uint64_t ret;
	uint16_t num;

	alloc_from_group(s, 1, s->system_group,
			 &ret, &num);

        *suballoc_bit = (int)(ret - s->system_group->gd->bg_blkno);

        /* Did I mention I hate this code? */
	return (ret << s->blocksize_bits);
}

static DirData *
alloc_directory(State *s)
{
	DirData *dir;

	dir = do_malloc(s, sizeof(DirData));
	memset(dir, 0, sizeof(DirData));

	return dir;
}

static void
add_entry_to_directory(State *s, DirData *dir, char *name, uint64_t byte_off,
		       uint8_t type)
{
	struct ocfs2_dir_entry *de, *de1;
	int new_rec_len;
	void *new_buf, *p;
	int new_size, rec_len, real_len;

	new_rec_len = OCFS2_DIR_REC_LEN(strlen(name));

	if (dir->buf) {
		de = (struct ocfs2_dir_entry *)(dir->buf + dir->last_off);
		rec_len = le16_to_cpu(de->rec_len);
		real_len = OCFS2_DIR_REC_LEN(de->name_len);

		if ((le64_to_cpu(de->inode) == 0 && rec_len >= new_rec_len) ||
		    (rec_len >= real_len + new_rec_len)) {
			if (le64_to_cpu(de->inode)) {
				de1 =(struct ocfs2_dir_entry *) ((char *) de + real_len);
				de1->rec_len = cpu_to_le16(le16_to_cpu(de->rec_len) - real_len);
				de->rec_len = cpu_to_le16(real_len);
				de = de1;
			}

			goto got_it;
		}

		new_size = dir->record->file_size + s->blocksize;
	} else {
		new_size = s->blocksize;
	}

	new_buf = memalign(s->blocksize, new_size);

	if (new_buf == NULL) {
		com_err(s->progname, 0, "Failed to grow directory");
		exit(1);
	}

	if (dir->buf) {
		memcpy(new_buf, dir->buf, dir->record->file_size);
		free(dir->buf);
		p = new_buf + dir->record->file_size;
		memset(p, 0, s->blocksize);
	} else {
		p = new_buf;
		memset(new_buf, 0, new_size);
	}

	dir->buf = new_buf;
	dir->record->file_size = new_size;

	de = (struct ocfs2_dir_entry *)p;
	de->inode = 0;
	de->rec_len = cpu_to_le16(s->blocksize);

got_it:
	de->name_len = strlen(name);

	de->inode = cpu_to_le64(byte_off >> s->blocksize_bits);

	de->file_type = type;

        strcpy(de->name, name);

        dir->last_off = ((char *)de - (char *)dir->buf);

        if (type == OCFS2_FT_DIR)
                dir->record->links++;
}

static uint32_t
blocks_needed(State *s)
{
	uint32_t num;

	num = LEADING_SPACE_BLOCKS;
	num += SUPERBLOCK_BLOCKS;
	num += FILE_ENTRY_BLOCKS;
	num += AUTOCONF_BLOCKS(s->initial_nodes, 32);
	num += PUBLISH_BLOCKS(s->initial_nodes, 32);
	num += VOTE_BLOCKS(s->initial_nodes, 32);
	num += (s->initial_nodes * NUM_LOCAL_SYSTEM_FILES);
	num += SLOP_BLOCKS;

	return num;
}

static uint32_t
system_dir_blocks_needed(State *s)
{
	int bytes_needed = 0;
	int each = OCFS2_DIR_REC_LEN(SYSTEM_FILE_NAME_MAX);
	int entries_per_block = s->blocksize / each;

	bytes_needed = (blocks_needed(s) + entries_per_block -
			1 / entries_per_block) << s->blocksize_bits;

	return (bytes_needed + s->cluster_size - 1) >> s->cluster_size_bits;
}

static void
adjust_volume_size(State *s)
{
	uint32_t max;
	uint64_t vsize = s->volume_size_in_bytes -
		(MIN_RESERVED_TAIL_BLOCKS << s->blocksize_bits);

	max = MAX(s->pagesize_bits, s->blocksize_bits);
	max = MAX(max, s->cluster_size_bits);

	vsize >>= max;
	vsize <<= max;

	s->volume_size_in_blocks = vsize >> s->blocksize_bits;
	s->volume_size_in_clusters = vsize >> s->cluster_size_bits;
	s->reserved_tail_size = s->volume_size_in_bytes - vsize;
	s->volume_size_in_bytes = vsize;
}

static void
format_superblock(State *s, SystemFileDiskRecord *rec,
		  SystemFileDiskRecord *root_rec, SystemFileDiskRecord *sys_rec)
{
	ocfs2_dinode *di;
	uint64_t super_off = rec->fe_off;

	di = do_malloc(s, s->blocksize);
	memset(di, 0, s->blocksize);

	strcpy(di->i_signature, OCFS2_SUPER_BLOCK_SIGNATURE);
	di->i_suballoc_node = cpu_to_le16((__u16)-1);
	di->i_suballoc_bit = cpu_to_le16((__u16)-1);

	di->i_atime = 0;
	di->i_ctime = cpu_to_le64(s->format_time);
	di->i_mtime = cpu_to_le64(s->format_time);
	di->i_blkno = cpu_to_le64(super_off >> s->blocksize_bits);
	di->i_flags = cpu_to_le32(OCFS2_VALID_FL | OCFS2_SYSTEM_FL |
				  OCFS2_SUPER_BLOCK_FL);
	di->i_clusters = s->volume_size_in_clusters;
	di->id2.i_super.s_major_rev_level = cpu_to_le16(OCFS2_MAJOR_REV_LEVEL);
	di->id2.i_super.s_minor_rev_level = cpu_to_le16(OCFS2_MINOR_REV_LEVEL);
	di->id2.i_super.s_root_blkno = cpu_to_le64(root_rec->fe_off >> s->blocksize_bits);
	di->id2.i_super.s_system_dir_blkno = cpu_to_le64(sys_rec->fe_off >> s->blocksize_bits);
	di->id2.i_super.s_mnt_count = 0;
	di->id2.i_super.s_max_mnt_count = cpu_to_le16(OCFS2_DFL_MAX_MNT_COUNT);
	di->id2.i_super.s_state = 0;
	di->id2.i_super.s_errors = 0;
	di->id2.i_super.s_lastcheck = cpu_to_le64(s->format_time);
	di->id2.i_super.s_checkinterval = cpu_to_le32(OCFS2_DFL_CHECKINTERVAL);
	di->id2.i_super.s_creator_os = cpu_to_le32(OCFS2_OS_LINUX);
	di->id2.i_super.s_blocksize_bits = cpu_to_le32(s->blocksize_bits);
	di->id2.i_super.s_clustersize_bits = cpu_to_le32(s->cluster_size_bits);
	di->id2.i_super.s_max_nodes = cpu_to_le32(s->initial_nodes);

	strcpy(di->id2.i_super.s_label, s->vol_label);
	memcpy(di->id2.i_super.s_uuid, s->uuid, 16);

	do_pwrite(s, di, s->blocksize, super_off);
	free(di);
}

static int 
ocfs2_clusters_per_group(int block_size, int cluster_size_bits)
{
	int bytes;

	switch (block_size) {
	case (4096):
	case (2048):
		bytes = 4 * ONE_MEGA_BYTE;
		break;
	case (1024):
		bytes = 2 * ONE_MEGA_BYTE;
		break;
	case (512):
	default:
		bytes = ONE_MEGA_BYTE;
		break;
	}

	return(bytes >> cluster_size_bits);
}

static void
format_file(State *s, SystemFileDiskRecord *rec)
{
	ocfs2_dinode *di;
	int mode;
	uint32_t clusters;

	mode = rec->dir ? 0755 | S_IFDIR : 0644 | S_IFREG;

	clusters = (rec->extent_len + s->cluster_size - 1) >> s->cluster_size_bits;

	di = do_malloc(s, s->blocksize);
	memset(di, 0, s->blocksize);

	strcpy(di->i_signature, OCFS2_INODE_SIGNATURE);
	di->i_generation = cpu_to_le32(s->vol_generation);
	di->i_suballoc_node = cpu_to_le16(-1);
        di->i_suballoc_bit = cpu_to_le16(rec->suballoc_bit);
	di->i_blkno = cpu_to_le64(rec->fe_off >> s->blocksize_bits);
	di->i_uid = 0;
	di->i_gid = 0;
	di->i_size = cpu_to_le64(rec->file_size);
	di->i_mode = cpu_to_le16(mode);
	di->i_links_count = cpu_to_le16(rec->links);
	di->i_flags = cpu_to_le32(rec->flags);
	di->i_atime = di->i_ctime = di->i_mtime = cpu_to_le64(s->format_time);
	di->i_dtime = 0;
	di->i_clusters = cpu_to_le32(clusters);

	if (rec->flags & OCFS2_LOCAL_ALLOC_FL) {
		di->id2.i_lab.la_size =
			cpu_to_le16(ocfs2_local_alloc_size(s->blocksize));
		goto write_out;
	}

	if (rec->flags & OCFS2_BITMAP_FL) {
		di->id1.bitmap1.i_used = cpu_to_le32(rec->bi.used_bits);
		di->id1.bitmap1.i_total = cpu_to_le32(rec->bi.total_bits);
	}

	if (rec->flags & OCFS2_CHAIN_FL) {
		di->id2.i_chain.cl_count = 
			cpu_to_le16(ocfs2_chain_recs_per_inode(s->blocksize));
		di->id2.i_chain.cl_cpg = 
			cpu_to_le16(ocfs2_clusters_per_group(s->blocksize, 
						 s->cluster_size_bits));
		di->id2.i_chain.cl_bpc = 
			cpu_to_le16(s->cluster_size / s->blocksize);
		di->id2.i_chain.cl_next_free_rec = 0;

		if (rec->chain_off) {
			di->id2.i_chain.cl_next_free_rec =
				cpu_to_le16(1);
			di->id2.i_chain.cl_recs[0].c_free =
				cpu_to_le16(rec->group->gd->bg_free_bits_count);
			di->id2.i_chain.cl_recs[0].c_total =
				cpu_to_le16(rec->group->gd->bg_bits);
			di->id2.i_chain.cl_recs[0].c_blkno =
				cpu_to_le64(rec->chain_off >> s->blocksize_bits);
                        di->id2.i_chain.cl_cpg =
                            cpu_to_le16(rec->group->gd->bg_bits /
                                        le16_to_cpu(di->id2.i_chain.cl_bpc));
			di->i_clusters =
                            cpu_to_le64(di->id2.i_chain.cl_cpg);
			di->i_size =
                            cpu_to_le64(di->i_clusters << s->cluster_size_bits);
		}
		goto write_out;
	}
	di->id2.i_list.l_count =
		cpu_to_le16(ocfs2_extent_recs_per_inode(s->blocksize));
	di->id2.i_list.l_next_free_rec = 0;
	di->id2.i_list.l_tree_depth = 0;

	if (rec->extent_len) {
		di->id2.i_list.l_next_free_rec = cpu_to_le16(1);
		di->id2.i_list.l_recs[0].e_cpos = 0;
		di->id2.i_list.l_recs[0].e_clusters = cpu_to_le32(clusters);
		di->id2.i_list.l_recs[0].e_blkno =
			cpu_to_le64(rec->extent_off >> s->blocksize_bits);
	}

write_out:
	do_pwrite(s, di, s->blocksize, rec->fe_off);
	free(di);
}

static void
write_metadata(State *s, SystemFileDiskRecord *rec, void *src)
{
	void *buf;

	buf = do_malloc(s, rec->extent_len);
	memset(buf, 0, rec->extent_len);

	memcpy(buf, src, rec->file_size);

	do_pwrite(s, buf, rec->extent_len, rec->extent_off);

	free(buf);
}

static void
write_bitmap_data(State *s, AllocBitmap *bitmap)
{
	write_metadata(s, bitmap->bm_record, bitmap->buf);
}

static void
write_group_data(State *s, AllocGroup *group)
{
	do_pwrite(s, group->gd, s->blocksize,
		  group->gd->bg_blkno << s->blocksize_bits);
}

static void
write_directory_data(State *s, DirData *dir)
{
	write_metadata(s, dir->record, dir->buf);
}

static void
format_leading_space(State *s)
{
	int num_blocks = 2, size;
	ocfs1_vol_disk_hdr *hdr;
	ocfs1_vol_label *lbl;
	void *buf;
	char *p;

	size = num_blocks << s->blocksize_bits;

	p = buf = do_malloc(s, size);
	memset(buf, 2, size);

	hdr = buf;
	strcpy(hdr->signature, "this is an ocfs2 volume");
	strcpy(hdr->mount_point, "this is an ocfs2 volume");

	p += 512;
	lbl = (ocfs1_vol_label *)p;
	strcpy(lbl->label, "this is an ocfs2 volume");
	strcpy(lbl->cluster_name, "this is an ocfs2 volume");

	do_pwrite(s, buf, size, 0);
	free(buf);
}

static void
replacement_journal_create(State *s, uint64_t journal_off)
{
	journal_superblock_t *sb;
	void *buf;

	buf = do_malloc(s, OCFS2_DEFAULT_JOURNAL_SIZE);
	memset(buf, 0, OCFS2_DEFAULT_JOURNAL_SIZE);

	sb = buf;

	sb->s_header.h_magic     = htonl(JFS_MAGIC_NUMBER);
	sb->s_header.h_blocktype = htonl(JFS_SUPERBLOCK_V2);

	sb->s_blocksize = cpu_to_be32(s->blocksize);
	sb->s_maxlen =
		cpu_to_be32(OCFS2_DEFAULT_JOURNAL_SIZE >> s->blocksize_bits);

	if (s->blocksize == 512)
		sb->s_first = htonl(2);
	else
		sb->s_first = htonl(1);

	sb->s_start    = htonl(1);
	sb->s_sequence = htonl(1);
	sb->s_errno    = htonl(0);

	do_pwrite(s, buf, OCFS2_DEFAULT_JOURNAL_SIZE, journal_off);
	free(buf);
}

static void
open_device(State *s)
{
	s->fd = open64(s->device_name, O_RDWR);

	if (s->fd == -1) {
		com_err(s->progname, 0,
		        "Could not open device %s: %s",
			s->device_name, strerror (errno));
		exit(1);
	}
}

static void
close_device(State *s)
{
	fsync(s->fd);
	close(s->fd);
	s->fd = -1;
}

static int
initial_nodes_for_volume(uint64_t size)
{
	int i, shift = ONE_GB_SHIFT;
	int defaults[4] = { 2, 4, 8, 16 };

	for (i = 0, shift = ONE_GB_SHIFT; i < 4; i++, shift += 3) {
		size >>= shift;

		if (!size)
			break;
	}

	return (i < 4) ? defaults[i] : 32;
}

/* XXX: Hm, maybe replace this with libuuid? */
static void
generate_uuid(State *s)
{
	int randfd = 0;
	int readlen = 0;
	int len = 0;

	if ((randfd = open("/dev/urandom", O_RDONLY)) == -1) {
		com_err(s->progname, 0,
			"Error opening /dev/urandom: %s", strerror(errno));
		exit(1);
	}

	s->uuid = do_malloc(s, MAX_VOL_ID_LENGTH);

	while (readlen < MAX_VOL_ID_LENGTH) {
		if ((len = read(randfd, s->uuid + readlen, MAX_VOL_ID_LENGTH - readlen)) == -1) {
			com_err(s->progname, 0,
				"Error reading from /dev/urandom: %s",
				strerror(errno));
			exit(1);
		}

		readlen += len;
	}

	close(randfd);
}

static void create_generation(State *s)
{
	int randfd = 0;
	int readlen = sizeof(s->vol_generation);

	if ((randfd = open("/dev/urandom", O_RDONLY)) == -1) {
		com_err(s->progname, 0,
			"Error opening /dev/urandom: %s", strerror(errno));
		exit(1);
	}

	if (read(randfd, &s->vol_generation, readlen) != readlen) {
		com_err(s->progname, 0,
			"Error reading from /dev/urandom: %s",
			strerror(errno));
		exit(1);
	}

	close(randfd);
}

static void
write_autoconfig_header(State *s, SystemFileDiskRecord *rec)
{
	ocfs_node_config_hdr *hdr;

	hdr = do_malloc(s, s->blocksize);
	memset(hdr, 0, s->blocksize);

	strcpy(hdr->signature, OCFS2_NODE_CONFIG_HDR_SIGN);
	hdr->version = OCFS2_NODE_CONFIG_VER;
	hdr->num_nodes = 0;
	hdr->disk_lock.dl_master = -1;
	hdr->last_node = 0;

	do_pwrite(s, hdr, s->blocksize, rec->extent_off);
	free(hdr);
}

static void
init_record(State *s, SystemFileDiskRecord *rec, int type, int dir)
{
	memset(rec, 0, sizeof(SystemFileDiskRecord));

	rec->flags = OCFS2_VALID_FL | OCFS2_SYSTEM_FL;
	rec->dir = dir;

	rec->links = dir ? 0 : 1;

	rec->bi.used_bits = rec->bi.total_bits = 0;
	rec->flags = (OCFS2_VALID_FL | OCFS2_SYSTEM_FL);

	switch (type) {
	case SFI_JOURNAL:
		rec->flags |= OCFS2_JOURNAL_FL;
		break;
	case SFI_BITMAP:
		rec->flags |= OCFS2_BITMAP_FL;
		break;
	case SFI_LOCAL_ALLOC:
		rec->flags |= OCFS2_LOCAL_ALLOC_FL;
		break;
	case SFI_DLM:
		rec->flags |= OCFS2_DLM_FL;
		break;
	case SFI_CHAIN:
		rec->flags |= (OCFS2_BITMAP_FL|OCFS2_CHAIN_FL);
		break;
	case SFI_OTHER:
		break;
	}
}

static void
print_state(State *s)
{
	if (s->quiet)
		return;

	printf("Filesystem label=%s\n", s->vol_label);
	printf("Block size=%u (bits=%u)\n", s->blocksize, s->blocksize_bits);
	printf("Cluster size=%u (bits=%u)\n", s->cluster_size, s->cluster_size_bits);
	printf("Volume size=%llu (%u clusters)\n",
	       (unsigned long long) s->volume_size_in_bytes,
	       s->volume_size_in_clusters);
	printf("Initial number of nodes: %u\n", s->initial_nodes);
}
