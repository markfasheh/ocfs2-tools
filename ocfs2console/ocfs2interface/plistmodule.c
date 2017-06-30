/*
 * plistmodule.c
 *
 * Partition list python binding.
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301 USA.
 */

#include <Python.h>

#include <glib.h>

#include "ocfsplist.h"

typedef struct
{
  PyObject *func;
  PyObject *data;

  gboolean  mountpoint;
  gboolean  seen_error;
} ProxyData;


static void
proxy (OcfsPartitionInfo *info,
       gpointer           pdata)
{
  ProxyData *data = pdata;
  PyObject  *tuple, *val, *ret;
  gint       len = 2, pos = 0;

  if (data->seen_error)
    return;

  if (data->mountpoint) len++;
  if (data->data)       len++;

  tuple = PyTuple_New (len);

  PyTuple_SET_ITEM (tuple, pos, PyString_FromString (info->device));
  pos++;

  if (data->mountpoint)
    {
      if (info->mountpoint == NULL)
	{
	  Py_INCREF (Py_None);
	  val = Py_None;
	}
      else
	val = PyString_FromString (info->mountpoint);

      PyTuple_SET_ITEM (tuple, pos, val);
      pos++;
    }

  PyTuple_SET_ITEM (tuple, pos, PyString_FromString (info->fstype));
  pos++;

  if (data->data)
    {
      Py_INCREF (data->data);
      PyTuple_SET_ITEM (tuple, pos, data->data);
      pos++;
    }

  ret = PyObject_CallObject (data->func, tuple);

  if (ret == NULL)
    {
      PyErr_Print();
      data->seen_error = TRUE;
    }

  Py_DECREF (tuple);
}

static PyObject *
partition_list (PyObject *self,
		PyObject *args,
		PyObject *kwargs)
{
  ProxyData  pdata;
  PyObject  *py_func, *py_data = NULL;
  gchar     *filter = NULL, *fstype = NULL;
  gboolean   unmounted = FALSE, async = FALSE;

  static gchar *kwlist[] = {
    "callback", "data",
    "filter", "fstype", "unmounted", "async",
    NULL
  };

  if (!PyArg_ParseTupleAndKeywords (args, kwargs,
				    "O|Ozzii:partition_list", kwlist,
				    &py_func, &py_data,
				    &filter, &fstype, &unmounted, &async))
    return NULL;

  if (!PyCallable_Check (py_func))
    {
      PyErr_SetString (PyExc_TypeError, "callback must be a callable object");
      return NULL;
    }

  Py_INCREF (py_func);
  pdata.func = py_func;

  Py_XINCREF (py_data);
  pdata.data = py_data;

  pdata.mountpoint = !unmounted;
  pdata.seen_error = FALSE;

  ocfs_partition_list (proxy, &pdata, filter, fstype, unmounted, async);

  Py_DECREF (py_func);
  Py_XDECREF (py_data);

  Py_INCREF (Py_None);
  return Py_None;
}

static PyMethodDef plist_methods[] = {
  {"partition_list", (PyCFunction)partition_list, METH_KEYWORDS},
  {NULL,       NULL}    /* sentinel */
};

void
initplist (void)
{
  PyObject *m;

  m = Py_InitModule ("plist", plist_methods);

  if (PyErr_Occurred ())
    Py_FatalError ("can't initialize module plist");
}
