/*
 * ver.c
 *
 * prints version
 *
 * Copyright (C) 2003 Oracle.  All rights reserved.
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
 * Authors: Kurt Hackel, Sunil Mushran
 */

#include <stdio.h>

void version(char *progname);

/*
 * version()
 *
 */
void version(char *progname)
{
	printf("%s %s %s (build %s)\n", progname,
					FSCK_BUILD_VERSION,
					FSCK_BUILD_DATE,
					FSCK_BUILD_MD5);
	return ;
}				/* version */
