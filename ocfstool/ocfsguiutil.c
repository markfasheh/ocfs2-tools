/*
 * ocfsguiutil.c
 *
 * Misc gui helper functions
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

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "ocfsprocess.h"
#include "ocfsguiutil.h"


static GtkWidget *make_dialog         (GtkWindow      *parent,
				       const gchar    *title,
				       gboolean        yes_no,
				       gchar          *label,
				       GtkSignalFunc   cb,
				       gpointer        cb_data,
				       GtkWidget     **ok);
static gboolean   dialog_key_pressed  (GtkWidget      *dialog,
				       GdkEventKey    *event);
static void       do_yes              (GtkObject      *dialog);
static void       get_text            (GtkObject      *button,
				       gchar         **text);
static gboolean   quit_on_delete      (void);
static void       get_filename        (GtkWidget      *button,
				       gchar         **fname);
static void       octal_insert_filter (GtkEditable    *editable,
				       const gchar    *text,
				       gint            length,
				       gint           *position);
static gchar     *uid_iterator        (gpointer        data);
static gchar     *gid_iterator        (gpointer        data);


GtkWindow *
ocfs_widget_get_toplevel (GtkWidget *widget)
{
  GtkWidget *toplevel;

  toplevel = gtk_widget_get_toplevel (widget);

  if (GTK_WIDGET_TOPLEVEL (toplevel))
    return GTK_WINDOW (toplevel);

  return NULL;
}

static gboolean
dialog_key_pressed (GtkWidget   *dialog,
		    GdkEventKey *event)
{
  if (event->keyval == GDK_Escape)
    {
      gtk_main_quit ();
      return TRUE;
    }

  return FALSE;
} 

static GtkWidget *
make_dialog (GtkWindow      *parent,
	     const gchar    *title,
	     gboolean        yes_no,
	     gchar          *label,
	     GtkSignalFunc   cb,
	     gpointer        cb_data,
	     GtkWidget     **ok)
{
  GtkWidget *dialog;
  GtkWidget *action_area;
  GtkWidget *hbbox;
  GtkWidget *button;

  dialog = gtk_widget_new (GTK_TYPE_DIALOG,
			   "title", title,
			   "allow_grow", FALSE,
			   "modal", TRUE,
			   "signal::delete-event", gtk_main_quit, NULL,
			   "signal::key_press_event", dialog_key_pressed, NULL,
			   NULL);
  gtk_window_set_transient_for (GTK_WINDOW (dialog), parent);

  gtk_widget_set (GTK_DIALOG (dialog)->vbox,
		  "border_width", 4,
		  NULL);

  action_area = GTK_DIALOG (dialog)->action_area;
  gtk_widget_set (action_area,
		  "border_width", 2,
		  "homogeneous", FALSE,
		  NULL);

  hbbox = gtk_hbutton_box_new ();
  gtk_button_box_set_spacing (GTK_BUTTON_BOX (hbbox), 4);
  gtk_box_pack_end (GTK_BOX (action_area), hbbox, FALSE, FALSE, 0);

  button = gtk_widget_new (GTK_TYPE_BUTTON,
			   "label", yes_no ? "Yes" : "OK",
			   "parent", hbbox,
			   "can_default", TRUE,
			   "has_default", TRUE,
			   "signal::clicked", cb ? cb : gtk_main_quit, cb_data,
			   NULL);

  gtk_object_set_data (GTK_OBJECT (dialog), "button", button);

  if (cb)
    gtk_widget_new (GTK_TYPE_BUTTON,
		    "label", yes_no ? "No" : "Cancel",
		    "parent", hbbox,
		    "can_default", TRUE,
		    "signal::clicked", gtk_main_quit, NULL,
		    NULL);

  if (label)
    {
      gtk_widget_new (GTK_TYPE_LABEL,
		      "label", label,
		      "parent", GTK_DIALOG (dialog)->vbox,
		      "wrap", TRUE,
		      "yalign", 0.0,
		      NULL);
      g_free (label);
    }

  if (ok)
    *ok = button;

  return dialog;
}

GtkWidget *
ocfs_dialog_new (GtkWindow     *parent,
		 const gchar   *title,
		 GtkSignalFunc  cb,
		 gpointer       cb_data)
{
  return make_dialog (parent, title, FALSE, NULL, cb, cb_data, NULL);
}

gboolean
ocfs_dialog_run (GtkWidget *dialog)
{
  GtkWidget *button;
  gboolean   ret = FALSE;

  gtk_widget_show_all (dialog);

  gtk_main ();

  button = gtk_object_get_data (GTK_OBJECT (dialog), "button");

  if (gtk_object_get_data (GTK_OBJECT (button), "success"))
    ret = TRUE;

  gtk_widget_destroy (dialog);

  return ret;
}

void
ocfs_error_box (GtkWindow   *parent,
		const gchar *errmsg, 
    		const gchar *format,
		...)
{
  GtkWidget *dialog;
  gchar     *msg, *str;
  va_list    args;

  va_start (args, format);
  msg = g_strdup_vprintf (format, args);
  va_end (args);

  if (errmsg && errmsg[0])
    {
      str = g_strconcat (msg, ":\n", errmsg, NULL);
      g_free (msg);
    }
  else
    str = msg;

  dialog = make_dialog (parent, "Error", FALSE, str, NULL, NULL, NULL);

  ocfs_dialog_run (dialog);
}

static void
do_yes (GtkObject *button)
{
  gtk_object_set_data (button, "success", GINT_TO_POINTER (1));
  gtk_main_quit ();
}

gboolean
ocfs_query_box (GtkWindow   *parent,
    		const gchar *format,
		...)
{
  GtkWidget *dialog;
  gchar     *msg;
  va_list    args;

  va_start (args, format);
  msg = g_strdup_vprintf (format, args);
  va_end (args);

  dialog = make_dialog (parent, "Query", TRUE, msg, do_yes, NULL, NULL);

  return ocfs_dialog_run (dialog);
}

static void
get_text (GtkObject  *button,
	  gchar     **text)
{
  GtkEntry *entry;
  gchar    *str;

  entry = gtk_object_get_data (button, "entry");

  str = gtk_entry_get_text (entry);

  if (strlen (str) == 0)
    return;

  *text = g_strdup (str);

  gtk_object_set_data (button, "success", GINT_TO_POINTER (1));

  gtk_main_quit ();
}

gchar *
ocfs_query_text (GtkWindow   *parent,
		 const gchar *prompt,
		 const gchar *def)
{
  GtkWidget *dialog;
  GtkWidget *table;
  GtkWidget *entry;
  GtkWidget *label;
  GtkWidget *button;
  gboolean   success;
  gchar     *str, *text;

  dialog = make_dialog (parent, prompt, FALSE, NULL, get_text, &text, &button);

  table = gtk_widget_new (GTK_TYPE_TABLE,
			  "n_rows", 1,
			  "n_columns", 2,
			  "homogeneous", FALSE,
			  "row_spacing", 4,
			  "column_spacing", 4,
			  "border_width", 4,
			  "parent", GTK_DIALOG (dialog)->vbox,
			  NULL);

  entry = gtk_entry_new ();
  gtk_entry_set_text (GTK_ENTRY (entry), def);
  gtk_table_attach_defaults (GTK_TABLE (table), entry, 1, 2, 0, 1);

  gtk_object_set_data (GTK_OBJECT (button), "entry",  entry);

  str = g_strconcat (prompt, ":", NULL);
  label = gtk_widget_new (GTK_TYPE_LABEL,
			  "label", str,
			  "xalign", 1.0,
			  NULL);
  gtk_table_attach_defaults (GTK_TABLE (table), label, 0, 1, 0, 1);

  success = ocfs_dialog_run (dialog);

  return success ? text : NULL;
}

static gboolean
quit_on_delete (void)
{
  gtk_main_quit ();
  return TRUE;
}

static void
get_filename (GtkWidget  *button,
	      gchar     **fname)
{
  GtkWidget *fs;

  fs = gtk_widget_get_toplevel (button);

  *fname = g_strdup (gtk_file_selection_get_filename (GTK_FILE_SELECTION (fs)));

  gtk_main_quit ();
}

gchar *
ocfs_get_filename (GtkWindow   *parent,
		   const gchar *title)
{
  /* NOTE: not threadsafe */
  static GtkWidget *fs = NULL;
  static gchar     *fname = NULL;
  struct stat       sbuf;

  if (fs == NULL)
    {
      fs = gtk_widget_new (GTK_TYPE_FILE_SELECTION,
			   "modal", TRUE,
			   "signal::delete-event", quit_on_delete, NULL,
			   NULL);

      gtk_signal_connect (GTK_OBJECT (fs), "destroy",
			  GTK_SIGNAL_FUNC (gtk_widget_destroyed), &fs);

      gtk_signal_connect (GTK_OBJECT (GTK_FILE_SELECTION (fs)->ok_button),
			  "clicked", GTK_SIGNAL_FUNC (get_filename), &fname);
      gtk_signal_connect (GTK_OBJECT (GTK_FILE_SELECTION (fs)->cancel_button),
			  "clicked", GTK_SIGNAL_FUNC (gtk_main_quit), NULL);
    }

  gtk_window_set_title (GTK_WINDOW (fs), title);
  gtk_window_set_transient_for (GTK_WINDOW (fs), parent);

  gtk_widget_show (fs);

  gtk_main ();

  gtk_widget_hide (fs);

  if (stat (fname, &sbuf) == 0 &&
      !ocfs_query_box (parent, "%s exists. Overwrite?", fname))
    {
      g_free (fname);
      return NULL;
    }

  return fname;
}

gchar *
ocfs_get_user_name (uid_t uid)
{
  struct passwd *pwd;
  gchar         *str;

  pwd = getpwuid (uid);

  if (pwd)
    str = g_strdup (pwd->pw_name);
  else
    str = g_strdup_printf ("%d", uid);

  return str;
}

gchar *
ocfs_get_group_name (gid_t gid)
{
  struct group *grp;
  gchar        *str;

  grp = getgrgid (gid);

  if (grp)
    str = g_strdup (grp->gr_name);
  else
    str = g_strdup_printf ("%d", gid);

  return str;
}

/* Replace with G_GUINT64_FORMAT when we can */
#ifdef __LP64__
#define FORMAT64 "%lu"
#else
#define FORMAT64 "%llu"
#endif

gchar *
ocfs_format_bytes (guint64  bytes,
		   gboolean show_bytes)
{
  gfloat  fbytes = bytes;
  gchar  *suffixes[] = { "K", "MB", "GB", "TB" };
  gint    i;

  if (bytes == 1)
    return g_strdup ("1 byte");
  else if (bytes < 1024)
    return g_strdup_printf (FORMAT64 " bytes", bytes);

  for (i = -1; i < 3 && fbytes >= 1024; i++)
    fbytes /= 1024;

  if (show_bytes)
    return g_strdup_printf ("%.1f %s (" FORMAT64 "b)", fbytes, suffixes[i],
			    bytes);
  else
    return g_strdup_printf ("%.0f %s", fbytes, suffixes[i]);
}

void
ocfs_build_list (GtkWidget        *list,
		 const gchar      *def,
		 OcfsListIterator  iterator,
		 gpointer          data)
{
  GtkContainer *container = GTK_CONTAINER (list);
  GtkWidget    *item;
  gchar        *buf;

  while ((buf = iterator (data)) != NULL)
    {
      item = gtk_list_item_new_with_label (buf);
      gtk_widget_show (item);
      gtk_container_add (container, item);

      if (strcmp (def, buf) == 0)
        gtk_list_item_select (GTK_LIST_ITEM (item));

      g_free (buf);
    }
}

static void
octal_insert_filter (GtkEditable *editable,
		     const gchar *text,
		     gint         length,
		     gint        *position)
{
  gint i;

  for (i = 0; i < length; i++)
    if (text[i] < 0x30 || text[i] > 0x37)
      gtk_signal_emit_stop_by_name (GTK_OBJECT (editable), "insert_text");
}

GtkWidget *
ocfs_build_octal_entry (const gchar *def)
{
  GtkWidget *entry;

  entry = gtk_widget_new (GTK_TYPE_ENTRY,
			  "max_length", 4,
			  "signal::insert_text", octal_insert_filter, NULL,
			  NULL);

  gtk_entry_set_text (GTK_ENTRY (entry), def);

  return entry;
}

GtkWidget *
ocfs_build_combo (void)
{
  GtkWidget *combo;

  combo = gtk_combo_new ();

  gtk_editable_set_editable (GTK_EDITABLE (GTK_COMBO (combo)->entry), FALSE);

  return combo;
}

static gchar *
uid_iterator (gpointer data)
{
  struct passwd *pwd;

  pwd = getpwent ();

  return pwd ? g_strdup (pwd->pw_name) : NULL;
}

GtkWidget *
ocfs_build_combo_user (const gchar *def)
{
  GtkWidget *combo;

  combo = ocfs_build_combo ();

  setpwent ();
  ocfs_build_list (GTK_COMBO (combo)->list, def, uid_iterator, NULL);
  endpwent ();

  return combo;
}

static gchar *
gid_iterator (gpointer data)
{
  struct group *grp;

  grp = getgrent ();

  return grp ? g_strdup (grp->gr_name) : NULL;
}   

GtkWidget *
ocfs_build_combo_group (const gchar *def)
{
  GtkWidget *combo;

  combo = ocfs_build_combo ();

  setgrent ();
  ocfs_build_list (GTK_COMBO (combo)->list, def, gid_iterator, NULL);
  endgrent ();

  return combo;
}

GtkCList *
ocfs_build_clist (gint        columns,
                  gchar      *titles[],
		  GtkWidget **scrl_win)
{
  GtkCList *clist;
  gint      i;

  clist = GTK_CLIST (gtk_clist_new_with_titles (columns, titles));

  gtk_clist_set_selection_mode (clist, GTK_SELECTION_BROWSE);

  gtk_clist_column_titles_passive (clist);

  for (i = 0; i < columns; i++)
    gtk_clist_set_column_auto_resize (clist, i, TRUE);

  gtk_clist_set_auto_sort (clist, TRUE);

  if (scrl_win)
    {
      *scrl_win = gtk_widget_new (GTK_TYPE_SCROLLED_WINDOW,
				  "hscrollbar_policy", GTK_POLICY_AUTOMATIC,
				  "vscrollbar_policy", GTK_POLICY_AUTOMATIC,
				  NULL);

      gtk_container_add (GTK_CONTAINER (*scrl_win), GTK_WIDGET (clist));
    }

  return clist;
}
