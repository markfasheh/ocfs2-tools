/*
 * commands.c
 *
 * handles debugfs commands
 *
 * Copyright (C) 2004, 2008 Oracle.  All rights reserved.
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

typedef void (*PrintFunc) (void *buf);
typedef gboolean (*WriteFunc) (char **data, void *buf);

typedef void (*CommandFunc) (char **args);

typedef struct _Command Command;

struct _Command
{
	char        *cmd;
	CommandFunc  func;
};

static Command  *find_command (char  *cmd);

static void do_open (char **args);
static void do_close (char **args);
static void do_cd (char **args);
static void do_ls (char **args);
static void do_quit (char **args);
static void do_help (char **args);
static void do_hb (char **args);
static void do_dump (char **args);
static void do_rdump (char **args);
static void do_cat (char **args);
static void do_lcd (char **args);
static void do_curdev (char **args);
static void do_stats (char **args);
static void do_stat (char **args);
static void do_logdump (char **args);
static void do_group (char **args);
static void do_extent (char **args);
static void do_chroot (char **args);
static void do_slotmap (char **args);
static void do_encode_lockres (char **args);
static void do_decode_lockres (char **args);
static void do_locate (char **args);
static void do_fs_locks (char **args);
static void do_bmap (char **args);
static void do_icheck (char **args);
static void do_dlm_locks (char **args);

dbgfs_gbls gbls;

static Command commands[] = {
	{ "bmap",	do_bmap },
	{ "cat",	do_cat },
	{ "cd",		do_cd },
	{ "chroot",	do_chroot },
	{ "close",	do_close },
	{ "curdev",	do_curdev },
	{ "dlm_locks",	do_dlm_locks },
	{ "dump",	do_dump },
	{ "extent",	do_extent },
	{ "fs_locks",	do_fs_locks },
	{ "group",	do_group },
	{ "help",	do_help },
	{ "hb",		do_hb },
	{ "?",		do_help },
	{ "icheck",	do_icheck },
	{ "lcd",	do_lcd },
	{ "locate",	do_locate },
	{ "ncheck",	do_locate },
	{ "findpath",	do_locate },
	{ "logdump",	do_logdump },
	{ "ls",		do_ls },
	{ "open",	do_open },
	{ "quit",	do_quit },
	{ "q",		do_quit },
	{ "rdump",	do_rdump },
	{ "slotmap",	do_slotmap },
	{ "stat",	do_stat },
	{ "stats",	do_stats },
	{ "encode",	do_encode_lockres },
	{ "decode",	do_decode_lockres }
};

/*
 * handle_signal()
 *
 */
void handle_signal (int sig)
{
	switch (sig) {
	case SIGTERM:
	case SIGINT:
		if (gbls.device)
			do_close (NULL);
		exit(1);
	}

	return ;
}

/*
 * find_command()
 *
 */
static Command * find_command (char *cmd)
{
	unsigned int i;

	for (i = 0; i < sizeof (commands) / sizeof (commands[0]); i++)
		if (strcmp (cmd, commands[i].cmd) == 0)
			return &commands[i];

	return NULL;
}

/*
 * do_command()
 *
 */
void do_command (char *cmd)
{
	char    **args;
	Command  *command;

	if (*cmd == '\0')
		return;

	args = g_strsplit (cmd, " ", -1);

	/* Move empty strings to the end */
	crunch_strsplit(args);

	/* ignore commented line */
	if (!strncmp(args[0], "#", 1))
		goto bail;

	command = find_command (args[0]);

	fflush(stdout);

	if (command) {
		gbls.cmd = command->cmd;
		command->func (args);
	} else
		fprintf(stderr, "%s: command not found\n", args[0]);

bail:
	g_strfreev (args);
}

/*
 * check_device_open()
 *
 */
static int check_device_open(void)
{
	if (!gbls.fs) {
		fprintf(stderr, "No device open\n");
		return -1;
	}

	return 0;
}

/*
 * process_inode_args()
 *
 */
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

/*
 * process_ls_args()
 *
 */
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
/*
 * process_open_args
 *
 */
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
				return 1;
				break;
		}
	}

	if (!s)
		return 0;

	num = ocfs2_get_backup_super_offset(NULL,
					    byte_off, ARRAY_SIZE(byte_off));
	if (!num)
		return -1;

	if (s < 1 || s > num) {
		fprintf (stderr, "Backup super block is outside of valid range"
			 "(between 1 and %d)\n", num);
		return -1;
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
	return ret;
}

/*
 * get_slotnum()
 *
 */
static int get_slotnum(char **args, uint16_t *slotnum)
{
	struct ocfs2_super_block *sb = OCFS2_RAW_SB(gbls.fs->fs_super);
	char *endptr;

	if (args[1]) {
		*slotnum = strtoul(args[1], &endptr, 0);
		if (!*endptr) {
			if (*slotnum < sb->s_max_slots)
				return 0;
			else
				fprintf(stderr,
					"%s: Invalid node slot number\n",
					args[0]);
		} else
			fprintf(stderr, "usage: %s <slotnum>\n", args[0]);
	} else
		fprintf(stderr, "usage: %s <slotnum>\n", args[0]);

	return -1;
}

/*
 * find_block_offset()
 *
 */
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
				rec->e_blkno);
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

/*
 * traverse_extents()
 *
 */
static errcode_t traverse_extents (ocfs2_filesys *fs, struct ocfs2_extent_list *el, FILE *out)
{
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_rec *rec;
	errcode_t ret = 0;
	char *buf = NULL;
	int i;
	uint32_t clusters;

	dump_extent_list (out, el);

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

		if (el->l_tree_depth) {
			ret = ocfs2_malloc_block(gbls.fs->fs_io, &buf);
			if (ret)
				goto bail;

			ret = ocfs2_read_extent_block(fs, rec->e_blkno, buf);
			if (ret)
				goto bail;

			eb = (struct ocfs2_extent_block *)buf;

			dump_extent_block (out, eb);

			ret = traverse_extents (fs, &(eb->h_list), out);
			if (ret)
				goto bail;
		}
	}

bail:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

/*
 * traverse_chains()
 *
 */
static errcode_t traverse_chains (ocfs2_filesys *fs, struct ocfs2_chain_list *cl, FILE *out)
{
	struct ocfs2_group_desc *grp;
	struct ocfs2_chain_rec *rec;
	errcode_t ret = 0;
	char *buf = NULL;
	uint64_t blkno;
	int i;
	int index;

	dump_chain_list (out, cl);

	ret = ocfs2_malloc_block(gbls.fs->fs_io, &buf);
	if (ret)
		goto bail;

	for (i = 0; i < cl->cl_next_free_rec; ++i) {
		rec = &(cl->cl_recs[i]);
		blkno = rec->c_blkno;
		index = 0;
		fprintf(out, "\n");
		while (blkno) {
			ret = ocfs2_read_group_desc(fs, blkno, buf);
			if (ret)
				goto bail;

			grp = (struct ocfs2_group_desc *)buf;
			dump_group_descriptor(out, grp, index);
			blkno = grp->bg_next_group;
			index++;
		}
	}

bail:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}


/*
 * do_open()
 *
 */
static void do_open (char **args)
{
	char *dev = args[1];
	int flags;
	errcode_t ret = 0;
	char sysfile[SYSTEM_FILE_NAME_MAX];
	int i;
	struct ocfs2_super_block *sb;
	uint64_t superblock = 0, block_size = 0;

	if (gbls.device)
		do_close (NULL);

	if (dev == NULL || process_open_args(args, &superblock, &block_size)) {
		fprintf (stderr, "usage: %s <device> [-i] [-s num]\n", args[0]);
		gbls.imagefile = 0;
		return ;
	}

	flags = gbls.allow_write ? OCFS2_FLAG_RW : OCFS2_FLAG_RO;
        flags |= OCFS2_FLAG_HEARTBEAT_DEV_OK;
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
	snprintf (sysfile, sizeof(sysfile),
		  ocfs2_system_inodes[HEARTBEAT_SYSTEM_INODE].si_name);
	ret = ocfs2_lookup(gbls.fs, gbls.sysdir_blkno, sysfile,
			   strlen(sysfile), NULL, &gbls.hb_blkno);
	if (ret)
		gbls.hb_blkno = 0;

	/* lookup slotmap file */
	snprintf (sysfile, sizeof(sysfile),
		  ocfs2_system_inodes[SLOT_MAP_SYSTEM_INODE].si_name);
	ret = ocfs2_lookup(gbls.fs, gbls.sysdir_blkno, sysfile,
			   strlen(sysfile), NULL, &gbls.slotmap_blkno);
	if (ret)
		gbls.slotmap_blkno = 0;

	/* lookup journal files */
	for (i = 0; i < sb->s_max_slots; ++i) {
		snprintf (sysfile, sizeof(sysfile),
			  ocfs2_system_inodes[JOURNAL_SYSTEM_INODE].si_name, i);
		ret = ocfs2_lookup(gbls.fs, gbls.sysdir_blkno, sysfile,
				   strlen(sysfile), NULL, &gbls.jrnl_blkno[i]);
		if (ret)
			gbls.jrnl_blkno[i] = 0;
	}

	return ;
	
}

/*
 * do_close()
 *
 */
static void do_close (char **args)
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

	g_free (gbls.device);
	gbls.device = NULL;

	return ;
}

/*
 * do_cd()
 *
 */
static void do_cd (char **args)
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

/*
 * do_chroot()
 *
 */
static void do_chroot (char **args)
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

/*
 * do_ls()
 *
 */
static void do_ls (char **args)
{
	uint64_t blkno;
	errcode_t ret = 0;
	list_dir_opts ls_opts = { gbls.fs, NULL, 0, NULL };

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

/*
 * do_help()
 *
 */
static void do_help (char **args)
{
	printf ("bmap <filespec> <logical_blk>\t\tPrint the corresponding physical block# for the inode\n");
	printf ("cat <filespec>\t\t\t\tPrints file on stdout\n");
	printf ("cd <filespec>\t\t\t\tChange directory\n");
	printf ("chroot <filespec>\t\t\tChange root\n");
	printf ("close\t\t\t\t\tClose a device\n");
	printf ("curdev\t\t\t\t\tShow current device\n");
	printf ("decode <lockname#> ...\t\t\tDecode block#(s) from the lockname(s)\n");
	printf ("dlm_locks [-l] lockname\t\t\tShow live dlm locking state\n");
	printf ("dump [-p] <filespec> <outfile>\t\tDumps file to outfile on a mounted fs\n");
	printf ("encode <filespec>\t\t\tShow lock name\n");
	printf ("extent <block#>\t\t\t\tShow extent block\n");
	printf ("findpath <block#>\t\t\tList one pathname of the inode/lockname\n");
	printf ("fs_locks [-l]\t\t\t\tShow live fs locking state\n");
	printf ("group <block#>\t\t\t\tShow chain group\n");
	printf ("hb\t\t\t\t\tShows the used heartbeat blocks\n");
	printf ("help, ?\t\t\t\t\tThis information\n");
	printf ("icheck block# ...\t\t\tDo block->inode translation\n");
	printf ("lcd <directory>\t\t\t\tChange directory on a mounted flesystem\n");
	printf ("locate <block#> ...\t\t\tList all pathnames of the inode(s)/lockname(s)\n");
	printf ("logdump <slot#>\t\t\t\tPrints journal file for the node slot\n");
	printf ("ls [-l] <filespec>\t\t\tList directory\n");
	printf ("ncheck <block#> ...\t\t\tList all pathnames of the inode(s)/lockname(s)\n");
	printf ("open <device> [-i] [-s backup#]\t\tOpen a device\n");
	printf ("quit, q\t\t\t\t\tExit the program\n");
	printf ("rdump [-v] <filespec> <outdir>\t\tRecursively dumps from src to a dir on a mounted filesystem\n");
	printf ("slotmap\t\t\t\t\tShow slot map\n");
	printf ("stat <filespec>\t\t\t\tShow inode\n");
	printf ("stats [-h]\t\t\t\tShow superblock\n");
}

/*
 * do_quit()
 *
 */
static void do_quit (char **args)
{
	if (gbls.device)
		do_close (NULL);
	exit (0);
}

/*
 * do_lcd()
 *
 */
static void do_lcd (char **args)
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

/*
 * do_curdev()
 *
 */
static void do_curdev (char **args)
{
	printf ("%s\n", gbls.device ? gbls.device : "No device");
}

/*
 * do_stats()
 *
 */
static void do_stats (char **args)
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
		ret = ocfs2_read_backup_super(gbls.fs, sb_num, buf);
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

/*
 * do_stat()
 *
 */
static void do_stat (char **args)
{
	struct ocfs2_dinode *inode;
	uint64_t blkno;
	char *buf = NULL;
	FILE *out;
	errcode_t ret = 0;

	if (process_inode_args(args, &blkno))
		return ;

	buf = gbls.blockbuf;
	ret = ocfs2_read_inode(gbls.fs, blkno, buf);
	if (ret) {
		com_err(args[0], ret, "while reading inode %"PRIu64"", blkno);
		return ;
	}

	inode = (struct ocfs2_dinode *)buf;

	out = open_pager(gbls.interactive);
	dump_inode(out, inode);

	if ((inode->i_flags & OCFS2_LOCAL_ALLOC_FL))
		dump_local_alloc(out, &(inode->id2.i_lab));
	else if ((inode->i_flags & OCFS2_CHAIN_FL))
		ret = traverse_chains(gbls.fs, &(inode->id2.i_chain), out);
	else if (S_ISLNK(inode->i_mode) && !inode->i_clusters)
		dump_fast_symlink(out, (char *)inode->id2.i_symlink);
	else if (inode->i_flags & OCFS2_DEALLOC_FL)
		dump_truncate_log(out, &(inode->id2.i_dealloc));
	else
		ret = traverse_extents(gbls.fs, &(inode->id2.i_list), out);

	if (ret)
		com_err(args[0], ret, "while traversing inode at block "
			"%"PRIu64, blkno);

	close_pager(out);

	return ;
}
/*
 * do_hb()
 *
 */
static void do_hb (char **args)
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

/*
 * do_dump()
 *
 */
static void do_dump (char **args)
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

	fd = open(out_fn, O_CREAT | O_WRONLY | O_TRUNC, 0666);
	if (fd < 0) {
		com_err(args[0], errno, "'%s'", out_fn);
		return ;
	}

	ret = dump_file(gbls.fs, blkno, fd, out_fn, preserve);
	if (ret)
		com_err(args[0], ret, "while dumping file");

	return;
}

/*
 * do_cat()
 *
 */
static void do_cat (char **args)
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

static void do_logdump (char **args)
{
	errcode_t ret;
	uint16_t slotnum;
	uint64_t blkno;
	FILE *out;

	if (check_device_open())
		return ;

	if (get_slotnum(args, &slotnum))
		return ;

	blkno = gbls.jrnl_blkno[slotnum];

	out = open_pager(gbls.interactive);
	ret = read_journal(gbls.fs, blkno, out);
	close_pager (out);
	if (ret)
		com_err(gbls.cmd, ret, "while reading journal");

	return;
}

/*
 * do_group()
 *
 */
static void do_group (char **args)
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
			close_pager (out);
			return ;
		}

		grp = (struct ocfs2_group_desc *)buf;
		dump_group_descriptor(out, grp, index);
		blkno = grp->bg_next_group;
		index++;
	}

	close_pager (out);

	return ;
}

/*
 * do_extent()
 *
 */
static void do_extent (char **args)
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

/*
 * do_slotmap()
 *
 */
static void do_slotmap (char **args)
{
	FILE *out;
	errcode_t ret;
	char *buf = NULL;
	uint32_t len;

	if (check_device_open())
		return ;

	len = gbls.fs->fs_blocksize;
	/* read in the first block of the slot_map file */
	ret = read_whole_file(gbls.fs, gbls.slotmap_blkno, &buf, &len);
	if (ret) {
		com_err(args[0], ret, "while reading slotmap system file");
		goto bail;
	}

	out = open_pager(gbls.interactive);
	dump_slots (out, buf, len);
	close_pager (out);

bail:
	if (buf)
		ocfs2_free(&buf);

	return ;
}

/*
 * do_rdump()
 *
 */
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
 */
static void do_encode_lockres (char **args)
{
	struct ocfs2_dinode *inode;
	uint64_t blkno;
	char *buf = NULL;
	errcode_t ret = 0;
	char suprlock[50] = "\0";
	char metalock[50] = "\0";
	char datalock[50] = "\0";
	char rdwrlock[50] = "\0";

	if (process_inode_args(args, &blkno))
		return ;

	if (blkno == OCFS2_SUPER_BLOCK_BLKNO) {
		ret = ocfs2_encode_lockres(OCFS2_LOCK_TYPE_SUPER, blkno, 0,
					   suprlock);
	} else {
		buf = gbls.blockbuf;
		ret = ocfs2_read_inode(gbls.fs, blkno, buf);
		if (!ret) {
			inode = (struct ocfs2_dinode *)buf;
			ocfs2_encode_lockres(OCFS2_LOCK_TYPE_META, blkno,
					     inode->i_generation, metalock);
			ocfs2_encode_lockres(OCFS2_LOCK_TYPE_DATA, blkno,
					     inode->i_generation, datalock);
			ocfs2_encode_lockres(OCFS2_LOCK_TYPE_RW, blkno,
					     inode->i_generation, rdwrlock);
		}
	}

	if (ret) {
		com_err(args[0], ret, "while reading inode %"PRIu64"", blkno);
		return ;
	}

	printf("\t");
	if (*suprlock)
		printf("%s ", suprlock);
	if (*metalock)
		printf("%s ", metalock);
	if (*datalock)
		printf("%s ", datalock);
	if (*rdwrlock)
		printf("%s ", rdwrlock);
	printf("\n");

	return ;
}

/*
 * do_decode_lockres()
 *
 */
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

/*
 * do_locate()
 *
 */
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

/*
 * do_dlm_locks()
 *
 */
static void do_dlm_locks(char **args)
{
	FILE *out;
	int dump_lvbs = 0;
	int i;
	struct locknames *lock;
	struct list_head locklist;
	struct list_head *iter, *iter2;

	if (check_device_open())
		return;

	INIT_LIST_HEAD(&locklist);

	i = 1;
	if (args[i] && strlen(args[i])) {
		if (!strcmp("-l", args[i])) {
			dump_lvbs = 1;
			i++;
		}

		for ( ; args[i] && strlen(args[i]); ++i) {
			lock = calloc(1, sizeof(struct locknames));
			if (!lock)
				break;
			INIT_LIST_HEAD(&lock->list);
			strncpy(lock->name, args[i], sizeof(lock->name));
			list_add_tail(&lock->list, &locklist);
		}
	}

	out = open_pager(gbls.interactive);
	dump_dlm_locks(gbls.fs->uuid_str, out, dump_lvbs, &locklist);
	close_pager(out);

	if (!list_empty(&locklist)) {
		list_for_each_safe(iter, iter2, &locklist) {
			lock = list_entry(iter, struct locknames, list);
			list_del(iter);
			free(lock);
		}
	}
}

/*
 * do_dump_fs_locks()
 *
 */
static void do_fs_locks(char **args)
{
	FILE *out;
	int dump_lvbs = 0;

	if (check_device_open())
		return;

	if (args[1] && !strcmp("-l", args[1]))
		dump_lvbs = 1;

	out = open_pager(gbls.interactive);
	dump_fs_locks(gbls.fs->uuid_str, out, dump_lvbs);
	close_pager(out);
}

/*
 * do_bmap()
 *
 */
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

	if (!args[1]) {
		fprintf(stderr, "%s\n", testb_usage);
		return;
	}

	for (i = 0; i < MAX_BLOCKS && args[i + 1]; ++i) {
		blkno[i] = strtoull(args[i + 1], &endptr, 0);
		if (*endptr) {
			com_err(args[0], OCFS2_ET_BAD_BLKNO, "- %s", args[i + 1]);
			return;
		}

		if (blkno[i] >= gbls.max_blocks) {
			com_err(args[0], OCFS2_ET_BAD_BLKNO, "- %"PRIu64"", blkno[i]);
			return;
		}
	}

	out = open_pager(gbls.interactive);

	find_block_inode(gbls.fs, blkno, i, out);

	close_pager(out);

	return;
}
