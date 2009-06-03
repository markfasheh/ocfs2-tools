/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * openfs.c
 *
 * Open an OCFS2 filesystem.  Part of the OCFS2 userspace library.
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License, version 2,  as published by the Free Software Foundation.
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
 * Ideas taken from e2fsprogs/lib/ext2fs/openfs.c
 *   Copyright (C) 1993, 1994, 1995, 1996 Theodore Ts'o.
 */

#define _XOPEN_SOURCE 600  /* Triggers XOPEN2K in features.h */
#define _LARGEFILE64_SOURCE

#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <stdbool.h>

/* I hate glibc and gcc */
#ifndef ULLONG_MAX
# define ULLONG_MAX 18446744073709551615ULL
#endif

#include "ocfs2/byteorder.h"
#include "ocfs2/ocfs2.h"
#include "ocfs2-kernel/ocfs1_fs_compat.h"
#include "ocfs2/image.h"

/*
 * if the file is an o2image file, this routine maps the actual blockno to
 * relative block number in image file and then calls the underlying IO
 * function. At this point this function returns EIO if image file has any
 * holes
 */
static errcode_t __ocfs2_read_blocks(ocfs2_filesys *fs, int64_t blkno,
				     int count, char *data, bool nocache)
{
	int i;
	errcode_t err;

	if (fs->fs_flags & OCFS2_FLAG_IMAGE_FILE) {
		/*
		 * o2image copies all meta blocks. If a caller asks for
		 * N contiguous metadata blocks, all N should be in the
		 * image file. However we check for any holes and
		 * return -EIO if any.
		 */
		for (i = 0; i < count; i++)
			if (!ocfs2_image_test_bit(fs, blkno+i))
				return OCFS2_ET_IO;
		/* translate the block number */
		blkno = ocfs2_image_get_blockno(fs, blkno);
	}

	if (nocache)
		err = io_read_block_nocache(fs->fs_io, blkno, count, data);
	else
		err = io_read_block(fs->fs_io, blkno, count, data);

	return err;
}

errcode_t ocfs2_read_blocks_nocache(ocfs2_filesys *fs, int64_t blkno,
				    int count, char *data)
{
	return __ocfs2_read_blocks(fs, blkno, count, data, true);
}

errcode_t ocfs2_read_blocks(ocfs2_filesys *fs, int64_t blkno,
			    int count, char *data)
{
	return __ocfs2_read_blocks(fs, blkno, count, data, false);
}

static errcode_t ocfs2_validate_ocfs1_header(ocfs2_filesys *fs)
{
	errcode_t ret;
	char *blk;
	struct ocfs1_vol_disk_hdr *hdr;
	
	ret = ocfs2_malloc_block(fs->fs_io, &blk);
	if (ret)
		return ret;

	ret = ocfs2_read_blocks(fs, 0, 1, blk);
	if (ret)
		goto out;
	hdr = (struct ocfs1_vol_disk_hdr *)blk;

	ret = OCFS2_ET_OCFS_REV;
	if (le32_to_cpu(hdr->major_version) == OCFS1_MAJOR_VERSION)
		goto out;
	if (!memcmp(hdr->signature, OCFS1_VOLUME_SIGNATURE,
		    strlen(OCFS1_VOLUME_SIGNATURE)))
		goto out;

	ret = 0;

out:
	ocfs2_free(&blk);

	return ret;
}

errcode_t ocfs2_read_super(ocfs2_filesys *fs, uint64_t superblock, char *sb)
{
	errcode_t ret;
	char *blk, *swapblk;
	struct ocfs2_dinode *di, *orig_super;
	int orig_blocksize;
	int blocksize = io_get_blksize(fs->fs_io);

	ret = ocfs2_malloc_block(fs->fs_io, &blk);
	if (ret)
		return ret;

	ret = ocfs2_read_blocks(fs, superblock, 1, blk);
	if (ret)
		goto out_blk;

	di = (struct ocfs2_dinode *)blk;

	ret = OCFS2_ET_BAD_MAGIC;
	if (memcmp(di->i_signature, OCFS2_SUPER_BLOCK_SIGNATURE,
		   strlen(OCFS2_SUPER_BLOCK_SIGNATURE)))
		goto out_blk;

	/*
	 * We want to use the latest superblock to validate.  We need
	 * a local-endian copy in fs->fs_super, and the unswapped copy to
	 * check in blk.  ocfs2_validate_meta_ecc() uses fs->fs_super and
	 * fs->fs_blocksize.
	 */
	ret = ocfs2_malloc_block(fs->fs_io, &swapblk);
	if (ret)
		goto out_blk;

	memcpy(swapblk, blk, blocksize);
	orig_super = fs->fs_super;
	orig_blocksize = fs->fs_blocksize;
	fs->fs_super = (struct ocfs2_dinode *)swapblk;
	fs->fs_blocksize = blocksize;
	ocfs2_swap_inode_to_cpu(fs, fs->fs_super);

	ret = ocfs2_validate_meta_ecc(fs, blk, &di->i_check);

	fs->fs_super = orig_super;
	fs->fs_blocksize = orig_blocksize;
	ocfs2_free(&swapblk);

	if (ret)
		goto out_blk;

	ocfs2_swap_inode_to_cpu(fs, di);
	if (!sb)
		fs->fs_super = di;
	else {
		memcpy(sb, blk, fs->fs_blocksize);
		ocfs2_free(&blk);
	}

	return 0;

out_blk:
	ocfs2_free(&blk);

	return ret;
}

errcode_t ocfs2_write_primary_super(ocfs2_filesys *fs)
{
	errcode_t ret;
	char *blk;
	struct ocfs2_dinode *di;

	if (!(fs->fs_flags & OCFS2_FLAG_RW))
		return OCFS2_ET_RO_FILESYS;

	blk = (char *)fs->fs_super;
	di = (struct ocfs2_dinode *)blk;

	ret = OCFS2_ET_BAD_MAGIC;
	if (memcmp(di->i_signature, OCFS2_SUPER_BLOCK_SIGNATURE,
		   strlen(OCFS2_SUPER_BLOCK_SIGNATURE)))
		goto out_blk;

	ret = ocfs2_write_inode(fs, OCFS2_SUPER_BLOCK_BLKNO, blk);
	if (ret)
		goto out_blk;

	return 0;

out_blk:
	return ret;
}

errcode_t ocfs2_write_super(ocfs2_filesys *fs)
{
	errcode_t ret;

	ret = ocfs2_write_primary_super(fs);
	if (!ret)
		ret = ocfs2_refresh_backup_supers(fs);

	return ret;
}

errcode_t ocfs2_write_backup_super(ocfs2_filesys *fs, uint64_t blkno)
{
	errcode_t ret;
	char *buf = NULL;
	struct ocfs2_dinode *di;

	if (!(fs->fs_flags & OCFS2_FLAG_RW))
		return OCFS2_ET_RO_FILESYS;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		goto out_blk;

	memcpy(buf, (char *)fs->fs_super, fs->fs_blocksize);
	di = (struct ocfs2_dinode *)buf;

	ret = OCFS2_ET_BAD_MAGIC;
	if (memcmp(di->i_signature, OCFS2_SUPER_BLOCK_SIGNATURE,
		   strlen(OCFS2_SUPER_BLOCK_SIGNATURE)))
		goto out_blk;

	di->i_blkno = blkno;
	OCFS2_SET_COMPAT_FEATURE(OCFS2_RAW_SB(di),
				 OCFS2_FEATURE_COMPAT_BACKUP_SB);
	ret = ocfs2_write_inode(fs, blkno, buf);
	if (ret)
		goto out_blk;

	ret = 0;

out_blk:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

int ocfs2_mount_local(ocfs2_filesys *fs)
{
	return OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat &
	       OCFS2_FEATURE_INCOMPAT_LOCAL_MOUNT;
}

errcode_t ocfs2_open(const char *name, int flags,
		     unsigned int superblock, unsigned int block_size,
		     ocfs2_filesys **ret_fs)
{
	ocfs2_filesys *fs;
	errcode_t ret;
	int i, len;
	char *ptr;
	unsigned char *raw_uuid;

	ret = ocfs2_malloc0(sizeof(ocfs2_filesys), &fs);
	if (ret)
		return ret;

	fs->fs_flags = flags;
	fs->fs_umask = 022;

	ret = io_open(name, (flags & (OCFS2_FLAG_RO | OCFS2_FLAG_RW |
				      OCFS2_FLAG_BUFFERED)),
		      &fs->fs_io);
	if (ret)
		goto out;

	ret = ocfs2_malloc(strlen(name)+1, &fs->fs_devname);
	if (ret)
		goto out;
	strcpy(fs->fs_devname, name);

	/*
	 * If OCFS2_FLAG_IMAGE_FILE is specified, it needs to be handled
	 * differently
	 */
	if (flags & OCFS2_FLAG_IMAGE_FILE) {
		ret = ocfs2_image_load_bitmap(fs);
		if (ret)
			goto out;
		if (!superblock)
			superblock = fs->ost->ost_superblocks[0];
		if (!block_size)
			block_size = fs->ost->ost_fsblksz;
	}


	/*
	 * If OCFS2_FLAG_NO_REV_CHECK is specified, fsck (or someone
	 * like it) is asking to ignore the OCFS vol_header at
	 * block 0.
	 */
	if (!(flags & OCFS2_FLAG_NO_REV_CHECK)) {
		ret = ocfs2_validate_ocfs1_header(fs);
		if (ret)
			goto out;
	}

	if (superblock) {
		ret = OCFS2_ET_INVALID_ARGUMENT;
		if (!block_size)
			goto out;
		io_set_blksize(fs->fs_io, block_size);
		ret = ocfs2_read_super(fs, (uint64_t)superblock, NULL);
	} else {
		superblock = OCFS2_SUPER_BLOCK_BLKNO;
		if (block_size) {
			io_set_blksize(fs->fs_io, block_size);
			ret = ocfs2_read_super(fs, (uint64_t)superblock, NULL);
		} else {
			for (block_size = io_get_blksize(fs->fs_io);
			     block_size <= OCFS2_MAX_BLOCKSIZE;
			     block_size <<= 1) {
				io_set_blksize(fs->fs_io, block_size);
				ret = ocfs2_read_super(fs, (uint64_t)superblock,
						       NULL);
				if ((ret == OCFS2_ET_BAD_MAGIC) ||
				    (ret == OCFS2_ET_IO))
					continue;
				break;
			}
		}
	}
	if (ret)
		goto out;

	fs->fs_blocksize = block_size;
	if (superblock == OCFS2_SUPER_BLOCK_BLKNO) {
		ret = ocfs2_malloc_block(fs->fs_io, &fs->fs_orig_super);
		if (ret)
			goto out;
		memcpy((char *)fs->fs_orig_super,
		       (char *)fs->fs_super, fs->fs_blocksize);
	}

#if 0
	ret = OCFS2_ET_REV_TOO_HIGH;
	if (fs->fs_super->id2.i_super.s_major_rev_level >
	    OCFS2_LIB_CURRENT_REV)
		goto out;
#endif

	if (flags & OCFS2_FLAG_STRICT_COMPAT_CHECK) {
		ret = OCFS2_ET_UNSUPP_FEATURE;
		if (OCFS2_RAW_SB(fs->fs_super)->s_feature_compat &
		    ~OCFS2_LIB_FEATURE_COMPAT_SUPP)
			    goto out;

		/* We need to check s_tunefs_flag also to make sure
		 * fsck.ocfs2 won't try to clean up an aborted tunefs
		 * that it doesn't know.
		 */
		if (OCFS2_HAS_INCOMPAT_FEATURE(OCFS2_RAW_SB(fs->fs_super),
					OCFS2_FEATURE_INCOMPAT_TUNEFS_INPROG) &&
		    (OCFS2_RAW_SB(fs->fs_super)->s_tunefs_flag &
		     ~OCFS2_LIB_ABORTED_TUNEFS_SUPP))
			goto out;
	}

	ret = OCFS2_ET_UNSUPP_FEATURE;
	if (OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat &
	    ~OCFS2_LIB_FEATURE_INCOMPAT_SUPP)
		goto out;

	ret = OCFS2_ET_RO_UNSUPP_FEATURE;
	if ((flags & OCFS2_FLAG_RW) &&
	    (OCFS2_RAW_SB(fs->fs_super)->s_feature_ro_compat &
	     ~OCFS2_LIB_FEATURE_RO_COMPAT_SUPP))
		goto out;

	ret = OCFS2_ET_UNSUPP_FEATURE;
	if (!(flags & OCFS2_FLAG_HEARTBEAT_DEV_OK) &&
	    (OCFS2_RAW_SB(fs->fs_super)->s_feature_incompat &
	     OCFS2_FEATURE_INCOMPAT_HEARTBEAT_DEV))
		goto out;

	ret = OCFS2_ET_CORRUPT_SUPERBLOCK;
	if (!OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits)
		goto out;
	if (fs->fs_super->i_blkno != superblock)
		goto out;
	if ((OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits < 12) ||
	    (OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits > 20))
		goto out;
	if (!OCFS2_RAW_SB(fs->fs_super)->s_root_blkno ||
	    !OCFS2_RAW_SB(fs->fs_super)->s_system_dir_blkno)
		goto out;
	if (OCFS2_RAW_SB(fs->fs_super)->s_max_slots > OCFS2_MAX_SLOTS)
		goto out;

	ret = ocfs2_malloc0(OCFS2_RAW_SB(fs->fs_super)->s_max_slots *
			    sizeof(ocfs2_cached_inode *), 
			    &fs->fs_inode_allocs);
	if (ret)
		goto out;

	ret = ocfs2_malloc0(OCFS2_RAW_SB(fs->fs_super)->s_max_slots *
			    sizeof(ocfs2_cached_inode *), 
			    &fs->fs_eb_allocs);
	if (ret)
		goto out;

	ret = OCFS2_ET_UNEXPECTED_BLOCK_SIZE;
	if (block_size !=
	    (1U << OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits))
		goto out;

	fs->fs_clustersize =
		1 << OCFS2_RAW_SB(fs->fs_super)->s_clustersize_bits;

	/* FIXME: Read the system dir */
	
	fs->fs_root_blkno =
		OCFS2_RAW_SB(fs->fs_super)->s_root_blkno;
	fs->fs_sysdir_blkno =
		OCFS2_RAW_SB(fs->fs_super)->s_system_dir_blkno;

	fs->fs_clusters = fs->fs_super->i_clusters;
	fs->fs_blocks = ocfs2_clusters_to_blocks(fs, fs->fs_clusters);
	fs->fs_first_cg_blkno = 
		OCFS2_RAW_SB(fs->fs_super)->s_first_cluster_group;

	raw_uuid = OCFS2_RAW_SB(fs->fs_super)->s_uuid;
	for (i = 0, ptr = fs->uuid_str; i < OCFS2_VOL_UUID_LEN; i++) {
		/* print with null */
		len = snprintf(ptr, 3, "%02X", raw_uuid[i]);
		if (len != 2) {
			ret = OCFS2_ET_INTERNAL_FAILURE;
			goto out;
		}
		/* then only advace past the last char */
		ptr += 2;
	}

	*ret_fs = fs;
	return 0;

out:
	if (fs->fs_inode_allocs)
		ocfs2_free(&fs->fs_inode_allocs);

	ocfs2_freefs(fs);

	return ret;
}

#ifdef DEBUG_EXE
#include <getopt.h>
#include <limits.h>
#include <stdlib.h>

static int64_t read_number(const char *num)
{
	int64_t val;
	char *ptr;

	val = strtoll(num, &ptr, 0);
	if (!ptr || *ptr)
		return 0;

	return val;
}

static void print_usage(void)
{
	fprintf(stderr,
		"Usage: openfs [-s <superblock>] [-B <blksize>]\n"
	       	"               <filename>\n");
}

extern int opterr, optind;
extern char *optarg;

int main(int argc, char *argv[])
{
	errcode_t ret;
	int c;
	int64_t blkno, blksize;
	char *filename;
	ocfs2_filesys *fs;

	/* These mean "autodetect" */
	blksize = 0;
	blkno = 0;

	initialize_ocfs_error_table();

	while((c = getopt(argc, argv, "s:B:")) != EOF) {
		switch (c) {
			case 's':
				blkno = read_number(optarg);
				if (blkno < OCFS2_SUPER_BLOCK_BLKNO) {
					fprintf(stderr,
						"Invalid blkno: %s\n",
						optarg);
					print_usage();
					return 1;
				}
				break;

			case 'B':
				blksize = read_number(optarg);
				if (blksize < OCFS2_MIN_BLOCKSIZE) {
					fprintf(stderr, 
						"Invalid blksize: %s\n",
						optarg);
					print_usage();
					return 1;
				}
				break;

			default:
				print_usage();
				return 1;
				break;
		}
	}

	if (blksize % OCFS2_MIN_BLOCKSIZE) {
		fprintf(stderr, "Invalid blocksize: %"PRId64"\n", blksize);
		print_usage();
		return 1;
	}

	if (optind >= argc) {
		fprintf(stderr, "Missing filename\n");
		print_usage();
		return 1;
	}

	filename = argv[optind];

	ret = ocfs2_open(filename, OCFS2_FLAG_RO, blkno, blksize, &fs);
	if (ret) {
		com_err(argv[0], ret,
			"while opening file \"%s\"", filename);
		goto out;
	}

	fprintf(stdout, "OCFS2 filesystem on \"%s\":\n", filename);
	fprintf(stdout,
		"\tblocksize = %d\n"
 		"\tclustersize = %d\n"
		"\tclusters = %u\n"
		"\tblocks = %"PRIu64"\n"
		"\troot_blkno = %"PRIu64"\n"
		"\tsystem_dir_blkno = %"PRIu64"\n",
 		fs->fs_blocksize,
		fs->fs_clustersize,
		fs->fs_clusters,
		fs->fs_blocks,
		fs->fs_root_blkno,
		fs->fs_sysdir_blkno);

	ret = ocfs2_close(fs);
	if (ret) {
		com_err(argv[0], ret,
			"while closing file \"%s\"", filename);
	}

out:
	return 0;
}
#endif  /* DEBUG_EXE */
