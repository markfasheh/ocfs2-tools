/*
 * ocfsbrowser.c
 *
 * The file browser tab
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
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>

#include <gtk/gtk.h>

#include "libdebugocfs.h"

#include "ocfsplist.h"
#include "ocfsprocess.h"
#include "ocfsguiutil.h"
#include "ocfsnodemap.h"
#include "ocfsbrowser.h"


typedef struct _FileInfo FileInfo;

struct _FileInfo
{
  libocfs_stat  stat;
  gchar        *fullpath;
  gboolean      filled;
};


static void       info_field          (GtkTable       *table,
				       gint           *pos,
				       gboolean        list,
				       const gchar    *tag,
				       const gchar    *desc);
static void       free_file_info      (gpointer        info);
static gboolean   dir_populate        (GtkCTree       *ctree,
				       GtkCTreeNode   *parent,
				       const gchar    *device);
static void       load_node_map       (GtkObject      *table,
				       guint           bitmap,
				       gboolean        valid);
static void       tree_select         (GtkCTree       *ctree,
				       GtkCTreeNode   *row,
				       gint            column,
				       GtkObject      *table);
static void       set_label_size      (GtkWidget      *label,
				       GtkStyle       *style);
static GtkWidget *create_context_menu (GtkObject      *ctree,
				       const gchar    *device,
				       GtkWindow      *parent);
static gboolean   tree_button_press   (GtkCTree       *ctree,
				       GdkEventButton *event,
				       const gchar    *device);
static void       dump_file           (GtkObject      *item,
				       FileInfo       *info);


static void
info_field (GtkTable    *table,
	    gint        *pos,
	    gboolean     list,
	    const gchar *tag,
	    const gchar *desc)
{
  gchar        *str;
  GtkWidget    *label;
  GtkWidget    *field;
  GtkWidget    *attach;
  static gchar *titles[2] = { "Slot #", "Node Name" };

  str = g_strconcat (desc, ":", NULL);
  label = gtk_widget_new (GTK_TYPE_LABEL,
			  "label", str,
			  "xalign", 1.0,
			  "yalign", 0.0,
			  "justify", GTK_JUSTIFY_RIGHT,
			  NULL);
  g_free (str);

  gtk_table_attach (table, label, 0, 1, *pos, *pos + 1,
		    GTK_FILL, GTK_FILL, 0, 0);

  str = g_strconcat (tag, "-desc", NULL);
  gtk_object_set_data (GTK_OBJECT (table), str, label);
  g_free (str);

  if (list)
    {
      field = GTK_WIDGET (ocfs_build_clist (2, titles, &attach));
      gtk_widget_set_usize (field, -1, 100);
    }
  else
    attach = field = gtk_widget_new (GTK_TYPE_LABEL,
				     "label", "N/A",
				     "xalign", 0.0,
				     "yalign", 0.0,
				     "justify", GTK_JUSTIFY_LEFT,
				     NULL);

  gtk_table_attach (table, attach, 1, 2, *pos, *pos + 1,
		    GTK_FILL, GTK_FILL, 0, 0);

  str = g_strconcat (tag, list ? "-list" : "-label", NULL);
  gtk_object_set_data (GTK_OBJECT (table), str, field);
  g_free (str);

  (*pos)++;
}

static void
free_file_info (gpointer info)
{
  g_free (((FileInfo *) info)->fullpath);
  g_free (info);
}

static gboolean
dir_populate (GtkCTree     *ctree,
	      GtkCTreeNode *parent,
	      const gchar  *device)
{
  GtkCTreeNode *node;
  GArray       *files;
  gchar        *dir, *text;
  gint          ret, i;
  gboolean      is_dir;
  FileInfo     *info;

  if (parent)
    {
      info = gtk_ctree_node_get_row_data (ctree, parent);

      if (!info->filled)
	{
	  info->filled = TRUE;

	  dir = g_strconcat (info->fullpath, "/", NULL);

	  node = GTK_CTREE_ROW (parent)->children;
	  gtk_ctree_remove_node (ctree, node);
	}
      else
	return TRUE;
    }
  else
    dir = g_strdup ("/");

  if (device)
    ret = libocfs_readdir (device, dir, FALSE, &files);
  else
    ret = -1;

  if (ret == 0 && files->len == 0)
    {
      g_array_free (files, TRUE);
      ret = -2;
    }

  if (ret != 0)
    {
      if (ret == -1)
	text = "No device selected";
      else if (ret == -2)
	text = "Empty directory";
      else
	text = "Error reading device";

      gtk_ctree_insert_node (ctree, parent, NULL, &text, 4,
			     NULL, NULL, NULL, NULL,
			     FALSE, FALSE);

      g_free (dir);

      return FALSE;
    }

  gtk_clist_freeze (GTK_CLIST (ctree));

  for (i = 0; i < files->len; i++)
    {
      info = g_new0 (FileInfo, 1);
      memcpy (info, &(g_array_index (files, libocfs_stat, (i))),
		    sizeof (libocfs_stat));

      text = info->stat.name;

      is_dir = S_ISDIR (info->stat.protection);

      info->fullpath = g_strconcat (dir, info->stat.name, NULL);

      node = gtk_ctree_insert_node (ctree, parent, NULL, &text, 4,
				    NULL, NULL, NULL, NULL,
				    !is_dir, FALSE);

      if (is_dir)
	{
	  info->filled = FALSE;

	  text = "Dummy";
	  gtk_ctree_insert_node (ctree, node, NULL, &text, 4,
				 NULL, NULL, NULL, NULL,
				 FALSE, FALSE);
	}

      gtk_ctree_node_set_row_data_full (ctree, node, info, free_file_info);
    }

  g_array_free (files, TRUE);
  g_free (dir);

  gtk_clist_thaw (GTK_CLIST (ctree));

  return TRUE;
}

static void
load_node_map (GtkObject   *table,
	       guint        bitmap,
	       gboolean     valid)
{
  GtkCList  *clist;
  GtkLabel  *label;
  gchar     *device;
  gchar     *texts[2];

  clist = gtk_object_get_data (table, "nodemap-list");
  label = gtk_object_get_data (table, "nodemap-desc");

  gtk_label_set_text (label, "Opened By:");

#define INVALID_LIST()				G_STMT_START {	\
  gtk_clist_clear (clist);					\
  gtk_widget_set_sensitive (GTK_WIDGET (clist)->parent, FALSE);	\
  gtk_widget_set_sensitive (GTK_WIDGET (label), FALSE);		\
  texts[0] = "N/A"; texts[1] = "";				\
  gtk_clist_append (clist, texts);				\
  return;					} G_STMT_END

  if (valid)
    {
      gtk_widget_set_sensitive (GTK_WIDGET (clist)->parent, TRUE);
      gtk_widget_set_sensitive (GTK_WIDGET (label), TRUE);
    }
  else
    INVALID_LIST ();

  device = gtk_object_get_data (table, "device");

  if (!ocfs_nodemap_list (clist, device, bitmap))
    INVALID_LIST ();
}

static void
tree_select (GtkCTree     *ctree,
	     GtkCTreeNode *row,
	     gint          column,
	     GtkObject    *table)
{
  FileInfo *info;
  GtkLabel *label;
  gchar    *str;
  gboolean  valid = TRUE;

  if (table == NULL)
    table = gtk_object_get_data (GTK_OBJECT (ctree), "table");

  info = gtk_ctree_node_get_row_data (ctree, row);

  if (info == NULL)
    {
      valid = FALSE;
      info = g_new0 (FileInfo, 1);
    }

#define SET_LABEL(tag)			G_STMT_START {	\
  label = gtk_object_get_data (table, tag "-label");	\
  gtk_label_set_text (label, valid ? str : "N/A");	\
  g_free (str);				} G_STMT_END

  if (info->stat.current_master != -1)
    str = g_strdup_printf ("%d", info->stat.current_master);
  else
    str = g_strdup ("None");

  SET_LABEL ("current_master");

#define SET_LABEL_BYTES(field, show)	G_STMT_START {	\
  str = ocfs_format_bytes (info->stat.field, (show));	\
  SET_LABEL (#field);			} G_STMT_END

  SET_LABEL_BYTES (size, TRUE);
  SET_LABEL_BYTES (alloc_size, TRUE);

#undef SET_LABEL_BYTES

#define SET_LABEL_NAME(func, field)	G_STMT_START {	\
  str = func (info->stat.field);			\
  SET_LABEL (#field);			} G_STMT_END

  SET_LABEL_NAME (ocfs_get_user_name,  uid);
  SET_LABEL_NAME (ocfs_get_group_name, gid);

#undef SET_LABEL_NAME

  str = g_strdup_printf ("0%o", info->stat.protection & 0777);
  SET_LABEL ("protection");

#undef SET_LABEL

  load_node_map (table, info->stat.open_map,
		 valid && S_ISREG (info->stat.protection));

  if (!valid)
    {
      g_free (info);
      info = NULL;
    }

  gtk_object_set_data (GTK_OBJECT (ctree), "selected-info", info);
}

static void
set_label_size (GtkWidget *label,
		GtkStyle  *style)
{
  gint width;

  width = gdk_string_width (label->style->font, "1.0 TB (1000000000000b)");
  gtk_widget_set_usize (label, width, -1);
}

static GtkWidget *
create_context_menu (GtkObject   *ctree,
		     const gchar *device,
		     GtkWindow   *parent)
{
  GtkWidget *menu;
  GtkWidget *item;
  FileInfo  *info;

  menu = gtk_menu_new ();

  info = gtk_object_get_data (ctree, "selected-info");

  if (info == NULL || S_ISDIR (info->stat.protection))
    return NULL;

  item = gtk_menu_item_new_with_label ("Dump File...");
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  gtk_widget_show (item);

  gtk_object_set_data (GTK_OBJECT (item), "parent", parent);
  gtk_object_set_data (GTK_OBJECT (item), "device", (gpointer) device);

  gtk_signal_connect (GTK_OBJECT (item), "activate",
		      GTK_SIGNAL_FUNC (dump_file),
		      info);

  return menu;
}

static void
ungrab_tree (GtkWidget *ctree)
{
  if (GTK_WIDGET_HAS_GRAB (ctree))
    {
      gtk_grab_remove (ctree);

      if (gdk_pointer_is_grabbed ())
	gdk_pointer_ungrab (GDK_CURRENT_TIME);
    }
}

static gboolean
tree_button_press (GtkCTree       *ctree,
		   GdkEventButton *event,
		   const gchar    *device)
{
  GtkWindow *parent;
  GtkWidget *menu;

  if (event->button == 3)
    {
      parent = ocfs_widget_get_toplevel (GTK_WIDGET (ctree));

      menu = create_context_menu (GTK_OBJECT (ctree), device, parent);

      if (menu)
	{
	  gtk_signal_connect_object (GTK_OBJECT (menu), "hide",
				     GTK_SIGNAL_FUNC (ungrab_tree),
				     GTK_OBJECT (ctree));

	  gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL,
			  3, event->time);

	  gtk_main ();

	  gtk_widget_destroy (menu);
	}
    }

  return FALSE;
}

static void
dump_file (GtkObject *item,
	   FileInfo  *info)
{
  GtkWindow *parent;
  gchar     *device, *dump;

  parent = gtk_object_get_data (item, "parent");
  device = gtk_object_get_data (item, "device");

  dump = ocfs_get_filename (parent, "Dump File");

  if (dump == NULL)
    return;

  if (libocfs_dump_file (device, info->fullpath, dump))
    ocfs_error_box (parent, NULL, "Couldn't dump %s on device %s to %s",
		    info->fullpath, device, dump);

  g_free (dump);
}

GtkWidget *
ocfs_browser (const gchar *device,
	      gboolean     advanced)
{
  GtkWidget *hbox;
  GtkWidget *scrl_win;
  GtkWidget *ctree;
  GtkWidget *label;
  GtkTable  *table;
  gchar     *dev;
  gint       pos = 0;

  hbox = gtk_hbox_new (FALSE, 4);

  scrl_win = gtk_widget_new (GTK_TYPE_SCROLLED_WINDOW,
			     "hscrollbar_policy", GTK_POLICY_AUTOMATIC,
			     "vscrollbar_policy", GTK_POLICY_AUTOMATIC,
			     "parent", hbox,
			     NULL);

  ctree = gtk_ctree_new (1, 0);

  gtk_widget_set (ctree,
		  "selection_mode", GTK_SELECTION_BROWSE,
		  "parent", scrl_win,
		  NULL);

  gtk_clist_set_button_actions (GTK_CLIST (ctree), 2, GTK_BUTTON_SELECTS);

  gtk_clist_set_column_auto_resize (GTK_CLIST (ctree), 0, TRUE);
  gtk_clist_column_titles_hide (GTK_CLIST (ctree));
  gtk_clist_set_auto_sort (GTK_CLIST (ctree), TRUE);

  table = GTK_TABLE (gtk_widget_new (GTK_TYPE_TABLE,
				     "n_rows", 7,
				     "n_columns", 2,
				     "homogeneous", FALSE,
				     "row_spacing", 4,
				     "column_spacing", 4,
				     "border_width", 4,
				     NULL));
  gtk_box_pack_end (GTK_BOX (hbox), GTK_WIDGET (table), FALSE, FALSE, 0);

  info_field (table, &pos, FALSE, "current_master", "Current Master");
  info_field (table, &pos, FALSE, "size",           "Size");
  info_field (table, &pos, FALSE, "alloc_size",     "Allocation Size");
  info_field (table, &pos, FALSE, "uid",            "User");
  info_field (table, &pos, FALSE, "gid",            "Group");
  info_field (table, &pos, FALSE, "protection",     "Protection");
  info_field (table, &pos, TRUE,  "nodemap",        "Opened By");

  label = gtk_object_get_data (GTK_OBJECT (table), "size-label");
  gtk_signal_connect (GTK_OBJECT (label), "style_set",
		      GTK_SIGNAL_FUNC (set_label_size), NULL);

  gtk_signal_connect (GTK_OBJECT (ctree), "tree_select_row",
		      GTK_SIGNAL_FUNC (tree_select), table);

  if (dir_populate (GTK_CTREE (ctree), NULL, device))
    {
      dev = g_strdup (device);
      gtk_object_weakref (GTK_OBJECT (ctree), g_free, dev);

      gtk_object_set_data (GTK_OBJECT (table), "device", dev);

      gtk_signal_connect (GTK_OBJECT (ctree), "tree_expand",
			  GTK_SIGNAL_FUNC (dir_populate), dev);

      if (advanced)
	{
	  gtk_signal_connect_after (GTK_OBJECT (ctree), "button_press_event",
				    GTK_SIGNAL_FUNC (tree_button_press), dev);

	  gtk_object_set_data (GTK_OBJECT (ctree), "table", table);
	}

      gtk_clist_select_row (GTK_CLIST (ctree), 0, 0);
    }

  return hbox;
}
