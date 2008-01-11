/*
 * ocfsplist.c
 *
 * Walk the partition list on a system
 *
 * Copyright (C) 2002, 2005 Oracle Corporation.  All rights reserved.
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
#include <fcntl.h>

#include <glib.h>

#include "ocfs2/ocfs2.h"

#include <blkid/blkid.h>

#include "ocfsplist.h"


#define RAW_PARTITION_FSTYPE "partition table"
#define UNKNOWN_FSTYPE       "unknown"

#define FILL_ASYNC_ITERATIONS 20
#define WALK_ASYNC_ITERATIONS 10


typedef struct _WalkData WalkData;

struct _WalkData
{
  OcfsPartitionListFunc  func;
  gpointer               data;

  GPatternSpec          *filter;
  const gchar           *fstype;

  gboolean               unmounted;
  gboolean               async;

  guint                  count;

  blkid_cache            cache;
};


static gboolean  is_partition_data   (const gchar  *device);
static gboolean  used_unmounted      (const gchar  *fstype);
static gchar    *fstype_check        (const gchar  *device,
				      WalkData     *wdata);
static gchar    *get_device_fstype   (const gchar  *device,
				      WalkData     *wdata);
static void      partition_info_fill (GHashTable   *info,
				      gboolean      async);
static gboolean  partition_walk      (gpointer      key,
				      gpointer      value,
				      gpointer      user_data);


static inline void
async_loop_run (gboolean     async,
		guint       *count,
                const guint  num_iterations)
{
  if (async)
    {
      (*count)++;

      if (*count % num_iterations == 0)
	while (g_main_context_iteration (NULL, FALSE));
    }
}

static gboolean
is_partition_data (const gchar *device)
{
  guchar buf[512];
  gint   fd;
  gssize count;
  
  fd = open (device, O_RDWR);
  if (fd == -1)
    return FALSE;

  count = read (fd, buf, 512);
  close (fd);
  
  if (count != 512)
    return FALSE;

  return (buf[510] == 0x55) && (buf[511] == 0xaa);
}

static gboolean
used_unmounted (const gchar *fstype)
{
  return (strcmp (fstype, "oracleasm") == 0) ||
         (strcmp (fstype, RAW_PARTITION_FSTYPE) == 0);
}

static gchar *
fstype_check (const gchar *device,
	      WalkData    *wdata)
{
  blkid_dev  dev;
  gchar     *fstype = NULL;

  dev = blkid_get_dev(wdata->cache, device, BLKID_DEV_NORMAL);

  if (dev)
    {
      blkid_tag_iterate  iter;
      const gchar       *type, *value;

      iter = blkid_tag_iterate_begin (dev);

      while (blkid_tag_next (iter, &type, &value) == 0)
	{
	  if (strcmp (type, "TYPE") == 0)
	    {
	      if ((wdata->fstype == NULL) ||
		  (strcmp (value, wdata->fstype) == 0))
		{
		  fstype = g_strdup (value);
		  break;
		}
	    }
	}

      blkid_tag_iterate_end (iter);
    }

  if (fstype == NULL && wdata->fstype == NULL)
    {
      if (device && is_partition_data (device))
	fstype = g_strdup (RAW_PARTITION_FSTYPE);
      else
	fstype = g_strdup (UNKNOWN_FSTYPE);
    }

  return fstype;
}

static gchar *
get_device_fstype (const gchar  *device,
		   WalkData     *wdata)
{
  gboolean     is_bad = FALSE;
  struct stat  sbuf;
  FILE        *f;
  gchar        buf[100], *proc, *d;
  gint         i, fd;

  if (wdata->filter && !g_pattern_match_string (wdata->filter, device))
    return NULL;

  if ((stat (device, &sbuf) != 0) ||
      (!S_ISBLK (sbuf.st_mode)) ||
      ((sbuf.st_mode & 0222) == 0))
    return NULL;

  if (strncmp ("/dev/hd", device, 7) == 0)
    {
      i = strlen (device) - 1;
      while (i > 5 && g_ascii_isdigit (device[i]))
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
	return NULL;
    }

  fd = open (device, O_RDWR);
  if (fd == -1)
    return NULL;
  close (fd);

  return fstype_check (device, wdata);
}

static void
partition_info_fill (GHashTable *info,
		     gboolean    async)
{
  FILE   *proc;
  gchar   line[100], name[100], *device;
  GSList *list;
  gint    i;
  gchar  *p;
  guint   count = 0;

  proc = fopen ("/proc/partitions", "r");
  if (proc == NULL)
    return;

  while (fgets (line, sizeof(line), proc) != NULL)
    {
      if (sscanf(line, "%*d %*d %*d %99[^ \t\n]", name) != 1)
	continue;

      device = g_strconcat ("/dev/", name, NULL);

      i = strlen (device) - 1;
      if (g_ascii_isdigit (device[i]))
	{
	  while (i > 0 && g_ascii_isdigit (device[i]))
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

      async_loop_run (async, &count, FILL_ASYNC_ITERATIONS);
    }

  fclose (proc);
}

static gboolean
partition_walk (gpointer key,
		gpointer value,
		gpointer user_data)
{
  WalkData          *wdata;
  GSList            *list, *last;
  gchar             *device;
  gchar              mountpoint[PATH_MAX];
  OcfsPartitionInfo  info;
  gint               flags;
  errcode_t          ret;

  wdata = user_data;
  list = value;

  while (list)
    {
      device = list->data;

      info.fstype = get_device_fstype (device, wdata);

      if (info.fstype)
	{
	  info.device = device;

	  ret = ocfs2_check_mount_point (device, &flags, mountpoint, PATH_MAX);

	  if (ret == 0)
	    {
	      if (flags & OCFS2_MF_MOUNTED)
		info.mountpoint = mountpoint;
	      else
		info.mountpoint = NULL;

	      if (wdata->unmounted)
		{
		  if ((info.mountpoint == NULL) &&
		      !used_unmounted (info.fstype) &&
		      !(flags & OCFS2_MF_BUSY))
		    wdata->func (&info, wdata->data);
		}
	      else
		wdata->func (&info, wdata->data);
	    }
	  else
	    info.mountpoint = NULL;

	  g_free (info.fstype);
	}

      last = list;
      list = list->next;

      g_free (device);
      g_slist_free_1 (last);

      async_loop_run (wdata->async, &wdata->count, WALK_ASYNC_ITERATIONS);
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

void
ocfs_partition_list (OcfsPartitionListFunc  func,
		     gpointer               data,
		     const gchar           *filter,
		     const gchar           *fstype,
		     gboolean               unmounted,
		     gboolean               async)
{
  GHashTable *info;
  WalkData    wdata = { func, data, NULL, fstype, unmounted, async, 0 };

  if (blkid_get_cache (&wdata.cache, NULL) < 0)
    return;

  if (fstype && *fstype == '\0')
    wdata.fstype = NULL;

  if (filter && *filter)
    wdata.filter = g_pattern_spec_new (filter);

  info = g_hash_table_new (g_str_hash, g_str_equal);

  partition_info_fill (info, async);

#ifdef LIST_TEST_HASH
  g_hash_table_foreach (info, print_hash, NULL);
#endif

  g_hash_table_foreach_remove (info, partition_walk, &wdata);
  
  g_hash_table_destroy (info);

  blkid_put_cache (wdata.cache);
}

#ifdef LIST_TEST
static void
list_func (OcfsPartitionInfo *info,
           gpointer           data)
{
  g_print ("Device: %s; Mountpoint %s\n",
           info->device, info->mountpoint ? info->mountpoint : "N/A");
}

int
main (int    argc,
      char **argv)
{
  g_print ("All:\n");
  ocfs_partition_list (list_func, NULL, "ocfs2", NULL, FALSE, FALSE);
  
  g_print ("Unmounted:\n");
  ocfs_partition_list (list_func, NULL, NULL, NULL, TRUE, FALSE);

  return 0;
}
#endif
