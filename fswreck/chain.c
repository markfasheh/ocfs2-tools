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
 * mess_up_chains()
 *
 */
void mess_up_chains(ocfs2_filesys *fs, uint64_t blkno, int code)
{
	errcode_t ret;
	char *buf = NULL;
	ocfs2_dinode *di;
	ocfs2_chain_list *cl;
	ocfs2_chain_rec *cr;
	int i;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	di = (ocfs2_dinode *)buf;

	if (!(di->i_flags & OCFS2_BITMAP_FL))
		FSWRK_FATAL("not a bitmap");

	if (!(di->i_flags & OCFS2_CHAIN_FL))
		FSWRK_FATAL("not a chain group");

	cl = &(di->id2.i_chain);

	switch (code) {
	case 3:
		/* delink last chain from the inode */
		fprintf(stdout, "Corrupt #%02d: Delink group descriptor "
			"at block#%"PRIu64"\n", code, blkno);
		if (!cl->cl_next_free_rec) {
			fprintf(stdout, "No chains to delink\n");
			goto bail;
		} else {
			i = cl->cl_next_free_rec - 1;
			cr = &(cl->cl_recs[i]);
			fprintf(stdout, "Delinking ind=%d, block#=%"PRIu64", "
				"free=%u, total=%u\n", i, cr->c_blkno,
				cr->c_free, cr->c_total);
			cr->c_free = 12345;
			cr->c_total = 67890;
			cr->c_blkno = 1234567890;
			cl->cl_next_free_rec = i;
		}
		break;

	case 4:
		/* corrupt cl_count */
		fprintf(stdout, "Corrupt #%02d: Increase chainlist count "
			"at block#%"PRIu64" from %u to %u\n", code, blkno,
			cl->cl_count, (cl->cl_count + 123));
		cl->cl_count += 123;
		break;

	case 5:
		/* corrupt cl_next_free_rec */
		fprintf(stdout, "Corrupt #%02d: Increase chainlist nextfree "
			"at block#%"PRIu64" from %u to %u\n", code, blkno,
			cl->cl_next_free_rec, (cl->cl_next_free_rec + 10));
		cl->cl_next_free_rec += 10;
		break;

	case 6:
		/* corrupt id1.bitmap1.i_total/i_used */
		fprintf(stdout, "Corrupt #%02d: Increase bitmap total "
			"at block#%"PRIu64" from %u to %u\n", code, blkno,
			di->id1.bitmap1.i_total, di->id1.bitmap1.i_total + 10);
		fprintf(stdout, "Corrupt #%02d: Decrease bitmap used "
			"at block#%"PRIu64" from %u to %u\n", code, blkno,
			di->id1.bitmap1.i_used, 0);
		di->id1.bitmap1.i_total += 10;
		di->id1.bitmap1.i_used = 0;
		break;

	default:
		FSWRK_FATAL("Invalid code=%d", code);
	}

	ret = ocfs2_write_inode(fs, blkno, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	fprintf(stdout, "Corrupt #%02d: Finito\n", code);

bail:
	if (buf)
		ocfs2_free(&buf);

	return ;
}
