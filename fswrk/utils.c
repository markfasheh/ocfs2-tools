/*
 * utils.c
 *
 * utility functions
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
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
 * Authors: Sunil Mushran
 */

#include <main.h>

/*
 * add_extent_rec()
 *
 */
void add_extent_rec (GArray *arr, ocfs2_extent_rec *rec)
{
	ocfs2_extent_rec *new;

	if (!arr)
		return ;

	if (!(new = malloc(sizeof(ocfs2_extent_rec))))
		FSWRK_FATAL("%s", strerror(errno));

	memcpy(new, rec, sizeof(ocfs2_extent_rec));
	g_array_append_vals(arr, new, 1);

	return ;
}

/*
 * add_dir_rec()
 *
 */
void add_dir_rec (GArray *arr, struct ocfs2_dir_entry *rec)
{
	struct ocfs2_dir_entry *new;

	if (!arr)
		return ;

	if (!(new = malloc(sizeof(struct ocfs2_dir_entry))))
		FSWRK_FATAL("%s", strerror(errno));

	memset(new, 0, sizeof(struct ocfs2_dir_entry));

	new->inode = rec->inode;
	new->rec_len = rec->rec_len;
	new->name_len = rec->name_len;
	new->file_type = rec->file_type;
	strncpy(new->name, rec->name, rec->name_len);
	new->name[rec->name_len] = '\0';

	g_array_append_vals(arr, new, 1);

	return ;
}

/*
 * read_block()
 *
 */
int read_block(fswrk_ctxt *ctxt, uint64_t blkno, char **buf)
{
	ocfs2_super_block *sb = &(ctxt->super_block->id2.i_super);
	uint32_t len = 1 << 1 << sb->s_blocksize_bits;
	uint64_t off = blkno << sb->s_blocksize_bits;

	if (!*buf) {
		if (!(*buf = memalign(len, len)))
			FSWRK_FATAL("out of memory");
	}

	return ((pread64(ctxt->fd, *buf, len, off) == len) ? 0 : -1);
}
