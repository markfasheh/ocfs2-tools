/*
 * ocfstool.c
 *
 * The main gui
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
#include <signal.h>

#include <gtk/gtk.h>

#include "libdebugocfs.h"
#include "bindraw.h"

#include "ocfsprocess.h"
#include "ocfsguiutil.h"
#include "ocfsplist.h"
#include "ocfsmount.h"
#include "ocfsformat.h"
#include "ocfsgenconfig.h"
#include "ocfsgeneral.h"
#include "ocfsbrowser.h"
#include "ocfsnodemap.h"
#include "ocfsbitmap.h"
#include "ocfsfreespace.h"


typedef GtkWidget *(*TabFunc) (const gchar *device,
			       gboolean     advanced);


static void       cleanup                (void);
static void       usage                  (char          *prgname);
static void       about                  (GtkCList      *clist);
static void       level                  (GtkCList      *clist,
					  guint          advanced);
static void       select_device          (GtkCList      *clist,
					  const gchar   *device);
static void       refresh_partition_list (GtkCList      *clist);
static gint       list_compare           (GtkCList      *clist,
					  gconstpointer  p1,
					  gconstpointer  p2);
static void       update_tab             (GtkObject     *clist,
					  const gchar   *tag,
					  TabFunc        func,
					  const gchar   *device);
static void       update_notebook        (GtkObject     *clist,
					  const gchar   *device);
static void       list_select            (GtkCList      *clist,
					  gint           row,
					  gint           column,
					  GdkEvent      *event);
static GtkWidget *create_partition_list  (void);
static void       mount                  (GtkWidget     *button,
					  GtkCList      *clist);
static void       unmount                (GtkWidget     *button,
					  GtkCList      *clist);
static void       disk_op                (GtkCList      *clist,
					  guint          action);
static void       genconfig              (GtkCList      *clist);
static void       refresh                (GtkCList      *clist);
static GtkWidget *create_action_area     (GtkCList      *clist);
static void       tab_frame              (const gchar   *tag,
					  const gchar   *desc,
					  GtkWidget     *notebook, 
					  GtkWidget     *clist,
					  TabFunc        func);
static void       create_window          (void);
static void       handle_signal          (gint           sig);


static void
cleanup (void)
{
  gtk_main_quit ();
}

static void
usage (char *prgname)
{
  g_print ("Usage: %s [OPTION]...\n\n", prgname);
  g_print ("Options:\n");
  g_print ("  -V, --version  print version information and exit\n");
  g_print ("      --help     display this help and exit\n");
}

static void
about (GtkCList *clist)
{
  GtkWidget *dialog;

  dialog = ocfs_dialog_new (ocfs_widget_get_toplevel (GTK_WIDGET (clist)),
			    "About", NULL, NULL);


  gtk_widget_new (GTK_TYPE_LABEL,
		  "label", "Oracle Cluster Filesytem Tool\n"
			   "Version " OCFSTOOL_VERSION "\n\n"
			   "Copyright (C) Oracle Corporation 2002\n"
			   "All Rights Reserved",
		  "parent", GTK_DIALOG (dialog)->vbox,
		  NULL);

  ocfs_dialog_run (dialog);
}

static void
level (GtkCList *clist,
       guint     advanced)
{
  guint      old;
  gint       row;
  gchar     *device;
  GtkWidget *item;

  old = GPOINTER_TO_UINT (gtk_object_get_data (GTK_OBJECT (clist), "advanced"));

  if (old == advanced)
    return;

  gtk_object_set_data (GTK_OBJECT (clist), "advanced",
		       GUINT_TO_POINTER (advanced));

  if (clist->selection)
    {
      row = GPOINTER_TO_INT (clist->selection->data);

#ifdef DEVEL_MACHINE
      device = "test.dump";
#else
      gtk_clist_get_text (clist, row, 0, &device);
#endif  
    }
  else
    device = NULL;

  item = gtk_object_get_data (GTK_OBJECT (clist), "resize_item");

  if (advanced)
    gtk_widget_show (item);
  else
    gtk_widget_hide (item);
    
  update_notebook (GTK_OBJECT (clist), device);
}

static void
select_device (GtkCList    *clist,
	       const gchar *device)
{
  GList       *list;
  GtkCListRow *row;
  gint         i;

  for (list = clist->row_list, i = 0; list; list = list->next, i++)
    {
      row = list->data;

      if (strcmp (device, GTK_CELL_TEXT (row->cell[0])->text) == 0)
	{
	  gtk_clist_select_row (clist, i, 0);

	  if (!gtk_clist_row_is_visible (clist, i))
	    gtk_clist_moveto (clist, i, -1, 0.5, 0.0);
	}
    }
}

static void
refresh_partition_list (GtkCList *clist)
{
  GList             *list, *last;
  OcfsPartitionInfo *info;
  gchar             *texts[2], *device = NULL;
  gint               row;

  if (clist->selection)
    {
      row = GPOINTER_TO_INT (clist->selection->data);

      gtk_clist_get_text (clist, row, 0, &device);
      device = g_strdup (device);
    }

  gtk_clist_freeze (clist);

  gtk_clist_clear (clist);

  list = ocfs_partition_list (FALSE);

#ifndef DEVEL_MACHINE
  while (list)
    {
      last = list;
      list = list->next;

      info = last->data;

      texts[0] = info->device;
      texts[1] = info->mountpoint ? info->mountpoint : "";

      gtk_clist_append (clist, texts);

      g_free (info->device);
      g_free (info->mountpoint);

      g_list_free_1 (last);
    }
#else
  last = list;
  info = NULL;

  texts[0] = "/dev/hda1"; 
  texts[1] = "/"; 
  gtk_clist_append (clist, texts);

  texts[0] = "/dev/hda2"; 
  texts[1] = ""; 
  gtk_clist_append (clist, texts);
#endif

  if (device)
    select_device (clist, device);

  gtk_clist_thaw (clist);
}

static gint
list_compare (GtkCList      *clist,
	      gconstpointer  p1,
	      gconstpointer  p2)
{
  gchar       *d1, *d2;
  gchar       *m1, *m2;

  GtkCListRow *row1 = (GtkCListRow *) p1;
  GtkCListRow *row2 = (GtkCListRow *) p2;

  d1 = GTK_CELL_TEXT (row1->cell[0])->text;
  d2 = GTK_CELL_TEXT (row2->cell[0])->text;

  m1 = GTK_CELL_TEXT (row1->cell[1])->text;
  m2 = GTK_CELL_TEXT (row2->cell[1])->text;

  if (m1 && !m2)
    return -1;
  else if (!m1 && m2)
    return 1;
  else
    return strcmp (d1, d2);
}

static void
update_tab (GtkObject   *clist,
	    const gchar *tag,
	    TabFunc      func,
	    const gchar *device)
{
  gboolean   advanced;
  gchar     *str;
  GtkWidget *container;
  GtkWidget *info;

  advanced = gtk_object_get_data (clist, "advanced") != NULL;

  str = g_strconcat (tag, "-frame", NULL);
  container = gtk_object_get_data (clist, str);
  g_free (str);

  gtk_widget_destroy (GTK_BIN (container)->child);

  info = func (device, advanced);
  gtk_container_add (GTK_CONTAINER (container), info);
  gtk_widget_show_all (info);
}

static void
update_notebook (GtkObject   *clist,
		 const gchar *device)
{
  update_tab (clist, "general",   ocfs_general,   device);
  update_tab (clist, "browser",   ocfs_browser,   device);
  update_tab (clist, "nodemap",   ocfs_nodemap,   device);
  update_tab (clist, "bitmap",    ocfs_bitmap,    device);
  update_tab (clist, "freespace", ocfs_freespace, device);
}

static void
list_select (GtkCList *clist,
	     gint      row,
	     gint      column,
	     GdkEvent *event)
{
  gchar     *device, *mountpoint;
  GtkWidget *button;

  gtk_clist_get_text (clist, row, 0, &device);
  gtk_clist_get_text (clist, row, 1, &mountpoint);

  if (mountpoint && mountpoint[0] != '\0')
    {
      button = gtk_object_get_data (GTK_OBJECT (clist), "mount-button");
      gtk_widget_set_sensitive (button, FALSE);

      button = gtk_object_get_data (GTK_OBJECT (clist), "unmount-button");
      gtk_widget_set_sensitive (button, TRUE);
    }
  else
    {
      button = gtk_object_get_data (GTK_OBJECT (clist), "mount-button");
      gtk_widget_set_sensitive (button, TRUE);

      button = gtk_object_get_data (GTK_OBJECT (clist), "unmount-button");
      gtk_widget_set_sensitive (button, FALSE);
    }

#ifdef DEVEL_MACHINE
  device = "test.dump";
#endif  

  update_notebook (GTK_OBJECT (clist), device);
}

static GtkWidget *
create_partition_list (void)
{
  GtkCList     *clist;
  static gchar *titles[2] = { "Device", "Mountpoint" };

  clist = ocfs_build_clist (2, titles, NULL);

  gtk_clist_set_compare_func (clist, list_compare);

  gtk_signal_connect (GTK_OBJECT (clist), "select_row",
		      GTK_SIGNAL_FUNC (list_select), NULL);

  return GTK_WIDGET (clist);
}

static void
mount (GtkWidget *button,
       GtkCList  *clist)
{
  GtkWindow *parent;
  pid_t      pid;
  gint       errfd;
  gboolean   success, killed;
  gchar     *device, *mountpoint = NULL;
  gchar     *errmsg;
  gint       row;
  gboolean   advanced;

  if (clist->selection == NULL)
    return;

  parent = ocfs_widget_get_toplevel (button);

  advanced = gtk_object_get_data (GTK_OBJECT (clist), "advanced") != NULL;

  row = GPOINTER_TO_INT (clist->selection->data);

  gtk_clist_get_text (clist, row, 0, &device);

  pid = ocfs_mount (parent, device, advanced, mountpoint, &errfd);

  success = ocfs_process_reap (parent, pid, TRUE, FALSE,
			       "Mount", "Mounting",
			       -1, NULL, errfd, &errmsg, &killed);

  if (!success)
    {
      if (killed)
	ocfs_error_box (parent, NULL, "mount died unexpectedly! Your system "
				      "is probably in an inconsistent state. "
				      "You should reboot at the earliest "
				      "oppurtunity");
      else
	ocfs_error_box (parent, errmsg, "Could not mount %s", device);

      g_free (errmsg);
    }

  refresh_partition_list (clist);
}

static void
unmount (GtkWidget *button,
	 GtkCList  *clist)
{
  GtkWindow *parent;
  pid_t      pid;
  gint       errfd;
  gboolean   success, killed;
  gchar     *device, *mountpoint;
  gchar     *errmsg;
  gint       row;

  if (clist->selection == NULL)
    return;

  parent = ocfs_widget_get_toplevel (button);

  row = GPOINTER_TO_INT (clist->selection->data);

  gtk_clist_get_text (clist, row, 0, &device);
  device = g_strdup (device);

  gtk_clist_get_text (clist, row, 1, &mountpoint);

  pid = ocfs_unmount (mountpoint, &errfd);
  success = ocfs_process_reap (parent, pid, TRUE, FALSE,
			       "Unmount", "Unmounting",
			       -1, NULL, errfd, &errmsg, &killed);

  if (success)
    {
      refresh_partition_list (clist);
      select_device (clist, device);
    }
  else
    {
      if (killed)
	ocfs_error_box (parent, NULL, "umount died unexpectedly! Your system "
				      "is probably in an inconsistent state. "
				      "You should reboot at the earliest "
				      "oppurtunity");
      else
	ocfs_error_box (parent, errmsg, "Could not unmount %s mounted on %s",
			device, mountpoint);

      g_free (errmsg);

      refresh_partition_list (clist);
    }

  g_free (device);
}

static void
disk_op (GtkCList *clist,
         guint     action)
{
  GtkWindow   *parent;
  gboolean     advanced, success;
  gint         row;
  gchar       *device;
  struct stat  sbuf;

  advanced = gtk_object_get_data (GTK_OBJECT (clist), "advanced") != NULL;

  parent = ocfs_widget_get_toplevel (GTK_WIDGET (clist));

  if (clist->selection != NULL)
    {
      row = GPOINTER_TO_INT (clist->selection->data);

      gtk_clist_get_text (clist, row, 0, &device);
      device = g_strdup (device);
    }
  else
    device = NULL;

  if (action == 0)
    success = ocfs_format (parent, device, advanced);
  else
    success = ocfs_resize (parent, device, advanced);

  g_free (device);

  refresh_partition_list (clist);

  if (success && stat (CONFFILE, &sbuf) != 0)
    if (ocfs_query_box (parent, "Do you want to generate the config file?"))
      genconfig (clist);
}

static void
genconfig (GtkCList *clist)
{
  GtkWindow *parent;
  gboolean   advanced;

  advanced = gtk_object_get_data (GTK_OBJECT (clist), "advanced") != NULL;

  parent = ocfs_widget_get_toplevel (GTK_WIDGET (clist));

  ocfs_generate_config (parent, advanced);
}

static void
refresh (GtkCList *clist)
{
  refresh_partition_list (clist);

  if (clist->rows == 0)
    update_notebook (GTK_OBJECT (clist), NULL);
}

static GtkWidget *
create_action_area (GtkCList *clist)
{
  GtkWidget *vbbox;
  GtkWidget *button;

  vbbox = gtk_vbutton_box_new ();
  gtk_button_box_set_layout (GTK_BUTTON_BOX (vbbox), GTK_BUTTONBOX_START);
  gtk_box_set_spacing (GTK_BOX (vbbox), 5);
  gtk_container_set_border_width (GTK_CONTAINER (vbbox), 5);

  button = gtk_widget_new (GTK_TYPE_BUTTON,
			   "label", "Mount",
			   "parent", vbbox,
			   "signal::clicked", mount, clist,
			   NULL);
  gtk_object_set_data (GTK_OBJECT (clist), "mount-button", button);

  button = gtk_widget_new (GTK_TYPE_BUTTON,
			   "label", "Unmount",
			   "parent", vbbox,
			   "signal::clicked", unmount, clist,
			   NULL);
  gtk_object_set_data (GTK_OBJECT (clist), "unmount-button", button);

  button = gtk_widget_new (GTK_TYPE_BUTTON,
			   "label", "Refresh",
			   "parent", vbbox,
			   "object_signal::clicked", refresh, clist,
			   NULL);

  return vbbox;
}

static void
tab_frame (const gchar *tag,
	   const gchar *desc,
	   GtkWidget   *notebook,
	   GtkWidget   *clist,
	   TabFunc      func)
{
  gchar     *str;
  GtkWidget *frame;
  GtkWidget *info;

  frame = gtk_widget_new (GTK_TYPE_FRAME,
			  "shadow", GTK_SHADOW_NONE,
			  "border_width", 0,
			  NULL);

  str = g_strconcat (tag, "-frame", NULL);
  gtk_object_set_data (GTK_OBJECT (clist), str, frame);
  g_free (str);

  info = func (NULL, FALSE);
  gtk_container_add (GTK_CONTAINER (frame), info);

  gtk_container_add_with_args (GTK_CONTAINER (notebook), frame,
			       "tab_label", desc,
			       NULL);
}

static GtkItemFactoryEntry menu_items[] =
{
  { "/_File",                     NULL,         NULL,      0, "<Branch>" },
  { "/File/E_xit",                "<control>Q", cleanup,   0 },
  { "/_Tasks",                    NULL,         NULL,      0, "<Branch>" },
  { "/Tasks/_Format...",          "<control>F", disk_op,   0 },
  { "/Tasks/_Resize...",          "<control>R", disk_op,   1 },
  { "/Tasks/---",                 NULL,         NULL,      0, "<Separator>" },
  { "/Tasks/_Generate Config...", "<control>G", genconfig, 0 },
  { "/_Preferences",              NULL,         NULL,      0, "<Branch>" },
  { "/Preferences/_Basic",        "<control>B", level,     0, "<RadioItem>" },
  { "/Preferences/_Advanced",     "<control>A", level,     1, "/Preferences/Basic" },
  { "/_Help",                     NULL,         NULL,      0, "<Branch>" },
  { "/Help/_About...",            NULL,         about,     0 },
};

static int nmenu_items = sizeof (menu_items) / sizeof (menu_items[0]);


static void
create_window (void)
{
  GtkWidget      *window;
  GtkWidget      *hbox;
  GtkWidget      *vbox;
  GtkWidget      *vpaned;
  GtkWidget      *clist;
  GtkWidget      *scrl_win;
  GtkWidget      *frame;
  GtkWidget      *vbbox;
  GtkWidget      *notebook;
  GtkWidget      *item;
  GtkAccelGroup  *accel_group;
  GtkItemFactory *item_factory;

  window = gtk_widget_new (GTK_TYPE_WINDOW,
			   "type", GTK_WINDOW_TOPLEVEL,
			   "title", "OCFS Tool",
			   "default_width", 520,
			   "default_height", 420,
			   "border_width", 0,
			   "signal::delete_event", cleanup, NULL,
			   NULL);

  clist = create_partition_list ();

  accel_group = gtk_accel_group_new ();
  item_factory = gtk_item_factory_new (GTK_TYPE_MENU_BAR, "<main>",
				       accel_group);
  gtk_object_set_data_full (GTK_OBJECT (window), "<main>",
			    item_factory, (GtkDestroyNotify) gtk_object_unref);
  gtk_accel_group_attach (accel_group, GTK_OBJECT (window));

  gtk_item_factory_create_items (item_factory, nmenu_items, menu_items, clist);

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (window), vbox);

  gtk_box_pack_start (GTK_BOX (vbox),
		      gtk_item_factory_get_widget (item_factory, "<main>"),
      		      FALSE, FALSE, 0);

  vpaned = gtk_vpaned_new ();
  gtk_container_set_border_width (GTK_CONTAINER (vpaned), 4);
  gtk_box_pack_start (GTK_BOX (vbox), vpaned, TRUE, TRUE, 0);

  hbox = gtk_hbox_new (FALSE, 0);
  gtk_paned_pack1 (GTK_PANED (vpaned), hbox, FALSE, FALSE);

  scrl_win = gtk_widget_new (GTK_TYPE_SCROLLED_WINDOW,
			     "hscrollbar_policy", GTK_POLICY_AUTOMATIC,
			     "vscrollbar_policy", GTK_POLICY_AUTOMATIC,
			     "parent", hbox,
			     NULL);
  gtk_container_add (GTK_CONTAINER (scrl_win), clist);

  frame = gtk_widget_new (GTK_TYPE_FRAME,
      			  "shadow", GTK_SHADOW_IN,
			  NULL);
  gtk_box_pack_end (GTK_BOX (hbox), frame, FALSE, FALSE, 0);
  
  vbbox = create_action_area (GTK_CLIST (clist));
  gtk_container_add (GTK_CONTAINER (frame), vbbox);

  notebook = gtk_widget_new (GTK_TYPE_NOTEBOOK,
			     "tab_pos", GTK_POS_TOP,
			     NULL);
  gtk_paned_pack2 (GTK_PANED (vpaned), notebook, FALSE, FALSE);

  tab_frame ("general",   "General",          notebook, clist, ocfs_general);
  tab_frame ("browser",   "File Listing",     notebook, clist, ocfs_browser);
  tab_frame ("nodemap",   "Configured Nodes", notebook, clist, ocfs_nodemap);
  tab_frame ("bitmap",    "Bitmap View",      notebook, clist, ocfs_bitmap);
  tab_frame ("freespace", "Free Space",       notebook, clist, ocfs_freespace);

  refresh_partition_list (GTK_CLIST (clist));

  gtk_widget_show_all (window);

  item = gtk_item_factory_get_item (item_factory, "/Tasks/Resize...");
  gtk_object_set_data (GTK_OBJECT (clist), "resize_item", item);
  gtk_widget_hide (item);
}

static void
handle_signal (gint sig)
{
  switch (sig)
    {
    case SIGTERM:
    case SIGINT:
      libocfs_cleanup_raw ();
      exit(1);
    }
}

int
main (int    argc,
      char **argv)
{
  int i;

  gtk_init (&argc, &argv);

  for (i = 1; i < argc; i++)
    {
      if ((strcmp (argv[i], "--version") == 0) ||
	  (strcmp (argv[i], "-V") == 0))
	{
	  g_print ("OCFSTool version " OCFSTOOL_VERSION "\n");
	  exit (0);
	}
      else if (strcmp (argv[i], "--help") == 0)
	{
	  usage (argv[0]);
	  exit (0);
	}
      else
	{
	  usage (argv[0]);
	  exit (1);
	}
    }

#define INSTALL_SIGNAL(sig)		G_STMT_START {	\
  if (signal(sig, handle_signal) == SIG_ERR)		\
    {							\
      fprintf(stderr, "Could not set " #sig "\n");	\
      exit (1);						\
    }							\
} G_STMT_END

  INSTALL_SIGNAL(SIGTERM);
  INSTALL_SIGNAL(SIGINT);

  init_raw_cleanup_message ();

  if (libocfs_init_raw () != 0)
    {
      ocfs_error_box (NULL, NULL, "Could not get a raw device slot for disk "
				  "access.\nPlease free up some raw devices.");
      exit (1);
    }

  create_window ();

  gtk_main ();

  libocfs_cleanup_raw ();

  return 0;
}
