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
#include "o2cb.h"

int verbose = 0;
int mount_quiet = 0;
char *progname = NULL;

static int nomtab = 0;

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
 * Code based on similar function in util-linux-2.12p/mount/mount.c
 *
 */
static void print_one(const struct my_mntent *me)
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


static void my_free(const void *s)
{
	if (s)
		free((void *) s);
}

/*
 * Code based on similar function in util-linux-2.12p/mount/mount.c
 *
 */
static void update_mtab_entry(char *spec, char *node, char *type, char *opts,
			      int flags, int freq, int pass)
{
	struct my_mntent mnt;

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

	if (!nomtab && mtab_is_writable()) {
#if 0
		if (flags & MS_REMOUNT)
			update_mtab (mnt.mnt_dir, &mnt);
		else
#endif
		{
			mntFILE *mfp;

			lock_mtab();

			mfp = my_setmntent(MOUNTED, "a+");
			if (mfp == NULL || mfp->mntent_fp == NULL) {
				com_err(progname, OCFS2_ET_IO, "%s, %s",
					MOUNTED, strerror(errno));
			} else {
				if ((my_addmntent (mfp, &mnt)) == 1) {
					com_err(progname, OCFS2_ET_IO, "%s, %s",
						MOUNTED, strerror(errno));
				}
			}
			my_endmntent(mfp);
			unlock_mtab();
		}
	}
	my_free(mnt.mnt_fsname);
	my_free(mnt.mnt_dir);
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
	if (ret) {
		com_err(progname, ret, "while opening the device.");
		return status;
	}

	heartbeat_filename = ocfs2_system_inodes[HEARTBEAT_SYSTEM_INODE].si_name;
	ret = ocfs2_lookup(fs, fs->fs_sysdir_blkno, heartbeat_filename,
			   strlen(heartbeat_filename),  NULL, &blkno);
	if (ret) {
		com_err(progname, ret, "while looking up the hb system inode.");
		goto leave;
	}

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret) {
		com_err(progname, ret, "while allocating a block for hb.");
		goto leave;
	}
	
	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret) {
		com_err(progname, ret, "while reading hb inode.");
		goto leave;
	}

	di = (ocfs2_dinode *)buf;
	if (di->id2.i_list.l_tree_depth || 
	    di->id2.i_list.l_next_free_rec != 1) {
		com_err(progname, 0, "when checking for contiguous hb.");
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

static int start_heartbeat(char *hbuuid, char *device)
{
	int ret;
	char *cluster = NULL;
	errcode_t err;
	uint32_t block_bits, cluster_bits, num_clusters;
	uint64_t start_block, num_blocks;

	ret = get_ocfs2_disk_hb_params(device, &block_bits, &cluster_bits, 
				       &start_block, &num_clusters);
	if (ret < 0) {
		printf("hb_params failed\n");
		return ret;
	}

	num_blocks = num_clusters << cluster_bits;
	num_blocks >>= block_bits;

	/* clamp to NM_MAX_NODES */
	if (num_blocks > 254)
		num_blocks = 254;

        /* XXX: NULL cluster is a hack for right now */
	err = o2cb_create_heartbeat_region_disk(NULL,
						hbuuid,
						device,
						1 << block_bits,
						start_block,
						num_blocks);
	if (err) {
		com_err(progname, err, "while creating hb region with o2cb.");
		return -EINVAL;
	}

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

	ret = start_heartbeat(hbuuid, mo.dev);
	if (ret < 0) {
		fprintf(stderr, "%s: Error '%d' while starting heartbeat\n",
			progname, (int)ret);
		goto bail;
	}

	ret = mount(mo.dev, mo.dir, "ocfs2", mo.flags, mo.xtra_opts);
	if (ret) {
		com_err(progname, errno, "while mounting %s on %s", mo.dev, mo.dir);
		goto bail;
	}

	update_mtab_entry(mo.dev, mo.dir, "ocfs2", mo.xtra_opts, mo.flags, 0, 0);

bail:
	free(mo.dev);
	free(mo.dir);
	free(mo.opts);
	free(mo.xtra_opts);

	return ret;
}
