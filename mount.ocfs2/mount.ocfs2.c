/*
 * mount.ocfs2.c  Mounts ocfs2 volume
 *
 * Copyright (C) 2005 Oracle.  All rights reserved.
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

#include "mount.ocfs2.h"

int verbose = 0;
int mount_quiet=0;
int nomtab = 0;
char *progname = NULL;
char op_buf[PAGE_SIZE];

struct mount_options {
	char *dev;
	char *dir;
	char *opts;
	int flags;
	char *xtra_opts;
};

static int get_uuid(char *dev, char *uuid)
{
	ocfs2_filesys *fs = NULL;
	errcode_t ret = 0;
	int i;
	char *p;
	uint8_t *s_uuid;

	ret = ocfs2_open(dev, OCFS2_FLAG_RO, 0, 0, &fs);
	if (ret)
		goto out;

	s_uuid = OCFS2_RAW_SB(fs->fs_super)->s_uuid;

	for (i = 0, p = uuid; i < 16; i++, p += 2)
		sprintf(p, "%02X", s_uuid[i]);
	*p = '\0';

	ocfs2_close(fs);

out:
	return ret;
}

static void read_options(int argc, char **argv, struct mount_options *mo)
{
	int c;

	progname = basename(argv[0]);

	if (argc < 2)
		goto bail;

	while(1) {
		c = getopt(argc, argv, "vno:");
		if (c == -1)
			break;

		switch (c) {
		case 'v':
			++verbose;
			break;

		case 'n':
			++nomtab;
			break;

		case 'o':
			if (optarg)
				mo->opts = xstrdup(optarg);
			break;

		default:
			break;
		}
	}

	if (optind < argc && argv[optind])
		mo->dev = xstrdup(argv[optind]);

	++optind;

	if (optind < argc && argv[optind])
		mo->dir = xstrdup(argv[optind]);

bail:
	return ;
}

/*
 * Code based on similar function in util-linux-2.12a/mount/mount.c
 *
 */
static void print_one (const struct mntent *me)
{
	if (mount_quiet)
		return ;

	printf ("%s on %s", me->mnt_fsname, me->mnt_dir);

	if (me->mnt_type != NULL && *(me->mnt_type) != '\0')
		printf (" type %s", me->mnt_type);

	if (me->mnt_opts)
		printf (" (%s)", me->mnt_opts);

	printf ("\n");
}

/*
 * Code based on similar function in util-linux-2.12a/mount/mount.c
 *
 */
static void update_mtab_entry(char *spec, char *node, char *type, char *opts,
			      int flags, int freq, int pass)
{
	struct mntent mnt;
	mntFILE *mfp;

	mnt.mnt_fsname = canonicalize (spec);
	mnt.mnt_dir = canonicalize (node);
	mnt.mnt_type = type;
	mnt.mnt_opts = opts;
	mnt.mnt_freq = freq;
	mnt.mnt_passno = pass;
      
	/* We get chatty now rather than after the update to mtab since the
	mount succeeded, even if the write to /etc/mtab should fail.  */
	if (verbose)
		print_one (&mnt);

	if (nomtab || !mtab_is_writable())
		goto bail;

//	if (flags & MS_REMOUNT) {
//		update_mtab (mnt.mnt_dir, &mnt);
//		goto bail;
//	}

	lock_mtab();

	mfp = my_setmntent(MOUNTED, "a+");
	if (mfp == NULL || mfp->mntent_fp == NULL) {
		com_err(progname, OCFS2_ET_IO, "%s, %s", MOUNTED,
			strerror(errno));
		goto unlock;
	}

	if ((my_addmntent (mfp, &mnt)) == 1) {
		com_err(progname, OCFS2_ET_IO, "%s, %s", MOUNTED,
			strerror(errno));
	}

	my_endmntent(mfp);

unlock:
	unlock_mtab();
bail:
	return ;
}

static int get_my_nodenum(uint8_t *nodenum)
{
	FILE *file;
	int ret = -EINVAL;
	int retval=-EINVAL, num;
	nm_op *op = (nm_op *)op_buf;

	memset(op_buf, 0, PAGE_SIZE);
	op->magic = NM_OP_MAGIC;
	op->opcode = NM_OP_GET_GLOBAL_NODE_NUM;
	
	*nodenum = 255;

	file = fopen(CLUSTER_FILE, "r+");
	if (!file)
		return -errno;
	if (fwrite((char *)op, sizeof(nm_op), 1, file) != 1)
		goto done;
	if (fscanf(file, "%d: %d", &retval, &num) != 2 ||
	    retval != 0 || num < 0 || num > 255) {
		ret = -EINVAL;
		goto done;
	}
	*nodenum = num;
	ret = 0;	
done:	
	fclose(file);
	return ret;
}

static int create_group(char *uuid, uint8_t *group_num)
{
	FILE *file;
	int ret = -EINVAL, retval;
	int groupnum = NM_INVALID_SLOT_NUM;
	nm_op *op = (nm_op *)op_buf;
	struct stat st;
	char fname[100];

	if (strlen(uuid) != CLUSTER_DISK_UUID_LEN)
		return -EINVAL;

	sprintf(fname, "/proc/cluster/nm/%s", uuid);
	if (stat(fname, &st) == 0) {
		*group_num = st.st_ino - NM_GROUP_INODE_START;
		return -EEXIST;
	}
	
	*group_num = NM_INVALID_SLOT_NUM;

	memset(op_buf, 0, PAGE_SIZE);
	op->magic = NM_OP_MAGIC;
	op->opcode = NM_OP_CREATE_GROUP;

	op->arg_u.gc.group_num = NM_INVALID_SLOT_NUM;
	strcpy(op->arg_u.gc.name, uuid);
	strcpy(op->arg_u.gc.disk_uuid, uuid);

	file = fopen(CLUSTER_FILE, "r+");
	if (!file)
		return -errno;
	
	if (fwrite((char *)op, sizeof(nm_op), 1, file) != 1)
		goto done;

	if (fscanf(file, "%d: group %d", &retval, &groupnum) != 2) {
		ret = -EINVAL;
		goto done;
	}
	ret = retval;
	if ((ret == 0 || ret == -EEXIST) &&
	    groupnum >= 0 && groupnum < NM_INVALID_SLOT_NUM)
		*group_num = groupnum;
		
done:	
	fclose(file);
	return ret;
}


static int add_to_local_group(char *uuid, uint8_t group_num, uint8_t node_num)
{
	FILE *file;
	int ret = -EINVAL, retval;
	nm_op *op = (nm_op *)op_buf;
	char fname[100];
	DIR *dir;
	struct dirent *de;
	
	if (strlen(uuid) != CLUSTER_DISK_UUID_LEN)
		return -EINVAL;

	sprintf(fname, "/proc/cluster/nm/%s", uuid);
	dir = opendir(fname);
	if (dir) {
		while ((de = readdir(dir)) != NULL) {
			if (de->d_ino - NM_NODE_INODE_START == node_num) {
				closedir(dir);
				return -EEXIST;
			}
		}
		closedir(dir);
	}

	memset(op_buf, 0, PAGE_SIZE);
	op->magic = NM_OP_MAGIC;
	op->opcode = NM_OP_ADD_GROUP_NODE;
	op->arg_u.gc.group_num = group_num;
	op->arg_u.gc.node_num = node_num;
	op->arg_u.gc.slot_num = node_num;

	file = fopen(GROUP_FILE, "r+");
	if (!file)
		return -errno;
	
	if (fwrite((char *)op, sizeof(nm_op), 1, file) != 1)
		goto done;

	if (fscanf(file, "%d: node", &retval) != 1) {
		ret = -EINVAL;
		goto done;
	}
	ret = retval;
		
done:	
	fclose(file);
	return ret;
}

static int activate_group(char *group_name, char *group_dev, uint8_t group_num, 
		   uint32_t block_bits, uint64_t num_blocks, uint64_t start_block)
{
	int dev_fd = -1;
	int ret = -EINVAL, retval;
	FILE *file;
	hb_op *op;

	if (verbose)
		printf("starting disk heartbeat...\n");
	
	memset(op_buf, 0, PAGE_SIZE);
	op = (hb_op *)op_buf;
	op->magic = HB_OP_MAGIC;
	op->opcode = HB_OP_START_DISK_HEARTBEAT;
	op->group_num = group_num;
	strcpy(op->disk_uuid, group_name);
	op->bits = block_bits;
	op->blocks = num_blocks;
	op->start = start_block;
	
	dev_fd = open(group_dev, O_RDWR);
	if (dev_fd == -1)
		return -errno;
	op->fd = dev_fd;

	file = fopen(HEARTBEAT_DISK_FILE, "r+");
	if (!file)
		return -errno;
	
	if (fwrite((char *)op, sizeof(hb_op), 1, file) != 1)
		goto done;

	if (fscanf(file, "%d: ", &retval) != 1) {
		ret = -EINVAL;
		goto done;
	}
	ret = 0;
done:
	/* hb will keep its own ref */
	if (dev_fd != -1)
		close(dev_fd);

	fclose(file);
	return 0;
}

static int get_ocfs2_disk_hb_params(char *group_dev, uint32_t *block_bits, uint32_t *cluster_bits,
			     uint64_t *start_block, uint32_t *num_clusters)
{
	int status = -EINVAL;
	errcode_t ret = 0;
	uint64_t blkno;
	char *buf = NULL;
	char *heartbeat_filename;
	ocfs2_dinode *di;
	ocfs2_extent_rec *rec;
	ocfs2_filesys *fs = NULL;

	ret = ocfs2_open(group_dev, OCFS2_FLAG_RO, 0, 0, &fs);
	if (ret)
		return status;

	heartbeat_filename = ocfs2_system_inodes[HEARTBEAT_SYSTEM_INODE].si_name;
	ret = ocfs2_lookup(fs, fs->fs_sysdir_blkno, heartbeat_filename,
			   strlen(heartbeat_filename),  NULL, &blkno);
	if (ret)
		goto leave;
	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		goto leave;
	
	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret)
		goto leave;

	di = (ocfs2_dinode *)buf;
	if (di->id2.i_list.l_tree_depth || 
	    di->id2.i_list.l_next_free_rec != 1) {
		goto leave;
	}
	rec = &(di->id2.i_list.l_recs[0]);
	
	*block_bits = OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;
	*cluster_bits = OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits;
	*start_block = rec->e_blkno;
	*num_clusters = rec->e_clusters;
	status = 0;

leave:
	if (buf)
		ocfs2_free(&buf);
	if (fs)
		ocfs2_close(fs);
	return status;
}

static int get_node_map(uint8_t group_num, char *bitmap)
{
	FILE *file = NULL;
	hb_op *op;
	int ret = -EINVAL;
	int retval;
	uint8_t bytemap[NM_MAX_NODES];
	int i;

	if (verbose)	
		printf("getting node map...\n");
	
	memset(op_buf, 0, PAGE_SIZE);
	op = (hb_op *)op_buf;
	op->magic = HB_OP_MAGIC;
	op->opcode = HB_OP_GET_NODE_MAP;
	op->group_num = group_num;
	
	file = fopen(HEARTBEAT_DISK_FILE, "r+");
	if (!file)
		return -errno;
	
	if (fwrite((char *)op, sizeof(hb_op), 1, file) != 1)
		goto done;

	if (fscanf(file, "%d: ", &retval) != 1) {
		ret = -EINVAL;
		goto done;
	}
	if (retval != 0) {
		ret = retval;
		goto done;
	}
	if (fread(bytemap, 1, NM_MAX_NODES, file) < NM_MAX_NODES) {
		ret = -EINVAL;
		goto done;
	}

	for (i = 0; i < NM_MAX_NODES; ++i) {
		if (bytemap[i])
			ocfs2_set_bit(i, bitmap);
	}

	ret = 0;
done:
	fclose(file);
	return ret;
}

static int get_raw_node_map(uint8_t groupnum, char *groupdev,
			    uint32_t block_bits, uint32_t num_blocks,
			    uint64_t start_block, char *bitmap)
{
	int i;
	int ret = -EINVAL;
	char *buf = NULL, *tmpbuf;
	hb_disk_heartbeat_block *times = NULL;
	errcode_t err;
	io_channel *channel;

	if (verbose)
		printf("getting raw node map...\n");

	times = malloc(sizeof(hb_disk_heartbeat_block) * NM_MAX_NODES);
	if (!times) {
		ret = -ENOMEM;
		goto done;
	}

	err = io_open(groupdev, OCFS2_FLAG_RO, &channel);
	if (err) {
		ret = -EINVAL;
		goto done;
	}

	err = io_set_blksize(channel, 1 << block_bits);
	if (err) {
		ret = -EINVAL;
		goto done;
	}

	err = ocfs2_malloc_blocks(channel, (int)NM_MAX_NODES, &buf);
	if (err) {
		ret = -ENOMEM;
		goto done;
	}
	
	err = io_read_block(channel, start_block, (int)NM_MAX_NODES, buf);
	if (err) {
		ret = -EIO;
		if (err == OCFS2_ET_SHORT_READ)
			ret = -EINVAL;
		goto done;
	}
	
	tmpbuf = buf;
	for (i=0; i<NM_MAX_NODES; i++) {
		times[i].time = ((hb_disk_heartbeat_block *)tmpbuf)->time;
		tmpbuf += (1 << block_bits);
	}

	/* TODO: how long? */
	sleep(4);

	err = io_read_block(channel, start_block, (int)NM_MAX_NODES, buf);
	if (err) {
		ret = -EIO;
		if (err == OCFS2_ET_SHORT_READ)
			ret = -EINVAL;
		goto done;
	}

	tmpbuf = buf;
	for (i=0; i<NM_MAX_NODES; i++) {
		if (verbose)
			printf("node: %d: before=%"PRIu64", after=%"PRIu64"\n",
			       i, times[i].time, ((hb_disk_heartbeat_block *)tmpbuf)->time);
		if (times[i].time != ((hb_disk_heartbeat_block *)tmpbuf)->time) {
			if (verbose)
				printf(" >>>>>  aha node %d seems to be up!\n", i);
			ocfs2_set_bit(i, bitmap);
		}
		tmpbuf += (1 << block_bits);
	}

	ret = 0;
done:

	if (buf)
		ocfs2_free(&buf);
	io_close(channel);
	if (times)
		free(times);
	return ret;
}

static int create_remote_group(char *group_name, uint8_t node)
{
	int ret, fd = -1, remote_node = -1;
	gsd_ioc ioc;
	char fname[100];
	DIR *dir = NULL;
	struct dirent *de = NULL;

	if (verbose)
		printf("create_remote_group: name=%s, remote node=%u\n", group_name, node);

	/* NOTE: this is a bit of a hack.  we actually normally would not
	 * know which "global" node corresponds to this "group relative" node.
	 * but for now, they directly match up. */
	// sprintf(fname, "/proc/cluster/nm/%s/%03u", group_name, node);
	
	dir = opendir("/proc/cluster/nm");
	if (!dir) {
		ret = -EINVAL;
		goto leave;
	}

	fname[0]=0;
	while ((de = readdir(dir)) != NULL) {
		if (de->d_ino - NM_NODE_INODE_START == node) {
			sprintf(fname, "/proc/cluster/nm/%s", de->d_name);
			break;
		}
	}
	closedir(dir);
	if (!fname[0]) {
		ret = -EINVAL;
		goto leave;
	}
	if (verbose)
		printf("found file %s corresponding to node %u\n", fname, node);

	/* open a file descriptor to the node we want to talk to */
	remote_node = open(fname, O_RDONLY);
	if (remote_node == -1) {
		ret = -errno;
		goto leave;
	}
	if (verbose)
		printf("fd for remote node=%d\n", remote_node);

	/* TODO: move this over to a transaction file on the inode, eliminate the ioctl */
	fd = open("/proc/cluster/net", O_RDONLY);
	if (fd == -1) {
		ret = -errno;
		goto leave;
	}

	if (verbose)
		printf("fd for net ioctl file=%d\n", fd);

	/* call an ioctl to create the group over there */
	memset(&ioc, 0, sizeof(gsd_ioc));
	ioc.fd = remote_node;
	ioc.namelen = strlen(group_name);
	memcpy(ioc.name, group_name, ioc.namelen);
	if (ioctl(fd, GSD_IOC_CREATE_GROUP, &ioc) < 0) {
		ret = -errno;
		goto leave;
	}
	ret = ioc.status;
	if (verbose)
		printf("create group ioctl returned ret=%d\n", ret);

	if (ret != 0 && ret != -EEXIST)
		goto leave;
	
	/* call an ioctl to add this node to the group over there */
	memset(&ioc, 0, sizeof(gsd_ioc));
	ioc.fd = remote_node;
	ioc.namelen = strlen(group_name);
	memcpy(ioc.name, group_name, ioc.namelen);
	if (ioctl(fd, GSD_IOC_ADD_GROUP_NODE, &ioc) < 0) {
		ret = -errno;
		goto leave;
	}
	ret = ioc.status;
	if (verbose)
		printf("add node ioctl returned ret=%d\n", ret);

leave:
	if (fd != -1)
		close(fd);
	if (remote_node != -1)
		close(remote_node);
	return ret;
}

/*
 * this will try to add the group (and the node to the group)
 * for every mount.  luckily, there are many shortcut paths
 * along the way, so checking for -EEXIST will save time.
 */
static int add_me_to_group(char *groupname, char *groupdev)
{
	int ret;
	uint8_t my_nodenum, groupnum;
	uint32_t pre_nodemap[] = {0, 0, 0, 0, 0, 0, 0, 0};
	uint32_t post_nodemap[] = {0, 0, 0, 0, 0, 0, 0, 0};
	int start, next, i;
	uint32_t block_bits, cluster_bits, num_clusters;
	uint64_t start_block, num_blocks;

	/* either create the group or find that it already exists */
	ret = get_my_nodenum(&my_nodenum);
	if (ret < 0)
		return ret;

	ret = get_ocfs2_disk_hb_params(groupdev, &block_bits, &cluster_bits, 
				       &start_block, &num_clusters);
	if (ret < 0)
		return ret;

	num_blocks = num_clusters << cluster_bits;
	num_blocks >>= block_bits;
	
	ret = create_group(groupname, &groupnum);
	if (ret != -EEXIST && ret != 0)
		return ret;

	ret = activate_group(groupname, groupdev, groupnum, block_bits, num_blocks, start_block);
	if (ret < 0)
		return ret;

	ret = add_to_local_group(groupname, groupnum, my_nodenum);
	if (ret != -EEXIST && ret != 0)
		return ret;

	/* at this point my node is heartbeating, so any other nodes 
	 * joining right now must communicate with me */

	while (1) {
		ret = get_node_map(groupnum, (char *)pre_nodemap);
		if (ret < 0)
			return ret;
		if (ocfs2_test_bit(my_nodenum, (char *)pre_nodemap)) {
			if (verbose)
				printf("found myself (%u) in nodemap! continuing...\n", my_nodenum);
			break;
		} else {
			if (verbose)
				printf("have not yet found myself (%u) in nodemap...\n", my_nodenum);
		}
		/* TODO: set this to the default hb interval. 2 seconds right now */
		sleep(2);
	}

	/* now that we see ourself heartbeating, take a look
	 * at ALL of the nodes that seem to be heartbeating 
	 * on this device.  add them here and have them add
	 * me there... */
	ret = get_raw_node_map(groupnum, groupdev, block_bits, num_blocks, start_block, (char *)pre_nodemap);
	if (ret < 0)
		return ret;

again:
	/* go create this group and add this node on every other node I see */	
	start = 0;
	while (1) {
		next = ocfs2_find_next_bit_set((unsigned long *)pre_nodemap, NM_MAX_NODES, start);
		if (next >= NM_MAX_NODES) {
			break;
		}
		if (next != my_nodenum) {
			/* add remote node here... */
			ret = add_to_local_group(groupname, groupnum, next);
			if (ret != -EEXIST && ret != 0)
				return ret;

			/* ...and add this node there */
			ret = create_remote_group(groupname, next);
			if (ret != 0 && ret != -EEXIST) {
				com_err(progname, ret, "unable to create remote group");
				break;
			}
		}
		start = next + 1;
	}
	if (ret != 0 && ret != -EEXIST)
		return ret;

	if (verbose)
		printf("done creating remote groups\n");

	/* grab the nodemap again and look for changes */
	ret = get_raw_node_map(groupnum, groupdev, block_bits, num_blocks, start_block, (char *)post_nodemap);
	if (ret < 0)
		return ret;

	if (verbose)	
		printf("checking raw node map again.....\n");

	if (memcmp(pre_nodemap, post_nodemap, sizeof(pre_nodemap)) == 0) {
		/* nothing changed.  we are DONE! */
		if (verbose)
			printf("woot. nothing changed. all done\n");
		return 0;
	}
	
	if (verbose)	
		printf("something changed\n");
		
	/* something changed */
	for (i=0; i<8; i++) {
		post_nodemap[i] &= ~pre_nodemap[i];
		pre_nodemap[i] = post_nodemap[i];
		post_nodemap[i] = 0;
	}

	/* keep going while there are still nodes to contact */
	if (ocfs2_find_next_bit_set((unsigned long *)pre_nodemap, NM_MAX_NODES, 0) < NM_MAX_NODES)
		goto again;

	if (verbose)
		printf("ah nothing left to care about ... leaving\n");

	return 0;
}

static int process_options(struct mount_options *mo)
{
	if (!mo->dev) {
		com_err(progname, OCFS2_ET_BAD_DEVICE_NAME, " ");
		return -1;
	}

	if (!mo->dir) {
		com_err(progname, OCFS2_ET_INVALID_ARGUMENT, "no mountpoint specified");
		return -1;
	}

	if (mo->opts)
		parse_opts(mo->opts, &mo->flags, &mo->xtra_opts);

	return 0;
}

int main(int argc, char **argv)
{
	errcode_t ret = 0;
	struct mount_options mo = { NULL, NULL, NULL };
	char hbuuid[33];

	initialize_ocfs_error_table();
	initialize_o2dl_error_table();

	read_options (argc, argv, &mo);

	ret = process_options(&mo);
	if (ret)
		goto bail;

	ret = get_uuid(mo.dev, hbuuid);
	if (ret) {
		com_err(progname, ret, "while opening the file system");
		goto bail;
	}

	if (verbose)
		printf("device=%s hbuuid=%s\n", mo.dev, hbuuid);

	ret = add_me_to_group(hbuuid, mo.dev);
	if (ret < 0) {
		fprintf(stderr, "%s: Error '%d' while adding to group\n", progname, ret);
		goto bail;
	}

	mo.xtra_opts = realloc(mo.xtra_opts, (strlen(mo.xtra_opts) +
					      strlen(hbuuid) +
					      strlen("group=") + 1));
	if (!mo.xtra_opts) {
		com_err(progname, OCFS2_ET_NO_MEMORY, " ");
		goto bail;
	}

	if (strlen(mo.xtra_opts))
		strcat(mo.xtra_opts, ",");
	strcat(mo.xtra_opts, "group=");
	strcat(mo.xtra_opts, hbuuid);

	ret = mount(mo.dev, mo.dir, "ocfs2", mo.flags, mo.xtra_opts);
	if (ret) {
		com_err(progname, errno, "while mounting %s on %s", mo.dev, mo.dir);
		goto bail;
	}

	update_mtab_entry(mo.dev, mo.dir, "ocfs2", mo.xtra_opts, mo.flags, 0, 0);

bail:
	if (mo.dev)
		free(mo.dev);
	if (mo.dir)
		free(mo.dir);
	if (mo.opts)
		free(mo.opts);
	if (mo.xtra_opts)
		free(mo.xtra_opts);

	return ret;
}
