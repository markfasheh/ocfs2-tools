/*
 * ocfsformat.c
 *
 * The format dialog box
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
#include <grp.h>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

#include <gtk/gtk.h>

#include "libdebugocfs.h"

#include "ocfsprocess.h"
#include "ocfsguiutil.h"
#include "ocfsplist.h"
#include "ocfsformat.h"

enum {
  DEVICE,
  SIZE,
  LABEL,
  MOUNT,
  UID,
  GID,
  PERMS,
  CLEAR,
  FORCE,
  NUM_ENTRIES
};

/* FIXME: shouldn't really hardcode these */
#define SIZE_LABEL 64
#define SIZE_MOUNT (sizeof (((libocfs_volinfo *) 0)->mountpoint) / \
		    sizeof (((libocfs_volinfo *) 0)->mountpoint[0]) - 1)

typedef enum {
  ENTRY_COMBO,
  ENTRY_TEXT,
  ENTRY_HOSTNAME,
  ENTRY_OCTAL,
  ENTRY_SIZE,
  ENTRY_UID,
  ENTRY_GID,
  ENTRY_CHECK
} EntryType;

typedef struct _FormatEntry FormatEntry;

struct _FormatEntry
{
  const gchar *desc;
  const gchar *def;

  guint16      max;

  EntryType    type;

  gboolean     format_only;
  gboolean     advanced;

  GtkWidget   *entry;
};


static void      do_format     (GtkWidget   *button,
			        FormatEntry *entries);
static gchar    *size_iterator (gpointer     data);
static void      build_entries (GtkTable    *table,
				gboolean     advanced,
				gboolean     resize,
				FormatEntry *entries);
static gboolean  disk_op       (GtkWindow   *parent,
				gchar       *device,
				gboolean     advanced,
				gboolean     resize);


static void
do_format (GtkWidget   *button,
	   FormatEntry *entries)
{
  GtkWindow     *parent;
  gboolean       resize;
  struct passwd *pwd;
  struct group  *grp;
  pid_t          pid;
  gint           outfd, errfd;
  gboolean       success;
  gint           pos;
  gchar         *errmsg;
  gchar         *device;
  gchar         *size = NULL, *label = NULL, *mount = NULL;
  gchar         *uid, *gid, *perms;
  gchar         *query, *action, *actioning;
  gint           max_arg;
  gchar         *argv[] = {
    NULL,
    "-x", "-q",
    "-u", NULL,
    "-g", NULL,
    "-p", NULL,
    "-b", NULL,
    "-L", NULL,
    "-m", NULL,
    NULL, NULL, NULL,
    NULL
  };

  parent = ocfs_widget_get_toplevel (button);

  resize = GPOINTER_TO_INT (gtk_object_get_data (GTK_OBJECT (parent),
						 "resize_action"));

  if (resize)
    { 
      argv[0] = "resizeocfs";
      max_arg = 9;

      query = "resize";
      action = "Resize";
      actioning = "Resizing";
    }
  else
    {
      argv[0] = "mkfs.ocfs";
      max_arg = 15;

      query = "format";
      action = "Format";
      actioning = "Formatting";
    }

#define CHECK_TEXT(type, value, error)			G_STMT_START {	\
  gint len;								\
									\
  (value) = gtk_entry_get_text (GTK_ENTRY (entries[(type)].entry));	\
									\
  len = strlen ((value));						\
									\
  if (len == 0)								\
    {									\
      ocfs_error_box (parent, NULL, "Invalid %s", (error));		\
      return;								\
    }							} G_STMT_END

  if (!resize)
    CHECK_TEXT (LABEL, label, "volume label");

#undef CHECK_TEXT

#define CHECK_ENTRY(type, value, check, min, error)	G_STMT_START {	\
  gint len;								\
									\
  (value) = gtk_entry_get_text (GTK_ENTRY (entries[(type)].entry));	\
									\
  len = strlen ((value));						\
									\
  if (len < (min) || (value)[0] != (check))				\
    {									\
      ocfs_error_box (parent, NULL, "Invalid %s", (error));		\
      return;								\
    }							} G_STMT_END

  if (!resize)
    CHECK_ENTRY (MOUNT, mount, '/', 2, "mountpoint");

  CHECK_ENTRY (PERMS, perms, '0', 4, "protection");

#undef CHECK_ENTRY

#define COMBO_ENTRY(type, value) \
  (value) = gtk_entry_get_text (GTK_ENTRY (GTK_COMBO (entries[(type)].entry)->entry))
    
  COMBO_ENTRY (DEVICE, device);

  if (!resize)
    {
      COMBO_ENTRY (SIZE, size);
      pos = strspn (size, "0123456789");
      size = g_strndup (size, pos);
    }

  COMBO_ENTRY (UID, uid);
  pwd = getpwnam (uid);
  uid = g_strdup_printf ("%d", pwd->pw_uid);

  COMBO_ENTRY (GID, gid);
  grp = getgrnam (gid);
  gid = g_strdup_printf ("%d", grp->gr_gid);

#undef COMBO_ENTRY

#define OPT_CHECK(type, option)				G_STMT_START {	\
  GtkWidget *button;							\
									\
  button = entries[type].entry;						\
									\
  if (button)								\
    {									\
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)))	\
	argv[max_arg++] = option;					\
    }							} G_STMT_END

  OPT_CHECK (CLEAR, "-C");
  OPT_CHECK (FORCE, "-F");

#undef OPT_CHECK

  if (!ocfs_query_box (parent, "Are you sure you want to %s %s?", query, device))
    return;

  argv[4] = uid;
  argv[6] = gid;
  argv[8] = perms;

  if (!resize)
    {
      argv[10] = size;
      argv[12] = label;
      argv[14] = mount;
    }

  argv[max_arg++] = device;
  argv[max_arg] = NULL;

  pid = ocfs_process_run (argv[0], argv, &outfd, &errfd);

  g_free (uid);
  g_free (gid);

  success = ocfs_process_reap (parent, pid, FALSE, FALSE,
			       action, actioning,
			       outfd, NULL, errfd, &errmsg, NULL);
  if (!success)
    {
      ocfs_error_box (parent, errmsg, "%s error", action);
      g_free (errmsg);
    }
  else
    gtk_object_set_data (GTK_OBJECT (button), "success", GINT_TO_POINTER (1));

  gtk_main_quit ();
}

static gchar *
size_iterator (gpointer data)
{
  gint *i = data;

  *i += 1;

  return *i < 10 ? g_strdup_printf ("%d K", 2 << *i) : NULL;
}

static void
build_entries (GtkTable    *table,
	       gboolean     advanced,
	       gboolean     resize,
	       FormatEntry *entries)
{
  GtkWidget *label;
  GtkWidget *entry;
  gchar     *desc, *def;
  gint       i, j, row;
  gboolean   do_label;

  for (i = 0, row = 0; i < NUM_ENTRIES; i++)
    {
      if (resize && entries[i].format_only)
	{
	  entries[i].entry = NULL;
	  continue;
	}

      if (!advanced && entries[i].advanced)
	{
	  entries[i].entry = NULL;
	  continue;
	}

      do_label = TRUE;

      switch (entries[i].type)
	{
	case ENTRY_CHECK:
	  entry = gtk_check_button_new_with_label (entries[i].desc);

	  if (strcmp (entries[i].def, "1") == 0)
	    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (entry), TRUE);

	  do_label = FALSE;
	  break;

	case ENTRY_TEXT:
	case ENTRY_HOSTNAME:
	  entry = gtk_entry_new ();

	  if (entries[i].type == ENTRY_HOSTNAME)
	    {
	      def = g_new0 (char, entries[i].max + 1);
	      gethostname (def, entries[i].max + 1);
	      def[entries[i].max] = '\0';

	      gtk_entry_set_text (GTK_ENTRY (entry), def);

	      g_free (def);
	    }
	  else
	    gtk_entry_set_text (GTK_ENTRY (entry), entries[i].def);

	  gtk_entry_set_max_length (GTK_ENTRY (entry), entries[i].max);
	  break;

	case ENTRY_OCTAL:
	  entry = ocfs_build_octal_entry (entries[i].def);
	  break;

	case ENTRY_COMBO:
	case ENTRY_SIZE:
          entry = ocfs_build_combo ();

	  if (entries[i].type == ENTRY_SIZE)
	    {
	      j = 0;
	      def = g_strconcat (entries[i].def, " K", NULL);
	      ocfs_build_list (GTK_COMBO (entry)->list, def, size_iterator, &j);
	      g_free (def);
	    }

	  break;

	case ENTRY_UID:
	  entry = ocfs_build_combo_user (entries[i].def);
	  break;

	case ENTRY_GID:
	  entry = ocfs_build_combo_group (entries[i].def);
	  break;

	default:
	  entry = NULL;
	  break;
	}

      gtk_table_attach_defaults (table, entry, 1, 2, row, row + 1);
      entries[i].entry = entry;

      if (do_label)
	{
	  desc = g_strconcat (entries[i].desc, ":", NULL);
	  label = gtk_widget_new (GTK_TYPE_LABEL,
				  "label", desc,
				  "xalign", 1.0,
				  NULL);
	  g_free (desc);

	  gtk_table_attach_defaults (table, label, 0, 1, row, row + 1);
	}

      row++;
    }
}

static FormatEntry entries[] = {
  { "Device",                "",        0,          ENTRY_COMBO,    FALSE, FALSE, NULL },
  { "Block Size",            "128",     0,          ENTRY_SIZE,     TRUE,  FALSE, NULL },
  { "Volume Label",          "oracle",  SIZE_LABEL, ENTRY_TEXT,     TRUE,  FALSE, NULL },
  { "Mountpoint",            "/oracle", SIZE_MOUNT, ENTRY_TEXT,     TRUE,  FALSE, NULL },
  { "User",                  "root",    0,          ENTRY_UID,      FALSE, FALSE, NULL },
  { "Group",                 "root",    0,          ENTRY_GID,      FALSE, FALSE, NULL },
  { "Protection",            "0755",    0,          ENTRY_OCTAL,    FALSE, FALSE, NULL },
  { "Clear All Data Blocks", "",        0,          ENTRY_CHECK,    FALSE, TRUE,  NULL },
  { "Force",                 "",        0,          ENTRY_CHECK,    FALSE, TRUE,  NULL },
};

gboolean
disk_op (GtkWindow *parent,
	 gchar     *device,
	 gboolean   advanced,
	 gboolean   resize)
{
  GtkWidget    *dialog;
  GtkWidget    *table;
  GtkWidget    *item;
  GtkContainer *container;
  GList        *list, *last;
  gchar        *str, *title;
  gint          rows;

  list = ocfs_partition_list (TRUE);
  if (list == NULL)
    {
      ocfs_error_box (parent, NULL, "No unmounted partitions");
      return FALSE;
    }

  if (resize)
    {
      title = "OCFS Resize";

      if (advanced)
	rows = 9;
      else
	rows = 7;
    }
  else
    {
      title = "OCFS format";

      if (advanced)
	rows = 6;
      else
	rows = 4;
    }

  dialog = ocfs_dialog_new (parent, title, do_format, entries);
  gtk_object_set_data (GTK_OBJECT (dialog), "resize_action",
		       GINT_TO_POINTER (resize));

  table = gtk_widget_new (GTK_TYPE_TABLE,
			  "n_rows", rows,
			  "n_columns", 2,
			  "homogeneous", FALSE,
			  "row_spacing", 4,
			  "column_spacing", 4,
			  "border_width", 4,
			  "parent", GTK_DIALOG (dialog)->vbox,
			  NULL);

  build_entries (GTK_TABLE (table), advanced, resize, entries);

  list = g_list_sort (list, (GCompareFunc) strcmp);

  container = GTK_CONTAINER (GTK_COMBO (entries[DEVICE].entry)->list);

  while (list)
    {
      str = list->data;

      item = gtk_list_item_new_with_label (str);
      gtk_widget_show (item);
      gtk_container_add (container, item);

      if (device && strcmp (device, str) == 0)
	gtk_list_item_select (GTK_LIST_ITEM (item));

      g_free (str);

      last = list;
      list = list->next;

      g_list_free_1 (last);
    }

#ifdef FORMAT_TEST
  gtk_window_set_modal (GTK_WINDOW (dialog), FALSE);
#endif

  gtk_widget_grab_focus (entries[DEVICE].entry);

  return ocfs_dialog_run (dialog);
}

gboolean
ocfs_format (GtkWindow *parent,
	     gchar     *device,
	     gboolean   advanced)
{
  return disk_op (parent, device, advanced, FALSE);
}

gboolean
ocfs_resize (GtkWindow *parent,
	     gchar     *device,
	     gboolean   advanced)
{
  return disk_op (parent, device, advanced, TRUE);
}

#ifdef FORMAT_TEST
int
main (int    argc,
      char **argv)
{
  gtk_init (&argc, &argv);

  ocfs_format (NULL, NULL, FALSE);
  ocfs_resize (NULL, NULL, FALSE);

  return 0;
}
#endif
