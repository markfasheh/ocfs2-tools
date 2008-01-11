/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * compute_groups.c
 *
 * Lists the offsets of the group descriptors for all
 * block/cluster size combinations for a devuce of a given size.
 *
 * Copyright (C) 2006 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License, version 2,  as published by the Free Software Foundation.
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
 * Authors: Tao Ma
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>

#include "ocfs2/ocfs2.h"

#define TWO_TERA	2199023255552LL

static void stringyfy(uint32_t val, char x, char *str)
{
	if (val == 512)
		sprintf(str, "%c=512", x);
	else
		sprintf(str, "%c=%uK", x, val/1024);
}

int main (int argc, char **argv)
{
	uint32_t cs;  /* cluster bits */
	uint32_t bs;  /* block bits */
	uint32_t cpg; /* cluster per group */
	uint64_t clsoff;
	uint64_t bytoff;
	uint64_t max_size = TWO_TERA;
	char blkstr[20];
	char clsstr[20];

	if (argc > 1)
		max_size = strtoull(argv[1], NULL, 0);

	printf("Listing all group descriptor offsets for a volume of "
	       "size %"PRIu64" bytes\n", max_size);

	for (bs = 9; bs < 13; bs++) {
		cpg = ocfs2_group_bitmap_size(1 << bs) * 8;
		stringyfy((1 << bs), 'b', blkstr);
		for (cs = 12; cs < 21; cs++) {
			for (bytoff = 0, clsoff = 0; bytoff < max_size; ) {
				stringyfy((1 << cs), 'c', clsstr);
				printf("%-15llu  %-7s  %-7s\n", bytoff, clsstr,
				       blkstr);
				clsoff += cpg;
				bytoff = clsoff * (1 << cs);
			}
		}
	}

	return 0;
}
