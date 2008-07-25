/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * libocfs2ne.c
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
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <getopt.h>
#include <ctype.h>
#include <assert.h>

#include "ocfs2/ocfs2.h"
#include "ocfs2/bitops.h"

#include "libocfs2ne.h"
#include "libocfs2ne_err.h"

#define WHOAMI "tunefs.ocfs2"


/*
 * Keeps track of how ocfs2ne sees the filesystem.  This structure is
 * filled in by the master ocfs2_filesys (the first caller to
 * tunefs_open()).  Every other ocfs2_filesys refers to it.
 */
struct tunefs_filesystem_state {
	/* The master ocfs2_filesys (first tunefs_open()) */
	ocfs2_filesys	*ts_master;

	/*
	 * When a single-node (local) filesystem is opened, we prevent
	 * concurrent mount(2) by opening the device O_EXCL.  This is the
	 * fd we used.  The value is -1 for cluster-aware filesystems.
	 */
	int		ts_local_fd;

	/*
	 * Already-mounted filesystems can only do online operations.
	 * This is the fd we send ioctl(2)s to.  If the filesystem isn't
	 * in use, this is -1.
	 */
	int		ts_online_fd;

	/*
	 * Do we have the cluster locked?  This can be zero if we're a
	 * local filesystem.  If it is non-zero, ts_master->fs_dlm_ctxt
	 * must be valid.
	 */
	int		ts_cluster_locked;

	/* Non-zero if we've ever mucked with the allocator */
	int		ts_allocation;

	/* Size of the largest journal seen in tunefs_journal_check() */
	uint32_t	ts_journal_clusters;
};

struct tunefs_private {
	struct list_head		tp_list;
	ocfs2_filesys			*tp_fs;

	/* All tunefs_privates point to the master state. */
	struct tunefs_filesystem_state	*tp_state;

	/* Flags passed to tunefs_open() for this ocfs2_filesys */
	int				tp_open_flags;
};

/* List of all ocfs2_filesys objects opened by tunefs_open() */
static LIST_HEAD(fs_list);

static char progname[PATH_MAX] = "(Unknown)";
static int verbosity = 1;
static int interactive = 0;

/* Refcount for calls to tunefs_[un]block_signals() */
static unsigned int blocked_signals_count;

/* For DEBUG_EXE programs */
static const char *usage_string;

static inline struct tunefs_private *to_private(ocfs2_filesys *fs)
{
	return fs->fs_private;
}

static struct tunefs_filesystem_state *tunefs_get_master_state(void)
{
	struct tunefs_filesystem_state *s = NULL;
	struct tunefs_private *tp;

	if (!list_empty(&fs_list)) {
		tp = list_entry(fs_list.prev, struct tunefs_private,
			       tp_list);
		s = tp->tp_state;
	}

	return s;
}

static struct tunefs_filesystem_state *tunefs_get_state(ocfs2_filesys *fs)
{
	struct tunefs_private *tp = to_private(fs);

	return tp->tp_state;
}

static errcode_t tunefs_set_state(ocfs2_filesys *fs)
{
	errcode_t err = 0;
	struct tunefs_private *tp = to_private(fs);
	struct tunefs_filesystem_state *s = tunefs_get_master_state();

	if (!s) {
		err = ocfs2_malloc0(sizeof(struct tunefs_filesystem_state),
				    &s);
		if (!err) {
			s->ts_local_fd = -1;
			s->ts_online_fd = -1;
			s->ts_master = fs;
		} else
			s = NULL;
	}

	tp->tp_state = s;

	return err;
}

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
		*s = tolower(*s);
		if (*s == 'y')
			return 1;
	}

	return 0;
}

void tunefs_interactive(void)
{
	interactive = 1;
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

static void tunefs_close_all(void)
{
	struct list_head *pos, *n;
	struct tunefs_private *tp;

	list_for_each_safe(pos, n, &fs_list) {
		tp = list_entry(pos, struct tunefs_private, tp_list);
		tunefs_close(tp->tp_fs);
	}
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

	tunefs_close_all();

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
	if (!blocked_signals_count)
		block_signals(SIG_BLOCK);
	blocked_signals_count++;
}

void tunefs_unblock_signals(void)
{
	if (blocked_signals_count) {
		blocked_signals_count--;
		if (!blocked_signals_count)
			block_signals(SIG_UNBLOCK);
	} else
		errorf("Trying to unblock signals, but signals were not "
		       "blocked\n");
}

void tunefs_version(void)
{
	verbosef(VL_ERR, "%s %s\n", progname, VERSION);
}

const char *tunefs_progname(void)
{
	return progname;
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

static void tunefs_debug_usage(int error)
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

extern int optind, opterr, optopt;
extern char *optarg;
static void tunefs_parse_core_options(int *argc, char ***argv, char *usage)
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

	usage_string = usage;
	err = copy_argv(*argv, &new_argv);
	if (err) {
		tcom_err(err, "while processing command-line arguments");
		exit(1);
	}

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
				tunefs_interactive();
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
		tunefs_debug_usage(*error != '\0');

	if (print_usage || print_version)
		exit(0);

	if (*error)
		exit(1);

	shuffle_argv(argc, optind, new_argv);
	*argv = new_argv;
}

void tunefs_init(const char *argv0)
{
	initialize_tune_error_table();
	initialize_ocfs_error_table();
	initialize_o2dl_error_table();
	initialize_o2cb_error_table();

	setup_argv0(argv0);

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	if (setup_signals()) {
		errorf("%s\n", error_message(TUNEFS_ET_SIGNALS_FAILED));
		exit(1);
	}
}

errcode_t tunefs_dlm_lock(ocfs2_filesys *fs, const char *lockid,
			  int flags, enum o2dlm_lock_level level)
{
	struct tunefs_filesystem_state *state = tunefs_get_state(fs);

	if (ocfs2_mount_local(fs))
		return 0;

	return o2dlm_lock(state->ts_master->fs_dlm_ctxt, lockid, flags,
			  level);
}

errcode_t tunefs_dlm_unlock(ocfs2_filesys *fs, char *lockid)
{
	struct tunefs_filesystem_state *state = tunefs_get_state(fs);

	if (ocfs2_mount_local(fs))
		return 0;

	return o2dlm_unlock(state->ts_master->fs_dlm_ctxt, lockid);
}

/*
 * Single-node filesystems need to prevent mount(8) from happening
 * while tunefs.ocfs2 is running.  bd_claim does this for us when we
 * open O_EXCL.
 */
static errcode_t tunefs_lock_local(ocfs2_filesys *fs, int flags)
{
	errcode_t err = 0;
	int mount_flags;
	int rc;
	struct tunefs_filesystem_state *state = tunefs_get_state(fs);

	if (state->ts_local_fd > -1)
		return 0;

	rc = open64(fs->fs_devname, O_RDWR | O_EXCL);
	if (rc < 0) {
		if (errno == EBUSY) {
			/* bd_claim has a hold, let's see if it's ocfs2 */
			err = ocfs2_check_if_mounted(fs->fs_devname,
						     &mount_flags);
			if (!err) {
				if (!(mount_flags & OCFS2_MF_MOUNTED) ||
				    (mount_flags & OCFS2_MF_READONLY) ||
				    (mount_flags & OCFS2_MF_SWAP) ||
				    !(flags & TUNEFS_FLAG_ONLINE))
					err = TUNEFS_ET_DEVICE_BUSY;
				else
					err = TUNEFS_ET_PERFORM_ONLINE;
			}
		} else if (errno == ENOENT)
			err = OCFS2_ET_NAMED_DEVICE_NOT_FOUND;
		else
			err = OCFS2_ET_IO;
	} else
		state->ts_local_fd = rc;

	return err;
}

static void tunefs_unlock_local(ocfs2_filesys *fs)
{
	struct tunefs_filesystem_state *state = tunefs_get_state(fs);

	assert(state->ts_master == fs);
	if (state->ts_local_fd > -1) {
		close(state->ts_local_fd);  /* Don't care about errors */
		state->ts_local_fd = -1;
	}
}

static errcode_t tunefs_unlock_cluster(ocfs2_filesys *fs)
{
	errcode_t tmp, err = 0;
	struct tunefs_filesystem_state *state = tunefs_get_state(fs);

	assert(state->ts_master == fs);
	if (state->ts_cluster_locked) {
		assert(fs->fs_dlm_ctxt);

		tunefs_block_signals();
		err = ocfs2_release_cluster(fs);
		tunefs_unblock_signals();
		state->ts_cluster_locked = 0;
	}

	/* We shut down the dlm regardless of err */
	if (fs->fs_dlm_ctxt) {
		tmp = ocfs2_shutdown_dlm(fs, WHOAMI);
		if (!err)
			err = tmp;
	}

	return err;
}

/*
 * We only unlock if we're closing the master filesystem.  We unlock
 * both local and cluster locks, because we may have started as a local
 * filesystem, then switched to a cluster filesystem in the middle.
 */
static errcode_t tunefs_unlock_filesystem(ocfs2_filesys *fs)
{
	errcode_t err = 0;
	struct tunefs_filesystem_state *state = tunefs_get_state(fs);

	if (state->ts_master == fs) {
		tunefs_unlock_local(fs);
		err = tunefs_unlock_cluster(fs);
	}

	return err;
}

static errcode_t tunefs_lock_cluster(ocfs2_filesys *fs, int flags)
{
	errcode_t err = 0;
	struct tunefs_filesystem_state *state = tunefs_get_state(fs);
	ocfs2_filesys *master_fs = state->ts_master;

	if (state->ts_cluster_locked)
		goto out;

	if (!master_fs->fs_dlm_ctxt) {
		err = o2cb_init();
		if (err)
			goto out;

		err = ocfs2_initialize_dlm(master_fs, WHOAMI);
		if (flags & TUNEFS_FLAG_NOCLUSTER) {
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
			}
			/*
			 * Success means do nothing, any other error
			 * propagates up.
			 */
			goto out;
		} else if (err)
			goto out;
	}

	tunefs_block_signals();
	err = ocfs2_lock_down_cluster(master_fs);
	tunefs_unblock_signals();
	if (!err)
		state->ts_cluster_locked = 1;
	else if ((err == O2DLM_ET_TRYLOCK_FAILED) &&
		 (flags & TUNEFS_FLAG_ONLINE))
		err = TUNEFS_ET_PERFORM_ONLINE;
	else
		ocfs2_shutdown_dlm(fs, WHOAMI);

out:
	return err;
}

/*
 * We try to lock the filesystem in *this* ocfs2_filesys.  We get the
 * state off of the master, but the filesystem may have changed since
 * the master opened its ocfs2_filesys.  It might have been switched to
 * LOCAL or something.  We trust the current status in order to make our
 * decision.
 *
 * Inside the underlying lock functions, they check the state to see if
 * they actually need to do anything.  If they don't have it locked, they
 * will always retry the lock.  The filesystem may have gotten unmounted
 * right after we ran our latest online operation.
 */
static errcode_t tunefs_lock_filesystem(ocfs2_filesys *fs, int flags)
{
	errcode_t err = 0;

	if (ocfs2_mount_local(fs))
		err = tunefs_lock_local(fs, flags);
	else
		err = tunefs_lock_cluster(fs, flags);

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

	verbosef(VL_LIB, "Verifying the global allocator\n");

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

static errcode_t tunefs_open_bitmap_check(ocfs2_filesys *fs)
{
	struct tunefs_private *tp = to_private(fs);
	struct tunefs_filesystem_state *state = tunefs_get_state(fs);

	if (!(tp->tp_open_flags & TUNEFS_FLAG_ALLOCATION))
		return 0;

	state->ts_allocation = 1;
	return tunefs_global_bitmap_check(fs);
}

static errcode_t tunefs_close_bitmap_check(ocfs2_filesys *fs)
{
	struct tunefs_filesystem_state *state = tunefs_get_state(fs);

	if (!state->ts_allocation)
		return 0;

	if (state->ts_master != fs)
		return 0;

	return tunefs_global_bitmap_check(fs);
}

static errcode_t tunefs_journal_check(ocfs2_filesys *fs)
{
	errcode_t ret;
	char *buf = NULL;
	uint64_t blkno;
	struct ocfs2_dinode *di;
	int i, dirty = 0;
	uint16_t max_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;
	struct tunefs_private *tp = to_private(fs);
	struct tunefs_filesystem_state *state = tunefs_get_state(fs);

	/* We only need to check the journal once */
	if (state->ts_journal_clusters)
		return 0;

	verbosef(VL_LIB, "Checking for dirty journals\n");

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

		if (di->i_clusters > state->ts_journal_clusters)
			state->ts_journal_clusters = di->i_clusters;

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

	/*
	 * If anything follows a NOCLUSTER operation, it will have
	 * closed and reopened the filesystem.  It must recheck the
	 * journals.
	 */
	if (tp->tp_open_flags & TUNEFS_FLAG_NOCLUSTER)
		state->ts_journal_clusters = 0;

bail:
	if (buf)
		ocfs2_free(&buf);

	return ret;
}

static errcode_t tunefs_open_online_descriptor(ocfs2_filesys *fs)
{
	int rc, flags = 0;
	errcode_t ret = 0;
	char mnt_dir[PATH_MAX];
	struct tunefs_filesystem_state *state = tunefs_get_state(fs);

	if (state->ts_online_fd > -1)
		goto out;

	memset(mnt_dir, 0, sizeof(mnt_dir));

	ret = ocfs2_check_mount_point(fs->fs_devname, &flags,
				      mnt_dir, sizeof(mnt_dir));
	if (ret)
		goto out;

	if (!(flags & OCFS2_MF_MOUNTED) ||
	    (flags & OCFS2_MF_READONLY) ||
	    (flags & OCFS2_MF_SWAP)) {
		ret = TUNEFS_ET_NOT_MOUNTED;
		goto out;
	}

	rc = open64(mnt_dir, O_RDONLY);
	if (rc < 0) {
		if (errno == EBUSY)
			ret = TUNEFS_ET_DEVICE_BUSY;
		else if (errno == ENOENT)
			ret = TUNEFS_ET_NOT_MOUNTED;
		else
			ret = OCFS2_ET_IO;
	} else
		state->ts_online_fd = rc;

out:
	return ret;
}

static void tunefs_close_online_descriptor(ocfs2_filesys *fs)
{
	struct tunefs_filesystem_state *state = tunefs_get_state(fs);

	if ((state->ts_master == fs) && (state->ts_online_fd > -1)) {
		close(state->ts_online_fd);  /* Don't care about errors */
		state->ts_online_fd = -1;
	}
}

errcode_t tunefs_online_ioctl(ocfs2_filesys *fs, int op, void *arg)
{
	int rc;
	struct tunefs_filesystem_state *state = tunefs_get_state(fs);

	if (state->ts_online_fd < 0)
		return TUNEFS_ET_INTERNAL_FAILURE;

	rc = ioctl(state->ts_online_fd, op, arg);
	if (rc) {
		switch (errno) {
			case EBADF:
			case EFAULT:
			case ENOTTY:
				return TUNEFS_ET_INTERNAL_FAILURE;
				break;

			default:
				return TUNEFS_ET_ONLINE_FAILED;
				break;
		}
	}

	return 0;
}

static errcode_t tunefs_add_fs(ocfs2_filesys *fs, int flags)
{
	errcode_t err;
	struct tunefs_private *tp;

	err = ocfs2_malloc0(sizeof(struct tunefs_private), &tp);
	if (err)
		goto out;

	tp->tp_open_flags = flags;
	fs->fs_private = tp;
	tp->tp_fs = fs;

	err = tunefs_set_state(fs);
	if (err) {
		fs->fs_private = NULL;
		ocfs2_free(&tp);
		goto out;
	}

	/*
	 * This is purposely a push.  The first open of the filesystem
	 * will be the one holding the locks, so we want it to be the last
	 * close (a FILO stack).  When signals happen, tunefs_close_all()
	 * pops each off in turn, finishing with the lock holder.
	 */
	list_add(&tp->tp_list, &fs_list);

out:
	return err;
}

static void tunefs_remove_fs(ocfs2_filesys *fs)
{
	struct tunefs_private *tp = to_private(fs);
	struct tunefs_filesystem_state *s = NULL;

	if (tp) {
		s = tp->tp_state;
		list_del(&tp->tp_list);
		tp->tp_fs = NULL;
		fs->fs_private = NULL;
		ocfs2_free(&tp);
	}

	if (s && (s->ts_master == fs)) {
		assert(list_empty(&fs_list));
		ocfs2_free(&s);
	}
}

errcode_t tunefs_open(const char *device, int flags,
		      ocfs2_filesys **ret_fs)
{
	int rw = flags & TUNEFS_FLAG_RW;
	errcode_t err, tmp;
	int open_flags;
	ocfs2_filesys *fs = NULL;

	verbosef(VL_LIB, "Opening device \"%s\"\n", device);

	open_flags = OCFS2_FLAG_HEARTBEAT_DEV_OK;
	if (rw)
		open_flags |= OCFS2_FLAG_RW | OCFS2_FLAG_STRICT_COMPAT_CHECK;
	else
		open_flags |= OCFS2_FLAG_RO;

	err = ocfs2_open(device, open_flags, 0, 0, &fs);
	if (err)
		goto out;

	err = tunefs_add_fs(fs, flags);
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

	err = tunefs_lock_filesystem(fs, flags);
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
		if (!tmp)
			tmp = tunefs_open_bitmap_check(fs);
		if (tmp) {
			err = tmp;
			tunefs_unlock_filesystem(fs);
		}
	} else {
		tmp = tunefs_open_online_descriptor(fs);
		if (tmp) {
			err = tmp;
			tunefs_unlock_filesystem(fs);
		}
	}

out:
	if (err &&
	    (err != TUNEFS_ET_INVALID_STACK_NAME) &&
	    (err != TUNEFS_ET_PERFORM_ONLINE)) {
		if (fs) {
			tunefs_remove_fs(fs);
			ocfs2_close(fs);
			fs = NULL;
		}
		verbosef(VL_LIB, "Open of device \"%s\" failed\n", device);
	} else {
		verbosef(VL_LIB, "Device \"%s\" opened\n", device);
		*ret_fs = fs;
	}

	return err;
}

errcode_t tunefs_close(ocfs2_filesys *fs)
{
	errcode_t tmp, err = 0;

	/*
	 * We want to clean up everything we can even if there
	 * are errors, but we preserve the first error we get.
	 */
	if (fs) {
		verbosef(VL_LIB, "Closing device \"%s\"\n", fs->fs_devname);
		tunefs_close_online_descriptor(fs);
		err = tunefs_close_bitmap_check(fs);
		tmp = tunefs_unlock_filesystem(fs);
		if (!err)
			err = tmp;
		tmp = ocfs2_close(fs);
		if (!err)
			err = tmp;

		tunefs_remove_fs(fs);

		if (!err)
			verbosef(VL_LIB, "Device closed\n");
		else
			verbosef(VL_LIB, "Close of device failed\n");
		fs = NULL;
	}

	return err;
}

errcode_t tunefs_get_number(char *arg, uint64_t *res)
{
	char *ptr = NULL;
	uint64_t num;

	num = strtoull(arg, &ptr, 0);

	if ((ptr == arg) || (num == UINT64_MAX))
		return TUNEFS_ET_INVALID_NUMBER;

	switch (*ptr) {
	case '\0':
		break;

	case 'p':
	case 'P':
		num *= 1024;
		/* FALL THROUGH */

	case 't':
	case 'T':
		num *= 1024;
		/* FALL THROUGH */

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
		return TUNEFS_ET_INVALID_NUMBER;
	}

	*res = num;

	return 0;
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
	struct tunefs_filesystem_state *state = tunefs_get_state(fs);

	num_clusters =
		ocfs2_clusters_in_blocks(fs,
					 ocfs2_blocks_in_bytes(fs,
							       new_size));

	/* If no size was passed in, use the size we found at open() */
	if (!num_clusters)
		num_clusters = state->ts_journal_clusters;

	/*
	 * This can't come from a NOCLUSTER operation, so we'd better
	 * have a size in ts_journal_clusters
	 */
	assert(num_clusters);

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

static int single_feature_parse_option(struct tunefs_operation *op,
				       char *arg)
{
	int rc = 0;
	struct tunefs_feature *feat = op->to_private;

	if (!arg) {
		errorf("No action specified\n");
		rc = 1;
	} else if (!strcmp(arg, "enable"))
		feat->tf_action = FEATURE_ENABLE;
	else if (!strcmp(arg, "disable"))
		feat->tf_action = FEATURE_DISABLE;
	else {
		errorf("Invalid action: \"%s\"\n", arg);
		rc = 1;
	}

	return rc;
}

errcode_t tunefs_feature_run(ocfs2_filesys *master_fs,
			     struct tunefs_feature *feat)
{
	int rc = 0;
	errcode_t err, tmp;
	ocfs2_filesys *fs;
	int flags;

	verbosef(VL_DEBUG, "Running feature \"%s\"\n", feat->tf_name);

	flags = feat->tf_open_flags & ~(TUNEFS_FLAG_ONLINE |
				      TUNEFS_FLAG_NOCLUSTER);
	err = tunefs_open(master_fs->fs_devname, feat->tf_open_flags, &fs);
	if (err == TUNEFS_ET_PERFORM_ONLINE)
		flags |= TUNEFS_FLAG_ONLINE;
	else if (err == TUNEFS_ET_INVALID_STACK_NAME)
		flags |= TUNEFS_FLAG_NOCLUSTER;
	else if (err)
		goto out;

	err = 0;
	switch (feat->tf_action) {
		case FEATURE_ENABLE:
			rc = feat->tf_enable(fs, flags);
			break;

		case FEATURE_DISABLE:
			rc = feat->tf_disable(fs, flags);
			break;

		case FEATURE_NOOP:
			verbosef(VL_APP,
				 "Ran NOOP for feature \"%s\" - how'd "
				 "that happen?\n",
				 feat->tf_name);
			break;

		default:
			errorf("Unknown action %d called against feature "
			       "\"%s\"\n",
			       feat->tf_action, feat->tf_name);
			err = TUNEFS_ET_INTERNAL_FAILURE;
			break;
	}

	if (rc)
		err = TUNEFS_ET_OPERATION_FAILED;

	tmp = tunefs_close(fs);
	if (!err)
		err = tmp;

out:
	return err;
}

errcode_t tunefs_op_run(ocfs2_filesys *master_fs,
			struct tunefs_operation *op)
{
	errcode_t err, tmp;
	ocfs2_filesys *fs;
	int flags;

	verbosef(VL_DEBUG, "Running operation \"%s\"\n", op->to_name);

	flags = op->to_open_flags & ~(TUNEFS_FLAG_ONLINE |
				      TUNEFS_FLAG_NOCLUSTER);
	err = tunefs_open(master_fs->fs_devname, op->to_open_flags, &fs);
	if (err == TUNEFS_ET_PERFORM_ONLINE)
		flags |= TUNEFS_FLAG_ONLINE;
	else if (err == TUNEFS_ET_INVALID_STACK_NAME)
		flags |= TUNEFS_FLAG_NOCLUSTER;
	else if (err)
		goto out;

	err = 0;
	if (op->to_run(op, fs, flags))
		err = TUNEFS_ET_OPERATION_FAILED;

	tmp = tunefs_close(fs);
	if (!err)
		err = tmp;

out:
	return err;
}

static int single_feature_run(struct tunefs_operation *op,
			      ocfs2_filesys *fs, int flags)
{
	errcode_t err;
	struct tunefs_feature *feat = op->to_private;

	err = tunefs_feature_run(fs, feat);
	if (err && (err != TUNEFS_ET_OPERATION_FAILED))
		tcom_err(err, "while toggling feature \"%s\"",
			 feat->tf_name);

	return err;
}

DEFINE_TUNEFS_OP(single_feature,
		 NULL,
		 0,
		 single_feature_parse_option,
		 single_feature_run);

int tunefs_feature_main(int argc, char *argv[], struct tunefs_feature *feat)
{
	char usage[PATH_MAX];

	snprintf(usage, PATH_MAX,
		 "Usage: ocfs2ne_feature_%s [opts] <device> "
		 "{enable|disable}\n",
		 feat->tf_name);
	single_feature_op.to_debug_usage = usage;
	single_feature_op.to_open_flags = feat->tf_open_flags;
	single_feature_op.to_private = feat;

	return tunefs_op_main(argc, argv, &single_feature_op);
}

int tunefs_op_main(int argc, char *argv[], struct tunefs_operation *op)
{
	errcode_t err;
	int rc = 1;
	ocfs2_filesys *fs;
	char *arg = NULL;

	tunefs_init(argv[0]);
	tunefs_parse_core_options(&argc, &argv, op->to_debug_usage);
	if (argc < 2) {
		errorf("No device specified\n");
		tunefs_debug_usage(1);
		goto out;
	}

	if (op->to_parse_option) {
		if (argc > 3) {
			errorf("Too many arguments\n");
			tunefs_debug_usage(1);
			goto out;
		}
		if (argc == 3)
			arg = argv[2];

		rc = op->to_parse_option(op, arg);
		if (rc) {
			tunefs_debug_usage(1);
			goto out;
		}
	} else if (argc > 2) {
		errorf("Too many arguments\n");
		tunefs_debug_usage(1);
		goto out;
	}

	err = tunefs_open(argv[1], op->to_open_flags, &fs);
	if (err &&
	    (err != TUNEFS_ET_PERFORM_ONLINE) &&
	    (err != TUNEFS_ET_INVALID_STACK_NAME)) {
		tcom_err(err, "- Unable to open device \"%s\" read-write.",
			 argv[1]);
		goto out;
	}

	err = tunefs_op_run(fs, op);
	if (!err)
		rc = 0;
	else if (err != TUNEFS_ET_OPERATION_FAILED)
		tcom_err(err, "while running operation \"%s\"",
			 op->to_name);

	err = tunefs_close(fs);
	if (err) {
		tcom_err(err, "while closing device \"%s\"", argv[1]);
		rc = 1;
	}

out:
	return rc;
}

#ifdef DEBUG_EXE

#define DEBUG_PROGNAME "debug_libocfs2ne"
int parent = 0;


static void closeup(ocfs2_filesys *fs, const char *device)
{
	errcode_t err;

	verbosef(VL_OUT, "success\n");
	err = tunefs_close(fs);
	if (err)  {
		tcom_err(err, "- Unable to close device \"%s\".", device);
	}
}

int main(int argc, char *argv[])
{
	errcode_t err;
	const char *device;
	ocfs2_filesys *fs;

	tunefs_init(argv[0]);
	tunefs_parse_core_options(&argc, &argv,
				  "Usage: debug_libocfs2ne [-p] <device>\n");

	if (argc > 3) {
		errorf("Too many arguments\n");
		tunefs_debug_usage(1);
		return 1;
	}
	if (argc == 3) {
		if (strcmp(argv[1], "-p")) {
			errorf("Invalid argument: \'%s\'\n", argv[1]);
			tunefs_debug_usage(1);
			return 1;
		}
		parent = 1;
		device = argv[2];
	} else if ((argc == 2) &&
		   strcmp(argv[1], "-p")) {
		device = argv[1];
	} else {
		errorf("Device must be specified\n");
		tunefs_debug_usage(1);
		return 1;
	}

	verbosef(VL_OUT, "Opening device \"%s\" read-only... ", device);
	err = tunefs_open(device, TUNEFS_FLAG_RO, &fs);
	if (err) {
		verbosef(VL_OUT, "failed\n");
		tcom_err(err, "- Unable to open device \"%s\" read-only.",
			 device);
	} else
		closeup(fs, device);

	verbosef(VL_OUT, "Opening device \"%s\" read-write... ", device);
	err = tunefs_open(device, TUNEFS_FLAG_RW, &fs);
	if (err) {
		verbosef(VL_OUT, "failed\n");
		tcom_err(err, "- Unable to open device \"%s\" read-write.",
			 device);
	} else
		closeup(fs, device);

	verbosef(VL_OUT,
		 "Opening device \"%s\" for an online operation... ",
		 device);
	err = tunefs_open(device, TUNEFS_FLAG_RW | TUNEFS_FLAG_ONLINE,
			  &fs);
	if (err == TUNEFS_ET_PERFORM_ONLINE) {
		closeup(fs, device);
		verbosef(VL_OUT, "Operation would have been online\n");
	} else if (!err) {
		closeup(fs, device);
		verbosef(VL_OUT, "Operation would have been offline\n");
	} else {
		verbosef(VL_OUT, "failed\n");
		tcom_err(err, "- Unable to open device \"%s\" read-write.",
			 device);
	}

	verbosef(VL_OUT,
		 "Opening device \"%s\" for a stackless operation... ",
		 device);
	err = tunefs_open(device, TUNEFS_FLAG_RW | TUNEFS_FLAG_NOCLUSTER,
			  &fs);
	if (err == TUNEFS_ET_INVALID_STACK_NAME) {
		closeup(fs, device);
		verbosef(VL_OUT, "Expected cluster stack mismatch found\n");
	} else if (!err) {
		closeup(fs, device);
		verbosef(VL_OUT, "Cluster stacks already match\n");
	} else {
		verbosef(VL_OUT, "failed\n");
		tcom_err(err, "- Unable to open device \"%s\" read-write.",
			 device);
	}

	return 0;
}


#endif /* DEBUG_EXE */

