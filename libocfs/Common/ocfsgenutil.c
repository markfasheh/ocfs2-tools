/*
 * ocfsgenutil.c
 *
 * Generic utilities
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
 * Authors: Neeraj Goyal, Suchit Kaura, Kurt Hackel, Sunil Mushran,
 *          Manish Singh, Wim Coekaerts
 */

#ifdef __KERNEL__
#include <ocfs.h>
#else
#include <libocfs.h>
#endif


/* Tracing */
#define OCFS_DEBUG_CONTEXT     OCFS_DEBUG_CONTEXT_UTIL

/*
 * ocfs_compare_qstr()
 *
 */
int ocfs_compare_qstr (struct qstr * s1, struct qstr * s2)
{
        int s = strncmp ((const char *) s1->name, (const char *) s2->name,
                        s1->len < s2->len ? s1->len : s2->len);

        if (s != 0)
                return s;
        if (s1->len > s2->len)
                return 1;
        else if (s1->len < s2->len)
                return -1;
        else
                return s;
}				/* ocfs_compare_qstr */
