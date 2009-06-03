/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * inode.c
 *
 * Inode operations for the OCFS2 userspace library.
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
 * Ideas taken from e2fsprogs/lib/ext2fs/inode.c
 *   Copyright (C) 1993, 1994, 1995, 1996 Theodore Ts'o.
 */

#define _XOPEN_SOURCE 600  /* Triggers XOPEN2K in features.h */
#define _LARGEFILE64_SOURCE

#include <string.h>
#include <inttypes.h>

#include "ocfs2/byteorder.h"
#include "ocfs2/ocfs2.h"


errcode_t ocfs2_check_directory(ocfs2_filesys *fs, uint64_t dir)
{
	struct ocfs2_dinode *inode;
	char *buf;
	errcode_t ret;

	if ((dir < OCFS2_SUPER_BLOCK_BLKNO) ||
	    (dir > fs->fs_blocks))
		return OCFS2_ET_BAD_BLKNO;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_read_inode(fs, dir, buf);
	if (ret)
		goto out_buf;

	inode = (struct ocfs2_dinode *)buf;
	if (!S_ISDIR(inode->i_mode))
		ret = OCFS2_ET_NO_DIRECTORY;

out_buf:
	ocfs2_free(&buf);

	return ret;
}

static void ocfs2_swap_inode_third(ocfs2_filesys *fs, struct ocfs2_dinode *di)
{

	if (di->i_flags & OCFS2_CHAIN_FL) {
		struct ocfs2_chain_list *cl = &di->id2.i_chain;
		uint16_t i;

		for (i = 0; i < cl->cl_next_free_rec; i++) {
			struct ocfs2_chain_rec *rec = &cl->cl_recs[i];

			if (ocfs2_swap_barrier(fs, di, rec,
					       sizeof(struct ocfs2_chain_rec)))
				break;

			rec->c_free  = bswap_32(rec->c_free);
			rec->c_total = bswap_32(rec->c_total);
			rec->c_blkno = bswap_64(rec->c_blkno);
		}

	} else if (di->i_flags & OCFS2_DEALLOC_FL) {
		struct ocfs2_truncate_log *tl = &di->id2.i_dealloc;
		uint16_t i;

		for(i = 0; i < tl->tl_count; i++) {
			struct ocfs2_truncate_rec *rec =
				&tl->tl_recs[i];

			if (ocfs2_swap_barrier(fs, di, rec,
					     sizeof(struct ocfs2_truncate_rec)))
				break;

			rec->t_start    = bswap_32(rec->t_start);
			rec->t_clusters = bswap_32(rec->t_clusters);
		}
	}
}

static void ocfs2_swap_inode_second(struct ocfs2_dinode *di)
{
	if (S_ISCHR(di->i_mode) || S_ISBLK(di->i_mode))
		di->id1.dev1.i_rdev = bswap_64(di->id1.dev1.i_rdev);
	else if (di->i_flags & OCFS2_BITMAP_FL) {
		di->id1.bitmap1.i_used = bswap_32(di->id1.bitmap1.i_used);
		di->id1.bitmap1.i_total = bswap_32(di->id1.bitmap1.i_total);
	} else if (di->i_flags & OCFS2_JOURNAL_FL) {
		di->id1.journal1.ij_flags = bswap_32(di->id1.journal1.ij_flags);
		di->id1.journal1.ij_recovery_generation =
			bswap_32(di->id1.journal1.ij_recovery_generation);
	}

	/* we need to be careful to swap the union member that is in use.
	 * first the ones that are explicitly marked with flags.. */ 
	if (di->i_flags & OCFS2_SUPER_BLOCK_FL) {
		struct ocfs2_super_block *sb = &di->id2.i_super;

		sb->s_major_rev_level     = bswap_16(sb->s_major_rev_level);
		sb->s_minor_rev_level     = bswap_16(sb->s_minor_rev_level);
		sb->s_mnt_count           = bswap_16(sb->s_mnt_count);
		sb->s_max_mnt_count       = bswap_16(sb->s_max_mnt_count);
		sb->s_state               = bswap_16(sb->s_state);
		sb->s_errors              = bswap_16(sb->s_errors);
		sb->s_checkinterval       = bswap_32(sb->s_checkinterval);
		sb->s_lastcheck           = bswap_64(sb->s_lastcheck);
		sb->s_creator_os          = bswap_32(sb->s_creator_os);
		sb->s_feature_compat      = bswap_32(sb->s_feature_compat);
		sb->s_feature_ro_compat   = bswap_32(sb->s_feature_ro_compat);
		sb->s_feature_incompat    = bswap_32(sb->s_feature_incompat);
		sb->s_root_blkno          = bswap_64(sb->s_root_blkno);
		sb->s_system_dir_blkno    = bswap_64(sb->s_system_dir_blkno);
		sb->s_blocksize_bits      = bswap_32(sb->s_blocksize_bits);
		sb->s_clustersize_bits    = bswap_32(sb->s_clustersize_bits);
		sb->s_max_slots           = bswap_16(sb->s_max_slots);
		sb->s_tunefs_flag         = bswap_16(sb->s_tunefs_flag);
		sb->s_uuid_hash           = bswap_32(sb->s_uuid_hash);
		sb->s_first_cluster_group = bswap_64(sb->s_first_cluster_group);
		sb->s_xattr_inline_size   = bswap_16(sb->s_xattr_inline_size);

	} else if (di->i_flags & OCFS2_LOCAL_ALLOC_FL) {
		struct ocfs2_local_alloc *la = &di->id2.i_lab;

		la->la_bm_off = bswap_32(la->la_bm_off);
		la->la_size   = bswap_16(la->la_size);

	} else if (di->i_flags & OCFS2_CHAIN_FL) {
		struct ocfs2_chain_list *cl = &di->id2.i_chain;

		cl->cl_cpg           = bswap_16(cl->cl_cpg);
		cl->cl_bpc           = bswap_16(cl->cl_bpc);
		cl->cl_count         = bswap_16(cl->cl_count);
		cl->cl_next_free_rec = bswap_16(cl->cl_next_free_rec);

	} else if (di->i_flags & OCFS2_DEALLOC_FL) {
		struct ocfs2_truncate_log *tl = &di->id2.i_dealloc;

		tl->tl_count = bswap_16(tl->tl_count);
		tl->tl_used  = bswap_16(tl->tl_used);
	} else if (di->i_dyn_features & OCFS2_INLINE_DATA_FL) {
		struct ocfs2_inline_data *id = &di->id2.i_data;

		id->id_count = bswap_16(id->id_count);
	}
}

static void ocfs2_swap_inode_first(struct ocfs2_dinode *di)
{
	di->i_generation    = bswap_32(di->i_generation);
	di->i_suballoc_slot = bswap_16(di->i_suballoc_slot);
	di->i_suballoc_bit  = bswap_16(di->i_suballoc_bit);
	di->i_xattr_inline_size = bswap_16(di->i_xattr_inline_size);
	di->i_clusters      = bswap_32(di->i_clusters);
	di->i_uid           = bswap_32(di->i_uid);
	di->i_gid           = bswap_32(di->i_gid);
	di->i_size          = bswap_64(di->i_size);
	di->i_mode          = bswap_16(di->i_mode);
	di->i_links_count   = bswap_16(di->i_links_count);
	di->i_flags         = bswap_32(di->i_flags);
	di->i_atime         = bswap_64(di->i_atime);
	di->i_ctime         = bswap_64(di->i_ctime);
	di->i_mtime         = bswap_64(di->i_mtime);
	di->i_dtime         = bswap_64(di->i_dtime);
	di->i_blkno         = bswap_64(di->i_blkno);
	di->i_last_eb_blk   = bswap_64(di->i_last_eb_blk);
	di->i_fs_generation = bswap_32(di->i_fs_generation);
	di->i_atime_nsec    = bswap_32(di->i_atime_nsec);
	di->i_ctime_nsec    = bswap_32(di->i_ctime_nsec);
	di->i_mtime_nsec    = bswap_32(di->i_mtime_nsec);
	di->i_attr          = bswap_32(di->i_attr);
	di->i_orphaned_slot = bswap_16(di->i_orphaned_slot);
	di->i_dyn_features  = bswap_16(di->i_dyn_features);
	di->i_xattr_loc     = bswap_64(di->i_xattr_loc);
}

static int has_extents(struct ocfs2_dinode *di)
{
	/* inodes flagged with other stuff in id2 */
	if (di->i_flags & (OCFS2_SUPER_BLOCK_FL | OCFS2_LOCAL_ALLOC_FL |
			   OCFS2_CHAIN_FL | OCFS2_DEALLOC_FL))
		return 0;
	/* i_flags doesn't indicate when id2 is a fast symlink */
	if (S_ISLNK(di->i_mode) && di->i_size && di->i_clusters == 0)
		return 0;
	if (di->i_dyn_features & OCFS2_INLINE_DATA_FL)
		return 0;

	return 1;
}

static inline void ocfs2_swap_inline_dir(ocfs2_filesys *fs,
					 struct ocfs2_dinode *di, int to_cpu)
{
	void *de_buf = di->id2.i_data.id_data;
	uint64_t bytes = di->id2.i_data.id_count;
	int max_inline = ocfs2_max_inline_data(fs->fs_blocksize);

	if (di->i_dyn_features & OCFS2_INLINE_XATTR_FL)
		max_inline -= di->i_xattr_inline_size;

	/* Just in case i_xattr_inline_size is garbage */
	if (max_inline < 0)
		max_inline = 0;

	if (bytes > max_inline)
	    bytes = max_inline;

	if (to_cpu)
		ocfs2_swap_dir_entries_to_cpu(de_buf, bytes);
	else
		ocfs2_swap_dir_entries_from_cpu(de_buf, bytes);
}

void ocfs2_swap_inode_from_cpu(ocfs2_filesys *fs, struct ocfs2_dinode *di)
{
	if (cpu_is_little_endian)
		return;

	if (di->i_dyn_features & OCFS2_INLINE_XATTR_FL) {
		struct ocfs2_xattr_header *xh = (struct ocfs2_xattr_header *)
			((void *)di + fs->fs_blocksize -
			 di->i_xattr_inline_size);
		ocfs2_swap_xattrs_from_cpu(fs, di, xh);
	}
	if (has_extents(di))
		ocfs2_swap_extent_list_from_cpu(fs, di, &di->id2.i_list);
	if (di->i_dyn_features & OCFS2_INLINE_DATA_FL && S_ISDIR(di->i_mode))
		ocfs2_swap_inline_dir(fs, di, 0);
	ocfs2_swap_inode_third(fs, di);
	ocfs2_swap_inode_second(di);
	ocfs2_swap_inode_first(di);
}

void ocfs2_swap_inode_to_cpu(ocfs2_filesys *fs, struct ocfs2_dinode *di)
{
	if (cpu_is_little_endian)
		return;

	ocfs2_swap_inode_first(di);
	ocfs2_swap_inode_second(di);
	ocfs2_swap_inode_third(fs, di);
	if (di->i_dyn_features & OCFS2_INLINE_DATA_FL && S_ISDIR(di->i_mode))
		ocfs2_swap_inline_dir(fs, di, 1);
	if (has_extents(di))
		ocfs2_swap_extent_list_to_cpu(fs, di, &di->id2.i_list);
	if (di->i_dyn_features & OCFS2_INLINE_XATTR_FL) {
		struct ocfs2_xattr_header *xh = (struct ocfs2_xattr_header *)
			((void *)di + fs->fs_blocksize -
			 di->i_xattr_inline_size);
		ocfs2_swap_xattrs_to_cpu(fs, di, xh);
	}
}

errcode_t ocfs2_read_inode(ocfs2_filesys *fs, uint64_t blkno,
			   char *inode_buf)
{
	errcode_t ret;
	char *blk;
	struct ocfs2_dinode *di;

	if ((blkno < OCFS2_SUPER_BLOCK_BLKNO) ||
	    (blkno > fs->fs_blocks))
		return OCFS2_ET_BAD_BLKNO;

	ret = ocfs2_malloc_block(fs->fs_io, &blk);
	if (ret)
		return ret;

	ret = ocfs2_read_blocks(fs, blkno, 1, blk);
	if (ret)
		goto out;

	di = (struct ocfs2_dinode *)blk;
	ret = ocfs2_validate_meta_ecc(fs, blk, &di->i_check);
	if (ret)
		goto out;

	ret = OCFS2_ET_BAD_INODE_MAGIC;
	if (memcmp(di->i_signature, OCFS2_INODE_SIGNATURE,
		   strlen(OCFS2_INODE_SIGNATURE)))
		goto out;

	memcpy(inode_buf, blk, fs->fs_blocksize);

	di = (struct ocfs2_dinode *) inode_buf;
	ocfs2_swap_inode_to_cpu(fs, di);

	ret = 0;
out:
	ocfs2_free(&blk);

	return ret;
}

errcode_t ocfs2_write_inode(ocfs2_filesys *fs, uint64_t blkno,
			    char *inode_buf)
{
	errcode_t ret;
	char *blk;
	struct ocfs2_dinode *di;

	if (!(fs->fs_flags & OCFS2_FLAG_RW))
		return OCFS2_ET_RO_FILESYS;

	if ((blkno < OCFS2_SUPER_BLOCK_BLKNO) ||
	    (blkno > fs->fs_blocks))
		return OCFS2_ET_BAD_BLKNO;

	ret = ocfs2_malloc_block(fs->fs_io, &blk);
	if (ret)
		return ret;

	memcpy(blk, inode_buf, fs->fs_blocksize);

	di = (struct ocfs2_dinode *)blk;
	ocfs2_swap_inode_from_cpu(fs, di);

	ocfs2_compute_meta_ecc(fs, blk, &di->i_check);
	ret = io_write_block(fs->fs_io, blkno, 1, blk);
	if (ret)
		goto out;

	fs->fs_flags |= OCFS2_FLAG_CHANGED;
	ret = 0;

out:
	ocfs2_free(&blk);

	return ret;
}


#ifdef DEBUG_EXE
#include <stdlib.h>

static uint64_t read_number(const char *num)
{
	uint64_t val;
	char *ptr;

	val = strtoull(num, &ptr, 0);
	if (!ptr || *ptr)
		return 0;

	return val;
}

static void print_usage(void)
{
	fprintf(stderr,
		"Usage: inode <filename> <inode_num>\n");
}

extern int opterr, optind;
extern char *optarg;

int main(int argc, char *argv[])
{
	errcode_t ret;
	uint64_t blkno;
	char *filename, *buf;
	ocfs2_filesys *fs;
	struct ocfs2_dinode *di;

	blkno = OCFS2_SUPER_BLOCK_BLKNO;

	initialize_ocfs_error_table();

	if (argc < 2) {
		fprintf(stderr, "Missing filename\n");
		print_usage();
		return 1;
	}
	filename = argv[1];

	if (argc > 2) {
		blkno = read_number(argv[2]);
		if (blkno < OCFS2_SUPER_BLOCK_BLKNO) {
			fprintf(stderr, "Invalid blockno: %"PRIu64"\n", blkno);
			print_usage();
			return 1;
		}
	}

	ret = ocfs2_open(filename, OCFS2_FLAG_RO, 0, 0, &fs);
	if (ret) {
		com_err(argv[0], ret,
			"while opening file \"%s\"", filename);
		goto out;
	}

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret) {
		com_err(argv[0], ret,
			"while allocating inode buffer");
		goto out_close;
	}


	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret) {
		com_err(argv[0], ret, "while reading inode %"PRIu64, blkno);
		goto out_free;
	}

	di = (struct ocfs2_dinode *)buf;

	fprintf(stdout, "OCFS2 inode %"PRIu64" on \"%s\"\n", blkno,
		filename);


out_free:
	ocfs2_free(&buf);

out_close:
	ret = ocfs2_close(fs);
	if (ret) {
		com_err(argv[0], ret,
			"while closing file \"%s\"", filename);
	}

out:
	return 0;
}
#endif  /* DEBUG_EXE */
