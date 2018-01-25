/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * o2cluster.c
 *
 * ocfs2 update cluster stack.
 *
 * Copyright (C) 2011, 2012 Oracle.  All rights reserved.
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

#define _GNU_SOURCE /* for getopt_long and O_DIRECT */
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <getopt.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>

#include "ocfs2/ocfs2.h"
#include "ocfs2/bitops.h"

#include "o2ne_err.h"
#include "tools-internal/verbose.h"
#include "tools-internal/progress.h"

#define DIV_ROUND_UP(n,d)	(((n) + (d) - 1) / (d))
#define BITS_PER_BYTE		8
#define BITS_TO_LONGS(nr)	DIV_ROUND_UP(nr, BITS_PER_BYTE * sizeof(long))

enum {
	CL_UNKNOWN	= 0,
	CL_LIST_RUNNING	= 1,
	CL_LIST_ONDISK	= 2,
	CL_UPDATE_DISK	= 3,
};

static void usage(int rc)
{
	verbosef(VL_OUT, "Usage: %s [options] <device>\n", tools_progname());
	verbosef(VL_OUT, "       %s -r|--show-running (currently active clusterstack)\n", tools_progname());
	verbosef(VL_OUT, "       %s -h|--help\n", tools_progname());
	verbosef(VL_OUT, "       %s -V|--version\n", tools_progname());
	verbosef(VL_OUT, "[options] can be:\n");
	verbosef(VL_OUT, "\t-u|--update[=<clusterstack>]\n");
	verbosef(VL_OUT, "\t-o|--show-ondisk (shows clusterstack as stamped ondisk)\n");
	verbosef(VL_OUT, "\t-v|--verbose (increases verbosity; more than one permitted)\n");
	verbosef(VL_OUT, "\t-y|--yes\n");
	verbosef(VL_OUT, "\t-n|--no\n\n");

	verbosef(VL_OUT, "Updates and lists the cluster stack stamped on an OCFS2 file system.\n\n");

	verbosef(VL_OUT, "The clusterstack may be specified in one of two forms. The first as \"default\"\n");
	verbosef(VL_OUT, "denoting the original classic o2cb cluster stack with local heartbeat.\n");
	verbosef(VL_OUT, "The second as a triplet with the stack name, the cluster name and the cluster\n");
	verbosef(VL_OUT, "flags separated by commas. Like \"o2cb,mycluster,global\".\n\n");

	verbosef(VL_OUT, "Valid stack names are \"o2cb\", \"pcmk\" and \"cman\".\n\n");

	verbosef(VL_OUT, "Cluster names can be up to 16 characters. The o2cb stack further restricts\n");
	verbosef(VL_OUT, "the names to contain only alphanumeric characters.\n\n");

	verbosef(VL_OUT, "For the o2cb stack, valid flags are \"local\" and \"global\" denoting the two\n");
	verbosef(VL_OUT, "heartbeat modes. Use \"none\" for the other cluster stacks.\n");

	exit(rc);
}

static char * cluster_flags_in_string(struct o2cb_cluster_desc *desc)
{
	if (!desc->c_stack)
		return O2CB_LOCAL_HEARTBEAT_TAG;

	if (!strcmp(desc->c_stack, OCFS2_CLASSIC_CLUSTER_STACK)) {
		if (desc->c_flags == OCFS2_CLUSTER_O2CB_GLOBAL_HEARTBEAT)
			return O2CB_GLOBAL_HEARTBEAT_TAG;
		else
			return O2CB_LOCAL_HEARTBEAT_TAG;
	} else
		return "none";
}

static void cluster_desc_in_string(struct o2cb_cluster_desc *desc, char *str,
				   int len)
{
	if (desc->c_stack)
		snprintf(str, len, "%s,%s,%s", desc->c_stack, desc->c_cluster,
			 cluster_flags_in_string(desc));
	else
		snprintf(str, len, "%s", "default");
}

static errcode_t get_running_cluster(struct o2cb_cluster_desc *desc)
{
	errcode_t ret;

	ret = o2cb_init();
	if (!ret)
		ret = o2cb_running_cluster_desc(desc);

	return ret;
}

static int parse_cluster_info(char *csinfo, struct o2cb_cluster_desc *desc)
{
	char *tok, *tokens = NULL;
	int ret = 1;
	int i;
#define MAX_VALS	3
	char *val[MAX_VALS];

	tokens = strdup(csinfo);
	if (!tokens) {
		tcom_err(OCFS2_ET_NO_MEMORY, "while parsing cluster options");
		goto bail;
	}

	for (i = 0, tok = strtok(tokens, ","); i < MAX_VALS && tok;
	     tok = strtok(NULL, ","), ++i)
		val[i] = tok;

	if (i == 1 && !strcmp(val[0], "default")) {
		desc->c_stack = NULL;
		desc->c_cluster = NULL;
		desc->c_flags = 0;
		ret = 0;
		goto bail;
	}

	if (i != MAX_VALS) {
		errorf("Cluster details should be in the format "
		       "\"<stack>,<cluster>,<hbmode>\"\n");
		goto bail;
	}
#undef MAX_VALS

	desc->c_stack = strdup(val[0]);
	desc->c_cluster = strdup(val[1]);

	if (!desc->c_stack || !desc->c_cluster) {
		tcom_err(OCFS2_ET_NO_MEMORY, "while parsing cluster details");
		goto bail;
	}

	if (!o2cb_valid_stack_name(desc->c_stack)) {
		tcom_err(O2CB_ET_INVALID_STACK_NAME,
			 "; unknown cluster stack '%s'", desc->c_stack);
		goto bail;
	}

	if (!strcmp(desc->c_stack, OCFS2_CLASSIC_CLUSTER_STACK)) {
		if (!o2cb_valid_o2cb_cluster_name(desc->c_cluster)) {
			tcom_err(O2CB_ET_INVALID_CLUSTER_NAME,
				 "; max %d alpha-numeric characters",
				 OCFS2_CLUSTER_NAME_LEN);
			goto bail;
		}
		if (!o2cb_valid_heartbeat_mode(val[2])) {
			tcom_err(O2CB_ET_INVALID_HEARTBEAT_MODE,
				 "; unknown heartbeat mode '%s'", val[2]);
			goto bail;
		}
		if (!strcmp(val[2], O2CB_GLOBAL_HEARTBEAT_TAG))
			desc->c_flags = OCFS2_CLUSTER_O2CB_GLOBAL_HEARTBEAT;
	} else {
		if (!o2cb_valid_cluster_name(desc->c_cluster)) {
			tcom_err(O2CB_ET_INVALID_CLUSTER_NAME,
				 "; max %d characters", OCFS2_CLUSTER_NAME_LEN);
			goto bail;
		}
		if (strcasecmp(val[2], "none")) {
			tcom_err(O2CB_ET_INVALID_HEARTBEAT_MODE, "; heartbeat "
				 "mode must be 'none' for this cluster stack");
			goto bail;
		}
	}

	ret = 0;

bail:
	free(tokens);
	return ret;
}

static errcode_t parse_options(int argc, char *argv[], char **device,
			       int *task, struct o2cb_cluster_desc *desc)
{
	int ret = 1, c;
	static struct option long_options[] = {
		{ "update", 2, 0, 'u' },
		{ "show-ondisk", 0, 0, 'o' },
		{ "show-running", 0, 0, 'r' },
		{ "help", 0, 0, 'h' },
		{ "verbose", 0, 0, 'v' },
		{ "version", 0, 0, 'V' },
		{ 0, 0, 0, 0 },
	};

	while (1) {
		c = getopt_long(argc, argv, "+hvVynrou::", long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			usage(1);
			break;

		case 'n':
			tools_interactive_no();
			break;

		case 'y':
			tools_interactive_yes();
			break;

		case 'o':
			if (*task != CL_UNKNOWN)
				usage(1);
			*task = CL_LIST_ONDISK;
			break;

		case 'r':
			if (*task != CL_UNKNOWN)
				usage(1);
			*task = CL_LIST_RUNNING;
			break;

		case 'v':
			tools_verbose();
			break;

		case 'V':
			tools_version();
			exit(1);
			break;

		case 'u':
			if (*task != CL_UNKNOWN)
				usage(1);
			*task = CL_UPDATE_DISK;
			if (optarg) {
				if (parse_cluster_info(optarg, desc))
					goto out;
				break;
			}
			ret = get_running_cluster(desc);
			if (ret) {
				tcom_err(ret, "while discovering running "
					 "cluster stack");
				goto out;
			}
			break;

		default:
			usage(1);
			break;
		}
	}

	if (*task == CL_UNKNOWN)
		usage(1);

	if (*task == CL_LIST_RUNNING) {
		ret = 0;
		goto out;
	}

	if (optind >= argc) {
		errorf("No device specified\n");
		usage(1);
	}

	*device = strdup(argv[optind]);
	if (!*device) {
		tcom_err(OCFS2_ET_NO_MEMORY, "while parsing parameters");
		goto out;
	}
	optind++;

	if (optind < argc) {
		errorf("Too many arguments\n");
		usage(1);
	}

	ret = 0;

out:
	return ret;
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

//	tunefs_close_all();

	exit(1);
}

static int setup_signals(void)
{
	int rc = 0;
	struct sigaction act;

	act.sa_sigaction = NULL;
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

static void tool_init(const char *argv0)
{
	initialize_ocfs_error_table();
	initialize_o2cb_error_table();
	initialize_o2dl_error_table();

	tools_setup_argv0(argv0);

	tools_interactive();

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	if (setup_signals()) {
		errorf("Unable to setup signal handling\n");
		exit(1);
	}
}

static errcode_t scan_journals(ocfs2_filesys *fs, unsigned long *dirty_map)
{
 	errcode_t ret = 0;
	char *buf = NULL;
	ocfs2_cached_inode *ci = NULL;
	uint64_t blkno;
	int i;
	uint16_t max_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;

	if (OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat &
	    OCFS2_FEATURE_INCOMPAT_HEARTBEAT_DEV) {
		verbosef(VL_LIB, "Heartbeat device; No need to check for "
			 "dirty journals\n");
		goto bail;
	}

	verbosef(VL_LIB, "Checking for dirty journals\n");

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret) {
		verbosef(VL_LIB, "%s while allocating a block during journal "
			 "check\n", error_message(ret));
		goto bail;
	}

	for (i = 0; i < max_slots; ++i) {
		ret = ocfs2_lookup_system_inode(fs, JOURNAL_SYSTEM_INODE, i,
						&blkno);
		if (ret) {
			verbosef(VL_LIB, "%s while looking up journal inode "
				 "for slot %u during journal check\n",
				 error_message(ret), i);
			goto bail;
		}

		ret = ocfs2_read_cached_inode(fs, blkno, &ci);
		if (ret) {
			verbosef(VL_LIB, "%s while reading inode %"PRIu64" "
				 "during journal check", error_message(ret),
				 blkno);
			goto bail;
		}

		if (ci->ci_inode->id1.journal1.ij_flags &
		    OCFS2_JOURNAL_DIRTY_FL)
			ocfs2_set_bit(i, dirty_map);
	}

bail:
	if (ci)
		ocfs2_free_cached_inode(fs, ci);
	ocfs2_free(&buf);

	return ret;
}

#define DIRTY_JOURNAL_WARNING						\
	"Dirty journals could indicate that the volume is in use on "	\
	"one or more nodes.\nIf so, then this operation should not be "	\
	"performed. However, it could also be\nthat the last node "	\
	"using the filesystem crashed leaving a dirty journal.\n"	\
	"In the normal course, this journal would have been recovered "	\
	"during mount.\nDANGER: YOU MUST BE ABSOLUTELY SURE THAT NO "	\
	"OTHER NODE IS USING THIS FILESYSTEM\nBEFORE MODIFYING ITS "	\
	"CLUSTER CONFIGURATION.\nCONTINUE? "

static errcode_t journal_check(ocfs2_filesys *fs)
{
	int ret = 0;
	int i, out, inuse, slot, size;
	unsigned long dirty_map[BITS_TO_LONGS(O2NM_MAX_NODES)];
	char warning[1024] = "Device \"%s\" has dirty journals in slots ";

	memset(dirty_map, 0, sizeof(dirty_map));
	ret = scan_journals(fs, dirty_map);
	if (ret)
		goto out;

	inuse = ocfs2_get_bits_set(dirty_map, sizeof(dirty_map), 0);
	if (!inuse)
		goto out;

	out = strlen(warning);

	slot = -1;
	size = BITS_PER_BYTE * sizeof(dirty_map);
	for (i = 0; i < O2NM_MAX_NODES; ++i) {
		slot = ocfs2_find_next_bit_set(dirty_map, size, slot + 1);
		if (slot >= size)
			break;
		if (i)
			out += sprintf(warning + out, ", %d", slot);
		else
			out += sprintf(warning + out, "%d", slot);
	}
	out += sprintf(warning + out, ".\n");

	verbosef(VL_OUT, warning, fs->fs_devname);
#if 0
	if (tools_interact_critical(DIRTY_JOURNAL_WARNING))
		goto out;
#endif

	ret = -1;
out:
	return ret;
}

static errcode_t fs_open(const char *device, ocfs2_filesys **ret_fs)
{
	errcode_t err;
	int open_flags;

	verbosef(VL_LIB, "Opening device '%s'\n", device);

	*ret_fs = NULL;

	open_flags = OCFS2_FLAG_HEARTBEAT_DEV_OK;
	open_flags |= OCFS2_FLAG_RW | OCFS2_FLAG_STRICT_COMPAT_CHECK;

	err = ocfs2_open(device, open_flags, 0, 0, ret_fs);
	if (err) {
		tcom_err(err, "while opening device '%s'", device);
		goto out;
	}

	/*TODO  Review all features */


out:
	if (!err)
		verbosef(VL_LIB, "Device \"%s\" opened\n", device);
	else if (*ret_fs)
		ocfs2_close(*ret_fs);

	return err;
}

static void fs_close(ocfs2_filesys *fs)
{
	errcode_t err;

	if (!fs)
		return;

	verbosef(VL_LIB, "Closing device \"%s\"\n", fs->fs_devname);

	err = ocfs2_close(fs);
	if (!err)
		verbosef(VL_LIB, "Device closed\n");
	else
		verbosef(VL_LIB, "Device close failed (%s)\n",
			 error_message(err));
}

static int do_update(char *device, struct o2cb_cluster_desc *newcl)
{
	int ret;
	ocfs2_filesys *fs = NULL;
	struct o2cb_cluster_desc ondisk = { NULL, NULL, 0 };
	struct o2cb_cluster_desc *diskcl = &ondisk;
	char fromcs[64], tocs[64];

	ret = fs_open(device, &fs);
	if (ret)
		goto out;

	if (OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat &
	    OCFS2_FEATURE_INCOMPAT_LOCAL_MOUNT) {
		ret = -1;
		errorf("Device not clustered. Use tunefs.ocfs2(8) to enable "
		       "clustered mode.\n");
		goto out;
	}

	ret = journal_check(fs);
	if (ret)
		goto out;

	ret = ocfs2_fill_cluster_desc(fs, diskcl);
	if (ret) {
		tcom_err(ret, "while discovering ondisk cluster stack");
		goto out;
	}

	/* Abort if ondisk cluster matches requested cluster */
	if ((!newcl->c_stack && !diskcl->c_stack) ||
	    (newcl->c_stack && newcl->c_cluster &&
	     diskcl->c_stack && diskcl->c_cluster &&
	     !strcmp(newcl->c_stack, diskcl->c_stack) &&
	     !strcmp(newcl->c_cluster, diskcl->c_cluster) &&
	     newcl->c_flags == diskcl->c_flags)) {
		verbosef(VL_OUT, "New cluster stack is already on disk.\n");
		goto out;
	}

	cluster_desc_in_string(diskcl, fromcs, sizeof(fromcs));
	cluster_desc_in_string(newcl, tocs, sizeof(tocs));

	if (!tools_interact("Changing the clusterstack from %s to %s. "
			    "Continue? ", fromcs, tocs))
		goto out;

	ret = ocfs2_set_cluster_desc(fs, newcl);
	if (ret)
		tcom_err(ret, "while updating the cluster stack ondisk");
	else
		verbosef(VL_OUT, "Updated successfully.\n");

out:
	o2cb_free_cluster_desc(diskcl);
	fs_close(fs);

	return ret;
}

static errcode_t do_list_ondisk(char *device)
{
	int ret;
	ocfs2_filesys *fs = NULL;
	struct o2cb_cluster_desc desc = { NULL, NULL, 0 };
	char cldesc[64];

	ret = fs_open(device, &fs);
	if (ret)
		goto out;

	if (OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat &
	    OCFS2_FEATURE_INCOMPAT_LOCAL_MOUNT) {
		ret = 1;
		errorf("Clustering is not enabled on device '%s'.\n", device);
		goto out;
	}

	ret = ocfs2_fill_cluster_desc(fs, &desc);
	if (ret) {
		tcom_err(ret, "while discovering ondisk cluster stack");
		goto out;
	}

	cluster_desc_in_string(&desc, cldesc, sizeof(cldesc));

	verbosef(VL_OUT, "%s\n", cldesc);

out:
	o2cb_free_cluster_desc(&desc);
	fs_close(fs);

	return ret;
}

static errcode_t do_list_active(void)
{
	int ret;
	struct o2cb_cluster_desc desc = { NULL, NULL, 0 };
	char cldesc[64];

	ret = get_running_cluster(&desc);
	if (ret) {
		tcom_err(ret, "while discovering running cluster stack");
		goto out;
	}

	cluster_desc_in_string(&desc, cldesc, sizeof(cldesc));

	verbosef(VL_OUT, "%s\n", cldesc);

out:
	return ret;
}

int main(int argc, char *argv[])
{
	errcode_t ret;
	int task = CL_UNKNOWN;
	char *device = NULL;
	struct o2cb_cluster_desc desc = { NULL, NULL, 0 };

	tool_init(argv[0]);

	ret = parse_options(argc, argv, &device, &task, &desc);
	if (ret)
		goto out;

	switch (task) {
	case CL_LIST_RUNNING:
		ret = do_list_active();
		break;

	case CL_LIST_ONDISK:
		ret = do_list_ondisk(device);
		break;

	case CL_UPDATE_DISK:
		ret = do_update(device, &desc);
		break;

	default:
		break;
	}

out:
	if (ret)
		verbosef(VL_OUT, "Aborting.\n");
	o2cb_free_cluster_desc(&desc);
	free(device);

	return ret ? 1 : 0;
}
