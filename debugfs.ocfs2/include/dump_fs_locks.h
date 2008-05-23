/*
 * dump_fs_locks.h
 *
 * Function prototypes, macros, etc. for related 'C' files
 *
 * Copyright (C) 2005, 2008 Oracle.  All rights reserved.
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

#ifndef _DUMP_FS_LOCKS_H_
#define _DUMP_FS_LOCKS_H_

void dump_fs_locks(char *uuid, FILE *out, int dump_lvbs, int only_busy);

#endif		/* _DUMP_FS_LOCKS_H_ */
