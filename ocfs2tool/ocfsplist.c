/*
 * ocfsplist.c
 *
 * Create a list of valid partitions
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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include <glib.h>

#include "ocfs2.h"

#include "ocfsplist.h"


typedef struct _BuildData BuildData;

struct _BuildData
{
  gboolean  unmounted;
  GList    *list;
};


static gboolean check_partition_type (const gchar *device,
				      OcfsFSType  *type);
static gboolean valid_device         (const gchar *device,
				      gboolean     no_ocfs_check,
				      OcfsFSType  *type);
static void     partition_info_fill  (GHashTable  *info);
static gboolean list_builder         (gpointer     key,
				      gpointer     value,
				      gpointer     user_data);


static gboolean
check_partition_type (const gchar *device,
		      OcfsFSType  *type)
{
  errcode_t      ret;
  ocfs2_filesys *fs;

  ret = ocfs2_open (device, OCFS2_FLAG_RO, 0, 0, &fs);

  if (ret)
    {
      if (ret == OCFS2_ET_OCFS_REV)
	*type = OCFS_FS_TYPE_OCFS;
      else
	return FALSE;
    }
  else
    *type = OCFS_FS_TYPE_OCFS2;

  ocfs2_close (fs);
  return TRUE;
}

static gboolean
valid_device (const gchar *device,
	      gboolean     no_ocfs_check,
	      OcfsFSType  *type)
{
  gboolean     is_bad = FALSE;
  struct stat  sbuf;
  FILE        *f;
  gchar        buf[100], *proc, *d;
  gint         i, fd;

  if ((stat (device, &sbuf) != 0) ||
      (!S_ISBLK (sbuf.st_mode)) ||
      ((sbuf.st_mode & 0222) == 0))
    return FALSE;

  if (strncmp ("/dev/hd", device, 7) == 0)
    {
      i = strlen (device) - 1;
      while (i > 5 && isdigit (device[i]))
	i--;

      d =  g_strndup (device + 5, i + 1);
      proc = g_strconcat ("/proc/ide/", d, "/media", NULL);
      g_free (d);

      f = fopen (proc, "r");
      g_free (proc);

      if (f != NULL && fgets (buf, sizeof (buf), f))
	is_bad = ((strncmp (buf, "cdrom", 5) == 0) ||
		  (strncmp (buf, "tape",  4) == 0));

      if (f)
	fclose (f);
     
      if (is_bad)
	return FALSE; 
    }

#ifndef DEVEL_MACHINE
  fd = open (device, O_RDWR);
  if (fd == -1)
    return FALSE;
  close (fd);

  return no_ocfs_check ? TRUE : check_partition_type (device, type);
#else
  fd = 0;
  *type = OCFS_FS_TYPE_OCFS2;
  return TRUE;
#endif
}

static void
partition_info_fill (GHashTable *info)
{
  FILE   *proc;
  gchar   line[100], name[100], *device;
  GSList *list;
  gint    i;
  gchar  *p;

  proc = fopen ("/proc/partitions", "r");
  if (proc == NULL)
    return;

  while (fgets (line, sizeof(line), proc) != NULL)
    {
      if (sscanf(line, "%*d %*d %*d %99[^ \t\n]", name) != 1)
	continue;

      device = g_strconcat ("/dev/", name, NULL);

      i = strlen (device) - 1;
      if (isdigit (device[i]))
	{
	  while (i > 0 && isdigit (device[i]))
	    i--;

	  p = g_strndup (device, i + 1);
	  list = g_hash_table_lookup (info, p);

	  if (list == NULL)
	    {
	      list = g_slist_prepend (NULL, device);
	      g_hash_table_insert (info, p, list);
	    }
	  else
	    {
	      if (strcmp (p, list->data) == 0)
		{
		  g_free (list->data);
		  list->data = device;
		}
	      else
		g_slist_append (list, device);

	      g_free (p);
	    }
	}
      else if (!g_hash_table_lookup (info, device))
	{
	  list = g_slist_prepend (NULL, g_strdup (device));
	  g_hash_table_insert (info, device, list);
	}
      else
	g_free (device);
    }

  fclose (proc);
}

static gboolean
list_builder (gpointer key,
	      gpointer value,
	      gpointer user_data)
{
  BuildData         *bdata;
  GSList            *list, *last;
  gchar             *device;
  gchar              mountpoint[PATH_MAX];
  OcfsPartitionInfo *info;
  gint               flags;
  OcfsFSType         type;
  errcode_t          ret;

  bdata = user_data;
  list = value;

  while (list)
    {
      device = list->data;

      if (valid_device (device, bdata->unmounted, &type))
	{
	  info = g_new (OcfsPartitionInfo, 1);

	  info->device = g_strdup (device);
	  info->type = type;

	  ret = ocfs2_check_mount_point (device, &flags, mountpoint, PATH_MAX);

	  if (ret == 0)
	    {
	      if (flags & OCFS2_MF_MOUNTED)
		info->mountpoint = g_strdup (mountpoint);
	      else
		info->mountpoint = NULL;
	    }
	  else
	    info->mountpoint = NULL;

	  if (bdata->unmounted)
	    {
	      if (info->mountpoint)
		{
		  g_free (info->mountpoint);
		  g_free (info->device);
		}
	      else
		bdata->list = g_list_prepend (bdata->list, info->device);

	      g_free (info);
	    }
	  else
	    bdata->list = g_list_prepend (bdata->list, info);
	}

      last = list;
      list = list->next;

      g_free (device);
      g_slist_free_1 (last);
    }

  g_free (key);

  return TRUE;
}

#ifdef LIST_TEST_HASH
static void
print_hash (gpointer key,
	    gpointer value,
	    gpointer user_data)
{
  GSList *list;

  g_print ("Key: %s; Values:", (gchar *) key);
  for (list = value; list != NULL; list = list->next)
    g_print (" %s", (gchar *) list->data);
  g_print ("\n");
}
#endif

GList *
ocfs_partition_list (gboolean unmounted)
{
  GHashTable *info;
  BuildData   bdata = { unmounted, NULL };

  info = g_hash_table_new (g_str_hash, g_str_equal);

  partition_info_fill (info);

#ifdef LIST_TEST_HASH
  g_hash_table_foreach (info, print_hash, NULL);
#endif

  g_hash_table_foreach_remove (info, list_builder, &bdata);
  
  g_hash_table_destroy (info);

  return bdata.list;
}

#ifdef LIST_TEST
int
main (int    argc,
      char **argv)
{
  GList             *plist, *list;
  OcfsPartitionInfo *info;

  g_print ("All:\n");

  plist = ocfs_partition_list (FALSE);
  
  for (list = plist; list; list = list->next)
    {
      info = list->data;
      g_print ("Device: %s; Mountpoint %s; Type %s\n",
	       info->device, info->mountpoint,
	       info->type == OCFS_FS_TYPE_OCFS2 ? "ocfs2" : "ocfs");
    }

  g_print ("Unmounted:\n");

  plist = ocfs_partition_list (TRUE);
  
  for (list = plist; list; list = list->next)
    g_print ("Device: %s\n", (gchar *) list->data);

  return 0;
}
#endif
