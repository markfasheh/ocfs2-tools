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

#include "ocfsmarshal.h"


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
  PROP_0,
  PROP_MAP,
  PROP_CELL_WIDTH,
  PROP_CELL_HEIGHT,
  PROP_HADJUSTMENT,
  PROP_VADJUSTMENT
};


static void     ocfs_cell_map_class_init      (OcfsCellMapClass  *class);
static void     ocfs_cell_map_init            (OcfsCellMap       *cell_map);
static void     ocfs_cell_map_set_property    (GObject           *object,
					       guint              property_id,
					       const GValue      *value,
					       GParamSpec        *pspec);
static void     ocfs_cell_map_get_property    (GObject           *object,
					       guint              property_id,
					       GValue            *value,
					       GParamSpec        *pspec);
static void     ocfs_cell_map_finalize        (GObject           *object);
static void     ocfs_cell_map_size_request    (GtkWidget         *widget,
					       GtkRequisition    *requisition);
static void     paint_cell_map                (GtkWidget         *widget);
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


GType
ocfs_cell_map_get_type (void)
{
  static GType cell_map_type = 0;

  if (! cell_map_type)
    {
      static const GTypeInfo cell_map_info =
      {
	sizeof (OcfsCellMapClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) ocfs_cell_map_class_init,
	NULL,           /* class_finalize */
	NULL,           /* class_data     */
	sizeof (OcfsCellMap),
	0,              /* n_preallocs    */
	(GInstanceInitFunc) ocfs_cell_map_init,
      };

      cell_map_type = g_type_register_static (GTK_TYPE_DRAWING_AREA,
					      "OcfsCellMap",
					      &cell_map_info, 0);
    }
  
  return cell_map_type;
}

static void
ocfs_cell_map_class_init (OcfsCellMapClass *class)
{
  GObjectClass   *object_class;
  GtkWidgetClass *widget_class;

  object_class = (GObjectClass *) class;
  widget_class = (GtkWidgetClass *) class;

  parent_class = g_type_class_peek_parent (class);

  object_class->set_property = ocfs_cell_map_set_property;
  object_class->get_property = ocfs_cell_map_get_property;
  object_class->finalize = ocfs_cell_map_finalize;

  g_object_class_install_property (object_class,
				   PROP_MAP,
				   g_param_spec_object ("map",
							"Map",
							"The cell bitmap",
							OCFS_TYPE_BITMAP,
							G_PARAM_READWRITE));

  g_object_class_install_property (object_class,
				   PROP_CELL_WIDTH,
				   g_param_spec_int ("cell_width",
						     "Cell Width",
						     "The width of each cell in pixels",
						     -1,
						     G_MAXINT,
						     DEFAULT_CELL_WIDTH,
						     G_PARAM_READWRITE));

  g_object_class_install_property (object_class,
				   PROP_CELL_HEIGHT,
				   g_param_spec_int ("cell_height",
						     "Cell Height",
						     "The height of each cell in pixels",
						     -1,
						     G_MAXINT,
						     DEFAULT_CELL_HEIGHT,
						     G_PARAM_READWRITE));

  g_object_class_install_property (object_class,
				   PROP_HADJUSTMENT,
				   g_param_spec_object ("hadjustment",
							"Horizontal Adjustment",
							"The GtkAdjustment for the horizontal position",
							GTK_TYPE_ADJUSTMENT,
							G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (object_class,
				   PROP_VADJUSTMENT,
				   g_param_spec_object ("vadjustment",
							"Vertical Adjustment",
							"The GtkAdjustment for the vertical position",
							GTK_TYPE_ADJUSTMENT,
							G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  widget_class->state_changed = ocfs_cell_map_state_changed;
  widget_class->size_request = ocfs_cell_map_size_request;
  widget_class->configure_event = ocfs_cell_map_configure;
  widget_class->expose_event = ocfs_cell_map_expose;

  widget_class->button_press_event = ocfs_cell_map_button_press;
  widget_class->button_release_event = ocfs_cell_map_button_release;
  widget_class->motion_notify_event = ocfs_cell_map_motion_notify;

  widget_class->set_scroll_adjustments_signal =
    g_signal_new ("set_scroll_adjustments",
		  G_OBJECT_CLASS_TYPE (object_class),
		  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		  G_STRUCT_OFFSET (OcfsCellMapClass, set_scroll_adjustments),
		  NULL, NULL,
		  _ocfs_marshal_VOID__OBJECT_OBJECT,
		  G_TYPE_NONE, 2,
		  GTK_TYPE_ADJUSTMENT,
		  GTK_TYPE_ADJUSTMENT);

  class->set_scroll_adjustments = ocfs_cell_map_set_adjustments;
}

static void
ocfs_cell_map_init (OcfsCellMap *cell_map)
{
  gint old_mask;

  cell_map->map = NULL;

  cell_map->cell_width = DEFAULT_CELL_WIDTH;
  cell_map->cell_height = DEFAULT_CELL_HEIGHT;

  cell_map->hadj = NULL;
  cell_map->vadj = NULL;

  old_mask = gtk_widget_get_events (GTK_WIDGET (cell_map));
  gtk_widget_set_events (GTK_WIDGET (cell_map), old_mask | CELL_MAP_MASK);
}

static void
ocfs_cell_map_set_property (GObject      *object,
			    guint         property_id,
			    const GValue *value,
			    GParamSpec   *pspec)
{
  OcfsCellMap *cell_map = OCFS_CELL_MAP (object);

  switch (property_id)
    {
    case PROP_MAP:
      ocfs_cell_map_set_map (cell_map, g_value_get_object (value));
      break;
    case PROP_CELL_WIDTH:
      cell_map->cell_width = g_value_get_int (value);
      break;
    case PROP_CELL_HEIGHT:
      cell_map->cell_height = g_value_get_int (value);
      break;
    case PROP_HADJUSTMENT:
      ocfs_cell_map_set_adjustments (cell_map, g_value_get_object (value),
				     cell_map->vadj);
      break;
    case PROP_VADJUSTMENT:
      ocfs_cell_map_set_adjustments (cell_map, cell_map->hadj,
				     g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
ocfs_cell_map_get_property (GObject *object,
			    guint       property_id,
                            GValue     *value,
			    GParamSpec *pspec)

{
  OcfsCellMap *cell_map = OCFS_CELL_MAP (object);

  switch (property_id)
    {
    case PROP_MAP:
      g_value_set_object (value, cell_map->map);
      break;
    case PROP_CELL_WIDTH:
      g_value_set_int (value, cell_map->cell_width);
      break;
    case PROP_CELL_HEIGHT:
      g_value_set_int (value, cell_map->cell_height);
      break;
    case PROP_HADJUSTMENT:
      g_value_set_object (value, cell_map->hadj);
      break;
    case PROP_VADJUSTMENT:
      g_value_set_object (value, cell_map->vadj);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
ocfs_cell_map_finalize (GObject *object)
{
  OcfsCellMap *cell_map = OCFS_CELL_MAP (object);

  g_object_unref (cell_map->hadj);
  g_object_unref (cell_map->vadj);
 
  if (cell_map->map)
    g_object_unref (cell_map->map);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

GtkWidget *
ocfs_cell_map_new (OcfsBitmap *map)
{
  return g_object_new (OCFS_TYPE_CELL_MAP,
		       "map", map,
		       NULL);
}

void
ocfs_cell_map_set_map (OcfsCellMap *cell_map,
		       OcfsBitmap  *map)
{
  g_return_if_fail (OCFS_IS_CELL_MAP (cell_map));

  if (cell_map->map)
    g_object_unref (cell_map->map);

  cell_map->map = g_object_ref (map);
  g_object_notify (G_OBJECT (cell_map), "map");

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
paint_cell_map (GtkWidget *widget)
{
  OcfsCellMap  *cell_map = OCFS_CELL_MAP (widget);
  GtkStateType  state, type;
  gint          i;
  gint          width, height;
  gint          dx, dy;
  gint          per_row;
  gint          start, end;
  gint          val;

  gtk_paint_flat_box (widget->style,
		      widget->window,
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

	  gdk_draw_rectangle (widget->window,
			      widget->style->fg_gc[state],
			      FALSE,
			      dx, dy,
			      cell_map->cell_width,
			      cell_map->cell_height);

	  gdk_draw_rectangle (widget->window,
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

  compute_vertical_scroll (cell_map);

  return FALSE;
}

static gboolean
ocfs_cell_map_expose (GtkWidget      *widget,
		      GdkEventExpose *event)
{
  if (GTK_WIDGET_DRAWABLE (widget))
    paint_cell_map (widget);

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
      g_signal_handlers_disconnect_by_func (GTK_OBJECT (cell_map->hadj),
					    update_adjustment, cell_map);
      g_object_unref (cell_map->hadj);
    }

  if (cell_map->vadj && (cell_map->vadj != vadj))
    {
      g_signal_handlers_disconnect_by_func (GTK_OBJECT (cell_map->vadj),
					    update_adjustment, cell_map);
      g_object_unref (cell_map->vadj);
    }

  if (cell_map->hadj != hadj)
    {
      cell_map->hadj = hadj;
      g_object_ref (cell_map->hadj);
      gtk_object_sink (GTK_OBJECT (cell_map->hadj));

      g_signal_connect (cell_map->hadj, "changed",
			G_CALLBACK (update_adjustment), cell_map);
      g_signal_connect (cell_map->hadj, "value_changed",
			G_CALLBACK (update_adjustment), cell_map);

      update_adjustment (hadj, cell_map);
    }

  if (cell_map->vadj != vadj)
    {
      cell_map->vadj = vadj;
      g_object_ref (cell_map->vadj);
      gtk_object_sink (GTK_OBJECT (cell_map->vadj));

      g_signal_connect (cell_map->vadj, "changed",
			G_CALLBACK (update_adjustment), cell_map);
      g_signal_connect (cell_map->vadj, "value_changed",
			G_CALLBACK (update_adjustment), cell_map);

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
      g_signal_handlers_block_by_func (adj, update_adjustment, cell_map);
      gtk_adjustment_changed (adj);
      g_signal_handlers_unblock_by_func (adj, update_adjustment, cell_map);
    }

  if (GTK_WIDGET_REALIZED (cell_map))
    {
      if (adj == cell_map->vadj)
	gtk_widget_queue_draw (GTK_WIDGET (cell_map));
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

  g_signal_emit_by_name (cell_map->vadj, "changed");
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
  OcfsBitmap *map;
  guchar     *data;
  gint        i;

  gtk_init (&argc, &argv);

  data = g_new (guchar, LENGTH);

  for (i = 0; i < LENGTH; i++)
    data[i] = i % 2 ? 0xff : 0x00;

  map = ocfs_bitmap_new (data, LENGTH);

  window = g_object_connect (g_object_new (GTK_TYPE_WINDOW,
					   "type", GTK_WINDOW_TOPLEVEL,
					   "border_width", 5,
					   NULL),
			     "signal::delete-event", gtk_main_quit, NULL,
			     NULL);

  vbox = gtk_vbox_new (FALSE, 5);
  gtk_container_add (GTK_CONTAINER (window), vbox);

  scrl_win = g_object_new (GTK_TYPE_SCROLLED_WINDOW,
			   "hscrollbar_policy", GTK_POLICY_NEVER,
			   "vscrollbar_policy", GTK_POLICY_ALWAYS,
			   "parent", vbox,
			   NULL);

  cell_map = g_object_new (OCFS_TYPE_CELL_MAP,
			   "map", map,
			   "parent", scrl_win,
			   NULL);

  button = g_object_connect (g_object_new (GTK_TYPE_BUTTON,
					   "label", "Whee!",
					   NULL),
			     "signal::clicked", gtk_main_quit, NULL,
			     NULL);
  gtk_box_pack_end (GTK_BOX (vbox), button, FALSE, FALSE, 0);

  gtk_widget_show_all (window);

  gtk_main ();

  return 0;
}
#endif
