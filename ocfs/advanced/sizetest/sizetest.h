/*
 * sizetest.h
 *
 * Prints sizes and offsets of structures and its elements.
 * Useful to ensure cross platform compatibility.
 *
 * Copyright (C) 2003 Oracle Corporation.  All rights reserved.
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
 * Authors: Kurt Hackel, Sunil Mushran, Manish Singh, Wim Coekaerts
 */

#ifndef _SIZETEST_H_
#define _SIZETEST_H_

#include <ocfs.h>

#ifdef USE_HEX
# define NUMFORMAT  "0x%x"
#else
#define NUMFORMAT  "%d"
# endif

#define SHOW_SIZEOF(x,y)  printf("sizeof("#x") = "NUMFORMAT"\n", sizeof(##y))

#define SHOW_OFFSET(x,y)  printf("\t"#x" = "NUMFORMAT" (%d)\n", \
				(void *)&(##y.##x)-(void *)&##y, sizeof(##y.##x))

#endif		/* _SIZETEST_H_ */
