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
#include <string.h>
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
		return;
	}

	ret = ocfs2_write_inode(ost->ost_fs, blkno, (char *)di);
	if (ret) {
		com_err(whoami, ret, "while writing inode %"PRIu64, 
		        di->i_blkno);
		ost->ost_saw_error = 1;
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

errcode_t o2fsck_type_from_dinode(o2fsck_state *ost, uint64_t ino,
				  uint8_t *type)
{
	char *buf = NULL;
	errcode_t ret;
	ocfs2_dinode *dinode;
	char *whoami = __FUNCTION__;

	*type = 0;

	ret = ocfs2_malloc_block(ost->ost_fs->fs_io, &buf);
	if (ret) {
		com_err(whoami, ret, "while allocating an inode buffer to "
			"read and discover the type of inode %"PRIu64, ino);
		goto out;
	}

	ret = ocfs2_read_inode(ost->ost_fs, ino, buf);
	if (ret) {
		com_err(whoami, ret, "while reading inode %"PRIu64" to "
			"discover its file type", ino);
		goto out;
	}

	dinode = (ocfs2_dinode *)buf; 
	*type = ocfs_type_by_mode[(dinode->i_mode & S_IFMT)>>S_SHIFT];

out:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

errcode_t o2fsck_read_publish(o2fsck_state *ost)
{
	uint16_t i, max_nodes;
	char *dlm_buf = NULL;
	uint64_t dlm_ino;
	errcode_t ret;
	int buflen;
	char *whoami = "read_publish";

	if (ost->ost_publish)
		ocfs2_free(&ost->ost_publish);

	max_nodes = OCFS2_RAW_SB(ost->ost_fs->fs_super)->s_max_nodes;

	ret = ocfs2_malloc0(sizeof(ocfs_publish) * max_nodes,
			    &ost->ost_publish);
	if (ret) {
		com_err(whoami, ret, "while allocating an array to store each "
			"node's publish block");
		goto out;
	}

	ret = ocfs2_lookup_system_inode(ost->ost_fs, DLM_SYSTEM_INODE,
					0, &dlm_ino);
	if (ret) {
		com_err(whoami, ret, "while looking up the dlm system inode");
		goto out;
	}

	ret = ocfs2_read_whole_file(ost->ost_fs, dlm_ino, &dlm_buf, &buflen);
	if (ret) {
		com_err(whoami, ret, "while reading dlm file");
		goto out;
	}

	/* I have no idea what that magic math is really doing. */
	for (i = 0; i < max_nodes; i++) {
		int b_bits = OCFS2_RAW_SB(ost->ost_fs->fs_super)->s_blocksize_bits;

		memcpy(&ost->ost_publish[i],
		       dlm_buf + ((2 + 4 + max_nodes + i) << b_bits),
		       sizeof(ocfs_publish));
		if (ost->ost_publish[i].mounted)
			ost->ost_stale_mounts = 1;
	}
out:
	if (ret && ost->ost_publish)
		ocfs2_free(&ost->ost_publish);
	if (dlm_buf)
		ocfs2_free(&dlm_buf);
	return ret;
}
