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

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>

#include "ocfs2/ocfs2.h"

#include "libtunefs.h"
#include "libtunefs_err.h"

#define WHOAMI "tunefs.ocfs2"
#define TUNEFS_OCFS2_LOCK_ENV		"_TUNEFS_OCFS2_LOCK"
#define TUNEFS_OCFS2_LOCK_ENV_LOCKED	"locked"
#define TUNEFS_OCFS2_LOCK_ENV_ONLINE	"online"

static ocfs2_filesys *fs;
static int cluster_locked;

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
			fprintf(stderr, "Caught signal %d, exiting",
				caught_sig);
			exitp = 1;
			break;

		case SIGSEGV:
			fprintf(stderr, "Segmentation fault, exiting");
			exitp = 1;
			if (segv_already) {
				fprintf(stderr,
					"Segmentation fault loop detected");
				abortp = 1;
			} else
				segv_already = 1;
			break;

		default:
			fprintf(stderr, "Caught signal %d, ignoring",
				caught_sig);
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


errcode_t tunefs_init(void)
{
	initialize_tune_error_table();
	initialize_ocfs_error_table();
	initialize_o2dl_error_table();
	initialize_o2cb_error_table();

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	return setup_signals() ? TUNEFS_ET_SIGNALS_FAILED : 0;
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
	char *lockenv = getenv(TUNEFS_OCFS2_LOCK_ENV);

	if (!lockenv)
		return TUNEFS_ET_INVALID_STACK_NAME;
	if (!strcmp(lockenv, TUNEFS_OCFS2_LOCK_ENV_ONLINE))
		return TUNEFS_ET_PERFORM_ONLINE;
	if (!strcmp(lockenv, TUNEFS_OCFS2_LOCK_ENV_LOCKED))
		return 0;

	return TUNEFS_ET_INVALID_STACK_NAME;
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


errcode_t tunefs_open(const char *device, int flags)
{
	int rw = flags & TUNEFS_FLAG_RW;
	errcode_t err;
	int open_flags;

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
	 * We will use block cache in io. Now whether the cluster is locked or
	 * the volume is mount local, in both situation we can safely use cache.
	 * If io_init_cache failed, we will go on the tunefs work without
	 * the io_cache, so there is no check here.
	 */
	io_init_cache(fs->fs_io, ocfs2_extent_recs_per_eb(fs->fs_blocksize));


out:
	if (fs && err &&
	    (err != TUNEFS_ET_INVALID_STACK_NAME) &&
	    (err != TUNEFS_ET_PERFORM_ONLINE)) {
		ocfs2_close(fs);
		fs = NULL;
	}

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
		err = tunefs_unlock_cluster();
		tmp = ocfs2_close(fs);
		if (!err)
			err = tmp;

		fs = NULL;
	}

	return err;
}

#ifdef DEBUG_EXE

#define DEBUG_PROGNAME "debug_libtunefs"
int parent = 0;

static void print_usage(void)
{
	fprintf(stderr,
		"Usage: %s [-p] <device>\n", DEBUG_PROGNAME);
}


static void closeup(const char *device)
{
	errcode_t err;

	fprintf(stdout, "success\n");
	err = tunefs_close();
	if (err)  {
		com_err(DEBUG_PROGNAME, err,
			"- Unable to close device \"%s\".",
			device);
	}
}

int main(int argc, char *argv[])
{
	errcode_t err;
	const char *device;

	if (argc > 3) {
		fprintf(stderr, "Too many arguments\n");
		print_usage();
		return 1;
	}
	if (argc == 3) {
		if (strcmp(argv[1], "-p")) {
			fprintf(stderr, "Invalid argument: \'%s\'\n",
				argv[1]);
			print_usage();
			return 1;
		}
		parent = 1;
		device = argv[2];
	} else if ((argc == 2) &&
		   strcmp(argv[1], "-p")) {
		device = argv[1];
	} else {
		fprintf(stderr, "Device must be specified\n");
		print_usage();
		return 1;
	}

	err = tunefs_init();
	if (err) {
		com_err(DEBUG_PROGNAME, err, "while initializing tunefs");
		return 1;
	}

	fprintf(stdout, "Opening device \"%s\" read-only... ", device);
	err = tunefs_open(device, TUNEFS_FLAG_RO);
	if (err) {
		fprintf(stdout, "failed\n");
		com_err(DEBUG_PROGNAME, err,
			"- Unable to open device \"%s\" read-only.",
			device);
	} else
		closeup(device);

	fprintf(stdout, "Opening device \"%s\" read-write... ", device);
	err = tunefs_open(device, TUNEFS_FLAG_RW);
	if (err) {
		fprintf(stdout, "failed\n");
		com_err(DEBUG_PROGNAME, err,
			"- Unable to open device \"%s\" read-write.",
			device);
	} else
		closeup(device);

	fprintf(stdout, "Opening device \"%s\" for an online operation... ",
		device);
	err = tunefs_open(device, TUNEFS_FLAG_RW | TUNEFS_FLAG_ONLINE);
	if (err == TUNEFS_ET_PERFORM_ONLINE) {
		closeup(device);
		fprintf(stdout, "Operation would have been online\n");
	} else if (!err) {
		closeup(device);
		fprintf(stdout, "Operation would have been offline\n");
	} else {
		fprintf(stdout, "failed\n");
		com_err(DEBUG_PROGNAME, err,
			"- Unable to open device \"%s\" read-write.",
			device);
	}

	fprintf(stdout, "Opening device \"%s\" for a stackless operation... ",
		device);
	err = tunefs_open(device, TUNEFS_FLAG_RW | TUNEFS_FLAG_NOCLUSTER);
	if (err == TUNEFS_ET_INVALID_STACK_NAME) {
		closeup(device);
		fprintf(stdout, "Expected cluster stack mismatch found\n");
	} else if (!err) {
		closeup(device);
		fprintf(stdout, "Cluster stacks already match\n");
	} else {
		fprintf(stdout, "failed\n");
		com_err(DEBUG_PROGNAME, err,
			"- Unable to open device \"%s\" read-write.",
			device);
	}

	return 0;
}

#endif /* DEBUG_EXE */

