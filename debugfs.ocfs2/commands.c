/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * commands.c
 *
 * handles debugfs commands
 *
 * Copyright (C) 2004, 2011 Oracle.  All rights reserved.
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
#include "ocfs2/image.h"
#include "ocfs2/byteorder.h"

#define SYSTEM_FILE_NAME_MAX	40
#define MAX_BLOCKS		50

struct dbgfs_gbls gbls;

typedef void (*cmdfunc)(char **args);

struct command {
	char		*cmd_name;
	cmdfunc		cmd_func;
	char		*cmd_usage;
	char		*cmd_desc;
};

static struct command *find_command(char  *cmd);

static void do_bmap(char **args);
static void do_cat(char **args);
static void do_cd(char **args);
static void do_chroot(char **args);
static void do_close(char **args);
static void do_controld(char **args);
static void do_curdev(char **args);
static void do_decode_lockres(char **args);
static void do_dirblocks(char **args);
static void do_dlm_locks(char **args);
static void do_dump(char **args);
static void do_dx_dump(char **args);
static void do_dx_leaf(char **args);
static void do_dx_root(char **args);
static void do_dx_space(char **args);
static void do_encode_lockres(char **args);
static void do_extent(char **args);
static void do_frag(char **args);
static void do_fs_locks(char **args);
static void do_group(char **args);
static void do_grpextents(char **args);
static void do_hb(char **args);
static void do_help(char **args);
static void do_icheck(char **args);
static void do_lcd(char **args);
static void do_locate(char **args);
static void do_logdump(char **args);
static void do_ls(char **args);
static void do_net_stats(char **args);
static void do_open(char **args);
static void do_quit(char **args);
static void do_rdump(char **args);
static void do_refcount(char **args);
static void do_slotmap(char **args);
static void do_stat(char **args);
static void do_stat_sysdir(char **args);
static void do_stats(char **args);
static void do_xattr(char **args);

static struct command commands[] = {
	{ "bmap",
		do_bmap,
		"bmap <filespec> <logical_blk>",
		"Show the corresponding physical block# for the inode",
	},
	{ "cat",
		do_cat,
		"cat <filespec>",
		"Show file on stdout",
	},
	{ "cd",
		do_cd,
		"cd <filespec>",
		"Change directory",
	},
	{ "chroot",
		do_chroot,
		"chroot <filespec>",
		"Change root",
	},
	{ "close",
		do_close,
		"close",
		"Close a device",
	},
	{ "controld",
		do_controld,
		"controld dump",
		"Obtain information from ocfs2_controld",
	},
	{ "curdev",
		do_curdev,
		"curdev",
		"Show current device",
	},
	{ "decode",
		do_decode_lockres,
		"decode <lockname#> ...",
		"Decode block#(s) from the lockname(s)",
	},
	{ "dirblocks",
		do_dirblocks,
		"dirblocks <filespec>",
		"Dump directory blocks",
	},
	{ "dlm_locks",
		do_dlm_locks,
		"dlm_locks [-f <file>] [-l] lockname",
		"Show live dlm locking state",
	},
	{ "dump",
		do_dump,
		"dump [-p] <filespec> <outfile>",
		"Dumps file to outfile on a mounted fs",
	},
	{ "dx_dump",
		do_dx_dump,
		"dx_dump <blkno>",
		"Show directory index information",
	},
	{ "dx_leaf",
		do_dx_leaf,
		"dx_leaf <blkno>",
		"Show directory index leaf block only",
	},
	{ "dx_root",
		do_dx_root,
		"dx_root <blkno>",
		"Show directory index root block only",
	},
	{ "dx_space",
		do_dx_space,
		"dx_space <filespec>",
		"Dump directory free space list",
	},
	{ "encode",
		do_encode_lockres,
		"encode <filespec>",
		"Show lock name",
	},
	{ "extent",
		do_extent,
		"extent <block#>",
		"Show extent block",
	},
	{ "findpath",
		do_locate,
		"findpath <block#>",
		"List one pathname of the inode/lockname",
	},
	{ "frag",
		do_frag,
		"frag <filespec>",
		"Show inode extents / clusters ratio",
	},
	{ "fs_locks",
		do_fs_locks,
		"fs_locks [-f <file>] [-l] [-B]",
		"Show live fs locking state",
	},
	{ "group",
		do_group,
		"group <block#>",
		"Show chain group",
	},
	{ "grpextents",
		do_grpextents,
		"grpextents <block#>",
		"Show free extents in a chain group",
	},
	{ "hb",
		do_hb,
		"hb",
		"Show the heartbeat blocks",
	},
	{ "help",
		do_help,
		"help, ?",
		"This information",
	},
	{ "?",
		do_help,
		NULL,
		NULL,
	},
	{ "icheck",
		do_icheck,
		"icheck block# ...",
		"List inode# that is using the block#",
	},
	{ "lcd",
		do_lcd,
		"lcd <directory>",
		"Change directory on a mounted flesystem",
	},
	{ "locate",
		do_locate,
		"locate <block#> ...",
		"List all pathnames of the inode(s)/lockname(s)",
	},
	{ "logdump",
		do_logdump,
		"logdump [-T] <slot#>",
		"Show journal file for the node slot",
	},
	{ "ls",
		do_ls,
		"ls [-l] <filespec>",
		"List directory",
	},
	{ "net_stats",
		do_net_stats,
		"net_stats [-f <file>] [interval [count]]",
		"Show net statistics",
	},
	{ "ncheck",
		do_locate,
		"ncheck <block#> ...",
		"List all pathnames of the inode(s)/lockname(s)",
	},
	{ "open",
		do_open,
		"open <device> [-i] [-s backup#]",
		"Open a device",
	},
	{ "quit",
		do_quit,
		"quit, q",
		"Exit the program",
	},
	{ "q",
		do_quit,
		NULL,
		NULL,
	},
	{ "rdump",
		do_rdump,
		"rdump [-v] <filespec> <outdir>",
		"Recursively dumps from src to a dir on a mounted filesystem",
	},
	{ "refcount",
		do_refcount,
		"refcount [-e] <filespec>",
		"Dump the refcount tree for the inode or refcount block",
	},
	{ "slotmap",
		do_slotmap,
		"slotmap",
		"Show slot map",
	},
	{ "stat",
		do_stat,
		"stat [-t|-T] <filespec>",
		"Show inode",
	},
	{ "stat_sysdir",
		do_stat_sysdir,
		"stat_sysdir",
		"Show all objects in the system directory",
	},
	{ "stats",
		do_stats,
		"stats [-h]",
		"Show superblock",
	},
	{ "xattr",
		do_xattr,
		"xattr [-v] <filespec>",
		"Show extended attributes",
	},
};

void handle_signal(int sig)
{
	switch (sig) {
	case SIGTERM:
	case SIGINT:
		if (gbls.device)
			do_close(NULL);
		exit(1);
	}

	return ;
}

static struct command *find_command(char *cmd)
{
	unsigned int i;

	for (i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
		if (strcmp(cmd, commands[i].cmd_name) == 0)
			return &commands[i];

	return NULL;
}

void do_command(char *cmd)
{
	char    **args;
	struct command  *command;

	if (*cmd == '\0')
		return;

	args = g_strsplit(cmd, " ", -1);

	/* Move empty strings to the end */
	crunch_strsplit(args);

	/* ignore commented line */
	if (!strncmp(args[0], "#", 1))
		goto bail;

	command = find_command(args[0]);

	fflush(stdout);

	if (command) {
		gbls.cmd = command->cmd_name;
		command->cmd_func(args);
	} else
		fprintf(stderr, "%s: command not found\n", args[0]);

bail:
	g_strfreev(args);
}

static int check_device_open(void)
{
	if (!gbls.fs) {
		fprintf(stderr, "No device open\n");
		return -1;
	}

	return 0;
}

static int process_inode_args(char **args, uint64_t *blkno)
{
	errcode_t ret;
	char *opts = args[1];

	if (check_device_open())
		return -1;

	if (!opts) {
		fprintf(stderr, "usage: %s <filespec>\n", args[0]);
		return -1;
	}

	ret = string_to_inode(gbls.fs, gbls.root_blkno, gbls.cwd_blkno,
			      opts, blkno);
	if (ret) {
		com_err(args[0], ret, "'%s'", opts);
		return -1;
	}

	if (*blkno >= gbls.max_blocks) {
		com_err(args[0], OCFS2_ET_BAD_BLKNO, "- %"PRIu64"", *blkno);
		return -1;
	}

	return 0;
}

static int process_ls_args(char **args, uint64_t *blkno, int *long_opt)
{
	errcode_t ret;
	char *def = ".";
	char *opts;
	int ind = 1;

	if (check_device_open())
		return -1;

	if (args[ind]) {
		if (!strcmp(args[1], "-l")) {
			*long_opt = 1;
			++ind;
		}
	}

	if (args[ind])
		opts = args[ind];
	else
		opts = def;

	ret = string_to_inode(gbls.fs, gbls.root_blkno, gbls.cwd_blkno,
			      opts, blkno);
	if (ret) {
		com_err(args[0], ret, "'%s'", opts);
		return -1;
	}

	if (*blkno >= gbls.max_blocks) {
		com_err(args[0], OCFS2_ET_BAD_BLKNO, "- %"PRIu64"", *blkno);
		return -1;
	}

	return 0;
}

/*
 * process_inodestr_args()
 * 	args:	arguments starting from command
 * 	count:	max space available in blkno
 * 	blkno:	block nums extracted
 * 	RETURN:	number of blknos
 *
 */
static int process_inodestr_args(char **args, int count, uint64_t *blkno)
{
	uint64_t *p;
	int i;

	if (check_device_open())
		return -1;

	if (count < 1)
		return 0;

	for (i = 1, p = blkno; i < count + 1; ++i, ++p) {
		if (!args[i] || inodestr_to_inode(args[i], p))
			break;
		if (*p >= gbls.max_blocks) {
			com_err(args[0], OCFS2_ET_BAD_BLKNO, "- %"PRIu64"", *p);
			return -1;
		}
	}

	--i;

	if (!i) {
		fprintf(stderr, "usage: %s <inode#>\n", args[0]);
		return -1;
	}

	return  i;
}

/* open the device, read the block from the device and get the
 * blocksize from the offset of the ocfs2_super_block.
 */
static errcode_t get_blocksize(char* dev, uint64_t offset, uint64_t *blocksize,
			       int super_no)
{
	errcode_t ret;
	uint64_t blkno;
	uint32_t blocksize_bits;
	char *buf = NULL;
	io_channel* channel = NULL;
	struct ocfs2_dinode *di = NULL;
	struct ocfs2_image_hdr *hdr;

	ret = io_open(dev, OCFS2_FLAG_RO, &channel);
	if (ret)
		goto bail;

	/* since ocfs2_super_block inode can be stored in OCFS2_MIN_BLOCKSIZE,
	 * so here we just use the minimum block size and read the information
	 * in the specific offset.
	 */
	ret = io_set_blksize(channel, OCFS2_MIN_BLOCKSIZE);
	if (ret)
		goto bail;

	ret = ocfs2_malloc_block(channel, &buf);
	if (ret)
		goto bail;

	if (gbls.imagefile) {
		ret = io_read_block(channel, 0, 1, buf);
		if (ret)
			goto bail;
		hdr = (struct ocfs2_image_hdr *)buf;
		ocfs2_image_swap_header(hdr);
		if (super_no > hdr->hdr_superblkcnt) {
			ret = OCFS2_ET_IO;
			goto bail;
		}
		offset = hdr->hdr_superblocks[super_no-1] * hdr->hdr_fsblksz;
	}

	blkno = offset / OCFS2_MIN_BLOCKSIZE;
	ret = io_read_block(channel, blkno, 1, buf);
	if (ret)
		goto bail;

	di = (struct ocfs2_dinode *)buf;
	blocksize_bits = le32_to_cpu(di->id2.i_super.s_blocksize_bits);
	*blocksize = 1ULL << blocksize_bits;
bail:
	if (buf)
		ocfs2_free(&buf);
	if (channel)
		io_close(channel);
	return ret;
}

static int process_open_args(char **args,
			     uint64_t *superblock, uint64_t *blocksize)
{
	errcode_t ret = 0;
	uint32_t s = 0;
	char *ptr, *dev;
	uint64_t byte_off[OCFS2_MAX_BACKUP_SUPERBLOCKS];
	uint64_t blksize = 0;
	int num, argc, c;

	for (argc = 0; (args[argc]); ++argc);
	dev = strdup(args[1]);
	optind = 0;
	while ((c = getopt(argc, args, "is:")) != EOF) {
		switch (c) {
			case 'i':
				gbls.imagefile = 1;
				break;
			case 's':
				s = strtoul(optarg, &ptr, 0);
				break;
			default:
				ret = 1;
				goto bail;
				break;
		}
	}

	if (!s) {
		ret = 0;
		goto bail;
	}

	num = ocfs2_get_backup_super_offsets(NULL, byte_off,
					     ARRAY_SIZE(byte_off));
	if (!num) {
		ret = -1;
		goto bail;
	}

	if (s < 1 || s > num) {
		fprintf(stderr, "Backup super block is outside of valid range"
			"(between 1 and %d)\n", num);
		ret = -1;
		goto bail;
	}

	ret = get_blocksize(dev, byte_off[s-1], &blksize, s);
	if (ret) {
		com_err(args[0],ret, "Can't get the blocksize from the device"
			" by the num %u\n", s);
		goto bail;
	}

	*blocksize = blksize;
	*superblock = byte_off[s-1]/blksize;
	ret = 0;
bail:
	g_free(dev);
	return ret;
}

static int get_slotnum(char *str, uint16_t *slotnum)
{
	struct ocfs2_super_block *sb = OCFS2_RAW_SB(gbls.fs->fs_super);
	char *endptr;

	if (!str)
		return -1;

	*slotnum = strtoul(str, &endptr, 0);
	if (*endptr)
		return -1;

	if (*slotnum >= sb->s_max_slots)
		return -1;

	return 0;
}

static errcode_t find_block_offset(ocfs2_filesys *fs,
				   struct ocfs2_extent_list *el,
				   uint64_t blkoff, FILE *out)
{
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_rec *rec;
	errcode_t ret = 0;
	char *buf = NULL;
	int i;
	uint32_t clstoff, clusters;
	uint32_t tmp;

	clstoff = ocfs2_blocks_to_clusters(fs, blkoff);

	for (i = 0; i < el->l_next_free_rec; ++i) {
		rec = &(el->l_recs[i]);
		clusters = ocfs2_rec_clusters(el->l_tree_depth, rec);

		/*
		 * For a sparse file, we may find an empty record.
		 * Just skip it.
		 */
		if (!clusters)
			continue;

		if (clstoff >= (rec->e_cpos + clusters))
			continue;

		if (!el->l_tree_depth) {
			if (clstoff < rec->e_cpos) {
				dump_logical_blkno(out, 0);
			} else {
				tmp = blkoff -
					ocfs2_clusters_to_blocks(fs,
								 rec->e_cpos);
				dump_logical_blkno(out, rec->e_blkno + tmp);
			}
			goto bail;
		}

		ret = ocfs2_malloc_block(gbls.fs->fs_io, &buf);
		if (ret) {
			com_err(gbls.cmd, ret, "while allocating a block");
			goto bail;
		}

		ret = ocfs2_read_extent_block(fs, rec->e_blkno, buf);
		if (ret) {
			com_err(gbls.cmd, ret, "while reading extent %"PRIu64,
				(uint64_t)rec->e_blkno);
			goto bail;
		}

		eb = (struct ocfs2_extent_block *)buf;

		ret = find_block_offset(fs, &(eb->h_list), blkoff, out);
		goto bail;
	}

	dump_logical_blkno(out, 0);

bail:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

static void do_open(char **args)
{
	char *dev = args[1];
	int flags;
	errcode_t ret = 0;
	char sysfile[SYSTEM_FILE_NAME_MAX];
	int i;
	struct ocfs2_super_block *sb;
	uint64_t superblock = 0, block_size = 0;

	if (gbls.device)
		do_close(NULL);

	if (dev == NULL || process_open_args(args, &superblock, &block_size)) {
		fprintf(stderr, "usage: %s <device> [-i] [-s num]\n", args[0]);
		gbls.imagefile = 0;
		return ;
	}

	flags = gbls.allow_write ? OCFS2_FLAG_RW : OCFS2_FLAG_RO;
        flags |= OCFS2_FLAG_HEARTBEAT_DEV_OK|OCFS2_FLAG_NO_ECC_CHECKS;
	if (gbls.imagefile)
		flags |= OCFS2_FLAG_IMAGE_FILE;

	ret = ocfs2_open(dev, flags, superblock, block_size, &gbls.fs);
	if (ret) {
		gbls.fs = NULL;
		gbls.imagefile = 0;
		com_err(args[0], ret, "while opening context for device %s",
			dev);
		return ;
	}

	/* allocate blocksize buffer */
	ret = ocfs2_malloc_block(gbls.fs->fs_io, &gbls.blockbuf);
	if (ret) {
		com_err(args[0], ret, "while allocating a block");
		return ;
	}

	sb = OCFS2_RAW_SB(gbls.fs->fs_super);

	/* set globals */
	gbls.device = g_strdup (dev);
	gbls.max_clusters = gbls.fs->fs_super->i_clusters;
	gbls.max_blocks = ocfs2_clusters_to_blocks(gbls.fs, gbls.max_clusters);
	gbls.root_blkno = sb->s_root_blkno;
	gbls.sysdir_blkno = sb->s_system_dir_blkno;
	gbls.cwd_blkno = sb->s_root_blkno;
	if (gbls.cwd)
		free(gbls.cwd);
	gbls.cwd = strdup("/");

	/* lookup heartbeat file */
	snprintf(sysfile, sizeof(sysfile), "%s",
		  ocfs2_system_inodes[HEARTBEAT_SYSTEM_INODE].si_name);
	ret = ocfs2_lookup(gbls.fs, gbls.sysdir_blkno, sysfile,
			   strlen(sysfile), NULL, &gbls.hb_blkno);
	if (ret)
		gbls.hb_blkno = 0;

	/* lookup slotmap file */
	snprintf(sysfile, sizeof(sysfile), "%s",
		  ocfs2_system_inodes[SLOT_MAP_SYSTEM_INODE].si_name);
	ret = ocfs2_lookup(gbls.fs, gbls.sysdir_blkno, sysfile,
			   strlen(sysfile), NULL, &gbls.slotmap_blkno);
	if (ret)
		gbls.slotmap_blkno = 0;

	/* lookup journal files */
	for (i = 0; i < sb->s_max_slots; ++i) {
		snprintf(sysfile, sizeof(sysfile),
			  ocfs2_system_inodes[JOURNAL_SYSTEM_INODE].si_name, i);
		ret = ocfs2_lookup(gbls.fs, gbls.sysdir_blkno, sysfile,
				   strlen(sysfile), NULL, &gbls.jrnl_blkno[i]);
		if (ret)
			gbls.jrnl_blkno[i] = 0;
	}

	return ;
	
}

static void do_close(char **args)
{
	errcode_t ret = 0;

	if (check_device_open())
		return ;

	ret = ocfs2_close(gbls.fs);
	if (ret)
		com_err(args[0], ret, "while closing context");
	gbls.fs = NULL;
	gbls.imagefile = 0;

	if (gbls.blockbuf)
		ocfs2_free(&gbls.blockbuf);

	g_free(gbls.device);
	gbls.device = NULL;

	return ;
}

static void do_cd(char **args)
{
	uint64_t blkno;
	errcode_t ret;

	if (process_inode_args(args, &blkno))
		return ;

	ret = ocfs2_check_directory(gbls.fs, blkno);
	if (ret) {
		com_err(args[0], ret, "while checking directory at "
			"block %"PRIu64"", blkno);
		return ;
	}

	gbls.cwd_blkno = blkno;

	return ;
}

static void do_chroot(char **args)
{
	uint64_t blkno;
	errcode_t ret;

	if (process_inode_args(args, &blkno))
		return ;

	ret = ocfs2_check_directory(gbls.fs, blkno);
	if (ret) {
		com_err(args[0], ret, "while checking directory at "
			"blkno %"PRIu64"", blkno);
		return ;
	}

	gbls.root_blkno = blkno;

	return ;
}

static void do_ls(char **args)
{
	uint64_t blkno;
	errcode_t ret = 0;
	struct list_dir_opts ls_opts = { gbls.fs, NULL, 0, NULL };

	if (process_ls_args(args, &blkno, &ls_opts.long_opt))
		return ;

	ret = ocfs2_check_directory(gbls.fs, blkno);
	if (ret) {
		com_err(args[0], ret, "while checking directory at "
			"block %"PRIu64"", blkno);
		return ;
	}

	if (ls_opts.long_opt) {
		ret = ocfs2_malloc_block(gbls.fs->fs_io, &ls_opts.buf);
		if (ret) {
			com_err(args[0], ret, "while allocating a block");
			return ;
		}
	}

	ls_opts.out = open_pager(gbls.interactive);
	ret = ocfs2_dir_iterate(gbls.fs, blkno, 0, NULL,
				dump_dir_entry, (void *)&ls_opts);
	if (ret)
		com_err(args[0], ret, "while iterating directory at "
			"block %"PRIu64"", blkno);

	close_pager(ls_opts.out);

	if (ls_opts.buf)
		ocfs2_free(&ls_opts.buf);

	return ;
}

static void do_help(char **args)
{
	int i, usagelen = 0;
	int numcmds = sizeof(commands) / sizeof(commands[0]);

	for (i = 0; i < numcmds; ++i) {
		if (commands[i].cmd_usage)
			usagelen = max(usagelen, strlen(commands[i].cmd_usage));
	}

#define MIN_USAGE_LEN	40
	usagelen = max(usagelen, MIN_USAGE_LEN);

	for (i = 0; i < numcmds; ++i) {
		if (commands[i].cmd_usage)
			fprintf(stdout, "%-*s  %s\n", usagelen,
				commands[i].cmd_usage, commands[i].cmd_desc);
	}
}

static void do_quit(char **args)
{
	if (gbls.device)
		do_close(NULL);
	exit(0);
}

static void do_lcd(char **args)
{
	char buf[PATH_MAX];

	if (check_device_open())
		return ;

	if (!args[1]) {
		/* show cwd */
		if (!getcwd(buf, sizeof(buf))) {
			com_err(args[0], errno, "while reading current "
				"working directory");
			return ;
		}
		fprintf(stdout, "%s\n", buf);
		return ;
	}

	if (chdir(args[1]) == -1) {
		com_err(args[0], errno, "'%s'", args[1]);
		return ;
	}

	return ;
}

static void do_controld_dump(char **args)
{
	FILE *out;
	errcode_t ret;
	char *debug_buffer;

	ret = o2cb_control_daemon_debug(&debug_buffer);
	if (ret) {
		com_err(args[0], ret, "while obtaining the debug buffer");
		return;
	}

	out = open_pager(gbls.interactive);
	fprintf(out, "%s", debug_buffer);
	close_pager(out);
	free(debug_buffer);
}

static void do_controld(char **args)
{
	if (!args[1])
		fprintf(stderr, "%s: Operation required\n", args[0]);
	else if (!strcmp(args[1], "dump"))
		do_controld_dump(args);
	else
		fprintf(stderr, "%s: Invalid operation: \"%s\"\n",
			args[0], args[1]);
}

static void do_curdev(char **args)
{
	printf("%s\n", gbls.device ? gbls.device : "No device");
}

static void do_stats(char **args)
{
	FILE *out;
	errcode_t ret;
	struct ocfs2_dinode *in;
	struct ocfs2_super_block *sb;
	int c, argc;
	int sb_num = 0;
	int only_super = 0;
	char *ptr = NULL;
	char *stats_usage = "usage: stats [-h] [-s backup#]";
	char *buf = gbls.blockbuf;

	if (check_device_open())
		goto bail;

	for (argc = 0; (args[argc]); ++argc);
	optind = 0;

	while ((c = getopt(argc, args, "hs:")) != -1) {
		switch (c) {
		case 'h':
			only_super = 1;
			break;
		case 's':
			sb_num = strtoul(optarg, &ptr, 0);
			if (!ptr || *ptr) {
				fprintf(stderr, "%s\n", stats_usage);
				goto bail;
			}
			break;
		default:
			break;
		}
	}

	if (!sb_num)
		in = gbls.fs->fs_super;
	else {
		ret = ocfs2_read_backup_super(gbls.fs, sb_num - 1, buf);
		if (ret) {
			com_err(gbls.cmd, ret, "while reading backup "
				"superblock");
			goto bail;
		}
		in = (struct ocfs2_dinode *)buf;
	}

	sb = OCFS2_RAW_SB(in);

	out = open_pager(gbls.interactive);
	dump_super_block(out, sb);
	if (!only_super)
		dump_inode(out, in);
	close_pager(out);

bail:
	return ;
}

static void do_stat(char **args)
{
	struct ocfs2_dinode *inode;
	uint64_t blkno;
	char *buf = NULL;
	FILE *out;
	errcode_t ret = 0;
	const char *stat_usage = "usage: stat [-t|-T] <filespec>";
	int index = 1, traverse = 1;

	if (check_device_open())
		return;

	if (!args[index]) {
		fprintf(stderr, "%s\n", stat_usage);
		return ;
	}

	if (!strncmp(args[index], "-t", 2)) {
		traverse = 1;
		index++;
	} else if (!strncmp(args[index], "-T", 2)) {
		traverse = 0;
		index++;
	}

	ret = string_to_inode(gbls.fs, gbls.root_blkno, gbls.cwd_blkno,
			      args[index], &blkno);
	if (ret) {
		com_err(args[0], ret, "'%s'", args[index]);
		return ;
	}

	buf = gbls.blockbuf;
	ret = ocfs2_read_inode(gbls.fs, blkno, buf);
	if (ret) {
		com_err(args[0], ret, "while reading inode %"PRIu64"", blkno);
		return ;
	}

	inode = (struct ocfs2_dinode *)buf;

	out = open_pager(gbls.interactive);
	dump_inode(out, inode);

	if (traverse) {
		if ((inode->i_flags & OCFS2_LOCAL_ALLOC_FL))
			dump_local_alloc(out, &(inode->id2.i_lab));
		else if ((inode->i_flags & OCFS2_CHAIN_FL))
			ret = traverse_chains(gbls.fs,
					      &(inode->id2.i_chain), out);
		else if (S_ISLNK(inode->i_mode) && !inode->i_clusters)
			dump_fast_symlink(out,
					  (char *)inode->id2.i_symlink);
		else if (inode->i_flags & OCFS2_DEALLOC_FL)
			dump_truncate_log(out, &(inode->id2.i_dealloc));
		else if (!(inode->i_dyn_features & OCFS2_INLINE_DATA_FL))
			ret = traverse_extents(gbls.fs,
					       &(inode->id2.i_list), out);

		if (ret)
			com_err(args[0], ret,
				"while traversing inode at block "
				"%"PRIu64, blkno);
	}

	close_pager(out);

	return ;
}

static void do_hb(char **args)
{
	char *hbbuf = NULL;
	FILE *out;
	int len;
	errcode_t ret;

	if (check_device_open())
		return ;

	ret = ocfs2_read_whole_file(gbls.fs, gbls.hb_blkno, &hbbuf, &len);
	if (ret) {
		com_err(args[0], ret, "while reading heartbeat system file");
		goto bail;
	}

	out = open_pager(gbls.interactive);
	dump_hb(out, hbbuf, len);
	close_pager(out);

bail:
	if (hbbuf)
		ocfs2_free(&hbbuf);

	return ;
}

static void do_dump(char **args)
{
	uint64_t blkno;
	int preserve = 0;
	int ind;
	const char *dump_usage = "usage: dump [-p] <filespec> <out_file>";
	char *in_fn;
	char *out_fn;
	errcode_t ret;
	int fd;
	
	if (check_device_open())
		return;

	ind = 1;
	if (!args[ind]) {
		fprintf(stderr, "%s\n", dump_usage);
		return ;
	}

	if (!strncasecmp(args[ind], "-p", 2)) {
		++preserve;
		++ind;
	}

	in_fn = args[ind];
	out_fn = args[ind + 1];

	if (!in_fn || !out_fn) {
		fprintf(stderr, "%s\n", dump_usage);
		return ;
	}

	ret = string_to_inode(gbls.fs, gbls.root_blkno, gbls.cwd_blkno,
			      in_fn, &blkno);
	if (ret) {
		com_err(args[0], ret, "'%s'", in_fn);
		return ;
	}

	fd = open64(out_fn, O_CREAT | O_WRONLY | O_TRUNC, 0666);
	if (fd < 0) {
		com_err(args[0], errno, "'%s'", out_fn);
		return ;
	}

	ret = dump_file(gbls.fs, blkno, fd, out_fn, preserve);
	if (ret)
		com_err(args[0], ret, "while dumping file");

	return;
}

static void do_cat(char **args)
{
	uint64_t blkno;
	errcode_t ret;
	char *buf;
	struct ocfs2_dinode *di;

	if (process_inode_args(args, &blkno))
		return ;

	buf = gbls.blockbuf;
	ret = ocfs2_read_inode(gbls.fs, blkno, buf);
	if (ret) {
		com_err(args[0], ret, "while reading inode %"PRIu64"", blkno);
		return ;
	}

	di = (struct ocfs2_dinode *)buf;
	if (!S_ISREG(di->i_mode)) {
		fprintf(stderr, "%s: Not a regular file\n", args[0]);
		return ;
	}

	ret = dump_file(gbls.fs, blkno, fileno(stdout),  NULL, 0);
	if (ret)
		com_err(args[0], ret, "while reading file for inode %"PRIu64"",
			blkno);

	return ;
}

static void do_logdump(char **args)
{
	errcode_t ret;
	uint16_t slotnum;
	uint64_t blkno;
	FILE *out;
	int index = 1, traverse = 1;
	const char *logdump_usage = "usage: logdump [-T] <slot#>";

	if (check_device_open())
		return ;

	if (!args[index]) {
		fprintf(stderr, "%s\n", logdump_usage);
		return ;
	}

	if (!strncmp(args[index], "-T", 2)) {
		traverse = 0;
		index++;
	}

	if (get_slotnum(args[index], &slotnum)) {
		fprintf(stderr, "%s: Invalid node slot number\n", args[0]);
		fprintf(stderr, "%s\n", logdump_usage);
		return ;
	}

	blkno = gbls.jrnl_blkno[slotnum];

	out = open_pager(gbls.interactive);
	ret = read_journal(gbls.fs, blkno, out);
	close_pager(out);
	if (ret)
		com_err(gbls.cmd, ret, "while reading journal");

	return;
}

static void do_group(char **args)
{
	struct ocfs2_group_desc *grp;
	uint64_t blkno;
	char *buf = NULL;
	FILE *out;
	errcode_t ret = 0;
	int index = 0;

	if (process_inodestr_args(args, 1, &blkno) != 1)
		return ;

	buf = gbls.blockbuf;
	out = open_pager(gbls.interactive);
	while (blkno) {
		ret = ocfs2_read_group_desc(gbls.fs, blkno, buf);
		if (ret) {
			com_err(args[0], ret, "while reading block group "
				"descriptor %"PRIu64"", blkno);
			close_pager(out);
			return ;
		}

		grp = (struct ocfs2_group_desc *)buf;
		dump_group_descriptor(out, grp, index);
		blkno = grp->bg_next_group;
		index++;
	}

	close_pager(out);

	return ;
}

static void do_grpextents(char **args)
{
	struct ocfs2_group_desc *grp;
	uint64_t blkno;
	char *buf = NULL;
	FILE *out;
	errcode_t ret = 0;

	if (process_inodestr_args(args, 1, &blkno) != 1)
		return;

	buf = gbls.blockbuf;
	out = open_pager(gbls.interactive);
	while (blkno) {
		ret = ocfs2_read_group_desc(gbls.fs, blkno, buf);
		if (ret) {
			com_err(args[0], ret, "while reading block group "
				"descriptor %"PRIu64"", blkno);
			close_pager(out);
			return;
		}

		grp = (struct ocfs2_group_desc *)buf;
		dump_group_extents(out, grp);
		blkno = grp->bg_next_group;
	}

	close_pager(out);
}

static int dirblocks_proxy(ocfs2_filesys *fs, uint64_t blkno,
			   uint64_t bcount, uint16_t ext_flags,
			   void *priv_data)
{
	errcode_t ret;
	struct dirblocks_walk *ctxt = priv_data;

	ret = ocfs2_read_dir_block(fs, ctxt->di, blkno, ctxt->buf);
	if (!ret) {
		fprintf(ctxt->out, "\tDirblock: %"PRIu64"\n", blkno);
		dump_dir_block(ctxt->out, ctxt->buf);
	} else
		com_err(gbls.cmd, ret,
			"while reading dirblock %"PRIu64" on inode "
			"%"PRIu64"\n",
			blkno, (uint64_t)ctxt->di->i_blkno);
	return 0;
}

static void do_dirblocks(char **args)
{
	uint64_t ino_blkno;
	errcode_t ret = 0;
	struct dirblocks_walk ctxt = {
		.fs = gbls.fs,
	};

	if (process_inode_args(args, &ino_blkno))
		return;

	ret = ocfs2_check_directory(gbls.fs, ino_blkno);
	if (ret) {
		com_err(args[0], ret, "while checking directory at "
			"block %"PRIu64"", ino_blkno);
		return;
	}

	ret = ocfs2_read_inode(gbls.fs, ino_blkno, gbls.blockbuf);
	if (ret) {
		com_err(args[0], ret, "while reading inode %"PRIu64"",
			ino_blkno);
		return;
	}
	ctxt.di = (struct ocfs2_dinode *)gbls.blockbuf;

	ret = ocfs2_malloc_block(ctxt.fs->fs_io, &ctxt.buf);
	if (ret) {
		com_err(gbls.cmd, ret, "while allocating a block");
		return;
	}

	ctxt.out = open_pager(gbls.interactive);
	ret = ocfs2_block_iterate_inode(gbls.fs, ctxt.di, 0,
					dirblocks_proxy, &ctxt);
	if (ret)
		com_err(args[0], ret, "while iterating directory at "
			"block %"PRIu64"", ino_blkno);
	close_pager(ctxt.out);

	ocfs2_free(&ctxt.buf);
}

static void do_dx_root(char **args)
{
	struct ocfs2_dx_root_block *dx_root;
	uint64_t blkno;
	char *buf = NULL;
	FILE *out;
	errcode_t ret = 0;

	if (process_inodestr_args(args, 1, &blkno) != 1)
		return;

	buf = gbls.blockbuf;
	out = open_pager(gbls.interactive);

	ret = ocfs2_read_dx_root(gbls.fs, blkno, buf);
	if (ret) {
		com_err(args[0], ret, "while reading dx dir root "
			"block %"PRIu64"", blkno);
		close_pager(out);
		return;
	}

	dx_root = (struct ocfs2_dx_root_block *)buf;
	dump_dx_root(out, dx_root);
	if (!(dx_root->dr_flags & OCFS2_DX_FLAG_INLINE))
		traverse_extents(gbls.fs, &dx_root->dr_list, out);
	close_pager(out);

	return;
}

static void do_dx_leaf(char **args)
{
	struct ocfs2_dx_leaf *dx_leaf;
	uint64_t blkno;
	char *buf = NULL;
	FILE *out;
	errcode_t ret = 0;

	if (process_inodestr_args(args, 1, &blkno) != 1)
		return;

	buf = gbls.blockbuf;
	out = open_pager(gbls.interactive);

	ret = ocfs2_read_dx_leaf(gbls.fs, blkno, buf);
	if (ret) {
		com_err(args[0], ret, "while reading dx dir leaf "
			"block %"PRIu64"", blkno);
		close_pager(out);
		return;
	}

	dx_leaf = (struct ocfs2_dx_leaf *)buf;
	dump_dx_leaf(out, dx_leaf);

	close_pager(out);

	return;
}

static void do_dx_dump(char **args)
{
	struct ocfs2_dinode *inode;
	uint64_t ino_blkno;
	char *buf = NULL;
	FILE *out;
	errcode_t ret = 0;

	if (process_inode_args(args, &ino_blkno))
		return;

	out = open_pager(gbls.interactive);

	buf = gbls.blockbuf;
	ret = ocfs2_read_inode(gbls.fs, ino_blkno, buf);
	if (ret) {
		com_err(args[0], ret, "while reading inode %"PRIu64"",
			ino_blkno);
		close_pager(out);
		return ;
	}

	inode = (struct ocfs2_dinode *)buf;

	dump_dx_entries(out, inode);

	close_pager(out);

	return;
}

static void do_dx_space(char **args)
{
	struct ocfs2_dinode *inode;
	struct ocfs2_dx_root_block *dx_root;
	uint64_t ino_blkno, dx_blkno;
	char *buf = NULL, *dx_root_buf = NULL;
	FILE *out;
	errcode_t ret = 0;

	if (process_inode_args(args, &ino_blkno))
		return;

	out = open_pager(gbls.interactive);

	buf = gbls.blockbuf;
	ret = ocfs2_read_inode(gbls.fs, ino_blkno, buf);
	if (ret) {
		com_err(args[0], ret, "while reading inode %"PRIu64"",
			ino_blkno);
		goto out;
	}

	inode = (struct ocfs2_dinode *)buf;
	if (!(ocfs2_dir_indexed(inode))) {
		fprintf(out, "Inode %"PRIu64" is not indexed\n", ino_blkno);
		goto out;
	}

	ret = ocfs2_malloc_block(gbls.fs->fs_io, &dx_root_buf);
	if (ret) {
		goto out;
	}

	dx_blkno = (uint64_t) inode->i_dx_root;

	ret = ocfs2_read_dx_root(gbls.fs, dx_blkno, dx_root_buf);
	if (ret) {
		com_err(args[0], ret, "while reading dx dir root "
			"block %"PRIu64"", dx_blkno);
		goto out;
	}

	dx_root = (struct ocfs2_dx_root_block *)dx_root_buf;

	dump_dx_space(out, inode, dx_root);
out:
	close_pager(out);
	if (dx_root_buf)
		ocfs2_free(&dx_root_buf);

	return;
}

static void do_extent(char **args)
{
	struct ocfs2_extent_block *eb;
	uint64_t blkno;
	char *buf = NULL;
	FILE *out;
	errcode_t ret = 0;

	if (process_inodestr_args(args, 1, &blkno) != 1)
		return ;

	buf = gbls.blockbuf;
	ret = ocfs2_read_extent_block(gbls.fs, blkno, buf);
	if (ret) {
		com_err(args[0], ret, "while reading extent block %"PRIu64"",
			blkno);
		return ;
	}

	eb = (struct ocfs2_extent_block *)buf;

	out = open_pager(gbls.interactive);
	dump_extent_block(out, eb);
	dump_extent_list(out, &eb->h_list);
	close_pager(out);

	return ;
}

static void do_slotmap(char **args)
{
	FILE *out;
	errcode_t ret;
	int num_slots;
	struct ocfs2_slot_map_extended *se = NULL;
	struct ocfs2_slot_map *sm = NULL;

	if (check_device_open())
		return ;

	num_slots = OCFS2_RAW_SB(gbls.fs->fs_super)->s_max_slots;
	if (ocfs2_uses_extended_slot_map(OCFS2_RAW_SB(gbls.fs->fs_super)))
		ret = ocfs2_read_slot_map_extended(gbls.fs, num_slots, &se);
	else
		ret = ocfs2_read_slot_map(gbls.fs, num_slots, &sm);
	if (ret) {
		com_err(args[0], ret, "while reading slotmap system file");
		goto bail;
	}

	out = open_pager(gbls.interactive);
	dump_slots(out, se, sm, num_slots);
	close_pager(out);

bail:
	if (sm)
		ocfs2_free(&sm);

	return ;
}

static void do_rdump(char **args)
{
	uint64_t blkno;
	struct stat st;
	char *p;
	char *usage = "usage: rdump [-v] <srcdir> <dstdir>";
	errcode_t ret;
	int ind = 1;
	int verbose = 0;
	char tmp_str[40];

	if (check_device_open())
		return ;

	if (!args[1]) {
		fprintf(stderr, "%s\n", usage);
		return ;
	}

	if (!strcmp(args[1], "-v")) {
		++ind;
		++verbose;
	}

	if (!args[ind] || !args[ind+1]) {
		fprintf(stderr, "%s\n", usage);
		return ;
	}

	/* source */
	ret = string_to_inode(gbls.fs, gbls.root_blkno, gbls.cwd_blkno,
			      args[ind], &blkno);
	if (ret) {
		com_err(args[0], ret, "while translating %s", args[ind]);
		return ;
	}

	/* destination... has to be a dir on a mounted fs */
	if (stat(args[ind+1], &st) == -1) {
		com_err(args[0], errno, "'%s'", args[ind+1]);
		return ;
	}

	if (!S_ISDIR(st.st_mode)) {
		com_err(args[0], OCFS2_ET_NO_DIRECTORY, "'%s'", args[ind+1]);
		return ;
	}

	p = strrchr(args[ind], '/');
	if (p)
		p++;
	else
		p = args[ind];

	/* I could traverse the dirs from the root and find the directory */
	/* name... but this is debugfs, for crying out loud */
	if (!strcmp(p, ".") || !strcmp(p, "..") || !strcmp(p, "/")) {
		time_t tt;
		struct tm *tm;

		time(&tt);
		tm = localtime(&tt);
		/* YYYY-MM-DD_HH:MI:SS */
		snprintf(tmp_str, sizeof(tmp_str), "%4d-%2d-%2d_%02d:%02d:%02d",
			 1900 + tm->tm_year, tm->tm_mon, tm->tm_mday,
			 tm->tm_hour, tm->tm_min, tm->tm_sec);
		p = tmp_str;
	}

	/* drop the trailing '/' in destination */
	if (1) {
		char *q = args[ind+1];
		q = q + strlen(args[ind+1]) - 1;
		if (*q == '/')
			*q = '\0';
	}

	fprintf(stdout, "Copying to %s/%s\n", args[ind+1], p);

	ret = rdump_inode(gbls.fs, blkno, p, args[ind+1], verbose);
	if (ret)
		com_err(args[0], ret, "while recursively dumping "
			"inode %"PRIu64, blkno);

	return ;
}

/*
 * do_encode_lockres()
 *
 * This function only encodes the Super and the Inode lock. For the
 * rest, use the --encode parameter directly.
 */
static void do_encode_lockres(char **args)
{
	struct ocfs2_dinode *inode = (struct ocfs2_dinode *)gbls.blockbuf;
	uint64_t blkno;
	uint32_t gen = 0;
	errcode_t ret = 0;
	char lock[OCFS2_LOCK_ID_MAX_LEN];
	enum ocfs2_lock_type type = OCFS2_LOCK_TYPE_META;

	if (process_inode_args(args, &blkno))
		return;

	if (blkno == OCFS2_SUPER_BLOCK_BLKNO)
		type = OCFS2_LOCK_TYPE_SUPER;
	else {
		ret = ocfs2_read_inode(gbls.fs, blkno, (char *)inode);
		if (ret) {
			com_err(args[0], ret, "while reading inode %"PRIu64"",
				blkno);
			return;
		}
		gen = inode->i_generation;
	}

	ret = ocfs2_encode_lockres(type, blkno, gen, 0, lock);
	if (ret)
		return;

	printf("\t");
	printf("%s ", lock);
	printf("\n");

	return ;
}

static void do_decode_lockres(char **args)
{
	uint64_t blkno[MAX_BLOCKS];
	int count;
	int i;

	count = process_inodestr_args(args, MAX_BLOCKS, blkno);
	if (count < 1)
		return ;

	for (i = 0; i < count; ++i)
		printf("\t%s\t%"PRIu64"\n", args[i + 1], blkno[i]);
}

static void do_locate(char **args)
{
	uint64_t blkno[MAX_BLOCKS];
	int count = 0;
	int findall = 1;
	
	count = process_inodestr_args(args, MAX_BLOCKS, blkno);
	if (count < 1)
		return ;

	if (!strncasecmp(args[0], "findpath", 8))
		findall = 0;

	find_inode_paths(gbls.fs, args, findall, count, blkno, stdout);
}

static void do_dlm_locks(char **args)
{
	FILE *out;
	int dump_lvbs = 0;
	int i;
	struct list_head locklist;
	char *path = NULL, *uuid_str = NULL;
	int c, argc;

	init_stringlist(&locklist);

	for (argc = 0; (args[argc]); ++argc);
	optind = 0;

	while ((c = getopt(argc, args, "lf:")) != -1) {
		switch (c) {
		case 'l':
			dump_lvbs = 1;
			break;
		case 'f':
			path = optarg;
			break;
		default:
			break;
		}
	}
	if ((path == NULL)) {
		/* Only error for a missing device if we're asked to
		 * read from a live file system. */
		if (check_device_open())
			return;

		uuid_str = gbls.fs->uuid_str;
	}

	i = optind;
	if (args[i] && strlen(args[i])) {
		for ( ; args[i] && strlen(args[i]); ++i)
			if (add_to_stringlist(args[i], &locklist))
				break;
	}

	out = open_pager(gbls.interactive);
	dump_dlm_locks(uuid_str, out, path, dump_lvbs, &locklist);
	close_pager(out);

	free_stringlist(&locklist);
}

static void do_fs_locks(char **args)
{
	FILE *out;
	int dump_lvbs = 0;
	int only_busy = 0;
	int c, argc;
	struct list_head locklist;
	char *path = NULL, *uuid_str = NULL;

	for (argc = 0; (args[argc]); ++argc);
	optind = 0;

	while ((c = getopt(argc, args, "lBf:")) != -1) {
		switch (c) {
		case 'l':
			dump_lvbs = 1;
			break;
		case 'B':
			only_busy = 1;
			break;
		case 'f':
			path = optarg;
			break;
		default:
			break;
		}
	}
	if ((path == NULL)) {
		/* Only error for a missing device if we're asked to
		 * read from a live file system. */
		if (check_device_open())
			return;

		uuid_str = gbls.fs->uuid_str;
	}

	init_stringlist(&locklist);

	if (optind < argc) {
		for ( ; args[optind] && strlen(args[optind]); ++optind)
			if (add_to_stringlist(args[optind], &locklist))
				break;
	}

	out = open_pager(gbls.interactive);
	dump_fs_locks(uuid_str, out, path, dump_lvbs, only_busy,
		      &locklist);
	close_pager(out);

	free_stringlist(&locklist);
}

static void do_bmap(char **args)
{
	struct ocfs2_dinode *inode;
	const char *bmap_usage = "usage: bmap <filespec> <logical_blk>";
	uint64_t blkno;
	uint64_t blkoff;
	char *endptr;
	char *buf = NULL;
	FILE *out;
	errcode_t ret = 1;
 
	if (check_device_open())
		return;

	if (!args[1]) {
		fprintf(stderr, "usage: %s\n", bmap_usage);
		return;
	}

	if (!args[1] || !args[2]) {
		fprintf(stderr, "usage: %s\n", bmap_usage);
		return;
	}

	ret = string_to_inode(gbls.fs, gbls.root_blkno, gbls.cwd_blkno,
			      args[1], &blkno);
	if (ret) {
		com_err(args[0], ret, "'%s'", args[1]);
		return;
	}

	if (blkno >= gbls.max_blocks) {
		com_err(args[0], OCFS2_ET_BAD_BLKNO, "- %"PRIu64"", blkno);
		return;
	}

	blkoff = strtoull(args[2], &endptr, 0);
	if (*endptr) {
		com_err(args[0], OCFS2_ET_BAD_BLKNO, "- %"PRIu64"", blkoff);
		return;
	}
 
	buf = gbls.blockbuf;
	ret = ocfs2_read_inode(gbls.fs, blkno, buf);
	if (ret) {
		com_err(args[0], ret, "while reading inode %"PRIu64, blkno);
		return;
	}
 
	inode = (struct ocfs2_dinode *)buf;
 
	out = open_pager(gbls.interactive);
 
	if (inode->i_flags & (OCFS2_LOCAL_ALLOC_FL | OCFS2_CHAIN_FL |
			      OCFS2_DEALLOC_FL)) {
		dump_logical_blkno(out, 0);
		goto bail;
	}

	if (S_ISLNK(inode->i_mode) && !inode->i_clusters) {
		dump_logical_blkno(out, 0);
		goto bail;
	}

	if (blkoff > (inode->i_size >>
		      OCFS2_RAW_SB(gbls.fs->fs_super)->s_blocksize_bits)) {
		dump_logical_blkno(out, 0);
		goto bail;
	}

	find_block_offset(gbls.fs, &(inode->id2.i_list), blkoff, out);

bail:
	close_pager(out);
 
	return;
}

static void do_icheck(char **args)
{
	const char *testb_usage = "usage: icheck block# ...";
	char *endptr;
	uint64_t blkno[MAX_BLOCKS];
	int i;
	FILE *out;

	if (check_device_open())
		return;

	if (!args[1]) {
		fprintf(stderr, "%s\n", testb_usage);
		return;
	}

	for (i = 0; i < MAX_BLOCKS && args[i + 1]; ++i) {
		blkno[i] = strtoull(args[i + 1], &endptr, 0);
		if (*endptr) {
			com_err(args[0], OCFS2_ET_BAD_BLKNO, "- %s",
				args[i + 1]);
			return;
		}

		if (blkno[i] >= gbls.max_blocks) {
			com_err(args[0], OCFS2_ET_BAD_BLKNO, "- %"PRIu64"",
				blkno[i]);
			return;
		}
	}

	out = open_pager(gbls.interactive);

	find_block_inode(gbls.fs, blkno, i, out);

	close_pager(out);

	return;
}

static void do_xattr(char **args)
{
	struct ocfs2_dinode *inode;
	char *buf = NULL;
	FILE *out;
	char *usage = "usage: xattr [-v] <filespec>";
	uint64_t blkno, xattrs_bucket = 0;
	uint32_t xattrs_ibody = 0, xattrs_block = 0;
	int ind = 1, verbose = 0;
	errcode_t ret = 0;

	if (check_device_open())
		return;

	if (!args[1]) {
		fprintf(stderr, "%s\n", usage);
		return;
	}

	if (!strcmp(args[1], "-v")) {
		++ind;
		++verbose;
	}

	if (!args[ind]) {
		fprintf(stderr, "%s\n", usage);
		return;
	}

	ret = string_to_inode(gbls.fs, gbls.root_blkno, gbls.cwd_blkno,
			      args[ind], &blkno);
	if (ret) {
		com_err(args[0], ret, "while translating %s", args[ind]);
		return;
	}

	buf = gbls.blockbuf;
	ret = ocfs2_read_inode(gbls.fs, blkno, buf);
	if (ret) {
		com_err(args[0], ret, "while reading inode %"PRIu64"", blkno);
		return;
	}

	inode = (struct ocfs2_dinode *)buf;
	if (!(inode->i_dyn_features & OCFS2_HAS_XATTR_FL))
		return;

	out = open_pager(gbls.interactive);

	xattrs_ibody = dump_xattr_ibody(out, gbls.fs, inode, verbose);

	if (inode->i_xattr_loc)
		ret = dump_xattr_block(out, gbls.fs, inode, &xattrs_block,
				       &xattrs_bucket, verbose);

	if (ret)
		com_err(args[0], ret, "while traversing inode at block "
			"%"PRIu64, blkno);
	else
		fprintf(out, "\n\tExtended attributes in total: %"PRIu64"\n",
			xattrs_ibody + xattrs_block + xattrs_bucket);
	close_pager(out);

	return ;
}

static errcode_t calc_num_extents(ocfs2_filesys *fs,
				  struct ocfs2_extent_list *el,
				  uint32_t *ne)
{
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_rec *rec;
	errcode_t ret = 0;
	char *buf = NULL;
	int i;
	uint32_t clusters;
	uint32_t extents = 0;

	*ne = 0;

	for (i = 0; i < el->l_next_free_rec; ++i) {
		rec = &(el->l_recs[i]);
		clusters = ocfs2_rec_clusters(el->l_tree_depth, rec);

		/*
		 * In a unsuccessful insertion, we may shift a tree
		 * add a new branch for it and do no insertion. So we
		 * may meet a extent block which have
		 * clusters == 0, this should only be happen
		 * in the last extent rec. */
		if (!clusters && i == el->l_next_free_rec - 1)
			break;

		extents = 1;

		if (el->l_tree_depth) {
			ret = ocfs2_malloc_block(gbls.fs->fs_io, &buf);
			if (ret)
				goto bail;

			ret = ocfs2_read_extent_block(fs, rec->e_blkno, buf);
			if (ret)
				goto bail;

			eb = (struct ocfs2_extent_block *)buf;

			ret = calc_num_extents(fs, &(eb->h_list), &extents);
			if (ret)
				goto bail;

		}		

		*ne = *ne + extents;
	}

bail:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

static void do_frag(char **args)
{
	struct ocfs2_dinode *inode;
	uint64_t blkno;
	char *buf = NULL;
	FILE *out;
	errcode_t ret = 0;
	uint32_t clusters;
	uint32_t extents = 0;

	if (process_inode_args(args, &blkno))
		return;

	buf = gbls.blockbuf;
	ret = ocfs2_read_inode(gbls.fs, blkno, buf);
	if (ret) {
		com_err(args[0], ret, "while reading inode %"PRIu64"", blkno);
		return ;
	}

	inode = (struct ocfs2_dinode *)buf;

	out = open_pager(gbls.interactive);

	clusters = inode->i_clusters;
	if (!(inode->i_dyn_features & OCFS2_INLINE_DATA_FL))
		ret = calc_num_extents(gbls.fs, &(inode->id2.i_list), &extents);

	if (ret)
		com_err(args[0], ret, "while traversing inode at block "
			"%"PRIu64, blkno);
	else
		dump_frag(out, inode->i_blkno, clusters, extents);


	close_pager(out);

	return ;
}

static void walk_refcount_block(FILE *out, struct ocfs2_refcount_block *rb,
				int extent_tree)
{
	errcode_t ret = 0;
	uint32_t phys_cpos = UINT32_MAX;
	uint32_t e_cpos = 0, num_clusters = 0;
	uint64_t p_blkno = 0;
	char *buf = NULL;
	struct ocfs2_refcount_block *leaf_rb;

	if (!(rb->rf_flags & OCFS2_REFCOUNT_TREE_FL)) {
		dump_refcount_records(out, rb);
		return;
	}

	if (extent_tree) {
		fprintf(out,
			"\tExtent tree in refcount block %"PRIu64"\n"
			"\tDepth: %d  Records: %d\n",
			(uint64_t)rb->rf_blkno,
			rb->rf_list.l_tree_depth,
			rb->rf_list.l_next_free_rec);
		ret = traverse_extents(gbls.fs, &rb->rf_list, out);
		if (ret)
			com_err("refcount", ret,
				"while traversing the extent tree "
				"of refcount block %"PRIu64,
				(uint64_t)rb->rf_blkno);
	}

	ret = ocfs2_malloc_block(gbls.fs->fs_io, &buf);
	if (ret) {
		com_err("refcount", ret, "while allocating a buffer");
		return;
	}

	while (phys_cpos > 0) {
		ret = ocfs2_refcount_tree_get_rec(gbls.fs, rb, phys_cpos,
						  &p_blkno, &e_cpos,
						  &num_clusters);
		if (ret) {
			com_err("refcount", ret,
				"while looking up next refcount leaf in "
				"recount block %"PRIu64"\n",
				(uint64_t)rb->rf_blkno);
			break;
		}

		ret = ocfs2_read_refcount_block(gbls.fs, p_blkno, buf);
		if (ret) {
			com_err("refcount", ret, "while reading refcount block"
				" %"PRIu64, p_blkno);
			break;
		}

		leaf_rb = (struct ocfs2_refcount_block *)buf;
		dump_refcount_block(out, leaf_rb);
		walk_refcount_block(out, leaf_rb, extent_tree);

		if (e_cpos == 0)
			break;

		phys_cpos = e_cpos - 1;
	}

	ocfs2_free(&buf);
}

/* do_refcount() can take an inode or a refcount block address. */
static void do_refcount(char **args)
{
	struct ocfs2_dinode *di;
	struct ocfs2_refcount_block *rb;
	uint64_t blkno;
	char *buf = NULL;
	FILE *out;
	errcode_t ret = 0;
	int extent_tree = 0;
	char *inode_args[3] = {
		args[0],
		args[1],
		NULL,
	};

	if (args[1] && !strcmp(args[1], "-e")) {
		extent_tree = 1;
		inode_args[1] = args[2];
	}

	if (!inode_args[1]) {
		fprintf(stderr, "usage: %s [-e] <filespec>\n", args[0]);
		return;
	}

	if (process_inode_args(inode_args, &blkno))
		return ;

	buf = gbls.blockbuf;
	ret = ocfs2_read_inode(gbls.fs, blkno, buf);
	if (!ret) {
		di = (struct ocfs2_dinode *)buf;
		if (!(di->i_dyn_features & OCFS2_HAS_REFCOUNT_FL)) {
			fprintf(stderr,
				"%s: Inode %"PRIu64" does not have a "
				"refcount tree\n",
				args[0], blkno);
			return;
		}

		blkno = di->i_refcount_loc;
	} else if ((ret != OCFS2_ET_IO) && (ret != OCFS2_ET_BAD_INODE_MAGIC)) {
		/*
		 * If the user passed a refcount block address,
		 * read_inode() will return ET_IO or ET_BAD_INODE_MAGIC.
		 * For those cases we proceed treating blkno as a
		 * refcount block.  All other errors are real errors.
		 */
		com_err(args[0], ret, "while reading inode %"PRIu64"", blkno);
		return;
	}

	ret = ocfs2_read_refcount_block(gbls.fs, blkno, buf);
	if (ret) {
		com_err(args[0], ret, "while reading refcount block %"PRIu64,
			blkno);
		return;
	}

	out = open_pager(gbls.interactive);

	rb = (struct ocfs2_refcount_block *)buf;
	dump_refcount_block(out, rb);
	walk_refcount_block(out, rb, extent_tree);

	close_pager(out);
}

static void do_net_stats(char **args)
{
	int interval = 0, count = 0;
	char *net_stats_usage = "usage: net_stats [-f <file>] [interval [count]]";
	char *endptr;
	char *path = NULL;
	int c, argc;

	for (argc = 0; (args[argc]); ++argc);
	optind = 0;

	while ((c = getopt(argc, args, "f:")) != -1) {
		switch (c) {
		case 'f':
			path = optarg;
			break;
		default:
			break;
		}
	}

	if (args[optind]) {
		interval = strtoul(args[optind], &endptr, 0);
		if (!*endptr && args[++optind])
			count = strtoul(args[optind], &endptr, 0);
		if (*endptr) {
			fprintf(stderr, "%s\n", net_stats_usage);
			return ;
		}
	}

	/* Ignore device is user specifies the stats file */
	if (path == NULL && check_device_open())
		return;

	dump_net_stats(stdout, path, interval, count);
}

static void do_stat_sysdir(char **args)
{
	FILE *out;

	if (check_device_open())
		goto bail;

	out = open_pager(gbls.interactive);
	show_stat_sysdir(gbls.fs, out);
	close_pager(out);

bail:
	return ;
}
