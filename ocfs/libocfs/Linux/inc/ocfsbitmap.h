/*
 * ocfsbitmap.h
 *
 * Function prototypes for related 'C' file.
 *
 * Copyright (C) 2002 Oracle Corporation.  All rights reserved.
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
 * Authors: Neeraj Goyal, Suchit Kaura, Kurt Hackel, Sunil Mushran,
 *          Manish Singh, Wim Coekaerts
 */

#ifndef _OCFSBITMAP_H_
#define _OCFSBITMAP_H_

void ocfs_initialize_bitmap (ocfs_alloc_bm * bitmap, void *buf, __u32 sz);
int ocfs_find_clear_bits (ocfs_alloc_bm * bitmap, __u32 numBits, __u32 offset, __u32 sysonly);
int ocfs_count_bits (ocfs_alloc_bm * bitmap);
void ocfs_set_bits (ocfs_alloc_bm * bitmap, __u32 start, __u32 num);
void ocfs_clear_bits (ocfs_alloc_bm * bitmap, __u32 start, __u32 num);

#endif				/* _OCFSBITMAP_H_ */
