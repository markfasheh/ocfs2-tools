/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dump_net_stats.c
 *
 * Interface with the kernel and dump current o2net locking state
 *
 * Copyright (C) 2011 Oracle.  All rights reserved.
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
 */

#define _XOPEN_SOURCE 600  /* Triggers XOPEN2K in features.h */
#define _LARGEFILE64_SOURCE

#include "main.h"
#include "ocfs2/byteorder.h"
#include "ocfs2_internals.h"

static char *cmd = "net_stats";

static void show_net_stats(FILE *out, struct net_stats *prev,
			     struct net_stats *curr, int num_entries,
			     int interval, unsigned long proto)
{
	int i;
	struct net_stats *c, *p;
	double total_send_time;
	double send_count, aqry_time, send_time, wait_time;
	double recv_count, proc_time;

	fprintf(out, "%-5s  %-s %-s %-s   %-s %-s %-s\n", " ",
		"-------", "msg / sec", "-------",
		"---------------------------", "usecs / msg",
		"---------------------------");

	fprintf(out, "%-5s  %-12s %-12s  %-12s   %-12s  %-13s %-12s %-12s\n",
		"Node#", "send q", "recv q", "(acquiry", "xmit",
		"wait        )", "send", "process");

	for (i = 0; i < num_entries; ++i) {
		c = &(curr[i]);
		p = &(prev[i]);

		if (!c->ns_valid)
			continue;

		c->ns_send_count *= 10;
		c->ns_send_count /= 10;

		send_count = c->ns_send_count;
		aqry_time  = c->ns_aqry_time;
		send_time  = c->ns_send_time;
		wait_time  = c->ns_wait_time;
		recv_count = c->ns_recv_count;
		proc_time  = c->ns_proc_time;

		if (p->ns_valid) {
			send_count -= p->ns_send_count;
			if (send_count) {
				aqry_time -= p->ns_aqry_time;
				send_time -= p->ns_send_time;
				wait_time -= p->ns_wait_time;
			}
			recv_count -= p->ns_recv_count;
			if (recv_count)
				proc_time -= p->ns_proc_time;
		}

		/* Times converted from nsecs to usecs */
		if (send_count) {
			aqry_time /= (send_count * 1000);
			send_time /= (send_count * 1000);
			wait_time /= (send_count * 1000);
		} else {
			aqry_time = 0;
			send_time = 0;
			wait_time = 0;
		}

		if (recv_count)
			proc_time /= (recv_count * 1000);
		else
			proc_time = 0;

		if (p->ns_valid && interval) {
			if (send_count)
				send_count /= interval;

			if (recv_count)
				recv_count /= interval;
		}

		total_send_time = aqry_time + send_time + wait_time;


		fprintf(out, "%-5d  %-12lu %-12lu   %-12.3f  %-12.3f  "
			"%-12.3f  %-12.3f %-12.3f\n",
			i, (unsigned long)send_count, (unsigned long)recv_count,
			aqry_time, send_time, wait_time, total_send_time,
			proc_time);
	}
	fprintf(out, "\n\n");
}

#define CURRENT_O2NET_STATS_PROTO	1
#define MAX_O2NET_STATS_STR_LEN		1024

static int read_net_stats(const char *debugfs_path, char *path,
			  struct net_stats *stats, int num_entries,
			  unsigned long *proto)
{
	FILE *file = NULL;
	char rec1[MAX_O2NET_STATS_STR_LEN], rec2[MAX_O2NET_STATS_STR_LEN];
	unsigned long node_num;
	errcode_t ret;

	memset(stats, 0, sizeof(struct net_stats) * num_entries);

	if (!path) {
		ret = open_debugfs_file(debugfs_path, "o2net", NULL, "stats",
					&file);
		if (ret) {
			com_err(cmd, ret, "; could not open %s/o2net/stats",
				debugfs_path);
			goto bail;
		}
	} else {
		file = fopen(path, "r");
		if (!file) {
			ret = errno;
			com_err(cmd, ret, "\"%s\"", path);
			goto bail;
		}
	}

	while (fgets(rec1, sizeof(rec1), file)) {

		/* read protocol version */
		ret = sscanf(rec1, "%lu,%s\n", proto, rec2);
		if (ret != 2) {
			ret = OCFS2_ET_INTERNAL_FAILURE;
			com_err(cmd, ret, "Error reading protocol version\n");
			goto bail;
		}

		if (*proto > CURRENT_O2NET_STATS_PROTO) {
			ret = OCFS2_ET_INTERNAL_FAILURE;
			com_err(cmd, ret, "o2net stats proto %lu found, but %u "
				"is the highest I understand.\n", *proto,
				CURRENT_O2NET_STATS_PROTO);
			goto bail;
		}

		/* Protocol version 1 - begin */

		ret = sscanf(rec2, "%lu,%s", &node_num, rec1);
		if (ret != 2) {
			ret = OCFS2_ET_INTERNAL_FAILURE;
			com_err(cmd, ret, "Error reading node#\n");
			goto bail;
		}

		if (node_num > num_entries - 1) {
			ret = OCFS2_ET_INTERNAL_FAILURE;
			com_err(cmd, ret, "Invalid node# %lu\n", node_num);
			goto bail;
		}

		ret = sscanf(rec1, "%lu,%lld,%lld,%lld,%lu,%lld",
			     &stats[node_num].ns_send_count,
			     &stats[node_num].ns_aqry_time,
			     &stats[node_num].ns_send_time,
			     &stats[node_num].ns_wait_time,
			     &stats[node_num].ns_recv_count,
			     &stats[node_num].ns_proc_time);
		if (ret != 6) {
			ret = OCFS2_ET_INTERNAL_FAILURE;
			com_err(cmd, ret, "Error reading o2net stats\n");
			goto bail;
		}

		stats[node_num].ns_valid = 1;

		/* Protocol version 1 - end */
	}

	ret = 0;

bail:
	if (file)
		fclose(file);

	return ret;
}

void dump_net_stats(FILE *out, char *path, int interval, int count)
{
	errcode_t ret;
	char debugfs_path[PATH_MAX];
	struct net_stats buf1[O2NM_MAX_NODES], buf2[O2NM_MAX_NODES];
	struct net_stats *curr = buf1, *prev = buf2;
	unsigned long proto;

	ret = get_debugfs_path(debugfs_path, sizeof(debugfs_path));
	if (ret) {
		com_err(cmd, ret, "Could not locate debugfs file system. "
			"Perhaps it is not mounted?\n");
		return;
	}

	count = (count == 0) ? -1 : count;

	memset(prev, 0 , sizeof(buf1));

	do {
		ret = read_net_stats(debugfs_path, path, curr, O2NM_MAX_NODES,
				     &proto);
		if (ret)
			break;

		show_net_stats(out, prev, curr, O2NM_MAX_NODES, interval, proto);

		if (!interval)
			break;

		if (count > 0 && !--count)
			break;

		dbfs_swap(prev, curr);

		sleep(interval);

	} while(1);
}
