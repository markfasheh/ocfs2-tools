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

/*
 * corrupt_chains()
 *
 */
void corrupt_chains(ocfs2_filesys *fs, int code, uint16_t slotnum)
{
	errcode_t ret;
	uint64_t blkno;
	ocfs2_super_block *sb = OCFS2_RAW_SB(fs->fs_super);
	char sysfile[40];

	switch (code) {
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
	case 8:
	case 10:
	case 11:
	case 12:
		snprintf(sysfile, sizeof(sysfile),
			 ocfs2_system_inodes[GLOBAL_BITMAP_SYSTEM_INODE].si_name);
		break;
#ifdef _LATER_
	case X:
		snprintf(sysfile, sizeof(sysfile),
			 ocfs2_system_inodes[GLOBAL_INODE_ALLOC_SYSTEM_INODE].si_name);
		break;
	case Y: 
		snprintf(sysfile, sizeof(sysfile),
			 ocfs2_system_inodes[EXTENT_ALLOC_SYSTEM_INODE].si_name, slotnum);
		break;
	case Z:
		snprintf(sysfile, sizeof(sysfile),
			 ocfs2_system_inodes[INODE_ALLOC_SYSTEM_INODE].si_name, slotnum);
		break;
#endif
	default:
		FSWRK_FATAL("Invalid code=%d", code);
	}

	ret = ocfs2_lookup(fs, sb->s_system_dir_blkno, sysfile,
			   strlen(sysfile), NULL, &blkno);
	if (ret)
		FSWRK_FATAL();

	mess_up_chains(fs, blkno, code);

	return ;
}
