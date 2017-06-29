/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dir_util.h
 *
 * Structures for dir iteration for the OCFS2 userspace library.
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301 USA.
 *
 * Authors: Joel Becker
 */

#ifndef _DIR_UTIL_H
#define _DIR_UTIL_H

static inline int is_dots(const char *name, unsigned int len)
{
	if (len == 0)
		return 0;

	if (name[0] == '.') {
		if (len == 1)
			return 1;
		if (len == 2 && name[1] == '.')
			return 1;
	}

	return 0;
}

#endif  /* _DIR_UTIL_H */
