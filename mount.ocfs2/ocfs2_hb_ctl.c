/*
 * ocfs2_hb_ctl.c  Utility to start / stop heartbeat on demand
 *
 * Copyright (C) 2005 Oracle.  All rights reserved.
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
 */

#include "ocfs2_hb_ctl.h"

#include "o2cb.h"
#include "mount_hb.h"

char *progname = "ocfs2_hb_ctl";

enum hb_ctl_action {
	HB_ACTION_UKNOWN,
	HB_ACTION_USAGE,
	HB_ACTION_START,
	HB_ACTION_STOP,
};

struct hb_ctl_options {
	enum hb_ctl_action action;
	char *dev_str;
	char *uuid_str;
};

static void read_options(int argc, char **argv, struct hb_ctl_options *hbo)
{
	int c;

	if (argc < 4)
		return;

	while(1) {
		c = getopt(argc, argv, "SKd:u:h");
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			hbo->action = HB_ACTION_USAGE;
			break;

		case 'K':
			hbo->action = HB_ACTION_STOP;
			break;

		case 'S':
			hbo->action = HB_ACTION_START;
			break;

		case 'd':
			if (optarg)
				hbo->dev_str = strdup(optarg);
			break;

		case 'u':
			if (optarg)
				hbo->uuid_str = strdup(optarg);
			break;

		default:
			break;
		}
	}
}

static int process_options(struct hb_ctl_options *hbo)
{
	int ret = 0;

	switch (hbo->action) {
	case HB_ACTION_START:
		/* We can't start by uuid yet. */
		if (hbo->uuid_str || !hbo->dev_str)
			ret = -EINVAL;
		break;

	case HB_ACTION_STOP:
		/* For stop must specify exactly one of uuid or device. */
		if ((hbo->uuid_str && hbo->dev_str) ||
		    (!hbo->uuid_str && !hbo->dev_str))
			ret = -EINVAL;
		break;

	case HB_ACTION_UKNOWN:
		ret = -EINVAL;
		break;

	case HB_ACTION_USAGE:
	default:
		break;
	}

	return ret;
}

static void print_usage(int err)
{
	FILE *output = err ? stderr : stdout;

	fprintf(output, "Usage: %s -S -d <device>\n", progname);
	fprintf(output, "       %s -K -d <device>\n", progname);
	fprintf(output, "       %s -K -u <uuid>\n", progname);
	fprintf(output, "       %s -h\n", progname);
}

int main(int argc, char **argv)
{
	errcode_t err = 0;
	int ret = 0;
	struct hb_ctl_options hbo = { HB_ACTION_UKNOWN, NULL, NULL };
	char hbuuid[33];

	initialize_ocfs_error_table();
	initialize_o2dl_error_table();

	read_options(argc, argv, &hbo);

	ret = process_options(&hbo);
	if (ret) {
		print_usage(1);
		goto bail;
	}

	if (!hbo.uuid_str) {
		err = get_uuid(hbo.dev_str, hbuuid);
		if (err) {
			com_err(progname, err, "while reading uuid");
			ret = -EINVAL;
			goto bail;
		}

		hbo.uuid_str = hbuuid;
	}

	switch(hbo.action) {
	case HB_ACTION_USAGE:
		ret = 0;
		print_usage(0);
		break;

	case HB_ACTION_START:
		err = start_heartbeat(hbo.uuid_str, hbo.dev_str);
		if (err) {
			com_err(progname, err, "while starting heartbeat");
			ret = -EINVAL;
		}
		break;

	case HB_ACTION_STOP:
		err = stop_heartbeat(hbo.uuid_str);
		if (err) {
			com_err(progname, err, "while stopping heartbeat");
			ret = -EINVAL;
		}
		break;

	default:
		abort();
	}

bail:
	return ret;
}
