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
#include <commands.h>
#include <dump.h>
#include <readfs.h>
#include <utils.h>

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
static char    **get_data     (void);

static void      do_open     (char **args);
static void      do_close    (char **args);
static void      do_cd       (char **args);
static void      do_ls       (char **args);
static void      do_pwd      (char **args);
static void      do_mkdir    (char **args);
static void      do_rmdir    (char **args);
static void      do_rm       (char **args);
static void      do_read     (char **args);
static void      do_write    (char **args);
static void      do_quit     (char **args);
static void      do_help     (char **args);
static void      do_dump     (char **args);
static void      do_lcd      (char **args);
static void      do_curdev   (char **args);
static void      do_super    (char **args);
static void      do_inode    (char **args);

extern gboolean allow_write;

char *device = NULL;
int   dev_fd = -1;
__u32 blksz_bits = 0;
__u32 clstrsz_bits = 0;
__u64 root_blkno = 0;
__u64 sysdir_blkno = 0;
char *curdir = NULL;
char *superblk = NULL;
char *rootin = NULL;
char *sysdirin = NULL;

static Command commands[] =
{
  { "open",   do_open   },
  { "close",  do_close  },
  { "cd",     do_cd     },
  { "ls",     do_ls     },
  { "pwd",    do_pwd    },

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
  { "stat", do_inode }
};


/*
 * find_command()
 *
 */
static Command * find_command (char *cmd)
{
	int i;

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
 * do_open()
 *
 */
static void do_open (char **args)
{
	char *dev = args[1];
	ocfs2_dinode *inode;
	ocfs2_super_block *sb;
	__u32 len;

	if (device)
		do_close (NULL);

	if (dev == NULL)
		printf ("open requires a device argument\n");

	dev_fd = open (dev, allow_write ? O_RDONLY : O_RDWR);
	if (dev_fd == -1)
		printf ("could not open device %s\n", dev);

	device = g_strdup (dev);

	if (read_super_block (dev_fd, &superblk) != -1)
		curdir = g_strdup ("/");

	inode = (ocfs2_dinode *)superblk;
	sb = &(inode->id2.i_super);
	/* set globals */
	clstrsz_bits = sb->s_clustersize_bits;
	blksz_bits = sb->s_blocksize_bits;
	root_blkno = sb->s_root_blkno;
	sysdir_blkno = sb->s_system_dir_blkno;

	/* read root inode */
	len = 1 << blksz_bits;
	if (!(rootin = malloc(len)))
		DBGFS_FATAL("%s", strerror(errno));
	if ((pread64(dev_fd, rootin, len, (root_blkno << blksz_bits))) == -1)
		DBGFS_FATAL("%s", strerror(errno));

	/* read sysdir inode */
	len = 1 << blksz_bits;
	if (!(sysdirin = malloc(len)))
		DBGFS_FATAL("%s", strerror(errno));
	if ((pread64(dev_fd, sysdirin, len, (sysdir_blkno << blksz_bits))) == -1)
		DBGFS_FATAL("%s", strerror(errno));

	return ;
}					/* do_open */

/*
 * do_close()
 *
 */
static void do_close (char **args)
{
	if (device) {
		g_free (device);
		device = NULL;
		close (dev_fd);
		dev_fd = -1;

		g_free (curdir);
		curdir = NULL;

		safefree (superblk);
		safefree (rootin);
		safefree (sysdirin);
	} else
		printf ("device not open\n");

	return ;
}					/* do_close */

/*
 * do_cd()
 *
 */
static void do_cd (char **args)
{

}					/* do_cd */

/*
 * do_ls()
 *
 */
static void do_ls (char **args)
{
	char *opts = args[1];
	ocfs2_dinode *inode;
	ocfs2_extent_rec *rec;
	__u32 blknum;
	char *buf = NULL;
	int i;
	GArray *arr = NULL;
	__u32 len;
	__u64 off, foff;

	len = 1 << blksz_bits;
	if (!(buf = malloc(len)))
		DBGFS_FATAL("%s", strerror(errno));

	if (opts) {
		blknum = atoi(opts);
		if ((read_inode (dev_fd, blknum, buf, len)) == -1) {
			printf("Not an inode\n");
			goto bail;
		}
		inode = (ocfs2_dinode *)buf;
	} else {
		inode = (ocfs2_dinode *)rootin;
	}

	if (!S_ISDIR(inode->i_mode)) {
		printf("Not a dir\n");
		goto bail;
	}

	arr = g_array_new(0, 1, sizeof(ocfs2_extent_rec));

	traverse_extents (dev_fd, &(inode->id2.i_list), arr, 0);

	safefree (buf);

	for (i = 0; i < arr->len; ++i) {
		rec = &(g_array_index(arr, ocfs2_extent_rec, i));
		off = rec->e_blkno << blksz_bits;
                foff = rec->e_cpos << clstrsz_bits;
		len = rec->e_clusters << clstrsz_bits;
                if ((foff + len) > inode->i_size)
                    len = inode->i_size - foff;
		if (!(buf = malloc (len)))
			DBGFS_FATAL("%s", strerror(errno));
		if ((pread64(dev_fd, buf, len, off)) == -1)
			DBGFS_FATAL("%s", strerror(errno));
		dump_dir_entry ((struct ocfs2_dir_entry *)buf, len);
		safefree (buf);
	}

bail:
	safefree (buf);
	if (arr)
		g_array_free (arr, 1);
	return ;

}					/* do_ls */

/*
 * do_pwd()
 *
 */
static void do_pwd (char **args)
{
	printf ("%s\n", curdir ? curdir : "No dir");
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
	printf ("curdev\t\t\t\tShow current device\n");
	printf ("open [device]\t\t\tOpen a device\n");
	printf ("close\t\t\t\tClose a device\n");
	printf ("show_super_stats, stats [-h]\tShow superblock\n");
	printf ("show_inode_info, stat [blknum]\tShow inode\n");
//	printf ("cd\t\t\tChange working directory\n");
	printf ("pwd\t\t\t\tPrint working directory\n");
	printf ("ls [blknum]\t\t\tList directory\n");
//	printf ("rm\t\t\t\tRemove a file\n");
//	printf ("mkdir\t\t\t\tMake a directory\n");
//	printf ("rmdir\t\t\t\tRemove a directory\n");
//	printf ("dump, cat\t\t\tDump contents of a file\n");
//	printf ("lcd\t\t\t\tChange current local working directory\n");
//	printf ("read\t\t\t\tRead a low level structure\n");
//	printf ("write\t\t\t\tWrite a low level structure\n");
	printf ("help, ?\t\t\t\tThis information\n");
	printf ("quit, q\t\t\t\tExit the program\n");
}					/* do_help */

/*
 * do_quit()
 *
 */
static void do_quit (char **args)
{
	exit (0);
}					/* do_quit */

/*
 * do_dump()
 *
 */
static void do_dump (char **args)
{

}					/* do_dump */

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
	printf ("%s\n", device ? device : "No device");
}					/* do_curdev */

/*
 * get_data()
 *
 */
static char ** get_data (void)
{
	return NULL;
}					/* get_data */

/*
 * do_super()
 *
 */
static void do_super (char **args)
{
	char *opts = args[1];
	ocfs2_dinode *in;
	ocfs2_super_block *sb;

	in = (ocfs2_dinode *)superblk;
	sb = &(in->id2.i_super);
	dump_super_block(sb);

	if (!opts || strncmp(opts, "-h", 2))
		dump_inode(in);

	return ;
}					/* do_super */

/*
 * do_inode()
 *
 */
static void do_inode (char **args)
{
	char *opts = args[1];
	ocfs2_dinode *inode;
	__u32 blknum;
	char *buf = NULL;
	__u32 buflen;

	buflen = 1 << blksz_bits;
	if (!(buf = malloc(buflen)))
		DBGFS_FATAL("%s", strerror(errno));

	if (opts) {
		blknum = atoi(opts);
		if ((read_inode (dev_fd, blknum, buf, buflen)) == -1) {
			printf("Not an inode\n");
			goto bail;
		}
		inode = (ocfs2_dinode *)buf;
	} else {
		inode = (ocfs2_dinode *)rootin;
	}

	dump_inode(inode);

	traverse_extents (dev_fd, &(inode->id2.i_list), NULL, 1);

bail:
	safefree (buf);
	return ;
}					/* do_inode */
