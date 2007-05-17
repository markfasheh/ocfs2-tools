/*
 * tune.c
 *
 * ocfs2 tune utility
 *
 * Copyright (C) 2004, 2006 Oracle.  All rights reserved.
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
#include <uuid/uuid.h>

#include <ocfs2.h>
#include <ocfs2_fs.h>
#include <ocfs1_fs_compat.h>
#include <bitops.h>

#include <jbd.h>

#include <kernel-list.h>

#define SYSTEM_FILE_NAME_MAX   40

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#define MOUNT_LOCAL             1
#define MOUNT_CLUSTER           2
#define MOUNT_LOCAL_STR         "local"
#define MOUNT_CLUSTER_STR       "cluster"

enum {
	BACKUP_SUPER_OPTION = CHAR_MAX + 1,
};

typedef struct _ocfs2_tune_opts {
	uint16_t num_slots;
	uint64_t num_blocks;
	uint64_t jrnl_size;
	char *vol_label;
	char *progname;
	char *device;
	char *vol_uuid;
	int mount;
	int verbose;
	int quiet;
	int prompt;
	int backup_super;
	time_t tune_time;
	int fd;
} ocfs2_tune_opts;

static ocfs2_tune_opts opts;
static ocfs2_filesys *fs_gbl = NULL;
static int cluster_locked = 0;
static int resize = 0;

static void usage(const char *progname)
{
	fprintf(stderr, "usage: %s [-J journal-options] [-L volume-label]\n"
			"\t\t[-M mount-type] [-N number-of-node-slots]\n"
			"\t\t[-qSUvV] [--backup-super] device [blocks-count]\n",
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
     sigprocmask(how, &sigs, NULL);
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
			    val < OCFS2_MIN_JOURNAL_SIZE) {
				com_err(progname, 0,
					"Invalid journal size: %s\nSize must "
					"be greater than %d bytes",
					arg,
					OCFS2_MIN_JOURNAL_SIZE);
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
			"\tsize=<journal size>");
		exit(1);
	}

	free(options);
}

static void get_options(int argc, char **argv)
{
	int c;
	int show_version = 0;
	int uuid = 0;
	char *dummy;

	static struct option long_options[] = {
		{ "label", 1, 0, 'L' },
		{ "node-slots", 1, 0, 'N' },
		{ "verbose", 0, 0, 'v' },
		{ "quiet", 0, 0, 'q' },
		{ "version", 0, 0, 'V' },
		{ "journal-options", 0, 0, 'J'},
		{ "volume-size", 0, 0, 'S'},
		{ "uuid-reset", 0, 0, 'U'},
		{ "mount", 1, 0, 'M' },
		{ "backup-super", 0, 0, BACKUP_SUPER_OPTION },
		{ 0, 0, 0, 0}
	};

	if (argc && *argv)
		opts.progname = basename(argv[0]);
	else
		opts.progname = strdup("tunefs.ocfs2");

	opts.prompt = 1;

	while (1) {
		c = getopt_long(argc, argv, "L:N:J:M:SUvqVxb", long_options,
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

		case 'M':
			if (!strncasecmp(optarg, MOUNT_LOCAL_STR,
					 strlen(MOUNT_LOCAL_STR)))
				opts.mount = MOUNT_LOCAL;
			else if (!strncasecmp(optarg, MOUNT_CLUSTER_STR,
					      strlen(MOUNT_CLUSTER_STR)))
				opts.mount = MOUNT_CLUSTER;
			else {
				com_err(opts.progname, 0,
					"Invalid mount option: %s", optarg);
				exit(1);
			}
			break;

		case 'N':
			opts.num_slots = strtoul(optarg, &dummy, 0);

			if (opts.num_slots > OCFS2_MAX_SLOTS ||
			    *dummy != '\0') {
				com_err(opts.progname, 0,
					"Number of node slots must be no more "
					"than %d",
					OCFS2_MAX_SLOTS);
				exit(1);
			} else if (opts.num_slots < 2) {
				com_err(opts.progname, 0,
					"Number of node slots must be at "
					"least 2");
				exit(1);
			}
			break;

		case 'J':
			parse_journal_opts(opts.progname, optarg,
					   &opts.jrnl_size);
			break;

		case 'S':
			resize = 1;
			break;

		case 'U':
			uuid = 1;
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

		case BACKUP_SUPER_OPTION:
			opts.backup_super = 1;
			break;

		default:
			usage(opts.progname);
			break;
		}
	}

	/* we don't allow backup_super to be coexist with other tunefs
	 * options to keep things simple.
	 */
	if (opts.backup_super &&
	    (opts.vol_label || opts.num_slots ||
	     opts.mount || opts.jrnl_size || resize)) {
		com_err(opts.progname, 0, "Cannot backup superblock"
			" along with other tasks");
		exit(1);
	}

	if (!opts.quiet || show_version)
		version(opts.progname);

	if (resize && (opts.num_slots || opts.jrnl_size)) {
		com_err(opts.progname, 0, "Cannot resize volume while adding slots "
			"or resizing the journals");
		exit(0);
	}

	if (show_version)
		exit(0);

	if (uuid) {
		opts.vol_uuid = malloc(OCFS2_VOL_UUID_LEN);
		if (opts.vol_uuid)
			uuid_generate(opts.vol_uuid);
		else {
			com_err(opts.progname, OCFS2_ET_NO_MEMORY,
				"while allocating %d bytes during uuid generate",
				OCFS2_VOL_UUID_LEN);
			exit(1);
		}
	}

	if (optind == argc)
		usage(opts.progname);

	opts.device = strdup(argv[optind]);
	optind++;

	if (optind < argc) {
		if (resize) {
			opts.num_blocks = strtoull(argv[optind], &dummy, 0);
			if ((*dummy)) {
				com_err(opts.progname, 0, "Block count bad - %s",
					argv[optind]);
				exit(1);
			}
		}
		optind++;
	}

	if (optind < argc)
		usage(opts.progname);

	opts.tune_time = time(NULL);

	return ;
}

static void get_vol_size(ocfs2_filesys *fs)
{
	errcode_t ret = 0;
	uint64_t num_blocks;

	ret = ocfs2_get_device_size(opts.device, fs->fs_blocksize,
				    &num_blocks);
	if (ret) {
		com_err(opts.progname, ret, "while getting size of device %s",
			opts.device);
		exit(1);
	}

	if (!opts.num_blocks)
		opts.num_blocks = num_blocks;

	if (opts.num_blocks > num_blocks) {
		com_err(opts.progname, 0, "The containing partition (or device) "
			"is only %"PRIu64" blocks", num_blocks);
		exit(1);
	}

	return ;
}

static int validate_mount_change(ocfs2_filesys *fs)
{
	if (opts.mount == MOUNT_LOCAL) {
		if (!ocfs2_mount_local(fs))
			return 0;
	} else if (opts.mount == MOUNT_CLUSTER) {
		if (ocfs2_mount_local(fs))
			return 0;
	}

	return -1;
}

static int validate_vol_size(ocfs2_filesys *fs)
{
	uint64_t num_blocks;

	if (opts.num_blocks == fs->fs_blocks) {
		com_err(opts.progname, 0, "The filesystem is already "
			"%"PRIu64" blocks", fs->fs_blocks);
		return -1;
	}

	if (opts.num_blocks < fs->fs_blocks) {
		com_err(opts.progname, 0, "Cannot shrink volume size from "
		       "%"PRIu64" blocks to %"PRIu64" blocks",
		       fs->fs_blocks, opts.num_blocks);
		return -1;
	}

	num_blocks = ocfs2_clusters_to_blocks(fs, 1);
	if (num_blocks > (opts.num_blocks - fs->fs_blocks)) {
		com_err(opts.progname, 0, "Cannot grow volume size less than "
		       "%d blocks", num_blocks);
		return -1;
	}

	if (opts.num_blocks > UINT32_MAX) {
		com_err(opts.progname, 0, "As JBD can only store block numbers "
			"in 32 bits, %s cannot be grown to more than %"PRIu64" "
			"blocks.", opts.device, UINT32_MAX);
		return -1;
	}

	return 0;
}

static errcode_t add_slots(ocfs2_filesys *fs)
{
	errcode_t ret = 0;
	uint16_t old_num = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;
	char fname[SYSTEM_FILE_NAME_MAX];
	uint64_t blkno;
	int i, j;
	char *display_str = NULL;
	int ftype;

	for (i = OCFS2_LAST_GLOBAL_SYSTEM_INODE + 1; i < NUM_SYSTEM_INODES; ++i) {
		for (j = old_num; j < opts.num_slots; ++j) {
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
			if (ret) {
				printf("\n");
				com_err(opts.progname, ret, "while creating "
					"inode for system file %s",
					ocfs2_system_inodes[i].si_name);
				goto bail;
			}

			ftype = (S_ISDIR(ocfs2_system_inodes[i].si_mode) ?
				 OCFS2_FT_DIR : OCFS2_FT_REG_FILE);

			/* if dir, alloc space to it */
			if (ftype == OCFS2_FT_DIR) {
				ret = ocfs2_expand_dir(fs, blkno, fs->fs_sysdir_blkno);
				if (ret) {
					printf("\n");
					com_err(opts.progname, ret,
						"while expanding system directory");
					goto bail;
				}
			}

			/* Add the inode to the system dir */
			ret = ocfs2_link(fs, fs->fs_sysdir_blkno, fname, blkno,
					 ftype);
			if (!ret)
				goto next_file;
			if (ret == OCFS2_ET_DIR_NO_SPACE) {
				ret = ocfs2_expand_dir(fs, fs->fs_sysdir_blkno,
						       fs->fs_sysdir_blkno);
				if (!ret)
					ret = ocfs2_link(fs, fs->fs_sysdir_blkno,
							 fname, blkno, ftype);
			} else {
				com_err(opts.progname, 0, "while linking inode "
					"%"PRIu64" to the system directory", blkno);
				goto bail;
			}
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

static errcode_t journal_check(ocfs2_filesys *fs, int *dirty, uint64_t *jrnl_size)
{
	errcode_t ret;
	char *buf = NULL;
	uint64_t blkno;
	struct ocfs2_dinode *di;
	int i;
	int cs_bits = OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits;
	uint16_t max_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret) {
		com_err(opts.progname, ret, "while allocating a block "
			"during journal check");
		goto bail;
	}

	*dirty = 0;
	*jrnl_size = 0;

	for (i = 0; i < max_slots; ++i) {
		ret = ocfs2_lookup_system_inode(fs, JOURNAL_SYSTEM_INODE, i,
						&blkno);
		if (ret) {
			com_err(opts.progname, ret, "while looking up "
				"journal inode for slot %u during journal "
				"check", i);
			goto bail;
		}

		ret = ocfs2_read_inode(fs, blkno, buf);
		if (ret) {
			com_err(opts.progname, ret, "while reading inode "
				"%"PRIu64" during journal check", blkno);
			goto bail;
		}

		di = (struct ocfs2_dinode *)buf;

		*jrnl_size = MAX(*jrnl_size, (di->i_clusters << cs_bits));

		*dirty = di->id1.journal1.ij_flags & OCFS2_JOURNAL_DIRTY_FL;
		if (*dirty) {
			com_err(opts.progname, 0,
				"Node slot %d's journal is dirty. Run "
				"fsck.ocfs2 to replay all dirty journals.", i);
			break;
		}
	}
bail:
	if (buf)
		ocfs2_free(&buf);

	return ret;
}

static void get_total_free_bits(struct ocfs2_group_desc *gd, int *bits)
{
	int end = 0;
	int start;

	*bits = 0;

	while (end < gd->bg_bits) {
		start = ocfs2_find_next_bit_clear(gd->bg_bitmap, gd->bg_bits, end);
		if (start >= gd->bg_bits)
			break;
		end = ocfs2_find_next_bit_set(gd->bg_bitmap, gd->bg_bits, start);
		*bits += (end - start);
	}
}

static errcode_t validate_chain_group(ocfs2_filesys *fs, struct ocfs2_dinode *di,
				      int chain)
{
	errcode_t ret = 0;
	uint64_t blkno;
	char *buf = NULL;
	struct ocfs2_group_desc *gd;
	struct ocfs2_chain_list *cl;
	struct ocfs2_chain_rec *cr;
	uint32_t total = 0;
	uint32_t free = 0;
	uint32_t bits;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret) {
		com_err(opts.progname, ret, "while allocating a block "
			"during chain group validation");
		goto bail;
	}

	total = 0;
	free = 0;

	cl = &(di->id2.i_chain);
	cr = &(cl->cl_recs[chain]);
	blkno = cr->c_blkno;

	while (blkno) {
		ret = ocfs2_read_group_desc(fs, blkno, buf);
		if (ret) {
			com_err(opts.progname, ret,
				"while reading group descriptor at "
				"block %"PRIu64" during chain group validation",
				blkno);
			goto bail;
		}

		gd = (struct ocfs2_group_desc *)buf;

		if (gd->bg_parent_dinode != di->i_blkno) {
			ret = OCFS2_ET_CORRUPT_CHAIN;
			com_err(opts.progname, ret, " - group descriptor at "
				"%"PRIu64" does not belong to allocator %"PRIu64"",
				blkno, di->i_blkno);
			goto bail;
		}

		if (gd->bg_chain != chain) {
			ret = OCFS2_ET_CORRUPT_CHAIN;
			com_err(opts.progname, ret, " - group descriptor at "
				"%"PRIu64" does not agree to the chain it "
				"belongs to in allocator %"PRIu64"",
				blkno, di->i_blkno);
			goto bail;
		}

		get_total_free_bits(gd, &bits);
		if (bits != gd->bg_free_bits_count) {
			ret = OCFS2_ET_CORRUPT_CHAIN;
			com_err(opts.progname, ret, " - group descriptor at "
				"%"PRIu64" does not have a consistent free "
				"bit count", blkno);
			goto bail;
		}

		if (gd->bg_bits > gd->bg_size * 8) {
			ret = OCFS2_ET_CORRUPT_CHAIN;
			com_err(opts.progname, ret, " - group descriptor at "
				"%"PRIu64" does not have a valid total bit "
				"count", blkno);
			goto bail;
		}

		if (gd->bg_free_bits_count >= gd->bg_bits) {
			ret = OCFS2_ET_CORRUPT_CHAIN;
			com_err(opts.progname, ret, " - group descriptor at "
				"%"PRIu64" has inconsistent free/total bit "
				"counts", blkno);
			goto bail;
		}

		total += gd->bg_bits;
		free += gd->bg_free_bits_count;
		blkno = gd->bg_next_group;
	}

	if (cr->c_total != total) {
		ret = OCFS2_ET_CORRUPT_CHAIN;
		com_err(opts.progname, ret, " - total bits for chain %u in "
			"allocator %"PRIu64" does not match its chained group "
			"descriptors", chain, di->i_blkno);
		goto bail;

	}

	if (cr->c_free != free) {
		ret = OCFS2_ET_CORRUPT_CHAIN;
		com_err(opts.progname, ret, " - free bits for chain %u in "
			"allocator %"PRIu64" does not match its chained group "
			"descriptors", chain, di->i_blkno);
		goto bail;
	}

bail:
	if (buf)
		ocfs2_free(&buf);

	return ret;
}

static errcode_t global_bitmap_check(ocfs2_filesys *fs)
{
	errcode_t ret = 0;
	uint64_t bm_blkno = 0;
	char *buf = NULL;
	struct ocfs2_chain_list *cl;
	struct ocfs2_dinode *di;
	int i;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret) {
		com_err(opts.progname, ret, "while allocating a block "
			"during global bitmap check");
		goto bail;
	}

	ret = ocfs2_lookup_system_inode(fs, GLOBAL_BITMAP_SYSTEM_INODE, 0,
					&bm_blkno);
	if (ret) {
		com_err(opts.progname, ret, "while looking up global "
			"bitmap inode");
		goto bail;
	}

	ret = ocfs2_read_inode(fs, bm_blkno, buf);
	if (ret) {
		com_err(opts.progname, ret, "while reading global bitmap "
			"inode at block %"PRIu64"", bm_blkno);
		goto bail;
	}

	di = (struct ocfs2_dinode *)buf;
	cl = &(di->id2.i_chain);

	for (i = 0; i < cl->cl_next_free_rec; ++i) {
		ret = validate_chain_group(fs, di, i);
		if (ret)
			goto bail;
	}

bail:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

static errcode_t load_chain_allocator(ocfs2_filesys *fs,
					     ocfs2_cached_inode** inode)
{
	errcode_t ret;
	uint64_t blkno;

	ret = ocfs2_lookup_system_inode(fs, GLOBAL_BITMAP_SYSTEM_INODE,
					0, &blkno);
	if (ret)
		goto bail;

	ret = ocfs2_read_cached_inode(fs, blkno, inode);
	if (ret)
		goto bail;

	ret = ocfs2_load_chain_allocator(fs, *inode);

bail:
	return ret;
}

static errcode_t backup_super_check(ocfs2_filesys *fs)
{
	errcode_t ret;
	int i, num, val, failed = 0;
	ocfs2_cached_inode *chain_alloc = NULL;
	uint64_t blocks[OCFS2_MAX_BACKUP_SUPERBLOCKS];
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);

	/* if the compat flag is set, just return. */
	if (OCFS2_HAS_COMPAT_FEATURE(super, OCFS2_FEATURE_COMPAT_BACKUP_SB)) {
		com_err(opts.progname, 0,
			"Volume has been enabled for Backup superblock");
		return -1;
	}

	num = ocfs2_get_backup_super_offset(fs, blocks, ARRAY_SIZE(blocks));
	if (!num) {
		com_err(opts.progname, 0,
			"Volume is too small to hold backup superblocks");
		return -1;
	}

	ret = load_chain_allocator(fs, &chain_alloc);
	if (ret)
		goto bail;

	for (i = 0; i < num; i++) {
		ret = ocfs2_bitmap_test(chain_alloc->ci_chains,
					ocfs2_blocks_to_clusters(fs, blocks[i]),
					&val);
		if (ret)
			goto bail;

		if (val) {
			com_err(opts.progname, 0, "block %"PRIu64
				" is in use.", blocks[i]);
			/* in order to verify all the block in the 'blocks',
			 * we don't stop the loop here.
			 */
			failed = 1;
		}
	}

	if (failed) {
		ret = ENOSPC;
		com_err(opts.progname, 0, "Cannot enable backup superblock as "
			"backup blocks are in use");
	}

	if (chain_alloc)
		ocfs2_free_cached_inode(fs, chain_alloc);
bail:
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

static void update_volume_uuid(ocfs2_filesys *fs, int *changed)
{
	memcpy(OCFS2_RAW_SB(fs->fs_super)->s_uuid, opts.vol_uuid,
	       OCFS2_VOL_UUID_LEN);

	*changed = 1;

	return ;
}

static void update_mount_type(ocfs2_filesys *fs, int *changed)
{
	if (opts.mount == MOUNT_LOCAL)
		OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat |=
			OCFS2_FEATURE_INCOMPAT_LOCAL_MOUNT;
	else if (opts.mount == MOUNT_CLUSTER)
		OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat &=
			~OCFS2_FEATURE_INCOMPAT_LOCAL_MOUNT;
	else
		return;

	*changed = 1;

	return;
}

static errcode_t update_slots(ocfs2_filesys *fs, int *changed)
{
	errcode_t ret = 0;

	block_signals(SIG_BLOCK);
	ret = add_slots(fs);
	block_signals(SIG_UNBLOCK);
	if (ret)
		return ret;

	OCFS2_RAW_SB(fs->fs_super)->s_max_slots = opts.num_slots;
	*changed = 1;

	return ret;
}

static errcode_t update_journal_size(ocfs2_filesys *fs, int *changed)
{
	errcode_t ret = 0;
	char jrnl_file[40];
	uint64_t blkno;
	int i;
	uint16_t max_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;
	uint32_t num_clusters;
	char *buf = NULL;
	struct ocfs2_dinode *di;

	num_clusters = opts.jrnl_size >>
			OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret) {
		com_err(opts.progname, ret, "while allocating a block "
			"during journal resize");
		return ret;
	}

	for (i = 0; i < max_slots; ++i) {
		snprintf (jrnl_file, sizeof(jrnl_file),
			  ocfs2_system_inodes[JOURNAL_SYSTEM_INODE].si_name, i);

		ret = ocfs2_lookup(fs, fs->fs_sysdir_blkno, jrnl_file,
				   strlen(jrnl_file), NULL, &blkno);
		if (ret) {
			com_err(opts.progname, ret, "while looking up %s during "
				"journal resize", jrnl_file);
			goto bail;
		}

		ret = ocfs2_read_inode(fs, blkno, buf);
		if (ret) {
			com_err(opts.progname, ret, "while reading inode at "
				"block %"PRIu64" during journal resize", blkno);
			goto bail;
		}

		di = (struct ocfs2_dinode *)buf;
		if (num_clusters == di->i_clusters)
			continue;

		printf("Updating %s...  ", jrnl_file);
		block_signals(SIG_BLOCK);
		ret = ocfs2_make_journal(fs, blkno, num_clusters);
		block_signals(SIG_UNBLOCK);
		if (ret) {
			printf("\n");
			com_err(opts.progname, ret, "while creating %s at block "
				"%"PRIu64" of %u clusters during journal resize",
				jrnl_file, blkno, num_clusters);
			goto bail;
		}
		printf("\r                                                     \r");
		*changed = 1;
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
	errcode_t ret = 0;
	struct ocfs2_dinode *di;
	uint64_t bm_blkno = 0;
	uint64_t gd_blkno = 0;
	uint64_t lgd_blkno = 0;
	char *in_buf = NULL;
	char *gd_buf = NULL;
	char *lgd_buf = NULL;
	struct ocfs2_chain_list *cl;
	struct ocfs2_chain_rec *cr;
	struct ocfs2_group_desc *gd;
	uint32_t cluster_chunk;
	uint32_t num_new_clusters, save_new_clusters;
	uint32_t first_new_cluster;
	uint16_t chain;
	uint32_t used_bits;
	uint32_t total_bits;
	uint32_t num_bits;
	int flush_lgd = 0;
	char *zero_buf = NULL;

	ret = ocfs2_malloc_block(fs->fs_io, &in_buf);
	if (ret) {
		com_err(opts.progname, ret, "while allocating a block during "
			"volume resize");
		goto bail;
	}

	ret = ocfs2_malloc_block(fs->fs_io, &gd_buf);
	if (ret) {
		com_err(opts.progname, ret, "while allocating a block during "
			"volume resize");
		goto bail;
	}

	ret = ocfs2_malloc_block(fs->fs_io, &lgd_buf);
	if (ret) {
		com_err(opts.progname, ret, "while allocating a block during "
			"volume resize");
		goto bail;
	}

	ret = ocfs2_malloc_blocks(fs->fs_io, ocfs2_clusters_to_blocks(fs, 1),
				  &zero_buf);
	if (ret) {
		com_err(opts.progname, ret, "while allocating a cluster during "
			"volume resize");
		goto bail;
	}

	memset(zero_buf, 0, fs->fs_clustersize);

	/* read global bitmap */
	ret = ocfs2_lookup_system_inode(fs, GLOBAL_BITMAP_SYSTEM_INODE, 0,
					&bm_blkno);
	if (ret) {
		com_err(opts.progname, ret, "while looking up global bitmap "
			"inode during volume resize");
		goto bail;
	}

	ret = ocfs2_read_inode(fs, bm_blkno, in_buf);
	if (ret) {
		com_err(opts.progname, ret, "while reading inode at block "
			"%"PRIu64" during volume resize", bm_blkno);
		goto bail;
	}

	di = (struct ocfs2_dinode *)in_buf;
	cl = &(di->id2.i_chain);

	total_bits = di->id1.bitmap1.i_total;
	used_bits = di->id1.bitmap1.i_used;

	first_new_cluster = di->i_clusters;
	save_new_clusters = num_new_clusters =
	       	ocfs2_blocks_to_clusters(fs, opts.num_blocks) - di->i_clusters;

	/* Find the blknum of the last cluster group */
	lgd_blkno = ocfs2_which_cluster_group(fs, cl->cl_cpg, first_new_cluster - 1);

	ret = ocfs2_read_group_desc(fs, lgd_blkno, lgd_buf);
	if (ret) {
		com_err(opts.progname, ret, "while reading group descriptor "
			"at block %"PRIu64" during volume resize", lgd_blkno);
		goto bail;
	}

	gd = (struct ocfs2_group_desc *)lgd_buf;

	/* If only one cluster group then see if we need to adjust up cl_cpg */
	if (cl->cl_next_free_rec == 1) {
		if (cl->cl_cpg < 8 * gd->bg_size)
			cl->cl_cpg = 8 * gd->bg_size;
	}

	chain = gd->bg_chain;

	/* If possible round off the last group to cpg */
	cluster_chunk = MIN(num_new_clusters,
			    (cl->cl_cpg - (gd->bg_bits/cl->cl_bpc)));
	if (cluster_chunk) {
		num_new_clusters -= cluster_chunk;
		first_new_cluster += cluster_chunk;

		num_bits = cluster_chunk * cl->cl_bpc;

		gd->bg_bits += num_bits;
		gd->bg_free_bits_count += num_bits;

		cr = &(cl->cl_recs[chain]);
		cr->c_total += num_bits;
		cr->c_free += num_bits;

		total_bits += num_bits;

		fs->fs_clusters += cluster_chunk;
		fs->fs_blocks += ocfs2_clusters_to_blocks(fs, cluster_chunk);

		/* This cluster group block is written after the new */
		/* cluster groups are written to disk */
		flush_lgd = 1;
	}

	/* Init the new groups and write to disk */
	/* Add these groups one by one starting from the first chain after */
	/* the one containing the last group */

	gd = (struct ocfs2_group_desc *)gd_buf;

	while(num_new_clusters) {
		gd_blkno = ocfs2_which_cluster_group(fs, cl->cl_cpg,
						     first_new_cluster);
		cluster_chunk = MIN(num_new_clusters, cl->cl_cpg);
		num_new_clusters -= cluster_chunk;
		first_new_cluster += cluster_chunk;

		if (++chain >= cl->cl_count)
			chain = 0;

		ocfs2_init_group_desc(fs, gd, gd_blkno,
				      fs->fs_super->i_fs_generation, di->i_blkno,
				      (cluster_chunk *cl->cl_bpc), chain);

		/* Add group to chain */
		cr = &(cl->cl_recs[chain]);
		if (chain >= cl->cl_next_free_rec) {
			cl->cl_next_free_rec++;
			cr->c_free = 0;
			cr->c_total = 0;
			cr->c_blkno = 0;
		}

		gd->bg_next_group = cr->c_blkno;
		cr->c_blkno = gd_blkno;
		cr->c_free += gd->bg_free_bits_count;
		cr->c_total += gd->bg_bits;

		used_bits += (gd->bg_bits - gd->bg_free_bits_count);
		total_bits += gd->bg_bits;

		fs->fs_clusters += cluster_chunk;
		fs->fs_blocks += ocfs2_clusters_to_blocks(fs, cluster_chunk);

		/* Initialize the first cluster in the group */
		ret = io_write_block(fs->fs_io, gd_blkno,
				     ocfs2_clusters_to_blocks(fs, 1), zero_buf);
		if (ret) {
			com_err(opts.progname, ret, "while initializing the "
				"cluster starting at block %"PRIu64" during "
				"volume resize", gd_blkno);
			goto bail;
		}

		/* write a new group descriptor */
		ret = ocfs2_write_group_desc(fs, gd_blkno, gd_buf);
		if (ret) {
			com_err(opts.progname, ret, "while writing group "
				"descriptor at block %"PRIu64" during "
				"volume resize", gd_blkno);
			goto bail;
		}
	}

	di->id1.bitmap1.i_total = total_bits;
	di->id1.bitmap1.i_used = used_bits;

	di->i_clusters += save_new_clusters;
	di->i_size = (uint64_t) di->i_clusters * fs->fs_clustersize;

	fs->fs_super->i_clusters = di->i_clusters;

	block_signals(SIG_BLOCK);
	/* Flush that last group descriptor we updated before the new ones */
	if (flush_lgd) {
		ret = ocfs2_write_group_desc(fs, lgd_blkno, lgd_buf);
		if (ret) {
			block_signals(SIG_UNBLOCK);
			com_err(opts.progname, ret, "while flushing group "
				"descriptor at block %"PRIu64" during "
				"volume resize", lgd_blkno);
			goto bail;
		}
	}

	/* write the global bitmap inode */
	ret = ocfs2_write_inode(fs, bm_blkno, in_buf);
	if (ret) {
		block_signals(SIG_UNBLOCK);
		com_err(opts.progname, ret, "while writing global bitmap "
			"inode at block %"PRIu64" during volume resize",
			bm_blkno);
 		goto bail;
	}

	block_signals(SIG_UNBLOCK);

	*changed = 1;

bail:
	if (zero_buf)
		ocfs2_free(&zero_buf);
	if (in_buf)
		ocfs2_free(&in_buf);
	if (gd_buf)
		ocfs2_free(&gd_buf);
	if (lgd_buf)
		ocfs2_free(&lgd_buf);

	return ret;
}

static errcode_t refresh_backup_super(ocfs2_filesys *fs)
{
	errcode_t ret;
	int num;
	uint64_t blocks[OCFS2_MAX_BACKUP_SUPERBLOCKS];

	num = ocfs2_get_backup_super_offset(fs, blocks, ARRAY_SIZE(blocks));
	if (!num)
		return 0;

	ret = ocfs2_refresh_backup_super(fs, blocks, num);

	return ret;
}

static errcode_t update_backup_super(ocfs2_filesys *fs, uint64_t newblocks)
{
	errcode_t ret;
	int num, i;
	uint64_t *new_backup_super, blocks[OCFS2_MAX_BACKUP_SUPERBLOCKS];
	uint64_t startblk = fs->fs_blocks - newblocks;

	num = ocfs2_get_backup_super_offset(fs, blocks, ARRAY_SIZE(blocks));
	if (!num)
		return 0;

	if (newblocks) {
		for (i = 0; i < num; i++) {
			if (blocks[i] >= startblk)
				break;
		}

		if (!(num - i))
			return 0;

		new_backup_super = &blocks[i];
		num -= i;
	} else
		new_backup_super = blocks;

	ret = ocfs2_set_backup_super(fs, new_backup_super, num);
	if (ret) {
		com_err(opts.progname, ret, "while backing up superblock.");
		goto bail;
	}

bail:
	return ret;
}

int main(int argc, char **argv)
{
	errcode_t ret = 0;
	ocfs2_filesys *fs = NULL;
	int open_flags = OCFS2_FLAG_RW | OCFS2_FLAG_STRICT_COMPAT_CHECK;
	int upd_label = 0;
	int upd_uuid = 0;
	int upd_slots = 0;
	int upd_jrnls = 0;
	int upd_blocks = 0;
	int upd_mount = 0;
	int upd_incompat = 0;
	int upd_backup_super = 0;
	char *tmpstr;
	uint16_t tmp;
	uint64_t def_jrnl_size = 0;
	uint64_t num_clusters;
	int dirty = 0;
	char old_uuid[OCFS2_VOL_UUID_LEN * 2 + 1];
	char new_uuid[OCFS2_VOL_UUID_LEN * 2 + 1];

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

	/* strict compat flag check */
	ret = ocfs2_open(opts.device, open_flags, 0, 0, &fs); //O_EXCL?
	if (ret) {
		com_err(opts.progname, ret, "while opening device %s",
			opts.device);
		goto close;
	}
	fs_gbl = fs;

	if (OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat &
	    OCFS2_FEATURE_INCOMPAT_RESIZE_INPROG) {
		fprintf(stderr, "Aborted resize detected. Run fsck.ocfs2 -f <device>.\n");
		goto close;
	}

	if (resize)
		get_vol_size(fs);

	if (!ocfs2_mount_local(fs)) {
		ret = o2cb_init();
		if (ret) {
			com_err(opts.progname, ret, "while initializing the cluster");
			exit(1);
		}

		ret = ocfs2_initialize_dlm(fs);
		if (ret) {
			com_err(opts.progname, ret, "while initializing the dlm");
			goto close;
		}

		block_signals(SIG_BLOCK);
		ret = ocfs2_lock_down_cluster(fs);
		if (ret) {
			block_signals(SIG_UNBLOCK);
			com_err(opts.progname, ret, "while locking down the cluster");
			goto close;
		}
		cluster_locked = 1;
		block_signals(SIG_UNBLOCK);
	}

	ret = journal_check(fs, &dirty, &def_jrnl_size);
	if (ret || dirty)
		goto unlock;

	/* If operation requires touching the global bitmap, ensure it is good */
	/* This is to handle failed resize */
	if (opts.num_blocks || opts.num_slots || opts.jrnl_size ||
	    opts.backup_super) {
		if (global_bitmap_check(fs)) {
			com_err(opts.progname, 0, "Global bitmap check failed. "
				"Run fsck.ocfs2 -f <device>.");
			goto unlock;
		}
	}

	/* check whether the block for backup superblock are used. */
	if (opts.backup_super) {
		if (backup_super_check(fs))
			goto unlock;
		else
			printf("Adding backup superblock for the volume\n");
	}

	/* validate volume label */
	if (opts.vol_label) {
		printf("Changing volume label from %s to %s\n",
		       OCFS2_RAW_SB(fs->fs_super)->s_label, opts.vol_label);
	}

	/* validate volume uuid */
	if (opts.vol_uuid) {
		uuid_unparse(OCFS2_RAW_SB(fs->fs_super)->s_uuid, old_uuid);
		uuid_unparse(opts.vol_uuid, new_uuid);
		printf("Changing volume uuid from %s to %s\n", old_uuid, new_uuid);
	}

	/* validate mount type */
	if (opts.mount) {
		if (!validate_mount_change(fs)) {
			if (opts.mount == MOUNT_LOCAL)
				tmpstr = MOUNT_LOCAL_STR;
			else
				tmpstr = MOUNT_CLUSTER_STR;
			printf("Changing mount type to %s\n", tmpstr);
		} else
			opts.mount = 0;
	}

	/* validate num slots */
	if (opts.num_slots) {
		tmp = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;
		if (opts.num_slots > tmp) {
			printf("Changing number of node slots from %d to %d\n",
			       tmp, opts.num_slots);
		} else {
			com_err(opts.progname, 0, "Node slots (%d) has to be "
				"more than the configured node slots (%d)",
			       opts.num_slots, tmp);
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

		if (opts.jrnl_size != def_jrnl_size)
			printf("Changing journal size %"PRIu64" to %"PRIu64"\n",
			       def_jrnl_size, opts.jrnl_size);
	}

	/* validate volume size */
	if (opts.num_blocks) {
		if (validate_vol_size(fs))
			opts.num_blocks = 0;
		else
			printf("Changing volume size from %"PRIu64" blocks to "
			       "%"PRIu64" blocks\n", fs->fs_blocks,
			       opts.num_blocks);
	}

	if (!opts.vol_label && !opts.vol_uuid && !opts.num_slots &&
	    !opts.jrnl_size && !opts.num_blocks && !opts.mount &&
	    !opts.backup_super) {
		com_err(opts.progname, 0, "Nothing to do. Exiting.");
		goto unlock;
	}

	/* Abort? */
	if (opts.prompt) {
		printf("Proceed (y/N): ");
		if (toupper(getchar()) != 'Y') {
			printf("Aborting operation.\n");
			goto unlock;
		}
	}

	/* Set resize incompat flag on superblock */
	if (opts.num_blocks) {
		OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat |=
			OCFS2_FEATURE_INCOMPAT_RESIZE_INPROG;
		ret = ocfs2_write_super(fs);
		if (ret) {
			com_err(opts.progname, ret,
				"while writing resize incompat flag");
			goto unlock;
		}
		upd_incompat = 1;
	}

	/* update volume label */
	if (opts.vol_label) {
		update_volume_label(fs, &upd_label);
		if (upd_label)
			printf("Changed volume label\n");
	}

	/* update volume uuid */
	if (opts.vol_uuid) {
		update_volume_uuid(fs, &upd_uuid);
		if (upd_uuid)
			printf("Changed volume uuid\n");
	}

	/* update number of slots */
	if (opts.num_slots) {
		ret = update_slots(fs, &upd_slots);
		if (ret) {
			com_err(opts.progname, ret,
				"while updating node slots");
			goto unlock;
		}
		if (upd_slots)
			printf("Added node slots\n");
	}

	/* change mount type */
	if (opts.mount) {
		update_mount_type(fs, &upd_mount);
		if (upd_mount)
			printf("Changed mount type\n");
	}

	/* update journal size */
	if (opts.jrnl_size) {
		ret = update_journal_size(fs, &upd_jrnls);
		if (ret) {
			com_err(opts.progname, ret,
				"while updating journal size");
			goto unlock;
		}
		if (upd_jrnls)
			printf("Resized journals\n");
	}

	/* update volume size */
	if (opts.num_blocks) {
		ret = update_volume_size(fs, &upd_blocks);
		if (ret) {
			com_err(opts.progname, ret,
				"while updating volume size");
			goto unlock;
		}
		/* Clear resize incompat flag on superblock */
		OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat &=
			~OCFS2_FEATURE_INCOMPAT_RESIZE_INPROG;
		if (upd_blocks)
			printf("Resized volume\n");
	}

	/* update the backup superblock. */
	if (opts.backup_super ||
	    (opts.num_blocks &&
	    OCFS2_HAS_COMPAT_FEATURE(OCFS2_RAW_SB(fs->fs_super),
				     OCFS2_FEATURE_COMPAT_BACKUP_SB))) {
		block_signals(SIG_BLOCK);
		ret = update_backup_super(fs, opts.num_blocks);
		block_signals(SIG_UNBLOCK);
		if (ret) {
			com_err(opts.progname, ret,
				"while backuping superblock");
			goto unlock;
		}
		OCFS2_SET_COMPAT_FEATURE(OCFS2_RAW_SB(fs->fs_super),
					 OCFS2_FEATURE_COMPAT_BACKUP_SB);

		if (opts.backup_super) {
			printf("Backed up Superblock.\n");
			upd_backup_super = 1;
		}
	}

	/* write superblock */
	if (upd_label || upd_uuid || upd_slots || upd_blocks || upd_incompat ||
	    upd_mount || upd_backup_super) {
		block_signals(SIG_BLOCK);
		ret = ocfs2_write_super(fs);
		if (ret) {
			com_err(opts.progname, ret, "while writing superblock");
			goto unlock;
		}
		block_signals(SIG_UNBLOCK);
		printf("Wrote Superblock\n");

		/* superblock's information has changed.
		 * We need to synchronize the backup blocks if needed.
		 * We also have to admit that if upd_backup_super is set,
		 * there is no need to refresh the backups since they are
		 * written above by update_backup_super.
		 */
		if (!upd_backup_super &&
		    OCFS2_HAS_COMPAT_FEATURE(OCFS2_RAW_SB(fs->fs_super),
					     OCFS2_FEATURE_COMPAT_BACKUP_SB)) {
			block_signals(SIG_BLOCK);
			ret = refresh_backup_super(fs);
			block_signals(SIG_UNBLOCK);
			if (ret) {
				printf("Unable to refresh backup superblocks. "
					"Please run fsck.ocfs2 before running "
					"tunefs.ocfs2 to re-enable "
					"backup superblocks.");
				OCFS2_CLEAR_COMPAT_FEATURE(
					OCFS2_RAW_SB(fs->fs_super),
					OCFS2_FEATURE_COMPAT_BACKUP_SB);
				block_signals(SIG_BLOCK);
				ocfs2_write_super(fs);
				block_signals(SIG_UNBLOCK);
			}
		}
	}

unlock:
	block_signals(SIG_BLOCK);
	if (cluster_locked && fs->fs_dlm_ctxt)
		ocfs2_release_cluster(fs);
	cluster_locked = 0;
	block_signals(SIG_UNBLOCK);

close:
	block_signals(SIG_BLOCK);
	if (fs && fs->fs_dlm_ctxt)
		ocfs2_shutdown_dlm(fs);
	block_signals(SIG_UNBLOCK);

	if (opts.vol_uuid)
		free(opts.vol_uuid);
	if (opts.vol_label)
		free(opts.vol_label);
	if (opts.device)
		free(opts.device);
	if (fs)
		ocfs2_close(fs);

	return ret;
}
