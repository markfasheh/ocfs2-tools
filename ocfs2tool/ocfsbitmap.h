/*
 * ocfsbitmap.h
 *
 * Function prototypes for related 'C' file.
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

#ifndef __OCFS_BITMAP_H__
#define __OCFS_BITMAP_H__


#include <glib-object.h>


#define OCFS_TYPE_BITMAP            (ocfs_bitmap_get_type ())
#define OCFS_BITMAP(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), OCFS_TYPE_BITMAP, OcfsBitmap))
#define OCFS_BITMAP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), OCFS_TYPE_BITMAP, OcfsBitmapClass))
#define OCFS_IS_BITMAP(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), OCFS_TYPE_BITMAP))
#define OCFS_IS_BITMAP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), OCFS_TYPE_BITMAP))
#define OCFS_BITMAP_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), OCFS_TYPE_BITMAP, OcfsBitmapCLass))


typedef struct _OcfsBitmap      OcfsBitmap;
typedef struct _OcfsBitmapClass OcfsBitmapClass;

struct _OcfsBitmap
{
  GObject  parent_instance;

  guchar  *data;
  guint    len;
};

struct _OcfsBitmapClass
{
  GObjectClass parent_class;
};


GType       ocfs_bitmap_get_type (void);
OcfsBitmap *ocfs_bitmap_new      (guchar *data,
                                  guint   len);


#endif /* __OCFS_BITMAP_H__ */
