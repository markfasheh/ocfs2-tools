/*
 * main.c
 *
 * entry point for normal and -X mode debugocfs
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
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
 * Author: Kurt Hackel, Sunil Mushran
 */

#include "debugocfs.h"

/* getopt stuff */
int getopt(int argc, char *const argv[], const char *optstring);
extern char *optarg;
extern int optind, opterr, optopt;

extern __u32 OcfsDebugCtxt;
extern __u32 OcfsDebugLevel;

void translate_usage(void);
void do_translate(int argc, char **argv);

/* global stuff */
int filenum;
user_args args;
char rawdev[255];
int rawminor = 0;
int fd = -1;

void usage(void)
{
    printf("debugocfs: Usage: debugocfs [-?] [-h] [-g] [-l] [-v range] [-p range]\n");
    printf("           [-d /dir/name] [-f /file/name [-s /path/to/file]] [-a range] [-A range]\n");
    printf("           [-b range] [-B range] [-r range] [-c range] [-L range] [-M range]\n");
    printf("           [-n nodenum] /dev/name\n");
    printf("\n");
    printf("       -h: volume header\n");
    printf("       -g: global bitmap\n");
    printf("       -l: full listing of all file entries\n");
    printf("       -v: vote sector\n");
    printf("       -2: print 8-byte number as 2 4-byte numbers\n");
    printf("       -p: publish sector\n");
    printf("       -d: first ocfs_dir_node structure for a given path\n");
    printf("       -D: all ocfs_dir_node structures for a given path\n");
    printf("       -f: ocfs_file_entry structure for a given file\n");
    printf("       -F: ocfs_file_entry and ocfs_extent_group structures for a given file\n");
    printf("       -s: suck file out to a given location\n");
    printf("       -a: file allocation system file\n");
    printf("       -A: dir allocation system file\n");
    printf("       -b: file allocation bitmap system file\n");
    printf("       -B: dir allocation bitmap system file\n");
    printf("       -r: recover log file system file\n");
    printf("       -c: cleanup log system file\n");
    printf("       -L: vol metadata log system file\n");
    printf("       -M: vol metadata system file\n");
    printf("       -n: perform action as node number given\n");
    printf("/dev/name: readable device\n");
    printf("    range: node numbers to inspect (0-31), commas and dashes ok\n");
    printf("            ex. 0-3,5,14-17\n");
}

void translate_usage(void)
{
    printf("Usage: debugocfs -X { -h highoff -l lowoff | -o off } -t type\n");
    printf("\n");
    printf("       highoff/lowoff: 32-bit high and low offsets to data\n");
    printf("                  off: 64-bit offset to data\n");
    printf("                 type: one of the following types to cast data to:\n");
    printf("                         ocfs_vol_label\n");
    printf("                         ocfs_vol_disk_hdr\n");
    printf("                         ocfs_dir_node\n");
    printf("                         ocfs_file_entry\n");
    printf("                         ocfs_vote\n");
    printf("                         ocfs_publish\n");
    printf("                         cdsl_offsets\n");
}


/* special translate mode junk */
typedef void (*debugocfs_print_func) (void *buf);

void do_translate(int argc, char **argv)
{
    int c, ok = true;
    __u64 off = 0;
    int size = 0;
    char *type = NULL;
    char *dot;
    debugocfs_print_func func;
    int flags;

    while (1)
    {
	c = getopt(argc, argv, "2o:h:l:s:t:");
	if (c == -1)
	    break;
	switch (c)
	{
	    case 'o':
		if ((dot = strchr(optarg, '.')) != NULL)
		{
		    *dot = '\0';
		    off = atoll(optarg);
		    *dot = '.';
		}
		break;
	    case 'h':
		off |= ((__u64) strtoul(optarg, NULL, 0)) << 32;
		ok = !ok;
		break;
	    case 'l':
		off |= strtoul(optarg, NULL, 0);
		ok = !ok;
		break;
	    case 't':
		type = (char *) strdup(optarg);
		break;
	    case '2':		/* display 8-byte nums as 2 4-byte nums */
		args.twoFourbyte = true;
		break;
            case '?':
                translate_usage();
                exit(1);
                break;
	    default:
		break;
	}
    }
    if (!ok)
    {
	printf("Oops. You must give both a high and low part.\n");
	exit(1);
    }

    if (type == NULL)
    {
	printf ("Oops. You must give a valid type.\n");
	exit(1);
    }
    if (strcasecmp(type, "ocfs_vol_label") == 0)
    {
	size = OCFS_ALIGN(sizeof(ocfs_vol_label), 512);
	func = print_vol_label;
    }
    else if (strcasecmp(type, "ocfs_vol_disk_hdr") == 0)
    {
        size = OCFS_ALIGN(sizeof(ocfs_vol_disk_hdr), 512);
	func = print_vol_disk_header;
    }
    else if (strcasecmp(type, "ocfs_dir_node") == 0)
    {
        size = OCFS_ALIGN(sizeof(ocfs_dir_node), 512);
        func = print_dir_node;
    }
    else if (strcasecmp(type, "ocfs_file_entry") == 0)
    {
        size = OCFS_ALIGN(sizeof(ocfs_file_entry), 512);
        func = print_file_entry;
    }
    else if (strcasecmp(type, "ocfs_vote") == 0)
    {
        size = OCFS_ALIGN(sizeof(ocfs_vote), 512);
        func = print_vote_sector;
    }
    else if (strcasecmp(type, "ocfs_publish") == 0)
    {
        size = OCFS_ALIGN(sizeof(ocfs_publish), 512);
        func = print_publish_sector;
    }
    else if (strcasecmp(type, "cdsl_offsets") == 0)
    {
        size = OCFS_ALIGN((sizeof(__u64) * MAX_NODES), 512);
        func = print_cdsl_offsets;
    }
    else if (strcasecmp(type, "ocfs_extent_group") == 0)
    {
	    size = OCFS_ALIGN(sizeof(ocfs_extent_group), 512);
	    func = print_extent_ex;
    }
    else
    {
	printf ("Oops. You must give a valid type.\n");
	exit(1);
    }

    if (bind_raw(argv[optind], &rawminor, rawdev, sizeof(rawdev)) == -1)
	    goto bail;

    flags = O_RDONLY | O_LARGEFILE;
    fd = open(rawdev, flags);
    if (fd == -1)
	goto bail;

    printf("offset: %lld, type: %s\n", off, type);

    {
	void *buf;

        buf = malloc_aligned(size);
	if (buf == NULL)
	{
	    printf("failed to alloc %d bytes\n", size);
	    exit(1);
	}
	myseek64(fd, off, SEEK_SET);

	printf("seeked ok\n");
	if (read(fd, buf, size) != -1)
	{
	    printf("successful read\n");
            func(buf);
	}
	free(buf);
    }

bail:
    if (type)
	free(type);
    if (fd != -1)
	    close(fd);
    if (rawminor)
	    unbind_raw(rawminor);
}

void version(char *prog)
{
	printf("%s %s %s (build %s)\n", prog, OCFS_BUILD_VERSION,
	       OCFS_BUILD_DATE, OCFS_BUILD_MD5);
}

void handle_signal(int sig)
{
    switch (sig) {
    case SIGTERM:
    case SIGINT:
	if (fd != -1)
	    close(fd);
	if (rawminor)
	    unbind_raw(rawminor);
	exit(1);
    }
}

/* uh, main */
int main(int argc, char **argv)
{
    ocfs_vol_label *volLabel = NULL;
    ocfs_vote *vs = NULL;
    ocfs_publish *ps = NULL;
    ocfs_vol_disk_hdr *diskHeader = NULL;
    int i;
    ocfs_super *vcb = NULL;
    char *env;
    int flags;

#define INSTALL_SIGNAL(sig)					\
    do {							\
	if (signal(sig, handle_signal) == SIG_ERR) {		\
	    fprintf(stderr, "Could not set " #sig "\n");	\
	    goto bail;						\
	}							\
    } while (0)

    INSTALL_SIGNAL(SIGTERM);
    INSTALL_SIGNAL(SIGINT);

    init_raw_cleanup_message();

    version(argv[0]);

    if ((env = getenv("dbgctxt")) != NULL)
        OcfsDebugCtxt = strtol(env, NULL, 16);
    if ((env = getenv("dbglvl")) != NULL)
        OcfsDebugLevel = strtol(env, NULL, 16);

    memset(&args, 0, sizeof(user_args));
    volLabel = (ocfs_vol_label *) malloc_aligned(512);
    vs = (ocfs_vote *) malloc_aligned(512);
    ps = (ocfs_publish *) malloc_aligned(512);
    diskHeader = (ocfs_vol_disk_hdr *) malloc_aligned(512);
    if (!volLabel || !vs || !ps || !diskHeader) {
	    LOG_ERROR("out of memory");
	    goto bail;
    }

    args.nodenum = -1;
    while (1)
    {
	int off = 0;
	int c = getopt(argc, argv, "hgl2v:p:d:D:f:F:a:A:b:B:b:r:c:L:M:s:n:X");

	if (c == -1)
	    break;
	switch (c)
	{
	    case 'h':		/* header */
		args.showHeader = true;
		break;
	    case 'g':		/* global bitmap */
		args.showBitmap = true;
		break;
	    case 'p':		/* publish 0-31 */
		args.showPublish = true;
		if (!parse_numeric_range(optarg, &(args.publishNodes[0]),
					 0, MAX_NODES, 0))
		{
		    usage();
		    exit(1);
		}
		break;
	    case 'v':		/* vote 0-31 */
		args.showVote = true;
		if (!parse_numeric_range(optarg, &(args.voteNodes[0]),
					 0, MAX_NODES, 0))
		{
		    usage();
		    exit(1);
		}
		break;
	    case 'c':
		if (!off)
		    off = OCFS_SYSFILE_TYPE_TO_FILE_NUM(OCFS_CLEANUP_LOG_SYSFILE, 0);
	    case 'r':
		if (!off)
                    off = OCFS_SYSFILE_TYPE_TO_FILE_NUM(OCFS_RECOVER_LOG_SYSFILE, 0);
	    case 'b':
		if (!off)
		    off = OCFS_SYSFILE_TYPE_TO_FILE_NUM(OCFS_FILE_EXTENT_BM_SYSFILE, 0);
	    case 'a':
		if (!off)
	   	    off = OCFS_SYSFILE_TYPE_TO_FILE_NUM(OCFS_FILE_EXTENT_SYSFILE, 0);
	    case 'B':
		if (!off)
		    off = OCFS_SYSFILE_TYPE_TO_FILE_NUM(OCFS_DIR_BM_SYSFILE, 0);
	    case 'A':
		if (!off)
		    off = OCFS_SYSFILE_TYPE_TO_FILE_NUM(OCFS_DIR_SYSFILE, 0);
	    case 'L':
		if (!off)
		    off = OCFS_SYSFILE_TYPE_TO_FILE_NUM(OCFS_VOL_MD_LOG_SYSFILE, 0);
	    case 'M':
		if (!off)
		    off = OCFS_SYSFILE_TYPE_TO_FILE_NUM(OCFS_VOL_MD_SYSFILE, 0);

		if (off==OCFS_INVALID_SYSFILE ||
                    !parse_numeric_range(optarg, &(args.systemFiles[0]), 0, MAX_NODES, off))
		{
		    usage();
		    exit(1);
		}
		args.showSystemFiles = true;
		break;
	    case 'l':		/* listing */
		args.showListing = true;
		break;
	    case 'd':		/* dirent */
		args.showDirent = true;
		args.dirent = (char *) strdup(optarg);
		break;
	    case 'D':		/* direntall */
		args.showDirentAll = true;
		args.dirent = (char *) strdup(optarg);
		break;
	    case 'f':		/* fileent */
		args.showFileent = true;
		args.fileent = (char *) strdup(optarg);
		break;
	    case 'F':		/* fileent + extents */
		args.showFileext = true;
		args.fileent = (char *) strdup(optarg);
		break;
	    case 's':		/* suck */
		args.suckFile = true;
		args.suckTo = (char *) strdup(optarg);
		break;
	    case 'n':		/* node number */
		args.nodenum = atoi(optarg);
		break;
	    case 'X':		/* translate */
		do_translate(argc, argv);
		exit(0);
		break;
	    case '2':		/* display 8-byte nums as 2 4-byte nums */
		args.twoFourbyte = true;
		break;
	    default:
	    case '?':
		usage();
		exit(1);
		break;
	}
    }

    // what should be the default node number? 
    if (args.nodenum < 0 || args.nodenum >= MAX_NODES)
	args.nodenum = DEFAULT_NODE_NUMBER;

    if (!(args.showHeader || args.showPublish || args.showVote ||
	  args.showListing || args.showDirent || args.showFileent ||
	  args.showFileext || args.showSystemFiles || args.showBitmap ||
	  args.showDirentAll))
    {
	usage();
	exit(1);
    }

    if (bind_raw(argv[optind], &rawminor, rawdev, sizeof(rawdev)) == -1)
	    goto bail;

    flags = O_RDONLY | O_LARGEFILE;
    fd = open(rawdev, flags);
    if (fd == -1)
    {
	usage();
	goto bail;
    }

    read_vol_disk_header(fd, diskHeader);
    read_vol_label(fd, volLabel);

    vcb = get_fake_vcb(fd, diskHeader, args.nodenum);
    if (!vcb) {
	    LOG_ERROR("out of memory");
	    goto bail;
    }

    if (args.showHeader)
    {
	printf("diskheader:\n");
	print_vol_disk_header(diskHeader);
	printf("\nvolumelabel:\n");
	print_vol_label(volLabel);
	printf("\n");
    }

    if (args.showBitmap)
    {
	printf("global_bitmap:\n");
	print_global_bitmap(fd, diskHeader);
	printf("\n");
    }

    if (args.showPublish)
    {
	for (i = 0; i < MAX_NODES; i++)
	{
	    if (args.publishNodes[i])
	    {
		printf("publish%d:\n", i);
		read_publish_sector(fd, ps,
				    diskHeader->publ_off + (__u64) (i * 512));
		print_publish_sector(ps);
	    }
	}
	printf("\n");
    }
    if (args.showVote)
    {
	for (i = 0; i < MAX_NODES; i++)
	{
	    if (args.voteNodes[i])
	    {
		printf("vote%d:\n", i);
		read_vote_sector(fd, vs,
				 diskHeader->vote_off + (__u64) (i * 512));
		print_vote_sector(vs);
	    }
	}
	printf("\n");
    }

    if (args.showListing)
    {
	filenum = 1;
	printf("filelisting:\n");
	walk_dir_nodes(fd, diskHeader->root_off, "/", NULL);
    }

    if (args.showDirent || args.showDirentAll)
    {
	printf("dirinfo:\n");
	if (strcmp(args.dirent, "/") == 0)
	{
	    ocfs_dir_node *dir = (ocfs_dir_node *) malloc_aligned(DIR_NODE_SIZE);
	    __u64 dir_off = diskHeader->root_off;

	    printf("\tName = /\n");
	    while (1) {
		    read_dir_node(fd, dir, dir_off);
		    print_dir_node(dir);
		    if (!args.showDirentAll || dir->next_node_ptr == INVALID_NODE_POINTER)
			    break;
		    dir_off = dir->next_node_ptr;
		    memset(dir, 0, DIR_NODE_SIZE);
	    	    printf("dirinfo:\n");
	    }
	    free(dir);
	}
	else
	{
	    find_file_entry(vcb, diskHeader->root_off, "/", args.dirent,
			    FIND_MODE_DIR, NULL);
	}
    }

    if (args.showSystemFiles)
    {
	for (i = 0; i < MAX_SYSTEM_FILES; i++)
	{
	    if (args.systemFiles[i])
	    {
		print_system_file(fd, diskHeader, i);
	    }
	}
    }

    if (args.showFileent || args.showFileext)
    {
	if (args.suckFile)
	{
	    suck_file(vcb, args.fileent, args.suckTo);
	    free(args.suckTo);
	}
	else
	{
	    printf("fileinfo:\n");
	    if (strcmp(args.fileent, "/") == 0)
	    {
		printf("the root directory '/' has no file entry\n");
		goto bail;
	    }
	    else
	    {
		find_file_entry(vcb, diskHeader->root_off, "/", args.fileent,
				(args.showFileext ? FIND_MODE_FILE_EXTENT :
				 FIND_MODE_FILE), NULL);
	    }
	}
    }

bail:
    if (volLabel)
	    free_aligned(volLabel);
    if (vs)
	    free_aligned(vs);
    if (ps)
	    free_aligned(ps);
    if (diskHeader)
	    free_aligned(diskHeader);
    if (vcb && vcb->sb)
	    free_aligned(vcb->sb);
    if (vcb)
	    free_aligned(vcb);
    if (fd != -1)
	    close(fd);
    if (rawminor)
	    unbind_raw(rawminor);
    exit(0);
}
