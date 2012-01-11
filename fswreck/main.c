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
	enum fsck_type	pc_codenum;
	char		*pc_codestr;
	char		*pc_fsfeat;
	int		pc_slots;
	void		(*pc_func)(ocfs2_filesys *fs,
				   enum fsck_type code, uint16_t slotnum);
	char		*pc_desc;
};

#define define_prompt_code(_code, _func, _feat, _slots, _desc)	\
		[_code] = {					\
				.pc_codenum	= _code,	\
				.pc_codestr	= #_code,	\
				.pc_fsfeat	= _feat,	\
				.pc_slots	= _slots,	\
				.pc_func	= _func,	\
				.pc_desc	= _desc,	\
		}

static struct prompt_code prompt_codes[NUM_FSCK_TYPE] = {
	define_prompt_code(EB_BLKNO, corrupt_file, "", 1,
			   "Corrupt an extent block's eb_blkno field"),
	define_prompt_code(EB_GEN, corrupt_file, "", 1,
			   "Corrupt an extent block's generation number"),
	define_prompt_code(EB_GEN_FIX, corrupt_file, "", 1,
			   "Corrupt an extent block's generation number "
			   "so that fsck.ocfs2 can fix it"),
	define_prompt_code(EXTENT_MARKED_UNWRITTEN, corrupt_file, "nounwritten", 1,
			   "Mark an extent unwritten when the filesystem "
			   "does not support it"),
	define_prompt_code(EXTENT_MARKED_REFCOUNTED, corrupt_file, "norefcount", 1,
			   "Mark an extent refcounted when the filesystem "
			   "does not support it"),
	define_prompt_code(EXTENT_BLKNO_UNALIGNED, corrupt_file, "", 1,
			   "Corrupt extent record's e_blkno"),
	define_prompt_code(EXTENT_CLUSTERS_OVERRUN, corrupt_file, "", 1,
			   "Corrupt extent record's e_leaf_clusters"),
	define_prompt_code(EXTENT_EB_INVALID, corrupt_file, "", 1,
			   "Corrupt an extent block's generation number"),
	define_prompt_code(EXTENT_LIST_DEPTH, corrupt_file, "", 1,
			   "Corrupt first extent block's list depth of an inode"),
	define_prompt_code(EXTENT_LIST_COUNT, corrupt_file, "", 1,
			   "Corrupt extent block's clusters"),
	define_prompt_code(EXTENT_LIST_FREE, corrupt_file, "", 1,
			   "Corrupt extent block's l_next_free_rec"),
	define_prompt_code(EXTENT_BLKNO_RANGE, corrupt_file, "", 1,
			   "Corrupt extent record's e_blkno to 1"),
	define_prompt_code(EXTENT_OVERLAP, corrupt_file, "", 1,
			   "Corrupt extent record's e_cpos to overlap"),
	define_prompt_code(EXTENT_HOLE, corrupt_file, "", 1,
			   "Corrupt extent record's e_cpos to create hole"),
	define_prompt_code(CHAIN_CPG, corrupt_sys_file, "", 1,
			   "Corrupt chain list's cl_cpg of global_bitmap"),
	define_prompt_code(SUPERBLOCK_CLUSTERS_EXCESS, corrupt_sys_file, "nometaecc", 1,
			   "Corrupt sb's i_clusters by wrong increment"),
	define_prompt_code(SUPERBLOCK_CLUSTERS_LACK, corrupt_sys_file, "nometaecc", 1,
			   "Corrupt sb's i_clusters by wrong decrement"),
	define_prompt_code(GROUP_UNEXPECTED_DESC, corrupt_group_desc, "", 1,
			   "Add a fake description to chain"),
	define_prompt_code(GROUP_EXPECTED_DESC, corrupt_group_desc, "", 1,
			   "Delete the right description from chain"),
	define_prompt_code(GROUP_GEN, corrupt_group_desc, "", 1,
			   "Corrupt chain group's generation"),
	define_prompt_code(GROUP_PARENT, corrupt_group_desc, "", 1,
			   "Corrupt chain group's group parent"),
	define_prompt_code(GROUP_BLKNO, corrupt_group_desc, "", 1,
			   "Corrupt chain group's blkno"),
	define_prompt_code(GROUP_CHAIN, corrupt_group_desc, "", 1,
			   "Corrupt chain group's chain where it was in"),
	define_prompt_code(GROUP_FREE_BITS, corrupt_group_desc, "", 1,
			   "Corrupt chain group's free bits"),
	define_prompt_code(CHAIN_COUNT, corrupt_sys_file, "", 1,
			   "Corrupt chain list's cl_count"),
	define_prompt_code(CHAIN_NEXT_FREE, corrupt_sys_file, "", 1,
			   "Corrupt chain list's cl_next_free_rec"),
	define_prompt_code(CHAIN_EMPTY, corrupt_sys_file, "", 1,
			   "Corrupt chain list's cl_recs into zero"),
	define_prompt_code(CHAIN_I_CLUSTERS, corrupt_sys_file, "", 1,
			   "Corrupt chain allocator's i_clusters"),
	define_prompt_code(CHAIN_I_SIZE, corrupt_sys_file, "", 1,
			   "Corrupt chain allocator's i_size"),
	define_prompt_code(CHAIN_GROUP_BITS, corrupt_sys_file, "", 1,
			   "Corrupt chain allocator's i_used of bitmap"),
	define_prompt_code(CHAIN_HEAD_LINK_RANGE, corrupt_sys_file, "", 1,
			   "Corrupt chain list's header blkno"),
	define_prompt_code(CHAIN_LINK_GEN, corrupt_sys_file, "", 1,
			   "Corrupt allocation group descriptor's "
			   "bg_generation field"),
	define_prompt_code(CHAIN_LINK_MAGIC, corrupt_sys_file, "", 1,
			   "Corrupt allocation group descriptor's "
			   "bg_signature field"),
	define_prompt_code(CHAIN_LINK_RANGE, corrupt_sys_file, "", 1,
			   "Corrupt allocation group descriptor's "
			   "bg_next_group field"),
	define_prompt_code(CHAIN_BITS, corrupt_sys_file, "", 1,
			   "Corrupt chain's total bits"),
	define_prompt_code(DISCONTIG_BG_DEPTH, corrupt_discontig_bg, "", 1,
			   "corrupt extent tree depth for a discontig bg"),
	define_prompt_code(DISCONTIG_BG_COUNT, corrupt_discontig_bg, "", 1,
			   "corrupt extent list count for a discontig bg"),
	define_prompt_code(DISCONTIG_BG_REC_RANGE, corrupt_discontig_bg, "", 1,
			   "corrupt extent rec range for a discontig bg"),
	define_prompt_code(DISCONTIG_BG_CORRUPT_LEAVES, corrupt_discontig_bg, "", 1,
			   "corrupt extent recs' clusters for a discontig bg"),
	define_prompt_code(DISCONTIG_BG_CLUSTERS, corrupt_discontig_bg, "", 1,
			   "corrupt a discontig bg by more clusters allocated"),
	define_prompt_code(DISCONTIG_BG_LESS_CLUSTERS, corrupt_discontig_bg, "", 1,
			   "corrupt a discontig bg by less clusters allocated"),
	define_prompt_code(DISCONTIG_BG_NEXT_FREE_REC, corrupt_discontig_bg, "", 1,
			   "corrupt extent list's next free of a discontig bg"),
	define_prompt_code(DISCONTIG_BG_LIST_CORRUPT, corrupt_discontig_bg, "", 1,
			   "corrupt extent list and rec for  a discontig bg"),
	define_prompt_code(DISCONTIG_BG_REC_CORRUPT, corrupt_discontig_bg, "", 1,
			   "corrupt extent rec for a discontig bg"),
	define_prompt_code(DISCONTIG_BG_LEAF_CLUSTERS, corrupt_discontig_bg, "", 1,
			   "corrupt extent rec's clusters for a discontig bg"),
	define_prompt_code(INODE_SUBALLOC, corrupt_file, "", 1,
			   "Corrupt inode's i_suballoc_slot field"),
	define_prompt_code(INODE_GEN, corrupt_file, "", 1,
			   "Corrupt inode's i_generation field"),
	define_prompt_code(INODE_GEN_FIX, corrupt_file, "", 1,
			   "Corrupt inode's i_generation field"),
	define_prompt_code(INODE_BLKNO, corrupt_file, "", 1,
			   "Corrupt inode's i_blkno field"),
	define_prompt_code(INODE_NZ_DTIME, corrupt_file, "", 1,
			   "Corrupt inode's i_dtime field"),
	define_prompt_code(INODE_SIZE, corrupt_file, "", 1,
			   "Corrupt inode's i_size field"),
	define_prompt_code(INODE_SPARSE_SIZE, corrupt_file, "", 1,
			   "Corrupt sparse inode's i_size field"),
	define_prompt_code(INODE_CLUSTERS, corrupt_file, "", 1,
			   "Corrupt inode's i_clusters field"),
	define_prompt_code(INODE_SPARSE_CLUSTERS, corrupt_file, "", 1,
			   "Corrupt sparse inode's i_clusters field"),
	define_prompt_code(INODE_COUNT, corrupt_file, "", 1,
			   "Corrupt inode's i_links_count field"),
	define_prompt_code(INODE_NOT_CONNECTED, corrupt_file, "", 1,
			   "Create an inode which has no links to dentries"),
	define_prompt_code(LINK_FAST_DATA, corrupt_file, "", 1,
			   "Corrupt symlink's i_clusters to 0"),
	define_prompt_code(LINK_NULLTERM, corrupt_file, "", 1,
			   "Corrupt symlink's all blocks with dummy texts"),
	define_prompt_code(LINK_SIZE, corrupt_file, "", 1,
			   "Corrupt symlink's i_size field"),
	define_prompt_code(LINK_BLOCKS, corrupt_file, "", 1,
			   "Corrupt symlink's e_leaf_clusters field"),
	define_prompt_code(ROOT_NOTDIR, corrupt_file, "", 1,
			   "Corrupt root inode, change its i_mode to 0"),
	define_prompt_code(ROOT_DIR_MISSING, corrupt_file, "", 1,
			   "Corrupt root inode, change its i_mode to 0"),
	define_prompt_code(LOSTFOUND_MISSING, corrupt_file, "", 1,
			   "Corrupt root inode, change its i_mode to 0"),
	define_prompt_code(DIR_DOTDOT, corrupt_file, "", 1,
			   "Corrupt dir's dotdot entry's ino it points to"),
	define_prompt_code(DIR_ZERO, corrupt_file, "noinline-data", 1,
			   "Corrupt directory, empty its content"),
	define_prompt_code(DIR_HOLE, corrupt_file, "", 1,
			   "Create a hole in the directory"),
	define_prompt_code(DIRENT_DOTTY_DUP, corrupt_file, "", 1,
			   "Duplicate '.' dirent to a directory"),
	define_prompt_code(DIRENT_NOT_DOTTY, corrupt_file, "", 1,
			   "Corrupt directory's '.' dirent to a dummy one"),
	define_prompt_code(DIRENT_DOT_INODE, corrupt_file, "", 1,
			   "Corrupt dot's inode no"),
	define_prompt_code(DIRENT_DOT_EXCESS, corrupt_file, "", 1,
			   "Corrupt dot's dirent length"),
	define_prompt_code(DIRENT_ZERO, corrupt_file, "", 1,
			   "Corrupt directory, add a zero dirent"),
	define_prompt_code(DIRENT_NAME_CHARS, corrupt_file, "", 1,
			   "Corrupt directory, add a invalid dirent"),
	define_prompt_code(DIRENT_INODE_RANGE, corrupt_file, "", 1,
			   "Corrupt directory, add an entry whose inode "
			   "exceeds the limits"),
	define_prompt_code(DIRENT_INODE_FREE, corrupt_file, "", 1,
			   "Corrupt directory, add an entry whose inode "
			   "isn't used"),
	define_prompt_code(DIRENT_TYPE, corrupt_file, "", 1,
			   "Corrupt dirent's mode"),
	define_prompt_code(DIRENT_DUPLICATE, corrupt_file, "", 1,
			   "Add two duplicated dirents to dir"),
	define_prompt_code(DIRENT_LENGTH, corrupt_file, "", 1,
			   "Corrupt dirent's length"),
	define_prompt_code(DIR_PARENT_DUP, corrupt_file, "", 1,
			   "Create a dir with two '..' dirent"),
	define_prompt_code(DIR_NOT_CONNECTED, corrupt_file, "", 1,
			   "Create a dir which has no connections"),
	define_prompt_code(INLINE_DATA_FLAG_INVALID, corrupt_file, "noinline-data", 1,
			   "Create an inlined inode on a unsupported volume"),
	define_prompt_code(INLINE_DATA_COUNT_INVALID, corrupt_file, "", 1,
			   "Corrupt inlined inode's id_count"),
	define_prompt_code(INODE_INLINE_SIZE, corrupt_file, "", 1,
			   "Corrupt inlined inode's i_size"),
	define_prompt_code(INODE_INLINE_CLUSTERS, corrupt_file, "", 1,
			   "Corrupt inlined inode's i_clusters"),
	define_prompt_code(DUP_CLUSTERS_CLONE, corrupt_file, "", 1,
			   "Allocate same cluster to different files"),
	define_prompt_code(DUP_CLUSTERS_DELETE, corrupt_file, "", 1,
			   "Allocate same cluster to different files"),
	define_prompt_code(DUP_CLUSTERS_SYSFILE_CLONE, corrupt_file, "", 1,
			   "Allocate same cluster to different system files"),
	define_prompt_code(CLUSTER_ALLOC_BIT, corrupt_group_desc, "", 1,
			   "Mark bits of global bitmap by unused clusters"),
	define_prompt_code(INODE_ORPHANED, corrupt_sys_file, "", 1,
			   "Create an inode under orphan dir"),
	define_prompt_code(INODE_ALLOC_REPAIR, corrupt_sys_file, "", 1,
			   "Create an invalid inode"),
	define_prompt_code(CLUSTER_GROUP_DESC, corrupt_group_desc, "", 1,
			   "Corrupt chain group's clusters and free bits"),
	define_prompt_code(LALLOC_SIZE, corrupt_local_alloc, "", 1,
			   "Corrupt local alloc's size"),
	define_prompt_code(LALLOC_NZ_USED, corrupt_local_alloc, "", 1,
			   "Corrupt local alloc's used and total clusters"),
	define_prompt_code(LALLOC_NZ_BM, corrupt_local_alloc, "", 1,
			   "Corrupt local alloc's starting bit offset"),
	define_prompt_code(LALLOC_BM_OVERRUN, corrupt_local_alloc, "", 1,
			   "Overrun local alloc's starting bit offset"),
	define_prompt_code(LALLOC_BM_STRADDLE, corrupt_local_alloc, "", 1,
			   "Straddle local alloc's starting bit offset"),
	define_prompt_code(LALLOC_BM_SIZE, corrupt_local_alloc, "", 1,
			   "Corrupt local alloc bitmap's i_total"),
	define_prompt_code(LALLOC_USED_OVERRUN, corrupt_local_alloc, "", 1,
			   "Corrupt local alloc bitmap's i_used"),
	define_prompt_code(LALLOC_CLEAR, corrupt_local_alloc, "", 1,
			   "Corrupt local alloc's size"),
	define_prompt_code(LALLOC_REPAIR, NULL, "", 1,
			   "Unimplemented corrupt code"),
	define_prompt_code(LALLOC_USED, NULL, "", 1,
			   "Unimplemented corrupt code"),
	define_prompt_code(DEALLOC_COUNT, corrupt_truncate_log, "", 1,
			   "Corrupt truncate log's tl_count"),
	define_prompt_code(DEALLOC_USED, corrupt_truncate_log, "", 1,
			   "Corrupt truncate log's tl_used"),
	define_prompt_code(TRUNCATE_REC_START_RANGE, corrupt_truncate_log, "", 1,
			   "Corrupt truncate log's t_start"),
	define_prompt_code(TRUNCATE_REC_WRAP, corrupt_truncate_log, "", 1,
			   "Corrupt truncate log's tl_recs"),
	define_prompt_code(TRUNCATE_REC_RANGE, corrupt_truncate_log, "", 1,
			   "Corrupt truncate log's t_clusters"),
	define_prompt_code(JOURNAL_FILE_INVALID, corrupt_sys_file, "", 1,
			   "Corrupt journal file as an invalid one."),
	define_prompt_code(JOURNAL_UNKNOWN_FEATURE, corrupt_sys_file, "", 1,
			   "Corrupt journal file with unknown feature ."),
	define_prompt_code(JOURNAL_MISSING_FEATURE, corrupt_sys_file, "", 4,
			   "Corrupt journal file by missing features."),
	define_prompt_code(JOURNAL_TOO_SMALL, corrupt_sys_file, "", 1,
			   "Corrupt journal file as a too small one."),
	define_prompt_code(QMAGIC_INVALID, corrupt_sys_file, "", 1,
			   "Corrupt quota system file's header."),
	define_prompt_code(QTREE_BLK_INVALID, corrupt_sys_file, "", 1,
			   "Corrupt quota tree block."),
	define_prompt_code(DQBLK_INVALID, corrupt_sys_file, "", 1,
			   "Corrupt quota data blok."),
	define_prompt_code(DUP_DQBLK_INVALID, corrupt_sys_file, "", 1,
			   "Duplicate a invalid quota limits."),
	define_prompt_code(DUP_DQBLK_VALID, corrupt_sys_file, "", 1,
			   "Duplicate a valid quota limits."),
	define_prompt_code(REFCOUNT_FLAG_INVALID, corrupt_file, "", 1,
			   "Create a refcounted inode on a unsupported volume"),
	define_prompt_code(REFCOUNT_LOC_INVALID, corrupt_file, "", 1,
			   "Corrupt a refcounted file's refcount location"),
	define_prompt_code(RB_BLKNO, corrupt_refcount, "", 1,
			   "Corrupt a refcount block's rf_blkno"),
	define_prompt_code(RB_GEN, corrupt_refcount, "", 1,
			   "Corrupt a refcount block's generation"),
	define_prompt_code(RB_GEN_FIX, corrupt_refcount, "", 1,
			   "Corrupt a refcount block's generation"),
	define_prompt_code(RB_PARENT, corrupt_refcount, "", 1,
			   "Corrupt a refcount block's rf_parent"),
	define_prompt_code(REFCOUNT_BLOCK_INVALID, corrupt_refcount, "", 1,
			   "Corrupt a refcount block's rf_parent"),
	define_prompt_code(REFCOUNT_ROOT_BLOCK_INVALID, corrupt_refcount, "", 1,
			   "Corrupt a refcount block's rf_parent"),
	define_prompt_code(REFCOUNT_LIST_COUNT, corrupt_refcount, "", 1,
			   "corrupt the refcount list in a refcount block"),
	define_prompt_code(REFCOUNT_LIST_USED, corrupt_refcount, "", 1,
			   "corrupt the refcount list in a refcount block"),
	define_prompt_code(REFCOUNT_CLUSTER_RANGE, corrupt_refcount, "", 1,
			   "corrupt the refcount list in a refcount block"),
	define_prompt_code(REFCOUNT_CLUSTER_COLLISION, corrupt_refcount, "", 1,
			   "corrupt the refcount list in a refcount block"),
	define_prompt_code(REFCOUNT_LIST_EMPTY, corrupt_refcount, "", 1,
			   "corrupt the refcount list in a refcount block"),
	define_prompt_code(REFCOUNT_CLUSTERS, corrupt_refcount, "", 1,
			   "corrupt the rf_clusters for a refcount tree"),
	define_prompt_code(REFCOUNT_COUNT, corrupt_refcount, "", 1,
			   "corrupt the rf_count for a refcount tree"),
	define_prompt_code(REFCOUNT_REC_REDUNDANT, corrupt_refcount, "", 1,
			   "corrupt the refcount record in a refcount block"),
	define_prompt_code(REFCOUNT_COUNT_INVALID, corrupt_refcount, "", 1,
			   "corrupt the refcount record in a refcount block"),
	define_prompt_code(DUP_CLUSTERS_ADD_REFCOUNT, corrupt_refcount, "", 1,
			   "corrupt refcount record and handle them in dup"),
};

#undef define_prompt_code

/*
 * usage()
 *
 */
static void usage(void)
{
	g_print("%s is a program to corrupt a filesystem\n", progname);
        g_print("***** THIS WILL DAMAGE YOUR FILESYSTEM.  USE AT YOUR OWN RISK. *****\n");
	g_print("Usage: %s [-c corrupt-string] [-C corrupt-number] [-L corrupt-number] " \
		"[-N slot-number] [-nlM] [DEVICE]\n", progname);
	g_print("	-c, -C, Corrupt the file system\n");
	g_print("	-L, Prints the corresponsing corrupt-string\n");
	g_print("	-l, Lists all the corrupt codes\n");
	g_print("	-n, Prints the total number of corrupt codes\n");
	g_print("	-M, Prints the mkfs options\n");
	exit(0);
}

static void print_codes(void)
{
	int i, len;

	g_print("Corrupt codes:\n");
	for (len = 0, i = 0; i < NUM_FSCK_TYPE; ++i)
		len = ocfs2_max(len, (int)strlen(prompt_codes[i].pc_codestr));
	for (i = 0; i < NUM_FSCK_TYPE; i++)
		fprintf(stdout, "%3d  %-*s  %s\n", prompt_codes[i].pc_codenum,
			len, prompt_codes[i].pc_codestr,
			prompt_codes[i].pc_desc);
	exit(0);
}

/*
 * print_version()
 *
 */
static void print_version(void)
{
	fprintf(stderr, "%s %s\n", progname, VERSION);
}


/*
 * handle_signal()
 *
 */
static void handle_signal(int sig)
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
		if (strcmp(corrupt_code, prompt_codes[i].pc_codestr) == 0)
			return i;

	return -1;
}

static int parse_corrupt_codes(const char *corrupt_codes)
{
	int ret = 0, i = -2;
	char *p;
	char *token = NULL;

	p = (char *)corrupt_codes;

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
	int i, c, listcode = 0, showmkfs = 0;
	int ret = 0;

	progname = basename(argv[0]);

	if (argc < 2)
		usage();

	while (1) {
		c = getopt(argc, argv, "lnc:C:N:L:M");
		if (c == -1)
			break;

		switch (c) {
		case 'c':	/* corrupt code string */
			ret = parse_corrupt_codes(optarg);
			break;

		case 'l':
			print_codes();
			break;

		case 'L':
			listcode = 1;
		case 'C':	/* corrupt code number */
			ret = atoi(optarg);
			if (ret < NUM_FSCK_TYPE)
				corrupt[ret] = 1;
			else {
				fprintf(stderr, "Corrupt code \"%d\" is not "
					"supported.\n", ret);
				exit(-1);
			}
			break;

		case 'M':	/* mkfs features */
			showmkfs = 1;
			break;

		case 'n':
			fprintf(stdout, "%d\n", NUM_FSCK_TYPE);
			exit(NUM_FSCK_TYPE);

		case 'N':	/* slotnum */
			slotnum = strtoul(optarg, NULL, 0);
			break;

		default:
			ret = -1;
			goto out;
		}
	}

	if (listcode) {
		for (i = 0; i < NUM_FSCK_TYPE; i++) {
			if (corrupt[i]) {
				fprintf(stdout, "%s\n",
					prompt_codes[i].pc_codestr);
				listcode = 0;
				break;
			}
		}
		exit(listcode);
	}

#define MKFS_PARAMS_FIX		\
	"-b 4096 -C 4096 --fs-feature-level=max-features -J size=16M "	\
	"-L fswreck -M local"

	if (showmkfs) {
		for (i = 0; i < NUM_FSCK_TYPE; i++) {
			if (corrupt[i]) {
				fprintf(stdout, MKFS_PARAMS_FIX);
				fprintf(stdout, " -N %d",
					prompt_codes[i].pc_slots);
				if (strlen(prompt_codes[i].pc_fsfeat))
					fprintf(stdout, " --fs-features=%s",
						prompt_codes[i].pc_fsfeat);
				fprintf(stdout, "\n");
				showmkfs = 0;
				break;
			}
		}
		exit(showmkfs);
	}

	if (optind >= argc || !argv[optind]) {
		ret = -1;
		goto out;
	}

	device = argv[optind];

out:
	if (ret < 0)
		usage();

	return ret;
}

/*
 * main()
 *
 */
int main(int argc, char **argv)
{
	ocfs2_filesys *fs = NULL;
	errcode_t ret = 0;
	int i;

	initialize_ocfs_error_table();

#define INSTALL_SIGNAL(sig)					\
	do {							\
		if (signal(sig, handle_signal) == SIG_ERR) {	\
		    fprintf(stderr, "Could not set " #sig "\n");\
		    goto bail;					\
		}						\
	} while (0)

	INSTALL_SIGNAL(SIGTERM);
	INSTALL_SIGNAL(SIGINT);

	memset(corrupt, 0, sizeof(corrupt));

	ret = read_options(argc, argv);
	if (ret < 0)
		goto bail;

	print_version();

	ret = ocfs2_open(device, OCFS2_FLAG_RW, 0, 0, &fs);
	if (ret) {
		com_err(progname, ret, "while opening \"%s\"", device);
		goto bail;
	}

	for (i = 0; i < NUM_FSCK_TYPE; ++i) {
		if (corrupt[i]) {
			if (!prompt_codes[i].pc_func) {
				fprintf(stderr, "Unimplemented corrupt code "
					"= %s\n", prompt_codes[i].pc_codestr);
				continue;
			}
			fprintf(stdout, "%s: Corrupting %s with code %s (%d)\n",
				progname, device, prompt_codes[i].pc_codestr,
				prompt_codes[i].pc_codenum);
			prompt_codes[i].pc_func(fs, prompt_codes[i].pc_codenum,
						slotnum);
		}
	}

bail:
	if (fs) {
		ret = ocfs2_close(fs);
		if (ret)
			com_err(progname, ret, "while closing \"%s\"", device);
	}

	return ret;
}					/* main */
