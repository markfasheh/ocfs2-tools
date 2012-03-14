/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * o2cb_abi.c
 *
 * Layout of configfs paths for O2CB cluster configuration.
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
 */

#ifndef _O2CB_ABI_H
#define _O2CB_ABI_H

/*
 * The latest place is /sys/kernel/config, but older O2CB put it
 * at /config.  So, libo2cb has to handle detection
 */
#define CONFIGFS_FORMAT_PATH "%s/config"

#define O2CB_FORMAT_CLUSTER_DIR		CONFIGFS_FORMAT_PATH "/cluster"
#define O2CB_FORMAT_CLUSTER		O2CB_FORMAT_CLUSTER_DIR "/%s"
#define O2CB_FORMAT_NODE_DIR		O2CB_FORMAT_CLUSTER "/node"
#define O2CB_FORMAT_NODE		O2CB_FORMAT_NODE_DIR "/%s"
#define O2CB_FORMAT_NODE_ATTR		O2CB_FORMAT_NODE "/%s"
#define O2CB_FORMAT_HEARTBEAT_DIR	O2CB_FORMAT_CLUSTER "/heartbeat"
#define O2CB_FORMAT_HEARTBEAT_REGION	O2CB_FORMAT_HEARTBEAT_DIR "/%s"
#define O2CB_FORMAT_HEARTBEAT_REGION_ATTR	O2CB_FORMAT_HEARTBEAT_REGION "/%s"
#define O2CB_FORMAT_HEARTBEAT_MODE	O2CB_FORMAT_HEARTBEAT_DIR "/mode"

#define O2CB_FORMAT_DEAD_THRESHOLD	O2CB_FORMAT_HEARTBEAT_DIR "/dead_threshold"
#define O2CB_FORMAT_IDLE_TIMEOUT	O2CB_FORMAT_CLUSTER "/idle_timeout_ms"
#define O2CB_FORMAT_KEEPALIVE_DELAY	O2CB_FORMAT_CLUSTER "/keepalive_delay_ms"
#define O2CB_FORMAT_RECONNECT_DELAY	O2CB_FORMAT_CLUSTER "/reconnect_delay_ms"

/*
 * Cluster info flags (ocfs2_cluster_info.ci_stackflags)
 */
#define OCFS2_CLUSTER_O2CB_GLOBAL_HEARTBEAT	(0x01)

#endif  /* _O2CB_ABI_H */
