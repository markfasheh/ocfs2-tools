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
	struct ocfs2_dinode *di;
	struct ocfs2_chain_list *cl;
	struct ocfs2_chain_rec *cr;
	int i;
	uint32_t tmp1, tmp2;
	uint64_t tmpblk;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	ret = ocfs2_read_inode(fs, blkno, buf);
	if (ret)
		FSWRK_COM_FATAL(progname, ret);

	di = (struct ocfs2_dinode *)buf;

	if (!(di->i_flags & OCFS2_BITMAP_FL))
		FSWRK_FATAL("not a bitmap");

	if (!(di->i_flags & OCFS2_CHAIN_FL))
		FSWRK_FATAL("not a chain group");

	cl = &(di->id2.i_chain);

	if (!cl->cl_next_free_rec) {
		fprintf(stdout, "No chains found at block#%"PRIu64"\n", blkno);
		goto bail;
	}

	switch (code) {
	case 3: /* delink the last chain from the inode */
		fprintf(stdout, "Corrupt #%02d: Delink group descriptor "
			"in block#%"PRIu64"\n", code, blkno);

		i = cl->cl_next_free_rec - 1;
		cr = &(cl->cl_recs[i]);
		fprintf(stdout, "Delinking ind=%d, block#=%"PRIu64", "
			"free=%u, total=%u\n", i, cr->c_blkno,
			cr->c_free, cr->c_total);
		cr->c_free = 12345;
		cr->c_total = 67890;
		cr->c_blkno = ocfs2_clusters_to_blocks(fs, fs->fs_super->i_clusters);
		cr->c_blkno += 1; /* 1 more block than the fs size */
		cl->cl_next_free_rec = i;
		break;

	case 4: /* corrupt cl_count */
		fprintf(stdout, "Corrupt #%02d: Modified cl_count "
			"in block#%"PRIu64" from %u to %u\n", code, blkno,
			cl->cl_count, (cl->cl_count + 100));
		cl->cl_count += 100;
		break;

	case 5: /* corrupt cl_next_free_rec */
		fprintf(stdout, "Corrupt #%02d: Modified cl_next_free_rec "
			"in block#%"PRIu64" from %u to %u\n", code, blkno,
			cl->cl_next_free_rec, (cl->cl_next_free_rec + 10));
		cl->cl_next_free_rec += 10;
		break;

	case 7: /* corrupt id1.bitmap1.i_total/i_used */
		fprintf(stdout, "Corrupt #%02d: Modified bitmap total "
			"in block#%"PRIu64" from %u to %u\n", code, blkno,
			di->id1.bitmap1.i_total, di->id1.bitmap1.i_total + 10);
		fprintf(stdout, "Corrupt #%02d: Modified bitmap used "
			"in block#%"PRIu64" from %u to %u\n", code, blkno,
			di->id1.bitmap1.i_used, 0);
		di->id1.bitmap1.i_total += 10;
		di->id1.bitmap1.i_used = 0;
		break;

	case 8: /* Corrupt c_blkno of the first record with a number larger than volume size */
		cr = &(cl->cl_recs[0]);
		tmpblk = ocfs2_clusters_to_blocks(fs, fs->fs_super->i_clusters);
		tmpblk++; /* 1 more block than the fs size */

		fprintf(stdout, "Corrupt #%02d: Modified c_blkno in "
			"block#%"PRIu64" from %"PRIu64" to %"PRIu64"\n",
			code, blkno, cr->c_blkno, tmpblk);

		cr->c_blkno = tmpblk;
		break;

	case 10: /* Corrupt c_blkno of the first record with an unaligned number */
		cr = &(cl->cl_recs[0]);
		tmpblk = 1234567;

		fprintf(stdout, "Corrupt #%02d: Modified c_blkno in "
			"block#%"PRIu64" from %"PRIu64" to %"PRIu64"\n",
			code, blkno, cr->c_blkno, tmpblk);

		cr->c_blkno = tmpblk;
		break;

	case 11: /* Corrupt c_blkno of the first record with 0 */
		cr = &(cl->cl_recs[0]);
		tmpblk = 0;

		fprintf(stdout, "Corrupt #%02d: Modified c_blkno in "
			"block#%"PRIu64" from %"PRIu64" to %"PRIu64"\n",
			code, blkno, cr->c_blkno, tmpblk);

		cr->c_blkno = tmpblk;
		break;

	case 12: /* corrupt c_total and c_free of the first record */
		cr = &(cl->cl_recs[0]);
		tmp1 = (cr->c_total >= 100) ? (cr->c_total - 100) : 0;
		tmp2 = (cr->c_free >= 100) ? (cr->c_free - 100) : 0;

		fprintf(stdout, "Corrupt #%02d: Modified c_total "
			"in block#%"PRIu64" for chain ind=%d from %u to %u\n",
			code, blkno, 0, cr->c_total, tmp1);
		fprintf(stdout, "Corrupt #%02d: Modified c_free "
			"in block#%"PRIu64" for chain ind=%d from %u to %u\n",
			code, blkno, 0, cr->c_free, tmp2);

		cr->c_total = tmp1;
		cr->c_free = tmp2;
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
