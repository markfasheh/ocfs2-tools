/*
 * ocfsgenconfig.c
 *
 * The /etc/ocfs.conf generator
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gtk/gtk.h>

#include "ocfsprocess.h"
#include "ocfsguiutil.h"
#include "ocfsgenconfig.h"


enum {
  NODENAME,
  DEVICE,
  PORT
};

/* FIXME: shouldn't really hardcode this */
#define SIZE_HOSTNAME 255 

#define INTERFACE_LIST_CMD "/sbin/ifconfig | grep '^[a-z]' | cut -c 1-8"

#define INTERFACE_INFO_CMD "/sbin/ifconfig %s | grep 'inet addr:' | " \
			   "sed 's/.*inet addr:\\([0-9.]*\\).*/\\1/'"


static void     uid_gen               (GtkWindow    *parent);
static void     do_config             (GtkWidget    *button,
				       GtkWidget   **entries);
static void     numeric_insert_filter (GtkEditable  *editable,
				       const gchar  *text,
				       gint          length,
				       gint         *position);
static gboolean build_entries         (GtkWindow    *parent,
				       GtkTable     *table,
				       gboolean      advanced,
				       GtkWidget   **entries);


static void
uid_gen (GtkWindow *parent)
{
  gchar    *argv[] = { "ocfs_uid_gen", "-c", NULL };
  pid_t     pid;
  gint      outfd, errfd;
  gchar    *outmsg, *errmsg;
  gboolean  success;

  pid = ocfs_process_run (argv[0], argv, &outfd, &errfd);

  success = ocfs_process_reap (parent, pid, TRUE, TRUE,
			       "UID Generator", "UID Generator",
			       outfd, &outmsg, errfd, &errmsg,
			       NULL);
  if (!success)
    {
      unlink (CONFFILE);

      ocfs_error_box (parent, errmsg, "ocfs_uid_gen failed");
      g_free (errmsg);
    }

  g_free (outmsg);
}

static void
do_config (GtkWidget  *button,
	   GtkWidget **entries)
{
  GtkWindow  *parent;
  gchar      *interface, *address, *nodename;
  gint        port;
  gchar      *cmd;
  gchar     **info;
  FILE       *config;

  parent = ocfs_widget_get_toplevel (button);

  nodename = gtk_entry_get_text (GTK_ENTRY (entries[NODENAME]));
  if (strlen (nodename) == 0)
    {
      ocfs_error_box (parent, NULL, "Invalid node name");
      return;
    }

  port = strtol (gtk_entry_get_text (GTK_ENTRY (entries[PORT])), NULL, 10);
  if (port == 0 || port > 65535)
    {
      ocfs_error_box (parent, NULL, "Invalid port");
      return;
    }

  interface = gtk_entry_get_text (GTK_ENTRY (entries[DEVICE]));
  
  cmd = g_strdup_printf (INTERFACE_INFO_CMD, interface);

  info = ocfs_shell_output (parent, cmd); 

  if (info && info[0] && info[0][0])
    {
      address = g_strdup (info[0]);
    }
  else
    {
      ocfs_error_box (parent, NULL, "Invalid interface");
      return;
    }

  g_strfreev (info);

  config = fopen (CONFFILE, "w");

  if (config == NULL)
    {
      ocfs_error_box (parent, NULL, "Could not open %s", CONFFILE);
      return;
    }

  fprintf (config, "#\n");
  fprintf (config, "# ocfs config\n");
  fprintf (config, "# Ensure this file exists in /etc\n");
  fprintf (config, "#\n\n");
  fprintf (config, "\tnode_name = %s\n", nodename);
  fprintf (config, "\tip_address = %s\n", address);
  fprintf (config, "\tip_port = %d\n", port);
  fprintf (config, "\tcomm_voting = 1\n");

  fclose (config);

  g_free (address);

  gtk_main_quit ();

  uid_gen (parent);
}

static void
numeric_insert_filter (GtkEditable  *editable,
		       const gchar  *text,
		       gint          length,
		       gint         *position)
{
  gint i;

  for (i = 0; i < length; i++)
    if (text[i] < 0x30 || text[i] > 0x39)
      gtk_signal_emit_stop_by_name (GTK_OBJECT (editable), "insert_text");
}

static gboolean
build_entries (GtkWindow  *parent,
	       GtkTable   *table,
	       gboolean    advanced,
	       GtkWidget **entries)
{
  GtkWidget     *label; 
  GtkCombo      *combo; 
  GtkWidget     *entry;
  GtkContainer  *list;
  GtkWidget     *item;
  gchar         *def;
  gchar        **interfaces;
  int            i;
  
  combo = GTK_COMBO (ocfs_build_combo ());

  entries[DEVICE] = entry = combo->entry;
  list = GTK_CONTAINER (combo->list);

  interfaces = ocfs_shell_output (parent, INTERFACE_LIST_CMD);

  if (interfaces && interfaces[0] && interfaces[0][0])
    {
      for (i = 0; interfaces[i] != NULL; i++)
	{
	  item = gtk_list_item_new_with_label (interfaces[i]);
	  gtk_widget_show (item);
	  gtk_container_add (list, item);

	  if (i == 0)
	    gtk_list_item_select (GTK_LIST_ITEM (item));
	}
    }
  else
    {
      ocfs_error_box (parent, NULL, "Unable to query network interfaces");
      return FALSE;
    }

  g_strfreev (interfaces);

  gtk_table_attach_defaults (table, GTK_WIDGET (combo), 1, 2, 0, 1);

  label = gtk_widget_new (GTK_TYPE_LABEL,
			  "label", "Interface:",
			  "xalign", 1.0,
			  NULL);
  gtk_table_attach_defaults (table, label, 0, 1, 0, 1);

  entry = gtk_widget_new (GTK_TYPE_ENTRY,
			  "max_length", SIZE_HOSTNAME,
			  NULL);

  def = g_new0 (char, SIZE_HOSTNAME + 1);
  gethostname (def, SIZE_HOSTNAME + 1);
  def[SIZE_HOSTNAME] = '\0';

  gtk_entry_set_text (GTK_ENTRY (entry), def);

  g_free (def);

  gtk_table_attach_defaults (table, entry, 1, 2, 2, 3);
  entries[NODENAME] = entry;

  label = gtk_widget_new (GTK_TYPE_LABEL,
			  "label", "Node Name:",
			  "xalign", 1.0,
			  NULL);
  gtk_table_attach_defaults (table, label, 0, 1, 2, 3);

  entry = gtk_widget_new (GTK_TYPE_ENTRY,
			  "max_length", 5,
			  "signal::insert_text", numeric_insert_filter, NULL,
			  NULL);
  gtk_entry_set_text (GTK_ENTRY (entry), "7000");

  gtk_table_attach_defaults (table, entry, 1, 2, 1, 2);
  entries[PORT] = entry;

  label = gtk_widget_new (GTK_TYPE_LABEL,
			  "label", "Port:",
			  "xalign", 1.0,
			  NULL);
  gtk_table_attach_defaults (table, label, 0, 1, 1, 2);

  return TRUE;
}

void
ocfs_generate_config (GtkWindow *parent,
		      gboolean   advanced)
{
  struct stat  sbuf;
  GtkWidget   *dialog;
  GtkWidget   *table;
  GtkWidget   *entries[3];

  if (stat (CONFFILE, &sbuf) == 0)
    {
      ocfs_error_box (parent, NULL, "WARNING: %s exists\nIf you need to "
				    "change settings or do recovery, please "
				    "do so using command line tools",
				    CONFFILE);
      return;
    }

  dialog = ocfs_dialog_new (parent, "OCFS Generate Config", do_config, entries);

  table = gtk_widget_new (GTK_TYPE_TABLE,
			  "n_rows", 3,
			  "n_columns", 2,
			  "homogeneous", FALSE,
			  "row_spacing", 4,
			  "column_spacing", 4,
			  "border_width", 4,
			  "parent", GTK_DIALOG (dialog)->vbox,
			  NULL);

  if (!build_entries (parent, GTK_TABLE (table), advanced, entries))
    return;

#ifdef GENCONFIG_TEST
  gtk_window_set_modal (GTK_WINDOW (dialog), FALSE);
#endif

  gtk_widget_grab_focus (entries[DEVICE]);

  ocfs_dialog_run (dialog);
}

#ifdef GENCONFIG_TEST
int
main (int    argc,
      char **argv)
{
  gtk_init (&argc, &argv);

  ocfs_generate_config (NULL, TRUE);

  return 0;
}
#endif
