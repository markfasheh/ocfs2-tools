/*
 * corrupt.c
 *
 * corruption routines
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

void corrupt_3(ocfs2_filesys *fs)
{
	errcode_t ret;
	uint64_t blkno;
	ocfs2_super_block *sb = OCFS2_RAW_SB(fs->fs_super);
	char *global_bitmap = sysfile_info[GLOBAL_BITMAP_SYSTEM_INODE].name;

	ret = ocfs2_lookup(fs, sb->s_system_dir_blkno, global_bitmap,
			   strlen(global_bitmap), NULL, &blkno);
	if (ret)
		FSWRK_FATAL();

	fprintf(stdout, "Corrupt #3: Delink group descriptor from "
		"global bitmap at block#%"PRIu64"\n", blkno);

	delink_chain_group(fs, blkno, 1);

	fprintf(stdout, "Corrupt #3: Finito\n");

	return ;
}
