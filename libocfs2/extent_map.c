/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * extent_map.c
 *
 * In-memory extent map for the OCFS2 userspace library.
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
#include <inttypes.h>

#include "ocfs2.h"

#include "extent_map.h"

struct extent_map_context {
	ocfs2_cached_inode *cinode;
	errcode_t errcode;
};

/*
 * Find an entry in the tree that intersects the region passed in.
 * Note that this will find straddled intervals, it is up to the
 * callers to enforce any boundary conditions.
 *
 * The rb_node garbage lets insertion share the search.  Trivial
 * callers pass NULL.
 */
static ocfs2_extent_map_entry *
ocfs2_extent_map_lookup(ocfs2_extent_map *em,
			uint32_t cpos, uint32_t clusters,
			struct rb_node ***ret_p,
			struct rb_node **ret_parent)
{
	struct rb_node **p = &em->em_extents.rb_node;
	struct rb_node *parent = NULL;
	ocfs2_extent_map_entry *ent = NULL;

	while (*p)
	{
		parent = *p;
		ent = rb_entry(parent, ocfs2_extent_map_entry, e_node); if ((cpos + clusters) <= ent->e_rec.e_cpos) {
			p = &(*p)->rb_left;
			ent = NULL;
		} else if (cpos >= (ent->e_rec.e_cpos +
				    ent->e_rec.e_clusters)) {
			p = &(*p)->rb_right;
			ent = NULL;
		} else
			break;
	}

	if (ret_p != NULL)
		*ret_p = p;
	if (ret_parent != NULL)
		*ret_parent = parent;
	return ent;
}

static errcode_t ocfs2_extent_map_find_leaf(ocfs2_cached_inode *cinode,
					    uint32_t cpos,
					    uint32_t clusters,
					    struct ocfs2_extent_list *el)
{
	errcode_t ret;
	int i;
	char *eb_buf = NULL;
	uint64_t blkno;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_rec *rec;

	if (el->l_tree_depth) {
		ret = ocfs2_malloc_block(cinode->ci_fs->fs_io, &eb_buf);
		if (ret)
			return ret;
	}

	while (el->l_tree_depth)
	{
		blkno = 0;
		for (i = 0; i < el->l_next_free_rec; i++) {
			rec = &el->l_recs[i];

			ret = OCFS2_ET_CORRUPT_EXTENT_BLOCK;
			if (rec->e_cpos >=
			    cinode->ci_inode->i_clusters)
				goto out_free;

			if ((rec->e_cpos + rec->e_clusters) <= cpos) {
				ret = ocfs2_extent_map_insert(cinode,
							      rec,
							      el->l_tree_depth);
				if (ret)
					goto out_free;
				continue;
			}
			if ((cpos + clusters) <= rec->e_cpos) {
				ret = ocfs2_extent_map_insert(cinode,
							      rec,
							      el->l_tree_depth);
				if (ret)
					goto out_free;
				continue;
			}
			
			/* Check to see if we're stradling */
			ret = OCFS2_ET_INVALID_EXTENT_LOOKUP;
			if ((rec->e_cpos > cpos) ||
			    ((cpos + clusters) >
			     (rec->e_cpos + rec->e_clusters)))
				goto out_free;

			/*
			 * We don't insert this record because we're
			 * about to traverse it
			 */

			ret = OCFS2_ET_CORRUPT_EXTENT_BLOCK;
			if (blkno)
				goto out_free;
			blkno = rec->e_blkno;
		}

		/*
		 * We don't support holes, and we're still up
		 * in the branches, so we'd better have found someone
		 */
		ret = OCFS2_ET_CORRUPT_EXTENT_BLOCK;
		if (!blkno)
			goto out_free;

		ret = ocfs2_read_extent_block(cinode->ci_fs,
					      blkno, eb_buf);
		if (ret)
			goto out_free;

		eb = (struct ocfs2_extent_block *)eb_buf;
		el = &eb->h_list;
	}

	if (el->l_tree_depth)
		abort();

	for (i = 0; i < el->l_next_free_rec; i++) {
		rec = &el->l_recs[i];
		ret = ocfs2_extent_map_insert(cinode, rec,
					      el->l_tree_depth);
		if (ret)
			goto out_free;
	}

	ret = 0;

out_free:
	if (eb_buf)
		ocfs2_free(&eb_buf);

	return ret;
}

/*
 * This lookup actually will read from disk.  It has one invariant:
 * It will never re-traverse blocks.  This means that all inserts should
 * be new regions or more granular regions (both allowed by insert).
 */
static errcode_t ocfs2_extent_map_lookup_read(ocfs2_cached_inode *cinode,
				      uint32_t cpos,
				      uint32_t clusters,
				      ocfs2_extent_map_entry **ret_ent)
{
	errcode_t ret;
	ocfs2_extent_map_entry *ent;
	char *eb_buf = NULL;
	ocfs2_extent_map *em = cinode->ci_map;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_list *el;

	ent = ocfs2_extent_map_lookup(em, cpos, clusters, NULL, NULL);
	if (ent) {
		if (!ent->e_tree_depth) {
			*ret_ent = ent;
			return 0;
		}

		ret = ocfs2_malloc_block(cinode->ci_fs->fs_io,
					 &eb_buf);
		if (ret)
			return ret;

		ret = ocfs2_read_extent_block(cinode->ci_fs,
					      ent->e_rec.e_blkno,
					      eb_buf);
		if (ret) {
			ocfs2_free(&eb_buf);
			return ret;
		}

		eb = (struct ocfs2_extent_block *)eb_buf;
		el = &eb->h_list;
	} else 
		el = &cinode->ci_inode->id2.i_list;

	ret = ocfs2_extent_map_find_leaf(cinode, cpos, clusters, el);
	if (eb_buf)
		ocfs2_free(&eb_buf);
	if (ret)
		return ret;

	ent = ocfs2_extent_map_lookup(em, cpos, clusters, NULL, NULL);
	if (!ent || ent->e_tree_depth)
		return OCFS2_ET_CORRUPT_EXTENT_BLOCK;

	*ret_ent = ent;

	return 0;
}

static errcode_t ocfs2_extent_map_insert_entry(ocfs2_extent_map *em,
					       ocfs2_extent_map_entry *ent)
{
	struct rb_node **p, *parent;
	ocfs2_extent_map_entry *old_ent;
	
	old_ent = ocfs2_extent_map_lookup(em, ent->e_rec.e_cpos,
					  ent->e_rec.e_clusters,
					  &p, &parent);
	if (old_ent)
		return OCFS2_ET_INVALID_EXTENT_LOOKUP;

	rb_link_node(&ent->e_node, parent, p);
	rb_insert_color(&ent->e_node, &em->em_extents);

	return 0;
}

errcode_t ocfs2_extent_map_insert(ocfs2_cached_inode *cinode,
				  struct ocfs2_extent_rec *rec,
				  int tree_depth)
{
	errcode_t ret;
	ocfs2_extent_map *em = cinode->ci_map;
	ocfs2_extent_map_entry *old_ent, *new_ent;
	ocfs2_extent_map_entry *left_ent = NULL, *right_ent = NULL;

	if (!em)
		return OCFS2_ET_INVALID_ARGUMENT;

	if ((rec->e_cpos + rec->e_clusters) > em->em_clusters)
		return OCFS2_ET_INVALID_EXTENT_LOOKUP;

	ret = ocfs2_malloc0(sizeof(struct _ocfs2_extent_map_entry),
			    &new_ent);
	if (ret)
		return ret;

	new_ent->e_rec = *rec;
	new_ent->e_tree_depth = tree_depth;
	ret = ocfs2_extent_map_insert_entry(em, new_ent);
	if (!ret)
		return 0;

	ret = OCFS2_ET_INTERNAL_FAILURE;
	old_ent = ocfs2_extent_map_lookup(em, rec->e_cpos,
					  rec->e_clusters, NULL, NULL);

	if (!old_ent)
		goto out_free;

	ret = OCFS2_ET_INVALID_EXTENT_LOOKUP;
	if (old_ent->e_tree_depth < tree_depth)
		goto out_free;
	if (old_ent->e_tree_depth == tree_depth) {
		if (!memcmp(rec, &old_ent->e_rec,
			    sizeof(struct ocfs2_extent_rec)))
			ret = 0;  /* Same entry, just skip */
		goto out_free;
	}

	/*
	 * We do it in this order specifically so that malloc failures
	 * do not leave an inconsistent tree.
	 */
	if (rec->e_cpos > old_ent->e_rec.e_cpos) {
		ret = ocfs2_malloc0(sizeof(struct _ocfs2_extent_map_entry),
				    &left_ent);
		if (ret)
			goto out_free;
		*left_ent = *old_ent;
		left_ent->e_rec.e_clusters =
			rec->e_cpos - left_ent->e_rec.e_cpos;
	}
	if ((old_ent->e_rec.e_cpos +
	     old_ent->e_rec.e_clusters) > 
	    (rec->e_cpos + rec->e_clusters)) {
		ret = ocfs2_malloc0(sizeof(struct _ocfs2_extent_map_entry),
				    &right_ent);
		if (ret)
			goto out_free;
		*right_ent = *old_ent;
		right_ent->e_rec.e_cpos =
			rec->e_cpos + rec->e_clusters;
		right_ent->e_rec.e_clusters =
			(old_ent->e_rec.e_cpos +
			 old_ent->e_rec.e_clusters) -
			right_ent->e_rec.e_cpos;
	}

	rb_erase(&old_ent->e_node, &em->em_extents);

	if (left_ent) {
		ret = ocfs2_extent_map_insert_entry(em,
						    left_ent);
		if (ret)
			goto out_free;
		left_ent = NULL;
	}

	ret = ocfs2_extent_map_insert_entry(em, new_ent);
	if (ret)
		goto out_free;
	new_ent = NULL;

	if (right_ent) {
		ret = ocfs2_extent_map_insert_entry(em,
						    right_ent);
		if (ret)
			goto out_free;
	}

	ocfs2_free(&old_ent);

	return 0;

out_free:
	if (left_ent)
		ocfs2_free(&left_ent);
	if (right_ent)
		ocfs2_free(&right_ent);
	if (new_ent)
		ocfs2_free(&new_ent);

	return ret;
}


/*
 * Look up the record containing this cluster offset.  This record is
 * part of the extent map.  Do not free it.  Any changes you make to
 * it will reflect in the extent map.  So, if your last extent
 * is (cpos = 10, clusters = 10) and you truncate the file by 5
 * clusters, you want to do:
 *
 * ret = ocfs2_extent_map_get_rec(em, orig_size - 5, &rec);
 * rec->e_clusters -= 5;
 */
errcode_t ocfs2_extent_map_get_rec(ocfs2_cached_inode *cinode,
				   uint32_t cpos,
				   struct ocfs2_extent_rec **rec)
{
	errcode_t ret = OCFS2_ET_EXTENT_NOT_FOUND;
	ocfs2_extent_map *em = cinode->ci_map;
	ocfs2_extent_map_entry *ent = NULL;

	*rec = NULL;

	if (!em)
		return OCFS2_ET_INVALID_ARGUMENT;

	if (cpos >= cinode->ci_inode->i_clusters)
		return OCFS2_ET_INVALID_EXTENT_LOOKUP;

	ent = ocfs2_extent_map_lookup(em, cpos, 1, NULL, NULL);
	
	if (ent) {
		*rec = &ent->e_rec;
		ret = 0;
	}

	return ret;
}

errcode_t ocfs2_extent_map_get_clusters(ocfs2_cached_inode *cinode,
					uint32_t v_cpos, int count,
					uint32_t *p_cpos,
					int *ret_count)
{
	errcode_t ret;
	uint32_t coff, ccount;
	ocfs2_extent_map_entry *ent = NULL;
	ocfs2_filesys *fs = cinode->ci_fs;

	*p_cpos = ccount = 0;

	if (!cinode->ci_map)
		return OCFS2_ET_INVALID_ARGUMENT;

	if ((v_cpos + count) > cinode->ci_map->em_clusters)
		return OCFS2_ET_INVALID_EXTENT_LOOKUP;

	ret = ocfs2_extent_map_lookup_read(cinode, v_cpos, count, &ent);
	if (ret)
		return ret;

	if (ent) {
		/* We should never find ourselves straddling an interval */
		if ((ent->e_rec.e_cpos > v_cpos) ||
		    ((v_cpos + count) >
		     (ent->e_rec.e_cpos + ent->e_rec.e_clusters)))
			return OCFS2_ET_INVALID_EXTENT_LOOKUP;

		coff = v_cpos - ent->e_rec.e_cpos;
		*p_cpos = ocfs2_blocks_to_clusters(fs,
						   ent->e_rec.e_blkno) +
			coff;

		if (ret_count)
			*ret_count = ent->e_rec.e_clusters - coff;

		return 0;
	}


	return OCFS2_ET_EXTENT_NOT_FOUND;
}

errcode_t ocfs2_extent_map_get_blocks(ocfs2_cached_inode *cinode,
				      uint64_t v_blkno, int count,
				      uint64_t *p_blkno, uint64_t *ret_count)
{
	errcode_t ret;
	uint64_t boff;
	uint32_t cpos, clusters;
	ocfs2_filesys *fs = cinode->ci_fs;
	int bpc = ocfs2_clusters_to_blocks(fs, 1);
	ocfs2_extent_map_entry *ent = NULL;
	struct ocfs2_extent_rec *rec;

	*p_blkno = 0;

	if (!cinode->ci_map)
		return OCFS2_ET_INVALID_ARGUMENT;

	cpos = ocfs2_blocks_to_clusters(fs, v_blkno);
	clusters = ocfs2_blocks_to_clusters(fs,
					    (uint64_t)count + bpc - 1);
	if ((cpos + clusters) > cinode->ci_map->em_clusters)
		return OCFS2_ET_INVALID_EXTENT_LOOKUP;

	ret = ocfs2_extent_map_lookup_read(cinode, cpos, clusters, &ent);
	if (ret)
		return ret;

	if (ent)
	{
		rec = &ent->e_rec;

		/* We should never find ourselves straddling an interval */
		if ((rec->e_cpos > cpos) ||
		    ((cpos + clusters) >
		     (rec->e_cpos + rec->e_clusters)))
			return OCFS2_ET_INVALID_EXTENT_LOOKUP;

		boff = ocfs2_clusters_to_blocks(fs, cpos - rec->e_cpos);
		boff += (v_blkno % bpc);
		*p_blkno = rec->e_blkno + boff;

		if (ret_count) {
			*ret_count = ocfs2_clusters_to_blocks(fs,
							      rec->e_clusters) - boff;
		}

		return 0;
	}

	return OCFS2_ET_EXTENT_NOT_FOUND;
}

errcode_t ocfs2_extent_map_init(ocfs2_filesys *fs,
				ocfs2_cached_inode *cinode)
{
	errcode_t ret;

	ret = ocfs2_malloc0(sizeof(struct _ocfs2_extent_map),
			    &cinode->ci_map);
	if (ret)
		return ret;

	cinode->ci_map->em_clusters = cinode->ci_inode->i_clusters;
	cinode->ci_map->em_extents = RB_ROOT;

	return 0;
}

void ocfs2_extent_map_free(ocfs2_cached_inode *cinode)
{
	if (!cinode->ci_map)
		return;

	ocfs2_extent_map_drop(cinode, 0);
	ocfs2_free(&cinode->ci_map);
}


static int extent_map_func(ocfs2_filesys *fs,
			   struct ocfs2_extent_rec *rec,
		  	   int tree_depth,
			   uint32_t ccount,
			   uint64_t ref_blkno,
			   int ref_recno,
			   void *priv_data)
{
	errcode_t ret;
	int iret = 0;
	struct extent_map_context *ctxt = priv_data;

	if (rec->e_cpos >= ctxt->cinode->ci_inode->i_clusters) {
		ctxt->errcode = OCFS2_ET_CORRUPT_EXTENT_BLOCK;
		iret |= OCFS2_EXTENT_ABORT;
	} else {
		ret = ocfs2_extent_map_insert(ctxt->cinode, rec,
					      tree_depth);
		if (ret) {
			ctxt->errcode = ret;
			iret |= OCFS2_EXTENT_ABORT;
		}
	}

	return iret;
}

errcode_t ocfs2_load_extent_map(ocfs2_filesys *fs,
				ocfs2_cached_inode *cinode)
{
	errcode_t ret;
	struct extent_map_context ctxt;

	if (!cinode)
		return OCFS2_ET_INVALID_ARGUMENT;

	ret = ocfs2_extent_map_init(fs, cinode);
	if (ret)
		return ret;

	ctxt.cinode = cinode;
	ctxt.errcode = 0;

	ret = ocfs2_extent_iterate(fs, cinode->ci_blkno, 0, NULL,
				   extent_map_func, &ctxt);
	if (ret)
		goto cleanup;

	if (ctxt.errcode) {
		ret = ctxt.errcode;
		goto cleanup;
	}

	return 0;

cleanup:
	ocfs2_extent_map_free(cinode);

	return ret;
}

static void __ocfs2_extent_map_drop(ocfs2_cached_inode  *cinode,
				    uint32_t new_clusters,
				    struct rb_node **free_head,
				    ocfs2_extent_map_entry **tail_ent)
{
	struct rb_node *node, *next;
	ocfs2_extent_map *em = cinode->ci_map;
	ocfs2_extent_map_entry *ent;

	*free_head = NULL;

	ent = NULL;
	node = rb_last(&em->em_extents);
	while (node)
	{
		next = rb_prev(node);

		ent = rb_entry(node, ocfs2_extent_map_entry,
			       e_node);
		if (ent->e_rec.e_cpos < new_clusters)
			break;

		rb_erase(&ent->e_node, &em->em_extents);

		node->rb_right = *free_head;
		*free_head = node;

		ent = NULL;
		node = next;
	}

	/* Do we have an entry straddling new_clusters? */
	if (tail_ent) {
		if (ent &&
		    ((ent->e_rec.e_cpos + ent->e_rec.e_clusters) >
		     new_clusters))
			*tail_ent = ent;
		else
			*tail_ent = NULL;
	}

	return;
}

static void __ocfs2_extent_map_drop_cleanup(struct rb_node *free_head)
{
	struct rb_node *node;
	ocfs2_extent_map_entry *ent;

	while (free_head) {
		node = free_head;
		free_head = node->rb_right;

		ent = rb_entry(node, ocfs2_extent_map_entry,
			       e_node);
		ocfs2_free(&ent);
	}
}


/*
 * Remove all entries past new_clusters, inclusive of an entry that
 * contains new_clusters.  This is effectively a cache forget.
 *
 * If you want to also clip the last extent by some number of clusters,
 * you need to call ocfs2_extent_map_trunc().
 */
errcode_t ocfs2_extent_map_drop(ocfs2_cached_inode *cinode,
				uint32_t new_clusters)
{
	struct rb_node *free_head = NULL;
	ocfs2_extent_map *em = cinode->ci_map;
	ocfs2_extent_map_entry *ent;

	if (!em)
		return OCFS2_ET_INVALID_ARGUMENT;

	__ocfs2_extent_map_drop(cinode, new_clusters, &free_head, &ent);

	if (ent) {
		rb_erase(&ent->e_node, &em->em_extents);
		ent->e_node.rb_right = free_head;
		free_head = &ent->e_node;
	}

	if (free_head)
		__ocfs2_extent_map_drop_cleanup(free_head);

	return 0;
}

/*
 * Remove all entries past new_clusters and also clip any extent
 * straddling new_clusters, if there is one.
 */
errcode_t ocfs2_extent_map_trunc(ocfs2_cached_inode *cinode,
				 uint32_t new_clusters)
{
	struct rb_node *free_head = NULL;
	ocfs2_extent_map_entry *ent = NULL;

	__ocfs2_extent_map_drop(cinode, new_clusters, &free_head, &ent);

	if (ent)
		ent->e_rec.e_clusters =
			new_clusters - ent->e_rec.e_cpos;

	if (free_head)
		__ocfs2_extent_map_drop_cleanup(free_head);

	return 0;
}


#ifdef DEBUG_EXE
#include <stdlib.h>
#include <getopt.h>
#include <limits.h>

enum debug_op {
	OP_NONE = 0,
	OP_WALK,
	OP_LOOKUP_CLUSTER,
	OP_LOOKUP_BLOCK,
	OP_LOOKUP_REC,
};

static uint64_t read_number(const char *num)
{
	uint64_t val;
	char *ptr;

	val = strtoull(num, &ptr, 0);
	if (!ptr || *ptr)
		return 0;

	return val;
}

static int read_b_numbers(const char *num, uint64_t *blkno, int *count)
{
	uint64_t val;
	char *ptr;

	val = strtoull(num, &ptr, 0);
	if (!ptr)
		return 1;
	if (*ptr != ':')
		return 1;
	*blkno = val;

	ptr++;

	val = strtoull(ptr, &ptr, 0);
	if (!ptr || *ptr)
		return 1;
	if (val > INT_MAX)
		return 1;
	*count = (int)val;

	return 0;
}

static int read_c_numbers(const char *num, uint32_t *cpos, int *count)
{
	uint64_t val;
	char *ptr;

	val = strtoull(num, &ptr, 0);
	if (!ptr)
		return 1;
	if (*ptr != ':')
		return 1;
	if (val > UINT32_MAX)
		return 1;
	*cpos = (uint32_t)val;

	ptr++;

	val = strtoull(ptr, &ptr, 0);
	if (!ptr || *ptr)
		return 1;
	if (val > INT_MAX)
		return 1;
	*count = (int)val;

	return 0;
}

static void print_usage(void)
{
	fprintf(stderr,
		"Usage: extent_map -i <inode_blkno> -w <filename>\n"
		"       extent_map -i <inode_blkno> -b <blkno>:<blocks> <filename>\n"
		"       extent_map -i <inode_blkno> -c <cpos>:<clusters> <filename>\n"
		"       extent_map -i <inode_blkno> -r <cpos> <filename>\n");
}

static int walk_extents_func(ocfs2_filesys *fs,
			     ocfs2_cached_inode *cinode, int op)
{
	ocfs2_extent_map *em;
	struct rb_node *node;
	uint32_t ccount;
	ocfs2_extent_map_entry *ent;
	int i;

	em = cinode->ci_map;

	fprintf(stdout, "EXTENTS:\n");

	ccount = 0;

	for (node = rb_first(&em->em_extents); node; node = rb_next(node)) {
		ent = rb_entry(node, ocfs2_extent_map_entry, e_node);

		if (op == OP_WALK) {
			fprintf(stdout,
				"(%08"PRIu32", %08"PRIu32", %08"PRIu64") |"
				" + %08"PRIu32" = %08"PRIu32" / %08"PRIu32"\n",
				ent->e_rec.e_cpos,
				ent->e_rec.e_clusters,
				ent->e_rec.e_blkno, ccount,
				ccount + ent->e_rec.e_clusters,
				cinode->ci_inode->i_clusters);

			ccount += ent->e_rec.e_clusters;
		} else {
			fprintf(stdout, "@%d: ",
				ent->e_tree_depth);

			for (i = cinode->ci_inode->id2.i_list.l_tree_depth;
			     i > ent->e_tree_depth; i--)
				fprintf(stdout, "  ");

			fprintf(stdout,
				"(%08"PRIu32", %08"PRIu32", %09"PRIu64")\n",
				ent->e_rec.e_cpos,
				ent->e_rec.e_clusters,
				ent->e_rec.e_blkno);
		}
	}

	if (op == OP_WALK)
		fprintf(stdout, "TOTAL: %"PRIu32"\n",
			cinode->ci_inode->i_clusters);

	return 0;
}


extern int opterr, optind;
extern char *optarg;

int main(int argc, char *argv[])
{
	errcode_t ret;
	uint64_t blkno, blkoff;
	uint32_t cpos, coff;
	int count;
	uint64_t cblks;
	int cclts;
	int c, op = 0;
	char *filename;
	ocfs2_filesys *fs;
	ocfs2_cached_inode *cinode;
	struct ocfs2_extent_rec *rec;

	blkno = OCFS2_SUPER_BLOCK_BLKNO;

	initialize_ocfs_error_table();

	while ((c = getopt(argc, argv, "i:b:c:r:w")) != EOF) {
		switch (c) {
			case 'i':
				blkno = read_number(optarg);
				if (blkno <= OCFS2_SUPER_BLOCK_BLKNO) {
					fprintf(stderr,
						"Invalid inode block: %s\n",
						optarg);
					print_usage();
					return 1;
				}
				break;

			case 'w':
				if (op) {
					fprintf(stderr, "Cannot specify more than one operation\n");
					print_usage();
					return 1;
				}
				op = OP_WALK;
				break;

			case 'b':
				if (op) {
					fprintf(stderr, "Cannot specify more than one operation\n");
					print_usage();
					return 1;
				}
				if (read_b_numbers(optarg,
						   &blkoff, &count)) {
					fprintf(stderr, "Invalid block range: %s\n", optarg);
					print_usage();
					return 1;
				}
				op = OP_LOOKUP_BLOCK;
				break;

			case 'c':
				if (op) {
					fprintf(stderr, "Cannot specify more than one operation\n");
					print_usage();
					return 1;
				}
				if (read_c_numbers(optarg,
						   &cpos, &count)) {
					fprintf(stderr, "Invalid cluster range: %s\n", optarg);
					print_usage();
					return 1;
				}
				op = OP_LOOKUP_CLUSTER;
				break;

			case 'r':
				if (op) {
					fprintf(stderr, "Cannot specify more than one operation\n");
					print_usage();
					return 1;
				}
				cpos = read_number(optarg);
				op = OP_LOOKUP_REC;
				break;

			default:
				print_usage();
				return 1;
				break;
		}
	}
	
	if (!op) {
		fprintf(stderr, "Missing operation\n");
		print_usage();
		return 1;
	}

	if (optind >= argc) {
		fprintf(stderr, "Missing filename\n");
		print_usage();
		return 1;
	}
	filename = argv[optind];
	
	ret = ocfs2_open(filename, OCFS2_FLAG_RO, 0, 0, &fs);
	if (ret) {
		com_err(argv[0], ret,
			"while opening file \"%s\"", filename);
		goto out;
	}

	ret = ocfs2_read_cached_inode(fs, blkno, &cinode);
	if (ret) {
		com_err(argv[0], ret, "while reading inode %"PRIu64, blkno);
		goto out_close;
	}

	fprintf(stdout, "OCFS2 inode %"PRIu64" on \"%s\" has depth %"PRId16"\n",
		blkno, filename,
		cinode->ci_inode->id2.i_list.l_tree_depth);

	if (op == OP_WALK) {
		ret = ocfs2_load_extent_map(fs, cinode);
		if (ret) {
			com_err(argv[0], ret,
				"while loading extents");
			goto out_free;
		}
	} else {
		ret = ocfs2_extent_map_init(fs, cinode);
		if (ret) {
			com_err(argv[0], ret,
				"while initializing extent map");
			goto out_free;
		}

		switch (op) {
			case OP_LOOKUP_BLOCK:
				ret = ocfs2_extent_map_get_blocks(cinode,
								  blkoff,
								  count,
								  &blkno,
								  &cblks);
				if (ret) {
					com_err(argv[0], ret, 
						"looking up block range %"PRIu64":%d", blkoff, count);
					goto out_free;
				}
				fprintf(stdout, "Lookup of block range %"PRIu64":%d "
					"returned %"PRIu64":%"PRIu64"\n",
					blkoff, count, blkno, cblks);
				break;

			case OP_LOOKUP_CLUSTER:
				ret = ocfs2_extent_map_get_clusters(cinode,
								  cpos,
								  count,
								  &coff,
								  &cclts);
				if (ret) {
					com_err(argv[0], ret, 
						"looking up cluster range %"PRIu32":%d", cpos, count);
					goto out_free;
				}
				fprintf(stdout, "Lookup of cluster range %"PRIu32":%d returned %"PRIu32":%d\n",
					cpos, count, coff, cclts);
				break;
				
			case OP_LOOKUP_REC:
				ret = ocfs2_extent_map_get_rec(cinode,
							       cpos,
							       &rec);
				if (ret) {
					com_err(argv[0], ret, 
						"looking up cluster %"PRIu32"", cpos);
					goto out_free;
				}
				fprintf(stdout, "Lookup of cluster %"PRIu32" returned (%"PRIu32", %"PRIu32", %"PRIu64")\n",
					cpos, rec->e_cpos,
					rec->e_clusters, rec->e_blkno);
				break;

			default:
				ret = OCFS2_ET_INTERNAL_FAILURE;
				com_err(argv[0], ret,
					"Invalid op can't happen!\n");
				goto out_free;
		}
	}

	walk_extents_func(fs, cinode, op);

out_free:
	ocfs2_free_cached_inode(fs, cinode);

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


