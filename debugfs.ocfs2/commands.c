/*
 * commands.c
 *
 * handles debugfs commands
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
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
 * Authors: Sunil Mushran, Manish Singh
 */

#include <main.h>
#define SYSTEM_FILE_NAME_MAX   40

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
static void do_pwd (char **args);
static void do_mkdir (char **args);
static void do_rmdir (char **args);
static void do_rm (char **args);
static void do_read (char **args);
static void do_write (char **args);
static void do_quit (char **args);
static void do_help (char **args);
static void do_dump (char **args);
static void do_lcd (char **args);
static void do_curdev (char **args);
static void do_super (char **args);
static void do_inode (char **args);
static void do_hb (char **args);
static void do_journal (char **args);
static void do_group (char **args);
static void do_extent (char **args);
static void do_chroot (char **args);

extern gboolean allow_write;

dbgfs_gbls gbls;

static Command commands[] =
{
  { "open",   do_open   },
  { "close",  do_close  },
  { "cd",     do_cd     },
  { "ls",     do_ls     },
  { "pwd",    do_pwd    },
  { "chroot", do_chroot },

  { "mkdir",  do_mkdir  },
  { "rmdir",  do_rmdir  },
  { "rm",     do_rm     },

  { "lcd",    do_lcd    },

  { "read",   do_read   },
  { "write",  do_write  },

  { "help",   do_help   },
  { "?",      do_help   },

  { "quit",   do_quit   },
  { "q",      do_quit   },

  { "dump",   do_dump   },
  { "cat",    do_dump   },

  { "curdev", do_curdev },

  { "show_super_stats", do_super },
  { "stats", do_super },

  { "show_inode_info", do_inode },
  { "stat", do_inode },

  { "nodes", do_hb },
  { "publish", do_hb },
  { "vote", do_hb },

  { "logdump", do_journal },

  { "group", do_group },
  { "extent", do_extent }
};


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
}					/* find_command */

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

	command = find_command (args[0]);

	if (command)
		command->func (args);
	else
		printf ("Unrecognized command: %s\n", args[0]);

	g_strfreev (args);
}					/* do_command */

/*
 * check_device_open()
 *
 */
static int check_device_open(void)
{
	if (!gbls.fs) {
		fprintf(stderr, "Device not open\n");
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

	if (check_device_open())
		return -1;

	if (!args[1]) {
		fprintf(stderr, "usage: %s [filespec]\n", args[0]);
		return -1;
	}

	ret = string_to_inode(gbls.fs, gbls.root_blkno, gbls.cwd_blkno,
			      args[1], blkno);
	if (ret) {
		com_err(gbls.progname, ret, " ");
		return -1;
	}

	if (*blkno >= gbls.max_blocks) {
		fprintf(stderr, "Block number is larger than volume size\n");
		ret = -1;
	}

	return 0;
}

/* only called by do_dump which is preprocessed out.. */
#if 0
/*
 * get_blknum()
 *
 */
static errcode_t get_blknum(char *blkstr, uint64_t *blkno)
{
	errcode_t ret = OCFS2_ET_INVALID_ARGUMENT;
	char *endptr;

	if (blkstr) {
		*blkno = strtoull(blkstr, &endptr, 0);
		if (!*endptr) {
			if (*blkno < gbls.max_blocks)
				ret = 0;
			else
				printf("Block number is too large\n");
		} else
			printf("Invalid block number\n");
	} else
		printf("No block number specified\n");

	return ret;
}
#endif

/*
 * get_nodenum()
 *
 */
static int get_nodenum(char **args, uint16_t *nodenum)
{
	ocfs2_super_block *sb = OCFS2_RAW_SB(gbls.fs->fs_super);
	char *endptr;

	if (args[1]) {
		*nodenum = strtoul(args[1], &endptr, 0);
		if (!*endptr) {
			if (*nodenum < sb->s_max_nodes)
				return 0;
			else
				fprintf(stderr, "Node number is too large\n");
		} else
			fprintf(stderr, "usage: %s [nodenum]\n", args[0]);
	} else
		fprintf(stderr, "usage: %s [nodenum]\n", args[0]);

	return -1;
}

/*
 * traverse_extents()
 *
 */
static int traverse_extents (ocfs2_filesys *fs, ocfs2_extent_list *el, FILE *out)
{
	ocfs2_extent_block *eb;
	ocfs2_extent_rec *rec;
	errcode_t ret = 0;
	char *buf = NULL;
	int i;

	dump_extent_list (out, el);

	for (i = 0; i < el->l_next_free_rec; ++i) {
		rec = &(el->l_recs[i]);
		if (el->l_tree_depth) {
			ret = ocfs2_malloc_block(gbls.fs->fs_io, &buf);
			if (ret) {
				com_err(gbls.progname, ret, "while allocating a block");
				goto bail;
			}

			ret = ocfs2_read_extent_block(fs, rec->e_blkno, buf);
			if (ret)
				goto bail;

			eb = (ocfs2_extent_block *)buf;

			dump_extent_block (out, eb);

			traverse_extents (fs, &(eb->h_list), out);
		}
	}

bail:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}				/* traverse_extents */

/*
 * traverse_chains()
 *
 */
static int traverse_chains (ocfs2_filesys *fs, ocfs2_chain_list *cl, FILE *out)
{
	ocfs2_group_desc *grp;
	ocfs2_chain_rec *rec;
	errcode_t ret = 0;
	char *buf = NULL;
	uint64_t blkno;
	int i;
	int index;

	dump_chain_list (out, cl);

	ret = ocfs2_malloc_block(gbls.fs->fs_io, &buf);
	if (ret) {
		com_err(gbls.progname, ret, "while allocating a block");
		goto bail;
	}

	for (i = 0; i < cl->cl_next_free_rec; ++i) {
		rec = &(cl->cl_recs[i]);
		blkno = rec->c_blkno;
		index = 0;
		fprintf(out, "\n");
		while (blkno) {
			ret = ocfs2_read_group_desc(fs, blkno, buf);
			if (ret)
				goto bail;

			grp = (ocfs2_group_desc *)buf;
			dump_group_descriptor(out, grp, index);
			blkno = grp->bg_next_group;
			index++;
		}
	}

bail:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}				/* traverse_chains */


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
	ocfs2_super_block *sb;

	if (gbls.device)
		do_close (NULL);

	if (dev == NULL) {
		fprintf (stderr, "usage: %s [device]\n", args[0]);
		return ;
	}

	flags = allow_write ? OCFS2_FLAG_RW : OCFS2_FLAG_RO;
	ret = ocfs2_open(dev, flags, 0, 0, &gbls.fs);
	if (ret) {
		gbls.fs = NULL;
		com_err(gbls.progname, ret, "while opening \"%s\"", dev);
		return ;
	}

	/* allocate blocksize buffer */
	ret = ocfs2_malloc_block(gbls.fs->fs_io, &gbls.blockbuf);
	if (ret) {
		com_err(gbls.progname, ret, "while allocating a block");
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

	/* lookup journal files */
	for (i = 0; i < sb->s_max_nodes; ++i) {
		snprintf (sysfile, sizeof(sysfile),
			  ocfs2_system_inodes[JOURNAL_SYSTEM_INODE].si_name, i);
		ret = ocfs2_lookup(gbls.fs, gbls.sysdir_blkno, sysfile,
				   strlen(sysfile), NULL, &gbls.jrnl_blkno[i]);
		if (ret)
			gbls.jrnl_blkno[i] = 0;
	}

	return ;
	
}					/* do_open */

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
		com_err(gbls.progname, ret, "while closing \"%s\"", gbls.device);
	gbls.fs = NULL;

	if (gbls.blockbuf)
		ocfs2_free(&gbls.blockbuf);

	g_free (gbls.device);
	gbls.device = NULL;

	return ;
}					/* do_close */

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
		com_err(gbls.progname, ret, " ");
		return ;
	}

	gbls.cwd_blkno = blkno;

	return ;
}					/* do_cd */

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
		com_err(gbls.progname, ret, " ");
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
	FILE *out = NULL;
	errcode_t ret = 0;

	if (process_inode_args(args, &blkno))
		return ;

	ret = ocfs2_check_directory(gbls.fs, blkno);
	if (ret) {
		com_err(gbls.progname, ret, " ");
		return ;
	}

	out = open_pager ();
	fprintf(out, "\t%-15s %-4s %-4s %-2s %-4s\n",
		"Inode", "Rlen", "Nlen", "Ty", "Name");

	ret = ocfs2_dir_iterate(gbls.fs, blkno, 0, NULL,
				dump_dir_entry, out);
	if (ret)
		com_err(gbls.progname, ret,
			"while listing inode %"PRIu64" on \"%s\"\n",
			blkno, gbls.device);

	close_pager (out);

	return ;
}					/* do_ls */

/*
 * do_pwd()
 *
 */
static void do_pwd (char **args)
{
	printf ("%s\n", __FUNCTION__);
}					/* do_pwd */

/*
 * do_mkdir()
 *
 */
static void do_mkdir (char **args)
{
	printf ("%s\n", __FUNCTION__);
}					/* do_mkdir */

/*
 * do_rmdir()
 *
 */
static void do_rmdir (char **args)
{
	printf ("%s\n", __FUNCTION__);
}					/* do_rmdir */

/*
 * do_rm()
 *
 */
static void do_rm (char **args)
{
  printf ("%s\n", __FUNCTION__);
}					/* do_rm */

/*
 * do_read()
 *
 */
static void do_read (char **args)
{

}					/* do_read */

/*
 * do_write()
 *
 */
static void do_write (char **args)
{

}					/* do_write */

/*
 * do_help()
 *
 */
static void do_help (char **args)
{
	printf ("curdev\t\t\tShow current device\n");
	printf ("open\t\t\tOpen a device\n");
	printf ("close\t\t\tClose a device\n");
	printf ("show_super_stats, stats\tShow superblock\n");
	printf ("show_inode_info, stat\tShow inode\n");
//	printf ("pwd\t\t\t\tPrint working directory\n");
	printf ("ls\t\t\tList directory\n");
	printf ("cd\t\t\tChange directory\n");
	printf ("chroot\t\t\tChange root\n");
//	printf ("cat <blknum> [outfile]\t\tPrints or concatenates file to stdout/outfile\n");
//	printf ("dump <blknum> <outfile>\t\tDumps file to outfile\n");
//	printf ("nodes\t\t\t\tList of nodes\n");
//	printf ("publish\t\t\t\tPublish blocks\n");
//	printf ("vote\t\t\t\tVote blocks\n");
	printf ("logdump\t\t\tPrints journal file for the node\n");
	printf ("extent\t\t\tShow extent block\n");
	printf ("group\t\t\tShow chain group\n");
	printf ("help, ?\t\t\tThis information\n");
	printf ("quit, q\t\t\tExit the program\n");
}					/* do_help */

/*
 * do_quit()
 *
 */
static void do_quit (char **args)
{
	if (gbls.device)
		do_close (NULL);
	exit (0);
}					/* do_quit */

/*
 * do_lcd()
 *
 */
static void do_lcd (char **args)
{

}					/* do_lcd */

/*
 * do_curdev()
 *
 */
static void do_curdev (char **args)
{
	printf ("%s\n", gbls.device ? gbls.device : "No device");
}					/* do_curdev */

/*
 * do_super()
 *
 */
static void do_super (char **args)
{
	char *opts = args[1];
	FILE *out;
	ocfs2_dinode *in;
	ocfs2_super_block *sb;

	if (check_device_open())
		goto bail;

	out = open_pager ();
	in = gbls.fs->fs_super;
	sb = OCFS2_RAW_SB(gbls.fs->fs_super);
	dump_super_block(out, sb);

	if (!opts || strncmp(opts, "-h", 2))
		dump_inode(out, in);

	close_pager (out);

bail:
	return ;
}					/* do_super */

/*
 * do_inode()
 *
 */
static void do_inode (char **args)
{
	ocfs2_dinode *inode;
	uint64_t blkno;
	char *buf = NULL;
	FILE *out;
	errcode_t ret = 0;

	if (process_inode_args(args, &blkno))
		return ;

	buf = gbls.blockbuf;
	ret = ocfs2_read_inode(gbls.fs, blkno, buf);
	if (ret) {
		com_err(gbls.progname, ret,
			"while reading inode in block %"PRIu64, blkno);
		return ;
	}

	inode = (ocfs2_dinode *)buf;

	out = open_pager();
	dump_inode(out, inode);

	if ((inode->i_flags & OCFS2_LOCAL_ALLOC_FL))
		dump_local_alloc(out, &(inode->id2.i_lab));
	else if ((inode->i_flags & OCFS2_CHAIN_FL))
		traverse_chains(gbls.fs, &(inode->id2.i_chain), out);
	else
		traverse_extents(gbls.fs, &(inode->id2.i_list), out);

	close_pager(out);

	return ;
}					/* do_inode */

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
	void (*dump_func) (FILE *out, char *buf);

	if (check_device_open())
		return ;

	DBGFS_FATAL("internal");

	ret = ocfs2_read_whole_file(gbls.fs, gbls.hb_blkno, &hbbuf, &len);
	if (ret) {
		com_err(gbls.progname, ret, "while reading hb file");
		goto bail;
	}

	out = open_pager();
	dump_func(out, hbbuf);
	close_pager(out);

bail:
	if (hbbuf)
		ocfs2_free(&hbbuf);

	return ;
}					/* do_hb */

/*
 * do_dump()
 *
 */
static void do_dump (char **args)
{
#if 0
	uint64_t blkno = 0;
	int32_t outfd = -1;
	FILE *out = NULL;
	int flags;
	int op = 0;  /* 0 = dump, 1 = cat */
	char *outfile = NULL;
	errcode_t ret;

	if (!gbls.fs == -1) {
		printf ("device not open\n");
		goto bail;
	}

	if (!strncasecmp (args[0], "cat", 3)) {
		out = open_pager ();
		outfd = fileno(out);
		op = 1;
	}

	ret = get_blknum(args[1], &blkno);
	if (ret)
		goto bail;

	if (args[2]) {
		outfile = args[2];
		flags = O_WRONLY| O_CREAT | O_LARGEFILE;
		flags |= ((op) ? O_APPEND : O_TRUNC);
		if ((outfd = open (outfile, flags)) == -1) {
			printf ("unable to open file %s\n", outfile);
			goto bail;
		}
	}
#if 0
	/* TODO */
	read_file (gbls.dev_fd, blknum, outfd, NULL);
#endif

bail:
	if (out) {
		close_pager (out);
		outfd = -1;
	}
	
	if (outfd > 2)
		close (outfd);
#endif
	return ;
}					/* do_dump */

/*
 * do_journal()
 *
 */
static void do_journal (char **args)
{
	char *logbuf = NULL;
	uint64_t blkno = 0;
	int32_t len = 0;
	FILE *out;
	uint16_t nodenum;
	errcode_t ret = 0;

	if (check_device_open())
		return ;

	if (get_nodenum(args, &nodenum))
		return ;

	blkno = gbls.jrnl_blkno[nodenum];

	ret = ocfs2_read_whole_file(gbls.fs, blkno, &logbuf, &len);
	if (ret) {
		com_err(gbls.progname, ret, "while reading journal file");
		goto bail;
	}

	out = open_pager ();
	read_journal (out, logbuf, (uint64_t)len);
	close_pager (out);

bail:
	if (logbuf)
		ocfs2_free(&logbuf);

	return ;
}					/* do_journal */

/*
 * do_group()
 *
 */
static void do_group (char **args)
{
	ocfs2_group_desc *grp;
	uint64_t blkno;
	char *buf = NULL;
	FILE *out;
	errcode_t ret = 0;
	int index = 0;

	if (process_inode_args(args, &blkno))
		return ;

	buf = gbls.blockbuf;

	out = open_pager();
	while (blkno) {
		ret = ocfs2_read_group_desc(gbls.fs, blkno, buf);
		if (ret) {
			com_err(gbls.progname, ret,
				"while reading chain group in block %"PRIu64, blkno);
			close_pager (out);
			return ;
		}

		grp = (ocfs2_group_desc *)buf;
		dump_group_descriptor(out, grp, index);
		blkno = grp->bg_next_group;
		index++;
	}

	close_pager (out);

	return ;
}					/* do_group */

/*
 * do_extent()
 *
 */
static void do_extent (char **args)
{
	ocfs2_extent_block *eb;
	uint64_t blkno;
	char *buf = NULL;
	FILE *out;
	errcode_t ret = 0;

	if (process_inode_args(args, &blkno))
		return ;

	buf = gbls.blockbuf;
	ret = ocfs2_read_extent_block(gbls.fs, blkno, buf);
	if (ret) {
		com_err(gbls.progname, ret,
			"while reading extent in block %"PRIu64, blkno);
		return ;
	}

	eb = (ocfs2_extent_block *)buf;

	out = open_pager();
	dump_extent_block(out, eb);
	dump_extent_list(out, &eb->h_list);
	close_pager(out);

	return ;
}					/* do_extent */

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
}					/* handle_signal */

