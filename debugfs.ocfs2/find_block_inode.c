/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * find_block_inode.c
 *
 * Take a block number and returns the owning inode number.
 *
 * Copyright (C) 2006 Oracle.  All rights reserved.
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
 */

#include <main.h>

extern dbgfs_gbls gbls;

struct block_array {
	uint64_t blkno;
	uint32_t inode;		/* Backing inode# */
	uint64_t offset;
	int data;		/* offset is valid if this is set */
#define STATUS_UNKNOWN	0
#define STATUS_USED	1
#define STATUS_FREE	2
	int status;
};

static errcode_t lookup_regular(ocfs2_filesys *fs, uint64_t inode,
				struct ocfs2_extent_list *el,
				struct block_array *ba, int count, int *found)
{
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_rec *rec;
	errcode_t ret = 0;
	char *buf = NULL;
	int i;
	int j;
	uint64_t numblks;

	if (*found >= count)
		return 0;

	ret = ocfs2_malloc_block(gbls.fs->fs_io, &buf);
	if (ret) {
		com_err(gbls.cmd, ret, "while allocating a block");
		goto bail;
	}

	eb = (struct ocfs2_extent_block *)buf;

	for (i = 0; i < el->l_next_free_rec; ++i) {
		rec = &(el->l_recs[i]);
		if (el->l_tree_depth) {
			ret = ocfs2_read_extent_block(fs, rec->e_blkno, buf);
			if (ret) {
				com_err(gbls.cmd, ret, "while reading extent "
					"block %"PRIu64, rec->e_blkno);
				goto bail;
			}

			for (j = 0; j < count; ++j) {
				if (ba[j].status != STATUS_UNKNOWN)
					continue;
				if (ba[j].blkno == rec->e_blkno) {
					ba[j].status = STATUS_USED;
					ba[j].inode = inode;
					(*found)++;
				}
			}

			lookup_regular(fs, inode, &(eb->h_list), ba, count,
				       found);

			continue;
		}

		for (j = 0; j < count; ++j) {
			if (ba[j].status != STATUS_UNKNOWN)
				continue;

			numblks = ocfs2_clusters_to_blocks(fs, rec->e_clusters);

			if (ba[j].blkno >= rec->e_blkno &&
			    ba[j].blkno < rec->e_blkno + numblks) {
				ba[j].status = STATUS_USED;
				ba[j].inode = inode;
				ba[j].data = 1;
				ba[j].offset = ocfs2_clusters_to_blocks(fs, rec->e_cpos);
				ba[j].offset += ba[j].blkno - rec->e_blkno;
				(*found)++;
			}
		}

		if (*found >= count)
			return 0;
	}
bail:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

struct walk_it {
	char *buf;
	struct block_array *ba;
	uint64_t inode;
	int count;
	int found;
};

static int walk_chain_func(ocfs2_filesys *fs, uint64_t blkno, int chain,
			   void *priv_data)
{
	struct walk_it *wi = (struct walk_it *)priv_data;
	struct ocfs2_group_desc *gd;
	int i;
	errcode_t ret;

	ret = ocfs2_read_group_desc(fs, blkno, wi->buf);
	if (ret) {
		com_err(gbls.cmd, ret, "while reading group %"PRIu64, blkno);
		return ret;
	}

	gd = (struct ocfs2_group_desc *)wi->buf;

	for (i = 0; i < wi->count; ++i)
	{
		if (wi->ba[i].status != STATUS_UNKNOWN)
			continue;

		if (wi->ba[i].blkno == gd->bg_blkno) {
			wi->ba[i].status = STATUS_USED;
			wi->ba[i].inode = wi->inode;
			wi->found++;
		}

		if (wi->found >= wi->count)
			break;
	}

	return 0;
}

static errcode_t lookup_chain(ocfs2_filesys *fs, struct ocfs2_dinode *di,
			      struct block_array *ba, int count, int *found)
{
	struct walk_it wi;
	errcode_t ret = 0;

	memset(&wi, 0, sizeof(wi));
	wi.ba = ba;
	wi.inode = di->i_blkno;
	wi.count = count;
	wi.found = *found;

	ret = ocfs2_malloc_block(fs->fs_io, &wi.buf);
	if (ret) {
		com_err(gbls.cmd, ret, "while allocating a block");
		goto bail;
	}

	ret = ocfs2_chain_iterate(fs, di->i_blkno, walk_chain_func, &wi);
	if (ret) {
		com_err(gbls.cmd, ret, "while walking extents");
		goto bail;
	}

	*found = wi.found;

bail:
	if (wi.buf)
		ocfs2_free(&wi.buf);

	return ret;
}

static errcode_t lookup_global_bitmap(ocfs2_filesys *fs, uint64_t *blkno)
{
	char sysfile[50];
	errcode_t ret = 0;

	snprintf(sysfile, sizeof(sysfile),
		 ocfs2_system_inodes[GLOBAL_BITMAP_SYSTEM_INODE].si_name);

	ret = ocfs2_lookup(fs, fs->fs_sysdir_blkno, sysfile,
			   strlen(sysfile), NULL, blkno);
	if (ret)
		com_err(gbls.cmd, ret, "while looking up global bitmap");

	return ret;
}

static errcode_t scan_bitmap(ocfs2_filesys *fs, uint64_t bm_blkno,
			     struct block_array *ba, int count, int *found)
{
	ocfs2_cached_inode *ci = NULL;
	uint32_t num_cluster;
	errcode_t ret = 0;
	int set;
	int i;

	ret = ocfs2_read_cached_inode(fs, bm_blkno, &ci);
	if (ret) {
		com_err(gbls.cmd, ret, "while reading inode %"PRIu64, bm_blkno);
		goto bail;
	}

	ret = ocfs2_load_chain_allocator(fs, ci);
	if (ret) {
		com_err(gbls.cmd, ret, "while loading chain allocator");
		goto bail;
	}

	for (i = 0; i < count; ++i) {
		if (ba[i].status != STATUS_UNKNOWN)
			continue;

		num_cluster = ocfs2_blocks_to_clusters(fs, ba[i].blkno);

		ret = ocfs2_bitmap_test(ci->ci_chains, (uint64_t)num_cluster,
					&set);
		if (ret) {
			com_err(gbls.cmd, ret, "while looking up global bitmap");
			goto bail;
		}

		if (!set) {
			ba[i].status = STATUS_FREE;
			(*found)++;
		}
	}

bail:
	if (ci)
		ocfs2_free_cached_inode(fs, ci);

	return ret;
}

static void check_computed_blocks(ocfs2_filesys *fs, uint64_t gb_blkno,
				  struct block_array *ba, int count, int *found)
{
	uint64_t blks_in_superzone;
	uint32_t cpg;
	uint32_t bpg;
	uint64_t blkoff;
	uint32_t blks_in_cluster = ocfs2_clusters_to_blocks(fs, 1);
	int i;

	/* Blocks in the superblock zone */
	blks_in_superzone = ocfs2_clusters_to_blocks(fs, 1);
	if (blks_in_superzone < OCFS2_SUPER_BLOCK_BLKNO)
		blks_in_superzone = OCFS2_SUPER_BLOCK_BLKNO;

	for (i = 0; i < count; ++i) {
		if (ba[i].blkno <= blks_in_superzone) {
			ba[i].status = STATUS_USED;
			ba[i].inode = OCFS2_SUPER_BLOCK_BLKNO;
			(*found)++;
		}

		if (ba[i].blkno >= fs->fs_first_cg_blkno &&
		    ba[i].blkno < (fs->fs_first_cg_blkno + blks_in_cluster)) {
			ba[i].status = STATUS_USED;
			ba[i].inode = gb_blkno;
			(*found)++;
		}
	}

	if (*found >= count)
		return;

	cpg = ocfs2_group_bitmap_size(1 << fs->fs_blocksize) * 8;
	bpg = ocfs2_clusters_to_blocks(fs, cpg);

	for (i = 0; i < count; ++i) {
		for (blkoff = bpg; blkoff < fs->fs_blocks; blkoff += bpg) {
			if (ba[i].blkno >= blkoff &&
			    ba[i].blkno < (blkoff + blks_in_cluster)) {
				ba[i].status = STATUS_USED;
				ba[i].inode = gb_blkno;
				(*found)++;
			}
		}
	}

	return;
}

errcode_t find_block_inode(ocfs2_filesys *fs, uint64_t *blkno, int count,
			   FILE *out)
{
	errcode_t ret = 0;
	uint64_t inode_num;
	struct block_array *ba = NULL;
	char *buf = NULL;
	struct ocfs2_dinode *di;
	ocfs2_inode_scan *scan = NULL;
	int i;
	int found = 0;
	uint64_t gb_blkno;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret) {
		com_err(gbls.cmd, ret, "while allocating a block");
		goto out;
	}
	di = (struct ocfs2_dinode *)buf;

	ba = calloc(count, sizeof(struct block_array));
	if (!ba) {
		com_err(gbls.cmd, ret, "while allocating memory");
		goto out;
	}

	for (i = 0; i < count; ++i)
		ba[i].blkno = blkno[i];

	if (found >= count)
		goto output;

	ret = lookup_global_bitmap(fs, &gb_blkno);
	if (ret)
		goto out_free;

	check_computed_blocks(fs, gb_blkno, ba, count, &found);
	if (found >= count)
		goto output;

	ret = scan_bitmap(fs, gb_blkno, ba, count, &found);
	if (ret)
		goto out_free;

	if (found >= count)
		goto output;

	ret = ocfs2_open_inode_scan(fs, &scan);
	if (ret) {
		com_err(gbls.cmd, ret, "while opening inode scan");
		goto out_free;
	}

	for (;;) {
		ret = ocfs2_get_next_inode(scan, &inode_num, (char *)di);
		if (ret) {
			com_err(gbls.cmd, ret, "while scanning next inode");
			goto out_close_scan;
		}

		if (!inode_num)
			break;

		if (memcmp(di->i_signature, OCFS2_INODE_SIGNATURE,
			   strlen(OCFS2_INODE_SIGNATURE)))
			continue;

		if (!(di->i_flags & OCFS2_VALID_FL))
			continue;

		for (i = 0; i < count; ++i) {
			if (ba[i].status != STATUS_UNKNOWN)
				continue;
			if (ba[i].blkno == di->i_blkno) {
				ba[i].status = STATUS_USED;
				ba[i].inode = di->i_blkno;
				found++;
			}
		}

		if (found >= count)
			break;

		if (S_ISLNK(di->i_mode) && !di->i_clusters)
			continue;

		if (di->i_flags & (OCFS2_LOCAL_ALLOC_FL | OCFS2_DEALLOC_FL))
			continue;

		if (di->i_blkno == gb_blkno)
			continue;

		if (di->i_flags & OCFS2_CHAIN_FL)
			ret = lookup_chain(fs, di, ba, count, &found);
		else
			ret = lookup_regular(fs, di->i_blkno, &(di->id2.i_list),
					     ba, count, &found);
		if (ret)
			goto out_close_scan;

		if (found >= count)
			break;
	}

output:
	for (i = 0; i < count; ++i)
		dump_icheck(out, (i == 0), ba[i].blkno, ba[i].inode,
			    ba[i].data, ba[i].offset, ba[i].status);

out_close_scan:
	if (scan)
		ocfs2_close_inode_scan(scan);

out_free:
	if (buf)
		ocfs2_free(&buf);
	if (ba)
		free(ba);
out:
	return 0;
}
