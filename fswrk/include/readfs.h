/*
 * readfs.h
 *
 * Function prototypes, macros, etc. for related 'C' files
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

#ifndef __READFS_H__
#define __READFS_H__

fswrk_ctxt *open_fs(char *device);
void close_fs(fswrk_ctxt *ctxt);
void read_dir (fswrk_ctxt *ctxt, ocfs2_extent_list *ext, __u64 size, GArray *dirarr);
int read_file (fswrk_ctxt *ctxt, __u64 blknum, int fdo, char **buf);
int read_inode (fswrk_ctxt *ctxt, uint64_t blkno, char *buf);
int read_group (fswrk_ctxt *ctxt, uint64_t blkno, char *buf);

#endif		/* __READFS_H__ */
