/*
 * ocfsfreespace.c
 *
 * The free space display tab
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
 * Author: Manish Singh
 */

#include <stdlib.h>

#include <gtk/gtk.h>

#include "libdebugocfs.h"

#include "ocfsguiutil.h"
#include "ocfsfreespace.h"


typedef struct _FreeNode FreeNode;
                                                                                
struct _FreeNode
{
    guint64 size;
    gint    offset;
};


static gint      size_compare   (gconstpointer  da,
				 gconstpointer  db);
static GSList   *get_free_areas (guint8        *bits,
                                 gint           length,
				 gint           extent_size);
static gboolean  list_populate  (GtkCList      *clist,
				 const gchar   *device);


static gint
size_compare (gconstpointer da,
              gconstpointer db)
{
  const FreeNode *a, *b;

  a = da;
  b = db;

  if (a->size < b->size)
    return 1;
  else if (a->size > b->size)
    return -1;
  else if (a->offset < b->offset)
    return -1;
  else if (a->offset > b->offset)
    return 1;
  else
    return 0;
}

static GSList *
get_free_areas (guint8 *bits,
		gint    length,
		gint    extent_size) 
{
  FreeNode *node = NULL;
  GSList   *node_list = NULL;
  gint      i;

  for (i = 0; i < length; i++)
    {
      if (!(bits[i / 8] & (1 << (i % 8))))
	{
	  if (!node)
	    {
	      node = g_new (FreeNode, 1);

	      node->size = extent_size;
	      node->offset = i;

	      node_list = g_slist_append (node_list, node);
	    }
	  else
	    node->size += extent_size;
	}
      else
	node = NULL;
    }

  node_list = g_slist_sort (node_list, size_compare);

  return node_list;
}

static gboolean
list_populate (GtkCList    *clist,
	       const gchar *device)
{
  guint8          *bits;
  gint             length;
  libocfs_volinfo *info;
  GSList          *node_list, *last;
  FreeNode        *node;
  gchar           *texts[2], buf[10];

  if (!device ||
      libocfs_get_bitmap (device, &bits, &length) ||
      libocfs_get_volume_info (device, &info))
    return FALSE;

  node_list = get_free_areas (bits, length, info->extent_size);

  free (info);
  g_free (bits);

  gtk_clist_freeze (clist);
  gtk_clist_clear (clist);

  texts[1] = buf;

  while (node_list)
    {
      node = node_list->data;

      texts[0] = ocfs_format_bytes (node->size, FALSE);

      g_snprintf (buf, sizeof (buf), "%d", node->offset);

      gtk_clist_append (clist, texts);
      
      g_free (texts[0]);

      last = node_list;
      node_list = node_list->next;
      
      g_free (node);
      g_slist_free_1 (last);
    }

  gtk_clist_thaw (clist);

  return TRUE;
}

GtkWidget *
ocfs_freespace (const gchar *device,
		gboolean     advanced)
{
  GtkCList     *clist;
  GtkWidget    *scrl_win;
  static gchar *titles[2] = { "Size", "Bit #" };

  clist = ocfs_build_clist (2, titles, &scrl_win);
  gtk_clist_set_auto_sort (clist, FALSE);

  if (list_populate (clist, device))
    return scrl_win;
  else
    return gtk_label_new ("Invalid device");
}
