/*
 * ocfsnodemap.c
 *
 * The node map display tab
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

#include <string.h>

#include <gtk/gtk.h>

#include "libdebugocfs.h"

#include "ocfsguiutil.h"
#include "ocfsnodemap.h"


gboolean
ocfs_nodemap_list (GtkCList    *clist,
		   const gchar *device,
		   guint        bitmap)
{
  GArray *nodes;
  gchar  *texts[4], buf[6];
  gint    i;

  gtk_clist_freeze (clist);
  gtk_clist_clear (clist);

  if (!device || libocfs_get_node_map (device, &nodes))
    return FALSE;

#define INODE(i) (g_array_index (nodes, libocfs_node, (i)))

  for (i = 0; i < nodes->len; i++)
    {
      if (bitmap & (1 << i))
	{
	  g_snprintf (buf, sizeof (buf), "%d", INODE(i).slot);
	  texts[0] = buf;

	  texts[1] = INODE(i).name;
	  texts[2] = INODE(i).addr;
	  texts[3] = INODE(i).guid;

	  gtk_clist_append (clist, texts);
	}
    }

  gtk_clist_thaw (clist);

  g_array_free (nodes, TRUE);

  return TRUE;
}

GtkWidget *
ocfs_nodemap (const gchar *device,
	      gboolean     advanced)
{
  GtkCList     *clist;
  GtkWidget    *scrl_win;
  static gchar *titles[4] = { "Slot #", "Node Name", "IP Address", "GUID" };

  clist = ocfs_build_clist (advanced ? 4 : 3, titles, &scrl_win);

  if (ocfs_nodemap_list (clist, device, 0xffffffff))
    return scrl_win;
  else
    return gtk_label_new ("Invalid device");
}
