/*
 * ocfsbool.h
 *
 * Defines a boolean type. Yes, we use our own boolean type. Eventually
 * someone will have to do a global search and replace to just use __s32
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
 * Authors: Neeraj Goyal, Suchit Kaura, Kurt Hackel, Sunil Mushran,
 *          Manish Singh, Wim Coekaerts, Mark Fasheh
 */


#ifndef  _OCFSBOOL_H_
#define  _OCFSBOOL_H_

typedef enum { false = 0, true = 1 } ocfs_bool;

/* This should be removed and all old code fixed to just use ocfs_bool */
typedef ocfs_bool bool;

#endif	/* _OCFSBOOL_H_ */
