/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
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
 * --
 *
 * Little helpers that are used by all passes.
 *
 * XXX
 * 	pull more in here.. look in include/pass?.h for incongruities
 *
 */
#include <inttypes.h>
#include "ocfs2.h"

#include "util.h"

void o2fsck_write_inode(o2fsck_state *ost, uint64_t blkno, ocfs2_dinode *di)
{
	errcode_t ret;
	char *whoami = "o2fsck_write_inode";

	if (blkno != di->i_blkno) {
		com_err(whoami, OCFS2_ET_INTERNAL_FAILURE, "when asked to "
			"write an inode with an i_blkno of %"PRIu64" to block "
			"%"PRIu64, di->i_blkno, blkno);
		ost->ost_write_error = 1;
		return;
	}

	ret = ocfs2_write_inode(ost->ost_fs, blkno, (char *)di);
	if (ret) {
		com_err(whoami, ret, "while writing inode %"PRIu64, 
		        di->i_blkno);
		ost->ost_write_error = 1;
	}
}

void o2fsck_mark_cluster_allocated(o2fsck_state *ost, uint32_t cluster)
{
	int was_set;

	ocfs2_bitmap_set(ost->ost_allocated_clusters, cluster, &was_set);

	if (was_set) /* XX can go away one all callers handle this */
		com_err(__FUNCTION__, OCFS2_ET_INTERNAL_FAILURE,
			"!! duplicate cluster %"PRIu32, cluster);
}

void o2fsck_mark_clusters_allocated(o2fsck_state *ost, uint32_t cluster,
				    uint32_t num)
{
	while(num--)
		o2fsck_mark_cluster_allocated(ost, cluster++);
}
