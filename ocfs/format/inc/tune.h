/*
 * resize.h
 *
 * Function prototypes for related 'C' file.
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
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

#ifndef _RESIZE_H_
#define _RESIZE_H_

#include <signal.h>
#include <ctype.h>
#include <limits.h>

/* function prototypes */

int read_options(int argc, char **argv);

bool validate_options(char *progname);

int update_volume_header(int file, ocfs_vol_disk_hdr *volhdr,
			 __u32 sect_size, __u64 vol_size, bool *update);

int update_node_cfg(int file, ocfs_vol_disk_hdr *volhdr, __u64 cfg_hdr_off,
		    __u64 cfg_node_off, __u64 new_cfg_off,
		    ocfs_node_config_hdr *node_hdr,
		    ocfs_disk_node_config_info *node_info, __u32 sect_size,
		    bool *update);

void handle_signal(int sig);

int print_node_cfgs(int file, ocfs_vol_disk_hdr *volhdr, __u32 sect_size);

int process_new_volsize(int file, ocfs_vol_disk_hdr *volhdr, __u32 sect_size,
		       	__u64 vol_size, bool *update);

#endif /* _RESIZE_H_ */
