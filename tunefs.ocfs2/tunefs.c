/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
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

#include <tunefs.h>

#define WHOAMI "tunefs.ocfs2"

ocfs2_tune_opts opts;
ocfs2_filesys *fs_gbl = NULL;
static int cluster_locked = 0;
static int resize = 0;
static int online_resize = 0;
static uint64_t def_jrnl_size = 0;
static char old_uuid[OCFS2_VOL_UUID_LEN * 2 + 1];
static char new_uuid[OCFS2_VOL_UUID_LEN * 2 + 1];

static void usage(const char *progname)
{
	fprintf(stderr, "usage: %s [-J journal-options] [-L volume-label]\n"
			"\t\t[-M mount-type] [-N number-of-node-slots] [-Q query-fmt]\n"
			"\t\t[--update-cluster-stack]\n"
			"\t\t[-qSUvV] [--backup-super] [--list-sparse]\n"
			"\t\t[--fs-features=[no]sparse,...]] device [blocks-count]\n",
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
			ocfs2_shutdown_dlm(fs_gbl, WHOAMI);

		exit(1);
	}

	return ;
}

/* Call this with SIG_BLOCK to block and SIG_UNBLOCK to unblock */
void block_signals(int how)
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
	errcode_t ret = 0;

	static struct option long_options[] = {
		{ "label", 1, 0, 'L' },
		{ "node-slots", 1, 0, 'N' },
		{ "verbose", 0, 0, 'v' },
		{ "quiet", 0, 0, 'q' },
		{ "query", 1, 0, 'Q' },
		{ "version", 0, 0, 'V' },
		{ "journal-options", 0, 0, 'J'},
		{ "volume-size", 0, 0, 'S'},
		{ "uuid-reset", 0, 0, 'U'},
		{ "mount", 1, 0, 'M' },
		{ "backup-super", 0, 0, BACKUP_SUPER_OPTION },
		{ "update-cluster-stack", 0, 0, UPDATE_CLUSTER_OPTION },
		{ "list-sparse", 0, 0, LIST_SPARSE_FILES },
		{ "fs-features=", 1, 0, FEATURES_OPTION },
		{ 0, 0, 0, 0}
	};

	if (argc && *argv)
		opts.progname = basename(argv[0]);
	else
		opts.progname = strdup("tunefs.ocfs2");

	opts.prompt = 1;

	while (1) {
		c = getopt_long(argc, argv, "L:N:J:M:Q:SUvqVxb", long_options,
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
			} else if (opts.num_slots < 1) {
				com_err(opts.progname, 0,
					"Number of node slots must be at "
					"least 1");
				exit(1);
			}
			break;
		case 'Q':
			opts.queryfmt = strdup(optarg);
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

		case UPDATE_CLUSTER_OPTION:
			opts.update_cluster = 1;
			break;

		case LIST_SPARSE_FILES:
			opts.list_sparse = 1;
			break;

		case FEATURES_OPTION:
			ret = ocfs2_parse_feature(optarg,
						  &opts.set_feature,
						  &opts.clear_feature);
			if (ret) {
				com_err(opts.progname, ret,
					"when parsing --fs-features string");
				exit(1);
			}
			opts.feature_string = strdup(optarg);
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
	     opts.mount || opts.jrnl_size || resize ||
	     opts.list_sparse || opts.feature_string ||
	     opts.update_cluster)) {
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

	/*
	 * We don't allow list-sparse to be coexist with other tunefs
	 * options to keep things simple.
	 */
	if (opts.list_sparse &&
	    (opts.vol_label || opts.num_slots ||
	     opts.mount || opts.jrnl_size || resize || opts.backup_super ||
	     opts.feature_string || opts.update_cluster)) {
		com_err(opts.progname, 0, "Cannot list sparse files"
			" along with other tasks");
		exit(1);
	}

	/*
	 * We don't allow feature-modifation to be coexist with other tunefs
	 * options to keep things simple.
	 */
	if (opts.feature_string &&
	    (opts.vol_label || opts.num_slots ||
	     opts.mount || opts.jrnl_size || resize || opts.backup_super ||
	     opts.list_sparse || opts.update_cluster)) {
		com_err(opts.progname, 0, "Cannot modify fs features"
			" along with other tasks");
		exit(1);
	}

	/*
	 * We don't allow cluster information to be modified with other
	 * tunefs options to keep things simple.
	 */
	if (opts.update_cluster &&
	    (opts.vol_label || opts.num_slots || opts.feature_string ||
	     opts.mount || opts.jrnl_size || resize || opts.backup_super ||
	     opts.list_sparse)) {
		com_err(opts.progname, 0, "Cannot modify cluster stack"
			" along with other tasks");
		exit(1);
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
				ret = ocfs2_init_dir(fs, blkno,
						     fs->fs_sysdir_blkno);
				if (ret) {
					printf("\n");
					com_err(opts.progname, ret, "while "
						"initializing system directory");
					goto bail;
				}
			}

			/* Add the inode to the system dir */
			ret = ocfs2_link(fs, fs->fs_sysdir_blkno, fname, blkno,
					 ftype);
			if (ret) {
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

static void get_total_free_bits(struct ocfs2_group_desc *gd, uint16_t *bits)
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
	uint16_t bits;

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
				blkno, (uint64_t)di->i_blkno);
			goto bail;
		}

		if (gd->bg_chain != chain) {
			ret = OCFS2_ET_CORRUPT_CHAIN;
			com_err(opts.progname, ret, " - group descriptor at "
				"%"PRIu64" does not agree to the chain it "
				"belongs to in allocator %"PRIu64"",
				blkno, (uint64_t)di->i_blkno);
			goto bail;
		}

		get_total_free_bits(gd, &bits);
		if (bits != gd->bg_free_bits_count) {
			ret = OCFS2_ET_CORRUPT_CHAIN;
			com_err(opts.progname, ret, " - group descriptor at "
				"%"PRIu64" does not have a consistent free "
				"bit count", (uint64_t)blkno);
			goto bail;
		}

		if (gd->bg_bits > gd->bg_size * 8) {
			ret = OCFS2_ET_CORRUPT_CHAIN;
			com_err(opts.progname, ret, " - group descriptor at "
				"%"PRIu64" does not have a valid total bit "
				"count", (uint64_t)blkno);
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
			"descriptors", chain, (uint64_t)di->i_blkno);
		goto bail;

	}

	if (cr->c_free != free) {
		ret = OCFS2_ET_CORRUPT_CHAIN;
		com_err(opts.progname, ret, " - free bits for chain %u in "
			"allocator %"PRIu64" does not match its chained group "
			"descriptors", chain, (uint64_t)di->i_blkno);
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

	num = ocfs2_get_backup_super_offsets(fs, blocks,
					     ARRAY_SIZE(blocks));
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
	strncpy ((char *)OCFS2_RAW_SB(fs->fs_super)->s_label, opts.vol_label,
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

static errcode_t update_mount_type(ocfs2_filesys *fs, int *changed)
{
	errcode_t ret = 0;
	struct o2cb_cluster_desc desc;
	struct ocfs2_super_block *sb = OCFS2_RAW_SB(fs->fs_super);

	if (opts.mount == MOUNT_LOCAL) {
		sb->s_feature_incompat |=
			OCFS2_FEATURE_INCOMPAT_LOCAL_MOUNT;
		sb->s_feature_incompat &=
			~OCFS2_FEATURE_INCOMPAT_USERSPACE_STACK;
	} else if (opts.mount == MOUNT_CLUSTER) {
		ret = o2cb_init();
		if (ret)
			goto out;
		ret = o2cb_running_cluster_desc(&desc);
		if (ret)
			goto out;
		sb->s_feature_incompat &=
			~OCFS2_FEATURE_INCOMPAT_LOCAL_MOUNT;
		ret = ocfs2_set_cluster_desc(fs, &desc);
		o2cb_free_cluster_desc(&desc);
		if (ret)
			goto out;
	} else
		goto out;

	*changed = 1;

out:
	return ret;
}

static errcode_t update_cluster(ocfs2_filesys *fs)
{
	errcode_t ret;
	struct o2cb_cluster_desc desc;

	ret = o2cb_running_cluster_desc(&desc);
	if (!ret) {
		ret = ocfs2_set_cluster_desc(fs, &desc);
		o2cb_free_cluster_desc(&desc);
	}

	return ret;
}

static errcode_t update_slots(ocfs2_filesys *fs, int *changed)
{
	errcode_t ret = 0;
	int orig_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;

	block_signals(SIG_BLOCK);
	if (opts.num_slots > orig_slots)
		ret = add_slots(fs);
	else
		ret = remove_slots(fs);
	if (ret)
		goto unblock;

	OCFS2_RAW_SB(fs->fs_super)->s_max_slots = opts.num_slots;
	ret = ocfs2_format_slot_map(fs);
	if (!ret)
		*changed = 1;
	else
		OCFS2_RAW_SB(fs->fs_super)->s_max_slots = orig_slots;

unblock:
	block_signals(SIG_UNBLOCK);

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

static errcode_t update_backup_super(ocfs2_filesys *fs, uint64_t startblk,
				     uint64_t newblocks)
{
	errcode_t ret;
	int num, i;
	uint64_t *new_backup_super, blocks[OCFS2_MAX_BACKUP_SUPERBLOCKS];

	num = ocfs2_get_backup_super_offsets(fs, blocks,
					     ARRAY_SIZE(blocks));
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

	ret = ocfs2_set_backup_super_list(fs, new_backup_super, num);
	if (ret) {
		com_err(opts.progname, ret, "while backing up superblock.");
		goto bail;
	}

bail:
	return ret;
}

static void free_opts(void)
{
	if (opts.vol_uuid)
		free(opts.vol_uuid);
	if (opts.vol_label)
		free(opts.vol_label);
	if (opts.queryfmt)
		free(opts.queryfmt);
	if (opts.device)
		free(opts.device);
}

static errcode_t volume_check(ocfs2_filesys *fs)
{
	errcode_t ret;
	int dirty = 0;
	uint16_t max_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;

	/*
	 * online_resize can't coexist with other tasks, and it does't
	 * need other checks, so we just do the check and return.
	 */
	if (online_resize) {
		ret = online_resize_check(fs);
		if (ret)
			com_err(opts.progname, 0, "online resize check failed.");
		goto bail;
	}

	ret = journal_check(fs, &dirty, &def_jrnl_size);
	if (ret || dirty)
		goto bail;

	ret = 1;
	if (opts.list_sparse) {
		if (!ocfs2_sparse_alloc(OCFS2_RAW_SB(fs->fs_super))) {
			com_err(opts.progname, 0,
				"sparse_file flag check failed. ");
			goto bail;
		}
		printf("List all the sparse files in the volume\n");
	}

	if (opts.feature_string) {
		if (feature_check(fs)) {
			com_err(opts.progname, 0,
				"feature check failed. ");
			goto bail;
		}
		printf("Modify feature \"%s\" for the volume\n",
			opts.feature_string);
	}

	/* If operation requires touching the global bitmap, ensure it is good */
	/* This is to handle failed resize */
	if (opts.num_blocks || opts.num_slots || opts.jrnl_size ||
	    opts.backup_super) {
		if (global_bitmap_check(fs)) {
			com_err(opts.progname, 0, "Global bitmap check failed. "
				"Run fsck.ocfs2 -f <device>.");
			goto bail;
		}
	}

	/* check whether the block for backup superblock are used. */
	if (opts.backup_super) {
		if (backup_super_check(fs))
			goto bail;
	}

	/* remove slot check. */
	if (opts.num_slots && opts.num_slots < max_slots) {
		ret = remove_slot_check(fs);
		if (ret) {
			com_err(opts.progname, 0,
				"remove slot check failed. ");
			goto bail;
		}
	}

	ret = 0;
bail:
	return ret;
}

static void validate_parameter(ocfs2_filesys *fs)
{
	uint16_t max_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;
	uint64_t num_clusters;
	char *tmpstr;

	/* valid backup super. */
	if (opts.backup_super)
		printf("Adding backup superblock for the volume\n");

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
	max_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;
	if (opts.num_slots) {
		if (opts.num_slots > max_slots) {
			if (!opts.jrnl_size)
				opts.jrnl_size = def_jrnl_size;

		} else if (opts.num_slots == max_slots) {
			printf("Giving the same number of nodes. "
				"Ignore the change of slots.");
			opts.num_slots = 0;
		}

		if (opts.num_slots)
			printf("Changing number of node slots from %d to %d\n",
			       max_slots, opts.num_slots);
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
}

int main(int argc, char **argv)
{
	errcode_t ret = 0;
	ocfs2_filesys *fs = NULL;
	int open_flags;
	int upd_label = 0;
	int upd_uuid = 0;
	int upd_slots = 0;
	int upd_jrnls = 0;
	int upd_blocks = 0;
	int upd_mount = 0;
	int upd_incompat = 0;
	int upd_backup_super = 0;
	int upd_feature = 0;
	uint16_t max_slots;
	uint64_t old_blocks = 0;

	initialize_ocfs_error_table();
	initialize_o2dl_error_table();
	initialize_o2cb_error_table();

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	memset (&opts, 0, sizeof(opts));

	get_options(argc, argv);

	if (signal(SIGTERM, handle_signal) == SIG_ERR) {
		com_err(opts.progname, 0, "Could not set SIGTERM");
		exit(1);
	}

	if (signal(SIGINT, handle_signal) == SIG_ERR) {
		com_err(opts.progname, 0, "Could not set SIGINT");
		exit(1);
	}

	open_flags = OCFS2_FLAG_HEARTBEAT_DEV_OK;
	if (opts.queryfmt)
		open_flags |= OCFS2_FLAG_RO;
	else
		open_flags |= OCFS2_FLAG_RW | OCFS2_FLAG_STRICT_COMPAT_CHECK;

	ret = ocfs2_open(opts.device, open_flags, 0, 0, &fs); //O_EXCL?
	if (ret) {
		com_err(opts.progname, ret, "while opening device %s",
			opts.device);
		goto close;
	}
	fs_gbl = fs;

	if (opts.queryfmt) {
		print_query(opts.queryfmt);
		goto close;
	}

	if (OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat &
	    OCFS2_FEATURE_INCOMPAT_HEARTBEAT_DEV) {
		com_err(opts.progname, 0, "Heartbeat devices cannot be tuned, "
			"only re-formatted using mkfs.ocfs2");
		goto close;
	}

	if (OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat &
	    OCFS2_FEATURE_INCOMPAT_RESIZE_INPROG) {
		com_err(opts.progname, 0, "Aborted resize detected. "
			"Run fsck.ocfs2 -f <device>.\n");
		goto close;
	}

	if (OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat &
	    OCFS2_FEATURE_INCOMPAT_TUNEFS_INPROG) {
		fprintf(stderr, "Aborted tunefs operation detected. "
			"Run fsck.ocfs2 -f <device>.\n");
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

		ret = ocfs2_initialize_dlm(fs, WHOAMI);
		if (opts.update_cluster) {
			/* We have the right cluster, do nothing */
			if (!ret)
				goto close;
			if (ret == O2CB_ET_INVALID_STACK_NAME) {
				/* We expected this - why else ask for
				 * a cluster information update? */
				printf("Updating on-disk cluster information "
				       "to match the running cluster.\n"
				       "DANGER: YOU MUST BE ABSOLUTELY "
				       "SURE THAT NO OTHER NODE IS USING "
				       "THIS FILESYSTEM BEFORE MODIFYING "
				       "ITS CLUSTER CONFIGURATION.\n");
				goto skip_cluster_start;
			}
		}
		if (ret) {
			com_err(opts.progname, ret, "while initializing the dlm");
			goto close;
		}

		block_signals(SIG_BLOCK);
		ret = ocfs2_lock_down_cluster(fs);
		block_signals(SIG_UNBLOCK);
		if (!ret)
			cluster_locked = 1;
		else if (ret == O2DLM_ET_TRYLOCK_FAILED && resize) {
			/*
			 * We just set the flag here and more check and
			 * lock will be done later.
			 */
			online_resize = 1;
		} else {
			com_err(opts.progname, ret, "while locking down the cluster");
			goto close;
		}
	}

skip_cluster_start:
	/*
	 * We will use block cache in io. Now whether the cluster is locked or
	 * the volume is mount local, in both situation we can safely use cache.
	 * If io_init_cache failed, we will go on the tunefs work without
	 * the io_cache, so there is no check here.
	 */
	io_init_cache(fs->fs_io, ocfs2_extent_recs_per_eb(fs->fs_blocksize));

	ret = volume_check(fs);
	if (ret)
		goto unlock;

	validate_parameter(fs);

	if (!opts.vol_label && !opts.vol_uuid && !opts.num_slots &&
	    !opts.jrnl_size && !opts.num_blocks && !opts.mount &&
	    !opts.backup_super && !opts.list_sparse && !opts.feature_string &&
	    !opts.update_cluster) {
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

	/*
	 * We handle online resize seperately here, since it is
	 * not like tunefs operations.
	 */
	if (online_resize) {
		ret = online_resize_lock(fs);
		if (ret)
			goto close;

		ret = update_volume_size(fs, &upd_blocks, 1);
		if (ret) {
			com_err(opts.progname, ret,
				"while updating volume size");
			goto online_resize_unlock;
		}
		if (upd_blocks)
			printf("Resized volume\n");

		goto online_resize_unlock;
	}

	/* Set resize incompat flag on superblock */
	max_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;
	if (opts.num_blocks ||
	    (opts.num_slots && opts.num_slots < max_slots)) {
		if (opts.num_blocks)
			OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat |=
				OCFS2_FEATURE_INCOMPAT_RESIZE_INPROG;
		else {
			OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat |=
				OCFS2_FEATURE_INCOMPAT_TUNEFS_INPROG;
			OCFS2_RAW_SB(fs->fs_super)->s_tunefs_flag |=
				OCFS2_TUNEFS_INPROG_REMOVE_SLOT;
		}

		ret = ocfs2_write_primary_super(fs);
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
		/* Clear remove slot incompat flag on superblock */
		if (opts.num_slots < max_slots) {
			OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat &=
				~OCFS2_FEATURE_INCOMPAT_TUNEFS_INPROG;
			OCFS2_RAW_SB(fs->fs_super)->s_tunefs_flag &=
				~OCFS2_TUNEFS_INPROG_REMOVE_SLOT;
		}
		if (upd_slots)
			printf("Changed node slots\n");
	}

	/* change mount type */
	if (opts.mount) {
		ret = update_mount_type(fs, &upd_mount);
		if (ret) {
			com_err(opts.progname, ret,
				"while changing mount type");
			goto unlock;
		}
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
		old_blocks = fs->fs_blocks;
		ret = update_volume_size(fs, &upd_blocks, 0);
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

	/*
	 * list all the files in the volume, and since list_sparse
	 * option can coexist with others, we jump to the end after
	 * the work.
	 */
	if (opts.list_sparse) {
		ret = list_sparse(fs);
		if (ret) {
			com_err(opts.progname, ret,
				"while listing sparse files");
			goto unlock;
		}
	}

	/* change the feature in the super block.*/
	if (opts.feature_string) {
		ret = update_feature(fs);
		if (ret) {
			com_err(opts.progname, ret,
				"while updating feature in super block");
			goto unlock;
		}
		upd_feature = 1;
	}

	/* Update the cluster configuration */
	if (opts.update_cluster) {
		ret = update_cluster(fs);
		if (ret) {
			com_err(opts.progname, ret,
				"while updating cluster information");
			goto unlock;
		}
	}

	/* update the backup superblock. */
	if (opts.backup_super ||
	    (opts.num_blocks &&
	    OCFS2_HAS_COMPAT_FEATURE(OCFS2_RAW_SB(fs->fs_super),
				     OCFS2_FEATURE_COMPAT_BACKUP_SB))) {
		block_signals(SIG_BLOCK);
		ret = update_backup_super(fs, old_blocks, opts.num_blocks);
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
	    upd_mount || upd_backup_super || upd_feature) {
		block_signals(SIG_BLOCK);
		ret = ocfs2_write_super(fs);
		if (ret) {
			com_err(opts.progname, ret,
				"while writing superblock(s)");
			goto unlock;
		}
		block_signals(SIG_UNBLOCK);
		printf("Wrote Superblock(s)\n");
	}

online_resize_unlock:
	if (online_resize)
		online_resize_unlock(fs);
unlock:
	block_signals(SIG_BLOCK);
	if (cluster_locked && fs->fs_dlm_ctxt)
		ocfs2_release_cluster(fs);
	cluster_locked = 0;
	block_signals(SIG_UNBLOCK);

close:
	if (fs && fs->fs_io)
		io_destroy_cache(fs->fs_io);
	block_signals(SIG_BLOCK);
	if (fs && fs->fs_dlm_ctxt)
		ocfs2_shutdown_dlm(fs, WHOAMI);
	block_signals(SIG_UNBLOCK);

	free_clear_ctxt();

	free_opts();

	if (fs)
		ocfs2_close(fs);

	return ret;
}
