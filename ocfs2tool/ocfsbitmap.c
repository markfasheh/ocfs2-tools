/*
 * ocfsbitmap.c
 *
 * A simple object containing a bitmap
 *
 * Copyright (C) 2004 Oracle Corporation.  All rights reserved.
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

#include <glib-object.h>

#include "ocfsbitmap.h"


static void     ocfs_bitmap_class_init      (OcfsBitmapClass  *class);
static void     ocfs_bitmap_init            (OcfsBitmap       *bitmap);
static void     ocfs_bitmap_finalize        (GObject           *object);


static GObjectClass *parent_class = NULL;


GType
ocfs_bitmap_get_type (void)
{
  static GType bitmap_type = 0;

  if (! bitmap_type)
    {
      static const GTypeInfo bitmap_info =
      {
	sizeof (OcfsBitmapClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) ocfs_bitmap_class_init,
	NULL,           /* class_finalize */
	NULL,           /* class_data     */
	sizeof (OcfsBitmap),
	0,              /* n_preallocs    */
	(GInstanceInitFunc) ocfs_bitmap_init,
      };

      bitmap_type = g_type_register_static (G_TYPE_OBJECT, "OcfsBitmap",
					    &bitmap_info, 0);
    }
  
  return bitmap_type;
}

static void
ocfs_bitmap_class_init (OcfsBitmapClass *class)
{
  GObjectClass *object_class;

  object_class = (GObjectClass *) class;

  parent_class = g_type_class_peek_parent (class);

  object_class->finalize = ocfs_bitmap_finalize;
}

static void
ocfs_bitmap_init (OcfsBitmap *bitmap)
{
  bitmap->data = NULL;
  bitmap->len = 0;
}

static void
ocfs_bitmap_finalize (GObject *object)
{
  OcfsBitmap *bitmap = OCFS_BITMAP (object);

  g_free (bitmap->data);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

OcfsBitmap *
ocfs_bitmap_new (guchar *data,
                 guint   len)
{
  OcfsBitmap *bitmap;

  bitmap = g_object_new (OCFS_TYPE_BITMAP, NULL);

  bitmap->data = data;
  bitmap->len  = len;

  return bitmap;
}
