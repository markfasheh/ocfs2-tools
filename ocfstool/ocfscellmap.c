/*
 * ocfscellmap.c
 *
 * A scrollable bitmap display widget
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

#include <gtk/gtk.h>

#include "ocfscellmap.h"


#define DEFAULT_CELL_WIDTH  10
#define DEFAULT_CELL_HEIGHT 10

#define CELL_MAP_MASK       (GDK_EXPOSURE_MASK |            \
			     GDK_POINTER_MOTION_MASK |      \
			     GDK_POINTER_MOTION_HINT_MASK | \
			     GDK_ENTER_NOTIFY_MASK |        \
			     GDK_BUTTON_PRESS_MASK |        \
			     GDK_BUTTON_RELEASE_MASK |      \
			     GDK_BUTTON1_MOTION_MASK)


enum {
  ARG_0,
  ARG_MAP,
  ARG_CELL_WIDTH,
  ARG_CELL_HEIGHT,
  ARG_HADJUSTMENT,
  ARG_VADJUSTMENT
};


static void     ocfs_cell_map_class_init      (OcfsCellMapClass  *class);
static void     ocfs_cell_map_init            (OcfsCellMap       *cell_map);
static void     ocfs_cell_map_set_arg         (GtkObject         *object,
					       GtkArg            *arg,
					       guint              arg_id);
static void     ocfs_cell_map_get_arg         (GtkObject         *object,
					       GtkArg            *arg,
					       guint              arg_id);
static void     ocfs_cell_map_destroy         (GtkObject         *object);
static void     ocfs_cell_map_finalize        (GtkObject         *object);
static void     ocfs_cell_map_size_request    (GtkWidget         *widget,
					       GtkRequisition    *requisition);
static void     create_offscreen_pixmap       (OcfsCellMap       *cell_map);
static void     paint_cell_map                (OcfsCellMap       *cell_map);
static void     ocfs_cell_map_state_changed   (GtkWidget         *widget,
					       GtkStateType       old_state);
static gboolean ocfs_cell_map_configure       (GtkWidget         *widget,
					       GdkEventConfigure *event);
static gboolean ocfs_cell_map_expose          (GtkWidget         *widget,
					       GdkEventExpose    *event);
static gboolean ocfs_cell_map_button_press    (GtkWidget         *widget,
					       GdkEventButton    *event);
static gboolean ocfs_cell_map_button_release  (GtkWidget         *widget,
					       GdkEventButton    *event);
static gboolean ocfs_cell_map_motion_notify   (GtkWidget         *widget,
					       GdkEventMotion    *event);
static void     ocfs_cell_map_set_adjustments (OcfsCellMap       *cell_map,
					       GtkAdjustment     *hadj,
					       GtkAdjustment     *vadj);
static void     update_adjustment             (GtkAdjustment     *adj,
					       OcfsCellMap       *cell_map);
static void     compute_vertical_scroll       (OcfsCellMap       *cell_map);


static GtkDrawingAreaClass *parent_class = NULL;


GtkType
ocfs_cell_map_get_type (void)
{
  static GtkType cell_map_type = 0;

  if (! cell_map_type)
    {
      static const GtkTypeInfo cell_map_info =
      {
        "OcfsCellMap",
	sizeof (OcfsCellMap),
	sizeof (OcfsCellMapClass),
	(GtkClassInitFunc) ocfs_cell_map_class_init,
	(GtkObjectInitFunc) ocfs_cell_map_init,
	/* reserved_1 */ NULL,
	/* reserved_2 */ NULL,
	(GtkClassInitFunc) NULL,
      };

      cell_map_type = gtk_type_unique (GTK_TYPE_DRAWING_AREA, &cell_map_info);
    }
  
  return cell_map_type;
}

static void
ocfs_cell_map_class_init (OcfsCellMapClass *class)
{
  GtkObjectClass *object_class;
  GtkWidgetClass *widget_class;

  object_class = (GtkObjectClass *) class;
  widget_class = (GtkWidgetClass *) class;

  parent_class = gtk_type_class (GTK_TYPE_WIDGET);

  gtk_object_add_arg_type ("OcfsCellMap::map",
			   GTK_TYPE_POINTER,
			   GTK_ARG_READWRITE,
			   ARG_MAP);
  gtk_object_add_arg_type ("OcfsCellMap::cell_width",
			   GTK_TYPE_INT,
			   GTK_ARG_READWRITE,
			   ARG_CELL_WIDTH);
  gtk_object_add_arg_type ("OcfsCellMap::cell_height",
			   GTK_TYPE_INT,
			   GTK_ARG_READWRITE,
			   ARG_CELL_HEIGHT);
  gtk_object_add_arg_type ("OcfsCellMap::hadjustment",
			   GTK_TYPE_ADJUSTMENT,
			   GTK_ARG_READWRITE | GTK_ARG_CONSTRUCT,
			   ARG_HADJUSTMENT);
  gtk_object_add_arg_type ("OcfsCellMap::vadjustment",
			   GTK_TYPE_ADJUSTMENT,
			   GTK_ARG_READWRITE | GTK_ARG_CONSTRUCT,
			   ARG_VADJUSTMENT);

  object_class->set_arg = ocfs_cell_map_set_arg;
  object_class->get_arg = ocfs_cell_map_get_arg;
  object_class->destroy = ocfs_cell_map_destroy;
  object_class->finalize = ocfs_cell_map_finalize;

  widget_class->state_changed = ocfs_cell_map_state_changed;
  widget_class->size_request = ocfs_cell_map_size_request;
  widget_class->configure_event = ocfs_cell_map_configure;
  widget_class->expose_event = ocfs_cell_map_expose;

  widget_class->button_press_event = ocfs_cell_map_button_press;
  widget_class->button_release_event = ocfs_cell_map_button_release;
  widget_class->motion_notify_event = ocfs_cell_map_motion_notify;

  widget_class->set_scroll_adjustments_signal =
    gtk_signal_new ("set_scroll_adjustments",
		    GTK_RUN_LAST,
		    object_class->type,
		    GTK_SIGNAL_OFFSET (OcfsCellMapClass, set_scroll_adjustments),
		    gtk_marshal_NONE__POINTER_POINTER,
		    GTK_TYPE_NONE, 2,
		    GTK_TYPE_ADJUSTMENT, GTK_TYPE_ADJUSTMENT);

  class->set_scroll_adjustments = ocfs_cell_map_set_adjustments;
}

static void
ocfs_cell_map_init (OcfsCellMap *cell_map)
{
  gint old_mask;

  cell_map->map = NULL;

  cell_map->cell_width = DEFAULT_CELL_WIDTH;
  cell_map->cell_height = DEFAULT_CELL_HEIGHT;

  cell_map->offscreen_pixmap = NULL;

  cell_map->hadj = NULL;
  cell_map->vadj = NULL;

  old_mask = gtk_widget_get_events (GTK_WIDGET (cell_map));
  gtk_widget_set_events (GTK_WIDGET (cell_map), old_mask | CELL_MAP_MASK);
}

static void
ocfs_cell_map_set_arg (GtkObject *object,
		       GtkArg    *arg,
		       guint      arg_id)
{
  OcfsCellMap *cell_map;

  cell_map = OCFS_CELL_MAP (object);

  switch (arg_id)
    {
    case ARG_MAP:
      ocfs_cell_map_set_map (cell_map, GTK_VALUE_POINTER (*arg));
      break;
    case ARG_CELL_WIDTH:
      cell_map->cell_width = GTK_VALUE_INT (*arg);
      break;
    case ARG_CELL_HEIGHT:
      cell_map->cell_height = GTK_VALUE_INT (*arg);
      break;
    case ARG_HADJUSTMENT:
      ocfs_cell_map_set_adjustments (cell_map, GTK_VALUE_POINTER (*arg), cell_map->vadj);
      break;
    case ARG_VADJUSTMENT:
      ocfs_cell_map_set_adjustments (cell_map, cell_map->hadj, GTK_VALUE_POINTER (*arg));
      break;
    default:
      break;
    }
}

static void
ocfs_cell_map_get_arg (GtkObject *object,
		       GtkArg    *arg,
		       guint      arg_id)
{
  OcfsCellMap *cell_map;

  cell_map = OCFS_CELL_MAP (object);

  switch (arg_id)
    {
    case ARG_MAP:
      GTK_VALUE_POINTER (*arg) = cell_map->map;
      break;
    case ARG_CELL_WIDTH:
      GTK_VALUE_INT (*arg) = cell_map->cell_width;
      break;
    case ARG_CELL_HEIGHT:
      GTK_VALUE_INT (*arg) = cell_map->cell_height;
      break;
    case ARG_HADJUSTMENT:
      GTK_VALUE_POINTER (*arg) = cell_map->hadj;
      break;
    case ARG_VADJUSTMENT:
      GTK_VALUE_POINTER (*arg) = cell_map->vadj;
      break;
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
    }
}

static void
ocfs_cell_map_destroy (GtkObject *object)
{
  OcfsCellMap *cell_map = OCFS_CELL_MAP (object);

  gtk_signal_disconnect_by_data (GTK_OBJECT (cell_map->hadj), cell_map);
  gtk_signal_disconnect_by_data (GTK_OBJECT (cell_map->vadj), cell_map);

  GTK_OBJECT_CLASS (parent_class)->destroy (object);
}

static void
ocfs_cell_map_finalize (GtkObject *object)
{
  OcfsCellMap *cell_map = OCFS_CELL_MAP (object);

  gtk_object_unref (GTK_OBJECT (cell_map->hadj));
  gtk_object_unref (GTK_OBJECT (cell_map->vadj));
 
  if (cell_map->offscreen_pixmap)
    gdk_pixmap_unref (cell_map->offscreen_pixmap);

  if (cell_map->map)
    g_byte_array_free (cell_map->map, TRUE);

  GTK_OBJECT_CLASS (parent_class)->finalize (object);
}

GtkWidget *
ocfs_cell_map_new (GByteArray *map)
{
  return gtk_widget_new (OCFS_TYPE_CELL_MAP,
			 "map", map,
			 NULL);
}

void
ocfs_cell_map_set_map (OcfsCellMap *cell_map,
		       GByteArray  *map)
{
  g_return_if_fail (OCFS_IS_CELL_MAP (cell_map));

  if (cell_map->map)
    g_byte_array_free (cell_map->map, TRUE);

  cell_map->map = map;

  paint_cell_map (cell_map);
  gtk_widget_queue_draw (GTK_WIDGET (cell_map));
}

void
ocfs_cell_map_set_cell_props (OcfsCellMap *cell_map,
			      gint         cell_width,
			      gint         cell_height)
{
  g_return_if_fail (OCFS_IS_CELL_MAP (cell_map));
  
#define SET_PROP(prop, def)	G_STMT_START {	\
  if (prop > -1)				\
    cell_map->prop = prop;			\
  else if (prop == -1)				\
    cell_map->prop = def;	} G_STMT_END

  SET_PROP (cell_width, DEFAULT_CELL_WIDTH);
  SET_PROP (cell_height, DEFAULT_CELL_HEIGHT);

#undef SET_PROP

  compute_vertical_scroll (cell_map);
}

static void
ocfs_cell_map_state_changed (GtkWidget    *widget,
			     GtkStateType  old_state)
{
  OcfsCellMap *cell_map = OCFS_CELL_MAP (widget);

  paint_cell_map (cell_map);
  gtk_widget_queue_draw (widget);
}

static void
ocfs_cell_map_size_request (GtkWidget      *widget,
			    GtkRequisition *requisition)
{
  OcfsCellMap *cell_map = OCFS_CELL_MAP (widget);

  requisition->width  = cell_map->cell_width;
  requisition->height = cell_map->cell_height;
}

static void
create_offscreen_pixmap (OcfsCellMap *cell_map)
{
  GtkWidget    *widget;

  if (GTK_WIDGET_REALIZED (cell_map))
    {
      widget = GTK_WIDGET (cell_map);

      if (cell_map->offscreen_pixmap)
	gdk_pixmap_unref (cell_map->offscreen_pixmap);

      cell_map->offscreen_pixmap = gdk_pixmap_new (widget->window,
						   widget->allocation.width,
						   widget->allocation.height,
						   -1);
      paint_cell_map (cell_map);
    }
}

static void
paint_cell_map (OcfsCellMap *cell_map)
{
  GtkWidget    *widget = GTK_WIDGET (cell_map);
  GtkStateType  state, type;
  gint          i;
  gint          width, height;
  gint          dx, dy;
  gint          per_row;
  gint          start, end;
  gint          val;

  if (!cell_map->offscreen_pixmap)
    return;

  gtk_paint_flat_box (widget->style,
		      cell_map->offscreen_pixmap,
		      GTK_STATE_NORMAL, GTK_SHADOW_NONE,
		      NULL, widget, "cell_map_bg",
		      0, 0,
		      widget->allocation.width,
		      widget->allocation.height);

  if (cell_map && cell_map->map->len)
    {
      width  = widget->allocation.width  - 1;
      height = widget->allocation.height - 1;

      per_row = width / cell_map->cell_width;
      per_row = MAX (1, per_row);
      
      val = cell_map->vadj->value;

      start = val / cell_map->cell_height * per_row;
      end = height / cell_map->cell_height * per_row + start;
      end = MIN (end, cell_map->map->len);

      if (end != cell_map->map->len && end + per_row > cell_map->map->len)
	{
	  if (val % cell_map->cell_height)
	    {
	      start += per_row;
	      end = cell_map->map->len;
	    }
	}

      state = GTK_STATE_NORMAL;
      if (!GTK_WIDGET_IS_SENSITIVE (widget))
	state = GTK_STATE_INSENSITIVE;

      for (i = start; i < end; i++)
	{
	  dx = (i % per_row) * cell_map->cell_width;
	  dy = ((i - start) / per_row) * cell_map->cell_height;

	  type = cell_map->map->data[i] ? GTK_STATE_SELECTED
					: state;

	  gdk_draw_rectangle (cell_map->offscreen_pixmap,
			      widget->style->fg_gc[state],
			      FALSE,
			      dx, dy,
			      cell_map->cell_width,
			      cell_map->cell_height);

	  gdk_draw_rectangle (cell_map->offscreen_pixmap,
			      widget->style->bg_gc[type],
			      TRUE,
			      dx + 1, dy + 1,
			      cell_map->cell_width - 1,
			      cell_map->cell_height - 1);
	}
    }
}

static gboolean
ocfs_cell_map_configure (GtkWidget         *widget,
			 GdkEventConfigure *event)
{
  OcfsCellMap *cell_map = OCFS_CELL_MAP (widget);

  create_offscreen_pixmap (cell_map);
  compute_vertical_scroll (cell_map);

  return FALSE;
}

static gboolean
ocfs_cell_map_expose (GtkWidget      *widget,
		      GdkEventExpose *event)
{
  if (GTK_WIDGET_DRAWABLE (widget))
    gdk_draw_pixmap (widget->window,
		     widget->style->black_gc,
		     OCFS_CELL_MAP (widget)->offscreen_pixmap,
		     event->area.x, event->area.y,
		     event->area.x, event->area.y,
		     event->area.width,
		     event->area.height);

  return FALSE;
}

static gboolean
ocfs_cell_map_button_press (GtkWidget      *widget,
			    GdkEventButton *event)
{
  return FALSE;
}

static gboolean
ocfs_cell_map_button_release (GtkWidget      *widget,
			      GdkEventButton *event)
{
  return FALSE;
}

static gboolean
ocfs_cell_map_motion_notify (GtkWidget      *widget,
			     GdkEventMotion *event)
{
  return FALSE;
}

static void
ocfs_cell_map_set_adjustments (OcfsCellMap   *cell_map,
			       GtkAdjustment *hadj,
			       GtkAdjustment *vadj)
{
  if (hadj)
    g_return_if_fail (GTK_IS_ADJUSTMENT (hadj));
  else
    hadj = GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 0.0, 0.0, 0.0, 0.0, 0.0));

  if (vadj)
    g_return_if_fail (GTK_IS_ADJUSTMENT (vadj));
  else
    vadj = GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 0.0, 0.0, 0.0, 0.0, 0.0));

  if (cell_map->hadj && (cell_map->hadj != hadj))
    {
      gtk_signal_disconnect_by_data (GTK_OBJECT (cell_map->hadj), cell_map);
      gtk_object_unref (GTK_OBJECT (cell_map->hadj));
    }

  if (cell_map->vadj && (cell_map->vadj != vadj))
    {
      gtk_signal_disconnect_by_data (GTK_OBJECT (cell_map->vadj), cell_map);
      gtk_object_unref (GTK_OBJECT (cell_map->vadj));
    }

  if (cell_map->hadj != hadj)
    {
      cell_map->hadj = hadj;
      gtk_object_ref (GTK_OBJECT (cell_map->hadj));
      gtk_object_sink (GTK_OBJECT (cell_map->hadj));

      gtk_signal_connect (GTK_OBJECT (cell_map->hadj), "changed",
			  GTK_SIGNAL_FUNC (update_adjustment), cell_map);
      gtk_signal_connect (GTK_OBJECT (cell_map->hadj), "value_changed",
			  GTK_SIGNAL_FUNC (update_adjustment), cell_map);

      update_adjustment (hadj, cell_map);
    }

  if (cell_map->vadj != vadj)
    {
      cell_map->vadj = vadj;
      gtk_object_ref (GTK_OBJECT (cell_map->vadj));
      gtk_object_sink (GTK_OBJECT (cell_map->vadj));

      gtk_signal_connect (GTK_OBJECT (cell_map->vadj), "changed",
			  GTK_SIGNAL_FUNC (update_adjustment), cell_map);
      gtk_signal_connect (GTK_OBJECT (cell_map->vadj), "value_changed",
			  GTK_SIGNAL_FUNC (update_adjustment), cell_map);

      update_adjustment (vadj, cell_map);
    }
}

static void
update_adjustment (GtkAdjustment *adj,
		   OcfsCellMap   *cell_map)
{
  gfloat prev_val;

  prev_val = adj->value;

  adj->value = MIN (adj->value, adj->upper - adj->page_size);
  adj->value = MAX (adj->value, 0.0);

  if (adj->value != prev_val)
    {
      gtk_signal_handler_block_by_func (GTK_OBJECT (adj),
					GTK_SIGNAL_FUNC (update_adjustment),
					cell_map);
      gtk_adjustment_changed (adj);
      gtk_signal_handler_unblock_by_func (GTK_OBJECT (adj),
					  GTK_SIGNAL_FUNC (update_adjustment),
					  cell_map);
    }

  if (GTK_WIDGET_REALIZED (cell_map))
    {
      if (adj == cell_map->vadj)
	{
	  paint_cell_map (cell_map);
	  gtk_widget_queue_draw (GTK_WIDGET (cell_map));
	}
      else
	g_warning ("Horizontal scrolling not supported");
    }
}

static void
compute_vertical_scroll (OcfsCellMap *cell_map)
{
  gint width, height;
  gint per_row, real_height;
  gint old_value;

  old_value = (gint) cell_map->vadj->value;

  width  = GTK_WIDGET (cell_map)->allocation.width - 1;
  height = GTK_WIDGET (cell_map)->allocation.height - 1;

  per_row = width / cell_map->cell_width;
  per_row = MAX (1, per_row);

  real_height = (cell_map->map->len / per_row + 1) * cell_map->cell_height;

  cell_map->vadj->upper = real_height;

  cell_map->vadj->step_increment = MIN (cell_map->vadj->upper, cell_map->cell_height);
  cell_map->vadj->page_increment = MIN (cell_map->vadj->upper, height - cell_map->cell_height * 2);
  cell_map->vadj->page_size      = MIN (cell_map->vadj->upper, height);
  cell_map->vadj->value          = MIN (cell_map->vadj->value, cell_map->vadj->upper - cell_map->vadj->page_size);
  cell_map->vadj->value          = MAX (cell_map->vadj->value, 0.0);

  gtk_signal_emit_by_name (GTK_OBJECT (cell_map->vadj), "changed");
}

#ifdef CELLMAP_TEST

#define LENGTH 160

int
main (int    argc,
      char **argv)
{
  GtkWidget  *window;
  GtkWidget  *vbox;
  GtkWidget  *scrl_win;
  GtkWidget  *cell_map;
  GtkWidget  *button;
  GByteArray *map;
  gint        i;

  gtk_init (&argc, &argv);

  map = g_byte_array_new ();
  g_byte_array_set_size (map, LENGTH);

  for (i = 0; i < LENGTH; i++)
    map->data[i] = i % 2 ? 0xff : 0x00;

  window = gtk_widget_new (GTK_TYPE_WINDOW,
			   "type", GTK_WINDOW_TOPLEVEL,
			   "border_width", 5,
			   "signal::delete-event", gtk_main_quit, NULL,
			   NULL);

  vbox = gtk_vbox_new (FALSE, 5);
  gtk_container_add (GTK_CONTAINER (window), vbox);

  scrl_win = gtk_widget_new (GTK_TYPE_SCROLLED_WINDOW,
			     "hscrollbar_policy", GTK_POLICY_NEVER,
			     "vscrollbar_policy", GTK_POLICY_ALWAYS,
			     "parent", vbox,
			     NULL);

  cell_map = gtk_widget_new (OCFS_TYPE_CELL_MAP,
			     "map", map,
			     "parent", scrl_win,
			     NULL);

  button = gtk_widget_new (GTK_TYPE_BUTTON,
			   "label", "Whee!",
			   "signal::clicked", gtk_main_quit, NULL,
			   NULL);
  gtk_box_pack_end (GTK_BOX (vbox), button, FALSE, FALSE, 0);

  gtk_widget_show_all (window);

  gtk_main ();

  return 0;
}
#endif
