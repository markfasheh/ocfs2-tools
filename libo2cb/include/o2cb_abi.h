/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * o2cb_abi.c
 *
 * Kernel<->User ABI for modifying cluster configuration.
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
 */

#ifndef _O2CB_ABI_H
#define _O2CB_ABI_H

/* Hardcoded paths to the cluster virtual files */
#define O2CB_CLUSTER_FILE 	"/proc/cluster/nm/.cluster"
#define O2CB_GROUP_FILE		"/proc/cluster/nm/.group"
#define O2CB_NODE_FILE		"/proc/cluster/nm/.node"
#define O2CB_NETWORKING_FILE	"/proc/cluster/net"

#endif  /* _O2CB_ABI_H */
