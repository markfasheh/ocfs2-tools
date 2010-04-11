/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * op_list_sparse_files.c
 *
 * ocfs2 tune utility to list sparse files.
 *
 * Copyright (C) 2004, 2008 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include <assert.h>

#include "ocfs2/ocfs2.h"
#include "ocfs2/kernel-rbtree.h"

#include "libocfs2ne.h"


struct multi_link_file {
	struct rb_node br_node;
	uint64_t blkno;
	uint32_t clusters;
};

struct list_ctxt {
	ocfs2_filesys *fs;
	uint32_t total_clusters;
	char file_name[OCFS2_MAX_FILENAME_LEN];
	int file_name_len;
	uint64_t ino;
	uint32_t file_hole_len;
	int duplicated;
	void (*func)(void *priv_data,
		     uint32_t hole_start,
		     uint32_t hole_len);
	struct rb_root multi_link_files;
};


static void inline empty_multi_link_files(struct list_ctxt *ctxt)
{
	struct multi_link_file *lf;
	struct rb_node *node;

	while ((node = rb_first(&ctxt->multi_link_files)) != NULL) {
		lf = rb_entry(node, struct multi_link_file, br_node);

		rb_erase(&lf->br_node, &ctxt->multi_link_files);
		ocfs2_free(&lf);
	}
}

static struct multi_link_file *multi_link_file_lookup(struct list_ctxt *ctxt,
						      uint64_t blkno)
{
	struct rb_node *p = ctxt->multi_link_files.rb_node;
	struct multi_link_file *file;

	while (p) {
		file = rb_entry(p, struct multi_link_file, br_node);
		if (blkno < file->blkno) {
			p = p->rb_left;
		} else if (blkno > file->blkno) {
			p = p->rb_right;
		} else
			return file;
	}

	return NULL;
}

static errcode_t multi_link_file_insert(struct list_ctxt *ctxt,
					uint64_t blkno, uint32_t clusters)
{
	errcode_t ret;
	struct multi_link_file *file = NULL;
	struct rb_node **p = &ctxt->multi_link_files.rb_node;
	struct rb_node *parent = NULL;

	while (*p) {
		parent = *p;
		file = rb_entry(parent, struct multi_link_file, br_node);
		if (blkno < file->blkno) {
			p = &(*p)->rb_left;
			file = NULL;
		} else if (blkno > file->blkno) {
			p = &(*p)->rb_right;
			file = NULL;
		} else
			assert(0);  /* We shouldn't find it. */
	}

	ret = ocfs2_malloc0(sizeof(struct multi_link_file), &file);
	if (ret)
		goto out;

	file->blkno = blkno;
	file->clusters = clusters;

	rb_link_node(&file->br_node, parent, p);
	rb_insert_color(&file->br_node, &ctxt->multi_link_files);
	ret = 0;

out:
	return ret;
}

static errcode_t get_total_free_clusters(ocfs2_filesys *fs, uint32_t *clusters)
{
	errcode_t ret;
	uint64_t blkno;
	char *buf = NULL;
	struct ocfs2_dinode *di = NULL;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		goto bail;

	ret = ocfs2_lookup_system_inode(fs, GLOBAL_BITMAP_SYSTEM_INODE,
					0, &blkno);
	if (ret)
		goto bail;

	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret)
		goto bail;

	di = (struct ocfs2_dinode *)buf;

	if (clusters)
		*clusters = di->id1.bitmap1.i_total - di->id1.bitmap1.i_used;
bail:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

static void list_sparse_iterate(void *priv_data,
				uint32_t hole_start,
				uint32_t hole_len)
{
	struct list_ctxt *ctxt =
			(struct list_ctxt *)priv_data;

	ctxt->file_hole_len += hole_len;
}

/*
 * Iterate a file.
 * Call "func" when we meet with a hole.
 * Call "unwritten_func" when we meet with unwritten clusters.
 * Call "seen_exceed" when we see some clusters exceed i_size.
 */
static errcode_t iterate_file(ocfs2_filesys *fs,
			      struct ocfs2_dinode *di,
			      void (*func)(void *priv_data,
					   uint32_t hole_start,
					   uint32_t hole_len),
			      void (*unwritten_func)(void *priv_data,
						     uint32_t start,
						     uint32_t len,
						     uint64_t p_start),
			      void (*seen_exceed)(void *priv_data),
			      void *priv_data)
{
	errcode_t ret;
	uint32_t clusters, v_cluster = 0, p_cluster, num_clusters;
	uint32_t last_v_cluster = 0;
	uint64_t p_blkno;
	uint16_t extent_flags;
	ocfs2_cached_inode *ci = NULL;

	clusters = (di->i_size + fs->fs_clustersize -1 ) /
			fs->fs_clustersize;

	ret = ocfs2_read_cached_inode(fs, di->i_blkno, &ci);
	if (ret)
		goto bail;

	while (v_cluster < clusters) {
		ret = ocfs2_get_clusters(ci,
					 v_cluster, &p_cluster,
					 &num_clusters, &extent_flags);
		if (ret)
			goto bail;

		if (!p_cluster) {
			/*
			 * If the tail of the file is a hole, let the
			 * hole length only cover the last i_size.
			 */
			if (v_cluster + num_clusters == UINT32_MAX)
				num_clusters = clusters - v_cluster;

			if (func)
				func(priv_data, v_cluster, num_clusters);
		}

		if ((extent_flags & OCFS2_EXT_UNWRITTEN) && unwritten_func) {
			p_blkno = ocfs2_clusters_to_blocks(fs, p_cluster);
			unwritten_func(priv_data,
				       v_cluster, num_clusters, p_blkno);
		}

		v_cluster += num_clusters;
	}

	/*
	 * If the last allocated cluster's virtual offset is greater
	 * than the clusters we calculated from i_size, this cluster
	 * must exceed the limit of i_size.
	 */
	ret = ocfs2_get_last_cluster_offset(fs, di, &last_v_cluster);
	if (ret)
		goto bail;

	if (last_v_cluster >= clusters && seen_exceed)
		seen_exceed(priv_data);

bail:
	if (ci)
		ocfs2_free_cached_inode(fs, ci);
	return ret;
}

/*
 * For a regular file, we will iterate it and calculate all the
 * hole in it and store the information in ctxt->file_hole_len.
 *
 * for the file which has i_links_count > 1, we only iterate it
 * when we meet it the first time and record it into multi_link_file
 * tree, so the next time we will just search the tree and set
 * file_hole_len accordingly.
 */
static errcode_t list_sparse_file(ocfs2_filesys *fs,
				  struct ocfs2_dinode *di,
				  struct list_ctxt *ctxt)
{
	errcode_t ret = 0;
	struct multi_link_file *file = NULL;

	assert(S_ISREG(di->i_mode));

	ctxt->file_hole_len = 0;

	if (di->i_dyn_features & OCFS2_INLINE_DATA_FL)
		return 0;

	if (di->i_links_count > 1) {
		file = multi_link_file_lookup(ctxt, di->i_blkno);

		if (file) {
			ctxt->file_hole_len = file->clusters;
			ctxt->duplicated = 1;
			goto print;
		}
	}

	ret = iterate_file(fs, di, ctxt->func, NULL, NULL, ctxt);
	if (ret)
		goto bail;

	if ( di->i_links_count > 1) {
		ret = multi_link_file_insert(ctxt,
					     di->i_blkno, ctxt->file_hole_len);
		if (ret)
			goto bail;
	}

print:
	if (ctxt->file_hole_len > 0)
		printf("%"PRIu64"\t%u\t\t%s\n", (uint64_t)di->i_blkno,
			ctxt->file_hole_len, ctxt->file_name);

bail:
	return ret;
}

static int list_sparse_func(struct ocfs2_dir_entry *dirent,
			    uint64_t blocknr, int offset,
			    int blocksize, char *buf, void *priv_data)
{
	errcode_t ret;
	char *di_buf = NULL;
	struct ocfs2_dinode *di = NULL;
	char file_name[OCFS2_MAX_FILENAME_LEN];
	int file_name_len = 0;
	struct list_ctxt *ctxt = (struct list_ctxt *)priv_data;
	ocfs2_filesys *fs = ctxt->fs;

	ret = ocfs2_malloc_block(fs->fs_io, &di_buf);
	if (ret)
		goto bail;

	ret = ocfs2_read_inode(fs, (uint64_t)dirent->inode, di_buf);
	if (ret)
		goto bail;

	di = (struct ocfs2_dinode *)di_buf;

	/* currently, we only handle directories and regular files. */
	if (!S_ISDIR(di->i_mode) && !S_ISREG(di->i_mode))
		return 0;

	strcpy(file_name, ctxt->file_name);
	file_name_len = ctxt->file_name_len;

	if (dirent->name_len + ctxt->file_name_len + 1 >= PATH_MAX)
		goto bail;

	strncat(ctxt->file_name,
		dirent->name,dirent->name_len);
	ctxt->file_name_len += dirent->name_len;

	if (S_ISDIR(di->i_mode)) {
		strcat(ctxt->file_name,"/");
		ctxt->file_name_len++;
		ret = ocfs2_dir_iterate(fs, di->i_blkno,
					OCFS2_DIRENT_FLAG_EXCLUDE_DOTS,
					NULL, list_sparse_func, ctxt);
		if (ret)
			goto bail;
	} else {
		ctxt->duplicated = 0;
		ret = list_sparse_file(fs, di, ctxt);
		if (ret)
			goto bail;
		if (!ctxt->duplicated)
			ctxt->total_clusters +=
					 ctxt->file_hole_len;
	}

bail:
	strcpy(ctxt->file_name, file_name);
	ctxt->file_name_len = file_name_len;

	if (di_buf)
		ocfs2_free(&di_buf);

	return ret;
}

/*
 * list_sparse will iterate from "/" and all the orphan_dirs recursively
 * and print out all the hole information on the screen.
 *
 * We will use ocfs2_dir_iterate to iterate from the very beginning and
 * tunefs_iterate_func will handle every dir entry.
 */
static errcode_t list_sparse(ocfs2_filesys *fs)
{
	int i;
	errcode_t ret;
	uint64_t blkno;
	char file_name[OCFS2_MAX_FILENAME_LEN];
	struct list_ctxt ctxt;
	struct ocfs2_super_block *sb = OCFS2_RAW_SB(fs->fs_super);
	uint32_t total_holes = 0, free_clusters = 0;

	printf("Iterating from the root directory:\n");
	printf("#inode\tcluster nums\tfilepath\n");

	memset(&ctxt, 0, sizeof(ctxt));
	ctxt.fs = fs;
	ctxt.multi_link_files = RB_ROOT;
	ctxt.func = list_sparse_iterate;
	sprintf(ctxt.file_name, "/");
	ctxt.file_name_len = strlen(ctxt.file_name);
	ret = ocfs2_dir_iterate(fs, sb->s_root_blkno,
				OCFS2_DIRENT_FLAG_EXCLUDE_DOTS,
				NULL, list_sparse_func, &ctxt);
	if (ret)
		goto bail;

	printf("Total hole clusters in /: %u\n", ctxt.total_clusters);
	total_holes += ctxt.total_clusters;

	printf("Iterating orphan_dirs:\n");

	for (i = 0; i < sb->s_max_slots; i++) {
		ocfs2_sprintf_system_inode_name(file_name,
						OCFS2_MAX_FILENAME_LEN,
						ORPHAN_DIR_SYSTEM_INODE, i);
		ret = ocfs2_lookup_system_inode(fs, ORPHAN_DIR_SYSTEM_INODE,
						i, &blkno);
		if (ret)
			goto bail;

		empty_multi_link_files(&ctxt);
		memset(&ctxt, 0, sizeof(ctxt));
		ctxt.fs = fs;
		ctxt.multi_link_files = RB_ROOT;
		ctxt.func = list_sparse_iterate;
		sprintf(ctxt.file_name, "%s/", file_name);
		ctxt.file_name_len = strlen(ctxt.file_name);
		ret = ocfs2_dir_iterate(fs, blkno,
					OCFS2_DIRENT_FLAG_EXCLUDE_DOTS,
					NULL, list_sparse_func, &ctxt);
		if (ret)
			goto bail;

		printf("Total hole clusters in %s: %u\n",
			file_name, ctxt.total_clusters);
		total_holes += ctxt.total_clusters;
	}

	printf("Total hole clusters in the volume: %u\n\n", total_holes);

	/* Get the total free bits in the global_bitmap. */
	ret = get_total_free_clusters(fs, &free_clusters);
	if (ret)
		goto bail;

	printf("Total free %u clusters in the volume.\n", free_clusters);

bail:
	empty_multi_link_files(&ctxt);
	return ret;
}

static int list_sparse_run(struct tunefs_operation *op, ocfs2_filesys *fs,
			   int flags)
{
	int rc = 0;
	errcode_t err;

	err = list_sparse(fs);
	if (err) {
		tcom_err(err,
			 "- unable to update list all sparse files on "
			 "device \"%s\"",
			 fs->fs_devname);
		rc = 1;
	}

	return rc;
}

DEFINE_TUNEFS_OP(list_sparse,
		 "Usage: op_list_sparse_files [opts] <device>\n",
		 TUNEFS_FLAG_RW,
		 NULL,
		 list_sparse_run);

#ifdef DEBUG_EXE
int main(int argc, char *argv[])
{
	return tunefs_op_main(argc, argv, &list_sparse_op);
}
#endif
