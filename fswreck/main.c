/*
 * main.c
 *
 * entry point for fswrk
 *
 * Copyright (C) 2006 Oracle.  All rights reserved.
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
 */

#include "main.h"


char *progname = NULL;

static char *device = NULL;
static uint16_t slotnum = UINT16_MAX;

struct corrupt_funcs {
	void (*func) (ocfs2_filesys *fs, int code, uint16_t slotnum);
	char *desc;
};

static struct corrupt_funcs cf[MAX_CORRUPT] = {
	{ NULL, 		"Not applicable"},
	{ NULL, 		"Not applicable"},
	{ NULL, 		"Not applicable"},
				/* Following relates to the Global bitmap: */
	{ &corrupt_chains, 	"Delink the last chain from the inode"},
	{ &corrupt_chains, 	"Corrupt cl_count"},
	{ &corrupt_chains,	"Corrupt cl_next_free_rec"},
	{ NULL,			"Not applicable"},
	{ &corrupt_chains,	"Corrupt id1.bitmap1.i_total/i_used"},
	{ &corrupt_chains,	"Corrupt c_blkno of the first record with a number larger than volume size"},
	{ NULL,			"Not applicable"},
	{ &corrupt_chains,	"Corrupt c_blkno of the first record with an unaligned number"},
	{ &corrupt_chains,	"Corrupt c_blkno of the first record with 0"},
	{ &corrupt_chains,	"Corrupt c_total/c_free of the first record"},
	{ &corrupt_file,	"Extent block error: EB_BLKNO, EB_GEN, EB_GEN_FIX, EXTENT_EB_INVALID"},
	{ &corrupt_file,	"Extent list error: EXTENT_LIST_DEPTH, EXTENT_LIST_COUNT, EXTENT_LIST_FREE"},
	{ &corrupt_file,	"Extent record error: EXTENT_BLKNO_UNALIGNED, EXTENT_CLUSTERS_OVERRUN, EXTENT_BLKNO_RANGE"},
	{ &corrupt_sys_file,	"Chain list error:	CHAIN_COUNT, CHAIN_NEXT_FREE"},
	{ &corrupt_sys_file,	"Chain record error: CHAIN_EMPTY, CHAIN_HEAD_LINK_RANGE, CHAIN_BITS, CLUSTER_ALLOC_BIT"},
	{ &corrupt_sys_file,	"Chain inode error: CHAIN_I_CLUSTERS, CHAIN_I_SIZE, CHAIN_GROUP_BITS"},
	{ &corrupt_sys_file,	"Chain group error: CHAIN_LINK_GEN, CHAIN_LINK_RANGE"},
	{ &corrupt_sys_file,	"Group magic error: CHAIN_LINK_MAGIC"},
	{ &corrupt_sys_file,	"Chain resize error: CHAIN_CPG"},
	{ &corrupt_sys_file,	"Superblock error: SUPERBLOCK_CLUSTERS_EXCESS, excess error"},
	{ &corrupt_sys_file,	"Superblock error: SUPERBLOCK_CLUSTERS_LACK, lack error"},
				/* Following relates to corrupting group descriptor */
	{ &corrupt_group_desc,	"Group minor field error: GROUP_PARENT, GROUP_BLKNO, GROUP_CHAIN, GROUP_FREE_BITS"},
	{ &corrupt_group_desc,	"Group generation error: GROUP_GEN"},
	{ &corrupt_group_desc,	"Group list error: GROUP_UNEXPECTED_DESC, GROUP_EXPECTED_DESC"},
				/* Following relates to corrupting inode information */
	{ &corrupt_file,	"Inode field error: INODE_SUBALLOC, INODE_GEN, INODE_GEN_FIX,INODE_BLKNO,"
				"INODE_NZ_DTIME, INODE_SIZE, INODE_CLUSTERS, INODE_COUNT"},
	{ &corrupt_file,	"Inode link not connected error: INODE_LINK_NOT_CONNECTED "},
	{ &corrupt_sys_file,	"Inode orphaned error:	INODE_ORPHANED"},
	{ &corrupt_sys_file,	"Inode alloc error:	INODE_ALLOC_REPAIR"},
				/* Following relates to corrupting local alloc file */
	{ &corrupt_local_alloc,	"Empty local alloc  error:	LALLOC_SIZE, LALLOC_NZ_USED, LALLOC_NZ_BM"},
	{ &corrupt_local_alloc,	"Local alloc bitmap error: 	LALLOC_BM_OVERRUN, LALLOC_BM_STRADDLE,LALLOC_BM_SIZE"},
	{ &corrupt_local_alloc,	"Local alloc used info error:	LALLOC_USED_OVERRUN, LALLOC_CLEAR"},
				/* Following relates to corrupting truncate log file */
 	{ &corrupt_truncate_log,"Truncate log list error: 	DEALLOC_COUNT, DEALLOC_USED"},
 	{ &corrupt_truncate_log,"Truncate log rec error: 	TRUNCATE_REC_START_RANGE, TRUNCATE_REC_WRAP, TRUNCATE_REC_RANGE"},
				/* Following relates to corrupting symlink file */
 	{ &corrupt_file,	"Link file error: LINK_FAST_DATA, LINK_NULLTERM, LINK_SIZE, LINK_BLOCKS"},
				/* Following relates to corrupting root and lost+found */
 	{ &corrupt_file,	"Special files error: ROOT_NOTDIR, ROOT_DIR_MISSING, LOSTFOUND_MISSING"},
				/* Following relates to corrupting directory */
 	{ &corrupt_file,	"Directory inode error: DIR_ZERO"},
 	{ &corrupt_file,	"Dirent dot error: DIRENT_DOTTY_DUP, DIRENT_NOT_DOTTY, DIRENT_DOT_INODE, DIRENT_DOT_EXCESS"},
	{ &corrupt_file,	"Dirent field error: DIRENT_ZERO, DIRENT_NAME_CHARS,DIRENT_INODE_RANGE, DIRENT_INODE_FREE, DIRENT_TYPE, DIRENT_DUPLICATE, DIRENT_LENGTH"}, 
	{ &corrupt_file,	"Directory parent duplicate error: DIR_PARENT_DUP"},
	{ &corrupt_file,	"Directory not connected error: DIR_NOT_CONNECTED"},
	{ &corrupt_group_desc,	"Create an error of GROUP_FREE_BITS and CLUSTER_ALLOC_BITS, simulate bug841 in oss.oracle.com/bugzilla"},
	{ &corrupt_file,        "Inline file dyn_features flag error: INLINE_DATA_FLAG_INVALID"},
	{ &corrupt_file,        "Inline file id_count,i_clusters and i_size error: INLINE_DATA_COUNT_INVALID"},
	{ &corrupt_file,        "Allocate the same cluster to two different files"},

};

static int corrupt[MAX_CORRUPT];
/*
 * usage()
 *
 */
static void usage (char *progname)
{
	int i;

	g_print ("%s is a program to corrupt a filesystem\n", progname);
        g_print ("THIS WILL DAMAGE YOUR FILESYSTEM.  USE AT YOUR OWN RISK.\n");
	g_print ("Usage: %s [OPTION]... [DEVICE]\n", progname);
	g_print ("	-n <node slot number>\n");
	g_print ("	-c <corrupt code>\n");
	g_print ("	corrupt code description:\n");
	for (i = 0; i < MAX_CORRUPT; i++)
		g_print ("	%02d - %s\n", i, cf[i].desc);

	exit (0);
}					/* usage */

/*
 * print_version()
 *
 */
static void print_version (char *progname)
{
	fprintf(stderr, "%s %s\n", progname, VERSION);
}					/* print_version */


/*
 * handle_signal()
 *
 */
static void handle_signal (int sig)
{
	switch (sig) {
	case SIGTERM:
	case SIGINT:
		exit(1);
	}

	return ;
}					/* handle_signal */


/*
 * read_options()
 *
 */
static int read_options(int argc, char **argv)
{
	int c;
	int ind;

	progname = basename(argv[0]);

	if (argc < 2) {
		usage(progname);
		return 1;
	}

	while(1) {
		c = getopt(argc, argv, "c:n:");
		if (c == -1)
			break;

		switch (c) {
		case 'c':	/* corrupt */
			ind = strtoul(optarg, NULL, 0);
			if (ind < MAX_CORRUPT)
				corrupt[ind] = 1;
			else {
				fprintf(stderr, "Invalid corrupt code:%d\n", ind);
				return -1;
			}
			break;

		case 'n':	/* slotnum */
			slotnum = strtoul(optarg, NULL, 0);
			break;

		default:
			break;
		}
	}

	if (optind < argc && argv[optind])
		device = argv[optind];

	return 0;
}

/*
 * main()
 *
 */
int main (int argc, char **argv)
{
	ocfs2_filesys *fs = NULL;
	errcode_t ret = 0;
	int i;

	initialize_ocfs_error_table();

#define INSTALL_SIGNAL(sig)					\
	do {							\
		if (signal(sig, handle_signal) == SIG_ERR) {	\
		    printf("Could not set " #sig "\n");		\
		    goto bail;					\
		}						\
	} while (0)

	INSTALL_SIGNAL(SIGTERM);
	INSTALL_SIGNAL(SIGINT);

	memset(corrupt, 0, sizeof(corrupt));

	if (read_options(argc, argv))
		goto bail;

	if (!device) {
		usage(progname);
		goto bail;
	}

	print_version (progname);

	ret = ocfs2_open(device, OCFS2_FLAG_RW, 0, 0, &fs);
	if (ret) {
		com_err(progname, ret, "while opening \"%s\"", device);
		goto bail;
	}

	for (i = 1; i < MAX_CORRUPT; ++i) {
		if (corrupt[i]) {
			if (cf[i].func)
				cf[i].func(fs, i, slotnum);
			else
				fprintf(stderr, "Unimplemented corrupt code = %d\n", i);
		}
	}

bail:
	if (fs) {
		ret = ocfs2_close(fs);
		if (ret)
			com_err(progname, ret, "while closing \"%s\"", device);
	}

	return 0;
}					/* main */
