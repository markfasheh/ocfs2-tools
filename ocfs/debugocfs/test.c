/*
 * test.c
 *
 * 
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
 * Author: Kurt Hackel
 *
 * gcc -o test test.c `pkg-config --cflags --libs glib` -ldebugocfs -locfsusr -L. -L../../ocfs2/ -g
 */

#include <glib.h>
#include <stdio.h>
#include "libdebugocfs.h"

int main(int argc, char **argv)
{
    int ret, i;
    GArray *arr;

    if (argc < 3)
    {
	fprintf(stderr, "usage: test /dev/path /dir/to/read/\n");
	exit(1);
    }

    ret = libocfs_readdir(argv[1], argv[2], TRUE, &arr);
    printf("ret=%d arraysize=%d\n", ret, arr->len);
    if (ret == 0)
    {
	for (i = 0; i < arr->len; i++)
	{
	    libocfs_stat *x;

	    x = (libocfs_stat *) (arr->data + (sizeof(libocfs_stat) * i));
	    printf("name=%s size=%llu\n", x->name, x->size);
	}
    }
    g_array_free(arr, TRUE);

    ret = libocfs_get_node_map(argv[1], &arr);
    printf("ret=%d arraysize=%d\n", ret, arr->len);

    if (ret == 0)
    {
	for (i = 0; i < arr->len; i++)
	{
	    libocfs_node *x;

	    x = (libocfs_node *) (arr->data + (sizeof(libocfs_node) * i));
	    printf("name='%s' ip=%s slot=%d guid='%s'\n", x->name,
		   x->addr, x->slot, x->guid);
	}
    }
    g_array_free(arr, TRUE);



}

#if 0
33 typedef struct _libocfs_node 34
{
    35 char name[MAX_NODE_NAME_LENGTH + 1];
    36 int num_addrs;
    37 char **addrs;		// array of char[16]
    38 int slot;
  39}
libocfs_node;

#endif
