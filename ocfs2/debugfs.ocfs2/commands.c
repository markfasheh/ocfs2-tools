
#include <main.h>
#include <commands.h>
#include <dump.h>
#include <readfs.h>

#if 0
#define _GNU_SOURCE

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <glib.h>

#include <readline/readline.h>

#include <linux/types.h>
#include <ocfs2_fs.h>
#endif

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

static char *device = NULL;
static int   dev_fd = -1;
static __u32 blksz_bits = 0;
static char *curdir = NULL;
static char superblk[512];
static char rootin[512];

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

	if (device)
		do_close (NULL);

	if (dev == NULL)
		printf ("open requires a device argument\n");

	dev_fd = open (dev, allow_write ? O_RDONLY : O_RDWR);
	if (dev_fd == -1)
		printf ("could not open device %s\n", dev);

	device = g_strdup (dev);

	if (read_super_block (dev_fd, superblk, sizeof(superblk), &blksz_bits) != -1)
		curdir = g_strdup ("/");

	/* read root inode */
	inode = (ocfs2_dinode *)superblk;
	if ((pread64(dev_fd, rootin, sizeof(rootin),
		     (inode->id2.i_super.s_root_blkno << blksz_bits))) == -1) {
		LOG_INTERNAL("%s", strerror(errno));
		goto bail;
	}

bail:
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

		memset (superblk, 0, sizeof(superblk));
		memset (rootin, 0, sizeof(rootin));
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
#if 0
  char *newdir, *dir = args[1];

  if (!dir)
    {
      printf ("No directory given\n");
      return;
    }

  if (dir[0] == '/')
    newdir = g_strdup (dir);
  else
    newdir = g_strconcat (curdir, "/", dir, NULL);

  g_free (curdir);
  curdir = newdir;
#endif
}					/* do_cd */

/*
 * do_ls()
 *
 */
static void do_ls (char **args)
{
#if 0
  ocfs_super *vcb;
  __u64 off;

  if (strcmp (curdir, "/") != 0)
    {
      vcb = get_fake_vcb (dev_fd, header, 0);
      find_file_entry(vcb, header->root_off, "/", curdir, FIND_MODE_DIR, &off);
      free (vcb);
    }
  else
    off = header->root_off;

  if (off <= 0)
    {
      printf ("Invalid directory %s\n", curdir);
      return;
    }

  walk_dir_nodes (dev_fd, off, curdir, NULL);
#endif
}					/* do_ls */

/*
 * do_pwd()
 *
 */
static void do_pwd (char **args)
{
#if 0
  if (!curdir)
    curdir = g_strdup ("/");

  printf ("%s\n", curdir);
#endif
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
#if 0
  PrintFunc  func;
  char      *type = args[1];
  loff_t     offset = -1;
  void      *buf;
  int        nodenum = 0;

  if (!device)
    {
      printf ("Device not open\n");
      return;
    }

  if (!type)
    {
      printf ("No type given\n");
      return;
    }

  if (strcasecmp (type, "dir_node") == 0 ||
      strcasecmp (type, "ocfs_dir_node") == 0)
    {
      char *dirarg = args[2];

      func = print_dir_node;

      if (dirarg && *dirarg)
	{
	  if (dirarg[0] == '/')
	    {
	      if (strcmp (dirarg, "/") == 0)
		{
		  printf ("Name: /\n");
		  offset = header->root_off;
		}
	    }
	}
      else
        {
	  printf ("No name or offset\n");
	  return;
	}
    }
  else if (strcasecmp (type, "file_entry") == 0 ||
	   strcasecmp (type, "ocfs_file_entry") == 0)
    {
      func = print_file_entry;
    }
  else if (strcasecmp (type, "publish") == 0 ||
	   strcasecmp (type, "ocfs_publish") == 0)
    {
      func = print_publish;

      if (args[2])
	{
	  nodenum = strtol (args[2], NULL, 10);
	  nodenum = MAX(0, nodenum);
	  nodenum = MIN(31, nodenum);
	}

      offset = header->publ_off + nodenum * 512;
    }
  else if (strcasecmp (type, "vote") == 0 ||
	   strcasecmp (type, "ocfs_vote") == 0)
    {
      func = print_vote;

      if (args[2])
	{
	  nodenum = strtol (args[2], NULL, 10);
	  nodenum = MAX(0, nodenum);
	  nodenum = MIN(31, nodenum);
	}

      offset = header->vote_off + nodenum * 512;
    }
  else if (strcasecmp (type, "vol_disk_hdr") == 0 ||
	   strcasecmp (type, "ocfs_vol_disk_hdr") == 0)
    {
      func = print_vol_disk_hdr;
      offset = 0;
    }
  else if (strcasecmp (type, "vol_label") == 0 ||
	   strcasecmp (type, "ocfs_vol_label") == 0)
    {
      func = print_vol_label;
      offset = 512;
    }
  else
    {
      printf ("Invalid type\n");
      return;
    }

  if (offset == -1)
    {
      if (!args[2])
	{
	  printf ("No offset given\n");
	  return;
	}
      else
	offset = strtoll (args[2], NULL, 10);
    }

  printf ("Reading %s for node %d at offset %lld\n", type, nodenum, offset);

  buf = g_malloc (512);

  myseek64 (dev_fd, offset, SEEK_SET);

  if (read (dev_fd, buf, 512) != -1)
    func (buf);
  else
    printf ("Couldn't read\n");

  g_free (buf);
#endif
}					/* do_read */

/*
 * do_write()
 *
 */
static void do_write (char **args)
{
#if 0
  WriteFunc  func;
  char      *type = args[1];
  loff_t     offset = -1;
  void      *buf;
  int        nodenum = 0;
  char     **data;

  if (!device)
    {
      printf ("Device not open\n");
      return;
    }

  if (!type)
    {
      printf ("No type given\n");
      return;
    }

  if (strcasecmp (type, "dir_node") == 0 ||
      strcasecmp (type, "ocfs_dir_node") == 0)
    {
      char *dirarg = args[2];

      func = write_dir_node;

      if (dirarg && *dirarg)
	{
	  if (dirarg[0] == '/')
	    {
	      if (strcmp (dirarg, "/") == 0)
		{
		  printf ("Name: /\n");
		  offset = header->root_off;
		}
	    }
	}
      else
        {
	  printf ("No name or offset\n");
	  return;
	}
    }
  else if (strcasecmp (type, "file_entry") == 0 ||
	   strcasecmp (type, "ocfs_file_entry") == 0)
    {
      func = write_file_entry;
    }
  else if (strcasecmp (type, "publish") == 0 ||
	   strcasecmp (type, "ocfs_publish") == 0)
    {
      func = write_publish;

      if (args[2])
	{
	  nodenum = strtol (args[2], NULL, 10);
	  nodenum = MAX(0, nodenum);
	  nodenum = MIN(31, nodenum);
	}

      offset = header->publ_off + nodenum * 512;
    }
  else if (strcasecmp (type, "vote") == 0 ||
	   strcasecmp (type, "ocfs_vote") == 0)
    {
      func = write_vote;

      if (args[2])
	{
	  nodenum = strtol (args[2], NULL, 10);
	  nodenum = MAX(0, nodenum);
	  nodenum = MIN(31, nodenum);
	}

      offset = header->vote_off + nodenum * 512;
    }
  else if (strcasecmp (type, "vol_disk_hdr") == 0 ||
	   strcasecmp (type, "ocfs_vol_disk_hdr") == 0)
    {
      func = write_vol_disk_hdr;
      offset = 0;
    }
  else if (strcasecmp (type, "vol_label") == 0 ||
	   strcasecmp (type, "ocfs_vol_label") == 0)
    {
      func = write_vol_label;
      offset = 512;
    }
  else
    {
      printf ("Invalid type\n");
      return;
    }

  if (offset == -1)
    {
      if (!args[2])
	{
	  printf ("No offset given\n");
	  return;
	}
      else
	offset = strtoll (args[2], NULL, 10);
    }

  printf ("Writing %s for node %d at offset %lld\n", type, nodenum, offset);

  data = get_data ();

  myseek64 (dev_fd, offset, SEEK_SET);

  buf = g_malloc (512);

  if (read (dev_fd, buf, 512) != -1)
    {
      if (!func (data, buf))
	{
	  printf ("Invalid data\n");
	  return;
	}
    }
  else
    {
      printf ("Couldn't read\n");
      return;
    }

  myseek64 (dev_fd, offset, SEEK_SET);
  
  if (write (dev_fd, buf, 512) == -1)
    printf ("Write failed\n");

  g_strfreev (data);
  g_free (buf);
#endif
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
	printf ("cd\t\t\tChange working directory\n");
	printf ("pwd\t\t\tPrint working directory\n");
	printf ("ls\t\t\tList directory\n");
	printf ("rm\t\t\tRemove a file\n");
	printf ("mkdir\t\t\tMake a directory\n");
	printf ("rmdir\t\t\tRemove a directory\n");
	printf ("dump, cat\t\tDump contents of a file\n");
	printf ("lcd\t\t\tChange current local working directory\n");
	printf ("read\t\t\tRead a low level structure\n");
	printf ("write\t\t\tWrite a low level structure\n");
	printf ("help, ?\t\t\tThis information\n");
	printf ("quit, q\t\t\tExit the program\n");
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
#if 0
  char *fname = args[1], *filename, *out = args[2];
  ocfs_super *vcb;

  if (!fname)
    {
      printf ("No filename given\n");
      return;
    }

  if (!out)
    {
      printf ("No output given\n");
      return;
    }

  if (fname[0] == '/')
    filename = g_strdup (fname);
  else
    filename = g_strconcat (curdir, "/", fname, NULL);

  vcb = get_fake_vcb (dev_fd, header, 0);
  suck_file (vcb, filename, out);
  g_free (filename);
#endif
}					/* do_dump */

/*
 * do_lcd()
 *
 */
static void do_lcd (char **args)
{
#if 0
  char *dir = args[1];

  if (!dir)
    {
      printf ("Directory not given\n");
      return;
    }

  if (chdir (dir) == -1)
    printf ("Could not change directory\n");
#endif
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
#if 0
  char *line, **ret;
  GPtrArray *arr;

  arr = g_ptr_array_new ();

  while (1)
    {
      line = readline ("");

      if (line && strchr (line, '='))
	g_ptr_array_add (arr, line);
      else
	break;
    }

  if (line)
    free (line);

  ret = (char **) arr->pdata;

  g_ptr_array_free (arr, FALSE);

  return ret;
#endif
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

	inode = (ocfs2_dinode *)rootin;
	dump_inode(inode);

	return ;
}					/* do_inode */

