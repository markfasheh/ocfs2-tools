/*
 * ocfsgeneral.c
 *
 * The general overview tab
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
#include <pwd.h>
#include <grp.h>

#include <gtk/gtk.h>

#include "libdebugocfs.h"

#include "ocfsguiutil.h"
#include "ocfsgeneral.h"


enum {
  EDIT_UID,
  EDIT_GID,
  EDIT_PERMS,
  NUM_EDITS
};

typedef enum
{
  NONE,
  BYTES,
  SIZE,
  UID,
  GID
} FormatType;

typedef struct _InfoEntry InfoEntry;
typedef struct _InfoState InfoState;

struct _InfoEntry
{
  const gchar *key;

  GtkWidget *(* build_func) (const gchar *def);
};

struct _InfoState
{
  gchar     *device;

  GtkLabel  *labels[NUM_EDITS];
  GtkWidget *entries[NUM_EDITS];
};


static void info_label      (GtkTable    *table,
			     gint        *pos,
			     gboolean     valid,
			     FormatType   type,
			     const gchar *desc,
			     const gchar *format,
			     ...) G_GNUC_PRINTF (6, 7);
static void build_edit_info (GtkTable    *table,
			     GtkObject   *labels,
			     InfoEntry   *entries,
			     InfoState   *state);
static void edit_info       (GtkWidget   *button,
			     GtkObject   *labels);
static void info_change     (GtkWidget   *button,
			     InfoState   *state);


static void
info_label (GtkTable    *table,
	    gint        *pos,
	    gboolean     valid,
	    FormatType   type,
	    const gchar *desc,
	    const gchar *format,
	    ...)
{
  va_list    args;
  gchar     *str;
  gint       id, size;
  guint64    bytes;
  GtkWidget *label;

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

  if (valid)
    {
      va_start (args, format);

      switch (type)
	{
	case UID:
	  id = va_arg (args, gint);
	  str = ocfs_get_user_name (id);
	  break;

	case GID:
	  id = va_arg (args, gint);
	  str = ocfs_get_group_name (id);
	  break;

	case BYTES:
	  bytes = va_arg (args, guint64);
	  str = ocfs_format_bytes (bytes, TRUE);
	  break;

	case SIZE:
	  size = va_arg (args, gint);
	  str = ocfs_format_bytes (size, FALSE);
	  break;

	case NONE:
	default:
	  str = g_strdup_vprintf (format, args);
	  break;
	}

      va_end (args);
    }

  label = gtk_widget_new (GTK_TYPE_LABEL,
			  "label", valid ? str : "N/A",
			  "xalign", 0.0,
			  "yalign", 0.0,
			  "justify", GTK_JUSTIFY_LEFT,
			  NULL);
  if (valid)
    g_free (str);

  gtk_table_attach (table, label, 1, 2, *pos, *pos + 1,
		    GTK_FILL, GTK_FILL, 0, 0);

  gtk_object_set_data (GTK_OBJECT (table), desc, label);

  *pos += 1;
}

static void
build_edit_info (GtkTable  *table,
		 GtkObject *labels,
		 InfoEntry *entries,
		 InfoState *state)
{
  GtkWidget *label;
  gchar     *desc, *def;
  gint       i;

  for (i = 0; i < NUM_EDITS; i++)
    {
      state->labels[i] = GTK_LABEL (gtk_object_get_data (labels,
				    entries[i].key));

      gtk_label_get (state->labels[i], &def);
      state->entries[i] = entries[i].build_func (def);

      gtk_table_attach_defaults (table, state->entries[i], 1, 2, i, i + 1);

      desc = g_strconcat (entries[i].key, ":", NULL);
      label = gtk_widget_new (GTK_TYPE_LABEL,
			      "label", desc,
			      "xalign", 1.0,
			      NULL);
      g_free (desc);

      gtk_table_attach_defaults (table, label, 0, 1, i, i + 1);
    }
}

static InfoEntry entries[] = {
  { "UID",        ocfs_build_combo_user  },
  { "GID",        ocfs_build_combo_group },
  { "Protection", ocfs_build_octal_entry },
};

static void
edit_info (GtkWidget *button,
	   GtkObject *labels)
{
  GtkWidget *dialog;
  GtkWidget *table;
  InfoState  state;

  state.device = gtk_object_get_data (labels, "device");

  dialog = ocfs_dialog_new (ocfs_widget_get_toplevel (button),
			    "Edit Device Info", info_change, &state);

  table = gtk_widget_new (GTK_TYPE_TABLE,
			  "n_rows", 3,
			  "n_columns", 2,
			  "homogeneous", FALSE,
			  "row_spacing", 4,
			  "column_spacing", 4,
			  "border_width", 4,
			  "parent", GTK_DIALOG (dialog)->vbox,
			  NULL);

  build_edit_info (GTK_TABLE (table), labels, entries, &state);

  ocfs_dialog_run (dialog);
}

static void
info_change (GtkWidget *button,
	     InfoState *state)
{
  GtkWindow     *parent;
  const gchar   *buf;
  struct passwd *pwd;
  struct group  *grp;
  gint           len;
  gint           perms, uid, gid;

  parent = ocfs_widget_get_toplevel (button);

  buf = gtk_entry_get_text (GTK_ENTRY (state->entries[EDIT_PERMS]));

  len = strlen (buf);

  if (len == 0 || len < 4 || buf[0] != '0')
    {
      ocfs_error_box (parent, NULL, "Invalid protection");
      return;
    }

  perms = strtol (buf, NULL, 8);

#define COMBO_ENTRY(type) \
  buf = gtk_entry_get_text (GTK_ENTRY (GTK_COMBO (state->entries[(type)])->entry))

  COMBO_ENTRY (EDIT_UID);
  pwd = getpwnam (buf);
  uid = pwd->pw_uid;

  COMBO_ENTRY (EDIT_GID);
  grp = getgrnam (buf);
  gid = grp->gr_gid;


  if (libocfs_chown_volume (state->device, perms, uid, gid) != 0)
    {
      ocfs_error_box (parent, NULL, "Unable to chown volume %s", state->device);
      return;
    }

  buf = gtk_entry_get_text (GTK_ENTRY (state->entries[EDIT_PERMS]));
  gtk_label_set_text (state->labels[EDIT_PERMS], buf);

  COMBO_ENTRY (EDIT_UID);
  gtk_label_set_text (state->labels[EDIT_UID], buf);

  COMBO_ENTRY (EDIT_GID);
  gtk_label_set_text (state->labels[EDIT_GID], buf);

#undef COMBO_ENTRY

  gtk_main_quit (); 
}
    	
GtkWidget *
ocfs_general (const gchar *device,
	      gboolean     advanced)
{
  gboolean         valid = TRUE;
  libocfs_volinfo *info = NULL;
  GtkTable        *table;
  GtkWidget       *hbox;
  GtkWidget       *button;
  gint             pos = 0;

  if (device)
    libocfs_get_volume_info (device, &info);

  if (!info)
    {
      valid = FALSE;
      info = malloc (sizeof (libocfs_volinfo));
    }

  table = GTK_TABLE (gtk_widget_new (GTK_TYPE_TABLE,
				     "n_rows", advanced ? 10 : 9,
				     "n_columns", 2,
				     "homogeneous", FALSE,
				     "row_spacing", 4,
				     "column_spacing", 4,
				     "border_width", 4,
				     NULL));

  info_label (table, &pos, valid, NONE, "Version", "%d.%d",
	      info->major_ver, info->minor_ver);

  info_label (table, &pos, valid, NONE, "Mountpoint", info->mountpoint);

  info_label (table, &pos, valid, BYTES, "Volume Length", "%llu",
	      info->length);
  info_label (table, &pos, valid, NONE, "Number of Extents", "%llu",
	      info->num_extents);
  info_label (table, &pos, valid, SIZE, "Extent Size", "%d",
	      info->extent_size);

  info_label (table, &pos, valid, UID, "UID", "%d",  info->uid);
  info_label (table, &pos, valid, GID, "GID", "%d", info->gid);

  info_label (table, &pos, valid, NONE, "Protection", "0%o",
	      info->protection & 0777);

  free (info);

  if (advanced)
    {
      hbox = gtk_hbox_new (FALSE, 0);
      gtk_table_attach (table, hbox, 1, 2, pos, pos + 1,
	  		GTK_FILL, GTK_FILL, 0, 0);

      button = gtk_widget_new (GTK_TYPE_BUTTON,
			       "label", "Edit...",
			       "sensitive", valid,
			       "signal::clicked", edit_info, table,
			       NULL);

      gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, FALSE, 0);

      if (valid)
	gtk_object_set_data_full (GTK_OBJECT (table), "device",
				  g_strdup (device), g_free);
    }

  return GTK_WIDGET (table);
}
