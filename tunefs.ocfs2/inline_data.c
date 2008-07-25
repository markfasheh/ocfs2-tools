/*
 * inline_data.c
 *
 * source file of inline data functions for tunefs.
 *
 * Copyright (C) 2008 Oracle.  All rights reserved.
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

#include <ocfs2/kernel-rbtree.h>
#include <tunefs.h>
#include <assert.h>

struct inline_file {
	uint64_t blkno;
	struct inline_file *next;
};

struct inline_file_list {
	struct inline_file *files;
	uint64_t files_num;
};

struct inline_file_list files_list;

static errcode_t iterate_all_file(ocfs2_filesys *fs,
				  char *progname)
{
	errcode_t ret;
	uint64_t blkno;
	char *buf;
	struct ocfs2_dinode *di;
	struct inline_file *file;
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

		if (di->i_dyn_features & OCFS2_INLINE_DATA_FL) {
			ret = ocfs2_malloc0(sizeof(struct inline_file), &file);
			if (ret)
				break;

			files_list.files_num++;
			file->blkno = di->i_blkno;
			file->next = files_list.files;
			files_list.files = file;
		}
	}

	ocfs2_close_inode_scan(scan);
out_free:
	ocfs2_free(&buf);

out:
	return ret;
}

errcode_t clear_inline_data_check(ocfs2_filesys *fs, char *progname)
{
	errcode_t ret;
	uint32_t free_clusters;

	memset(&files_list, 0, sizeof(files_list));

	ret = iterate_all_file(fs, progname);
	if (ret)
		goto bail;

	ret = get_total_free_clusters(fs, &free_clusters);
	if (ret)
		goto bail;

	printf("We have %u clusters free and need %"PRIu64" clusters "
	       "for inline data\n", free_clusters, files_list.files_num);

	if (free_clusters < files_list.files_num) {
		com_err(progname, 0, "Don't have enough free space.");
		ret = OCFS2_ET_NO_SPACE;
	}
bail:
	return ret;
}

errcode_t clear_inline_data_flag(ocfs2_filesys *fs, char *progname)
{
	errcode_t ret = 0;
	struct inline_file *file = files_list.files;
	ocfs2_cached_inode *ci = NULL;

	while (file) {
		ret = ocfs2_read_cached_inode(fs, file->blkno, &ci);
		if (ret)
			goto bail;

		ret = ocfs2_convert_inline_data_to_extents(ci);
		if (ret)
			goto bail;

		file = file->next;
	}

	if (ocfs2_support_inline_data(OCFS2_RAW_SB(fs->fs_super)))
		OCFS2_CLEAR_INCOMPAT_FEATURE(OCFS2_RAW_SB(fs->fs_super),
					 OCFS2_FEATURE_INCOMPAT_INLINE_DATA);

bail:
	if (ci)
		ocfs2_free_cached_inode(fs, ci);
	free_inline_data_ctxt();
	return ret;
}

void free_inline_data_ctxt(void)
{
	struct inline_file *file = files_list.files, *next;

	while (file) {
		next = file->next;
		ocfs2_free(&file);
		file = next;
	}

	files_list.files = NULL;
}
