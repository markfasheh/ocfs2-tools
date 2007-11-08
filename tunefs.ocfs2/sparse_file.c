/*
 * sparse_files.c
 *
 * source file of sparse files functions for tunefs.
 *
 * Copyright (C) 2007 Oracle.  All rights reserved.
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

#include <kernel-rbtree.h>
#include <tunefs.h>
#include <assert.h>

struct multi_link_file {
	struct rb_node br_node;
	uint64_t blkno;
	uint32_t clusters;
};

struct list_ctxt {
	ocfs2_filesys *fs;
	uint64_t total_clusters;
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

struct hole_list {
	uint32_t start;
	uint32_t len;
	struct hole_list *next;
};

struct unwritten_list {
	uint32_t start;
	uint32_t len;
	uint64_t p_start;
	struct unwritten_list *next;
};

/*
 * A sparse file may have many holes and unwritten extents. So all the holes
 * will be stored in hole_list and all unwritten clusters will be stored in
 * unwritten_list. Since filling up a hole may need a new extent record and
 * lead to some new extent block, the total hole number in the sparse file
 * will also be recorded.
 */
struct sparse_file {
	uint64_t blkno;
	uint32_t holes_num;
	struct hole_list *holes;
	struct unwritten_list *unwritten;
	struct sparse_file *next;
};

struct clear_hole_unwritten_ctxt {
	errcode_t ret;
	uint32_t more_clusters;
	uint32_t more_ebs;
	struct sparse_file *files;
};

/*
 * clear_ctxt is initailized and calculated in clear_sparse_file_check and
 * we will use it in clear_sparse_file_flag.
 */
static struct clear_hole_unwritten_ctxt clear_ctxt;

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
	char file_name[OCFS2_MAX_FILENAME_LEN];
	struct ocfs2_super_block *sb = OCFS2_RAW_SB(fs->fs_super);

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		goto bail;

	snprintf(file_name, sizeof(file_name),
		 ocfs2_system_inodes[GLOBAL_BITMAP_SYSTEM_INODE].si_name);

	ret = ocfs2_lookup(fs, sb->s_system_dir_blkno, file_name,
			   strlen(file_name), NULL, &blkno);
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
			      void *priv_data)
{
	errcode_t ret;
	uint32_t clusters, v_cluster = 0, p_cluster, num_clusters;
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

	if (di->i_links_count > 1) {
		file = multi_link_file_lookup(ctxt, di->i_blkno);

		if (file) {
			ctxt->file_hole_len = file->clusters;
			ctxt->duplicated = 1;
			goto print;
		}
	}

	ret = iterate_file(fs, di, ctxt->func, NULL, ctxt);
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
		printf("%"PRIu64"\t%u\t\t%s\n", di->i_blkno,
			ctxt->file_hole_len, ctxt->file_name);

bail:
	return ret;
}

static int list_sparse_func(struct ocfs2_dir_entry *dirent,
			    int offset, int blocksize,
			    char *buf, void *priv_data)
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
errcode_t list_sparse(ocfs2_filesys *fs)
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
		snprintf(file_name, sizeof(file_name),
		ocfs2_system_inodes[ORPHAN_DIR_SYSTEM_INODE].si_name, i);

		ret = ocfs2_lookup(fs, sb->s_system_dir_blkno, file_name,
				   strlen(file_name), NULL, &blkno);
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

static errcode_t iterate_all_regular(ocfs2_filesys *fs, char *progname,
				     errcode_t (*func)(ocfs2_filesys *fs,
						struct ocfs2_dinode *di))
{
	errcode_t ret;
	uint64_t blkno;
	char *buf;
	struct ocfs2_dinode *di;
	ocfs2_inode_scan *scan;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		goto out;

	di = (struct ocfs2_dinode *)buf;

	ret = ocfs2_open_inode_scan(fs, &scan);
	if (ret) {
		com_err(progname, ret, "while opening inode scan");
		goto out_free;
	}

	for(;;) {
		ret = ocfs2_get_next_inode(scan, &blkno, buf);
		if (ret) {
			com_err(progname, ret,
				"while getting next inode");
			break;
		}
		if (blkno == 0)
			break;

		if (memcmp(di->i_signature, OCFS2_INODE_SIGNATURE,
			    strlen(OCFS2_INODE_SIGNATURE)))
			continue;

		ocfs2_swap_inode_to_cpu(di);

		if (di->i_fs_generation != fs->fs_super->i_fs_generation)
			continue;

		if (!(di->i_flags & OCFS2_VALID_FL))
			continue;

		if (di->i_flags & OCFS2_SYSTEM_FL)
			continue;

		if (S_ISREG(di->i_mode) && func) {
			ret = func(fs, di);
			if (ret)
				break;
		}
	}

	ocfs2_close_inode_scan(scan);
out_free:
	ocfs2_free(&buf);

out:
	return ret;
}

static errcode_t set_func(ocfs2_filesys *fs, struct ocfs2_dinode *di)
{
	errcode_t ret;
	uint32_t new_clusters;
	ocfs2_cached_inode *ci = NULL;

	ret = ocfs2_read_cached_inode(fs, di->i_blkno, &ci);
	if (ret)
		goto out;

	ret = ocfs2_zero_tail_and_truncate(fs, ci, di->i_size, &new_clusters);

	if (new_clusters != ci->ci_inode->i_clusters) {
		ci->ci_inode->i_clusters = new_clusters;
		ret = ocfs2_write_cached_inode(fs, ci);
	}

	if (ci)
		ocfs2_free_cached_inode(fs, ci);
out:
	return ret;
}

errcode_t set_sparse_file_flag(ocfs2_filesys *fs, char *progname)
{
	errcode_t ret;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);

	/*
	 * The flag to call set_sparse_file may get set as a result of
	 * unwritten extents being turned on, despite the file system
	 * already supporting sparse files. We can safely do nothing
	 * here.
	 */
	if (ocfs2_sparse_alloc(super))
		return 0;

	ret = iterate_all_regular(fs, progname, set_func);

	if (ret)
		goto bail;

	OCFS2_SET_INCOMPAT_FEATURE(super, OCFS2_FEATURE_INCOMPAT_SPARSE_ALLOC);

bail:
	return ret;
}

void set_unwritten_extents_flag(ocfs2_filesys *fs)
{
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);

	/*
	 * If we hit this, it's a bug - the code in feature_check()
	 * should have prevented us from getting this far.
	 *
	 * XXX: Is it fatal? Should we just refuse the change and
	 * print a warning to the console?
	 */
	assert(ocfs2_sparse_alloc(OCFS2_RAW_SB(fs->fs_super)));

	OCFS2_SET_RO_COMPAT_FEATURE(super, OCFS2_FEATURE_RO_COMPAT_UNWRITTEN);
}

static void add_hole(void *priv_data, uint32_t hole_start, uint32_t hole_len)
{
	errcode_t ret;
	struct hole_list *hole = NULL;
	struct clear_hole_unwritten_ctxt *ctxt =
			(struct clear_hole_unwritten_ctxt *)priv_data;

	ret = ocfs2_malloc0(sizeof(struct hole_list), &hole);
	if (ret) {
		ctxt->ret = ret;
		return;
	}

	hole->start = hole_start;
	hole->len = hole_len;

	hole->next = ctxt->files->holes;
	ctxt->files->holes = hole;
	ctxt->more_clusters += hole_len;
	ctxt->files->holes_num++;
}

static void add_unwritten(void *priv_data, uint32_t start,
			  uint32_t len, uint64_t p_start)
{
	errcode_t ret;
	struct unwritten_list *unwritten = NULL;
	struct clear_hole_unwritten_ctxt *ctxt =
			(struct clear_hole_unwritten_ctxt *)priv_data;

	ret = ocfs2_malloc0(sizeof(struct unwritten_list), &unwritten);
	if (ret) {
		ctxt->ret = ret;
		return;
	}

	unwritten->start = start;
	unwritten->len = len;
	unwritten->p_start = p_start;

	unwritten->next = ctxt->files->unwritten;
	ctxt->files->unwritten = unwritten;
}

static errcode_t calc_hole_and_unwritten(ocfs2_filesys *fs,
					 struct ocfs2_dinode *di)
{
	errcode_t ret = 0;
	uint64_t blk_num;
	uint32_t clusters;
	struct sparse_file *file = NULL, *old_files = NULL;
	uint32_t recs_per_eb = ocfs2_extent_recs_per_eb(fs->fs_blocksize);

	assert(S_ISREG(di->i_mode));

	ret = ocfs2_malloc0(sizeof(struct sparse_file), &file);
	if (ret)
		goto bail;

	file->blkno = di->i_blkno;
	old_files = clear_ctxt.files;
	clear_ctxt.files = file;
	ret = iterate_file(fs, di, add_hole, add_unwritten, &clear_ctxt);
	if (ret || clear_ctxt.ret) {
		clear_ctxt.files = old_files;
		goto bail;
	}

	/* If there is no hole or unwritten extents in the file, free it. */
	if (!file->unwritten && !file->holes) {
		clear_ctxt.files = old_files;
		goto bail;
	}
	/*
	 * We have  "hole_num" holes, so more extent records are needed,
	 * and more extent blocks may needed here.
	 * In order to simplify the estimation process, we take it for
	 * granted that one hole need one extent record, so that we can
	 * calculate the extent block we need roughly.
	 */
	blk_num = (file->holes_num + recs_per_eb - 1) / recs_per_eb;
	clusters = ocfs2_clusters_in_blocks(fs, blk_num);
	clear_ctxt.more_ebs += clusters;

	file->next = old_files;

	return 0;
bail:
	if (file)
		ocfs2_free(&file);
	return ret;
}

errcode_t clear_sparse_file_check(ocfs2_filesys *fs, char *progname)
{
	errcode_t ret;
	uint32_t free_clusters = 0;

	memset(&clear_ctxt, 0, sizeof(clear_ctxt));
	ret = iterate_all_regular(fs, progname, calc_hole_and_unwritten);
	if (ret)
		goto bail;

	ret = get_total_free_clusters(fs, &free_clusters);
	if (ret)
		goto bail;

	printf("We have %u clusters free and need %u clusters for sparse files "
		"and %u clusters for more extent blocks\n",
		free_clusters, clear_ctxt.more_clusters, clear_ctxt.more_ebs);

	if (free_clusters < clear_ctxt.more_clusters + clear_ctxt.more_ebs) {
		com_err(progname, 0, "Don't have enough free space.");
		ret = OCFS2_ET_NO_SPACE;
	}
bail:
	return ret;
}

static errcode_t empty_clusters(ocfs2_filesys *fs,
				uint64_t start_blk,
				uint32_t num_clusters)
{
	errcode_t ret;
	char *buf = NULL;
	uint16_t bpc = fs->fs_clustersize / fs->fs_blocksize;

	ret = ocfs2_malloc_blocks(fs->fs_io, bpc, &buf);
	if (ret)
		goto bail;

	memset(buf, 0, fs->fs_clustersize);

	while (num_clusters) {
		ret = io_write_block(fs->fs_io, start_blk, bpc, buf);
		if (ret)
			goto bail;

		num_clusters--;
		start_blk += bpc;
	}

bail:
	if (buf)
		ocfs2_free(&buf);

	return ret;
}

errcode_t clear_sparse_file_flag(ocfs2_filesys *fs, char *progname)
{
	errcode_t ret = 0;
	uint32_t len, start, n_clusters;
	uint64_t p_start;
	char *buf = NULL;
	struct ocfs2_dinode *di = NULL;
	struct hole_list *hole = NULL;
	struct unwritten_list *unwritten = NULL;
	struct sparse_file *file = clear_ctxt.files;
	struct ocfs2_super_block *super = OCFS2_RAW_SB(fs->fs_super);

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		goto bail;

	/* Iterate all the holes and fill them. */
	while (file) {
		hole = file->holes;
		while (hole) {
			len = hole->len;
			start = hole->start;
			while (len) {
				ret = ocfs2_new_clusters(fs, 1, len,
							 &p_start, &n_clusters);
				if (n_clusters == 0)
					ret = OCFS2_ET_NO_SPACE;
				if (ret)
					goto bail;

				ret = empty_clusters(fs, p_start, n_clusters);
				if (ret)
					goto bail;

				ret = ocfs2_insert_extent(fs, file->blkno,
							  start, p_start,
							  n_clusters, 0);
				if (ret)
					goto bail;

				len -= n_clusters;
				start += n_clusters;
			}

			hole = hole->next;
		}

		file = file->next;
	}

	/*
	 * Iterate all the unwritten extents, empty its content and
	 * mark it written.
	 */
	file = clear_ctxt.files;
	while (file) {
		if (!file->unwritten)
			goto next_file;

		ret = ocfs2_read_inode(fs, file->blkno, buf);
		if (ret)
			goto bail;
		di = (struct ocfs2_dinode *)buf;

		unwritten = file->unwritten;
		while (unwritten) {
			start = unwritten->start;
			len = unwritten->len;
			p_start = unwritten->p_start;

			ret = empty_clusters(fs, p_start, len);
			if (ret)
				goto bail;

			ret = ocfs2_mark_extent_written(fs, di, start,
							len, p_start);
			if (ret)
				goto bail;
			unwritten = unwritten->next;
		}
next_file:
		file = file->next;
	}

	if(ocfs2_writes_unwritten_extents(super))
		OCFS2_CLEAR_RO_COMPAT_FEATURE(super,
					 OCFS2_FEATURE_RO_COMPAT_UNWRITTEN);

	OCFS2_CLEAR_INCOMPAT_FEATURE(super,
				     OCFS2_FEATURE_INCOMPAT_SPARSE_ALLOC);

bail:
	return ret;
}

void free_clear_ctxt(void)
{
	struct sparse_file *file = NULL;
	struct hole_list *hole = NULL;
	struct unwritten_list *unwritten = NULL;

	while (clear_ctxt.files) {
		while (clear_ctxt.files->holes) {
			hole = clear_ctxt.files->holes;
			clear_ctxt.files->holes = hole->next;
			ocfs2_free(&hole);
		}

		while (clear_ctxt.files->unwritten) {
			unwritten = clear_ctxt.files->unwritten;
			clear_ctxt.files->unwritten = unwritten->next;
			ocfs2_free(&unwritten);
		}

		file = clear_ctxt.files;
		clear_ctxt.files = file->next;
		ocfs2_free(&file);
	}
}
