/*
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
 * Authors: Sunil Mushran
 */


#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE /* Because libc really doesn't want us using O_DIRECT? */

#include <sys/types.h>
#include <asm/types.h>
#include <inttypes.h>

#define u8   __u8
#define s8   __s8
#define u16  __u16
#define s16  __s16
#define u32  __u32
#define s32  __s32
#define u64  __u64
#define s64  __s64
#define atomic_t int
#define spinlock_t unsigned long
typedef unsigned short kdev_t;




#include <asm/page.h>
#include <sys/mount.h>
#include <dirent.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fd.h>
#include <string.h>
#include <sys/stat.h>

#define  OCFS2_FLAT_INCLUDES	1
#include <ocfs2.h>
#include <ocfs2_fs.h>
#include <ocfs1_fs_compat.h>
#include <kernel-list.h>

#include "bitops.h"

#include "ocfs2_nodemanager.h"
#include "ocfs2_heartbeat.h"
#include "ocfs2_tcp.h"

#define CLUSTER_FILE   "/proc/cluster/nm/.cluster"
#define GROUP_FILE     "/proc/cluster/nm/.group"
#define NODE_FILE      "/proc/cluster/nm/.node"
#define HEARTBEAT_DISK_FILE "/proc/cluster/heartbeat/.disk"


int create_remote_group(char *group_name, __u8 node);
int get_node_map(__u8 group_num, char *bitmap);
int get_raw_node_map(__u8 groupnum, char *groupdev, __u32 block_bits, __u32 num_blocks, __u64 start_block, char *bitmap);
int get_ocfs2_disk_hb_params(char *group_dev, __u32 *block_bits, __u32 *cluster_bits,
			     __u64 *start_block, __u32 *num_clusters);
int activate_group(char *group_name, char *group_dev, __u8 group_num, 
 		   __u32 block_bits, __u64 num_blocks, __u64 start_block);
int add_to_local_group(char *uuid, __u8 group_num, __u8 node_num);
int create_group(char *uuid, __u8 *group_num);
int get_my_nodenum(__u8 *nodenum);
int add_me_to_group(char *groupname, char *groupdev);
int ocfs2_detect_one(char *dev, char *uuid, int uuid_size);

char *op_buf = NULL;

/* returns fs_type: 0 for unknown, 1 for ocfs, 2 for ocfs2 */
int ocfs2_detect_one(char *dev, char *uuid, int uuid_size)
{
	ocfs2_filesys *fs = NULL;
	int fs_type = 0;
	int fs_size = sizeof(OCFS2_RAW_SB(fs->fs_super)->s_uuid);
	errcode_t ret;

	if (uuid_size != fs_size)
		goto out;

	ret = ocfs2_open(dev, OCFS2_FLAG_RO, 0, 0, &fs);
	if (ret)
		goto out;

	memcpy(uuid, OCFS2_RAW_SB(fs->fs_super)->s_uuid,
	       uuid_size);
	fs_type = 2;

	ocfs2_close(fs);

out:
	return fs_type;
}

/*
 * main()
 *
 */
int main(int argc, char **argv)
{
	char *device = NULL;
	char *hbuuid = NULL;
	errcode_t ret = 0;
	ocfs2_devices *dev;
	char *p;
	int i;

	if (argc < 2) {
		ret = OCFS2_ET_BAD_DEVICE_NAME;
		com_err(argv[0], ret, "no device specified");
		goto bail;
	}

	device = argv[1];

	op_buf = malloc(PAGE_SIZE);
	if (!op_buf) {
		ret = 1;
		goto bail;
	}

	dev = calloc(1, sizeof(*dev));
	if (dev == NULL) {
		ret = OCFS2_ET_NO_MEMORY;
		com_err("mount.ocfs2", ret, "while allocating a dev");
		goto bail;
	}
	snprintf(dev->dev_name, sizeof(dev->dev_name), "%s", device);

	ret = ocfs2_detect_one(dev->dev_name, dev->uuid, sizeof(dev->uuid));
	if (ret != 2) {
		com_err("mount.ocfs2", ret, "while opening the file system");
		goto bail;
	}
	dev->fs_type = ret;

	hbuuid = malloc(33);
	memset(hbuuid, 0, 33);
	for (i = 0, p = hbuuid; i < 16; i++, p += 2)
		sprintf(p, "%02X", dev->uuid[i]);

	printf("device=%s hbuuid=%s\n", device, hbuuid);

	ret = add_me_to_group(hbuuid, device);
	if (ret < 0) {
		printf("eeek! something bad happened in add_me_to_group: "
		       "ret=%d\n", (int)ret);
		goto bail;
	}

bail:
	if (device)
		free(device);
	if (hbuuid)
		free(hbuuid);
	if (op_buf)
		free(op_buf);

	return ret;
}



/*
 * this will try to add the group (and the node to the group)
 * for every mount.  luckily, there are many shortcut paths
 * along the way, so checking for -EEXIST will save time.
 */
int add_me_to_group(char *groupname, char *groupdev)
{
	int ret;
	__u8 my_nodenum, groupnum;
	__u32 pre_nodemap[] = {0, 0, 0, 0, 0, 0, 0, 0};
	__u32 post_nodemap[] = {0, 0, 0, 0, 0, 0, 0, 0};
	int start, next, i;
	__u32 block_bits, cluster_bits, num_clusters;
	__u64 start_block, num_blocks;

	/* either create the group or find that it already exists */
	ret = get_my_nodenum(&my_nodenum);
	if (ret < 0) {
		printf("I couldn't get my node num!\n");
		return ret;
	}

	ret = get_ocfs2_disk_hb_params(groupdev, &block_bits, &cluster_bits, 
				       &start_block, &num_clusters);
	if (ret < 0) {
		printf("I couldn't get disk hb params!\n");
		return ret;
	}

	num_blocks = num_clusters << cluster_bits;
	num_blocks >>= block_bits;
	
	ret = create_group(groupname, &groupnum);
	if (ret != -EEXIST && ret != 0) {
		printf("add_me_to_group: could not create group!\n");
		return ret;
	}

	ret = activate_group(groupname, groupdev, groupnum, block_bits, num_blocks, start_block);
	if (ret < 0) {
		printf("add_me_to_group: could not activate group\n");
		return ret;
	}

	ret = add_to_local_group(groupname, groupnum, my_nodenum);
	if (ret != -EEXIST && ret != 0) {
		printf("add_me_to_group: could not add myself to the local "
		       "group\n");
		return ret;
	}

	/* at this point my node is heartbeating, so any other nodes 
	 * joining right now must communicate with me */

	while (1) {
		ret = get_node_map(groupnum, (char *)pre_nodemap);
		if (ret < 0) {
			printf("problem re reading node map!\n");
			return ret;
		}
		if (ocfs2_test_bit(my_nodenum, (char *)pre_nodemap)) {
			printf("found myself (%u) in nodemap! continuing...\n", my_nodenum);
			break;
		} else {
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
	if (ret < 0) {
		printf("add_me_to_group: error return %d from "
		       "get_raw_node_map\n", ret);
		return ret;
	}

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
			if (ret != -EEXIST && ret != 0) {
				printf("add_me_to_group: return %d from "
				       "add_to_local_group\n", ret);
				return ret;
			}

			/* ...and add this node there */
			ret = create_remote_group(groupname, next);
			if (ret != 0 && ret != -EEXIST) {
				printf("create_remote_group: node=%u returned %d!\n",
				       next, ret);
				break;
			}
		}
		start = next + 1;
	}
	if (ret != 0 && ret != -EEXIST)
		return ret;

	printf("done creating remote groups\n");

	/* grab the nodemap again and look for changes */
	ret = get_raw_node_map(groupnum, groupdev, block_bits, num_blocks, start_block, (char *)post_nodemap);
	if (ret < 0)
		return ret;
	
	printf("checking raw node map again.....\n");

	if (memcmp(pre_nodemap, post_nodemap, sizeof(pre_nodemap)) == 0) {
		/* nothing changed.  we are DONE! */
		printf("woot. nothing changed. all done\n");
		return 0;
	}
		
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
	
	printf("ah nothing left to care about ... leaving\n");

	return 0;
}

int get_my_nodenum(__u8 *nodenum)
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
	if (!file) {
		ret = -errno;
		printf("get_my_nodenum: error %d opening %s\n", ret,
		       CLUSTER_FILE);
		return ret;
	}
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

int create_group(char *uuid, __u8 *group_num)
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


int add_to_local_group(char *uuid, __u8 group_num, __u8 node_num)
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

int activate_group(char *group_name, char *group_dev, __u8 group_num, 
		   __u32 block_bits, __u64 num_blocks, __u64 start_block)
{
	int dev_fd = -1;
	int ret = -EINVAL, retval;
	FILE *file;
	hb_op *op;

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


int get_ocfs2_disk_hb_params(char *group_dev, __u32 *block_bits, __u32 *cluster_bits,
			     __u64 *start_block, __u32 *num_clusters)
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

int get_node_map(__u8 group_num, char *bitmap)
{
	FILE *file = NULL;
	hb_op *op;
	int ret = -EINVAL;
	int retval;
	
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
	if (fread(bitmap, 1, (NM_MAX_NODES+7)/8, file) < (NM_MAX_NODES+7)/8) {
		ret = -EINVAL;
		goto done;
	}
	ret = 0;
done:
	fclose(file);
	return ret;
}


int get_raw_node_map(__u8 groupnum, char *groupdev, __u32 block_bits, __u32 num_blocks, __u64 start_block, char *bitmap)
{
	int i;
	int ret = -EINVAL;
	char *buf = NULL, *tmpbuf;
	hb_disk_heartbeat_block *times = NULL;

	errcode_t err;
	io_channel *channel;

	
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
//		printf("node: %d: before=%"PRIu64", after=%"PRIu64"\n", i,
//			times[i].time,
//			((hb_disk_heartbeat_block *)tmpbuf)->time);
		if (times[i].time != ((hb_disk_heartbeat_block *)tmpbuf)->time) {
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

int create_remote_group(char *group_name, __u8 node)
{
	int ret, fd = -1, remote_node = -1;
	gsd_ioc ioc;
	char fname[100];
	DIR *dir = NULL;
	struct dirent *de = NULL;

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
	printf("found file %s corresponding to node %u\n", fname, node);

	/* open a file descriptor to the node we want to talk to */
	remote_node = open(fname, O_RDONLY);
	if (remote_node == -1) {
		ret = -errno;
		goto leave;
	}
	printf("fd for remote node=%d\n", remote_node);

	/* TODO: move this over to a transaction file on the inode, eliminate the ioctl */
	fd = open("/proc/cluster/net", O_RDONLY);
	if (fd == -1) {
		ret = -errno;
		goto leave;
	}

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
	printf("add node ioctl returned ret=%d\n", ret);

leave:
	if (fd != -1)
		close(fd);
	if (remote_node != -1)
		close(remote_node);
	return ret;
}
