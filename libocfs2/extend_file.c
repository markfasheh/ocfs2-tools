/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * extend_file.c
 *
 * Adds extents to an OCFS2 inode.  For the OCFS2 userspace library.
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
 */

#define _XOPEN_SOURCE 600 /* Triggers magic in features.h */
#define _LARGEFILE64_SOURCE

#include <string.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <inttypes.h>
#include <errno.h>
#include <assert.h>
#include "ocfs2.h"

/*
 * Structures which describe a path through a btree, and functions to
 * manipulate them.
 *
 * The idea here is to be as generic as possible with the tree
 * manipulation code.
 */
struct ocfs2_path_item {
	uint64_t			blkno;
	char				*buf;
	struct ocfs2_extent_list	*el;
};

#define OCFS2_MAX_PATH_DEPTH	5

struct ocfs2_path {
	int			p_tree_depth;
	struct ocfs2_path_item	p_node[OCFS2_MAX_PATH_DEPTH];
};

#define path_root_blkno(_path) ((_path)->p_node[0].blkno)
#define path_root_buf(_path) ((_path)->p_node[0].buf)
#define path_root_el(_path) ((_path)->p_node[0].el)
#define path_leaf_blkno(_path) ((_path)->p_node[(_path)->p_tree_depth].blkno)
#define path_leaf_buf(_path) ((_path)->p_node[(_path)->p_tree_depth].buf)
#define path_leaf_el(_path) ((_path)->p_node[(_path)->p_tree_depth].el)
#define path_num_items(_path) ((_path)->p_tree_depth + 1)

struct insert_ctxt {
	ocfs2_filesys *fs;
	struct ocfs2_dinode *di;
	struct ocfs2_extent_rec rec;
};
/*
 * Reset the actual path elements so that we can re-use the structure
 * to build another path. Generally, this involves freeing the buffer
 * heads.
 */
static void ocfs2_reinit_path(struct ocfs2_path *path, int keep_root)
{
	int i, start = 0, depth = 0;
	struct ocfs2_path_item *node;

	if (keep_root)
		start = 1;

	for(i = start; i < path_num_items(path); i++) {
		node = &path->p_node[i];
		if (!node->buf)
			continue;

		ocfs2_free(&node->buf);
		node->blkno = 0;
		node->buf = NULL;
		node->el = NULL;
	}

	/*
	 * Tree depth may change during truncate, or insert. If we're
	 * keeping the root extent list, then make sure that our path
	 * structure reflects the proper depth.
	 */
	if (keep_root)
		depth = path_root_el(path)->l_tree_depth;

	path->p_tree_depth = depth;
}

static void ocfs2_free_path(struct ocfs2_path *path)
{
	/* We don't free the root because often in libocfs2 the root is a
	 * shared buffer such as the inode.  Caller must be responsible for
	 * handling the root of the path.
	 */
	if (path) {
		ocfs2_reinit_path(path, 1);
		ocfs2_free(&path);
	}
}

/*
 * Make the *dest path the same as src and re-initialize src path to
 * have a root only.
 */
static void ocfs2_mv_path(struct ocfs2_path *dest, struct ocfs2_path *src)
{
	int i;

	assert(path_root_blkno(dest) == path_root_blkno(src));

	for(i = 1; i < OCFS2_MAX_PATH_DEPTH; i++) {
		ocfs2_free(&dest->p_node[i].buf);

		dest->p_node[i].blkno = src->p_node[i].blkno;
		dest->p_node[i].buf = src->p_node[i].buf;
		dest->p_node[i].el = src->p_node[i].el;

		src->p_node[i].blkno = 0;
		src->p_node[i].buf = NULL;
		src->p_node[i].el = NULL;
	}
}

/*
 * Insert an extent block at given index.
 *
 * Note:
 * This buf will be inserted into the path, so the caller shouldn't free it.
 */
static inline void ocfs2_path_insert_eb(struct ocfs2_path *path, int index,
					char *buf)
{
	struct ocfs2_extent_block *eb = (struct ocfs2_extent_block *) buf;
	/*
	 * Right now, no root buf is an extent block, so this helps
	 * catch code errors with dinode trees. The assertion can be
	 * safely removed if we ever need to insert extent block
	 * structures at the root.
	 */
	assert(index);

	path->p_node[index].blkno = eb->h_blkno;
	path->p_node[index].buf = (char *)buf;
	path->p_node[index].el = &eb->h_list;
}

static struct ocfs2_path *ocfs2_new_path(ocfs2_filesys* fs, char *buf,
					 struct ocfs2_extent_list *root_el)
{
	errcode_t ret = 0;
	struct ocfs2_path *path = NULL;
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)buf;

	assert(root_el->l_tree_depth < OCFS2_MAX_PATH_DEPTH);

	ret = ocfs2_malloc0(sizeof(*path), &path);
	if (path) {
		path->p_tree_depth = root_el->l_tree_depth;
		path->p_node[0].blkno = di->i_blkno;
		path->p_node[0].buf = buf;
		path->p_node[0].el = root_el;
	}

	return path;
}

/*
 * Allocate and initialize a new path based on a disk inode tree.
 */
static struct ocfs2_path *ocfs2_new_inode_path(ocfs2_filesys *fs,
					       struct ocfs2_dinode *di)
{
	struct ocfs2_extent_list *el = &di->id2.i_list;

	return ocfs2_new_path(fs, (char *)di, el);
}

/* Write all the extent block information to the disk.
 * We write all paths furthur down than subtree_index.
 * The caller will handle writing the sub_index.
 */
static errcode_t ocfs2_write_path_eb(ocfs2_filesys *fs,
				     struct ocfs2_path *path, int sub_index)
{
	errcode_t ret;
	int i;

	for (i = path->p_tree_depth; i > sub_index; i--) {
		ret = ocfs2_write_extent_block(fs,
					       path->p_node[i].blkno,
					       path->p_node[i].buf);
		if (ret)
			return ret;
	}

	return 0;
}

/* some extent blocks is modified and we need to synchronize them to the disk
 * accordingly.
 *
 * We will not update the inode if subtree_index is "0" since it should be
 * updated by the caller.
 */
static errcode_t ocfs2_sync_path_to_disk(ocfs2_filesys *fs,
					 struct ocfs2_path *left_path,
					 struct ocfs2_path *right_path,
					 int subtree_index)
{
	errcode_t ret;
	uint64_t blkno = right_path->p_node[subtree_index].blkno;
	char *sub_root = right_path->p_node[subtree_index].buf;

	assert(right_path);

	if (left_path) {
		ret = ocfs2_write_path_eb(fs, left_path, subtree_index);
		if (ret)
			goto bail;
	}

	ret = ocfs2_write_path_eb(fs, right_path, subtree_index);
	if (ret)
		goto bail;

	if (subtree_index) {
		/* subtree_index indicates an extent block. */
		ret = ocfs2_write_extent_block(fs, blkno, sub_root);
		if (ret)
			goto bail;
	}
bail:
	return ret;
}

enum ocfs2_contig_type {
	CONTIG_NONE = 0,
	CONTIG_LEFT,
	CONTIG_RIGHT
};

/*
 * NOTE: ocfs2_block_extent_contig(), ocfs2_extents_adjacent() and
 * ocfs2_extent_contig only work properly against leaf nodes!
 */
static inline int ocfs2_block_extent_contig(ocfs2_filesys *fs,
					    struct ocfs2_extent_rec *ext,
					    uint64_t blkno)
{
	uint64_t blk_end = ext->e_blkno;

	blk_end += ocfs2_clusters_to_blocks(fs, ext->e_leaf_clusters);

	return blkno == blk_end;
}

static inline int ocfs2_extents_adjacent(struct ocfs2_extent_rec *left,
					 struct ocfs2_extent_rec *right)
{
	uint32_t left_range;

	left_range = left->e_cpos + left->e_leaf_clusters;

	return (left_range == right->e_cpos);
}

static enum ocfs2_contig_type
	ocfs2_extent_contig(ocfs2_filesys *fs,
			    struct ocfs2_extent_rec *ext,
			    struct ocfs2_extent_rec *insert_rec)
{
	uint64_t blkno = insert_rec->e_blkno;

	if (ocfs2_extents_adjacent(ext, insert_rec) &&
	    ocfs2_block_extent_contig(fs, ext, blkno))
			return CONTIG_RIGHT;

	blkno = ext->e_blkno;
	if (ocfs2_extents_adjacent(insert_rec, ext) &&
	    ocfs2_block_extent_contig(fs, insert_rec, blkno))
		return CONTIG_LEFT;

	return CONTIG_NONE;
}

/*
 * NOTE: We can have pretty much any combination of contiguousness and
 * appending.
 *
 * The usefulness of APPEND_TAIL is more in that it lets us know that
 * we'll have to update the path to that leaf.
 */
enum ocfs2_append_type {
	APPEND_NONE = 0,
	APPEND_TAIL,
};

struct ocfs2_insert_type {
	enum ocfs2_append_type	ins_appending;
	enum ocfs2_contig_type	ins_contig;
	int			ins_contig_index;
	int			ins_free_records;
	int			ins_tree_depth;
};

/*
 * Helper function for ocfs2_add_branch() and shift_tree_depth().
 *
 * Returns the sum of the rightmost extent rec logical offset and
 * cluster count.
 *
 * ocfs2_add_branch() uses this to determine what logical cluster
 * value should be populated into the leftmost new branch records.
 *
 * shift_tree_depth() uses this to determine the # clusters
 * value for the new topmost tree record.
 */
static inline uint32_t ocfs2_sum_rightmost_rec(struct ocfs2_extent_list  *el)
{
	uint16_t i = el->l_next_free_rec - 1;

	return el->l_recs[i].e_cpos +
		 ocfs2_rec_clusters(el->l_tree_depth, &el->l_recs[i]);

}

/*
 * Add an entire tree branch to our inode. eb_buf is the extent block
 * to start at, if we don't want to start the branch at the dinode
 * structure.
 *
 * last_eb_buf is required as we have to update it's next_leaf pointer
 * for the new last extent block.
 *
 * the new branch will be 'empty' in the sense that every block will
 * contain a single record with e_clusters == 0.
 */
static int ocfs2_add_branch(ocfs2_filesys *fs,
			    struct ocfs2_dinode *fe,
			    char *eb_buf,
			    char *last_eb_buf)
{
	errcode_t ret;
	int new_blocks, i;
	uint64_t next_blkno, new_last_eb_blk;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_list  *eb_el;
	struct ocfs2_extent_list  *el;
	uint32_t new_cpos;
	uint64_t *new_blknos = NULL;
	char	**new_eb_bufs = NULL;
	char *buf = NULL;

	assert(last_eb_buf);

	if (eb_buf) {
		eb = (struct ocfs2_extent_block *) eb_buf;
		el = &eb->h_list;
	} else
		el = &fe->id2.i_list;

	/* we never add a branch to a leaf. */
	assert(el->l_tree_depth);

	new_blocks = el->l_tree_depth;

	/* allocate the number of new eb blocks we need new_blocks should be
	 * allocated here.*/
	ret = ocfs2_malloc0(sizeof(uint64_t) * new_blocks, &new_blknos);
	if (ret)
		goto bail;
	memset(new_blknos, 0, sizeof(uint64_t) * new_blocks);

	ret = ocfs2_malloc0(sizeof(char *) * new_blocks, &new_eb_bufs);
	if (ret)
		goto bail;
	memset(new_eb_bufs, 0, sizeof(char *) * new_blocks);

	for (i = 0; i < new_blocks; i++) {
		ret = ocfs2_malloc_block(fs->fs_io, &buf);
		if (ret)
			return ret;
		new_eb_bufs[i] = buf;

		ret = ocfs2_new_extent_block(fs, &new_blknos[i]);
		if (ret)
			goto bail;

		ret = ocfs2_read_extent_block(fs, new_blknos[i], buf);
		if (ret)
			goto bail;
	}

	eb = (struct ocfs2_extent_block *)last_eb_buf;
	new_cpos = ocfs2_sum_rightmost_rec(&eb->h_list);

	/* Note: new_eb_bufs[new_blocks - 1] is the guy which will be
	 * linked with the rest of the tree.
	 * conversly, new_eb_bufs[0] is the new bottommost leaf.
	 *
	 * when we leave the loop, new_last_eb_blk will point to the
	 * newest leaf, and next_blkno will point to the topmost extent
	 * block.
	 */
	next_blkno = new_last_eb_blk = 0;
	for(i = 0; i < new_blocks; i++) {
		buf = new_eb_bufs[i];
		eb = (struct ocfs2_extent_block *) buf;
		eb_el = &eb->h_list;

		eb->h_next_leaf_blk = 0;
		eb_el->l_tree_depth = i;
		eb_el->l_next_free_rec = 1;
		memset(eb_el->l_recs, 0,
		       sizeof(struct ocfs2_extent_rec) * eb_el->l_count);
		/*
		 * This actually counts as an empty extent as
		 * c_clusters == 0
		 */
		eb_el->l_recs[0].e_cpos = new_cpos;
		eb_el->l_recs[0].e_blkno = next_blkno;
		/*
		 * eb_el isn't always an interior node, but even leaf
		 * nodes want a zero'd flags and reserved field so
		 * this gets the whole 32 bits regardless of use.
		 */
		eb_el->l_recs[0].e_int_clusters = 0;

		if (!eb_el->l_tree_depth)
			new_last_eb_blk = eb->h_blkno;

		next_blkno = eb->h_blkno;
	}

	/* Link the new branch into the rest of the tree (el will
	 * either be on the fe, or the extent block passed in.
	 */
	i = el->l_next_free_rec;
	el->l_recs[i].e_blkno = next_blkno;
	el->l_recs[i].e_cpos = new_cpos;
	el->l_recs[i].e_int_clusters = 0;
	el->l_next_free_rec++;

	/* fe needs a new last extent block pointer, as does the
	 * next_leaf on the previously last-extent-block.
	 */
	fe->i_last_eb_blk = new_last_eb_blk;

	/* here all the extent block and the new inode information should be
	 * written back to the disk.
	 */
	for(i = 0; i < new_blocks; i++) {
		buf = new_eb_bufs[i];
		ret = ocfs2_write_extent_block(fs, new_blknos[i], buf);
		if (ret)
			goto bail;
	}

	/* update last_eb_buf's next_leaf pointer for
	 * the new last extent block.
	 */
	eb = (struct ocfs2_extent_block *) last_eb_buf;
	eb->h_next_leaf_blk = new_last_eb_blk;
	ret = ocfs2_write_extent_block(fs, eb->h_blkno, last_eb_buf);
	if (ret)
		goto bail;

	if (eb_buf) {
		eb = (struct ocfs2_extent_block *)eb_buf;
		ret = ocfs2_write_extent_block(fs, eb->h_blkno, eb_buf);
		if (ret)
			goto bail;
	}

	/* The inode information isn't updated since we use duplicated extent
	 * block in the insertion and it may fail in other steps.
	 */
	ret = 0;
bail:
	if (new_eb_bufs) {
		for (i = 0; i < new_blocks; i++)
			if (new_eb_bufs[i])
				ocfs2_free(&new_eb_bufs[i]);
		ocfs2_free(&new_eb_bufs);
	}

	if (ret && new_blknos)
		for (i = 0; i < new_blocks; i++)
			if (new_blknos[i])
				ocfs2_delete_extent_block(fs, new_blknos[i]);

	if (new_blknos)
		ocfs2_free(&new_blknos);

	return ret;
}

/*
 * Should only be called when there is no space left in any of the
 * leaf nodes. What we want to do is find the lowest tree depth
 * non-leaf extent block with room for new records. There are three
 * valid results of this search:
 *
 * 1) a lowest extent block is found, then we pass it back in
 *    *target_buf and return '0'
 *
 * 2) the search fails to find anything, but the dinode has room. We
 *    pass NULL back in *target_buf, but still return '0'
 *
 * 3) the search fails to find anything AND the dinode is full, in
 *    which case we return > 0
 *
 * return status < 0 indicates an error.
 */
static errcode_t ocfs2_find_branch_target(ocfs2_filesys *fs,
					  struct ocfs2_dinode *fe,
					  char **target_buf)
{
	errcode_t ret = 0;
	int i;
	uint64_t blkno;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_list  *el;
	char *buf = NULL, *lowest_buf = NULL;

	*target_buf = NULL;

	el = &fe->id2.i_list;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	while(el->l_tree_depth > 1) {
		if (el->l_next_free_rec == 0) {
			ret = OCFS2_ET_CORRUPT_EXTENT_BLOCK;
			goto bail;
		}
		i = el->l_next_free_rec - 1;
		blkno = el->l_recs[i].e_blkno;
		if (!blkno) {
			ret = OCFS2_ET_CORRUPT_EXTENT_BLOCK;
			goto bail;
		}

		ret = ocfs2_read_extent_block(fs, blkno, buf);
		if (ret)
			goto bail;

		eb = (struct ocfs2_extent_block *) buf;
		el = &eb->h_list;

		if (el->l_next_free_rec < el->l_count)
			lowest_buf = buf;
	}

	/* If we didn't find one and the fe doesn't have any room,
	 * then return '1' */
	if (!lowest_buf
	    && (fe->id2.i_list.l_next_free_rec == fe->id2.i_list.l_count))
		ret = 1;

	*target_buf = lowest_buf;
bail:
	if (buf && !*target_buf)
		ocfs2_free(&buf);

	return ret;
}

/*
 * This is only valid for leaf nodes, which are the only ones that can
 * have empty extents anyway.
 */
static inline int ocfs2_is_empty_extent(struct ocfs2_extent_rec *rec)
{
	return !rec->e_leaf_clusters;
}

/*
 * This function will discard the rightmost extent record.
 */
static void ocfs2_shift_records_right(struct ocfs2_extent_list *el)
{
	int next_free = el->l_next_free_rec;
	int count = el->l_count;
	unsigned int num_bytes;

	assert(next_free);
	/* This will cause us to go off the end of our extent list. */
	assert(next_free < count);

	num_bytes = sizeof(struct ocfs2_extent_rec) * next_free;

	memmove(&el->l_recs[1], &el->l_recs[0], num_bytes);
}

static void ocfs2_rotate_leaf(struct ocfs2_extent_list *el,
			      struct ocfs2_extent_rec *insert_rec)
{
	int i, insert_index, next_free, has_empty, num_bytes;
	uint32_t insert_cpos = insert_rec->e_cpos;
	struct ocfs2_extent_rec *rec;

	next_free = el->l_next_free_rec;
	has_empty = ocfs2_is_empty_extent(&el->l_recs[0]);

	assert(next_free);

	/* The tree code before us didn't allow enough room in the leaf. */
	if (el->l_next_free_rec == el->l_count && !has_empty)
		assert(0);

	/*
	 * The easiest way to approach this is to just remove the
	 * empty extent and temporarily decrement next_free.
	 */
	if (has_empty) {
		/*
		 * If next_free was 1 (only an empty extent), this
		 * loop won't execute, which is fine. We still want
		 * the decrement above to happen.
		 */
		for(i = 0; i < (next_free - 1); i++)
			el->l_recs[i] = el->l_recs[i+1];

		next_free--;
	}

	/* Figure out what the new record index should be. */
	for(i = 0; i < next_free; i++) {
		rec = &el->l_recs[i];

		if (insert_cpos < rec->e_cpos)
			break;
	}
	insert_index = i;

	assert(insert_index >= 0);
	assert(insert_index < el->l_count);
	assert(insert_index <= next_free);

	/* No need to memmove if we're just adding to the tail. */
	if (insert_index != next_free) {
		assert(next_free < el->l_count);

		num_bytes = next_free - insert_index;
		num_bytes *= sizeof(struct ocfs2_extent_rec);
		memmove(&el->l_recs[insert_index + 1],
			&el->l_recs[insert_index],
			num_bytes);
	}

	/*
	 * Either we had an empty extent, and need to re-increment or
	 * there was no empty extent on a non full rightmost leaf node,
	 * in which case we still need to increment.
	 */
	next_free++;
	el->l_next_free_rec = next_free;
	/* Make sure none of the math above just messed up our tree. */
	assert(el->l_next_free_rec <= el->l_count);

	el->l_recs[insert_index] = *insert_rec;
}

/*
 * Create an empty extent record .
 *
 * l_next_free_rec may be updated.
 *
 * If an empty extent already exists do nothing.
 */
static void ocfs2_create_empty_extent(struct ocfs2_extent_list *el)
{
	int next_free = el->l_next_free_rec;

	assert(el->l_tree_depth == 0);

	if (next_free == 0)
		goto set_and_inc;

	if (ocfs2_is_empty_extent(&el->l_recs[0]))
		return;

	ocfs2_shift_records_right(el);

set_and_inc:
	el->l_next_free_rec += 1;
	memset(&el->l_recs[0], 0, sizeof(struct ocfs2_extent_rec));
}

/*
 * For a rotation which involves two leaf nodes, the "root node" is
 * the lowest level tree node which contains a path to both leafs. This
 * resulting set of information can be used to form a complete "subtree"
 *
 * This function is passed two full paths from the dinode down to a
 * pair of adjacent leaves. It's task is to figure out which path
 * index contains the subtree root - this can be the root index itself
 * in a worst-case rotation.
 *
 * The array index of the subtree root is passed back.
 */
static int ocfs2_find_subtree_root(struct ocfs2_path *left,
				   struct ocfs2_path *right)
{
	int i = 0;

	/* Check that the caller passed in two paths from the same tree. */
	assert(path_root_blkno(left) == path_root_blkno(right));

	do {
		i++;

		/* The caller didn't pass two adjacent paths. */
 		if (i > left->p_tree_depth)
			assert(0);
	} while (left->p_node[i].blkno == right->p_node[i].blkno);

	return i - 1;
}

typedef errcode_t (path_insert_t)(void *, char *);

/*
 * Traverse a btree path in search of cpos, starting at root_el.
 *
 * This code can be called with a cpos larger than the tree, in which
 * case it will return the rightmost path.
 */
static errcode_t __ocfs2_find_path(ocfs2_filesys *fs,
				   struct ocfs2_extent_list *root_el,
				   uint32_t cpos,
				   path_insert_t *func,
				   void *data)
{
	int i, ret = 0;
	uint32_t range;
	uint64_t blkno;
	char *buf = NULL;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_list *el;
	struct ocfs2_extent_rec *rec;

	el = root_el;
	while (el->l_tree_depth) {
		if (el->l_next_free_rec == 0) {
			ret = OCFS2_ET_CORRUPT_EXTENT_BLOCK;
			goto out;

		}


		for(i = 0; i < el->l_next_free_rec - 1; i++) {
			rec = &el->l_recs[i];

			/*
			 * In the case that cpos is off the allocation
			 * tree, this should just wind up returning the
			 * rightmost record.
			 */
			range = rec->e_cpos +
				ocfs2_rec_clusters(el->l_tree_depth, rec);
			if (cpos >= rec->e_cpos && cpos < range)
			    break;
		}

		blkno = el->l_recs[i].e_blkno;
		if (blkno == 0) {
			ret = OCFS2_ET_CORRUPT_EXTENT_BLOCK;
			goto out;
		}

		ret = ocfs2_malloc_block(fs->fs_io, &buf);
		if (ret)
			return ret;

		ret = ocfs2_read_extent_block(fs, blkno, buf);
		if (ret)
			goto out;

		eb = (struct ocfs2_extent_block *) buf;
		el = &eb->h_list;

		if (el->l_next_free_rec > el->l_count) {
			ret = OCFS2_ET_CORRUPT_EXTENT_BLOCK;
			goto out;
		}

		/* The user's callback must give us the tip for how to
		 * handle the buf we allocated by return values.
		 *
 		 * 1) return '0':
		 *    the function succeeds,and it will use the buf and
		 *    take care of the buffer release.
		 *
 		 * 2) return > 0:
		 *    the function succeeds, and there is no need for buf,
		 *    so we will release it.
		 *
		 * 3) return < 0:
		 *    the function fails.
		 */
		if (func) {
			ret = func(data, buf);

			if (ret == 0) {
				buf = NULL;
				continue;
			}
			else if (ret < 0)
				goto out;
		}
		ocfs2_free(&buf);
		buf = NULL;
	}

out:
	/* Catch any trailing buf that the loop didn't handle. */
	if (buf)
		ocfs2_free(&buf);

	return ret;
}

/*
 * Given an initialized path (that is, it has a valid root extent
 * list), this function will traverse the btree in search of the path
 * which would contain cpos.
 *
 * The path traveled is recorded in the path structure.
 *
 * Note that this will not do any comparisons on leaf node extent
 * records, so it will work fine in the case that we just added a tree
 * branch.
 */
struct find_path_data {
	int index;
	struct ocfs2_path *path;
};

static errcode_t find_path_ins(void *data, char *eb)
{
	struct find_path_data *fp = data;

	ocfs2_path_insert_eb(fp->path, fp->index, eb);
	fp->index++;

	return 0;
}

static int ocfs2_find_path(ocfs2_filesys *fs, struct ocfs2_path *path,
			   uint32_t cpos)
{
	struct find_path_data data;

	data.index = 1;
	data.path = path;
	return __ocfs2_find_path(fs, path_root_el(path), cpos,
				 find_path_ins, &data);
}

/*
 * Find the leaf block in the tree which would contain cpos. No
 * checking of the actual leaf is done.
 *
 * This function doesn't handle non btree extent lists.
 */
int ocfs2_find_leaf(ocfs2_filesys *fs, struct ocfs2_dinode *di,
		    uint32_t cpos, char **leaf_buf)
{
	int ret;
	char *buf = NULL;
	struct ocfs2_path *path = NULL;
	struct ocfs2_extent_list *el = &di->id2.i_list;

	assert(el->l_tree_depth > 0);

	path = ocfs2_new_inode_path(fs, di);
	if (!path) {
		ret = OCFS2_ET_NO_MEMORY;
		goto out;
	}

	ret = ocfs2_find_path(fs, path, cpos);
	if (ret)
		goto out;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		goto out;

	memcpy(buf, path_leaf_buf(path), fs->fs_blocksize);
	*leaf_buf = buf;
out:
	ocfs2_free_path(path);
	return ret;
}

/*
 * Adjust the adjacent records (left_rec, right_rec) involved in a rotation.
 *
 * Basically, we've moved stuff around at the bottom of the tree and
 * we need to fix up the extent records above the changes to reflect
 * the new changes.
 *
 * left_rec: the record on the left.
 * left_child_el: is the child list pointed to by left_rec
 * right_rec: the record to the right of left_rec
 * right_child_el: is the child list pointed to by right_rec
 *
 * By definition, this only works on interior nodes.
 */
static void ocfs2_adjust_adjacent_records(struct ocfs2_extent_rec *left_rec,
				    struct ocfs2_extent_list *left_child_el,
				    struct ocfs2_extent_rec *right_rec,
				    struct ocfs2_extent_list *right_child_el)
{
	uint32_t left_clusters, right_end;

	/*
	 * Interior nodes never have holes. Their cpos is the cpos of
	 * the leftmost record in their child list. Their cluster
	 * count covers the full theoretical range of their child list
	 * - the range between their cpos and the cpos of the record
	 * immediately to their right.
	 */
	left_clusters = right_child_el->l_recs[0].e_cpos;
	left_clusters -= left_rec->e_cpos;
	left_rec->e_int_clusters = left_clusters;

	/*
	 * Calculate the rightmost cluster count boundary before
	 * moving cpos - we will need to adjust clusters after
	 * updating e_cpos to keep the same highest cluster count.
	 */
	right_end = right_rec->e_cpos;
	right_end += right_rec->e_int_clusters;

	right_rec->e_cpos = left_rec->e_cpos;
	right_rec->e_cpos += left_clusters;

	right_end -= right_rec->e_cpos;
	right_rec->e_int_clusters = right_end;
}

/*
 * Adjust the adjacent root node records involved in a
 * rotation. left_el_blkno is passed in as a key so that we can easily
 * find it's index in the root list.
 */
static void ocfs2_adjust_root_records(struct ocfs2_extent_list *root_el,
				      struct ocfs2_extent_list *left_el,
				      struct ocfs2_extent_list *right_el,
				      uint64_t left_el_blkno)
{
	int i;

	assert(root_el->l_tree_depth > left_el->l_tree_depth);

	for(i = 0; i < root_el->l_next_free_rec - 1; i++) {
		if (root_el->l_recs[i].e_blkno == left_el_blkno)
			break;
	}

	/*
	 * The path walking code should have never returned a root and
	 * two paths which are not adjacent.
	 */
	assert(i < (root_el->l_next_free_rec - 1));

	ocfs2_adjust_adjacent_records(&root_el->l_recs[i], left_el,
				      &root_el->l_recs[i + 1], right_el);
}

/*
 * We've changed a leaf block (in right_path) and need to reflect that
 * change back up the subtree.
 *
 * This happens in multiple places:
 *   - When we've moved an extent record from the left path leaf to the right
 *     path leaf to make room for an empty extent in the left path leaf.
 *   - When our insert into the right path leaf is at the leftmost edge
 *     and requires an update of the path immediately to it's left. This
 *     can occur at the end of some types of rotation and appending inserts.
 */
static void ocfs2_complete_edge_insert(ocfs2_filesys *fs,
				       struct ocfs2_path *left_path,
				       struct ocfs2_path *right_path,
				       int subtree_index)
{
	int i, idx;
	uint64_t blkno;
	struct ocfs2_extent_list *el, *left_el, *right_el;
	struct ocfs2_extent_rec *left_rec, *right_rec;

	/*
	 * Update the counts and position values within all the
	 * interior nodes to reflect the leaf rotation we just did.
	 *
	 * The root node is handled below the loop.
	 *
	 * We begin the loop with right_el and left_el pointing to the
	 * leaf lists and work our way up.
	 *
	 * NOTE: within this loop, left_el and right_el always refer
	 * to the *child* lists.
	 */
	left_el = path_leaf_el(left_path);
	right_el = path_leaf_el(right_path);
	for(i = left_path->p_tree_depth - 1; i > subtree_index; i--) {

		/*
		 * One nice property of knowing that all of these
		 * nodes are below the root is that we only deal with
		 * the leftmost right node record and the rightmost
		 * left node record.
		 */
		el = left_path->p_node[i].el;
		idx = left_el->l_next_free_rec - 1;
		left_rec = &el->l_recs[idx];

		el = right_path->p_node[i].el;
		right_rec = &el->l_recs[0];

		ocfs2_adjust_adjacent_records(left_rec, left_el, right_rec,
					      right_el);

		/*
		 * Setup our list pointers now so that the current
		 * parents become children in the next iteration.
		 */
		left_el = left_path->p_node[i].el;
		right_el = right_path->p_node[i].el;
	}

	/*
	 * At the root node, adjust the two adjacent records which
	 * begin our path to the leaves.
	 */

	el = left_path->p_node[subtree_index].el;
	left_el = left_path->p_node[subtree_index + 1].el;
	right_el = right_path->p_node[subtree_index + 1].el;
	blkno = left_path->p_node[subtree_index + 1].blkno;

	ocfs2_adjust_root_records(el, left_el, right_el, blkno);

	/* ocfs2_adjust_root_records only update the extent block in the left
	 * path, and actually right_path->p_node[subtree_index].eb indicates the
	 * same extent block, so we must keep them the same content.
	 */
	memcpy(right_path->p_node[subtree_index].buf,
	       left_path->p_node[subtree_index].buf, fs->fs_blocksize);
}

/* Rotate the subtree to right.
 *
 * Note: After successful rotation, the extent block will be flashed
 * to disk accordingly.
 */
static errcode_t ocfs2_rotate_subtree_right(ocfs2_filesys *fs,
					    struct ocfs2_path *left_path,
					    struct ocfs2_path *right_path,
					    int subtree_index)
{
	errcode_t ret;
	int i;
	char *right_leaf_eb;
	char *left_leaf_eb = NULL;
	struct ocfs2_extent_list *right_el, *left_el;
	struct ocfs2_extent_rec move_rec;
	struct ocfs2_extent_block *eb;

	left_leaf_eb = path_leaf_buf(left_path);
	eb = (struct ocfs2_extent_block *)left_leaf_eb;
	left_el = path_leaf_el(left_path);

	if (left_el->l_next_free_rec != left_el->l_count)
		return OCFS2_ET_CORRUPT_EXTENT_BLOCK;

	/*
	 * This extent block may already have an empty record, so we
	 * return early if so.
	 */
	if (ocfs2_is_empty_extent(&left_el->l_recs[0]))
		return 0;

	assert(left_path->p_node[subtree_index].blkno ==
	       right_path->p_node[subtree_index].blkno);

	right_leaf_eb = path_leaf_buf(right_path);
	right_el = path_leaf_el(right_path);

	ocfs2_create_empty_extent(right_el);

	/* Do the copy now. */
	i = left_el->l_next_free_rec - 1;
	move_rec = left_el->l_recs[i];
	right_el->l_recs[0] = move_rec;

	/*
	 * Clear out the record we just copied and shift everything
	 * over, leaving an empty extent in the left leaf.
	 *
	 * We temporarily subtract from next_free_rec so that the
	 * shift will lose the tail record (which is now defunct).
	 */
	left_el->l_next_free_rec -= 1;
	ocfs2_shift_records_right(left_el);
	memset(&left_el->l_recs[0], 0, sizeof(struct ocfs2_extent_rec));
	left_el->l_next_free_rec += 1;

	ocfs2_complete_edge_insert(fs, left_path, right_path, subtree_index);

	ret = ocfs2_sync_path_to_disk(fs, left_path, right_path, subtree_index);

	return ret;
}

/*
 * Given a full path, determine what cpos value would return us a path
 * containing the leaf immediately to the left of the current one.
 *
 * Will return zero if the path passed in is already the leftmost path.
 */
static int ocfs2_find_cpos_for_left_leaf(struct ocfs2_path *path,
					 uint32_t *cpos)
{
	int i, j, ret = 0;
	uint64_t blkno;
	struct ocfs2_extent_list *el;

	assert(path->p_tree_depth > 0);

	*cpos = 0;

	blkno = path_leaf_blkno(path);

	/* Start at the tree node just above the leaf and work our way up. */
	i = path->p_tree_depth - 1;
	while (i >= 0) {
		el = path->p_node[i].el;

		/* Find the extent record just before the one in our path. */
		for(j = 0; j < el->l_next_free_rec; j++) {
			if (el->l_recs[j].e_blkno == blkno) {
				if (j == 0) {
					if (i == 0) {
						/*
						 * We've determined that the
						 * path specified is already
						 * the leftmost one - return a
						 * cpos of zero.
						 */
						goto out;
					}
					/*
					 * The leftmost record points to our
					 * leaf - we need to travel up the
					 * tree one level.
					 */
					goto next_node;
				}

				*cpos = el->l_recs[j - 1].e_cpos;
				*cpos = *cpos + ocfs2_rec_clusters(
							el->l_tree_depth,
							&el->l_recs[j - 1]);
				*cpos = *cpos - 1;
				goto out;
			}
		}

		/*
		 * If we got here, we never found a valid node where
		 * the tree indicated one should be.
		 */
		ret = OCFS2_ET_CORRUPT_EXTENT_BLOCK;
		goto out;

next_node:
		blkno = path->p_node[i].blkno;
		i--;
	}

out:
	return ret;
}

/*
 * Trap the case where we're inserting into the theoretical range past
 * the _actual_ left leaf range. Otherwise, we'll rotate a record
 * whose cpos is less than ours into the right leaf.
 *
 * It's only necessary to look at the rightmost record of the left
 * leaf because the logic that calls us should ensure that the
 * theoretical ranges in the path components above the leaves are
 * correct.
 */
static int ocfs2_rotate_requires_path_adjustment(struct ocfs2_path *left_path,
						 uint32_t insert_cpos)
{
	struct ocfs2_extent_list *left_el;
	struct ocfs2_extent_rec *rec;
	int next_free;

	left_el = path_leaf_el(left_path);
	next_free = left_el->l_next_free_rec;
	rec = &left_el->l_recs[next_free - 1];

	if (insert_cpos > rec->e_cpos)
		return 1;
	return 0;
}

/*
 * Rotate all the records in a btree right one record, starting at insert_cpos.
 *
 * The path to the rightmost leaf should be passed in.
 *
 * The array is assumed to be large enough to hold an entire path (tree depth).
 *
 * Upon succesful return from this function:
 *
 * - The 'right_path' array will contain a path to the leaf block
 *   whose range contains e_cpos.
 * - That leaf block will have a single empty extent in list index 0.
 * - In the case that the rotation requires a post-insert update,
 *   *ret_left_path will contain a valid path which can be passed to
 *   ocfs2_insert_path().
 */
static int ocfs2_rotate_tree_right(ocfs2_filesys *fs,
				   uint32_t insert_cpos,
				   struct ocfs2_path *right_path,
				   struct ocfs2_path **ret_left_path)
{
	int ret, start;
	uint32_t cpos;
	struct ocfs2_path *left_path = NULL;

	*ret_left_path = NULL;

	left_path = ocfs2_new_path(fs, path_root_buf(right_path),
				   path_root_el(right_path));
	if (!left_path) {
		ret = OCFS2_ET_NO_MEMORY;
		goto out;
	}

	ret = ocfs2_find_cpos_for_left_leaf(right_path, &cpos);
	if (ret)
		goto out;

	/*
	 * What we want to do here is:
	 *
	 * 1) Start with the rightmost path.
	 *
	 * 2) Determine a path to the leaf block directly to the left
         *    of that leaf.
	 *
	 * 3) Determine the 'subtree root' - the lowest level tree node
	 *    which contains a path to both leaves.
	 *
	 * 4) Rotate the subtree.
	 *
	 * 5) Find the next subtree by considering the left path to be
         *    the new right path.
	 *
	 * The check at the top of this while loop also accepts
	 * insert_cpos == cpos because cpos is only a _theoretical_
	 * value to get us the left path - insert_cpos might very well
	 * be filling that hole.
	 *
	 * Stop at a cpos of '0' because we either started at the
	 * leftmost branch (i.e., a tree with one branch and a
	 * rotation inside of it), or we've gone as far as we can in
	 * rotating subtrees.
	 */
	while (cpos && insert_cpos <= cpos) {

		ret = ocfs2_find_path(fs, left_path, cpos);
		if (ret)
			goto out;

		if (path_leaf_blkno(left_path) == path_leaf_blkno(right_path))
			assert(0);

		if (ocfs2_rotate_requires_path_adjustment(left_path,
							  insert_cpos)) {
			/*
			 * We've rotated the tree as much as we
			 * should. The rest is up to
			 * ocfs2_insert_path() to complete, after the
			 * record insertion. We indicate this
			 * situation by returning the left path.
			 *
			 * The reason we don't adjust the records here
			 * before the record insert is that an error
			 * later might break the rule where a parent
			 * record e_cpos will reflect the actual
			 * e_cpos of the 1st nonempty record of the
			 * child list.
			 */
			*ret_left_path = left_path;
			goto out_ret_path;
		}

		start = ocfs2_find_subtree_root(left_path, right_path);

		ret = ocfs2_rotate_subtree_right(fs, left_path, right_path,
						 start);
		if (ret)
			goto out;

		/*
		 * There is no need to re-read the next right path
		 * as we know that it'll be our current left
		 * path. Optimize by copying values instead.
		 */
		ocfs2_mv_path(right_path, left_path);

		ret = ocfs2_find_cpos_for_left_leaf(right_path, &cpos);
		if (ret)
			goto out;
	}

out:
	ocfs2_free_path(left_path);

out_ret_path:
	return ret;
}

/*
 * Change the depth of the tree. That means allocating an extent block,
 * copying all extent records from the dinode into the extent block,
 * and then pointing the dinode to the new extent_block.
 */
static errcode_t shift_tree_depth(ocfs2_filesys *fs,
				  struct ocfs2_dinode *di,
				  char **new_eb)
{
	errcode_t ret;
	char *buf = NULL;
	uint64_t blkno;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_list *el;
	uint32_t new_clusters;

	el = &di->id2.i_list;
	if (el->l_next_free_rec != el->l_count)
		return OCFS2_ET_INTERNAL_FAILURE;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_new_extent_block(fs, &blkno);
	if (ret)
		goto out;

	ret = ocfs2_read_extent_block(fs, blkno, buf);
	if (ret)
		goto out;

	eb = (struct ocfs2_extent_block *)buf;
	eb->h_list.l_tree_depth = el->l_tree_depth;
	eb->h_list.l_next_free_rec = el->l_next_free_rec;
	memcpy(eb->h_list.l_recs, el->l_recs,
	       sizeof(struct ocfs2_extent_rec) * el->l_count);

	new_clusters = ocfs2_sum_rightmost_rec(&eb->h_list);

	el->l_tree_depth++;
	memset(el->l_recs, 0,
	       sizeof(struct ocfs2_extent_rec) * el->l_count);
	el->l_recs[0].e_cpos = 0;
	el->l_recs[0].e_blkno = blkno;
	el->l_recs[0].e_int_clusters = new_clusters;
	el->l_next_free_rec = 1;

	if (el->l_tree_depth == 1)
		di->i_last_eb_blk = blkno;

	ret = ocfs2_write_extent_block(fs, blkno, buf);
	if (!ret)
		*new_eb = buf;
out:
	if (buf && !*new_eb)
		ocfs2_free(&buf);

	return ret;
}

static void ocfs2_figure_contig_type(ocfs2_filesys *fs,
				     struct ocfs2_insert_type *insert,
				     struct ocfs2_extent_list *el,
				     struct ocfs2_extent_rec *insert_rec)
{
	int i;
	enum ocfs2_contig_type contig_type = CONTIG_NONE;

	assert(el->l_tree_depth == 0);

	for(i = 0; i < el->l_next_free_rec; i++) {
		contig_type = ocfs2_extent_contig(fs, &el->l_recs[i],
						  insert_rec);
		if (contig_type != CONTIG_NONE) {
			insert->ins_contig_index = i;
			break;
		}
	}
	insert->ins_contig = contig_type;
}

/*
 * This should only be called against the righmost leaf extent list.
 *
 * ocfs2_figure_appending_type() will figure out whether we'll have to
 * insert at the tail of the rightmost leaf.
 *
 * This should also work against the dinode list for tree's with 0
 * depth. If we consider the dinode list to be the rightmost leaf node
 * then the logic here makes sense.
 */
static void ocfs2_figure_appending_type(struct ocfs2_insert_type *insert,
					struct ocfs2_extent_list *el,
					struct ocfs2_extent_rec *insert_rec)
{
	int i;
	uint32_t cpos = insert_rec->e_cpos;
	struct ocfs2_extent_rec *rec;

	insert->ins_appending = APPEND_NONE;

	assert(el->l_tree_depth == 0);

	if (!el->l_next_free_rec)
		goto set_tail_append;

	if (ocfs2_is_empty_extent(&el->l_recs[0])) {
		/* Were all records empty? */
		if (el->l_next_free_rec == 1)
			goto set_tail_append;
	}

	i = el->l_next_free_rec - 1;
	rec = &el->l_recs[i];

	if (cpos >= (rec->e_cpos + rec->e_leaf_clusters))
		goto set_tail_append;

	return;

set_tail_append:
	insert->ins_appending = APPEND_TAIL;
}

/*
 * Helper function called at the begining of an insert.
 *
 * This computes a few things that are commonly used in the process of
 * inserting into the btree:
 *   - Whether the new extent is contiguous with an existing one.
 *   - The current tree depth.
 *   - Whether the insert is an appending one.
 *   - The total # of free records in the tree.
 *
 * All of the information is stored on the ocfs2_insert_type
 * structure.
 */
static int ocfs2_figure_insert_type(struct insert_ctxt *ctxt,
				    char **last_eb_buf,
				    struct ocfs2_insert_type *insert)
{
	int ret;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_list *el;
	struct ocfs2_dinode *di = ctxt->di;
	struct ocfs2_extent_rec *insert_rec = &ctxt->rec;
	ocfs2_filesys *fs = ctxt->fs;
	struct ocfs2_path *path = NULL;
	char *buf = NULL;

	el = &di->id2.i_list;
	insert->ins_tree_depth = el->l_tree_depth;

	if (el->l_tree_depth) {
		/*
		 * If we have tree depth, we read in the
		 * rightmost extent block ahead of time as
		 * ocfs2_figure_insert_type() and ocfs2_add_branch()
		 * may want it later.
		 */
		ret = ocfs2_malloc_block(fs->fs_io, &buf);
		if (ret)
			return ret;

		ret = ocfs2_read_extent_block(fs, di->i_last_eb_blk, buf);
		if (ret)
			goto out;

		eb = (struct ocfs2_extent_block *) buf;
		el = &eb->h_list;
	}
	/*
	 * Unless we have a contiguous insert, we'll need to know if
	 * there is room left in our allocation tree for another
	 * extent record.
	 *
	 * XXX: This test is simplistic, we can search for empty
	 * extent records too.
	 */
	insert->ins_free_records = el->l_count - el->l_next_free_rec;

	if (!insert->ins_tree_depth) {
		insert->ins_free_records = el->l_count - el->l_next_free_rec;
		ocfs2_figure_contig_type(fs, insert, el, insert_rec);
		ocfs2_figure_appending_type(insert, el, insert_rec);
		return 0;
	}

	path = ocfs2_new_inode_path(fs, di);
	if (!path) {
		ret = OCFS2_ET_NO_MEMORY;
		goto out;
	}
	/*
	 * In the case that we're inserting past what the tree
	 * currently accounts for, ocf2_find_path() will return for
	 * us the rightmost tree path. This is accounted for below in
	 * the appending code.
	 */
	ret = ocfs2_find_path(fs, path, insert_rec->e_cpos);
	if (ret)
		goto out;

	el = path_leaf_el(path);

	/*
	 * Now that we have the path, there's two things we want to determine:
	 * 1) Contiguousness (also set contig_index if this is so)
	 *
	 * 2) Are we doing an append? We can trivially break this up
         *     into two types of appends: simple record append, or a
         *     rotate inside the tail leaf.
	 */
	ocfs2_figure_contig_type(fs, insert, el, insert_rec);

	/*
	 * The insert code isn't quite ready to deal with all cases of
	 * left contiguousness. Specifically, if it's an insert into
	 * the 1st record in a leaf, it will require the adjustment of
	 * e_clusters on the last record of the path directly to it's
	 * left. For now, just catch that case and fool the layers
	 * above us. This works just fine for tree_depth == 0, which
	 * is why we allow that above.
	 */
	if (insert->ins_contig == CONTIG_LEFT &&
	    insert->ins_contig_index == 0)
		insert->ins_contig = CONTIG_NONE;

	/*
	 * Ok, so we can simply compare against last_eb to figure out
	 * whether the path doesn't exist. This will only happen in
	 * the case that we're doing a tail append, so maybe we can
	 * take advantage of that information somehow.
	 */
	if (di->i_last_eb_blk == path_leaf_blkno(path)) {
		/*
		 * Ok, ocfs2_find_path() returned us the rightmost
		 * tree path. This might be an appending insert. There are
		 * two cases:
		 *    1) We're doing a true append at the tail:
		 *	-This might even be off the end of the leaf
		 *    2) We're "appending" by rotating in the tail
		 */
		ocfs2_figure_appending_type(insert, el, insert_rec);
	}

out:
	ocfs2_free_path(path);

	if (ret == 0)
		*last_eb_buf = buf;
	else if (buf)
		ocfs2_free(&buf);
	return ret;
}

/*
 * Do the final bits of extent record insertion at the target leaf
 * list. If this leaf is part of an allocation tree, it is assumed
 * that the tree above has been prepared.
 */
static void ocfs2_insert_at_leaf(struct ocfs2_extent_rec *insert_rec,
				 struct ocfs2_extent_list *el,
				 struct ocfs2_insert_type *insert)
{
	int i = insert->ins_contig_index;
	unsigned int range;
	struct ocfs2_extent_rec *rec;

	assert(el->l_tree_depth == 0);

	/*
	 * Contiguous insert - either left or right.
	 */
	if (insert->ins_contig != CONTIG_NONE) {
		rec = &el->l_recs[i];
		if (insert->ins_contig == CONTIG_LEFT) {
			rec->e_blkno = insert_rec->e_blkno;
			rec->e_cpos = insert_rec->e_cpos;
		}
		rec->e_leaf_clusters += insert_rec->e_leaf_clusters;
		return;
	}

	/*
	 * Handle insert into an empty leaf.
	 */
	if (el->l_next_free_rec == 0 ||
	    (el->l_next_free_rec == 1 &&
	     ocfs2_is_empty_extent(&el->l_recs[0]))) {
		el->l_recs[0] = *insert_rec;
		el->l_next_free_rec = 1;
		return;
	}

	/*
	 * Appending insert.
	 */
	if (insert->ins_appending == APPEND_TAIL) {
		i = el->l_next_free_rec - 1;
		rec = &el->l_recs[i];
		range = rec->e_cpos + rec->e_leaf_clusters;
		assert(insert_rec->e_cpos >= range);

		i++;
		el->l_recs[i] = *insert_rec;
		el->l_next_free_rec += 1;
		return;
	}

	/*
	 * Ok, we have to rotate.
	 *
	 * At this point, it is safe to assume that inserting into an
	 * empty leaf and appending to a leaf have both been handled
	 * above.
	 *
	 * This leaf needs to have space, either by the empty 1st
	 * extent record, or by virtue of an l_next_rec < l_count.
	 */
	ocfs2_rotate_leaf(el, insert_rec);
}

static int ocfs2_append_rec_to_path(ocfs2_filesys *fs,
				    struct ocfs2_extent_rec *insert_rec,
				    struct ocfs2_path *right_path,
				    struct ocfs2_path **ret_left_path)
{
	int ret, i, next_free;
	struct ocfs2_extent_list *el;
	struct ocfs2_path *left_path = NULL;

	*ret_left_path = NULL;

	/*
	 * This shouldn't happen for non-trees. The extent rec cluster
	 * count manipulation below only works for interior nodes.
	 */
	assert(right_path->p_tree_depth > 0);

	/*
	 * If our appending insert is at the leftmost edge of a leaf,
	 * then we might need to update the rightmost records of the
	 * neighboring path.
	 */

	el = path_leaf_el(right_path);
	next_free = el->l_next_free_rec;
	if (next_free == 0 ||
	    (next_free == 1 && ocfs2_is_empty_extent(&el->l_recs[0]))) {
		uint32_t left_cpos;

		ret = ocfs2_find_cpos_for_left_leaf(right_path, &left_cpos);
		if (ret)
			goto out;
		/*
		 * No need to worry if the append is already in the
		 * leftmost leaf.
		 */
		if (left_cpos) {
			left_path = ocfs2_new_path(fs,
						   path_root_buf(right_path),
						   path_root_el(right_path));
			if (!left_path) {
				ret = OCFS2_ET_NO_MEMORY;
				goto out;
			}

			ret = ocfs2_find_path(fs, left_path, left_cpos);
			if (ret)
				goto out;
		}
	}

	el = path_root_el(right_path);
	i = 0;
	while (1) {
		struct ocfs2_extent_rec *rec;

		next_free = el->l_next_free_rec;
		if (next_free == 0) {
			ret = OCFS2_ET_CORRUPT_EXTENT_BLOCK;
			goto out;
		}

		rec = &el->l_recs[next_free - 1];
		rec->e_int_clusters = insert_rec->e_cpos;
		rec->e_int_clusters += insert_rec->e_leaf_clusters;
		rec->e_int_clusters -= rec->e_cpos;

		/*
		 * Since we have changed the extent block in the right path,
		 * we have to keep them the same in the left path we found
		 * above.
		 */
		if (left_path && left_path->p_node[i].blkno ==
					right_path->p_node[i].blkno)
			memcpy(left_path->p_node[i].buf,
			       right_path->p_node[i].buf,
			       fs->fs_blocksize);
		/* Don't touch the leaf node */
		if (++i >= right_path->p_tree_depth)
			break;

		el = right_path->p_node[i].el;
	}

	*ret_left_path = left_path;
	ret = 0;
out:
	if (ret)
		ocfs2_free_path(left_path);
	return ret;
}

/*
 * This function only does inserts on an allocation b-tree. For dinode
 * lists, ocfs2_insert_at_leaf() is called directly.
 *
 * right_path is the path we want to do the actual insert
 * in. left_path should only be passed in if we need to update that
 * portion of the tree after an edge insert.
 */
static int ocfs2_insert_path(struct insert_ctxt* ctxt,
			     struct ocfs2_path *left_path,
			     struct ocfs2_path *right_path,
			     struct ocfs2_extent_rec *insert_rec,
			     struct ocfs2_insert_type *insert)
{
	int ret, subtree_index;
	struct ocfs2_extent_list *el;

	el = path_leaf_el(right_path);

	ocfs2_insert_at_leaf(insert_rec, el, insert);

	if (left_path) {
		/*
		 * The rotate code has indicated that we need to fix
		 * up portions of the tree after the insert.
		 */
		subtree_index = ocfs2_find_subtree_root(left_path, right_path);
		ocfs2_complete_edge_insert(ctxt->fs, left_path,
				        right_path, subtree_index);
	} else
		subtree_index = 0;

	ret = ocfs2_sync_path_to_disk(ctxt->fs, left_path,
				      right_path, subtree_index);
	if (ret)
		goto out;

	ret = 0;
out:
	return ret;
}

static int ocfs2_do_insert_extent(struct insert_ctxt* ctxt,
				  struct ocfs2_insert_type *type)
{
	int ret, rotate = 0;
	uint32_t cpos;
	struct ocfs2_path *right_path = NULL;
	struct ocfs2_path *left_path = NULL;
	struct ocfs2_extent_rec *insert_rec = &ctxt->rec;
	ocfs2_filesys *fs = ctxt->fs;
	struct ocfs2_dinode *di = ctxt->di;
	struct ocfs2_extent_list *el = &di->id2.i_list;

	if (el->l_tree_depth == 0) {
		ocfs2_insert_at_leaf(insert_rec, el, type);
		goto out_update_clusters;
	}

	right_path = ocfs2_new_inode_path(fs, di);
	if (!right_path) {
		ret = OCFS2_ET_NO_MEMORY;
		goto out;
	}

	/*
	 * Determine the path to start with. Rotations need the
	 * rightmost path, everything else can go directly to the
	 * target leaf.
	 */
	cpos = insert_rec->e_cpos;
	if (type->ins_appending == APPEND_NONE &&
	    type->ins_contig == CONTIG_NONE) {
		rotate = 1;
		cpos = UINT_MAX;
	}

	ret = ocfs2_find_path(fs, right_path, cpos);
	if (ret)
		goto out;

	/*
	 * Rotations and appends need special treatment - they modify
	 * parts of the tree's above them.
	 *
	 * Both might pass back a path immediate to the left of the
	 * one being inserted to. This will be cause
	 * ocfs2_insert_path() to modify the rightmost records of
	 * left_path to account for an edge insert.
	 *
	 * XXX: When modifying this code, keep in mind that an insert
	 * can wind up skipping both of these two special cases...
	 */

	if (rotate) {
		ret = ocfs2_rotate_tree_right(fs, insert_rec->e_cpos,
					      right_path, &left_path);
		if (ret)
			goto out;
	} else if (type->ins_appending == APPEND_TAIL
		   && type->ins_contig != CONTIG_LEFT) {
		ret = ocfs2_append_rec_to_path(fs, insert_rec,
					       right_path, &left_path);
		if (ret)
			goto out;
 	}

	ret = ocfs2_insert_path(ctxt, left_path, right_path, insert_rec, type);
	if (ret)
		goto out;

out_update_clusters:
	di->i_clusters += insert_rec->e_leaf_clusters;
	ret = 0;

out:
	ocfs2_free_path(left_path);
	ocfs2_free_path(right_path);

	return ret;
}

struct duplicate_ctxt {
	struct ocfs2_dinode *di;
	uint64_t next_leaf_blk;
};

static errcode_t duplicate_extent_block(ocfs2_filesys *fs,
					struct ocfs2_extent_list *old_el,
					struct ocfs2_extent_list *new_el,
					struct duplicate_ctxt *ctxt)
{
	int i;
	errcode_t ret;
	uint64_t blkno, new_blkno;
	struct ocfs2_extent_rec *rec = NULL;
	char *eb_buf = NULL, *new_eb_buf = NULL;
	struct ocfs2_extent_block *eb = NULL;
	struct ocfs2_extent_list *child_old_el = NULL, *child_new_el = NULL;

	assert (old_el->l_tree_depth > 0);

	/* empty the whole extent list at first. */
	*new_el = *old_el;
	new_el->l_next_free_rec = 0;
	memset(new_el->l_recs, 0,
	       sizeof(struct ocfs2_extent_rec) * new_el->l_count);

	if (old_el->l_next_free_rec == 0) {
		/* XXX:
		 * We have a tree depth > 0 and no extent record in it,
		 * should it be a corrupted block?
		 */
		ret = OCFS2_ET_CORRUPT_EXTENT_BLOCK;
		goto bail;
	}

	ret = ocfs2_malloc_block(fs->fs_io, &eb_buf);
	if (ret)
		goto bail;
	ret = ocfs2_malloc_block(fs->fs_io, &new_eb_buf);
	if (ret)
		goto bail;

	/* we iterate the extent list from the last one for recording
	 * the next_leaf_blk for the previous leaf.
	 */
	for (i = old_el->l_next_free_rec - 1; i >= 0; i--) {
		rec = &old_el->l_recs[i];

		if (!ocfs2_rec_clusters(old_el->l_tree_depth, rec))
			continue;

		blkno = rec->e_blkno;
		ret = ocfs2_read_extent_block(fs, blkno, eb_buf);
		if (ret)
			goto bail;

		/* First make the new_buf the same as the old buf. */
		memcpy(new_eb_buf, eb_buf, fs->fs_blocksize);

		eb = (struct ocfs2_extent_block *)eb_buf;
		child_old_el = &eb->h_list;
		eb = (struct ocfs2_extent_block *)new_eb_buf;
		child_new_el = &eb->h_list;

		if (child_old_el->l_tree_depth > 0) {
			/* the extent record in our list still has child extent
			 * block, so we have to iterate it.
			 */
			ret = duplicate_extent_block(fs,
						     child_old_el,
						     child_new_el,
						     ctxt);
			if (ret)
				goto bail;
		}

		/* now we allocate a new extent block and save it. */
		ret = ocfs2_new_extent_block(fs, &new_blkno);
		if (ret)
			goto bail;

		eb = (struct ocfs2_extent_block *)new_eb_buf;
		eb->h_blkno = new_blkno;
		if (child_old_el->l_tree_depth == 0) {
			/*
			 * This is the leaf blkno, we have to set its
			 * h_next_leaf_blk and then record itself for
			 * future use.
			 */
			eb->h_next_leaf_blk = ctxt->next_leaf_blk;
			ctxt->next_leaf_blk = new_blkno;
		}

		ret = ocfs2_write_extent_block(fs, new_blkno, new_eb_buf);
		if (ret)
			goto bail;

		memcpy(&new_el->l_recs[i], rec, sizeof(struct ocfs2_extent_rec));
		new_el->l_recs[i].e_blkno = new_blkno;

		eb = (struct ocfs2_extent_block *)new_eb_buf;
		/* set the new i_last_eb_blk in the new dinode. */
		if (ctxt->di->i_last_eb_blk == blkno)
			ctxt->di->i_last_eb_blk = new_blkno;
	}

	new_el->l_next_free_rec = old_el->l_next_free_rec;
	ret = 0;

bail:
	if (eb_buf)
		ocfs2_free(&eb_buf);
	if (new_eb_buf)
		ocfs2_free(&new_eb_buf);
	/* Free all the extent block we allocate. */
	if (ret) {
		for (i = 0; i < old_el->l_next_free_rec; i++) {
			rec = &new_el->l_recs[i];
			if (rec->e_blkno)
				ocfs2_delete_extent_block(fs, rec->e_blkno);
		}
	}

	return ret;
}

static errcode_t duplicate_extent_block_dinode(ocfs2_filesys *fs,
					       char *old_buf, char *new_buf)
{
	errcode_t ret = 0;
	struct ocfs2_dinode *old_di = NULL, *new_di = NULL;
	struct ocfs2_extent_list *old_el = NULL, *new_el = NULL;
	struct duplicate_ctxt ctxt;

	old_di = (struct ocfs2_dinode *)old_buf;
	old_el = &old_di->id2.i_list;
	new_di = (struct ocfs2_dinode *)new_buf;
	new_el = &new_di->id2.i_list;

	assert(old_el->l_tree_depth > 0);

	/* empty the whole extent list at first. */
	*new_el = *old_el;
	memset(new_el->l_recs, 0,
	       sizeof(struct ocfs2_extent_rec) * new_el->l_count);
	new_el->l_next_free_rec = 0;

	memset(&ctxt, 0, sizeof(ctxt));
	ctxt.di = new_di;
	ctxt.next_leaf_blk = 0;
	ret = duplicate_extent_block(fs, old_el, new_el, &ctxt);

	return ret;
}

static void free_duplicated_extent_block(ocfs2_filesys *fs,
					struct ocfs2_extent_list *el)
{
	int i;
	errcode_t ret;
	char *buf = NULL;
	struct ocfs2_extent_rec *rec;
	struct ocfs2_extent_list *child_el;
	struct ocfs2_extent_block *eb;

	assert(el->l_tree_depth > 0);

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return;

	for (i = 0; i < el->l_next_free_rec; i ++) {
		rec = &el->l_recs[i];

		if (!ocfs2_rec_clusters(el->l_tree_depth, rec))
			continue;

		ret = ocfs2_read_extent_block(fs, rec->e_blkno, buf);
		if (ret)
			continue;

		eb = (struct ocfs2_extent_block *)buf;
		child_el = &eb->h_list;
		if (child_el->l_tree_depth > 0)
			free_duplicated_extent_block(fs, child_el);

		ocfs2_delete_extent_block(fs, rec->e_blkno);
	}

	if(buf)
		ocfs2_free(&buf);
}

static void free_duplicated_extent_block_dinode(ocfs2_filesys *fs,
						char *di_buf)
{
	struct ocfs2_dinode *di = NULL;
	struct ocfs2_extent_list *el = NULL;

	di = (struct ocfs2_dinode *)di_buf;
	el = &di->id2.i_list;

	assert(el->l_tree_depth > 0);

	free_duplicated_extent_block(fs, el);
}

/*
 * Grow a b-tree so that it has more records.
 *
 * We might shift the tree depth in which case existing paths should
 * be considered invalid.
 *
 * Tree depth after the grow is returned via *final_depth.
 */
static int ocfs2_grow_tree(ocfs2_filesys *fs, struct ocfs2_dinode *di,
			   int *final_depth, char *last_eb)
{
	errcode_t ret;
	char *eb_buf = NULL;
	int shift;
	int depth = di->id2.i_list.l_tree_depth;

	shift = ocfs2_find_branch_target(fs, di, &eb_buf);
	if (shift < 0) {
		ret = shift;
		goto out;
	}

	/* We traveled all the way to the bottom of the allocation tree
	 * and didn't find room for any more extents - we need to add
	 * another tree level */
	if (shift) {

		/* shift_tree_depth will return us a buffer with
		 * the new extent block (so we can pass that to
		 * ocfs2_add_branch). */
		ret = shift_tree_depth(fs, di, &eb_buf);
		if (ret)
			goto out;

		depth++;
		/*
		 * Special case: we have room now if we shifted from
		 * tree_depth 0, so no more work needs to be done.
		 */
		if (depth == 1)
			goto out;
	}

	/* call ocfs2_add_branch to add the final part of the tree with
	 * the new data. */
	ret = ocfs2_add_branch(fs, di, eb_buf, last_eb);

out:
	if (final_depth)
		*final_depth = depth;
	return ret;
}

/*
 * Insert an extent into an inode btree.
 */
errcode_t ocfs2_insert_extent(ocfs2_filesys *fs, uint64_t ino, uint32_t cpos,
			      uint64_t c_blkno, uint32_t clusters)
{
	errcode_t ret;
	struct insert_ctxt ctxt;
	struct ocfs2_insert_type insert = {0, };
	char *di_buf = NULL, *last_eb = NULL;
	char *backup_buf = NULL;

	ret = ocfs2_malloc_block(fs->fs_io, &di_buf);
	if (ret)
		return ret;

	ctxt.fs = fs;
	ctxt.di = (struct ocfs2_dinode *)di_buf;

	ret = ocfs2_read_inode(fs, ino, di_buf);
	if (ret)
		goto bail;

	/* In order to orderize the written block sequence and avoid
	 * the corruption for the inode, we duplicate the extent block
	 * here and do the insertion in the duplicated ones.
	 *
	 * Note: we only do this in case the file has extent blocks.
	 * And if the duplicate process fails, we should go on the normal
	 * insert process.
	 */
	if (ctxt.di->id2.i_list.l_tree_depth) {
		ret = ocfs2_malloc_block(fs->fs_io, &backup_buf);
		if (ret)
			goto bail;

		memcpy(backup_buf, di_buf, fs->fs_blocksize);

		/* duplicate the extent block. If it succeeds, di_buf
		 * will point to the new allocated extent blocks, and
		 * the following insertion will happens to the new ones.
		 */
		ret = duplicate_extent_block_dinode(fs, backup_buf, di_buf);
		if (ret) {
			memcpy(di_buf, backup_buf,fs->fs_blocksize);
			ocfs2_free(&backup_buf);
			backup_buf = NULL;
		}
	}

	memset(&ctxt.rec, 0, sizeof(struct ocfs2_extent_rec));
	ctxt.rec.e_cpos = cpos;
	ctxt.rec.e_blkno = c_blkno;
	ctxt.rec.e_leaf_clusters = clusters;

	ret = ocfs2_figure_insert_type(&ctxt,&last_eb, &insert);
	if (ret)
		goto bail;

	if (insert.ins_contig == CONTIG_NONE && insert.ins_free_records == 0) {
		ret = ocfs2_grow_tree(fs, ctxt.di,
				      &insert.ins_tree_depth, last_eb);
		if (ret)
			goto bail;
	}

	/* Finally, we can add clusters. This might rotate the tree for us. */
	ret = ocfs2_do_insert_extent(&ctxt, &insert);
	if (ret)
		goto bail;

	ret = ocfs2_write_inode(fs, ino, di_buf);

bail:
	if (backup_buf) {
		/* we have duplicated the extent block during the insertion.
		 * so if it succeeds, we should free the old ones, and if fails,
		 * the duplicate ones should be freed.
		 */
		if (ret)
			free_duplicated_extent_block_dinode(fs, di_buf);
		else
			free_duplicated_extent_block_dinode(fs, backup_buf);
		ocfs2_free(&backup_buf);
	}

	if (last_eb)
		ocfs2_free(&last_eb);
	if (di_buf)
		ocfs2_free(&di_buf);

	return ret;
}

errcode_t ocfs2_extend_allocation(ocfs2_filesys *fs, uint64_t ino,
				  uint32_t new_clusters)
{
	errcode_t ret = 0;
	uint32_t n_clusters = 0, cpos;
	uint64_t blkno, file_size;
	char *buf = NULL;
	struct ocfs2_dinode* di = NULL;

	if (!(fs->fs_flags & OCFS2_FLAG_RW))
		return OCFS2_ET_RO_FILESYS;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		goto out_free_buf;

	ret = ocfs2_read_inode(fs, ino, buf);
	if (ret)
		goto out_free_buf;

	di = (struct ocfs2_dinode *)buf;

	file_size = di->i_size;
	cpos = (file_size + fs->fs_clustersize - 1) / fs->fs_clustersize;
	while (new_clusters) {
		n_clusters = 1;
		ret = ocfs2_new_clusters(fs, 1, new_clusters, &blkno,
					 &n_clusters);
		if (ret)
			break;

	 	ret = ocfs2_insert_extent(fs, ino, cpos, blkno, n_clusters);
		if (ret) {
			/* XXX: We don't wan't to overwrite the error
			 * from insert_extent().  But we probably need
			 * to BE LOUDLY UPSET. */
			ocfs2_free_clusters(fs, n_clusters, blkno);
			goto out_free_buf;
		}

	 	new_clusters -= n_clusters;
		cpos += n_clusters;
	}

out_free_buf:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

errcode_t ocfs2_extend_file(ocfs2_filesys *fs, uint64_t ino, uint64_t new_size)
{
	errcode_t ret = 0;
	char *buf = NULL;
	struct ocfs2_dinode* di = NULL;

	if (!(fs->fs_flags & OCFS2_FLAG_RW))
		return OCFS2_ET_RO_FILESYS;
	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		return ret;

	ret = ocfs2_read_inode(fs, ino, buf);
	if (ret)
		goto out_free_buf;

	di = (struct ocfs2_dinode *)buf;
	if (di->i_size >= new_size) {
		ret = EINVAL;
		goto out_free_buf;
	}

	di->i_size = new_size;

	ret = ocfs2_write_inode(fs, ino, buf);

out_free_buf:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

#ifdef DEBUG_EXE
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <inttypes.h>

static void print_usage(void)
{
	fprintf(stdout, "debug_extend_file -i <ino> -c <clusters> <device>\n");
}

static uint64_t read_number(const char *num)
{
	uint64_t val;
	char *ptr;

	val = strtoull(num, &ptr, 0);
	if (!ptr || *ptr)
		return 0;

	return val;
}

extern int opterr, optind;
extern char *optarg;

int main(int argc, char *argv[])
{
	errcode_t ret;
	char *filename;
	ocfs2_filesys *fs;
	uint64_t ino = 0;
	uint32_t new_clusters = 0;
	int c;

	initialize_ocfs_error_table();

	while ((c = getopt(argc, argv, "i:c:")) != EOF) {
		switch (c) {
			case 'i':
				ino = read_number(optarg);
				if (ino <= OCFS2_SUPER_BLOCK_BLKNO) {
					fprintf(stderr,
						"Invalid inode block: %s\n",
						optarg);
					print_usage();
					return 1;
				}
				break;

			case 'c':
				new_clusters = read_number(optarg);
				if (!new_clusters) {
					fprintf(stderr,
						"Invalid cluster count: %s\n",
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

	if (!ino) {
		fprintf(stderr, "You must specify an inode block\n");
		print_usage();
		return 1;
	}

	if (!new_clusters) {
		fprintf(stderr, "You must specify how many clusters to extend\n");
		print_usage();
		return 1;
	}

	if (optind >= argc) {
		fprintf(stderr, "Missing filename\n");
		print_usage();
		return 1;
	}
	filename = argv[optind];

	ret = ocfs2_open(filename, OCFS2_FLAG_RW, 0, 0, &fs);
	if (ret) {
		com_err(argv[0], ret,
			"while opening file \"%s\"", filename);
		goto out;
	}

	ret = ocfs2_extend_allocation(fs, ino, new_clusters);
	if (ret) {
		com_err(argv[0], ret,
			"while extending inode %"PRIu64, ino);
	}

	ret = ocfs2_close(fs);
	if (ret) {
		com_err(argv[0], ret,
			"while closing file \"%s\"", filename);
	}
out:
	return !!ret;
}
#endif  /* DEBUG_EXE */
