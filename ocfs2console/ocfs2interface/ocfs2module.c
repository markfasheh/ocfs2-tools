/*
 * ocfs2module.c
 *
 * OCFS2 python binding.
 *
 * Copyright (C) 2004, 2005 Oracle.  All rights reserved.
 *
 * Author: Manish Singh <manish.singh@oracle.com>
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
 * You should have recieved a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include "Python.h"
#include "structseq.h"

#include <glib.h>

#include "ocfs2.h"

#include "ocfsplist.h"


#define MAX_CLUSTER_SIZE       1048576
#define MIN_CLUSTER_SIZE       4096


static PyObject *ocfs2_error;

typedef struct
{
  PyObject *func;
  PyObject *data;
} ProxyData;

static void
proxy_partition_func (OcfsPartitionInfo *info,
		      gpointer           pdata)
{
  ProxyData *data = pdata;

  PyObject_CallFunction (data->func, "ss", info->device, info->mountpoint);
}

static void
proxy_partition_func_data (OcfsPartitionInfo *info,
			   gpointer           pdata)
{
  ProxyData *data = pdata;

  PyObject_CallFunction (data->func, "ssO",
			 info->device, info->mountpoint, data->data);
}

static void
proxy_unmounted_func (OcfsPartitionInfo *info,
		      gpointer           pdata)
{
  ProxyData *data = pdata;

  PyObject_CallFunction (data->func, "s", info->device);
}

static void
proxy_unmounted_func_data (OcfsPartitionInfo *info,
			   gpointer           pdata)
{
  ProxyData *data = pdata;

  PyObject_CallFunction (data->func, "sO", info->device, data->data);
}


static PyObject *
partition_list (PyObject *self,
		PyObject *args,
		PyObject *kwargs)
{
  ProxyData              proxy_data;
  OcfsPartitionListFunc  func;
  PyObject              *py_func, *py_data = NULL;
  gchar                 *filter = NULL;
  gboolean               unmounted = FALSE, async = FALSE;

  static gchar *kwlist[] = {
    "callback", "data",
    "filter", "unmounted", "async",
    NULL
  };

  if (!PyArg_ParseTupleAndKeywords (args, kwargs,
				    "O|Osii:partition_list", kwlist,
				    &py_func, &py_data,
				    &filter, &unmounted, &async))
    return NULL;

  if (!PyCallable_Check (py_func))
    {
      PyErr_SetString (PyExc_TypeError, "callback must be a callable object");
      return NULL;
    }

  proxy_data.func = py_func;

  if (py_data)
    {
      proxy_data.data = py_data;

      if (unmounted)
	func = proxy_unmounted_func_data;
      else
	func = proxy_partition_func_data;
    }
  else
    {
      if (unmounted)
	func = proxy_unmounted_func;
      else
	func = proxy_partition_func;
    }

  ocfs_partition_list (func, &proxy_data, filter, unmounted, async);

  Py_INCREF (Py_None);
  return Py_None;
}

static PyStructSequence_Field struct_ocfs2_super_fields[] = {
  {"s_major_rev_level",  "major revision level"},
  {"s_minor_rev_level",  "minor revision level"},
  {"s_blocksize_bits",   "blocksize bits"},
  {"s_clustersize_bits", "cluster size bits"},
  {"s_max_nodes",        "maximum nodes"},
  {"s_label",            "volume label"},
  {"s_uuid",             "UUID"},
  {0}
};

struct PyStructSequence_Desc struct_ocfs2_super_desc = {
  "ocfs2.struct_ocfs2_super",
  NULL,
  struct_ocfs2_super_fields,
  7,
};

static PyTypeObject StructOcfs2SuperType;

static PyObject *
get_super (PyObject *self,
	   PyObject *args)
{
  gchar         *device;
  errcode_t      ret;
  ocfs2_filesys *fs;
  PyObject      *v;
  gint           index = 0;
  GString       *uuid;
  gint           i;

  static gchar *fmt[] = { "%02x", "%02x", "%02x", "%02x",
			  "-%02x", "%02x", "-%02x", "%02x", "-%02x", "%02x",
			  "-%02x", "%02x", "%02x", "%02x", "%02x", "%02x" };

  if (!PyArg_ParseTuple (args, "s:get_super", &device))
    return NULL;

  ret = ocfs2_open (device, OCFS2_FLAG_RO, 0, 0, &fs);
  if (ret)
    {
      PyErr_SetString (ocfs2_error, error_message (ret));
      return NULL;
    }

  v = PyStructSequence_New (&StructOcfs2SuperType);
  if (v == NULL)
    return NULL;

#define SET(m) \
  PyStructSequence_SET_ITEM (v, index++, PyInt_FromLong \
    (OCFS2_RAW_SB (fs->fs_super)->m));

  SET (s_major_rev_level);
  SET (s_minor_rev_level);
  SET (s_blocksize_bits);
  SET (s_clustersize_bits);
  SET (s_max_nodes);

#undef SET
#define SET(m) \
  PyStructSequence_SET_ITEM \
    (v, index++, PyString_FromString (OCFS2_RAW_SB (fs->fs_super)->m));

  PyStructSequence_SET_ITEM
    (v, index++, PyString_FromString (OCFS2_RAW_SB (fs->fs_super)->s_label));

#undef SET

  uuid = g_string_sized_new (32);

  for (i = 0; i < 16; i++)
    g_string_append_printf (uuid, fmt[i],
			    OCFS2_RAW_SB (fs->fs_super)->s_uuid[i]);

  PyStructSequence_SET_ITEM (v, index++, PyString_FromString (uuid->str));

  g_string_free (uuid, TRUE);

  ocfs2_close (fs);

  if (PyErr_Occurred())
    {
      Py_DECREF (v);
      return NULL;
    }

  return v;
}

static PyMethodDef ocfs2_methods[] = {
  {"partition_list", (PyCFunction)partition_list, METH_VARARGS | METH_KEYWORDS},
  {"get_super", (PyCFunction)get_super, METH_VARARGS},
  {NULL,       NULL}    /* sentinel */
};

void
initocfs2 (void)
{
  PyObject *m;

  initialize_ocfs_error_table ();

  m = Py_InitModule ("ocfs2", ocfs2_methods);

  PyStructSequence_InitType (&StructOcfs2SuperType, &struct_ocfs2_super_desc);
  Py_INCREF (&StructOcfs2SuperType);
  PyModule_AddObject (m, "struct_ocfs2_super",
                      (PyObject *) &StructOcfs2SuperType);

  ocfs2_error = PyErr_NewException ("ocfs2.error", PyExc_RuntimeError, NULL);
  if (ocfs2_error)
    {
      Py_INCREF (ocfs2_error);
      PyModule_AddObject (m, "error", ocfs2_error);
    }

  PyModule_AddIntConstant (m, "MAX_VOL_LABEL_LEN", MAX_VOL_LABEL_LEN);
  PyModule_AddIntConstant (m, "MAX_NODES", OCFS2_MAX_NODES);

  PyModule_AddIntConstant (m, "MIN_BLOCKSIZE", OCFS2_MIN_BLOCKSIZE);
  PyModule_AddIntConstant (m, "MAX_BLOCKSIZE", OCFS2_MAX_BLOCKSIZE);

  PyModule_AddIntConstant (m, "MIN_CLUSTER_SIZE", MIN_CLUSTER_SIZE);
  PyModule_AddIntConstant (m, "MAX_CLUSTER_SIZE", MAX_CLUSTER_SIZE);

  if (PyErr_Occurred ())
    Py_FatalError ("can't initialise module ocfs2");
}
