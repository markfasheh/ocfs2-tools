/*
 * ocfsplist.h
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301 USA.
 *
 * Author: Manish Singh
 */

#ifndef __OCFS_PARTITION_LIST_H__
#define __OCFS_PARTITION_LIST_H__

#include <glib.h>


typedef struct _OcfsPartitionInfo OcfsPartitionInfo;

struct _OcfsPartitionInfo
{
  gchar      *device;
  gchar      *mountpoint;
  gchar      *fstype;
};


typedef void (*OcfsPartitionListFunc) (OcfsPartitionInfo *info,
				       gpointer           data);


void ocfs_partition_list (OcfsPartitionListFunc  func,
			  gpointer               data,
			  const gchar           *filter,
			  const gchar           *fstype,
			  gboolean               unmounted,
			  gboolean               async);


#endif /* __OCFS_PARTITION_LIST_H__ */
