/*
 * ocfsprocess.c
 *
 * Misc process control helpers
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

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>

#include <gtk/gtk.h>

#include "ocfsguiutil.h"
#include "ocfsprocess.h"


#define INTERVAL 100
#define TIMEOUT  10000

typedef void (*OutputFunc) (const gchar *buf,
			    gpointer     data);

typedef struct _ProcInfo      ProcInfo;
typedef struct _KillInfo      KillInfo;
typedef struct _OutputClosure OutputClosure;

struct _ProcInfo
{
  gboolean     success;

  gboolean     killed;
  gboolean     cancel;

  pid_t        pid;

  gboolean     spin;

  gint         count;
  guint        threshold;

  const gchar *title;
  const gchar *desc;

  GtkWidget   *dialog;
  GtkWidget   *pbar;

  GtkWindow   *parent;
};

struct _KillInfo
{
  pid_t    pid;
  gboolean sent_kill;
};

struct _OutputClosure
{
  OutputFunc func;
  gpointer   data;
};


static gboolean  proc_timeout        (ProcInfo       *pinfo);
static void      proc_kill           (ProcInfo       *pinfo);
static gboolean  kill_timeout        (KillInfo       *kinfo);
static void      make_progress_box   (ProcInfo       *pinfo);
static gboolean  out_read            (GIOChannel     *channel,
				      GIOCondition    cond,
				      OutputClosure  *output);
static void      build_msg           (const gchar    *buf,
				      GString        *str);
static void      update_pbar         (const gchar    *buf,
				      GtkWidget      *pbar);


pid_t
ocfs_process_run (gchar  *progname,
		  gchar **argv,
		  gint   *outfd,
		  gint   *errfd)
{
  gint  rc, sleep_count = 0;
  pid_t pid;
  gint  out_pipe[2], err_pipe[2];

#ifdef DEBUG_PROCESS
  {
    gchar **arg = argv;

    while (*arg)
      {
        g_print ("%s ", *arg);
        arg++;
      }

    g_print ("\n");
  }
#endif

  rc = pipe (out_pipe);
  if (rc == -1)
    return -errno;

  rc = pipe (err_pipe);
  if (rc == -1)
    {
      rc = -errno;
      close (out_pipe[0]);
      close (out_pipe[1]);
      return rc;
    }

  while ((pid = fork ()) == -1)
    {
      sleep_count++;

      if (sleep_count > 4)
	{
	  rc = -errno;
	  close (out_pipe[0]);
	  close (out_pipe[1]);
	  return rc;
	}

      sleep (2);
    }

  if (pid == 0)
    {
      close (out_pipe[0]);
      close (err_pipe[0]);

      if (outfd)
	{
	  if (out_pipe[1] != STDOUT_FILENO)
	    {
	      rc = dup2 (out_pipe[1], STDOUT_FILENO);

	      if (rc == -1)
		_exit (-errno);

	      close (out_pipe[1]);
	    }
	}
      else
	close (out_pipe[1]);

      if (errfd)
	{
	  if (err_pipe[1] != STDERR_FILENO)
	    {
	      rc = dup2 (err_pipe[1], STDERR_FILENO);

	      if (rc == -1)
		_exit (-errno);

	      close (err_pipe[1]);
	    }
	}
      else
	close (err_pipe[1]);

#ifdef DEVEL_MACHINE
      putenv ("PATH=/usr/local/bin:/usr/bin:/bin:/usr/bin/X11:/usr/games:.");
#endif

      execvp (progname, argv);
      rc = -errno;
      fprintf (stderr, "Could not run \"%s\", %s\n", progname,
	       g_strerror (errno));
      _exit (rc);
    }

  close (out_pipe[1]);
  close (err_pipe[1]);

  if (outfd)
    *outfd = out_pipe[0];
  else
    close (out_pipe[0]);

  if (errfd)
    *errfd = err_pipe[0];
  else
    close (err_pipe[0]);
  
  return pid;
}

gboolean
ocfs_process_reap (GtkWindow    *parent,
		   pid_t         pid,
		   gboolean      spin,
		   gboolean      spin_wait,
		   const gchar  *title,
		   const gchar  *desc,
		   gint          outfd,
		   gchar       **outmsg,
		   gint          errfd,
		   gchar       **errmsg,
		   gboolean     *killed)
{
  ProcInfo       pinfo;
  OutputClosure  err_closure, out_closure;
  GIOChannel    *err = NULL, *out = NULL;
  gint           timeout_id, err_id = 0, out_id = 0;
  gboolean       do_out = FALSE;

  if (pid < 0)
    {
      *errmsg = g_strdup (g_strerror (-pid));
      return FALSE;
    }

  pinfo.pid = pid;

  pinfo.killed = FALSE;

  pinfo.spin = spin;

  pinfo.count = TIMEOUT / INTERVAL;
  pinfo.threshold = pinfo.count - INTERVAL * 10;

  pinfo.title = title;
  pinfo.desc = desc;

  pinfo.parent = parent;

  if (spin)
    {
      pinfo.cancel = FALSE;

      if (!spin_wait)
	{
	  pinfo.count = TIMEOUT * 60;
	  make_progress_box (&pinfo);
	}
      else
	pinfo.dialog = NULL;
    }
  else
    {
      pinfo.cancel = TRUE;
      make_progress_box (&pinfo);
    }

  timeout_id = g_timeout_add (INTERVAL, (GSourceFunc) proc_timeout, &pinfo);

  if (errmsg)
    {
      err_closure.func = (OutputFunc) build_msg;
      err_closure.data = g_string_new ("");

      err = g_io_channel_unix_new (errfd);
      err_id = g_io_add_watch (err, G_IO_IN, (GIOFunc) out_read, &err_closure);
    }

  if (outmsg)
    {
      out_closure.func = (OutputFunc) build_msg;
      out_closure.data = g_string_new ("");

      do_out = TRUE;
    }
  else if (!spin)
    {
      out_closure.func = (OutputFunc) update_pbar;
      out_closure.data = pinfo.pbar;

      do_out = TRUE;
    }

  if (do_out)
    {
      out = g_io_channel_unix_new (outfd);
      out_id = g_io_add_watch (out, G_IO_IN, (GIOFunc) out_read, &out_closure);
    }

  gtk_main ();

  if (pinfo.dialog)
    gtk_widget_destroy (pinfo.dialog);

  g_source_remove (timeout_id);

  if (errmsg)
    {
      GString *errstr = err_closure.data;

      g_source_remove (err_id);

      g_io_channel_close (err);
      g_io_channel_unref (err);

      if (!pinfo.success)
	{
	  if (pinfo.killed)
	    {
	      if (errstr->len)
		g_string_append (errstr, "\n");

	      g_string_append (errstr, "Killed prematurely.");
	    }

	  *errmsg = errstr->str;
	}

      g_string_free (errstr, pinfo.success);
    }

  if (do_out)
    {
      g_source_remove (out_id);

      g_io_channel_close (out);
      g_io_channel_unref (out);

      if (outmsg)
	{
	  GString *outstr = out_closure.data;

	  *outmsg = outstr->str;

	  g_string_free (outstr, FALSE);
	}
    }

  if (killed)
    *killed = pinfo.killed;

  return pinfo.success;
}

gchar **
ocfs_shell_output (GtkWindow *parent,
		   gchar     *command)
{
  gchar    **ret;
  pid_t      pid;
  gint       outfd, errfd;
  gboolean   success;
  gchar     *outmsg, *errmsg;
  gchar     *argv[] = {
    "/bin/sh", "-c",
    NULL,
    NULL
  };

  argv[2] = command; 

  pid = ocfs_process_run (argv[0], argv, &outfd, &errfd);

  success = ocfs_process_reap (parent, pid, TRUE, TRUE,
			       "Shell Comand", "Shell Command",
			       outfd, &outmsg, errfd, &errmsg,
			       NULL);

  ret = success ? g_strsplit (outmsg, "\n", 0) : NULL;

  g_free (outmsg);

  if (!success)
    g_free (errmsg);

  return ret;
}

static gboolean
proc_timeout (ProcInfo *pinfo)
{
  pid_t  pid;
  gint   status;

  if (pinfo->spin)
    pinfo->count--;

  pid = waitpid (pinfo->pid, &status, WNOHANG);

  if (pid == -1)
    {
      pinfo->success = FALSE;
      gtk_main_quit ();
      return FALSE;
    }
  else if (pid == pinfo->pid)
    {
      pinfo->success = (WIFEXITED (status) && WEXITSTATUS (status) == 0);
      gtk_main_quit ();
      return FALSE;
    }

  if (!pinfo->spin)
    return TRUE;

  if (pinfo->count < 1)
    {
      proc_kill (pinfo);
      return FALSE;
    }

  if (pinfo->count < pinfo->threshold && pinfo->dialog == NULL)
    make_progress_box (pinfo);

  if (pinfo->dialog)
    {
      GtkAdjustment *adj;
      gfloat         value;

      adj = GTK_PROGRESS (pinfo->pbar)->adjustment;

      value = adj->value + 1;
      if (value > adj->upper)
	value = adj->lower;

      gtk_progress_set_value (GTK_PROGRESS (pinfo->pbar), value);
    }

  return TRUE;
}

static void
proc_kill (ProcInfo *pinfo)
{
  KillInfo *kinfo;

  pinfo->success = FALSE;
  pinfo->killed = TRUE;

  kinfo = g_new0 (KillInfo, 1);
  kinfo->pid = pinfo->pid;

  kill (kinfo->pid, 15);
  g_timeout_add (INTERVAL * 5, (GSourceFunc) kill_timeout, kinfo);

  gtk_main_quit ();
}

static gboolean
kill_timeout (KillInfo *kinfo)
{
  if (!kinfo->sent_kill && waitpid (kinfo->pid, NULL, WNOHANG) == 0)
    {
      kill (kinfo->pid, 9);
      kinfo->sent_kill = TRUE;

      return TRUE;
    }

  g_free (kinfo);

  return FALSE;
}

static void
make_progress_box (ProcInfo *pinfo)
{
  GtkWidget *dialog;
  GtkWidget *action_area;
  GtkWidget *hbbox;
  GtkWidget *vbox;
  GtkWidget *label;
  gchar     *str;

  dialog = gtk_widget_new (pinfo->cancel ? GTK_TYPE_DIALOG : GTK_TYPE_WINDOW,
			   "title", pinfo->title,
			   "allow_grow", FALSE,
			   "modal", TRUE,
			   "signal::delete-event", gtk_true, NULL,
			   NULL);
  gtk_window_set_transient_for (GTK_WINDOW (dialog), pinfo->parent);

  if (pinfo->cancel)
    {
      vbox = GTK_DIALOG (dialog)->vbox;

      action_area = GTK_DIALOG (dialog)->action_area;
      gtk_widget_set (action_area,
		      "border_width", 2,
		      "homogeneous", FALSE,
		      NULL);

      hbbox = gtk_hbutton_box_new ();
      gtk_button_box_set_spacing (GTK_BUTTON_BOX (hbbox), 4);
      gtk_box_pack_end (GTK_BOX (action_area), hbbox, FALSE, FALSE, 0);

      gtk_widget_new (GTK_TYPE_BUTTON,
		      "label", "Cancel",
		      "parent", hbbox,
		      "can_default", TRUE,
		      "has_default", TRUE,
		      "signal::clicked", proc_kill, pinfo,
		      NULL);
    }
  else
    vbox = gtk_widget_new (GTK_TYPE_VBOX,
			   "spacing", 0,
			   "homogeneous", FALSE,
			   "border_width", 4,
			   "parent", dialog,
			   NULL);

  pinfo->dialog = dialog;

  str = g_strconcat (pinfo->desc, "...", NULL);
  label = gtk_label_new (str);
  g_free (str);

  gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

  pinfo->pbar = gtk_progress_bar_new ();
  gtk_box_pack_start (GTK_BOX (vbox), pinfo->pbar, FALSE, FALSE, 0);

  if (pinfo->spin)
    gtk_progress_set_activity_mode (GTK_PROGRESS (pinfo->pbar), TRUE);
  else
    gtk_progress_set_show_text (GTK_PROGRESS (pinfo->pbar), TRUE);

  gtk_widget_realize (dialog);
  gdk_window_set_decorations (dialog->window, GDK_DECOR_BORDER);

  gtk_widget_show_all (dialog);
}

gboolean
out_read (GIOChannel    *channel,
	  GIOCondition   cond,
	  OutputClosure *output)
{
  gint     count = 0;
  GIOError error;
  gchar    buf[256];

  error = g_io_channel_read (channel, buf, sizeof (buf) - 1, &count);

  if (error == G_IO_ERROR_NONE)
    {
      buf[count] = '\0';
      output->func (buf, output->data);
      return TRUE;
    }
  else if (error == G_IO_ERROR_AGAIN)
    return TRUE;

  return FALSE;
}

static void
build_msg (const gchar *buf,
	   GString     *str)
{
  g_string_append (str, buf);
}

static void
update_pbar (const gchar *buf,
	     GtkWidget   *pbar)
{
  gfloat value;

  if (strncmp (buf, "COMPLETE", 8) == 0)
    value = 100.0;
  else
    value = (gfloat) atoi (buf);

  gtk_progress_set_value (GTK_PROGRESS (pbar), value);
}
