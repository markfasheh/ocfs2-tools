/*
 * ocfsmount.c
 *
 * The mount and unmount actions
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

#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <glib.h>

#include "libdebugocfs.h"

#include "ocfsprocess.h"
#include "ocfsguiutil.h"
#include "ocfsmount.h"


#ifdef DEVEL_MACHINE
#define MOUNT_CMD   "ocfsmount"
#define UNMOUNT_CMD "ocfsumount"
#else
#define MOUNT_CMD   "mount"
#define UNMOUNT_CMD "umount"
#endif


pid_t
ocfs_mount (GtkWindow *parent,
	    gchar     *device,
	    gboolean   query,
	    gchar     *mountpoint,
	    gint      *errfd)
{
  libocfs_volinfo *info;
  gchar           *argv[] = { MOUNT_CMD, "-t", "ocfs", device, NULL, NULL };
  pid_t            pid;
  gchar           *def = "";

#ifndef DEVEL_MACHINE
  if (query)
    {
      if (libocfs_get_volume_info ((gchar *) device, &info) == 0)
	def = info->mountpoint;
      else
	info = malloc (4);

      mountpoint = ocfs_query_text (parent, "Mountpoint", def);

      if (!mountpoint || mountpoint[0] != '/')
	{
	  free (info);
	  return -ENOTDIR;
	}

      argv[4] = mountpoint;
    }
  else
    {
      if (libocfs_get_volume_info ((gchar *) device, &info) != 0)
	return -EIO;

      argv[4] = info->mountpoint;
    }
#else
  info = malloc (4);
  argv[4] = "/poop";
  def = "";
#endif

  pid = ocfs_process_run (argv[0], argv, NULL, errfd);

  free (info);

  return pid;
}

pid_t
ocfs_unmount (gchar *mountpoint,
	      gint  *errfd)
{
  gchar *argv[] = { UNMOUNT_CMD, mountpoint, NULL };

  return ocfs_process_run (argv[0], argv, NULL, errfd);
}
