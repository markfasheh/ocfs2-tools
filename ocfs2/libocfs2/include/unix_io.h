/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * unix_io.h
 *
 * I/O routines for the OCFS2 userspace library.
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

#ifndef _UNIX_IO_H
#define _UNIX_IO_H

typedef struct _io_channel io_channel;

#define OCFS2_FLAG_RO	0x00
#define OCFS2_FLAG_RW	0x01

errcode_t io_open(const char *name, int flags, io_channel **channel);
errcode_t io_close(io_channel *channel);
int io_get_error(io_channel *channel);
errcode_t io_set_blksize(io_channel *channel, int blksize);
errcode_t io_get_blksize(io_channel *channel, int *blksize);
errcode_t io_read_block(io_channel *channel, int64_t blkno, int count,
			char *data);
errcode_t io_write_block(io_channel *channel, int64_t blkno, int count,
			 const char *data);
#endif  /* _UNIX_IO_H */
