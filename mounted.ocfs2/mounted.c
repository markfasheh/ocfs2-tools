/*
 * mounted.c
 *
 * ocfs mount detect utility
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
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
 * Authors: Sunil Mushran
 */

#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE /* Because libc really doesn't want us using O_DIRECT? */

#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fd.h>
#include <string.h>
#include <sys/stat.h>

#define  OCFS2_FLAT_INCLUDES	1
#include <ocfs2.h>
#include <ocfs2_fs.h>
#include <ocfs2_disk_dlm.h>
#include <ocfs1_fs_compat.h>

void ocfs2_print_live_nodes(char **node_names, uint16_t num_nodes);

/*
 * main()
 *
 */
int main(int argc, char **argv)
{
	errcode_t ret;
	int mount_flags = 0;
	char *node_names[OCFS2_NODE_MAP_MAX_NODES];
	int i;
	ocfs2_filesys *fs = NULL;
	uint8_t vol_label[64];
	uint8_t vol_uuid[16];
	uint16_t num_nodes = OCFS2_NODE_MAP_MAX_NODES;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s device\n", argv[0]);
		goto bail;
	}

	initialize_ocfs_error_table();

	memset(node_names, 0, sizeof(node_names));
	memset(vol_label, 0, sizeof(vol_label));
	memset(vol_uuid, 0, sizeof(vol_uuid));

	/* open	fs */
	ret = ocfs2_open(argv[1], O_DIRECT | OCFS2_FLAG_RO, 0, 0, &fs);
	if (ret) {
		com_err(argv[0], ret, "while opening \"%s\"", argv[1]);
		goto bail;
	}

	num_nodes = OCFS2_RAW_SB(fs->fs_super)->s_max_nodes;
	memcpy(vol_label, OCFS2_RAW_SB(fs->fs_super)->s_label, sizeof(vol_label));
	memcpy(vol_uuid, OCFS2_RAW_SB(fs->fs_super)->s_uuid, sizeof(vol_uuid));

	ret = ocfs2_check_heartbeat(argv[1], &mount_flags, node_names);
	if (ret) {
		com_err(argv[0], ret, "while detecting heartbeat");
		goto bail;
	}

	printf("Label : %s\n", vol_label);
	printf("Id    : ");
	for (i = 0; i < 16; i++)
		printf("%02X", vol_uuid[i]);
	printf("\n");

	if (mount_flags & (OCFS2_MF_MOUNTED | OCFS2_MF_MOUNTED_CLUSTER)) {
		printf("Nodes :");
		ocfs2_print_live_nodes(node_names, num_nodes);
	} else {
		printf("Nodes : Not mounted\n");
		goto bail;
	}

bail:
	if (fs)
		ocfs2_close(fs);

	for (i = 0; i < num_nodes; ++i)
		if (node_names[i])
			ocfs2_free (&node_names[i]);

	return 0;
}

/*
 * ocfs2_print_live_nodes()
 *
 */
void ocfs2_print_live_nodes(char **node_names, uint16_t num_nodes)
{
	int i;
	char comma = '\0';

	for (i = 0; i < num_nodes; ++i) {
		if (node_names[i]) {
			printf("%c %s", comma, node_names[i]);
			comma = ',';
		}
	}
	printf("\n");
}
