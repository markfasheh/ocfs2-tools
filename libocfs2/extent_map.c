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
 *
 * Authors: Joel Becker
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

errcode_t ocfs2_extent_map_add(ocfs2_cached_inode *cinode,
			       ocfs2_extent_rec *rec)
{
	errcode_t ret;
	ocfs2_extent_map *em;
	struct list_head *next, *prev;
	ocfs2_extent_map_entry *ent, *tmp;
	
	if (!cinode || !cinode->ci_map)
		return OCFS2_ET_INVALID_ARGUMENT;

	if (rec->e_cpos >= cinode->ci_inode->i_clusters)
		return OCFS2_ET_INVALID_ARGUMENT;

	em = cinode->ci_map;

	next = prev = &em->em_extents;
	list_for_each(next, &em->em_extents) {
		tmp = list_entry(next, ocfs2_extent_map_entry, e_list);
		if (rec->e_cpos >=
		    (tmp->e_rec.e_cpos + tmp->e_rec.e_clusters)) {
			prev = next;
			continue;
		}

		if (!memcmp(rec, &tmp->e_rec,
			    sizeof(ocfs2_extent_map_entry)))
			return 0;

		if ((rec->e_cpos + rec->e_clusters) <=
		    tmp->e_rec.e_cpos)
			break;

		return OCFS2_ET_CORRUPT_EXTENT_BLOCK;
	}

	ret = ocfs2_malloc0(sizeof(ocfs2_extent_map_entry), &ent);
	if (ret)
		return ret;

	ent->e_rec.e_cpos = rec->e_cpos;
	ent->e_rec.e_clusters = rec->e_clusters;
	ent->e_rec.e_blkno = rec->e_blkno;
	list_add(&ent->e_list, prev);

	return 0;
}

static int extent_map_func(ocfs2_filesys *fs,
			   ocfs2_extent_rec *rec,
		  	   int tree_depth,
			   uint32_t ccount,
			   uint64_t ref_blkno,
			   int ref_recno,
			   void *priv_data)
{
	errcode_t ret;
	int iret = 0;
	struct extent_map_context *ctxt = priv_data;

	ret = ocfs2_extent_map_add(ctxt->cinode, rec);
	if (ret) {
		ctxt->errcode = ret;
		iret |= OCFS2_EXTENT_ABORT;
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

	ret = ocfs2_malloc0(sizeof(ocfs2_extent_map), &cinode->ci_map);
	if (ret)
		return ret;

	INIT_LIST_HEAD(&cinode->ci_map->em_extents);
	cinode->ci_map->em_cinode = cinode;

	ctxt.cinode = cinode;
	ctxt.errcode = 0;

	ret = ocfs2_extent_iterate(fs,
				   cinode->ci_blkno,
				   OCFS2_EXTENT_FLAG_DATA_ONLY,
				   NULL,
				   extent_map_func,
				   &ctxt);
	if (ret)
		goto cleanup;

	if (ctxt.errcode) {
		ret = ctxt.errcode;
		goto cleanup;
	}

	return 0;

cleanup:
	ocfs2_free_extent_map(fs, cinode);

	return ret;
}

errcode_t ocfs2_extent_map_clear(ocfs2_cached_inode *cinode,
				 uint32_t cpos, uint32_t clusters)
{
	ocfs2_extent_map *em;
	struct list_head *pos, *next;
	ocfs2_extent_map_entry *ent;

	if (!cinode || !cinode->ci_map)
		return OCFS2_ET_INVALID_ARGUMENT;

	em = cinode->ci_map;

	for (pos = em->em_extents.next, next = pos->next;
	     pos != &em->em_extents; 
	     pos = next, next = pos->next) {
		ent = list_entry(pos, ocfs2_extent_map_entry, e_list);

		if ((ent->e_rec.e_cpos + ent->e_rec.e_clusters) <=
		    cpos)
			continue;

		if ((cpos + clusters) <= (ent->e_rec.e_cpos))
			continue;

		list_del(pos);
		ocfs2_free(&ent);
	}

	return 0;
}

errcode_t ocfs2_free_extent_map(ocfs2_filesys *fs,
				ocfs2_cached_inode *cinode)
{
	ocfs2_extent_map *em;

	if (!cinode || !cinode->ci_map)
		return OCFS2_ET_INVALID_ARGUMENT;

	em = cinode->ci_map;

	ocfs2_extent_map_clear(cinode, 0, cinode->ci_inode->i_clusters);
	ocfs2_free(&cinode->ci_map);

	return 0;
}

#ifdef DEBUG_EXE
#include <stdlib.h>
#include <getopt.h>

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
		"Usage: extent_map -i <inode_blkno> <filename>\n");
}

static int walk_extents_func(ocfs2_filesys *fs,
			     ocfs2_cached_inode *cinode)
{
	ocfs2_extent_map *em;
	struct list_head *pos;
	uint32_t ccount;
	ocfs2_extent_map_entry *ent;

	em = cinode->ci_map;

	fprintf(stdout, "EXTENTS:\n");

	ccount = 0;
	list_for_each(pos, &em->em_extents) {
		ent = list_entry(pos, ocfs2_extent_map_entry, e_list);

		fprintf(stdout, "(%08"PRIu32", %08"PRIu32", %08"PRIu64") |"
				" + %08"PRIu32" = %08"PRIu32" / %08"PRIu32"\n",
			ent->e_rec.e_cpos, ent->e_rec.e_clusters,
			ent->e_rec.e_blkno, ccount,
			ccount + ent->e_rec.e_clusters,
			cinode->ci_inode->i_clusters);

		ccount += ent->e_rec.e_clusters;
	}

	fprintf(stdout, "TOTAL: %"PRIu32"\n", cinode->ci_inode->i_clusters);

	return 0;
}


extern int opterr, optind;
extern char *optarg;

int main(int argc, char *argv[])
{
	errcode_t ret;
	uint64_t blkno;
	int c;
	char *filename;
	ocfs2_filesys *fs;
	ocfs2_cached_inode *cinode;

	blkno = OCFS2_SUPER_BLOCK_BLKNO;

	initialize_ocfs_error_table();

	while ((c = getopt(argc, argv, "i:")) != EOF) {
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

			default:
				print_usage();
				return 1;
				break;
		}
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

	ret = ocfs2_load_extent_map(fs, cinode);
	if (ret) {
		com_err(argv[0], ret,
			"while loading extents");
		goto out_free;
	}

	walk_extents_func(fs, cinode);

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


