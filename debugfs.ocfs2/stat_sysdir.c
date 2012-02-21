/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * stat_sysdir.c
 *
 * Shows all the objects in the system directory
 *
 * Copyright (C) 2011 Oracle.  All rights reserved.
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

#include "main.h"
#include "ocfs2/image.h"
#include "ocfs2/byteorder.h"

extern struct dbgfs_gbls gbls;

static int show_system_inode(struct ocfs2_dir_entry *rec, uint64_t blocknr,
			     int offset, int blocksize, char *block_buf,
			     void *priv_data)
{
	struct list_dir_opts *ls_opts = (struct list_dir_opts *)priv_data;
	ocfs2_filesys *fs = ls_opts->fs;
	FILE *out = ls_opts->out;
	char *buf = ls_opts->buf;
	struct list_dir_opts ls;
	struct ocfs2_dinode *di;
	char tmp = rec->name[rec->name_len];
	struct ocfs2_slot_map_extended *se = NULL;
	struct ocfs2_slot_map *sm = NULL;
	int num_slots;
	errcode_t ret = 0;

	rec->name[rec->name_len] = '\0';

	if (!strcmp(rec->name, ".."))
		goto out;

	memset(buf, 0, fs->fs_blocksize);
	ocfs2_read_inode(fs, rec->inode, buf);
	di = (struct ocfs2_dinode *)buf;

	if (!strcmp(rec->name, "."))
		fprintf(out, "\n  //\n");
	else
		fprintf(out, "\n  //%s\n", rec->name);
	dump_inode(out, di);

	if ((di->i_flags & OCFS2_LOCAL_ALLOC_FL))
		dump_local_alloc(out, &(di->id2.i_lab));
	else if ((di->i_flags & OCFS2_CHAIN_FL))
		ret = traverse_chains(fs, &(di->id2.i_chain), out);
	else if (S_ISLNK(di->i_mode) && !di->i_clusters)
		dump_fast_symlink(out, (char *)di->id2.i_symlink);
	else if (di->i_flags & OCFS2_DEALLOC_FL)
		dump_truncate_log(out, &(di->id2.i_dealloc));
	else if (!(di->i_dyn_features & OCFS2_INLINE_DATA_FL))
		ret = traverse_extents(fs, &(di->id2.i_list), out);
	if (ret)
		com_err(gbls.cmd, ret, "while traversing inode at block "
			"%"PRIu64, (uint64_t)rec->inode);

	if (S_ISDIR(di->i_mode)) {
		ls.fs = ls_opts->fs;
		ls.out = ls_opts->out;
		ls.long_opt = 1;
		ret = ocfs2_malloc_block(fs->fs_io, &ls.buf);
		if (ret)
			return ret;
		ret = ocfs2_dir_iterate(fs, rec->inode, 0, NULL, dump_dir_entry,
					(void *)&ls);
		if (ret)
			com_err(gbls.cmd, ret, "while iterating // at block "
				"%"PRIu64, (uint64_t)rec->inode);
		ocfs2_free(&ls.buf);
	}

	if (!strcmp(rec->name,
		    ocfs2_system_inodes[SLOT_MAP_SYSTEM_INODE].si_name)) {
		num_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;
		if (ocfs2_uses_extended_slot_map(OCFS2_RAW_SB(fs->fs_super)))
			ret = ocfs2_read_slot_map_extended(fs, num_slots, &se);
		else
			ret = ocfs2_read_slot_map(fs, num_slots, &sm);
		if (ret)
			com_err(gbls.cmd, ret, "while reading //slotmap");
		else
			dump_slots(out, se, sm, num_slots);
		ocfs2_free(&sm);
		ocfs2_free(&se);
	}

out:
	rec->name[rec->name_len] = tmp;

	return 0;
}

void show_stat_sysdir(ocfs2_filesys *fs, FILE *out)
{
	errcode_t ret;
	struct ocfs2_dinode *di;
	struct ocfs2_super_block *sb;
	struct list_dir_opts ls;

	di = fs->fs_super;
	sb = OCFS2_RAW_SB(di);

	fprintf(out, "Device: %s\n", gbls.device);
	fprintf(out, "  superblock\n");
	dump_super_block(out, sb);
	dump_inode(out, di);

	ret = ocfs2_check_directory(fs, fs->fs_sysdir_blkno);
	if (ret) {
		com_err(gbls.cmd, ret, "while checking system directory at "
			"block %"PRIu64"", fs->fs_sysdir_blkno);
		goto bail;
	}

	ls.fs = fs;
	ls.out = out;
	ls.buf = gbls.blockbuf;
	ls.long_opt = 1;
	ret = ocfs2_dir_iterate(fs, fs->fs_sysdir_blkno, 0, NULL,
				show_system_inode, (void *)&ls);
	if (ret)
		com_err(gbls.cmd, ret, "while iterating system directory at "
			"block %"PRIu64"", fs->fs_sysdir_blkno);

bail:
	return ;
}
