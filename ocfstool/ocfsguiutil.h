/*
 * ocfsguiutil.h
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

#ifndef __OCFS_GUI_UTIL_H__
#define __OCFS_GUI_UTIL_H__


#include <sys/types.h>

#include <gtk/gtk.h>


typedef gchar *(*OcfsListIterator) (gpointer data);


GtkWindow *ocfs_widget_get_toplevel (GtkWidget         *widget);

GtkWidget *ocfs_dialog_new          (GtkWindow         *parent,
				     const gchar       *title,
				     GtkSignalFunc      callback,
				     gpointer           callback_data);
gboolean   ocfs_dialog_run          (GtkWidget         *dialog);

void       ocfs_error_box           (GtkWindow         *parent,
				     const gchar       *errmsg,
				     const gchar       *format,
				     ...) G_GNUC_PRINTF (3, 4);
gboolean   ocfs_query_box           (GtkWindow         *parent,
				     const gchar       *format,
				     ...) G_GNUC_PRINTF (2, 3);
gchar     *ocfs_query_text          (GtkWindow         *parent,
				     const gchar       *prompt,
				     const gchar       *def);

gchar     *ocfs_get_filename        (GtkWindow         *parent,
				     const gchar       *title);

gchar     *ocfs_get_user_name       (uid_t              uid);
gchar     *ocfs_get_group_name      (gid_t              gid);

gchar     *ocfs_format_bytes        (guint64            bytes,
				     gboolean           show_bytes);

void       ocfs_build_list          (GtkWidget         *list,
				     const gchar       *def,
				     OcfsListIterator   iterator,
				     gpointer           data);

GtkWidget *ocfs_build_octal_entry   (const gchar       *def);

GtkWidget *ocfs_build_combo         (void);

GtkWidget *ocfs_build_combo_user    (const gchar       *def);
GtkWidget *ocfs_build_combo_group   (const gchar       *def);

GtkCList  *ocfs_build_clist         (gint               columns,
				     gchar             *titles[],
				     GtkWidget        **scrl_win);


#endif /* __OCFS_GUI_UTIL_H__ */
