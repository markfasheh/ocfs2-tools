/*
 * mounted.c
 *
 * ocfs mount detect utility
 *
 * Copyright (C) 2003, 2004 Oracle.  All rights reserved.
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

#include <format.h>
#include <signal.h>

ocfs_options opts = {
	.device = "",
	.block_size = 0,
	.clear_data_blocks = false,
	.force_op = false,
	.gid = 0,
	.volume_label = "",
	.mount_point = "",
	.query_only = false,
	.perms = 0755,
	.quiet = false,
	.uid = 0,
	.print_progress = false,
	.slot_num = OCFS_INVALID_NODE_NUM,
	.device_size = 0
};

char *usage_string = "usage: %s <device>\n";
int file = 0;
int rawminor = 0;
char rawdev[FILE_NAME_SIZE];

#undef PRINT_VERBOSE
#define PRINT_VERBOSE(f, a...)	do { if (true) printf(f, ##a); \
				} while (0)

int get_vol_label(int file, ocfs_vol_label *vollabel, __u32 sect_size);
void handle_signal(int sig);
int read_options(int argc, char **argv);

/*
 * main()
 *
 */
int main(int argc, char **argv)
{
	ocfs_vol_disk_hdr *volhdr = NULL;
	ocfs_vol_label *vollbl = NULL;
	__u32 sect_size = OCFS_SECTOR_SIZE;
	__u64 vol_size = 0;
	__u32 i;
	bool ocfs_vol = false;
	char *node_names[OCFS_MAXIMUM_NODES];
	__u32 nodemap = 0;

	for (i = 0; i < OCFS_MAXIMUM_NODES; ++i)
		node_names[i] = NULL;

#define INSTALL_SIGNAL(sig) 						\
	do { 								\
		if (signal(sig, handle_signal) == SIG_ERR) {		\
			fprintf(stderr, "Could not set " #sig "\n");	\
			goto bail;					\
		}							\
	} while(0)

	INSTALL_SIGNAL(SIGTERM);
	INSTALL_SIGNAL(SIGINT);

	init_raw_cleanup_message();

	if (!read_options(argc, argv))
		goto bail;

	if (!(file = OpenDisk(opts.device)))
		goto bail;

	if (!(GetDiskGeometry(file, &vol_size, &sect_size)))
		goto bail;

	volhdr = MemAlloc(sect_size);
	vollbl = MemAlloc(sect_size);
	if (!volhdr || !vollbl)
		goto bail;
	else {
		memset(volhdr, 0, sect_size);
		memset(vollbl, 0, sect_size);
	}

	/* close and open after binding to raw */
	safeclose(file);

	if (bind_raw(opts.device, &rawminor, rawdev, sizeof(rawdev)))
		goto bail;
	
	if (!(file = OpenDisk(rawdev)))
		goto bail;

	if (!is_ocfs_volume(file, volhdr, &ocfs_vol, sect_size))
		goto bail;

	if (!ocfs_vol) {
		fprintf(stderr, "Error: %s is not an ocfs volume.\n", opts.device);
		goto bail;
	}

	printf("Device: %s\n", opts.device);

	if (get_vol_label(file, vollbl, sect_size)) {
		printf("Label : %s\n", vollbl->label);
		printf("Id    : ");
		for (i = 0; i < vollbl->vol_id_len; ++i)
			printf("%02X", vollbl->vol_id[i]);
		printf("\n");
	}

	if (!check_heart_beat(&file, rawdev, volhdr, &nodemap, sect_size)) {
		fprintf(stderr, "Error detecting heartbeat on volume.\n");
		goto bail;
	}

	if (!nodemap) {
		printf("Nodes : Not mounted\n");
		goto bail;
	}

	get_node_names(file, volhdr, node_names, sect_size);

	printf("Nodes :");
	print_node_names(node_names, nodemap);

bail:
	for (i = 0; i < OCFS_MAXIMUM_NODES; ++i)
		safefree(node_names[i]);

	safefree(volhdr);
	safefree(vollbl);
	safeclose(file);
	unbind_raw(rawminor);
	return 0;
}				/* main */


/*
 * get_vol_label()
 *
 */
int get_vol_label(int file, ocfs_vol_label *vollbl, __u32 sect_size)
{
	int ret = 0;

	if (!SetSeek(file, sect_size))
		goto bail;

	if (!(Read(file, sect_size, (char *)vollbl)))
		goto bail;

	ret = 1;
bail:
	return ret;
}				/* get_vol_label */


/*
 * handle_signal()
 */
void handle_signal(int sig)
{
	switch (sig) {
	case SIGTERM:
	case SIGINT:
		fprintf(stderr, "\nInterrupted.\n");
		safeclose(file);
		unbind_raw(rawminor);
		exit(1);
		break;
	}
}				/* handle_signal */


/*
 * read_options()
 *
 * mounted.ocfs <device>
 *
 */
int read_options(int argc, char **argv)
{
	int ret = 0;

	if (argc < 2) {
		version(argv[0]);
		usage(argv[0]);
		goto bail;
	}

	strncpy(opts.device, argv[1], FILE_NAME_SIZE);
	if (strlen(opts.device))
		ret = 1;
	else {
		version(argv[0]);
		usage(argv[0]);
	}

      bail:
	return ret;
}				/* read_options */

