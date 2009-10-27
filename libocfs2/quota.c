/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * quota.c
 *
 * Quota operations for the OCFS2 userspace library.
 *
 * Copyright (C) 2008 Novell.  All rights reserved.
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
 */

#include <inttypes.h>

#include "ocfs2/byteorder.h"
#include "ocfs2/ocfs2.h"

void ocfs2_swap_quota_header(struct ocfs2_disk_dqheader *header)
{
	if (cpu_is_little_endian)
		return;
	header->dqh_magic = bswap_32(header->dqh_magic);
	header->dqh_version = bswap_32(header->dqh_version);
}

void ocfs2_swap_quota_local_info(struct ocfs2_local_disk_dqinfo *info)
{
	if (cpu_is_little_endian)
		return;
	info->dqi_flags = bswap_32(info->dqi_flags);
	info->dqi_chunks = bswap_32(info->dqi_chunks);
	info->dqi_blocks = bswap_32(info->dqi_blocks);
}

void ocfs2_swap_quota_chunk_header(struct ocfs2_local_disk_chunk *chunk)
{
	if (cpu_is_little_endian)
		return;
	chunk->dqc_free = bswap_32(chunk->dqc_free);
}

void ocfs2_swap_quota_global_info(struct ocfs2_global_disk_dqinfo *info)
{
	if (cpu_is_little_endian)
		return;
	info->dqi_bgrace = bswap_32(info->dqi_bgrace);
	info->dqi_igrace = bswap_32(info->dqi_igrace);
	info->dqi_syncms = bswap_32(info->dqi_syncms);
	info->dqi_blocks = bswap_32(info->dqi_blocks);
	info->dqi_free_blk = bswap_32(info->dqi_free_blk);
	info->dqi_free_entry = bswap_32(info->dqi_free_entry);
}

void ocfs2_swap_quota_global_dqblk(struct ocfs2_global_disk_dqblk *dqblk)
{
	if (cpu_is_little_endian)
		return;
	dqblk->dqb_id = bswap_32(dqblk->dqb_id);
	dqblk->dqb_use_count = bswap_32(dqblk->dqb_use_count);
	dqblk->dqb_ihardlimit = bswap_64(dqblk->dqb_ihardlimit);
	dqblk->dqb_isoftlimit = bswap_64(dqblk->dqb_isoftlimit);
	dqblk->dqb_curinodes = bswap_64(dqblk->dqb_curinodes);
	dqblk->dqb_bhardlimit = bswap_64(dqblk->dqb_bhardlimit);
	dqblk->dqb_bsoftlimit = bswap_64(dqblk->dqb_bsoftlimit);
	dqblk->dqb_curspace = bswap_64(dqblk->dqb_curspace);
	dqblk->dqb_btime = bswap_64(dqblk->dqb_btime);
	dqblk->dqb_itime = bswap_64(dqblk->dqb_itime);
}

void ocfs2_swap_quota_leaf_block_header(struct qt_disk_dqdbheader *bheader)
{
	if (cpu_is_little_endian)
		return;
	bheader->dqdh_next_free = bswap_32(bheader->dqdh_next_free);
	bheader->dqdh_prev_free = bswap_32(bheader->dqdh_prev_free);
	bheader->dqdh_entries = bswap_16(bheader->dqdh_entries);
}

/* Should be power of two */
#define DEFAULT_QUOTA_HASH_SIZE 8192
/* Maxinum number of hash buckets - use at most 16 MB on a 64-bit arch */
#define MAX_QUOTA_HASH_SIZE (1<<21)

errcode_t ocfs2_new_quota_hash(ocfs2_quota_hash **hashp)
{
	ocfs2_quota_hash *hash;
	errcode_t err;

	err = ocfs2_malloc(sizeof(ocfs2_quota_hash), &hash);
	if (err)
		return err;
	hash->alloc_entries = DEFAULT_QUOTA_HASH_SIZE;
	hash->used_entries = 0;
	err = ocfs2_malloc0(sizeof(ocfs2_quota_hash *) *
			    DEFAULT_QUOTA_HASH_SIZE, &hash->hash);
	if (err) {
		ocfs2_free(&hash);
		return err;
	}
	*hashp = hash;
	return 0;
}

errcode_t ocfs2_free_quota_hash(ocfs2_quota_hash *hash)
{
	errcode_t err = 0, ret;

	if (hash->used_entries)
		return OCFS2_ET_NONEMTY_QUOTA_HASH;
	ret = ocfs2_free(&hash->hash);
	if (!err && ret)
		err = ret;
	ret = ocfs2_free(&hash);
	if (!err && ret)
		err = ret;
	return err;
}

static int quota_hash(ocfs2_quota_hash *hash, qid_t id)
{
	return (((unsigned long)id) * 5) & (hash->alloc_entries - 1);
}

static void quota_add_hash_chain(ocfs2_quota_hash *hash,
				 ocfs2_cached_dquot *dquot)
{
	int hash_val = quota_hash(hash, dquot->d_ddquot.dqb_id);

	dquot->d_next = hash->hash[hash_val];
	if (dquot->d_next)
		dquot->d_next->d_pprev = &dquot->d_next;
	hash->hash[hash_val] = dquot;
	dquot->d_pprev = hash->hash + hash_val;
}

errcode_t ocfs2_insert_quota_hash(ocfs2_quota_hash *hash,
				  ocfs2_cached_dquot *dquot)
{
	errcode_t err;

	if (hash->used_entries > hash->alloc_entries &&
	    hash->alloc_entries * 2 < MAX_QUOTA_HASH_SIZE) {
		ocfs2_cached_dquot **new_hash, **old_hash;
		ocfs2_cached_dquot *h_dquot, *h_next;
		int i;
		int old_entries;

		err = ocfs2_malloc0(sizeof(ocfs2_quota_hash *) *
				    hash->alloc_entries * 2, &new_hash);
		if (err)
			return err;
		old_entries = hash->alloc_entries;
		old_hash = hash->hash;
		hash->alloc_entries *= 2;
		hash->hash = new_hash;
		/* Rehash */
		for (i = 0; i < old_entries; i++) {
			for (h_dquot = old_hash[i]; h_dquot; h_dquot = h_next) {
				h_next = h_dquot->d_next;
				quota_add_hash_chain(hash, h_dquot);
			}
		}
		err = ocfs2_free(&old_hash);
		if (err)
			return err;
	}
	quota_add_hash_chain(hash, dquot);
	hash->used_entries++;
	return 0;
}

errcode_t ocfs2_remove_quota_hash(ocfs2_quota_hash *hash,
				  ocfs2_cached_dquot *dquot)
{
	*(dquot->d_pprev) = dquot->d_next;
	if (dquot->d_next)
		dquot->d_next->d_pprev = dquot->d_pprev;
	hash->used_entries--;
	return 0;
}

errcode_t ocfs2_find_quota_hash(ocfs2_quota_hash *hash, qid_t id,
				ocfs2_cached_dquot **dquotp)
{
	int hash_val = quota_hash(hash, id);
	ocfs2_cached_dquot *dquot;

	for (dquot = hash->hash[hash_val]; dquot; dquot = dquot->d_next) {
		if (dquot->d_ddquot.dqb_id == id) {
			*dquotp = dquot;
			return 0;
		}
	}
	*dquotp = NULL;
	return 0;
}

errcode_t ocfs2_find_create_quota_hash(ocfs2_quota_hash *hash, qid_t id,
				       ocfs2_cached_dquot **dquotp)
{
	errcode_t err;

	err = ocfs2_find_quota_hash(hash, id, dquotp);
	if (err)
		return err;
	if (*dquotp)
		return 0;
	err = ocfs2_malloc0(sizeof(ocfs2_cached_dquot), dquotp);
	if (err)
		return err;
	(*dquotp)->d_ddquot.dqb_id = id;
	err = ocfs2_insert_quota_hash(hash, *dquotp);
	if (err) {
		ocfs2_free(dquotp);
		return err;
	}
	return 0;
}

errcode_t ocfs2_find_read_quota_hash(ocfs2_filesys *fs, ocfs2_quota_hash *hash,
				     int type, qid_t id,
				     ocfs2_cached_dquot **dquotp)
{
	errcode_t err;

	err = ocfs2_find_quota_hash(hash, id, dquotp);
	if (err)
		return err;
	if (*dquotp)
		return 0;

	err = ocfs2_read_dquot(fs, type, id, dquotp);
	if (err)
		return err;

	err = ocfs2_insert_quota_hash(hash, *dquotp);
	if (err) {
		ocfs2_free(dquotp);
		return err;
	}
	return 0;
}

errcode_t ocfs2_compute_quota_usage(ocfs2_filesys *fs,
				    ocfs2_quota_hash *usr_hash,
				    ocfs2_quota_hash *grp_hash)
{
	errcode_t err = 0;
	ocfs2_inode_scan *scan;
	uint64_t blkno;
	char *buf;
	int close_scan = 0;
	struct ocfs2_dinode *di;
	ocfs2_cached_dquot *dquot;

	err = ocfs2_malloc_block(fs->fs_io, &buf);
	if (err)
		return err;
	di = (struct ocfs2_dinode *)buf;

	err = ocfs2_open_inode_scan(fs, &scan);
	if (err)
		goto out;
	close_scan = 1;

	while (1) {
		err = ocfs2_get_next_inode(scan, &blkno, buf);
		if (err || !blkno)
			break;
		/*
		 * Check whether the inode looks reasonable and interesting
		 * for quota
		 */
		if (memcmp(di->i_signature, OCFS2_INODE_SIGNATURE,
			   strlen(OCFS2_INODE_SIGNATURE)))
			continue;
		ocfs2_swap_inode_to_cpu(fs, di);
		if (di->i_fs_generation != fs->fs_super->i_fs_generation)
			continue;
		if (!(di->i_flags & OCFS2_VALID_FL))
			continue;
		if (di->i_flags & OCFS2_SYSTEM_FL &&
		    blkno != OCFS2_RAW_SB(fs->fs_super)->s_root_blkno)
			continue;
		if (usr_hash) {
			err = ocfs2_find_create_quota_hash(usr_hash, di->i_uid,
							   &dquot);
			if (err)
				break;
			dquot->d_ddquot.dqb_curspace +=
				ocfs2_clusters_to_bytes(fs, di->i_clusters);
			dquot->d_ddquot.dqb_curinodes++;
		}
		if (grp_hash) {
			err = ocfs2_find_create_quota_hash(grp_hash, di->i_gid,
							   &dquot);
			if (err)
				break;
			dquot->d_ddquot.dqb_curspace +=
				ocfs2_clusters_to_bytes(fs, di->i_clusters);
			dquot->d_ddquot.dqb_curinodes++;
		}
	}
out:
	if (close_scan)
		ocfs2_close_inode_scan(scan);
	ocfs2_free(&buf);
	return err;
}

errcode_t ocfs2_init_quota_change(ocfs2_filesys *fs,
				  ocfs2_quota_hash **usrhash,
				  ocfs2_quota_hash **grphash)
{
	errcode_t err;

	*usrhash = NULL;
	*grphash = NULL;
	if (OCFS2_HAS_RO_COMPAT_FEATURE(OCFS2_RAW_SB(fs->fs_super),
					OCFS2_FEATURE_RO_COMPAT_USRQUOTA)) {
		err = ocfs2_new_quota_hash(usrhash);
		if (err)
			return err;
	}
	if (OCFS2_HAS_RO_COMPAT_FEATURE(OCFS2_RAW_SB(fs->fs_super),
					OCFS2_FEATURE_RO_COMPAT_GRPQUOTA)) {
		err = ocfs2_new_quota_hash(grphash);
		if (err) {
			if (*usrhash)
				ocfs2_free_quota_hash(*usrhash);
			return err;
		}
	}
	return 0;
}

errcode_t ocfs2_finish_quota_change(ocfs2_filesys *fs,
				    ocfs2_quota_hash *usrhash,
				    ocfs2_quota_hash *grphash)
{
	errcode_t ret = 0, err;

	if (usrhash) {
		err = ocfs2_write_release_dquots(fs, USRQUOTA, usrhash);
		if (!ret)
			ret = err;
		err = ocfs2_free_quota_hash(usrhash);
		if (!ret)
			ret = err;
	}
	if (grphash) {
		err = ocfs2_write_release_dquots(fs, GRPQUOTA, grphash);
		if (!ret)
			ret = err;
		err = ocfs2_free_quota_hash(grphash);
		if (!ret)
			ret = err;
	}

	return ret;
}

errcode_t ocfs2_apply_quota_change(ocfs2_filesys *fs,
				   ocfs2_quota_hash *usrhash,
				   ocfs2_quota_hash *grphash,
				   uid_t uid, gid_t gid,
				   int64_t space_change,
				   int64_t inode_change)
{
	ocfs2_cached_dquot *dquot;
	errcode_t err;

	if (usrhash) {
		err = ocfs2_find_read_quota_hash(fs, usrhash, USRQUOTA, uid,
						 &dquot);
		if (err)
			return err;
		dquot->d_ddquot.dqb_curspace += space_change;
		dquot->d_ddquot.dqb_curinodes += inode_change;
	}
	if (grphash) {
		err = ocfs2_find_read_quota_hash(fs, grphash, GRPQUOTA, gid,
						 &dquot);
		if (err)
			return err;
		dquot->d_ddquot.dqb_curspace += space_change;
		dquot->d_ddquot.dqb_curinodes += inode_change;
	}
	return 0;
}

errcode_t ocfs2_iterate_quota_hash(ocfs2_quota_hash *hash,
				   errcode_t (*f)(ocfs2_cached_dquot *, void *),
				   void *data)
{
	errcode_t err = 0;
	int i;
	ocfs2_cached_dquot *dquot, *next;

	for (i = 0; i < hash->alloc_entries; i++)
		for (dquot = hash->hash[i]; dquot; dquot = next) {
			next = dquot->d_next;
			err = f(dquot, data);
			if (err)
				goto out;
		}
out:
	return err;
}

struct write_rel_ctx {
	ocfs2_filesys *fs;
	ocfs2_quota_hash *hash;
	int type;
};

static errcode_t write_release_quota_hash(ocfs2_cached_dquot *dquot, void *p)
{
	struct write_rel_ctx *ctx = p;
	errcode_t err;

	if (!dquot->d_ddquot.dqb_isoftlimit ||
	    dquot->d_ddquot.dqb_curinodes < dquot->d_ddquot.dqb_isoftlimit)
		dquot->d_ddquot.dqb_itime = 0;
	if (!dquot->d_ddquot.dqb_bsoftlimit ||
	    dquot->d_ddquot.dqb_curspace < dquot->d_ddquot.dqb_bsoftlimit)
		dquot->d_ddquot.dqb_btime = 0;

	err = ocfs2_write_dquot(ctx->fs, ctx->type, dquot);
	if (err)
		return err;
	err = ocfs2_remove_quota_hash(ctx->hash, dquot);
	if (err)
		return err;
	return ocfs2_free(&dquot);
}

errcode_t ocfs2_write_release_dquots(ocfs2_filesys *fs, int type,
				     ocfs2_quota_hash *hash)
{
	struct write_rel_ctx ctx;

	ctx.fs = fs;
	ctx.hash = hash;
	ctx.type = type;

	return ocfs2_iterate_quota_hash(hash, write_release_quota_hash, &ctx);
}

static void mark_quotafile_info_dirty(ocfs2_filesys *fs, int type)
{
	fs->qinfo[type].flags |= OCFS2_QF_INFO_DIRTY;
	fs->fs_flags |= OCFS2_FLAG_DIRTY;
}

static void ocfs2_checksum_quota_block(ocfs2_filesys *fs, char *buf)
{
	struct ocfs2_disk_dqtrailer *dqt =
			ocfs2_block_dqtrailer(fs->fs_blocksize, buf);

	ocfs2_compute_meta_ecc(fs, buf, &dqt->dq_check);
}

#define OCFS2_LOCAL_QF_INIT_BLOCKS 2

errcode_t ocfs2_init_local_quota_file(ocfs2_filesys *fs, int type,
				      uint64_t blkno)
{
	ocfs2_cached_inode *ci = NULL;
	struct ocfs2_dinode *di;
	struct ocfs2_disk_dqheader *header;
	struct ocfs2_local_disk_dqinfo *info;
	unsigned int magics[] = OCFS2_LOCAL_QMAGICS;
	int versions[] = OCFS2_LOCAL_QVERSIONS;
	char *buf = NULL;
	unsigned int written;
	int bytes = ocfs2_blocks_to_bytes(fs, OCFS2_LOCAL_QF_INIT_BLOCKS);
	errcode_t err;

	err = ocfs2_read_cached_inode(fs, blkno, &ci);
	if (err)
		goto out;

	if (!(ci->ci_inode->i_flags & OCFS2_VALID_FL) ||
	    !(ci->ci_inode->i_flags & OCFS2_SYSTEM_FL) ||
	    !(ci->ci_inode->i_flags & OCFS2_QUOTA_FL)) {
		err = OCFS2_ET_INTERNAL_FAILURE;
		goto out;
	}
	di = ci->ci_inode;

	/* We need at least two blocks */
	err = ocfs2_cached_inode_extend_allocation(ci,
		ocfs2_clusters_in_blocks(fs, OCFS2_LOCAL_QF_INIT_BLOCKS));
	if (err)
		goto out;
	di->i_size = bytes;
	di->i_mtime = time(NULL);
	err = ocfs2_write_inode(fs, blkno, (char *)di);
	if (err)
		goto out;

	err = ocfs2_malloc_blocks(fs->fs_io, OCFS2_LOCAL_QF_INIT_BLOCKS, &buf);
	if (err)
		goto out;
	memset(buf, 0, bytes);

	header = (struct ocfs2_disk_dqheader *)buf;
	header->dqh_magic = magics[type];
	header->dqh_version = versions[type];
	ocfs2_swap_quota_header(header);

	info = (struct ocfs2_local_disk_dqinfo *)(buf + OCFS2_LOCAL_INFO_OFF);
	info->dqi_chunks = 1;
	info->dqi_blocks = OCFS2_LOCAL_QF_INIT_BLOCKS;
	info->dqi_flags = OLQF_CLEAN;
	ocfs2_swap_quota_local_info(info);

	/* There are no free chunks because there are no blocks allocated for
	 * them yet. So chunk header is all-zero and needs no initialization */
	ocfs2_checksum_quota_block(fs, buf);
	ocfs2_checksum_quota_block(fs, buf + fs->fs_blocksize);
	err = ocfs2_file_write(ci, buf, bytes, 0, &written);
	if (!err && written != bytes) {
		err = OCFS2_ET_INTERNAL_FAILURE;
		goto out;
	}
out:
	if (ci)
		ocfs2_free_cached_inode(fs, ci);
	if (buf)
		ocfs2_free(&buf);
	return err;
}

errcode_t ocfs2_init_local_quota_files(ocfs2_filesys *fs, int type)
{
	int num_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;
	char fname[OCFS2_MAX_FILENAME_LEN];
	errcode_t ret;
	uint64_t blkno;
	int local_type = (type == USRQUOTA) ? LOCAL_USER_QUOTA_SYSTEM_INODE :
					      LOCAL_GROUP_QUOTA_SYSTEM_INODE;
	int i;

	for (i = 0; i < num_slots; i++) {
		ocfs2_sprintf_system_inode_name(fname, sizeof(fname),
						local_type, i);
		ret = ocfs2_lookup(fs, fs->fs_sysdir_blkno, fname,
				   strlen(fname), NULL, &blkno);
		if (ret)
			return ret;
		/* This is here mainly for fsck... */
		ret = ocfs2_truncate(fs, blkno, 0);
		if (ret)
			return ret;
		ret = ocfs2_init_local_quota_file(fs, type, blkno);
		if (ret)
			return ret;
	}
	return 0;
}

/* Return depth of quota tree in global file */
int ocfs2_qtree_depth(int blocksize)
{
	unsigned int epb = (blocksize - OCFS2_QBLK_RESERVED_SPACE) >> 2;
	unsigned long long entries = epb;
	int i;

	for (i = 1; entries < (1ULL << 32); i++)
		entries *= epb;
	return i;
}

/* Returns index of next block in the tree of dquots */
static int ocfs2_qtree_index(int blocksize, qid_t id, int depth)
{
	unsigned int epb = (blocksize - OCFS2_QBLK_RESERVED_SPACE) >> 2;

	depth = ocfs2_qtree_depth(blocksize) - depth - 1;
	while (depth--)
		id /= epb;
	return id % epb;
}

/* Is given leaf entry unused? */
int ocfs2_qtree_entry_unused(struct ocfs2_global_disk_dqblk *ddquot)
{
	static struct ocfs2_global_disk_dqblk empty;

	return !memcmp(&empty, ddquot, sizeof(empty));
}

errcode_t ocfs2_init_fs_quota_info(ocfs2_filesys *fs, int type)
{
	int global_type = (type == USRQUOTA) ?
				USER_QUOTA_SYSTEM_INODE :
				GROUP_QUOTA_SYSTEM_INODE;
	uint64_t blkno;
	char fname[OCFS2_MAX_FILENAME_LEN];
	errcode_t ret;

	if (fs->qinfo[type].qi_inode)
		return 0;

	ocfs2_sprintf_system_inode_name(fname, sizeof(fname),
		global_type, 0);
	ret = ocfs2_lookup(fs, fs->fs_sysdir_blkno, fname, strlen(fname),
			   NULL, &blkno);
	if (ret)
		return ret;
	ret = ocfs2_read_cached_inode(fs, blkno, &(fs->qinfo[type].qi_inode));
	if (ret)
		return ret;
	return 0;
}

/* Read given block */
static errcode_t read_blk(ocfs2_filesys *fs, int type, unsigned int blk,
			  char *buf)
{
	errcode_t err;
	uint32_t got;
	struct ocfs2_disk_dqtrailer *dqt =
			ocfs2_block_dqtrailer(fs->fs_blocksize, buf);

	err = ocfs2_file_read(fs->qinfo[type].qi_inode, buf,
			      fs->fs_blocksize, blk * fs->fs_blocksize, &got);
	if (err)
		return err;
	if (got != fs->fs_blocksize)
		return OCFS2_ET_SHORT_READ;

	return ocfs2_validate_meta_ecc(fs, buf, &dqt->dq_check);
}

/* Write given block */
static errcode_t write_blk(ocfs2_filesys *fs, int type, unsigned int blk,
			   char *buf)
{
	errcode_t err;
	uint32_t written;

	ocfs2_checksum_quota_block(fs, buf);

	err = ocfs2_file_write(fs->qinfo[type].qi_inode, buf, fs->fs_blocksize,
			       blk * fs->fs_blocksize, &written);
	if (err)
		return err;
	if (written != fs->fs_blocksize)
		return OCFS2_ET_SHORT_WRITE;
	return 0;
}

errcode_t ocfs2_read_global_quota_info(ocfs2_filesys *fs, int type)
{
	char *buf;
	errcode_t ret;
	struct ocfs2_global_disk_dqinfo *info;

	if (fs->qinfo[type].flags & OCFS2_QF_INFO_LOADED)
		return 0;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = read_blk(fs, type, 0, buf);
	if (ret)
		return ret;
	info = (struct ocfs2_global_disk_dqinfo *)(buf + OCFS2_GLOBAL_INFO_OFF);
	ocfs2_swap_quota_global_info(info);
	memcpy(&(fs->qinfo[type].qi_info), info,
	       sizeof(struct ocfs2_global_disk_dqinfo));
	fs->qinfo[type].flags |= OCFS2_QF_INFO_LOADED;

	return 0;
}

errcode_t ocfs2_write_global_quota_info(ocfs2_filesys *fs, int type)
{
	errcode_t ret;
	char *buf;
	struct ocfs2_disk_dqheader *header;
	struct ocfs2_global_disk_dqinfo *info;
	unsigned int magics[] = OCFS2_GLOBAL_QMAGICS;
	int versions[] = OCFS2_GLOBAL_QVERSIONS;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;
	header = (struct ocfs2_disk_dqheader *)buf;
	header->dqh_magic = magics[type];
	header->dqh_version = versions[type];
	ocfs2_swap_quota_header(header);

	info = (struct ocfs2_global_disk_dqinfo *)(buf + OCFS2_GLOBAL_INFO_OFF);
	memcpy(info, &(fs->qinfo[type].qi_info),
	       sizeof(struct ocfs2_global_disk_dqinfo));
	ocfs2_swap_quota_global_info(info);
	ret = write_blk(fs, type, 0, buf);
	if (ret)
		goto bail;
bail:
	ocfs2_free(&buf);
	return ret;
}

errcode_t ocfs2_load_fs_quota_info(ocfs2_filesys *fs)
{
	errcode_t err;

	if (OCFS2_HAS_RO_COMPAT_FEATURE(OCFS2_RAW_SB(fs->fs_super),
					OCFS2_FEATURE_RO_COMPAT_USRQUOTA)) {
		err = ocfs2_init_fs_quota_info(fs, USRQUOTA);
		if (err)
			return err;
		err = ocfs2_read_global_quota_info(fs, USRQUOTA);
		if (err)
			return err;
	}
	if (OCFS2_HAS_RO_COMPAT_FEATURE(OCFS2_RAW_SB(fs->fs_super),
					OCFS2_FEATURE_RO_COMPAT_GRPQUOTA)) {
		err = ocfs2_init_fs_quota_info(fs, GRPQUOTA);
		if (err)
			return err;
		err = ocfs2_read_global_quota_info(fs, GRPQUOTA);
		if (err)
			return err;
	}
	return 0;
}

#define OCFS2_GLOBAL_QF_INIT_BLOCKS 2

errcode_t ocfs2_init_global_quota_file(ocfs2_filesys *fs, int type)
{
	ocfs2_cached_inode *ci = fs->qinfo[type].qi_inode;
	struct ocfs2_dinode *di;
	char *buf = NULL;
	struct ocfs2_disk_dqheader *header;
	struct ocfs2_global_disk_dqinfo *info;
	unsigned int magics[] = OCFS2_GLOBAL_QMAGICS;
	int versions[] = OCFS2_GLOBAL_QVERSIONS;
	errcode_t err;
	int i;
	int bytes = ocfs2_blocks_to_bytes(fs, OCFS2_GLOBAL_QF_INIT_BLOCKS);

	if (!(ci->ci_inode->i_flags & OCFS2_VALID_FL) ||
	    !(ci->ci_inode->i_flags & OCFS2_SYSTEM_FL) ||
	    !(ci->ci_inode->i_flags & OCFS2_QUOTA_FL)) {
		err = OCFS2_ET_INTERNAL_FAILURE;
		goto out;
	}
	err = ocfs2_cached_inode_extend_allocation(ci,
		ocfs2_clusters_in_blocks(fs, OCFS2_GLOBAL_QF_INIT_BLOCKS));
	if (err)
		goto out;

	/* Mark info dirty so that quota inode gets written */
	mark_quotafile_info_dirty(fs, type);

	di = ci->ci_inode;
	di->i_size = bytes;
	di->i_mtime = time(NULL);

	err = ocfs2_malloc_blocks(fs->fs_io, OCFS2_GLOBAL_QF_INIT_BLOCKS,
				  &buf);
	if (err)
		goto out;
	memset(buf, 0, bytes);

	header = (struct ocfs2_disk_dqheader *)buf;
	header->dqh_magic = magics[type];
	header->dqh_version = versions[type];
	ocfs2_swap_quota_header(header);

	fs->qinfo[type].qi_info.dqi_blocks = OCFS2_GLOBAL_QF_INIT_BLOCKS;
	fs->qinfo[type].qi_info.dqi_free_blk = 0;
	fs->qinfo[type].qi_info.dqi_free_entry = 0;

	info = (struct ocfs2_global_disk_dqinfo *)(buf + OCFS2_GLOBAL_INFO_OFF);
	info->dqi_bgrace = fs->qinfo[type].qi_info.dqi_bgrace;
	info->dqi_igrace = fs->qinfo[type].qi_info.dqi_igrace;
	info->dqi_syncms = fs->qinfo[type].qi_info.dqi_syncms;
	info->dqi_blocks = OCFS2_GLOBAL_QF_INIT_BLOCKS;
	info->dqi_free_blk = 0;
	info->dqi_free_entry = 0;
	ocfs2_swap_quota_global_info(info);

	/*
	 * Write the buffer here so that all the headers are properly written.
	 * Normally we don't write tree root block.
	 */
	for (i = 0; i < OCFS2_GLOBAL_QF_INIT_BLOCKS; i++) {
		err = write_blk(fs, type, i, buf + (i * fs->fs_blocksize));
		if (err)
			goto out;
	}
out:
	if (buf)
		ocfs2_free(&buf);
	return err;
}

/* Is given dquot empty? */
static int ocfs2_global_entry_unused(struct ocfs2_global_disk_dqblk *ddqblk)
{
	static struct ocfs2_global_disk_dqblk empty;

	return !memcmp(&empty, ddqblk, sizeof(empty));
}

/* Get free block in file (either from free list or create new one) */
static errcode_t ocfs2_get_free_dqblk(ocfs2_filesys *fs, int type,
				      unsigned int *blk)
{
	errcode_t err;
	char *buf;
	struct qt_disk_dqdbheader *dh;
	struct ocfs2_global_disk_dqinfo *info = &(fs->qinfo[type].qi_info);
	ocfs2_cached_inode *ci = fs->qinfo[type].qi_inode;

	err = ocfs2_malloc_block(fs->fs_io, &buf);
	if (err)
		return err;
	dh = (struct qt_disk_dqdbheader *)buf;
	if (info->dqi_free_blk) {
		*blk = info->dqi_free_blk;
		err = read_blk(fs, type, *blk, buf);
		if (err)
			goto bail;
		info->dqi_free_blk = le32_to_cpu(dh->dqdh_next_free);
	}
	else {
		if (info->dqi_blocks ==
		    ocfs2_clusters_to_blocks(fs, ci->ci_inode->i_clusters)) {
			err = ocfs2_cached_inode_extend_allocation(ci, 1);
			if (err)
				goto bail;
		}
		*blk = info->dqi_blocks++;
		ci->ci_inode->i_size =
				ocfs2_blocks_to_bytes(fs, info->dqi_blocks);
	}
	mark_quotafile_info_dirty(fs, type);
bail:
	ocfs2_free(&buf);
	return err;
}

/* Put given block to free list */
static errcode_t ocfs2_put_free_dqblk(ocfs2_filesys *fs, int type,
				      char *buf, unsigned int blk)
{
	errcode_t err;
	struct qt_disk_dqdbheader *dh = (struct qt_disk_dqdbheader *)buf;
	struct ocfs2_global_disk_dqinfo *info = &(fs->qinfo[type].qi_info);

	dh->dqdh_next_free = info->dqi_free_blk;
	dh->dqdh_prev_free = 0;
	dh->dqdh_entries = 0;
	ocfs2_swap_quota_leaf_block_header(dh);
	err = write_blk(fs, type, blk, buf);
	ocfs2_swap_quota_leaf_block_header(dh);
	if (err)
		return err;
	info->dqi_free_blk = blk;
	mark_quotafile_info_dirty(fs, type);
	return 0;
}

/* Remove given block from the list of blocks with free entries */
static errcode_t ocfs2_remove_free_dqentry(ocfs2_filesys *fs, int type,
					   char *buf, unsigned int blk)
{
	errcode_t err;
	char *tmpbuf;
	struct qt_disk_dqdbheader *dh, *tdh;
	unsigned int nextblk, prevblk;

	err = ocfs2_malloc_block(fs->fs_io, &tmpbuf);
	if (err)
		return err;
	dh = (struct qt_disk_dqdbheader *)buf;
	tdh = (struct qt_disk_dqdbheader *)tmpbuf;
	nextblk = dh->dqdh_next_free;
	prevblk = dh->dqdh_prev_free;

	if (nextblk) {
		err = read_blk(fs, type, nextblk, tmpbuf);
		if (err)
			goto bail;
		ocfs2_swap_quota_leaf_block_header(tdh);
		tdh->dqdh_prev_free = prevblk;
		ocfs2_swap_quota_leaf_block_header(tdh);
		err = write_blk(fs, type, nextblk, tmpbuf);
		if (err)
			goto bail;
	}
	if (prevblk) {
		/* Failure here is bad since we potentially corrupt free list.
		 * OTOH something must be really wrong when read/write fails */
		err = read_blk(fs, type, prevblk, tmpbuf);
		if (err)
			goto bail;
		ocfs2_swap_quota_leaf_block_header(tdh);
		tdh->dqdh_next_free = nextblk;
		ocfs2_swap_quota_leaf_block_header(tdh);
		err = write_blk(fs, type, prevblk, tmpbuf);
		if (err)
			goto bail;
	}
	else {
		fs->qinfo[type].qi_info.dqi_free_entry = nextblk;
		mark_quotafile_info_dirty(fs, type);
	}
	dh->dqdh_next_free = dh->dqdh_prev_free = 0;
	ocfs2_swap_quota_leaf_block_header(dh);
	/* No matter whether write succeeds block is out of list */
	write_blk(fs, type, blk, buf);
	ocfs2_swap_quota_leaf_block_header(dh);
bail:
	ocfs2_free(&tmpbuf);
	return err;
}

/* Insert given block to the beginning of list with free entries */
static errcode_t ocfs2_insert_free_dqentry(ocfs2_filesys *fs, int type,
					   char *buf, unsigned int blk)
{
	errcode_t err;
	char *tmpbuf;
	struct qt_disk_dqdbheader *tdh, *dh = (struct qt_disk_dqdbheader *)buf;
	struct ocfs2_global_disk_dqinfo *info = &(fs->qinfo[type].qi_info);

	err = ocfs2_malloc_block(fs->fs_io, &tmpbuf);
	if (err)
		return err;
	dh->dqdh_next_free = info->dqi_free_entry;
	dh->dqdh_prev_free = 0;
	ocfs2_swap_quota_leaf_block_header(dh);
	err = write_blk(fs, type, blk, buf);
	ocfs2_swap_quota_leaf_block_header(dh);
	if (err)
		goto bail;

	if (info->dqi_free_entry) {
		tdh = (struct qt_disk_dqdbheader *)tmpbuf;
		err = read_blk(fs, type, info->dqi_free_entry, tmpbuf);
		if (err)
			goto bail;
		ocfs2_swap_quota_leaf_block_header(tdh);
		tdh->dqdh_prev_free = blk;
		ocfs2_swap_quota_leaf_block_header(tdh);
		err = write_blk(fs, type, info->dqi_free_entry, tmpbuf);
		if (err)
			goto bail;
	}
	info->dqi_free_entry = blk;
	mark_quotafile_info_dirty(fs, type);
bail:
	ocfs2_free(&tmpbuf);
	return err;
}

/* Find space for dquot */
static errcode_t ocfs2_find_free_dqentry(ocfs2_filesys *fs, int type,
					 unsigned int *treeblk, loff_t *off)
{
	errcode_t err;
	unsigned int blk, i;
	struct ocfs2_global_disk_dqblk *ddquot;
	struct qt_disk_dqdbheader *dh;
	struct ocfs2_global_disk_dqinfo *info = &(fs->qinfo[type].qi_info);
	char *buf;

	err = ocfs2_malloc_block(fs->fs_io, &buf);
	if (err)
		return err;
	dh = (struct qt_disk_dqdbheader *)buf;
	ddquot = (struct ocfs2_global_disk_dqblk *)(buf +
		 sizeof(struct qt_disk_dqdbheader));
	if (info->dqi_free_entry) {
		blk = info->dqi_free_entry;
		err = read_blk(fs, type, blk, buf);
		if (err)
			goto bail;
		ocfs2_swap_quota_leaf_block_header(dh);
	}
	else {
		err = ocfs2_get_free_dqblk(fs, type, &blk);
		if (err)
			goto bail;
		memset(buf, 0, fs->fs_blocksize);
		info->dqi_free_entry = blk;
		mark_quotafile_info_dirty(fs, type);
	}
	/* Block will be full? */
	if (dh->dqdh_entries + 1 >=
	    ocfs2_global_dqstr_in_blk(fs->fs_blocksize)) {
		err = ocfs2_remove_free_dqentry(fs, type, buf, blk);
		if (err)
			goto bail;
	}
	dh->dqdh_entries++;
	/* Find free structure in block */
	for (i = 0;
	     i < ocfs2_global_dqstr_in_blk(fs->fs_blocksize) &&
	     !ocfs2_global_entry_unused(ddquot + i);
	     i++);
	if (i == ocfs2_global_dqstr_in_blk(fs->fs_blocksize)) {
		err = OCFS2_ET_CORRUPT_QUOTA_FILE;
		goto bail;
	}
	ocfs2_swap_quota_leaf_block_header(dh);
	err = write_blk(fs, type, blk, buf);
	if (err)
		goto bail;
	*off = (blk * fs->fs_blocksize) + sizeof(struct qt_disk_dqdbheader) +
	       i * sizeof(struct ocfs2_global_disk_dqblk);
	*treeblk = blk;
bail:
	ocfs2_free(&buf);
	return err;
}

/* Insert reference to structure into the trie */
static errcode_t ocfs2_do_insert_tree(ocfs2_filesys *fs, int type, qid_t id,
				      unsigned int *treeblk, int depth,
				      loff_t *off)
{
	char *buf;
	int newson = 0, newact = 0;
	u_int32_t *ref;
	unsigned int newblk;
	errcode_t err;

	err = ocfs2_malloc_block(fs->fs_io, &buf);
	if (err)
		return err;
	if (!*treeblk) {
		err = ocfs2_get_free_dqblk(fs, type, &newblk);
		if (err)
			goto bail;
		*treeblk = newblk;
		memset(buf, 0, fs->fs_blocksize);
		newact = 1;
	}
	else {
		err = read_blk(fs, type, *treeblk, buf);
		if (err)
			goto bail;
	}
	ref = (u_int32_t *) buf;
	newblk = le32_to_cpu(ref[
		 ocfs2_qtree_index(fs->fs_blocksize, id, depth)]);
	if (!newblk)
		newson = 1;
	if (depth == ocfs2_qtree_depth(fs->fs_blocksize) - 1) {
		if (newblk) {
			err = OCFS2_ET_CORRUPT_QUOTA_FILE;
			goto bail;
		}
		err = ocfs2_find_free_dqentry(fs, type, &newblk, off);
	}
	else
		err = ocfs2_do_insert_tree(fs, type, id, &newblk, depth + 1,
					   off);
	if (newson && !err) {
		ref[ocfs2_qtree_index(fs->fs_blocksize, id, depth)] =
							cpu_to_le32(newblk);
		err = write_blk(fs, type, *treeblk, buf);
	}
	else if (newact && err)
		ocfs2_put_free_dqblk(fs, type, buf, *treeblk);
bail:
	ocfs2_free(&buf);
	return err;
}

/* Wrapper for inserting quota structure into tree */
static errcode_t ocfs2_insert_qtree(ocfs2_filesys *fs, int type, qid_t id,
				    loff_t *off)
{
	unsigned int tmp = QT_TREEOFF;

	return ocfs2_do_insert_tree(fs, type, id, &tmp, 0, off);
}

/* Write dquot to file */
errcode_t ocfs2_write_dquot(ocfs2_filesys *fs, int type,
			    ocfs2_cached_dquot *dquot)
{
	errcode_t err;
	char *buf;
	struct ocfs2_global_disk_dqblk *ddquot;

	err = ocfs2_malloc_block(fs->fs_io, &buf);
	if (err)
		return err;

	if (!dquot->d_off) {
		err = ocfs2_insert_qtree(fs, type, dquot->d_ddquot.dqb_id,
					 &dquot->d_off);
		if (err)
			goto bail;
	}
	err = read_blk(fs, type, dquot->d_off / fs->fs_blocksize, buf);
	if (err)
		goto bail;
	ddquot = (struct ocfs2_global_disk_dqblk *)(buf +
					(dquot->d_off % fs->fs_blocksize));
	memcpy(ddquot, &dquot->d_ddquot,
	       sizeof(struct ocfs2_global_disk_dqblk));
	ddquot->dqb_pad1 = 0;
	ddquot->dqb_pad2 = 0;
	ocfs2_swap_quota_global_dqblk(ddquot);
	err = write_blk(fs, type, dquot->d_off / fs->fs_blocksize, buf);
bail:
	ocfs2_free(&buf);
	return err;
}

/* Remove dquot entry from its data block */
static errcode_t ocfs2_remove_leaf_dqentry(ocfs2_filesys *fs,
					   int type,
					   ocfs2_cached_dquot *dquot,
					   unsigned int blk)
{
	errcode_t err;
	char *buf;
	struct qt_disk_dqdbheader *dh;

	if (blk != dquot->d_off / fs->fs_blocksize)
		return OCFS2_ET_CORRUPT_QUOTA_FILE;

	err = ocfs2_malloc_block(fs->fs_io, &buf);
	if (err)
		return err;

	err = read_blk(fs, type, blk, buf);
	if (err)
		goto bail;

	dh = (struct qt_disk_dqdbheader *)buf;
	ocfs2_swap_quota_leaf_block_header(dh);
	dh->dqdh_entries--;
	if (!dh->dqdh_entries) {	/* Block got free? */
		err = ocfs2_remove_free_dqentry(fs, type, buf, blk);
		if (err)
			goto bail;
		err = ocfs2_put_free_dqblk(fs, type, buf, blk);
		if (err)
			goto bail;
	}
	else {
		memset(buf + (dquot->d_off & (fs->fs_blocksize - 1)), 0,
		       sizeof(struct ocfs2_global_disk_dqblk));

		/* First free entry? */
		if (dh->dqdh_entries ==
		    ocfs2_global_dqstr_in_blk(fs->fs_blocksize) - 1) {
			/* This will also write data block */
			err = ocfs2_insert_free_dqentry(fs, type, buf, blk);
		}
		else
			err = write_blk(fs, type, blk, buf);
	}
	dquot->d_off = 0;
bail:
	ocfs2_free(&buf);

	return err;
}

/* Remove reference to dquot from tree */
static errcode_t ocfs2_remove_tree_dqentry(ocfs2_filesys *fs,
					   int type,
					   ocfs2_cached_dquot *dquot,
					   unsigned int *blk,
					   int depth)
{
	errcode_t err;
	char *buf;
	unsigned int newblk;
	u_int32_t *ref;

	err = ocfs2_malloc_block(fs->fs_io, &buf);
	if (err)
		return err;

	err = read_blk(fs, type, *blk, buf);
	if (err)
		goto bail;

	ref = (u_int32_t *)buf;
	newblk = le32_to_cpu(ref[ocfs2_qtree_index(fs->fs_blocksize,
		 dquot->d_ddquot.dqb_id, depth)]);
	if (depth == ocfs2_qtree_depth(fs->fs_blocksize) - 1) {
		err = ocfs2_remove_leaf_dqentry(fs, type, dquot, newblk);
		newblk = 0;
	}
	else
		err = ocfs2_remove_tree_dqentry(fs, type, dquot, &newblk,
						depth + 1);
	if (err)
		goto bail;

	if (!newblk) {
		int i;

		ref[ocfs2_qtree_index(fs->fs_blocksize,
				      dquot->d_ddquot.dqb_id,
				      depth)] = cpu_to_le32(0);
		/* Block got empty? */
		for (i = 0; i < fs->fs_blocksize - OCFS2_QBLK_RESERVED_SPACE &&
		     !buf[i]; i++);
		/* Don't put the root block into the free block list */
		if (i == fs->fs_blocksize - OCFS2_QBLK_RESERVED_SPACE &&
		    *blk != QT_TREEOFF) {
			err = ocfs2_put_free_dqblk(fs, type, buf, *blk);
			if (err)
				goto bail;
			*blk = 0;
		}
		else
			err = write_blk(fs, type, *blk, buf);
	}
bail:
	ocfs2_free(&buf);

	return err;
}

/* Delete dquot from tree */
errcode_t ocfs2_delete_dquot(ocfs2_filesys *fs, int type,
			     ocfs2_cached_dquot *dquot)
{
	unsigned int tmp = QT_TREEOFF;

	if (!dquot->d_off)	/* Even not allocated? */
		return 0;
	return ocfs2_remove_tree_dqentry(fs, type, dquot, &tmp, 0);
}

/* Find entry in block */
static errcode_t ocfs2_find_block_dqentry(ocfs2_filesys *fs, int type,
					  ocfs2_cached_dquot *dquot,
					  unsigned int blk)
{
	char *buf;
	errcode_t err;
	int i;
	struct ocfs2_global_disk_dqblk *ddquot;

	err = ocfs2_malloc_block(fs->fs_io, &buf);
	if (err)
		return err;

	err = read_blk(fs, type, blk, buf);
	if (err)
		goto bail;

	ddquot = (struct ocfs2_global_disk_dqblk *)(buf +
		 sizeof(struct qt_disk_dqdbheader));

	for (i = 0; i < ocfs2_global_dqstr_in_blk(fs->fs_blocksize);
	     i++, ddquot++) {
		if (le32_to_cpu(ddquot->dqb_id) == dquot->d_ddquot.dqb_id) {
			if (dquot->d_ddquot.dqb_id == 0 &&
			    ocfs2_qtree_entry_unused(ddquot))
				continue;
			break;
		}
	}
	if (i == ocfs2_global_dqstr_in_blk(fs->fs_blocksize)) {
		err = OCFS2_ET_CORRUPT_QUOTA_FILE;
		goto bail;
	}
	dquot->d_off = blk * fs->fs_blocksize + ((char *)ddquot - buf);
	memcpy(&dquot->d_ddquot, ddquot,
	       sizeof(struct ocfs2_global_disk_dqblk));
	ocfs2_swap_quota_global_dqblk(&dquot->d_ddquot);
bail:
	ocfs2_free(&buf);
	return err;
}

/* Find entry for given id in the tree */
static errcode_t ocfs2_find_tree_dqentry(ocfs2_filesys *fs,
					 int type,
					 ocfs2_cached_dquot *dquot,
					 unsigned int blk,
					 int depth)
{
	errcode_t err;
	char *buf;
	u_int32_t *ref;

	err = ocfs2_malloc_block(fs->fs_io, &buf);
	if (err)
		return err;

	err = read_blk(fs, type, blk, buf);
	if (err)
		goto bail;
	ref = (u_int32_t *)buf;
	blk = le32_to_cpu(ref[ocfs2_qtree_index(fs->fs_blocksize,
	      dquot->d_ddquot.dqb_id, depth)]);
	if (!blk)		/* No reference? */
		goto bail;
	if (depth < ocfs2_qtree_depth(fs->fs_blocksize) - 1)
		err = ocfs2_find_tree_dqentry(fs, type, dquot, blk, depth + 1);
	else
		err = ocfs2_find_block_dqentry(fs, type, dquot, blk);
bail:
	ocfs2_free(&buf);
	return err;
}

/*
 *  Read dquot from disk
 */
errcode_t ocfs2_read_dquot(ocfs2_filesys *fs, int type, qid_t id,
			   ocfs2_cached_dquot **ret_dquot)
{
	errcode_t err;
	ocfs2_cached_dquot *dquot;

	err = ocfs2_malloc0(sizeof(ocfs2_cached_dquot), &dquot);
	if (err)
		return err;

	dquot->d_ddquot.dqb_id = id;
	err = ocfs2_find_tree_dqentry(fs, type, dquot, QT_TREEOFF, 0);
	if (err)
		goto bail;
	*ret_dquot = dquot;
	return 0;
bail:
	ocfs2_free(&dquot);
	return err;
}
