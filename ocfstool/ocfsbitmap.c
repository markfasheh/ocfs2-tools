/*
 * ocfsbitmap.c
 *
 * The bitmap display tab
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

#include <gtk/gtk.h>

#include "libdebugocfs.h"

#include "ocfscellmap.h"
#include "ocfsbitmap.h"


GtkWidget *
ocfs_bitmap (const gchar *device,
	     gboolean     advanced)
{
  GtkWidget  *scrl_win;
  guint8     *bits;
  gint        length, i;
  GByteArray *map;

  if (device && libocfs_get_bitmap (device, &bits, &length) == 0)
    {
      scrl_win = gtk_widget_new (GTK_TYPE_SCROLLED_WINDOW,
				 "hscrollbar_policy", GTK_POLICY_NEVER,
				 "vscrollbar_policy", GTK_POLICY_ALWAYS,
				 "border_width", 4,
				 NULL);

      map = g_byte_array_new ();
      g_byte_array_set_size (map, length);

      for (i = 0; i < length; i++)
	map->data[i] = (bits[i / 8] & (1 << (i % 8))) ? 0xff : 0x00;

      g_free (bits);
      
      gtk_widget_new (OCFS_TYPE_CELL_MAP,
		      "map", map,
		      "parent", scrl_win,
		      NULL);

      return scrl_win;
    }
  else
    return gtk_label_new ("Invalid device");
}
