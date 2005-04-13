/*
 * gidlemodule.c
 *
 * Fuller interface to GLIB's idle sources.
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

static PyMethodDef gidle_methods[] = {
  {NULL,       NULL}    /* sentinel */
};

void
initgidle (void)
{
  PyObject *m;

  m = Py_InitModule ("gidle", gidle_methods);

  if (PyErr_Occurred ())
    Py_FatalError ("can't initialize module gidle");
}
