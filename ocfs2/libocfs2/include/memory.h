/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * memory.h
 *
 * Memory routines for the OCFS2 userspace library.
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
 * Authors: Joel Becker
 */

#ifndef _MEMORY_H
#define _MEMORY_H

errcode_t ocfs2_malloc(unsigned long size, void *ptr);
errcode_t ocfs2_malloc0(unsigned long size, void *ptr);
errcode_t ocfs2_free(void *ptr);
errcode_t ocfs2_realloc(unsigned long size, void *ptr);
errcode_t ocfs2_realloc0(unsigned long size, void *ptr,
			 unsigned long old_size);
errcode_t ocfs2_malloc_blocks(io_channel *channel, int num_blocks,
			      void *ptr);
errcode_t ocfs2_malloc_block(io_channel *channel, void *ptr);

#endif  /* _MEMORY_H */
