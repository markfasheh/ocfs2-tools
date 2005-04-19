/*
 * o2cbmodule.c
 *
 * O2CB python binding.
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
#include <structmember.h>

#include "o2cb.h"
#include "o2cb_abi.h"


typedef struct {
  PyObject_HEAD
  PyObject *name;
} O2CBObject;

#define O2CB_OBJECT_NAME(obj) (((O2CBObject *) (obj))->name)

typedef O2CBObject Cluster;

typedef struct {
  O2CBObject  object;
  Cluster    *cluster;
} Node;


static PyObject *o2cb_error;

#define CHECK_ERROR(call)				do {	\
  ret = call;							\
  if (ret)							\
    {								\
      PyErr_SetString (o2cb_error, error_message (ret));	\
      return NULL;						\
    }								\
} while (0)


static void
o2cb_object_dealloc (O2CBObject *self)
{
  Py_XDECREF (self->name);
  PyObject_DEL (self);
}

static PyObject *
o2cb_object_repr (O2CBObject *self,
                  const char *type_name)
{
  return PyString_FromFormat ("<o2cb.%s '%s'>", type_name,
			      PyString_AS_STRING (self->name));
}

static PyMemberDef o2cb_object_members[] = {
  {"name", T_OBJECT, offsetof (O2CBObject, name), RO},
  {0}
};

static PyObject *
o2cb_object_new (O2CBObject *self,
		 const char *name)
{
  if (self == NULL)
    return NULL;

  self->name = PyString_FromString (name);

  if (self->name == NULL)
    {
      PyObject_DEL (self);
      return NULL;
    }

  return (PyObject *) self;
}

static PyObject *
node_number (Node *self, void *closure)
{
  errcode_t ret;
  uint16_t  node_num;

  CHECK_ERROR (o2cb_get_node_num (PyString_AS_STRING (self->cluster->name),
				  PyString_AS_STRING (O2CB_OBJECT_NAME (self)),
				  &node_num));

  return PyInt_FromLong (node_num);
}

static PyGetSetDef node_getsets[] = {
  {"number", (getter)node_number, (setter)0},
  {NULL}
};

static void
node_dealloc (Node *self)
{
  Py_XDECREF (self->cluster);
  o2cb_object_dealloc ((O2CBObject *) self);
}

static PyObject *
node_repr (Node *self)
{
  return o2cb_object_repr ((O2CBObject *) self, "Node");
}

static PyTypeObject Node_Type = {
  PyObject_HEAD_INIT(NULL)
  0,					/* ob_size */
  "o2cb.Node",				/* tp_name */
  sizeof(Node),				/* tp_basicsize */
  0,					/* tp_itemsize */
  (destructor)node_dealloc,		/* tp_dealloc */
  0,					/* tp_print */
  0,					/* tp_getattr */
  0,					/* tp_setattr */
  0,					/* tp_compare */
  (reprfunc)node_repr,			/* tp_repr */
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
  0,					/* tp_methods */
  o2cb_object_members,			/* tp_members */
  node_getsets,				/* tp_getset */
  0,					/* tp_base */
  0,					/* tp_dict */
  0,					/* tp_descr_get */
  0,					/* tp_descr_set */
  0,					/* tp_dictoffset */
  0,					/* tp_init */
  0,					/* tp_alloc */
  0,					/* tp_new */
};

static PyObject *
node_new (Cluster    *cluster,
	  const char *name)
{
  Node *self;

  self = PyObject_NEW (Node, &Node_Type);

  self = (Node *) o2cb_object_new ((O2CBObject *) self, name);

  if (self)
    {
      Py_INCREF (cluster);
      self->cluster = cluster;
    }

  return (PyObject *) self;
}

static PyObject *
cluster_add_node (Cluster  *self,
		  PyObject *args,
		  PyObject *kwargs)
{
  /* XXX: We could be smarter about type conversions here */
  const char *node_name, *node_num, *ip_address, *ip_port, *local;
  errcode_t   ret;

  static char *kwlist[] = { "node_name", "node_num",
			    "ip_address", "ip_port", "local",
			    NULL };

  if (!PyArg_ParseTupleAndKeywords (args, kwargs, "sssss:add_node", kwlist,
				    &node_name, &node_num,
				    &ip_address, &ip_port, &local))
    return NULL;

  CHECK_ERROR (o2cb_add_node (PyString_AS_STRING (self->name),
			      node_name, node_num,
			      ip_address, ip_port, local));

  return node_new (self, node_name);
}

static PyObject *
cluster_create_heartbeat_region_disk (Cluster  *self,
				      PyObject *args,
				      PyObject *kwargs)
{
  const char *region_name, *device_name;
  int         block_bytes;
  uint64_t    start_block, blocks;
  errcode_t   ret;

  static char *kwlist[] = { "region_name", "device_name",
			    "block_bytes", "start_block", "blocks",
			    NULL };

  if (!PyArg_ParseTupleAndKeywords (args, kwargs,
				    "ssiKK:create_heartbeat_region_disk",
				    kwlist,
				    &region_name, &device_name,
				    &block_bytes, &start_block, &blocks))
    return NULL;

  CHECK_ERROR 
    (o2cb_create_heartbeat_region_disk (PyString_AS_STRING (self->name),
					region_name, device_name,
					block_bytes, start_block, blocks));

  Py_INCREF (Py_None);
  return Py_None;
}

static PyObject *
cluster_remove_heartbeat_region_disk (Cluster  *self,
				      PyObject *args,
				      PyObject *kwargs)
{
  const char *region_name;
  errcode_t   ret;

  static char *kwlist[] = { "region_name", NULL };

  if (!PyArg_ParseTupleAndKeywords (args, kwargs,
				    "s:remove_heartbeat_region_disk", kwlist,
				    &region_name))
    return NULL;

  CHECK_ERROR
    (o2cb_remove_heartbeat_region_disk (PyString_AS_STRING (self->name),
					region_name));

  Py_INCREF (Py_None);
  return Py_None;
}

static PyMethodDef cluster_methods[] = {
  {"add_node", (PyCFunction)cluster_add_node, METH_VARARGS | METH_KEYWORDS},
  {"create_heartbeat_region_disk", (PyCFunction)cluster_create_heartbeat_region_disk, METH_VARARGS | METH_KEYWORDS},
  {"remove_heartbeat_region_disk", (PyCFunction)cluster_remove_heartbeat_region_disk, METH_VARARGS | METH_KEYWORDS},
  {NULL, NULL}
};

static PyObject *
cluster_nodes (Cluster *self, void *closure)
{
  char      **nodes, **name;
  errcode_t   ret;
  PyObject   *list, *node;
  int         status;

  CHECK_ERROR (o2cb_list_nodes (PyString_AS_STRING (self->name), &nodes));

  list = PyList_New (0);
  if (list == NULL)
    goto cleanup;

  for (name = nodes; *name != NULL; name++)
    {
      node = node_new (self, *name);
      if (node == NULL)
	goto err;

      status = PyList_Append (list, node);
      Py_DECREF (node);

      if (status)
	goto err;
    }

  goto cleanup;

err:
  Py_DECREF (list);

cleanup:
  o2cb_free_nodes_list (nodes);

  return list;
}

static PyGetSetDef cluster_getsets[] = {
  {"nodes", (getter)cluster_nodes, (setter)0},
  {NULL}
};

static PyObject *
cluster_repr (Cluster *self)
{
  return o2cb_object_repr (self, "Cluster");
}

static int
cluster_init (Cluster  *self,
	      PyObject *args,
	      PyObject *kwargs)
{
  errcode_t   ret;
  const char *name;

  static char *kwlist[] = { "name", NULL };

  if (!PyArg_ParseTupleAndKeywords(args, kwargs,
				   "s:o2cb.Cluster.__init__", kwlist,
				   &name))
    return -1;

  self->name = PyString_FromString (name);
  if (self->name == NULL)
    return -1;

  ret = o2cb_create_cluster (name);

  if (ret)
    {
      Py_DECREF (self->name);
      PyErr_SetString (o2cb_error, error_message (ret));
      return -1;
    }

  return 0;
}

static PyTypeObject Cluster_Type = {
  PyObject_HEAD_INIT(NULL)
  0,					/* ob_size */
  "o2cb.Cluster",			/* tp_name */
  sizeof(Cluster),			/* tp_basicsize */
  0,					/* tp_itemsize */
  (destructor)o2cb_object_dealloc,	/* tp_dealloc */
  0,					/* tp_print */
  0,					/* tp_getattr */
  0,					/* tp_setattr */
  0,					/* tp_compare */
  (reprfunc)cluster_repr,		/* tp_repr */
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
  cluster_methods,			/* tp_methods */
  o2cb_object_members,			/* tp_members */
  cluster_getsets,			/* tp_getset */
  0,					/* tp_base */
  0,					/* tp_dict */
  0,					/* tp_descr_get */
  0,					/* tp_descr_set */
  0,					/* tp_dictoffset */
  (initproc)cluster_init,		/* tp_init */
  0,					/* tp_alloc */
  0,					/* tp_new */
};

static PyObject *
cluster_new (const char *name)
{
  Cluster *self;

  self = PyObject_NEW (Cluster, &Cluster_Type);

  return o2cb_object_new (self, name);
}

static PyObject *
list_clusters (PyObject *self)
{
  char      **clusters, **name;
  errcode_t   ret;
  PyObject   *list, *cluster;
  int         status;

  CHECK_ERROR (o2cb_list_clusters (&clusters));

  list = PyList_New (0);
  if (list == NULL)
    goto cleanup;

  for (name = clusters; *name != NULL; name++)
    {
      cluster = cluster_new (*name);
      if (cluster == NULL)
	goto err;

      status = PyList_Append (list, cluster);
      Py_DECREF (cluster);

      if (status)
	goto err;
    }

  goto cleanup;

err:
  Py_DECREF (list);

cleanup:
  o2cb_free_cluster_list (clusters);

  return list;
}

static PyObject *
create_heartbeat_region_disk (PyObject *self,
			      PyObject *args,
			      PyObject *kwargs)
{
  const char *cluster_name, *region_name, *device_name;
  int         block_bytes;
  uint64_t    start_block, blocks;
  errcode_t   ret;

  static char *kwlist[] = { "cluster_name", "region_name", "device_name",
			    "block_bytes", "start_block", "blocks",
			    NULL };

  if (!PyArg_ParseTupleAndKeywords (args, kwargs,
				    "zssiKK:create_heartbeat_region_disk",
				    kwlist,
				    &cluster_name, &region_name, &device_name,
				    &block_bytes, &start_block, &blocks))
    return NULL;

  CHECK_ERROR 
    (o2cb_create_heartbeat_region_disk (cluster_name, region_name, device_name,
					block_bytes, start_block, blocks));

  Py_INCREF (Py_None);
  return Py_None;
}

static PyObject *
remove_heartbeat_region_disk (PyObject *self,
			      PyObject *args,
			      PyObject *kwargs)
{
  const char *cluster_name, *region_name;
  errcode_t   ret;

  static char *kwlist[] = { "cluster_name", "region_name", NULL };

  if (!PyArg_ParseTupleAndKeywords (args, kwargs,
				    "zs:remove_heartbeat_region_disk", kwlist,
				    &cluster_name, &region_name))
    return NULL;

  CHECK_ERROR (o2cb_remove_heartbeat_region_disk (cluster_name, region_name));

  Py_INCREF (Py_None);
  return Py_None;
}

static PyMethodDef o2cb_methods[] = {
  {"list_clusters", (PyCFunction)list_clusters, METH_NOARGS},
  {"create_heartbeat_region_disk", (PyCFunction)create_heartbeat_region_disk, METH_VARARGS | METH_KEYWORDS},
  {"remove_heartbeat_region_disk", (PyCFunction)remove_heartbeat_region_disk, METH_VARARGS | METH_KEYWORDS},
  {NULL,       NULL}    /* sentinel */
};

static void
add_constants (PyObject *m)
{
  PyModule_AddStringConstant (m, "CONFIGFS_PATH", CONFIGFS_PATH);

#define ADD_STR_CONSTANT(name) \
  PyModule_AddStringConstant (m, "FORMAT_" #name, O2CB_FORMAT_ ## name)

  ADD_STR_CONSTANT (CLUSTER_DIR);
  ADD_STR_CONSTANT (CLUSTER);
  ADD_STR_CONSTANT (NODE_DIR);
  ADD_STR_CONSTANT (NODE);
  ADD_STR_CONSTANT (NODE_ATTR);
  ADD_STR_CONSTANT (HEARTBEAT_DIR);
  ADD_STR_CONSTANT (HEARTBEAT_REGION);
  ADD_STR_CONSTANT (HEARTBEAT_REGION_ATTR);

#undef ADD_STR_CONSTANT
}

void
inito2cb (void)
{
  PyObject *m;

  if (PyType_Ready (&Node_Type) < 0)
    return;

  Cluster_Type.tp_new = PyType_GenericNew;
  if (PyType_Ready (&Cluster_Type) < 0)
    return;

  initialize_o2cb_error_table ();

  m = Py_InitModule ("o2cb", o2cb_methods);

  o2cb_error = PyErr_NewException ("o2cb.error", PyExc_RuntimeError, NULL);

  if (o2cb_error)
    {
      Py_INCREF (o2cb_error);
      PyModule_AddObject (m, "error", o2cb_error);
    }

  Py_INCREF (&Node_Type);
  PyModule_AddObject (m, "Node", (PyObject *) &Node_Type);

  Py_INCREF (&Cluster_Type);
  PyModule_AddObject (m, "Cluster", (PyObject *) &Cluster_Type);

  add_constants (m);

  if (PyErr_Occurred ())
    Py_FatalError ("can't initialize module o2cb");
}
