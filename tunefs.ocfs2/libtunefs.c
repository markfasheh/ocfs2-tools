/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * libtunefs.c
 *
 * Shared routines for the ocfs2 tunefs utility
 *
 * Copyright (C) 2004, 2008 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE /* for getopt_long and O_DIRECT */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <getopt.h>
#include <ctype.h>

#include "ocfs2/ocfs2.h"
#include "ocfs2/bitops.h"

#include "libtunefs.h"
#include "libtunefs_err.h"

#define WHOAMI "tunefs.ocfs2"
#define TUNEFS_OCFS2_LOCK_ENV		"_TUNEFS_OCFS2_LOCK"
#define TUNEFS_OCFS2_LOCK_ENV_LOCKED	"locked"
#define TUNEFS_OCFS2_LOCK_ENV_ONLINE	"online"

ocfs2_filesys *fs;
static char progname[PATH_MAX] = "(Unknown)";
static const char *usage_string;
static int cluster_locked;
static int verbosity = 1;
static int interactive = 0;
static uint32_t journal_clusters = 0;


/* If all verbosity is turned off, make sure com_err() prints nothing. */
static void quiet_com_err(const char *prog, long errcode, const char *fmt,
			  va_list args)
{
	return;
}

void tunefs_verbose(void)
{
	verbosity++;
	if (verbosity == 1)
		reset_com_err_hook();
}

void tunefs_quiet(void)
{
	if (verbosity == 1)
		set_com_err_hook(quiet_com_err);
	verbosity--;
}

static void vfverbosef(FILE *f, int level, const char *fmt, va_list args)
{
	if (level <= verbosity)
		vfprintf(f, fmt, args);
}

static void fverbosef(FILE *f, int level, const char *fmt, ...)
	__attribute__ ((format (printf, 3, 4)));
static void fverbosef(FILE *f, int level, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vfverbosef(f, level, fmt, args);
	va_end(args);
}

void verbosef(enum tunefs_verbosity_level level, const char *fmt, ...)
{
	va_list args;
	FILE *f = stderr;

	if (level & VL_FLAG_STDOUT) {
		f = stdout;
		level &= ~VL_FLAG_STDOUT;
	}

	va_start(args, fmt);
	vfverbosef(f, level, fmt, args);
	va_end(args);
}

void errorf(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	fverbosef(stderr, VL_ERR, "%s: ", progname);
	vfverbosef(stderr, VL_ERR, fmt, args);
	va_end(args);
}

void tcom_err(errcode_t code, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	com_err_va(progname, code, fmt, args);
	va_end(args);
}

static int vtunefs_interact(enum tunefs_verbosity_level level,
			    const char *fmt, va_list args)
{
	char *s, buffer[NAME_MAX];

	vfverbosef(stderr, level, fmt, args);

	s = fgets(buffer, sizeof(buffer), stdin);
	if (s && *s) {
		tolower(*s);
		if (*s == 'y')
			return 1;
	}

	return 0;
}

/* Pass this a question without a newline. */
int tunefs_interact(const char *fmt, ...)
{
	int rc;
	va_list args;

	if (!interactive)
		return 1;

	va_start(args, fmt);
	rc = vtunefs_interact(VL_ERR, fmt, args);
	va_end(args);

	return rc;
}

/* Only for "DON'T DO THIS WITHOUT REALLY CHECKING!" stuff */
int tunefs_interact_critical(const char *fmt, ...)
{
	int rc;
	va_list args;

	va_start(args, fmt);
	rc = vtunefs_interact(VL_CRIT, fmt, args);
	va_end(args);

	return rc;
}

static void handle_signal(int caught_sig)
{
	int exitp = 0, abortp = 0;
	static int segv_already = 0;

	switch (caught_sig) {
		case SIGQUIT:
			abortp = 1;
			/* FALL THROUGH */

		case SIGTERM:
		case SIGINT:
		case SIGHUP:
			errorf("Caught signal %d, exiting\n", caught_sig);
			exitp = 1;
			break;

		case SIGSEGV:
			errorf("Segmentation fault, exiting\n");
			exitp = 1;
			if (segv_already) {
				errorf("Segmentation fault loop detected\n");
				abortp = 1;
			} else
				segv_already = 1;
			break;

		default:
			errorf("Caught signal %d, ignoring\n", caught_sig);
			break;
	}

	if (!exitp)
		return;

	if (abortp)
		abort();

	tunefs_close();

	exit(1);
}

static int setup_signals(void)
{
	int rc = 0;
	struct sigaction act;

	act.sa_sigaction = NULL;
	act.sa_restorer = NULL;
	sigemptyset(&act.sa_mask);
	act.sa_handler = handle_signal;
#ifdef SA_INTERRUPT
	act.sa_flags = SA_INTERRUPT;
#endif

	rc += sigaction(SIGTERM, &act, NULL);
	rc += sigaction(SIGINT, &act, NULL);
	rc += sigaction(SIGHUP, &act, NULL);
	rc += sigaction(SIGQUIT, &act, NULL);
	rc += sigaction(SIGSEGV, &act, NULL);
	act.sa_handler = SIG_IGN;
	rc += sigaction(SIGPIPE, &act, NULL);  /* Get EPIPE instead */

	return rc;
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

void tunefs_block_signals(void)
{
	block_signals(SIG_BLOCK);
}

void tunefs_unblock_signals(void)
{
	block_signals(SIG_UNBLOCK);
}


static void setup_argv0(const char *argv0)
{
	char *pname;
	char pathtmp[PATH_MAX];

	/* This shouldn't care which basename(3) we get */
	snprintf(pathtmp, PATH_MAX, "%s", argv0);
	pname = basename(pathtmp);
	snprintf(progname, PATH_MAX, "%s", pname);
}

static errcode_t copy_argv(char **argv, char ***new_argv)
{
	int i;
	char **t_argv;

	for (i = 0; argv[i]; i++)
		;  /* Count argv */

	/* This is intentionally leaked */
	t_argv = malloc(sizeof(char *) * (i + 1));
	if (!t_argv)
		return TUNEFS_ET_NO_MEMORY;

	for (i = 0; argv[i]; i++)
		t_argv[i] = (char *)argv[i];
	t_argv[i] = NULL;

	*new_argv = t_argv;
	return 0;
}

/* All the +1 are to leave argv[0] in place */
static void shuffle_argv(int *argc, int optind, char **argv)
{
	int src, dst;
	int new_argc = *argc - optind + 1;

	for (src = optind, dst = 1; src < *argc; src++, dst++)
		argv[dst] = argv[src];
	if (dst != new_argc)
		verbosef(VL_DEBUG,
			 "dst is not new_argc %d %d\n", dst, new_argc);

	argv[dst] = NULL;
	*argc = new_argc;
}

static void tunefs_usage_internal(int error)
{
	FILE *f = stderr;

	if (!error)
		f = stdout;

	fverbosef(f, VL_ERR, "%s", usage_string ? usage_string : "(null)");
	fverbosef(f, VL_ERR,
		  "[opts] can be any mix of:\n"
		  "\t-i|--interactive\n"
		  "\t-v|--verbose (more than one increases verbosity)\n"
		  "\t-q|--quiet (more than one decreases verbosity)\n"
		  "\t-h|--help\n"
		  "\t-V|--version\n");
}

void tunefs_usage(void)
{
	tunefs_usage_internal(1);
}

extern int optind, opterr, optopt;
extern char *optarg;
static errcode_t tunefs_parse_core_options(int *argc, char ***argv)
{
	errcode_t err;
	int c;
	char **new_argv;
	int print_usage = 0, print_version = 0;
	char error[PATH_MAX];
	static struct option long_options[] = {
		{ "help", 0, NULL, 'h' },
		{ "version", 0, NULL, 'V' },
		{ "verbose", 0, NULL, 'v' },
		{ "quiet", 0, NULL, 'q' },
		{ "interactive", 0, NULL, 'i'},
		{ 0, 0, 0, 0}
	};

	setup_argv0(*argv[0]);
	err = copy_argv(*argv, &new_argv);
	if (err)
		return err;

	opterr = 0;
	error[0] = '\0';
	while ((c = getopt_long(*argc, new_argv,
				":hVvqi", long_options, NULL)) != EOF) {
		switch (c) {
			case 'h':
				print_usage = 1;
				break;

			case 'V':
				print_version = 1;
				break;

			case 'v':
				tunefs_verbose();
				break;

			case 'q':
				tunefs_quiet();
				break;

			case 'i':
				interactive = 1;
				break;

			case '?':
				snprintf(error, PATH_MAX,
					 "Invalid option: \'-%c\'",
					 optopt);
				print_usage = 1;
				break;

			case ':':
				snprintf(error, PATH_MAX,
					 "Option \'-%c\' requires an argument",
					 optopt);
				print_usage = 1;
				break;

			default:
				snprintf(error, PATH_MAX,
					 "Shouldn't get here %c %c",
					 optopt, c);
				break;
		}

		if (*error)
			break;
	}

	if (*error)
		errorf("%s\n", error);

	if (print_version)
		verbosef(VL_ERR, "%s %s\n", progname, VERSION);

	if (print_usage)
		tunefs_usage_internal(*error != '\0');

	if (print_usage || print_version)
		exit(0);

	if (*error)
		exit(1);

	shuffle_argv(argc, optind, new_argv);
	*argv = new_argv;

	return 0;
}

errcode_t tunefs_init(int *argc, char ***argv, const char *usage)
{
	initialize_tune_error_table();
	initialize_ocfs_error_table();
	initialize_o2dl_error_table();
	initialize_o2cb_error_table();

	usage_string = usage;

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	if (setup_signals())
		return TUNEFS_ET_SIGNALS_FAILED;

	return tunefs_parse_core_options(argc, argv);
}

static errcode_t tunefs_set_lock_env(const char *status)
{
	errcode_t err = 0;

	if (!status) {
		if (unsetenv(TUNEFS_OCFS2_LOCK_ENV))
			err = TUNEFS_ET_INTERNAL_FAILURE;
	} else if (setenv(TUNEFS_OCFS2_LOCK_ENV, status, 1))
		err = TUNEFS_ET_INTERNAL_FAILURE;

	return err;
}

static errcode_t tunefs_get_lock_env(void)
{
	errcode_t err = TUNEFS_ET_INVALID_STACK_NAME;
	int parent_locked = 0;
	char *lockenv = getenv(TUNEFS_OCFS2_LOCK_ENV);

	if (lockenv) {
		parent_locked = 1;
		if (!strcmp(lockenv, TUNEFS_OCFS2_LOCK_ENV_ONLINE))
			err = TUNEFS_ET_PERFORM_ONLINE;
		else if (!strcmp(lockenv, TUNEFS_OCFS2_LOCK_ENV_LOCKED))
			err = 0;
		else
			parent_locked = 0;
	}

	if (parent_locked)
		snprintf(progname, PATH_MAX, "%s",  PROGNAME);

	return err;
}

static errcode_t tunefs_unlock_cluster(void)
{
	errcode_t tmp, err = 0;

	if (!fs)
		return TUNEFS_ET_INTERNAL_FAILURE;

	if (cluster_locked && fs->fs_dlm_ctxt) {
		tunefs_block_signals();
		err = ocfs2_release_cluster(fs);
		tunefs_unblock_signals();
		cluster_locked = 0;
	}

	if (fs->fs_dlm_ctxt) {
		tmp = ocfs2_shutdown_dlm(fs, WHOAMI);
		if (!err)
			err = tmp;
	}

	tmp = tunefs_set_lock_env(NULL);
	if (!err)
		err = tmp;

	return err;
}

static errcode_t tunefs_lock_cluster(int flags)
{
	errcode_t tmp, err = 0;

	if (!ocfs2_mount_local(fs)) {
		/* Has a parent process has done the locking for us? */
		err = tunefs_get_lock_env();
		if (!err ||
		    ((flags & TUNEFS_FLAG_ONLINE) &&
		     (err == TUNEFS_ET_PERFORM_ONLINE)))
			goto out_err;

		err = o2cb_init();
		if (err)
			goto out_err;

		err = ocfs2_initialize_dlm(fs, WHOAMI);
		if (flags & TUNEFS_FLAG_NOCLUSTER) {
			/* We have the right cluster, do nothing */
			if (!err)
				goto out_set;
			if (err == O2CB_ET_INVALID_STACK_NAME) {
				/*
				 * We expected this - why else ask for
				 * TUNEFS_FLAG_NOCLUSTER?
				 *
				 * Note that this is distinct from the O2CB
				 * error, as that is a real error when
				 * TUNEFS_FLAG_NOCLUSTER is not specified.
				 */
				err = TUNEFS_ET_INVALID_STACK_NAME;
				goto out_set;
			}
		}

		if (err)
			goto out_err;

		tunefs_block_signals();
		err = ocfs2_lock_down_cluster(fs);
		tunefs_unblock_signals();
		if (!err)
			cluster_locked = 1;
		else if ((err == O2DLM_ET_TRYLOCK_FAILED) &&
			 (flags & TUNEFS_FLAG_ONLINE)) {
			err = TUNEFS_ET_PERFORM_ONLINE;
		} else {
			ocfs2_shutdown_dlm(fs, WHOAMI);
			goto out_err;
		}
	}

out_set:
	if (!err && cluster_locked)
		tmp = tunefs_set_lock_env(TUNEFS_OCFS2_LOCK_ENV_LOCKED);
	else if (err == TUNEFS_ET_PERFORM_ONLINE)
		tmp = tunefs_set_lock_env(TUNEFS_OCFS2_LOCK_ENV_ONLINE);
	else
		tmp = tunefs_set_lock_env(NULL);
	if (tmp) {
		err = tmp;
		/*
		 * We safely call unlock here - the state is right.  Ignore
		 * the result to pass the error from set_lock_env()
		 */
		tunefs_unlock_cluster();
	}

out_err:
	return err;
}

static int tunefs_count_free_bits(struct ocfs2_group_desc *gd)
{
	int end = 0;
	int start;
	int bits = 0;

	while (end < gd->bg_bits) {
		start = ocfs2_find_next_bit_clear(gd->bg_bitmap, gd->bg_bits, end);
		if (start >= gd->bg_bits)
			break;
		end = ocfs2_find_next_bit_set(gd->bg_bitmap, gd->bg_bits, start);
		bits += (end - start);
	}

	return bits;
}

static errcode_t tunefs_validate_chain_group(ocfs2_filesys *fs,
					     struct ocfs2_dinode *di,
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
		verbosef(VL_LIB,
			 "%s while allocating a buffer for chain group "
			 "validation\n",
			 error_message(ret));
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
			verbosef(VL_LIB,
				 "%s while reading chain group descriptor "
				 "at block %"PRIu64"\n",
				 error_message(ret), blkno);
			goto bail;
		}

		gd = (struct ocfs2_group_desc *)buf;

		if (gd->bg_parent_dinode != di->i_blkno) {
			ret = OCFS2_ET_CORRUPT_CHAIN;
			verbosef(VL_LIB,
				 "Chain allocator at block %"PRIu64" is "
				 "corrupt.  It contains group descriptor "
				 "at %"PRIu64", but that descriptor says "
				 "it belongs to allocator %"PRIu64"\n",
				 (uint64_t)di->i_blkno, blkno,
				 (uint64_t)gd->bg_parent_dinode);
			goto bail;
		}

		if (gd->bg_chain != chain) {
			ret = OCFS2_ET_CORRUPT_CHAIN;
			verbosef(VL_LIB,
				 "Chain allocator at block %"PRIu64" is "
				 "corrupt.  Group descriptor at %"PRIu64" "
				 "was found on chain %u, but it says it "
				 "belongs to chain %u\n",
				 (uint64_t)di->i_blkno, blkno,
				 chain, gd->bg_chain);
			goto bail;
		}

		bits = tunefs_count_free_bits(gd);
		if (bits != gd->bg_free_bits_count) {
			ret = OCFS2_ET_CORRUPT_CHAIN;
			verbosef(VL_LIB,
				 "Chain allocator at block %"PRIu64" is "
				 "corrupt.  Group descriptor at %"PRIu64" "
				 "has %u free bits but says it has %u\n",
				 (uint64_t)di->i_blkno, (uint64_t)blkno,
				 bits, gd->bg_free_bits_count);
			goto bail;
		}

		if (gd->bg_bits > gd->bg_size * 8) {
			ret = OCFS2_ET_CORRUPT_CHAIN;
			verbosef(VL_LIB,
				 "Chain allocator at block %"PRIu64" is "
				 "corrupt.  Group descriptor at %"PRIu64" "
				 "can only hold %u bits, but it claims to "
				 "have %u\n",
				 (uint64_t)di->i_blkno, (uint64_t)blkno,
				 gd->bg_size * 8, gd->bg_bits);
			goto bail;
		}

		if (gd->bg_free_bits_count >= gd->bg_bits) {
			ret = OCFS2_ET_CORRUPT_CHAIN;
			verbosef(VL_LIB,
				 "Chain allocator at block %"PRIu64" is "
				 "corrupt.  Group descriptor at %"PRIu64" "
				 "claims to have more free bits than "
				 "total bits\n",
				 (uint64_t)di->i_blkno, (uint64_t)blkno);
			goto bail;
		}

		total += gd->bg_bits;
		free += gd->bg_free_bits_count;
		blkno = gd->bg_next_group;
	}

	if (cr->c_total != total) {
		ret = OCFS2_ET_CORRUPT_CHAIN;
		verbosef(VL_LIB,
			 "Chain allocator at block %"PRIu64" is corrupt. "
			 "It contains %u total bits, but it says it has "
			 "%u\n",
			 (uint64_t)di->i_blkno, total, cr->c_total);
		goto bail;

	}

	if (cr->c_free != free) {
		ret = OCFS2_ET_CORRUPT_CHAIN;
		verbosef(VL_LIB,
			 "Chain allocator at block %"PRIu64" is corrupt. "
			 "It contains %u free bits, but it says it has "
			 "%u\n",
			 (uint64_t)di->i_blkno, free, cr->c_free);
		goto bail;
	}

bail:
	if (buf)
		ocfs2_free(&buf);

	return ret;
}


static errcode_t tunefs_global_bitmap_check(ocfs2_filesys *fs)
{
	errcode_t ret = 0;
	uint64_t bm_blkno = 0;
	char *buf = NULL;
	struct ocfs2_chain_list *cl;
	struct ocfs2_dinode *di;
	int i;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret) {
		verbosef(VL_LIB,
			 "%s while allocating an inode buffer to validate "
			 "the global bitmap\n",
			 error_message(ret));
		goto bail;
	}

	ret = ocfs2_lookup_system_inode(fs, GLOBAL_BITMAP_SYSTEM_INODE, 0,
					&bm_blkno);
	if (ret) {
		verbosef(VL_LIB,
			 "%s while looking up the global bitmap inode\n",
			 error_message(ret));
		goto bail;
	}

	ret = ocfs2_read_inode(fs, bm_blkno, buf);
	if (ret) {
		verbosef(VL_LIB,
			 "%s while reading the global bitmap inode at "
			 "block %"PRIu64"",
			 error_message(ret), bm_blkno);
		goto bail;
	}

	di = (struct ocfs2_dinode *)buf;
	cl = &(di->id2.i_chain);

	for (i = 0; i < cl->cl_next_free_rec; ++i) {
		ret = tunefs_validate_chain_group(fs, di, i);
		if (ret)
			goto bail;
	}

bail:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

static errcode_t tunefs_journal_check(ocfs2_filesys *fs)
{
	errcode_t ret;
	char *buf = NULL;
	uint64_t blkno;
	struct ocfs2_dinode *di;
	int i, dirty = 0;
	uint16_t max_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret) {
		verbosef(VL_LIB,
			"%s while allocating a block during journal "
			"check\n",
			error_message(ret));
		goto bail;
	}

	for (i = 0; i < max_slots; ++i) {
		ret = ocfs2_lookup_system_inode(fs, JOURNAL_SYSTEM_INODE, i,
						&blkno);
		if (ret) {
			verbosef(VL_LIB,
				 "%s while looking up journal inode for "
				 "slot %u during journal check\n",
				 error_message(ret), i);
			goto bail;
		}

		ret = ocfs2_read_inode(fs, blkno, buf);
		if (ret) {
			verbosef(VL_LIB,
				 "%s while reading inode %"PRIu64" during "
				 " journal check",
				 error_message(ret), blkno);
			goto bail;
		}

		di = (struct ocfs2_dinode *)buf;

		if (di->i_clusters > journal_clusters)
			journal_clusters = di->i_clusters;

		dirty = di->id1.journal1.ij_flags & OCFS2_JOURNAL_DIRTY_FL;
		if (dirty) {
			ret = TUNEFS_ET_JOURNAL_DIRTY;
			verbosef(VL_LIB,
				 "Node slot %d's journal is dirty. Run "
				 "fsck.ocfs2 to replay all dirty journals.",
				 i);
			break;
		}
	}

bail:
	if (buf)
		ocfs2_free(&buf);

	return ret;
}

errcode_t tunefs_open(const char *device, int flags)
{
	int rw = flags & TUNEFS_FLAG_RW;
	errcode_t err, tmp;
	int open_flags;

	verbosef(VL_LIB, "Opening device \"%s\"\n", device);

	open_flags = OCFS2_FLAG_HEARTBEAT_DEV_OK;
	if (rw)
		open_flags |= OCFS2_FLAG_RW | OCFS2_FLAG_STRICT_COMPAT_CHECK;
	else
		open_flags |= OCFS2_FLAG_RO;

	err = ocfs2_open(device, open_flags, 0, 0, &fs);
	if (err)
		goto out;

	if (!rw)
		goto out;

	if (OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat &
	    OCFS2_FEATURE_INCOMPAT_HEARTBEAT_DEV) {
		err = TUNEFS_ET_HEARTBEAT_DEV;
		goto out;
	}

	if (OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat &
	    OCFS2_FEATURE_INCOMPAT_RESIZE_INPROG) {
		err = TUNEFS_ET_RESIZE_IN_PROGRESS;
		goto out;
	}

	if (OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat &
	    OCFS2_FEATURE_INCOMPAT_TUNEFS_INPROG) {
		err = TUNEFS_ET_TUNEFS_IN_PROGRESS;
		goto out;
	}

	err = tunefs_lock_cluster(flags);
	if (err &&
	    (err != TUNEFS_ET_INVALID_STACK_NAME) &&
	    (err != TUNEFS_ET_PERFORM_ONLINE))
		goto out;

	/*
	 * We will use block cache in io.  Now, whether the cluster is
	 * locked or the volume is mount local, in both situation we can
	 * safely use cache.  If io_init_cache failed, we will go on the
	 * tunefs work without the io_cache, so there is no check here.
	 */
	io_init_cache(fs->fs_io, ocfs2_extent_recs_per_eb(fs->fs_blocksize));

	/* Offline operations need clean journals */
	if (err != TUNEFS_ET_PERFORM_ONLINE) {
		tmp = tunefs_journal_check(fs);
		/* Allocating operations should validate the bitmap */
		if (!tmp && (flags & TUNEFS_FLAG_ALLOCATION))
			tmp = tunefs_global_bitmap_check(fs);
		if (tmp) {
			err = tmp;
			tunefs_unlock_cluster();
		}
	}

out:
	if (err &&
	    (err != TUNEFS_ET_INVALID_STACK_NAME) &&
	    (err != TUNEFS_ET_PERFORM_ONLINE)) {
		if (fs) {
			ocfs2_close(fs);
			fs = NULL;
		}
		verbosef(VL_LIB, "Open of device \"%s\" failed\n", device);
	} else
		verbosef(VL_LIB, "Device \"%s\" opened\n", device);

	return err;
}

errcode_t tunefs_close(void)
{
	errcode_t tmp, err = 0;

	/*
	 * We want to clean up everything we can even if there
	 * are errors, but we preserve the first error we get.
	 */
	if (fs) {
		verbosef(VL_LIB, "Closing device \"%s\"\n", fs->fs_devname);
		err = tunefs_unlock_cluster();
		tmp = ocfs2_close(fs);
		if (!err)
			err = tmp;

		if (!err)
			verbosef(VL_LIB, "Device closed\n");
		else
			verbosef(VL_LIB, "Close of device failed\n");
		fs = NULL;
	}

	return err;
}

errcode_t tunefs_set_in_progress(ocfs2_filesys *fs, int flag)
{
	/* RESIZE is a special case due for historical reasons */
	if (flag == OCFS2_FEATURE_INCOMPAT_RESIZE_INPROG) {
		OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat |=
			OCFS2_FEATURE_INCOMPAT_RESIZE_INPROG;
	} else {
		OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat |=
			OCFS2_FEATURE_INCOMPAT_TUNEFS_INPROG;
		OCFS2_RAW_SB(fs->fs_super)->s_tunefs_flag |= flag;
	}

	return ocfs2_write_primary_super(fs);
}

errcode_t tunefs_clear_in_progress(ocfs2_filesys *fs, int flag)
{
	/* RESIZE is a special case due for historical reasons */
	if (flag == OCFS2_FEATURE_INCOMPAT_RESIZE_INPROG) {
		OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat &=
			~OCFS2_FEATURE_INCOMPAT_RESIZE_INPROG;
	} else {
		OCFS2_RAW_SB(fs->fs_super)->s_tunefs_flag &= ~flag;
		if (OCFS2_RAW_SB(fs->fs_super)->s_tunefs_flag == 0)
			OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat &=
				~OCFS2_FEATURE_INCOMPAT_TUNEFS_INPROG;
	}

	return ocfs2_write_primary_super(fs);
}

errcode_t tunefs_set_journal_size(ocfs2_filesys *fs, uint64_t new_size)
{
	errcode_t ret = 0;
	char jrnl_file[OCFS2_MAX_FILENAME_LEN];
	uint64_t blkno;
	int i;
	int max_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;
	uint32_t num_clusters;
	char *buf = NULL;
	struct ocfs2_dinode *di;

	num_clusters =
		ocfs2_clusters_in_blocks(fs,
					 ocfs2_blocks_in_bytes(fs,
							       new_size));

	/* If no size was passed in, use the size we found at open() */
	if (!num_clusters)
		num_clusters = journal_clusters;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret) {
		verbosef(VL_LIB,
			 "%s while allocating inode buffer for journal "
			 "resize\n",
			 error_message(ret));
		return ret;
	}

	for (i = 0; i < max_slots; ++i) {
		ocfs2_sprintf_system_inode_name(jrnl_file,
						OCFS2_MAX_FILENAME_LEN,
						JOURNAL_SYSTEM_INODE, i);
		ret = ocfs2_lookup_system_inode(fs, JOURNAL_SYSTEM_INODE, i,
						&blkno);
		if (ret) {
			verbosef(VL_LIB,
				 "%s while looking up \"%s\" during "
				 "journal resize\n",
				 error_message(ret),
				 jrnl_file);
			goto bail;
		}

		ret = ocfs2_read_inode(fs, blkno, buf);
		if (ret) {
			verbosef(VL_LIB,
				 "%s while reading journal inode "
				 "%"PRIu64" for resizing\n",
				 error_message(ret), blkno);
			goto bail;
		}

		di = (struct ocfs2_dinode *)buf;
		if (num_clusters == di->i_clusters)
			continue;

		verbosef(VL_LIB,
			 "Resizing journal \"%s\" to %"PRIu32" clusters\n",
			 jrnl_file, num_clusters);
		ret = ocfs2_make_journal(fs, blkno, num_clusters);
		if (ret) {
			verbosef(VL_LIB,
				 "%s while resizing \"%s\" at block "
				 "%"PRIu64" to %"PRIu32" clusters\n",
				 error_message(ret), jrnl_file, blkno,
				 num_clusters);
			goto bail;
		}
		verbosef(VL_LIB, "Successfully resized journal \"%s\"\n",
			 jrnl_file);
	}

bail:
	if (buf)
		ocfs2_free(&buf);

	return ret;
}


#ifdef DEBUG_EXE

#define DEBUG_PROGNAME "debug_libtunefs"
int parent = 0;


static void closeup(const char *device)
{
	errcode_t err;

	verbosef(VL_OUT, "success\n");
	err = tunefs_close();
	if (err)  {
		tcom_err(err, "- Unable to close device \"%s\".", device);
	}
}

int main(int argc, char *argv[])
{
	errcode_t err;
	const char *device;

	err = tunefs_init(&argc, &argv,
			  "Usage: debug_libtunefs [-p] <device>\n");
	if (err) {
		tcom_err(err, "while initializing tunefs");
		return 1;
	}

	if (argc > 3) {
		errorf("Too many arguments\n");
		tunefs_usage();
		return 1;
	}
	if (argc == 3) {
		if (strcmp(argv[1], "-p")) {
			errorf("Invalid argument: \'%s\'\n", argv[1]);
			tunefs_usage();
			return 1;
		}
		parent = 1;
		device = argv[2];
	} else if ((argc == 2) &&
		   strcmp(argv[1], "-p")) {
		device = argv[1];
	} else {
		errorf("Device must be specified\n");
		tunefs_usage();
		return 1;
	}

	verbosef(VL_OUT, "Opening device \"%s\" read-only... ", device);
	err = tunefs_open(device, TUNEFS_FLAG_RO);
	if (err) {
		verbosef(VL_OUT, "failed\n");
		tcom_err(err, "- Unable to open device \"%s\" read-only.",
			 device);
	} else
		closeup(device);

	verbosef(VL_OUT, "Opening device \"%s\" read-write... ", device);
	err = tunefs_open(device, TUNEFS_FLAG_RW);
	if (err) {
		verbosef(VL_OUT, "failed\n");
		tcom_err(err, "- Unable to open device \"%s\" read-write.",
			 device);
	} else
		closeup(device);

	verbosef(VL_OUT,
		 "Opening device \"%s\" for an online operation... ",
		 device);
	err = tunefs_open(device, TUNEFS_FLAG_RW | TUNEFS_FLAG_ONLINE);
	if (err == TUNEFS_ET_PERFORM_ONLINE) {
		closeup(device);
		verbosef(VL_OUT, "Operation would have been online\n");
	} else if (!err) {
		closeup(device);
		verbosef(VL_OUT, "Operation would have been offline\n");
	} else {
		verbosef(VL_OUT, "failed\n");
		tcom_err(err, "- Unable to open device \"%s\" read-write.",
			 device);
	}

	verbosef(VL_OUT,
		 "Opening device \"%s\" for a stackless operation... ",
		 device);
	err = tunefs_open(device, TUNEFS_FLAG_RW | TUNEFS_FLAG_NOCLUSTER);
	if (err == TUNEFS_ET_INVALID_STACK_NAME) {
		closeup(device);
		verbosef(VL_OUT, "Expected cluster stack mismatch found\n");
	} else if (!err) {
		closeup(device);
		verbosef(VL_OUT, "Cluster stacks already match\n");
	} else {
		verbosef(VL_OUT, "failed\n");
		tcom_err(err, "- Unable to open device \"%s\" read-write.",
			 device);
	}

	return 0;
}


#endif /* DEBUG_EXE */

