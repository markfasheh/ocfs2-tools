/*
 * chain.c
 *
 * chain group corruptions
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

extern char *progname;

/*
 * delink_chain_group()
 *
 */
void delink_chain_group(ocfs2_filesys *fs, int blkno, int count)
{
	errcode_t ret;
	char *buf = NULL;
	ocfs2_dinode *di;
	ocfs2_chain_list *cl;
	ocfs2_chain_rec *cr;
	int i, j;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	di = (ocfs2_dinode *)buf;

	if (!(di->i_flags & OCFS2_CHAIN_FL))
		FSWRK_FATAL("not a chain group");

	cl = &(di->id2.i_chain);

	/* delink 'count' chains from the inode */
	for (i = cl->cl_next_free_rec, j = 0; i && j < count; --i, ++j) {
		cr = &(cl->cl_recs[i - 1]);
		fprintf(stdout, "Delinking ind=%d, block#=%"PRIu64", free=%u, total=%u\n",
			i - 1, cr->c_blkno, cr->c_free, cr->c_total);
		cr->c_free = 12345;
		cr->c_total = 67890;
		cr->c_blkno = 1234567890;
	}

	cl->cl_next_free_rec = i;

	ret = ocfs2_write_inode(fs, blkno, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	fprintf(stdout, "Delinked %d blocks\n", j);

	if (buf)
		ocfs2_free(&buf);

	return ;
}
