/*
 * gidlemodule.c
 *
 * Richer interface to GLIB's idle sources.
 *
 * Copyright (C) 2005 Oracle.  All rights reserved.
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

#include <Python.h>

#include <glib.h>


typedef struct {
  PyObject_HEAD
  GSource *idle;
  gboolean attached;
} Idle;


#define CHECK_DESTROYED(self, ret)			G_STMT_START {	\
  if ((self)->idle == NULL)						\
    {									\
      PyErr_SetString (PyExc_RuntimeError, "idle is destroyed");	\
      return (ret);							\
    }									\
} G_STMT_END


static PyObject *
idle_attach (Idle *self)
{
  CHECK_DESTROYED (self, NULL);

  self->attached = TRUE;

  return PyInt_FromLong (g_source_attach (self->idle, NULL));
}

static PyObject *
idle_destroy (Idle *self)
{
  CHECK_DESTROYED (self, NULL);

  g_source_destroy (self->idle);
  self->idle = NULL;

  Py_INCREF (Py_None);
  return Py_None;
}

/* The next three functions are based on code from PyGTK gobjectmodule.c
 * Copyright (C) 1998-2003  James Henstridge
 */
static void
destroy_notify (gpointer user_data)
{
  PyObject *obj = (PyObject *) user_data;

  Py_DECREF (obj);
}

static gboolean
handler_marshal(gpointer user_data)
{
  PyObject *tuple, *ret;
  gboolean res;

  g_return_val_if_fail (user_data != NULL, FALSE);

  tuple = (PyObject *) user_data;

  ret = PyObject_CallObject (PyTuple_GetItem(tuple, 0),
			     PyTuple_GetItem(tuple, 1));

  if (!ret) {
    PyErr_Print();
    res = FALSE;
  } else {
    res = PyObject_IsTrue(ret);
    Py_DECREF(ret);
  }

  return res;
}

static PyObject *
idle_set_callback (Idle *self,
		   PyObject *args)
{
  PyObject *first, *callback, *cbargs = NULL, *data;
  gint len;

  len = PyTuple_Size (args);
  if (len < 1)
    {
      PyErr_SetString (PyExc_TypeError,
		       "set_callback requires at least 1 argument");
      return NULL;
    }

  first = PySequence_GetSlice (args, 0, 1);
  if (!PyArg_ParseTuple (first, "O:set_callback", &callback))
    {
      Py_DECREF (first);
      return NULL;
    }
  Py_DECREF (first);

  if (!PyCallable_Check (callback))
    {
      PyErr_SetString(PyExc_TypeError, "first argument not callable");
      return NULL;
    }

  cbargs = PySequence_GetSlice (args, 1, len);
  if (cbargs == NULL)
    return NULL;

  data = Py_BuildValue ("(ON)", callback, cbargs);
  if (data == NULL)
    return NULL;

  g_source_set_callback (self->idle, handler_marshal, data, destroy_notify);

  Py_INCREF (Py_None);
  return Py_None;
}

static PyMethodDef idle_methods[] = {
  {"attach", (PyCFunction)idle_attach, METH_NOARGS},
  {"destroy", (PyCFunction)idle_destroy, METH_NOARGS},
  {"set_callback", (PyCFunction)idle_set_callback, METH_VARARGS},
  {NULL, NULL}
};

static PyObject *
idle_get_priority (Idle *self, void *closure)
{
  CHECK_DESTROYED (self, NULL);

  return PyInt_FromLong (g_source_get_priority (self->idle));
}

static int
idle_set_priority (Idle *self, PyObject *value, void *closure)
{
  CHECK_DESTROYED (self, -1);

  if (value == NULL)
    {
      PyErr_SetString (PyExc_TypeError, "cannot delete priority");
      return -1;
    }

  if (!PyInt_Check (value))
    {
      PyErr_SetString (PyExc_TypeError, "type mismatch");
      return -1;
    }

  g_source_set_priority (self->idle, PyInt_AsLong (value));

  return 0;
}

static PyObject *
idle_get_can_recurse (Idle *self, void *closure)
{
  CHECK_DESTROYED (self, NULL);

  return PyBool_FromLong (g_source_get_can_recurse (self->idle));
}

static int
idle_set_can_recurse (Idle *self, PyObject *value, void *closure)
{
  CHECK_DESTROYED (self, -1);

  if (value == NULL)
    {
      PyErr_SetString (PyExc_TypeError, "cannot delete can_recurse");
      return -1;
    }

  if (!PyInt_Check (value))
    {
      PyErr_SetString (PyExc_TypeError, "type mismatch");
      return -1;
    }

  g_source_set_can_recurse (self->idle, PyInt_AsLong (value));

  return 0;
}

static PyObject *
idle_get_id (Idle *self, void *closure)
{
  CHECK_DESTROYED (self, NULL);

  if (!self->attached)
    {
      PyErr_SetString (PyExc_RuntimeError, "idle is not attached");
      return NULL;
    }

  return PyInt_FromLong (g_source_get_id (self->idle));
}

static PyGetSetDef idle_getsets[] = {
  {"priority", (getter)idle_get_priority, (setter)idle_set_priority},
  {"can_recurse", (getter)idle_get_can_recurse, (setter)idle_set_can_recurse},
  {"id", (getter)idle_get_id, (setter)0},
};

static void
idle_dealloc (Idle *self)
{
  if (self->idle)
    g_source_unref (self->idle);

  PyObject_DEL (self);
}

static int
idle_init (Idle *self,
	   PyObject *args,
	   PyObject *kwargs)
{
  gint priority = G_PRIORITY_DEFAULT_IDLE;

  static gchar *kwlist[] = { "priority", NULL };

  if (!PyArg_ParseTupleAndKeywords(args, kwargs,
				   "|i:gidle.Idle.__init__", kwlist,
				   &priority))
    return -1;

  self->idle = g_idle_source_new ();

  g_source_set_priority (self->idle, priority);

  self->attached = FALSE;

  return 0;
}

static PyTypeObject Idle_Type = {
  PyObject_HEAD_INIT(NULL)
  0,					/* ob_size */
  "gidle.Idle",				/* tp_name */
  sizeof(Idle),				/* tp_basicsize */
  0,					/* tp_itemsize */
  (destructor)idle_dealloc,		/* tp_dealloc */
  0,					/* tp_print */
  0,					/* tp_getattr */
  0,					/* tp_setattr */
  0,					/* tp_compare */
  0,					/* tp_repr */
  0,					/* tp_as_number */
  0,					/* tp_as_sequence */
  0,					/* tp_as_mapping */
  0,					/* tp_hash */
  0,					/* tp_call */
  0,					/* tp_str */
  0,					/* tp_getattro */
  0,					/* tp_setattro */
  0,					/* tp_as_buffer */
  Py_TPFLAGS_DEFAULT,			/* tp_flags */
  NULL,					/* tp_doc */
  0,					/* tp_traverse */
  0,					/* tp_clear */
  0,					/* tp_richcompare */
  0,					/* tp_weaklistoffset */
  0,					/* tp_iter */
  0,					/* tp_iternext */
  idle_methods,				/* tp_methods */
  0,					/* tp_members */
  idle_getsets,				/* tp_getset */
  0,					/* tp_base */
  0,					/* tp_dict */
  0,					/* tp_descr_get */
  0,					/* tp_descr_set */
  0,					/* tp_dictoffset */
  (initproc)idle_init,			/* tp_init */
  0,					/* tp_alloc */
  0,					/* tp_new */
};

static PyMethodDef gidle_methods[] = {
  {NULL,       NULL}    /* sentinel */
};

void
initgidle (void)
{
  PyObject *m;

  Idle_Type.tp_new = PyType_GenericNew;
  if (PyType_Ready (&Idle_Type) < 0)
    return;

  m = Py_InitModule ("gidle", gidle_methods);

  Py_INCREF (&Idle_Type);
  PyModule_AddObject (m, "Idle", (PyObject *) &Idle_Type);

  if (PyErr_Occurred ())
    Py_FatalError ("can't initialize module gidle");
}
