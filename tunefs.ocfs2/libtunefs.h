/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * libtunefs.h
 *
 * tunefs helper library prototypes.
 *
 * Copyright (C) 2004, 2008 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef _LIBTUNEFS_H
#define _LIBTUNEFS_H

#define PROGNAME "tunefs.ocfs2"

/* Flags for tunefs_open() */
#define TUNEFS_FLAG_RO		0x00
#define TUNEFS_FLAG_RW		0x01
#define TUNEFS_FLAG_ONLINE	0x02	/* Operation can run online */
#define TUNEFS_FLAG_NOCLUSTER	0x04	/* Operation does not need the
					   cluster stack */


errcode_t tunefs_init(void);
void tunefs_block_signals(void);
void tunefs_unblock_signals(void);
errcode_t tunefs_open(const char *device, int flags);
errcode_t tunefs_close(void);

#endif  /* _LIBTUNEFS_H */
