/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * encode_lockres.c
 *
 * Encodes a lockres name based on information passed
 *
 * Copyright (C) 2005 Oracle.  All rights reserved.
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
 * Authors: Mark Fasheh
 */


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>

/* Begin paste from kernel module */
enum ocfs2_lock_type {
	OCFS_TYPE_META = 0,
	OCFS_TYPE_DATA,
	OCFS_TYPE_SUPER,
	OCFS_NUM_LOCK_TYPES
};

/* lock ids are made up in the following manner:
 * name[0]     --> type
 * name[1-6]   --> 6 pad characters, reserved for now
 * name[7-22]  --> block number, expressed in hex as 16 chars
 * name[23-30] --> i_generation, expressed in hex 8 chars
 * name[31]    --> '\0' */
#define OCFS2_LOCK_ID_MAX_LEN  32
#define OCFS2_LOCK_ID_PAD "000000"

static char ocfs2_lock_type_char[OCFS_NUM_LOCK_TYPES] = {
	[OCFS_TYPE_META]	'M',
	[OCFS_TYPE_DATA] 	'D',
	[OCFS_TYPE_SUPER]       'S'
};
/* End paste from kernel module */

static void usage(char *program)
{
	printf("%s [M|D|S] [blkno] [generation]\n", program);
	printf("encodes a lockres name\n");
}

int main(int argc, char **argv)
{
	uint64_t blkno;
	uint32_t generation;
	unsigned long long tmp;
	char type;
	int i;

	if (argc < 4) {
		usage(argv[0]);
		return 0;
	}

	type = argv[1][0];
	blkno = atoll(argv[2]);
	tmp = atoll(argv[3]);
	generation = (uint32_t) tmp;

	for (i = 0; i < OCFS_NUM_LOCK_TYPES; i++)
		if (type == ocfs2_lock_type_char[i])
			break;

	if (i == OCFS_NUM_LOCK_TYPES) {
		fprintf(stderr, "Invalid lock type '%c'\n", type);
		return 1;
	}

	fprintf(stdout, "%c%s%016"PRIx64"%08x\n", type, OCFS2_LOCK_ID_PAD,
		blkno, generation);

	return 0;
}
