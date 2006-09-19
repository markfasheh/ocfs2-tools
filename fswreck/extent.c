/*
 * extent.c
 *
 * extent corruptions
 *
 * Copyright (C) 2006 Oracle.  All rights reserved.
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


/* This file will create the following errors for extent rec and blocks .
 *
 * Extent block error:	EB_BLKNO, EB_GEN, EB_GEN_FIX, EXTENT_EB_INVALID
 *
 * Extent list error:	EB_LIST_DEPTH, EXTENT_LIST_COUNT, EXTENT_LIST_FREE 
 *
 * Extent record error: EXTENT_BLKNO_UNALIGNED, EXTENT_CLUSTERS_OVERRUN, 
 *			EXTENT_BLKNO_RANGE
 */

#include <errno.h>
#include <main.h>


extern char *progname;

static void create_file(ocfs2_filesys *fs, uint64_t blkno, uint64_t *retblkno)
{
	errcode_t ret;
	uint64_t tmp_blkno = 0;
	char random_name[OCFS2_MAX_FILENAME_LEN];

	memset(random_name, 0, sizeof(random_name));
	sprintf(random_name, "testXXXXXX");
	
	/* Don't use mkstemp since it will create a file 
	 * in the working directory which is no use.
	 * Use mktemp instead Although there is a compiling warning.
	 * mktemp fails to work in some implementations follow BSD 4.3,
	 * but currently ocfs2 will only support linux,
	 * so it will not affect us.
	 */
	if (!mktemp(random_name))
		FSWRK_COM_FATAL(progname, errno);

	ret = ocfs2_check_directory(fs, blkno);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_new_inode(fs, &tmp_blkno, S_IFREG | 0755);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_link(fs, blkno, random_name, tmp_blkno, OCFS2_FT_REG_FILE);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	*retblkno = tmp_blkno;

	return;
}

/*
 * This function is similar to ocfs2_extend_allocation() as both extend files.
 * However, this one ensures that the extent record tree also grows.
 */
static void custom_extend_allocation(ocfs2_filesys *fs, uint64_t ino,
				     uint32_t new_clusters)
{
	errcode_t ret;
	uint32_t n_clusters;
	uint32_t i;
	uint64_t blkno;
	uint64_t tmpblk;

	if (!(fs->fs_flags & OCFS2_FLAG_RW))
		FSWRK_FATAL("read-only filesystem");

	while (new_clusters) {
		ret = ocfs2_new_clusters(fs, 1, new_clusters, &blkno,
					 &n_clusters);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);
		if (!n_clusters)
			FSWRK_FATAL("ENOSPC");

		/* In order to ensure the extent records are not coalesced,
		 * we insert each cluster in reverse. */
		for(i = n_clusters; i; --i) {
			tmpblk = blkno + ocfs2_clusters_to_blocks(fs, i - 1);
		 	ret = ocfs2_insert_extent(fs, ino, tmpblk, 1);
			if (ret) 
				FSWRK_COM_FATAL(progname, ret);	
		}
	 	new_clusters -= n_clusters;
	}

	return;
}

/* Damage the file's extent block according to the given fsck_type */
static void damage_extent_block(ocfs2_filesys *fs, uint64_t blkno,
				enum fsck_type type)
{
	errcode_t ret;
	char *inobuf = NULL;
	char *extbuf = NULL;
	struct ocfs2_dinode *di;
	struct ocfs2_extent_list *el;
	struct ocfs2_extent_block *eb;
	uint32_t oldno;
	uint64_t oldblkno;

	ret = ocfs2_malloc_block(fs->fs_io, &inobuf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);
	
	ret = ocfs2_read_inode(fs, blkno, inobuf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	di = (struct ocfs2_dinode *)inobuf;

	if (!(di->i_flags & OCFS2_VALID_FL))
		FSWRK_FATAL("not a file");

	el = &(di->id2.i_list);

	if (el->l_next_free_rec > 0 && el->l_tree_depth > 0) {
		ret = ocfs2_malloc_block(fs->fs_io, &extbuf);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);

		ret = ocfs2_read_extent_block(fs, el->l_recs[0].e_blkno,
					      extbuf);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);

		eb = (struct ocfs2_extent_block *)extbuf;

		switch (type) {
		case EB_BLKNO:
			oldblkno = eb->h_blkno;
			eb->h_blkno += 1;
			fprintf(stdout, "EB_BLKNO: Corrupt inode#%"PRIu64", "
				"change extent block's number from %"PRIu64" to "
			       	"%"PRIu64"\n", blkno, oldblkno, eb->h_blkno);
			break;
		case EB_GEN:
		case EB_GEN_FIX:
		case EXTENT_EB_INVALID:
			oldno = eb->h_fs_generation;
			eb->h_fs_generation = 0x1234;
			if (type == EB_GEN)
				fprintf(stdout, "EB_GEN: ");
			else if (type == EB_GEN_FIX)
				fprintf(stdout, "EB_GEN_FIX: ");
			else
				fprintf(stdout, "EXTENT_EB_INVALID: ");
			fprintf(stdout, "Corrupt inode#%"PRIu64", change "
				"generation number from 0x%x to 0x%x\n",
				blkno, oldno, eb->h_fs_generation);
			break;
		case EXTENT_LIST_DEPTH: 
			oldno = eb->h_list.l_tree_depth;
			eb->h_list.l_tree_depth += 1;
			fprintf(stdout, "EXTENT_LIST_DEPTH: Corrupt inode#"
				"%"PRIu64", change first block's list depth "
				"from %d to %d\n", blkno, oldno,
				eb->h_list.l_tree_depth);
			break;
	 	case EXTENT_LIST_COUNT:
			oldno = eb->h_list.l_count;
			eb->h_list.l_count = 2 *
				ocfs2_extent_recs_per_eb(fs->fs_blocksize);
			fprintf(stdout, "EXTENT_LIST_COUNT: Corrupt inode#"
				"%"PRIu64", change cluster from %d to %d\n",
				blkno, oldno, eb->h_list.l_count);
			break;
		case EXTENT_LIST_FREE:
			oldno = eb->h_list.l_next_free_rec;
			eb->h_list.l_next_free_rec = 2 * 
				ocfs2_extent_recs_per_eb(fs->fs_blocksize);
			fprintf(stdout, "EXTENT_LIST_FREE: Corrupt inode#%"PRIu64", "
				"change blkno from %d to %d\n",
				blkno, oldno, eb->h_list.l_next_free_rec);
			break;
		default:
			FSWRK_FATAL("Invalid type=%d", type);
		}

		ret = ocfs2_write_extent_block(fs, el->l_recs[0].e_blkno,
					       extbuf);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);
		
		ret = ocfs2_write_inode(fs, blkno, inobuf);
		if (ret) 
			FSWRK_COM_FATAL(progname, ret);

	} else
		FSWRK_WARN("File inode#%"PRIu64" does not have an extent "
			   "block to corrupt.", blkno);

	if (extbuf)
		ocfs2_free(&extbuf);
	if (inobuf)
		ocfs2_free(&inobuf);
	return;
}

static void damage_extent_block_multi_types(ocfs2_filesys *fs, uint64_t blkno,
					    enum fsck_type* types, int num)
{
	uint64_t tmpblkno;
	volatile int i;
	uint32_t clusters;

	if (num <= 0)
		FSWRK_FATAL("Invalid num %d", num);

	/* Extend enough clusters to assure that we end up with a file
	 * with atleast an extent block */
	clusters = 2 * ocfs2_extent_recs_per_inode(fs->fs_blocksize);

	for (i = 0; i < num; ++i) {
		create_file(fs, blkno, &tmpblkno);

		custom_extend_allocation(fs, tmpblkno, clusters);

		damage_extent_block(fs, tmpblkno, types[i]);
	}

	return;
}

void mess_up_extent_list(ocfs2_filesys *fs, uint64_t blkno)
{
	enum fsck_type types[] = { EXTENT_LIST_DEPTH,
				   EXTENT_LIST_COUNT,
				   EXTENT_LIST_FREE };
	int numtypes = sizeof(types) / sizeof(enum fsck_type);

	damage_extent_block_multi_types(fs, blkno, types, numtypes);

	return;
}

void mess_up_extent_block(ocfs2_filesys *fs, uint64_t blkno)
{
	enum fsck_type types[] = { EB_BLKNO,
				   EB_GEN,
				   EB_GEN_FIX,
				   EXTENT_EB_INVALID };
	int numtypes = sizeof(types) / sizeof(enum fsck_type);

	damage_extent_block_multi_types(fs, blkno, types, numtypes);

	return;
}

static void mess_up_record(ocfs2_filesys *fs, uint64_t blkno, 
				enum fsck_type type)
{
	errcode_t ret;
	char *buf = NULL;
	struct ocfs2_dinode *di;
	struct ocfs2_extent_list *el;
	struct ocfs2_extent_rec *er;
	uint64_t oldno;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)  
		FSWRK_COM_FATAL(progname, ret);	

	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);	

	di = (struct ocfs2_dinode *)buf;

	if (!(di->i_flags & OCFS2_VALID_FL))
		FSWRK_COM_FATAL(progname, ret);	

	el = &(di->id2.i_list);

	if (el->l_next_free_rec > 0) {
		er = el->l_recs;
		oldno = er->e_blkno;
		switch (type) {
		case EXTENT_BLKNO_UNALIGNED: 
			er->e_blkno += 1;
			fprintf(stdout, "EXTENT_BLKNO_UNALIGNED: "
				"Corrupt inode#%"PRIu64", change blkno "
				"from %"PRIu64 " to %"PRIu64"\n",
				blkno, oldno, er->e_blkno);
			break;
	 	case EXTENT_CLUSTERS_OVERRUN:
			oldno = er->e_clusters;
			er->e_clusters = fs->fs_clusters + 1;
			er->e_blkno = ocfs2_clusters_to_blocks(fs, 
							fs->fs_clusters - 1);
			fprintf(stdout, "EXTENT_CLUSTERS_OVERRUN: "
				"Corrupt inode#%"PRIu64", "
				"change cluster from %"PRIu64 " to %d\n",
				blkno, oldno, er->e_clusters);
			break;
		case EXTENT_BLKNO_RANGE:
			er->e_blkno = 1;
			fprintf(stdout, "EXTENT_BLKNO_RANGE: "
			"Corrupt inode#%"PRIu64", change blkno "
			" from %"PRIu64 " to %"PRIu64"\n",
			blkno, oldno, er->e_blkno);
			break;
		default:
			goto bail;
		}

		ret = ocfs2_write_inode(fs, blkno, buf);
		if (ret) 
			FSWRK_COM_FATAL(progname, ret);	

	} else
		FSWRK_WARN("Test file inode#%"PRIu64" has no content."
				"Can't damage it.\n", blkno);

bail:
	if (buf)
		ocfs2_free(&buf);
	return;
}

void mess_up_extent_record(ocfs2_filesys *fs, uint64_t blkno)
{
	uint64_t tmpblkno;
	errcode_t ret;
	int i;
	enum fsck_type types[] = { EXTENT_BLKNO_UNALIGNED,
				   EXTENT_BLKNO_RANGE,
				   EXTENT_CLUSTERS_OVERRUN };

	for (i = 0; i < sizeof(types) / sizeof(enum fsck_type); ++i) {
		create_file(fs, blkno, &tmpblkno);

		ret = ocfs2_extend_allocation(fs, tmpblkno, 1);
		if (ret)
			FSWRK_COM_FATAL(progname, ret);

		mess_up_record(fs, tmpblkno, types[i]);
	}

	return;
}
