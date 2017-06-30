/*
 * dump_net_stats.h
 *
 * Function prototypes, macros, etc. for related 'C' files
 *
 * Copyright (C) 2011 Oracle.  All rights reserved.
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301 USA.
 *
 */

#ifndef _DUMP_NET_STATS_H_
#define _DUMP_NET_STATS_H_

struct net_stats {
	int ns_valid;
	unsigned long ns_send_count;
	long long ns_aqry_time;
	long long ns_send_time;
	long long ns_wait_time;
	unsigned long ns_recv_count;
	long long ns_proc_time;
};

void dump_net_stats(FILE *out, char *path, int interval, int count);

#endif		/* _DUMP_NET_STATS_H_ */
