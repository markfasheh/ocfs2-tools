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

static int corrupt[NUM_FSCK_TYPE];

struct prompt_code {

	enum fsck_type type;
	char *str;
	void (*func)(ocfs2_filesys *fs, enum fsck_type code, uint16_t slotnum);
	char *desc;
};

#define define_prompt_code(_type, _func, _desc) \
		[_type] = {			\
				.type = _type,  \
				.str = #_type,  \
				.func = _func,  \
				.desc = _desc,  \
		}

static struct prompt_code prompt_codes[NUM_FSCK_TYPE] = {

	define_prompt_code(EB_BLKNO, corrupt_file,
			   "Corrupt an extent block's eb_blkno field"),
	define_prompt_code(EB_GEN, corrupt_file,
			   "Corrupt an extent block's generation number"),
	define_prompt_code(EB_GEN_FIX, corrupt_file,
			   "Corrupt an extent block's generation number "
			   "so that fsck.ocfs2 can fix it"),
	define_prompt_code(EXTENT_EB_INVALID, corrupt_file,
			   "Corrupt an extent block's generation number"),
	define_prompt_code(EXTENT_BLKNO_UNALIGNED, corrupt_file,
			   "Corrupt extent record's e_blkno"),
	define_prompt_code(EXTENT_CLUSTERS_OVERRUN, corrupt_file,
			   "Corrupt extent record's e_leaf_clusters"),
	define_prompt_code(EXTENT_BLKNO_RANGE, corrupt_file,
			   "Corrupt extent record's e_blkno to 1"),
	define_prompt_code(EXTENT_LIST_DEPTH, corrupt_file,
			   "Corrupt first extent block's list depth of an inode"),
	define_prompt_code(EXTENT_LIST_COUNT, corrupt_file,
			   "Corrupt extent block's clusters"),
	define_prompt_code(EXTENT_LIST_FREE, corrupt_file,
			   "Corrupt extent block's l_next_free_rec"),
	define_prompt_code(INODE_SUBALLOC, corrupt_file,
			   "Corrupt inode's i_suballoc_slot field"),
	define_prompt_code(INODE_GEN, corrupt_file,
			   "Corrupt inode's i_generation field"),
	define_prompt_code(INODE_GEN_FIX, corrupt_file,
			   "Corrupt inode's i_generation field"),
	define_prompt_code(INODE_BLKNO, corrupt_file,
			   "Corrupt inode's i_blkno field"),
	define_prompt_code(INODE_NZ_DTIME, corrupt_file,
			   "Corrupt inode's i_dtime field"),
	define_prompt_code(INODE_SIZE, corrupt_file,
			   "Corrupt inode's i_size field"),
	define_prompt_code(INODE_CLUSTERS, corrupt_file,
			   "Corrupt inode's i_clusters field"),
	define_prompt_code(INODE_COUNT, corrupt_file,
			   "Corrupt inode's i_links_count field"),
	define_prompt_code(INODE_LINK_NOT_CONNECTED, corrupt_file,
			   "Create an inode which has no links"),
	define_prompt_code(INODE_NOT_CONNECTED, NULL,
			   "Unimplemented corrupt code"),
	define_prompt_code(LINK_FAST_DATA, corrupt_file,
			   "Corrupt symlink's i_clusters to 0"),
	define_prompt_code(LINK_NULLTERM, corrupt_file,
			   "Corrupt symlink's all blocks with dummy texts"),
	define_prompt_code(LINK_SIZE, corrupt_file,
			   "Corrupt symlink's i_size field"),
	define_prompt_code(LINK_BLOCKS, corrupt_file,
			   "Corrupt symlink's e_leaf_clusters field"),
	define_prompt_code(ROOT_NOTDIR, corrupt_file,
			   "Corrupt root inode, change its i_mode to 0"),
	define_prompt_code(ROOT_DIR_MISSING, corrupt_file,
			   "Corrupt root inode, change its i_mode to 0"),
	define_prompt_code(LOSTFOUND_MISSING, corrupt_file,
			   "Corrupt root inode, change its i_mode to 0"),
	define_prompt_code(DIR_DOTDOT, NULL,
			   "Unimplemented corrupt code"),
	define_prompt_code(DIR_ZERO, corrupt_file,
			   "Corrupt directory, empty its content"),
	define_prompt_code(DIRENT_DOTTY_DUP, corrupt_file,
			   "Duplicate '.' dirent to a directory"),
	define_prompt_code(DIRENT_NOT_DOTTY, corrupt_file,
			   "Corrupt directory's '.' dirent to a dummy one"),
	define_prompt_code(DIRENT_DOT_INODE, corrupt_file,
			   "Corrupt dot's inode no"),
	define_prompt_code(DIRENT_DOT_EXCESS, corrupt_file,
			   "Corrupt dot's dirent length"),
	define_prompt_code(DIRENT_ZERO, corrupt_file,
			   "Corrupt directory, add a zero dirent"),
	define_prompt_code(DIRENT_NAME_CHARS, corrupt_file,
			   "Corrupt directory, add a invalid dirent"),
	define_prompt_code(DIRENT_INODE_RANGE, corrupt_file,
			   "Corrupt directory, add an entry whose inode "
			   "exceeds the limits"),
	define_prompt_code(DIRENT_INODE_FREE, corrupt_file,
			   "Corrupt directory, add an entry whose inode "
			   "isn't used"),
	define_prompt_code(DIRENT_TYPE, corrupt_file,
			   "Corrupt dirent's mode"),
	define_prompt_code(DIRENT_DUPLICATE, corrupt_file,
			   "Add two duplicated dirents to dir"),
	define_prompt_code(DIRENT_LENGTH, corrupt_file,
			   "Corrupt dirent's length"),
	define_prompt_code(DIR_PARENT_DUP, corrupt_file,
			   "Create a dir with two '..' dirent"),
	define_prompt_code(DIR_NOT_CONNECTED, corrupt_file,
			   "Create a dir which has no connections"),
	define_prompt_code(INLINE_DATA_FLAG_INVALID, corrupt_file,
			   "Create an inlined inode on a unsupported volume"),
	define_prompt_code(INLINE_DATA_COUNT_INVALID, corrupt_file,
			   "Corrupt inlined inode's id_count, "
			   "i_size and i_clusters"),
	define_prompt_code(DUPLICATE_CLUSTERS, corrupt_file,
			   "Allocate same cluster to different files"),
	define_prompt_code(CHAIN_COUNT, corrupt_sys_file,
			   "Corrupt chain list's cl_count"),
	define_prompt_code(CHAIN_NEXT_FREE, corrupt_sys_file,
			   "Corrupt chain list's cl_next_free_rec"),
	define_prompt_code(CHAIN_EMPTY, corrupt_sys_file,
			   "Corrupt chain list's cl_recs into zero"),
	define_prompt_code(CHAIN_HEAD_LINK_RANGE, corrupt_sys_file,
			   "Corrupt chain list's header blkno"),
	define_prompt_code(CHAIN_BITS, corrupt_sys_file,
			   "Corrupt chain's total bits"),
	define_prompt_code(CLUSTER_ALLOC_BIT, NULL,
			   "Unimplemented corrupt code"),
	define_prompt_code(CHAIN_I_CLUSTERS, corrupt_sys_file,
			   "Corrupt chain allocator's i_clusters"),
	define_prompt_code(CHAIN_I_SIZE, corrupt_sys_file,
			   "Corrupt chain allocator's i_size"),
	define_prompt_code(CHAIN_GROUP_BITS, corrupt_sys_file,
			   "Corrupt chain allocator's i_used of bitmap"),
	define_prompt_code(CHAIN_LINK_GEN, corrupt_sys_file,
			   "Corrupt allocation group descriptor's "
			   "bg_generation field"),
	define_prompt_code(CHAIN_LINK_RANGE, corrupt_sys_file,
			   "Corrupt allocation group descriptor's "
			   "bg_next_group field"),
	define_prompt_code(CHAIN_LINK_MAGIC, corrupt_sys_file,
			   "Corrupt allocation group descriptor's "
			   "bg_signature field"),
	define_prompt_code(CHAIN_CPG, corrupt_sys_file,
			   "Corrupt chain list's cl_cpg of global_bitmap"),
	define_prompt_code(SUPERBLOCK_CLUSTERS_EXCESS, corrupt_sys_file,
			   "Corrupt sb's i_clusters by wrong increment"),
	define_prompt_code(SUPERBLOCK_CLUSTERS_LACK, corrupt_sys_file,
			   "Corrupt sb's i_clusters by wrong decrement"),
	define_prompt_code(INODE_ORPHANED, corrupt_sys_file,
			   "Create an inode under orphan dir"),
	define_prompt_code(INODE_ALLOC_REPAIR, corrupt_sys_file,
			   "Create an invalid inode"),
	define_prompt_code(GROUP_PARENT, corrupt_group_desc,
			   "Corrupt chain group's group parent"),
	define_prompt_code(GROUP_BLKNO, corrupt_group_desc,
			   "Corrupt chain group's blkno"),
	define_prompt_code(GROUP_CHAIN, corrupt_group_desc,
			   "Corrupt chain group's chain where it was in"),
	define_prompt_code(GROUP_FREE_BITS, corrupt_group_desc,
			   "Corrupt chain group's free bits"),
	define_prompt_code(GROUP_GEN, corrupt_group_desc,
			   "Corrupt chain group's generation"),
	define_prompt_code(GROUP_UNEXPECTED_DESC, corrupt_group_desc,
			   "Add a fake description to chain"),
	define_prompt_code(GROUP_EXPECTED_DESC, corrupt_group_desc,
			   "Delete the right description from chain"),
	define_prompt_code(CLUSTER_GROUP_DESC, corrupt_group_desc,
			   "Corrupt chain group's clusters and free bits"),
	define_prompt_code(LALLOC_SIZE, corrupt_local_alloc,
			   "Corrupt local alloc's size"),
	define_prompt_code(LALLOC_NZ_USED, corrupt_local_alloc,
			   "Corrupt local alloc's used and total clusters"),
	define_prompt_code(LALLOC_NZ_BM, corrupt_local_alloc,
			   "Corrupt local alloc's starting bit offset"),
	define_prompt_code(LALLOC_BM_OVERRUN, corrupt_local_alloc,
			   "Overrun local alloc's starting bit offset"),
	define_prompt_code(LALLOC_BM_STRADDLE, corrupt_local_alloc,
			   "Straddle local alloc's starting bit offset"),
	define_prompt_code(LALLOC_BM_SIZE, corrupt_local_alloc,
			   "Corrupt local alloc bitmap's i_total"),
	define_prompt_code(LALLOC_USED_OVERRUN, corrupt_local_alloc,
			   "Corrupt local alloc bitmap's i_used"),
	define_prompt_code(LALLOC_CLEAR, corrupt_local_alloc,
			   "Corrupt local alloc's size"),
	define_prompt_code(LALLOC_REPAIR, NULL,
			   "Unimplemented corrupt code"),
	define_prompt_code(LALLOC_USED, NULL,
			   "Unimplemented corrupt code"),
	define_prompt_code(DEALLOC_COUNT, corrupt_truncate_log,
			   "Corrupt truncate log's tl_count"),
	define_prompt_code(DEALLOC_USED, corrupt_truncate_log,
			   "Corrupt truncate log's tl_used"),
	define_prompt_code(TRUNCATE_REC_START_RANGE, corrupt_truncate_log,
			   "Corrupt truncate log's t_start"),
	define_prompt_code(TRUNCATE_REC_WRAP, corrupt_truncate_log,
			   "Corrupt truncate log's tl_recs"),
	define_prompt_code(TRUNCATE_REC_RANGE, corrupt_truncate_log,
			   "Corrupt truncate log's t_clusters"),
};

#undef define_prompt_code

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
	for (i = 0; i < NUM_FSCK_TYPE; i++)
		fprintf(stdout, "	%s - %s\n", prompt_codes[i].str,
			 prompt_codes[i].desc);

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

static int corrupt_code_match(const char *corrupt_code)
{
	int i;

	for (i = 0; i < NUM_FSCK_TYPE; i++)
		if (strcmp(corrupt_code, prompt_codes[i].str) == 0)
			return i;

	return -1;
}

static int parse_corrupt_codes(const char *corrupt_codes)
{
	int ret = 0, i = -2;
	char *p;
	char *token = NULL;

	p = corrupt_codes;

	while (p) {

		token = p;
		p = strchr(p, ',');

		if (p)
			*p = 0;

		if (strcmp(token, "") != 0) {

			i = corrupt_code_match(token);

			if (i >= 0)
				corrupt[i] = 1;
			else {

				fprintf(stderr, "Corrupt code \"%s\" was not "
					"supported.\n", token);
				ret = -1;
				break;
			}

		}

		if (!p)
			continue;
		p++;

	}

	if (i == -2) {
		fprintf(stderr, "At least one corrupt code needed.\n");
		ret = -1;
	}

	return ret;
}


/*
 * read_options()
 *
 */
static int read_options(int argc, char **argv)
{
	int c;
	int ret = 0;

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
			ret = parse_corrupt_codes(optarg);
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

	if (ret < 0)
		usage(progname);

	return ret;
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

	for (i = 1; i < NUM_FSCK_TYPE; ++i) {
		if (corrupt[i]) {
			if (prompt_codes[i].func)
				prompt_codes[i].func(fs, prompt_codes[i].type,
						     slotnum);
			else
				fprintf(stderr, "Unimplemented corrupt code "
					"= %s\n", prompt_codes[i].str);
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
