/* 
 * jiterator.c
 *
 * Code for opaque iterators.
 *
 * Copyright (C) 2001 Oracle Corporation, Joel Becker
 * <joel.becker@oracle.com>
 * All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 * 
 * You should have recieved a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */
 
/*
 * MT safe
 */

#include <sys/types.h>
#include <glib.h>

#include "jiterator.h"


/* Structures
 */
struct _JIterator
{
  gpointer context;
  JIteratorFunc      has_more_func;
  JIteratorFunc      get_next_func;
  GDestroyNotify        notify_func;
};


static gpointer j_iterator_list_has_more       (gpointer   context);
static gpointer j_iterator_list_get_next       (gpointer   context);
static void     j_iterator_list_destroy_notify (gpointer   context);



/* Enumerations
 */
JIterator*
j_iterator_new (gpointer               context,
            JIteratorFunc       has_more_func,
            JIteratorFunc       get_next_func,
            GDestroyNotify         notify_func)
{
  JIterator *iterator;

  iterator = g_new (JIterator, 1);
  iterator->context = context;
  iterator->has_more_func = has_more_func;
  iterator->get_next_func = get_next_func;
  iterator->notify_func = notify_func;

  return iterator;
}  /* j_iterator_new() */

JIterator*
j_iterator_new_from_list (GList *init_list)
{
  JIterator *iterator;
  GList *list_copy, *header;

  /* The list is copied here, the caller is responsible for the
   * data items.  On _free(), the list is removed and the data items
   * left alone.  If the caller wants different semantics, the
   * caller can specify their own functions with _new() */
  list_copy = g_list_copy(init_list);

  /* This is a header element to refer to the list */
  header = g_list_prepend(NULL, (gpointer) list_copy);

  iterator = j_iterator_new ((gpointer) header,
                             j_iterator_list_has_more,
                             j_iterator_list_get_next,
                             j_iterator_list_destroy_notify);

  return(iterator);
}  /* j_iterator_new_from_list() */

gboolean
j_iterator_has_more (JIterator *iterator)
{
  gpointer result;

  g_return_val_if_fail(iterator != NULL, FALSE);

  result = (*iterator->has_more_func) (iterator->context);

  return (gboolean) GPOINTER_TO_INT(result);
}  /* j_iterator_has_more() */

gpointer
j_iterator_get_next (JIterator *iterator)
{
  gpointer result;

  g_return_val_if_fail (iterator != NULL, NULL);

  result = (*iterator->get_next_func) (iterator->context);

  return result;
}  /* j_iterator_get_next() */

void
j_iterator_free (JIterator *iterator)
{
  (*iterator->notify_func) (iterator->context);

  g_free(iterator);
}  /* j_iterator_free() */

static gpointer
j_iterator_list_has_more (gpointer context)
{
  GList *header, *elem;
  gboolean result;

  g_return_val_if_fail(context != NULL, GINT_TO_POINTER(FALSE));

  header = (GList *) context;
  elem = (GList *) header->data;

  result = (elem != NULL);

  return (gpointer) GINT_TO_POINTER(result);
}  /* j_iterator_list_has_more() */

static gpointer
j_iterator_list_get_next (gpointer context)
{
  GList *header, *elem;
  gpointer result;

  g_return_val_if_fail(context != NULL, NULL);

  header = (GList *) context;
  elem = (GList *) header->data;

  /* User should have called has_more() */
  g_return_val_if_fail(elem != NULL, NULL);

  result = elem->data;

  header->data = (gpointer) g_list_next(elem);

  g_list_free_1(elem);

  return result;
}  /* j_iterator_list_get_next() */

static void
j_iterator_list_destroy_notify (gpointer context)
{
  GList *header, *elem;

  g_return_if_fail (context != NULL);

  header = (GList *) context;
  elem = (GList *) header->data;

  g_list_free(elem);  /* NULL if at end of list */

  g_list_free(header);
}  /* j_iterator_list_destroy_notify() */
