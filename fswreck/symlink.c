/*
 * symlink.c
 *
 * symlink file corruptions
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

/* This file will create the errors for the link file.
 *
 * Link file error: LINK_FAST_DATA, LINK_NULLTERM, LINK_SIZE, LINK_BLOCKS
 *
 */

#include <main.h>

static char *dummy = "/dummy00/dummy00";
extern char *progname;

/*this function fill up the block with dummy texts. */
static int fillup_block(ocfs2_filesys *fs,
			uint64_t blkno,
			uint64_t bcount,
			void *priv_data)
{
	errcode_t ret = 0;
	int i;
	char *buf = NULL, *out = NULL;
	int len = strlen(dummy);

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	out = buf;
	for (i = 0; i < fs->fs_blocksize/len; i++, out+=len)
		memcpy(out, dummy, len);
	if(fs->fs_blocksize%len)
		memcpy(out, dummy, fs->fs_blocksize%len);

	ret = io_write_block(fs->fs_io, blkno, 1, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	fprintf(stdout, "Fill block#%"PRIu64" with dummy texts.\n", blkno);

	if (buf)
		ocfs2_free(&buf);
	return 0;
}

/* Add dummy texts as symname to the inodes.
 * Attention: we will not use fast symlink, and the cluster
 * has already been allocated to the file.
 * Here just get it, copy the symname and write back
 * to the disk.
 */
static void add_symlink(ocfs2_filesys *fs, uint64_t blkno)
{
	errcode_t ret = 0;
	ocfs2_cached_inode *cinode = NULL;
	uint64_t new_blk;
	int contig;
	char *buf = NULL;

	ret = ocfs2_read_cached_inode(fs, blkno, &cinode);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_extent_map_init(fs, cinode);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	/* get first block of the file */
	ret = ocfs2_extent_map_get_blocks(cinode, 0, 1,
					  &new_blk, &contig);
	if (ret) 
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	strcpy(buf, dummy);

	ret = io_write_block(fs->fs_io, new_blk, 1, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	cinode->ci_inode->i_size  = strlen(dummy);

	ret = ocfs2_write_cached_inode(fs, cinode);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	if (buf)
		ocfs2_free(&buf);
	if (cinode)
		ocfs2_free_cached_inode(fs, cinode);
	return;
}

static void create_symlink(ocfs2_filesys *fs,
				 uint64_t blkno, uint64_t *retblkno)
{
	errcode_t ret;
	uint64_t tmp_blkno;
	uint32_t clusters = 1;
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

	ret = ocfs2_new_inode(fs, &tmp_blkno, S_IFLNK | 0755);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_link(fs, blkno, random_name, tmp_blkno, OCFS2_FT_SYMLINK);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_extend_allocation(fs, tmp_blkno, clusters);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	add_symlink(fs, tmp_blkno);

	*retblkno = tmp_blkno;
	return;
}

static void corrupt_symlink_file(ocfs2_filesys *fs, uint64_t blkno,
				enum fsck_type type)
{
	errcode_t ret;
	char *buf = NULL;
	struct ocfs2_dinode *di;
	struct ocfs2_extent_list *el;
	struct ocfs2_extent_rec *er;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	di = (struct ocfs2_dinode *)buf;

	if (!(di->i_flags & OCFS2_VALID_FL))
		FSWRK_FATAL("not a file");

	if (!S_ISLNK(di->i_mode))
		FSWRK_FATAL("not a link file");

	el = &(di->id2.i_list);

	switch(type) {
	case LINK_FAST_DATA:
		di->i_clusters = 0;
		fprintf(stdout, "LINK_FAST_DATA: "
			"Corrupt inode#%"PRIu64","
			"change clusters from %u to 0", blkno, di->i_clusters);
		break;
	case LINK_NULLTERM:
		ocfs2_block_iterate_inode(fs, di,
			OCFS2_BLOCK_FLAG_APPEND,
			fillup_block, NULL);
		fprintf(stdout, "LINK_NULLTERM: "
			"Corrupt inode#%"PRIu64","
			"fill all blocks with dummy texts\n", blkno);
		di->i_clusters = 1;
		di->i_size = di->i_clusters * fs->fs_clustersize;
			break;
	case LINK_SIZE:
		fprintf(stdout, "LINK_SIZE: "
			"Corrupt inode#%"PRIu64","
			"change size from %"PRIu64" to %"PRIu64"\n",
			blkno, di->i_size, (di->i_size + 10));
		di->i_size += 10;
		break;
	case LINK_BLOCKS:
		er = el->l_recs;
		fprintf(stdout, "LINK_BLOCKS: "
			"Corrupt inode#%"PRIu64","
			"change e_clusters from %u to %u\n",
			blkno, er->e_clusters, (er->e_clusters + 1));
		er->e_clusters += 1;
		break;
	default:
		FSWRK_FATAL("Invalid type[%d]\n", type);
	}

	ret = ocfs2_write_inode(fs, blkno, buf);
	if (ret) 
		FSWRK_COM_FATAL(progname, ret);

	if (buf)
		ocfs2_free(&buf);
	return;
}

void mess_up_symlink(ocfs2_filesys *fs, uint64_t blkno)
{
	uint64_t tmp_blkno;
	int i;
	enum fsck_type types[] = { 	LINK_FAST_DATA, LINK_NULLTERM,
					LINK_SIZE, LINK_BLOCKS };

	for (i = 0; i < ARRAY_ELEMENTS(types); i++) {
		create_symlink(fs, blkno, &tmp_blkno);

		corrupt_symlink_file(fs, tmp_blkno, types[i]);
	}

	return ;
}
