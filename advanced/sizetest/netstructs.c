/*
 * netstructs.c
 *
 * Prints sizes and offsets of structures and its elements.
 * Useful to ensure cross platform compatibility.
 *
 * Copyright (C) 2003 Oracle Corporation.  All rights reserved.
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
 * Authors: Kurt Hackel, Sunil Mushran, Manish Singh, Wim Coekaerts
 */

#include "sizetest.h"

bool show_all = false;

void __wake_up(wait_queue_head_t *q, unsigned int mode, int nr)
{
    return;
}

void usage(void)
{
	printf("usage: netstructs [all]\n");
	return ;
}


static void print_ocfs_dlm_msg_hdr()
{
	ocfs_dlm_msg_hdr s;

	SHOW_SIZEOF(ocfs_dlm_msg_hdr, s);
	if (show_all) {
		SHOW_OFFSET(lock_id, s);
		SHOW_OFFSET(flags, s);
		SHOW_OFFSET(lock_seq_num, s);
		SHOW_OFFSET(open_handle, s);
		printf("\n");
	}
}


static void print_ocfs_dlm_reply_master()
{
	ocfs_dlm_reply_master s;

	SHOW_SIZEOF(ocfs_dlm_reply_master, s);
	if (show_all) {
		SHOW_OFFSET(h, s);
		SHOW_OFFSET(status, s);
		printf("\n");
	}
}


static void print_ocfs_dlm_disk_vote_reply()
{
	ocfs_dlm_disk_vote_reply s;

	SHOW_SIZEOF(ocfs_dlm_disk_vote_reply, s);
	if (show_all) {
		SHOW_OFFSET(h, s);
		SHOW_OFFSET(status, s);
		printf("\n");
	}
}


static void print_ocfs_dlm_msg()
{
	ocfs_dlm_msg s;

	SHOW_SIZEOF(ocfs_dlm_msg, s);
	if (show_all) {
		SHOW_OFFSET(magic, s);
		SHOW_OFFSET(msg_len, s);
		SHOW_OFFSET(vol_id, s);
		SHOW_OFFSET(src_node, s);
		SHOW_OFFSET(dst_node, s);
		SHOW_OFFSET(msg_type, s);
		SHOW_OFFSET(check_sum, s);
		SHOW_OFFSET(msg_buf, s);
		printf("\n");
	}
}


int main(int argc, char **argv)
{
	if (argc > 1) {
		if (!strncasecmp(*++argv, "all", 3))
			show_all = true;
		else {
			usage();
			exit (1);
		}
	}

	print_ocfs_dlm_msg_hdr();
	print_ocfs_dlm_reply_master();
	print_ocfs_dlm_disk_vote_reply();
	print_ocfs_dlm_msg();

	return 0;
}				/* main */
