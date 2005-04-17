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

#include <Python.h>
#include <structmember.h>

#include <inttypes.h>

#include <uuid/uuid.h>

#include "ocfs2.h"


#define Filesystem_Check(op)  PyObject_TypeCheck(op, &Filesystem_Type)
#define DInode_Check(op)      PyObject_TypeCheck(op, &DInode_Type)
#define DirEntry_Check(op)    PyObject_TypeCheck(op, &DirEntry_Type)
#define SuperBlock_Check(op)  PyObject_TypeCheck(op, &SuperBlock_Type)
#define DirScanIter_Check(op) PyObject_TypeCheck(op, &DirScanIter_Type)


typedef struct {
  PyObject_HEAD
  PyObject      *device;
  ocfs2_filesys *fs;
} Filesystem;

typedef struct {
  PyObject_HEAD
  Filesystem    *fs_obj;
  ocfs2_dinode   dinode;
} DInode;

typedef struct {
  PyObject_HEAD
  Filesystem             *fs_obj;
  struct ocfs2_dir_entry  dentry;
} DirEntry;

typedef struct {
  PyObject_HEAD
  Filesystem        *fs_obj;
  ocfs2_super_block  super;
} SuperBlock;

typedef struct {
  PyObject_HEAD
  Filesystem     *fs_obj;
  ocfs2_dir_scan *scan;
} DirScanIter;


static PyObject *ocfs2_error;

#define CHECK_ERROR(call)				do {	\
  ret = call;							\
  if (ret)							\
    {								\
      PyErr_SetString (ocfs2_error, error_message (ret));	\
      return NULL;						\
    }								\
} while (0)


#define DINODE_U64_GETTER(name) \
  static PyObject *							\
  dinode_ ## name (DInode *self, void *closure)				\
  {									\
    return PyLong_FromUnsignedLongLong (self->dinode.i_ ## name);	\
  }

#define DINODE_GETTER_ENTRY(name) \
  {"i_" #name, (getter)dinode_ ## name, (setter)0}

DINODE_U64_GETTER(size)
DINODE_U64_GETTER(atime)
DINODE_U64_GETTER(ctime)
DINODE_U64_GETTER(mtime)
DINODE_U64_GETTER(dtime)
DINODE_U64_GETTER(blkno)
DINODE_U64_GETTER(last_eb_blk)

static PyObject *
dinode_rdev (DInode *self, void *closure)
{
  return PyLong_FromUnsignedLongLong (self->dinode.id1.dev1.i_rdev);
}

static PyObject *
dinode_jflags (DInode *self, void *closure)
{
  return PyInt_FromLong (self->dinode.id1.journal1.ij_flags);
}

static PyGetSetDef dinode_getsets[] = {
  DINODE_GETTER_ENTRY (size),
  DINODE_GETTER_ENTRY (atime),
  DINODE_GETTER_ENTRY (ctime),
  DINODE_GETTER_ENTRY (mtime),
  DINODE_GETTER_ENTRY (dtime),
  DINODE_GETTER_ENTRY (blkno),
  DINODE_GETTER_ENTRY (last_eb_blk),
  DINODE_GETTER_ENTRY (rdev),
  {"ij_flags", (getter)dinode_jflags, (setter)0},
  {NULL}
};

#undef DINODE_U64_GETTER
#undef DINODE_GETTER_ENTRY

#define DINODE_STR_MEMBER(name) \
  {"i_" #name, T_STRING_INPLACE, offsetof (DInode, dinode.i_ ## name), RO}
#define DINODE_S16_MEMBER(name) \
  {"i_" #name, T_SHORT, offsetof (DInode, dinode.i_ ## name), RO}
#define DINODE_U16_MEMBER(name) \
  {"i_" #name, T_USHORT, offsetof (DInode, dinode.i_ ## name), RO}
#define DINODE_U32_MEMBER(name) \
  {"i_" #name, T_UINT, offsetof (DInode, dinode.i_ ## name), RO}
#define DINODE_BITMAP_MEMBER(name) \
  {"i_" #name, T_UINT, offsetof (DInode, dinode.id1.bitmap1.i_ ## name), RO}

static PyMemberDef dinode_members[] = {
  DINODE_STR_MEMBER (signature),
  DINODE_U32_MEMBER (generation),
  DINODE_S16_MEMBER (suballoc_node),
  DINODE_U16_MEMBER (suballoc_bit),
  DINODE_U32_MEMBER (clusters),
  DINODE_U32_MEMBER (uid),
  DINODE_U32_MEMBER (gid),
  DINODE_U16_MEMBER (mode),
  DINODE_U16_MEMBER (links_count),
  DINODE_U32_MEMBER (flags),
  DINODE_U32_MEMBER (fs_generation),
  DINODE_BITMAP_MEMBER (used),
  DINODE_BITMAP_MEMBER (total),
  {"fs", T_OBJECT, offsetof (DInode, fs_obj), RO},
  {0}
};

#undef DINODE_STR_MEMBER
#undef DINODE_S16_MEMBER
#undef DINODE_U16_MEMBER
#undef DINODE_U32_MEMBER
#undef DINODE_BITMAP_MEMBER

static void
dinode_dealloc (DInode *self)
{
  Py_DECREF (self->fs_obj);
  PyObject_DEL (self);
}

static PyObject *
dinode_repr (DInode *self)
{
  char blkno[32];

  snprintf (blkno, sizeof (blkno), "%"PRIu64, self->dinode.i_blkno);
  return PyString_FromFormat ("<ocfs2.DInode %s on %s>", blkno,
			      PyString_AS_STRING (self->fs_obj->device));
}

static PyTypeObject DInode_Type = {
  PyObject_HEAD_INIT(NULL)
  0,					/* ob_size */
  "ocfs2.DInode",			/* tp_name */
  sizeof(DInode),			/* tp_basicsize */
  0,					/* tp_itemsize */
  (destructor)dinode_dealloc,		/* tp_dealloc */
  0,					/* tp_print */
  0,					/* tp_getattr */
  0,					/* tp_setattr */
  0,					/* tp_compare */
  (reprfunc)dinode_repr,		/* tp_repr */
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
  dinode_members,			/* tp_members */
  dinode_getsets,			/* tp_getset */
};

static PyObject *
dinode_new (Filesystem   *fs_obj,
	    ocfs2_dinode *dinode)
{
  DInode *self;

  self = PyObject_NEW (DInode, &DInode_Type);

  if (self == NULL)
    return NULL;

  Py_INCREF (fs_obj);
  self->fs_obj = fs_obj;

  memcpy (&self->dinode, dinode, sizeof (*dinode));

  return (PyObject *) self;
}

static PyObject *
dir_entry_name (DirEntry *self, void *closure)
{
  return PyString_FromStringAndSize (self->dentry.name, self->dentry.name_len);
}

static PyObject *
dir_entry_inode (DirEntry *self, void *closure)
{
  return PyLong_FromUnsignedLongLong (self->dentry.inode);
}

static PyGetSetDef dir_entry_getsets[] = {
  {"name", (getter)dir_entry_name, (setter)0},
  {"inode", (getter)dir_entry_inode, (setter)0},
  {NULL}
};

static PyMemberDef dir_entry_members[] = {
  {"rec_len", T_USHORT, offsetof (DirEntry, dentry.rec_len), RO},
  {"file_type", T_UBYTE, offsetof (DirEntry, dentry.file_type), RO},
  {"fs", T_OBJECT, offsetof (DirEntry, fs_obj), RO},
  {0}
};

static void
dir_entry_dealloc (DirEntry *self)
{
  PyObject_DEL (self);
}

static PyObject *
dir_entry_repr (DirEntry *self)
{
  char name[OCFS2_MAX_FILENAME_LEN + 1];

  strncpy (name, self->dentry.name, self->dentry.name_len);
  name[self->dentry.name_len] = '\0';

  return PyString_FromFormat ("<ocfs2.DirEntry '%s' on %s>", name,
			      PyString_AS_STRING (self->fs_obj->device));
}

static PyTypeObject DirEntry_Type = {
  PyObject_HEAD_INIT(NULL)
  0,					/* ob_size */
  "ocfs2.DirEntry",			/* tp_name */
  sizeof(DirEntry),			/* tp_basicsize */
  0,					/* tp_itemsize */
  (destructor)dir_entry_dealloc,	/* tp_dealloc */
  0,					/* tp_print */
  0,					/* tp_getattr */
  0,					/* tp_setattr */
  0,					/* tp_compare */
  (reprfunc)dir_entry_repr,		/* tp_repr */
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
  dir_entry_members,			/* tp_members */
  dir_entry_getsets,			/* tp_getset */
};

static PyObject *
dir_entry_new (Filesystem             *fs_obj,
	       struct ocfs2_dir_entry *dentry)
{
  DirEntry *self;

  self = PyObject_NEW (DirEntry, &DirEntry_Type);

  if (self == NULL)
    return NULL;

  Py_INCREF (fs_obj);
  self->fs_obj = fs_obj;

  memcpy (&self->dentry, dentry, sizeof (*dentry));

  return (PyObject *) self;
}

#define SUPER_U64_GETTER(name) \
  static PyObject *							\
  super_ ## name (SuperBlock *self, void *closure)			\
  {									\
    return PyLong_FromUnsignedLongLong (self->super.s_ ## name);	\
  }

#define SUPER_GETTER_ENTRY(name) \
  {"s_" #name, (getter)super_ ## name, (setter)0}

SUPER_U64_GETTER (lastcheck)
SUPER_U64_GETTER (root_blkno)
SUPER_U64_GETTER (system_dir_blkno)
SUPER_U64_GETTER (first_cluster_group)

static PyObject *
super_uuid (SuperBlock *self, void *closure)
{
  return PyString_FromStringAndSize (self->super.s_uuid,
                                     sizeof (self->super.s_uuid));
}

static PyObject *
super_uuid_unparsed (SuperBlock *self, void *closure)
{
  char buf[40];

  uuid_unparse (self->super.s_uuid, buf);
  return PyString_FromString (buf);
}

static PyGetSetDef super_getsets[] = {
  SUPER_GETTER_ENTRY (lastcheck),
  SUPER_GETTER_ENTRY (root_blkno),
  SUPER_GETTER_ENTRY (system_dir_blkno),
  SUPER_GETTER_ENTRY (first_cluster_group),
  SUPER_GETTER_ENTRY (uuid),
  {"uuid_unparsed", (getter)super_uuid_unparsed, (setter)0},
  {NULL}
};

#undef SUPER_U64_GETTER
#undef SUPER_GETTER_ENTRY

#define SUPER_U16_MEMBER(name) \
  {"s_" #name, T_USHORT, offsetof (SuperBlock, super.s_ ## name), RO}
#define SUPER_U32_MEMBER(name) \
  {"s_" #name, T_UINT, offsetof (SuperBlock, super.s_ ## name), RO}

static PyMemberDef super_members[] = {
  SUPER_U16_MEMBER (major_rev_level),
  SUPER_U16_MEMBER (minor_rev_level),
  SUPER_U16_MEMBER (mnt_count),
  SUPER_U16_MEMBER (state),
  SUPER_U16_MEMBER (errors),
  SUPER_U32_MEMBER (checkinterval),
  SUPER_U32_MEMBER (creator_os),
  SUPER_U32_MEMBER (feature_compat),
  SUPER_U32_MEMBER (feature_incompat),
  SUPER_U32_MEMBER (feature_ro_compat),
  SUPER_U32_MEMBER (blocksize_bits),
  SUPER_U32_MEMBER (clustersize_bits),
  SUPER_U16_MEMBER (max_nodes),
  {"s_label", T_STRING_INPLACE, offsetof (SuperBlock, super.s_label), RO},
  {"fs", T_OBJECT, offsetof (SuperBlock, fs_obj), RO},
  {0}
};

#undef SUPER_U16_MEMBER
#undef SUPER_U32_MEMBER

static void
super_dealloc (SuperBlock *self)
{
  Py_DECREF (self->fs_obj);
  PyObject_DEL (self);
}

static PyObject *
super_repr (SuperBlock *self)
{
  return PyString_FromFormat ("<ocfs2.SuperBlock on %s>",
			      PyString_AS_STRING (self->fs_obj->device));
}

static PyTypeObject SuperBlock_Type = {
  PyObject_HEAD_INIT(NULL)
  0,					/* ob_size */
  "ocfs2.SuperBlock",			/* tp_name */
  sizeof(SuperBlock),			/* tp_basicsize */
  0,					/* tp_itemsize */
  (destructor)super_dealloc,		/* tp_dealloc */
  0,					/* tp_print */
  0,					/* tp_getattr */
  0,					/* tp_setattr */
  0,					/* tp_compare */
  (reprfunc)super_repr,			/* tp_repr */
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
  super_members,			/* tp_members */
  super_getsets,			/* tp_getset */
};

static PyObject *
super_new (Filesystem   *fs_obj,
	   ocfs2_dinode *fs_super)
{
  SuperBlock *self;

  self = PyObject_NEW (SuperBlock, &SuperBlock_Type);

  if (self == NULL)
    return NULL;

  Py_INCREF (fs_obj);
  self->fs_obj = fs_obj;

  memcpy (&self->super, &fs_super->id2.i_super, sizeof (self->super));

  return (PyObject *) self;
}

static void
dir_scan_iter_dealloc (DirScanIter *self)
{
  if (self->scan)
    ocfs2_close_dir_scan (self->scan);

  Py_XDECREF (self->fs_obj);
  PyObject_DEL (self);
}

static PyObject *
dir_scan_iter_getiter (DirScanIter *self)
{
  Py_INCREF (self);
  return (PyObject *) self;
}

static PyObject *
dir_scan_iter_next (DirScanIter *self)
{
  errcode_t              ret;
  struct ocfs2_dir_entry dirent;

  if (self->scan == NULL)
    {
      PyErr_SetNone (PyExc_StopIteration);
      return NULL;
    }

  CHECK_ERROR (ocfs2_get_next_dir_entry (self->scan, &dirent));

  if (dirent.rec_len == 0)
    {
      ocfs2_close_dir_scan (self->scan);
      self->scan = NULL;

      Py_DECREF (self->fs_obj);
      self->fs_obj = NULL;

      PyErr_SetNone (PyExc_StopIteration);
      return NULL;
    }

  return dir_entry_new (self->fs_obj, &dirent);
}

static PyTypeObject DirScanIter_Type = {
  PyObject_HEAD_INIT(NULL)
  0,					/* ob_size */
  "ocfs2.DirScanIter",			/* tp_name */
  sizeof(DirScanIter),			/* tp_basicsize */
  0,					/* tp_itemsize */
  (destructor)dir_scan_iter_dealloc,	/* tp_dealloc */
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
  Py_TPFLAGS_DEFAULT|Py_TPFLAGS_HAVE_ITER, /* tp_flags */
  NULL,					/* tp_doc */
  0,					/* tp_traverse */
  0,					/* tp_clear */
  0,					/* tp_richcompare */
  0,					/* tp_weaklistoffset */
  (getiterfunc)dir_scan_iter_getiter,	/* tp_iter */
  (iternextfunc)dir_scan_iter_next,	/* tp_iternext */
};

static PyObject *
dir_scan_iter_new (Filesystem     *fs_obj,
		   ocfs2_dir_scan *scan)
{
  DirScanIter *self;

  self = PyObject_NEW (DirScanIter, &DirScanIter_Type);

  if (self == NULL)
    {
      ocfs2_close_dir_scan (scan);
      return NULL;
    }

  Py_INCREF (fs_obj);
  self->fs_obj = fs_obj;

  self->scan = scan;

  return (PyObject *) self;
}

static PyObject *
fs_flush (Filesystem *self)
{
  errcode_t ret;

  CHECK_ERROR (ocfs2_flush (self->fs));

  Py_INCREF(Py_None);
  return Py_None;
}

static PyObject *
fs_clusters_to_blocks (Filesystem *self,
		       PyObject   *args,
		       PyObject   *kwargs)
{
  unsigned int clusters;
  uint64_t     blocks;

  static char *kwlist[] = { "clusters", NULL };

  if (!PyArg_ParseTupleAndKeywords (args, kwargs,
				    "I:clusters_to_blocks", kwlist,
				    &clusters))
    return NULL;

  blocks = ocfs2_clusters_to_blocks (self->fs, clusters);

  return PyLong_FromUnsignedLongLong (blocks);
}

static PyObject *
fs_blocks_to_clusters (Filesystem *self,
		       PyObject   *args,
		       PyObject   *kwargs)
{
  unsigned long long blocks;
  uint32_t           clusters;

  static char *kwlist[] = { "blocks", NULL };

  if (!PyArg_ParseTupleAndKeywords (args, kwargs,
				    "K:blocks_to_clusters", kwlist,
				    &blocks))
    return NULL;

  clusters = ocfs2_clusters_to_blocks (self->fs, blocks);

  return PyInt_FromLong (clusters);
}

static PyObject *
fs_blocks_in_bytes (Filesystem *self,
		    PyObject   *args,
		    PyObject   *kwargs)
{
  unsigned long long bytes;
  uint64_t           blocks;

  static char *kwlist[] = { "bytes", NULL };

  if (!PyArg_ParseTupleAndKeywords (args, kwargs,
				    "K:blocks_in_bytes", kwlist,
				    &bytes))
    return NULL;

  blocks = ocfs2_blocks_in_bytes (self->fs, bytes);

  return PyLong_FromUnsignedLongLong (blocks);
}

static PyObject *
fs_clusters_in_blocks (Filesystem *self,
		       PyObject   *args,
		       PyObject   *kwargs)
{
  unsigned long long blocks;
  uint32_t           clusters;

  static char *kwlist[] = { "blocks", NULL };

  if (!PyArg_ParseTupleAndKeywords (args, kwargs,
				    "K:clusters_in_blocks", kwlist,
				    &blocks))
    return NULL;

  clusters = ocfs2_clusters_in_blocks (self->fs, blocks);

  return PyInt_FromLong (clusters);
}

static PyObject *
fs_block_out_of_range (Filesystem *self,
		       PyObject   *args,
		       PyObject   *kwargs)
{
  unsigned long long block;
  int                ret;

  static char *kwlist[] = { "block", NULL };

  if (!PyArg_ParseTupleAndKeywords (args, kwargs,
				    "K:block_out_of_range", kwlist,
				    &block))
    return NULL;

  ret = ocfs2_block_out_of_range(self->fs, block);

  return PyBool_FromLong (ret);
}

static PyObject *
fs_lookup_system_inode (Filesystem *self,
                        PyObject   *args,
                        PyObject   *kwargs)
{
  errcode_t ret;
  int       type, node_num = -1;
  uint64_t  blkno;

  static char *kwlist[] = { "type", "node_num", NULL };

  if (!PyArg_ParseTupleAndKeywords (args, kwargs,
				    "i|i:lookup_system_inode", kwlist,
				    &type, &node_num))
    return NULL;

  CHECK_ERROR (ocfs2_lookup_system_inode(self->fs, type, node_num, &blkno));

  return PyLong_FromUnsignedLongLong (blkno);
}

static PyObject *
fs_read_cached_inode (Filesystem *self,
                      PyObject   *args,
                      PyObject   *kwargs)
{
  errcode_t           ret;
  unsigned long long  blkno;
  ocfs2_cached_inode *cinode;
  PyObject           *dinode;

  static char *kwlist[] = { "blkno", NULL };

  if (!PyArg_ParseTupleAndKeywords (args, kwargs,
				    "K:read_cached_inode", kwlist,
				    &blkno))
    return NULL;

  CHECK_ERROR (ocfs2_read_cached_inode (self->fs, blkno, &cinode));

  dinode = dinode_new (self, cinode->ci_inode);

  /* XXX: error check */
  ocfs2_free_cached_inode (self->fs, cinode);

  return dinode;
}

typedef struct
{
  PyObject   *func;
  PyObject   *data;
  Filesystem *fs;
} WalkData;

static int
walk_dirs (struct ocfs2_dir_entry *dirent,
           int                     offset,
	   int                     blocksize,
	   char                   *buf,
	   void                   *priv_data)
{
  PyObject *de;
  WalkData *data = priv_data;

  de = dir_entry_new (data->fs, dirent);

  if (de == NULL)
    return OCFS2_DIRENT_ERROR;

  /* XXX: handle errors */
  if (data->data)
    PyObject_CallFunction (data->func, "OiiO", de, offset, blocksize,
			   data->data);
  else
    PyObject_CallFunction (data->func, "Oii", de, offset, blocksize);

  Py_DECREF (de);

  return 0;
}

static PyObject *
fs_dir_iterate (Filesystem *self,
                PyObject   *args,
                PyObject   *kwargs)
{
  errcode_t  ret;
  WalkData   wdata;
  PyObject  *py_func, *py_data = NULL, *py_dir = NULL;
  uint64_t   dir;
  int        flags = OCFS2_DIRENT_FLAG_EXCLUDE_DOTS;

  static char *kwlist[] = { "callback", "data", "dir", "flags", NULL };

  if (!PyArg_ParseTupleAndKeywords (args, kwargs,
				    "O|OOi:dir_iterate", kwlist,
				    &py_func, &py_data, &py_dir, &flags))
    return NULL;

  if (!PyCallable_Check (py_func))
    {
      PyErr_SetString (PyExc_TypeError, "callback must be a callable object");
      return NULL;
    }

  if (py_dir == NULL || py_dir == Py_None)
    dir = self->fs->fs_root_blkno;
  else if (DirEntry_Check (py_dir))
    dir = ((DirEntry *) py_dir)->dentry.inode;
  else if (PyInt_Check (py_dir))
    dir = PyInt_AsUnsignedLongMask (py_dir);
  else if (PyLong_Check (py_dir))
    dir = PyLong_AsUnsignedLongLongMask (py_dir);
  else
    {
      PyErr_SetString (PyExc_TypeError, "dir must be DirEntry or integer");
      return NULL;
    }

  Py_INCREF (py_func);
  wdata.func = py_func;

  Py_XINCREF (py_data);
  wdata.data = py_data;

  wdata.fs = self;

  /* XXX: handle errors */
  ret = ocfs2_dir_iterate (self->fs, dir, flags, NULL, walk_dirs, &wdata);

  Py_DECREF (py_func);
  Py_XDECREF (py_data);

  Py_INCREF (Py_None);
  return Py_None;
}

static PyObject *
fs_dir_scan (Filesystem *self,
             PyObject   *args,
             PyObject   *kwargs)
{
  errcode_t       ret;
  PyObject       *py_dir = NULL;
  uint64_t        dir;
  int             flags = OCFS2_DIR_SCAN_FLAG_EXCLUDE_DOTS;
  ocfs2_dir_scan *scan;

  static char *kwlist[] = { "dir", "flags", NULL };

  if (!PyArg_ParseTupleAndKeywords (args, kwargs,
				    "|Oi:dir_scan", kwlist,
				    &py_dir, &flags))
    return NULL;

  if (py_dir == NULL || py_dir == Py_None)
    dir = self->fs->fs_root_blkno;
  else if (DirEntry_Check (py_dir))
    dir = ((DirEntry *) py_dir)->dentry.inode;
  else if (PyInt_Check (py_dir))
    dir = PyInt_AsUnsignedLongMask (py_dir);
  else if (PyLong_Check (py_dir))
    dir = PyLong_AsUnsignedLongLongMask (py_dir);
  else
    {
      PyErr_SetString (PyExc_TypeError, "dir must be DirEntry or integer");
      return NULL;
    }

  CHECK_ERROR (ocfs2_open_dir_scan (self->fs, dir, flags, &scan));

  return dir_scan_iter_new (self, scan);
}

static PyMethodDef fs_methods[] = {
  {"flush", (PyCFunction)fs_flush, METH_NOARGS},
  {"clusters_to_blocks", (PyCFunction)fs_clusters_to_blocks, METH_VARARGS | METH_KEYWORDS},
  {"blocks_to_clusters", (PyCFunction)fs_blocks_to_clusters, METH_VARARGS | METH_KEYWORDS},
  {"blocks_in_bytes", (PyCFunction)fs_blocks_in_bytes, METH_VARARGS | METH_KEYWORDS},
  {"clusters_in_blocks", (PyCFunction)fs_clusters_in_blocks, METH_VARARGS | METH_KEYWORDS},
  {"block_out_of_range", (PyCFunction)fs_block_out_of_range, METH_VARARGS | METH_KEYWORDS},
  {"lookup_system_inode", (PyCFunction)fs_lookup_system_inode, METH_VARARGS | METH_KEYWORDS},
  {"read_cached_inode", (PyCFunction)fs_read_cached_inode, METH_VARARGS | METH_KEYWORDS},
  {"dir_iterate", (PyCFunction)fs_dir_iterate, METH_VARARGS | METH_KEYWORDS},
  {"iterdir", (PyCFunction)fs_dir_scan, METH_VARARGS | METH_KEYWORDS},
  {NULL, NULL}
};

static PyObject *
fs_super (Filesystem *self, void *closure)
{
  return super_new (self, self->fs->fs_super);
}

static PyObject *
fs_orig_super (Filesystem *self, void *closure)
{
  return super_new (self, self->fs->fs_orig_super);
}

static PyObject *
fs_uuid_str (Filesystem *self, void *closure)
{
  return PyString_FromString (self->fs->uuid_str);
}

#define FS_U32_GETTER(name) \
  static PyObject *							\
  fs_ ## name (Filesystem *self, void *closure)				\
  {									\
    return PyInt_FromLong (self->fs->fs_ ## name);			\
  }

#define FS_UINT_GETTER FS_U32_GETTER

#define FS_U64_GETTER(name) \
  static PyObject *							\
  fs_ ## name (Filesystem *self, void *closure)				\
  {									\
    return PyLong_FromUnsignedLongLong (self->fs->fs_ ## name);		\
  }

#define FS_GETTER_ENTRY(name) \
  {"fs_" #name, (getter)fs_ ## name, (setter)0}

FS_U32_GETTER  (flags)
FS_UINT_GETTER (blocksize)
FS_UINT_GETTER (clustersize)
FS_U32_GETTER  (clusters)
FS_U64_GETTER  (blocks)
FS_U32_GETTER  (umask)
FS_U64_GETTER  (root_blkno)
FS_U64_GETTER  (sysdir_blkno)
FS_U64_GETTER  (first_cg_blkno)

static PyGetSetDef fs_getsets[] = {
  FS_GETTER_ENTRY (flags),
  FS_GETTER_ENTRY (super),
  FS_GETTER_ENTRY (orig_super),
  FS_GETTER_ENTRY (blocksize),
  FS_GETTER_ENTRY (clustersize),
  FS_GETTER_ENTRY (clusters),
  FS_GETTER_ENTRY (blocks),
  FS_GETTER_ENTRY (umask),
  FS_GETTER_ENTRY (root_blkno),
  FS_GETTER_ENTRY (sysdir_blkno),
  FS_GETTER_ENTRY (first_cg_blkno),
  {"uuid_str", (getter)fs_uuid_str, (setter)0},
  {NULL}
};

#undef FS_UINT_GETTER
#undef FS_U32_GETTER
#undef FS_U64_GETTER
#undef FS_GETTER_ENTRY

static PyMemberDef fs_members[] = {
  {"device", T_OBJECT, offsetof (Filesystem, device), RO},
  {"fs_devname", T_OBJECT, offsetof (Filesystem, device), RO},
  {0}
};

static void
fs_dealloc (Filesystem *self)
{
  if (self->fs)
    ocfs2_close (self->fs);

  Py_XDECREF (self->device);
  PyObject_DEL (self);
}

static PyObject *
fs_repr (Filesystem *self)
{
  return PyString_FromFormat("<ocfs2.Filesystem on %s>",
			     PyString_AS_STRING (self->device));
}

static int
fs_init (Filesystem *self,
	 PyObject *args,
	 PyObject *kwargs)
{
  errcode_t     ret;
  char         *device;
  int           flags = OCFS2_FLAG_RO | OCFS2_FLAG_BUFFERED;
  unsigned int  superblock = 0, blksize = 0;

  static char *kwlist[] = { "device", "flags", "superblock", "blocksize",
			    NULL };

  if (!PyArg_ParseTupleAndKeywords(args, kwargs,
				   "s|iII:ocfs2.Filesystem.__init__", kwlist,
				   &device, &flags, &superblock, &blksize))
    return -1;

  self->fs = NULL;
  self->device = PyString_FromString (device);

  if (self->device == NULL)
    return -1;

  ret = ocfs2_open (device, flags, superblock, blksize, &self->fs); 

  if (ret)
    {
      Py_DECREF (self->device);
      self->device = NULL;

      PyErr_SetString (ocfs2_error, error_message (ret));
      return -1;
    }

  return 0;
}

static PyTypeObject Filesystem_Type = {
  PyObject_HEAD_INIT(NULL)
  0,					/* ob_size */
  "ocfs2.Filesystem",			/* tp_name */
  sizeof(Filesystem),			/* tp_basicsize */
  0,					/* tp_itemsize */
  (destructor)fs_dealloc,		/* tp_dealloc */
  0,					/* tp_print */
  0,					/* tp_getattr */
  0,					/* tp_setattr */
  0,					/* tp_compare */
  (reprfunc)fs_repr,			/* tp_repr */
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
  fs_methods,				/* tp_methods */
  fs_members,				/* tp_members */
  fs_getsets,				/* tp_getset */
  0,					/* tp_base */
  0,					/* tp_dict */
  0,					/* tp_descr_get */
  0,					/* tp_descr_set */
  0,					/* tp_dictoffset */
  (initproc)fs_init,			/* tp_init */
  0,					/* tp_alloc */
  0,					/* tp_new */
};

static PyMethodDef ocfs2_methods[] = {
  {NULL,       NULL}    /* sentinel */
};

static void
add_constants (PyObject *m)
{
#define ADD_INT_CONSTANT(name) \
  PyModule_AddIntConstant (m, #name, OCFS2_ ## name)
#define ADD_STR_CONSTANT(name) \
  PyModule_AddStringConstant (m, #name, OCFS2_ ## name)

  ADD_INT_CONSTANT (SUPER_BLOCK_BLKNO);

  ADD_INT_CONSTANT (MIN_CLUSTERSIZE);
  ADD_INT_CONSTANT (MAX_CLUSTERSIZE);

  ADD_INT_CONSTANT (MIN_BLOCKSIZE);
  ADD_INT_CONSTANT (MAX_BLOCKSIZE);

  ADD_INT_CONSTANT (SUPER_MAGIC);

  ADD_STR_CONSTANT (SUPER_BLOCK_SIGNATURE);
  ADD_STR_CONSTANT (INODE_SIGNATURE);
  ADD_STR_CONSTANT (EXTENT_BLOCK_SIGNATURE);
  ADD_STR_CONSTANT (GROUP_DESC_SIGNATURE);

  ADD_INT_CONSTANT (VALID_FL);
  ADD_INT_CONSTANT (ORPHANED_FL);

  ADD_INT_CONSTANT (SYSTEM_FL);
  ADD_INT_CONSTANT (SUPER_BLOCK_FL);
  ADD_INT_CONSTANT (LOCAL_ALLOC_FL);
  ADD_INT_CONSTANT (BITMAP_FL);
  ADD_INT_CONSTANT (JOURNAL_FL);
  ADD_INT_CONSTANT (HEARTBEAT_FL);
  ADD_INT_CONSTANT (CHAIN_FL);

  ADD_INT_CONSTANT (JOURNAL_DIRTY_FL);
  
  ADD_INT_CONSTANT (ERROR_FS);

  ADD_INT_CONSTANT (MAX_FILENAME_LEN);

  ADD_INT_CONSTANT (MAX_NODES);

  ADD_INT_CONSTANT (VOL_UUID_LEN);
  ADD_INT_CONSTANT (MAX_VOL_LABEL_LEN);

  ADD_INT_CONSTANT (MAX_CLUSTER_NAME_LEN);

  ADD_INT_CONSTANT (MIN_JOURNAL_SIZE);
  ADD_INT_CONSTANT (MAX_JOURNAL_SIZE);

  ADD_INT_CONSTANT (FIRST_ONLINE_SYSTEM_INODE);
  ADD_INT_CONSTANT (LAST_GLOBAL_SYSTEM_INODE);

  ADD_INT_CONSTANT (FT_UNKNOWN);
  ADD_INT_CONSTANT (FT_REG_FILE);
  ADD_INT_CONSTANT (FT_DIR);
  ADD_INT_CONSTANT (FT_CHRDEV);
  ADD_INT_CONSTANT (FT_BLKDEV);
  ADD_INT_CONSTANT (FT_FIFO);
  ADD_INT_CONSTANT (FT_SOCK);
  ADD_INT_CONSTANT (FT_SYMLINK);
  ADD_INT_CONSTANT (FT_MAX);

  ADD_INT_CONSTANT (LINK_MAX);

  ADD_INT_CONSTANT (FLAG_RO);
  ADD_INT_CONSTANT (FLAG_RW);
  ADD_INT_CONSTANT (FLAG_CHANGED);
  ADD_INT_CONSTANT (FLAG_DIRTY);
  ADD_INT_CONSTANT (FLAG_SWAP_BYTES);
  ADD_INT_CONSTANT (FLAG_BUFFERED);
  ADD_INT_CONSTANT (FLAG_NO_REV_CHECK);

  ADD_INT_CONSTANT (DIRENT_CHANGED);
  ADD_INT_CONSTANT (DIRENT_ABORT);
  ADD_INT_CONSTANT (DIRENT_ERROR);

  ADD_INT_CONSTANT (DIRENT_FLAG_INCLUDE_EMPTY);
  ADD_INT_CONSTANT (DIRENT_FLAG_INCLUDE_REMOVED);
  ADD_INT_CONSTANT (DIRENT_FLAG_EXCLUDE_DOTS);

#undef ADD_INT_CONSTANT
#undef ADD_STR_CONSTANT

#define ADD_INT_CONSTANT(name) \
  PyModule_AddIntConstant (m, #name, name)

  ADD_INT_CONSTANT (BAD_BLOCK_SYSTEM_INODE);
  ADD_INT_CONSTANT (GLOBAL_INODE_ALLOC_SYSTEM_INODE);
  ADD_INT_CONSTANT (SLOT_MAP_SYSTEM_INODE);
  ADD_INT_CONSTANT (HEARTBEAT_SYSTEM_INODE);
  ADD_INT_CONSTANT (GLOBAL_BITMAP_SYSTEM_INODE);
  ADD_INT_CONSTANT (ORPHAN_DIR_SYSTEM_INODE);
  ADD_INT_CONSTANT (EXTENT_ALLOC_SYSTEM_INODE);
  ADD_INT_CONSTANT (INODE_ALLOC_SYSTEM_INODE);
  ADD_INT_CONSTANT (JOURNAL_SYSTEM_INODE);
  ADD_INT_CONSTANT (LOCAL_ALLOC_SYSTEM_INODE);
  ADD_INT_CONSTANT (NUM_SYSTEM_INODES);

  //ADD_INT_CONSTANT (MAX_NODE_NAME_LEN);

#undef ADD_INT_CONSTANT
}

void
initocfs2 (void)
{
  PyObject *m;

  DInode_Type.tp_new = PyType_GenericNew;
  if (PyType_Ready (&DInode_Type) < 0)
    return;

  DirEntry_Type.tp_new = PyType_GenericNew;
  if (PyType_Ready (&DirEntry_Type) < 0)
    return;

  SuperBlock_Type.tp_new = PyType_GenericNew;
  if (PyType_Ready (&SuperBlock_Type) < 0)
    return;

  DirScanIter_Type.tp_new = PyType_GenericNew;
  if (PyType_Ready (&DirScanIter_Type) < 0)
    return;

  Filesystem_Type.tp_new = PyType_GenericNew;
  if (PyType_Ready (&Filesystem_Type) < 0)
    return;

  initialize_ocfs_error_table ();

  m = Py_InitModule ("ocfs2", ocfs2_methods);

  ocfs2_error = PyErr_NewException ("ocfs2.error", PyExc_RuntimeError, NULL);

  if (ocfs2_error)
    {
      Py_INCREF (ocfs2_error);
      PyModule_AddObject (m, "error", ocfs2_error);
    }

  Py_INCREF (&Filesystem_Type);
  PyModule_AddObject (m, "Filesystem", (PyObject *) &Filesystem_Type);

  add_constants (m);

  if (PyErr_Occurred ())
    Py_FatalError ("can't initialize module ocfs2");
}
