/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * decode_lockres.c
 *
 * Tells you all the information about an ocfs2 lockres available
 * based on it's name. Very useful for debugging dlm issues.
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
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

static char * ocfs2_lock_type_string[OCFS_NUM_LOCK_TYPES] = {
	[OCFS_TYPE_META]	"Metadata",
	[OCFS_TYPE_DATA] 	"Data",
	[OCFS_TYPE_SUPER]       "Superblock"
};

static void usage(char *program)
{
	printf("%s LOCKRES\n", program);
	printf("prints out information based on the lockres name\n");
}

static const char *get_lock_type_string(char c)
{
	enum ocfs2_lock_type t;

	if (c == ocfs2_lock_type_char[OCFS_TYPE_META])
		t = OCFS_TYPE_META;
	else if (c == ocfs2_lock_type_char[OCFS_TYPE_DATA])
		t = OCFS_TYPE_DATA;
	else if (c == ocfs2_lock_type_char[OCFS_TYPE_SUPER])
		t = OCFS_TYPE_SUPER;
	else
		return NULL;

	return ocfs2_lock_type_string[t];
}

static int decode_one_lockres(const char *lockres)
{
	const char *type;
	int i;
	unsigned long long blkno;
	unsigned int generation;
	char blkstr[17];

	if ((strlen(lockres) + 1) != OCFS2_LOCK_ID_MAX_LEN) {
		fprintf(stderr, "Invalid lockres id \"%s\"\n", lockres);
		return 1;
	}

	type = get_lock_type_string(lockres[0]);
	if (!type) {
		fprintf(stderr, "Invalid lockres type, '%c'\n", lockres[0]);
		return 1;
	}

	printf("Lockres:    %s\n", lockres);
	printf("Type:       %s\n", type);

	i = 1 + strlen(OCFS2_LOCK_ID_PAD);
	memset(blkstr, 0, 17);
	memcpy(blkstr, &lockres[i], 16);
	blkno = strtoull(blkstr, NULL, 16);
	printf("Block:      %llu\n", blkno);

	i+= 16;
	generation = strtoul(&lockres[i], NULL, 16);
	printf("Generation: 0x%08x\n", generation);

	printf("\n");

	return 0;
}

int main(int argc, char **argv)
{
	int i, status = 0;

	if (argc < 2) {
		usage(argv[0]);
		return 0;
	}

	for(i = 1; i < argc; i++) {
		status = decode_one_lockres(argv[i]);
		if (status)
			break;
	}

	return status;
}
