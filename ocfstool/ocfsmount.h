/*
 * ocfsmount.h
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
 * Author: Manish Singh
 */

#ifndef __OCFS_MOUNT_H__
#define __OCFS_MOUNT_H__


#include <sys/types.h>


pid_t ocfs_mount   (GtkWindow *parent,
		    gchar     *device,
		    gboolean   query, 
		    gchar     *mountpoint,
		    gint      *errfd);
pid_t ocfs_unmount (gchar     *mountpoint,
		    gint      *errfd);


#endif /* __OCFS_MOUNT_H__ */
