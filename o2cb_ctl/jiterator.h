/*
 * jiterator.h
 *
 * Prototypes for JIterator
 *
 * Copyright (C) 2002 Joel Becker <jlbec@evilplan.org>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License version 2 as published by the Free Software Foundation.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __JITERATOR_H
#define __JITERATOR_H


/* Typedefs */
typedef struct _JIterator		JIterator;
typedef gpointer (*JIteratorFunc)	(gpointer context);


/* Functions */
JIterator* j_iterator_new(gpointer context,
                          JIteratorFunc has_more_func,
                          JIteratorFunc get_next_func,
                          GDestroyNotify notify_func);
JIterator* j_iterator_new_from_list(GList *init_list);
gboolean j_iterator_has_more(JIterator *iterator);
gpointer j_iterator_get_next(JIterator *iterator);
void j_iterator_free(JIterator *iterator);

#endif /* __JITERATOR_H */

