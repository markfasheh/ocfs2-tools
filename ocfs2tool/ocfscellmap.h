/*
 * ocfscellmap.h
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

#ifndef __OCFS_CELL_MAP_H__
#define __OCFS_CELL_MAP_H__


#include <gtk/gtk.h>

#include "ocfsbitmap.h"


#define OCFS_TYPE_CELL_MAP            (ocfs_cell_map_get_type ())
#define OCFS_CELL_MAP(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), OCFS_TYPE_CELL_MAP, OcfsCellMap))
#define OCFS_CELL_MAP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), OCFS_TYPE_CELL_MAP, OcfsCellMapClass))
#define OCFS_IS_CELL_MAP(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), OCFS_TYPE_CELL_MAP))
#define OCFS_IS_CELL_MAP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), OCFS_TYPE_CELL_MAP))
#define OCFS_CELL_MAP_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), OCFS_TYPE_CELL_MAP, OcfsCellMapCLass))


typedef struct _OcfsCellMap      OcfsCellMap;
typedef struct _OcfsCellMapClass OcfsCellMapClass;

struct _OcfsCellMap
{
  GtkDrawingArea  parent_instance;

  OcfsBitmap     *map;

  gint            cell_width;
  gint            cell_height;

  GtkAdjustment  *hadj;
  GtkAdjustment  *vadj;
};

struct _OcfsCellMapClass
{
  GtkDrawingAreaClass parent_class;

  void  (*set_scroll_adjustments) (OcfsCellMap   *bd,
				   GtkAdjustment *hadjustment,
				   GtkAdjustment *vadjustment);
};


GType      ocfs_cell_map_get_type       (void);
GtkWidget *ocfs_cell_map_new            (OcfsBitmap  *map);
void       ocfs_cell_map_set_map        (OcfsCellMap *cell_map,
					 OcfsBitmap  *map);
void       ocfs_cell_map_set_cell_props (OcfsCellMap *cell_map,
					 gint         cell_width,
					 gint         cell_height);


#endif /* __OCFS_CELL_MAP_H__ */
