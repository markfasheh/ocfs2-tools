/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * resize_slotmap.c
 *
 * Copyright (C) 2011 Oracle.  All rights reserved.
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301 USA.
 */

#define _XOPEN_SOURCE 600  /* Triggers XOPEN2K in features.h */
#define _LARGEFILE64_SOURCE

#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <inttypes.h>
#include <libgen.h>
#include <ctype.h>

#include "ocfs2/byteorder.h"
#include "ocfs2/ocfs2.h"

char *progname;

static void usage(void)
{
	fprintf(stderr, "usage: %s device size\nUtility to resize //slotmap "
		"in an OCFS2 file system.\n", progname);
	exit(1);
}

static errcode_t resize_slot_map_file(ocfs2_filesys *fs, uint64_t slotsize)
{
	errcode_t ret;
	uint64_t blkno, maxsize, minsize;
	ocfs2_cached_inode *ci = NULL;
	struct ocfs2_dinode *di;
	int c;

	ret = ocfs2_lookup_system_inode(fs, SLOT_MAP_SYSTEM_INODE, 0, &blkno);
	if (ret)
		goto out;

	ret = ocfs2_read_cached_inode(fs, blkno, &ci);
	if (ret)
		goto out;

	di = ci->ci_inode;

	if (!(di->i_flags & OCFS2_VALID_FL) ||
	    !(di->i_flags & OCFS2_SYSTEM_FL)) {
		ret = OCFS2_ET_INTERNAL_FAILURE;
		goto out;
	}

	maxsize = ocfs2_clusters_to_bytes(fs, di->i_clusters);
	minsize = OCFS2_MAX_SLOTS * sizeof(struct ocfs2_extended_slot);

	if (slotsize > maxsize) {
		fprintf(stderr, "Error: The requested size (%"PRIu64" bytes) is "
			"larger than the allocated size (%"PRIu64" bytes).\n",
			slotsize, maxsize);
		ret = OCFS2_ET_INVALID_ARGUMENT;
		goto out;
	}

	if (slotsize < minsize) {
		fprintf(stderr, "Error: The requested size (%"PRIu64" bytes) is "
			"smaller than the minimum acceptable size "
			"(%"PRIu64" bytes).\n", slotsize, minsize);
		ret = OCFS2_ET_INVALID_ARGUMENT;
		goto out;
	}

	fprintf(stdout, "About to change the size of //slotmap from %llu bytes "
		"to %"PRIu64" bytes.\nContinue(y/N)? ", di->i_size, slotsize);
	while (1) {
		c = getchar();
		if (!isalpha(c))
			continue;
		if (c == 'Y' || c == 'y')
			break;
		/* We should add OCFS2_ET_ABORT_OPERATION */
		ret = OCFS2_ET_TOO_MANY_SLOTS;
		goto out;
	}

	di->i_size = slotsize;
	di->i_mtime = time(NULL);

	ret = ocfs2_write_cached_inode(fs, ci);
	if (ret)
		goto out;

out:
	if (ci)
		ocfs2_free_cached_inode(fs, ci);

	return ret;
}

extern int opterr, optind;
extern char *optarg;

int main(int argc, char *argv[])
{
	errcode_t ret;
	char *device, *p;
	uint64_t size;
	ocfs2_filesys *fs = NULL;
	int c;

	initialize_ocfs_error_table();

#define INSTALL_SIGNAL(sig)					\
	do {							\
		if (signal(sig, handle_signal) == SIG_ERR) {	\
		    printf("Could not set " #sig "\n");		\
		    goto bail;					\
		}						\
	} while (0)

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
	setbuf(stdin, NULL);

	progname = basename(argv[0]);

	if (argc < 3)
		usage();

	device = argv[1];

	size = strtoull(argv[2], &p, 0);
	if (!p || *p) {
		fprintf(stderr, "Error: Invalid size.\n");
		usage();
	}

	fprintf(stdout, "\nWARNING!!! Running %s with the file system mounted "
		"could lead to file system damage.\n", progname);
	fprintf(stdout, "Please ensure that the device \"%s\" is _not_ mounted "
		"on any node in the cluster.\nContinue(y/N)? ", device);
	while (1) {
		c = getchar();
		if (!isalpha(c))
			continue;
		if (c == 'Y' || c == 'y')
			break;
		goto out;
	}

	ret = ocfs2_open(device, OCFS2_FLAG_RW, 0, 0, &fs);
	if (ret) {
		com_err(progname, ret, "while opening device \"%s\"", device);
		usage();
	}

	ret = resize_slot_map_file(fs, size);
	if (!ret)
		fprintf(stdout, "Changed the size of //slotmap on device "
			"\"%s\" to %"PRIu64" bytes.\n", device, size);
	if (ret && ret != OCFS2_ET_TOO_MANY_SLOTS)
		com_err(progname, ret, "while resizing //slotmap on device "
			"\"%s\"", device);

out:
	if (fs)
		ocfs2_close(fs);

	return 0;
}
