/*
 * tune.c
 *
 * ocfs tune utility
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

#include <format.h>
#include <tune.h>

ocfs_options opts = {
	.device = "",
	.block_size = 0,
	.clear_data_blocks = false,
	.force_op = false,
	.gid = -1,
	.volume_label = "",
	.mount_point = "",
	.query_only = false,
	.perms = -1,
	.quiet = false,
	.uid = -1,
	.print_progress = false,
	.slot_num = OCFS_INVALID_NODE_NUM,
	.device_size = 0,
	.list_nodes = false,
	.convert = -1
};

bool ignore_signal = false;
int file = 0;
int rawminor = 0;
char rawdev[FILE_NAME_SIZE];

char *usage_string =
"usage: %s [-F] [-g gid] [-h] [-l] [-n] [-N nodenum] [-p permissions] "
"[-q] [-S size] [-u uid] [-V] device\n\n"
"	-F Force resize existing OCFS volume\n"
"	-g Group ID for the root directory\n"
"	-h Help\n"
"	-l List all the node config slots\n"
"	-n Query only\n"
"	-N Node config slot be to be cleared\n"
"	-p Permissions for the root directory\n"
"	-q Quiet execution\n"
"	-S Volume size, e.g., 50G (M for mega, G for giga, T for tera)\n"
"	-u User ID for the root directory\n"
"	-c Convert filesystem versions\n"
"	-V Print version and exit\n";

/*
 * main()
 *
 */
int main(int argc, char **argv)
{
	ocfs_vol_disk_hdr *volhdr = NULL;
	ocfs_node_config_hdr *node_hdr = NULL;
	ocfs_disk_node_config_info *node_info = NULL;
	__u32 sect_size = OCFS_SECTOR_SIZE;
	__u64 vol_size = 0;
	__u64 offset;
	bool ocfs_vol = false;
	char proceed;
	bool update = false;
	char *node_names[OCFS_MAXIMUM_NODES];
	__u32 nodemap = 0;
	int i;
	__u64 cfg_hdr_off = 0;
	__u64 cfg_node_off = 0;
	__u64 new_cfg_off = 0;

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

	/* Read the options */
	if (!read_options(argc, argv))
		goto bail;

	/* Validate the options */
	if (!validate_options(argv[0]))
		goto bail;

	/* Open the disk */
	if (!(file = OpenDisk(opts.device)))
		goto bail;

	/* Allocate mem */
	volhdr = MemAlloc(OCFS_SECTOR_SIZE);
	node_hdr = MemAlloc(OCFS_SECTOR_SIZE);
	node_info = MemAlloc(OCFS_SECTOR_SIZE);
	if (!volhdr || !node_hdr || !node_info)
		goto bail;

	/* Is this an existing ocfs volume */
	if (!is_ocfs_volume(file, volhdr, &ocfs_vol, sect_size))
		goto bail;


	/* Abort if not an ocfs volume */
	if (!ocfs_vol) {
		fprintf(stderr, "%s is not an ocfs volume.\nAborting.\n",
			opts.device);
		goto bail;
	}

	if (opts.list_nodes) {
		print_node_cfgs(file, volhdr, sect_size);
		goto bail;
	}

	/* Get the partition Information */
	if (!(GetDiskGeometry(file, &vol_size, &sect_size)))
		goto bail;

	if (opts.device_size) {
		if (!(validate_volume_size(opts.device_size, vol_size)))
			goto bail;
		vol_size = opts.device_size;
	}

	/* close and open after binding to raw */
	safeclose(file);

	/* bind device to raw */
	if (bind_raw(opts.device, &rawminor, rawdev, sizeof(rawdev)))
		goto bail;

	/* Open the disk */
	if (!(file = OpenDisk(rawdev)))
		goto bail;

	/* read volume header */
	if (!read_sectors(file, 0, 1, sect_size, (void *)volhdr))
		goto bail;

	/* Update node cfgs */
	if (IS_VALID_NODE_NUM(opts.slot_num)) {
		cfg_hdr_off = volhdr->node_cfg_off;
		cfg_node_off = volhdr->node_cfg_off + (2 + opts.slot_num) * sect_size;
		new_cfg_off = volhdr->new_cfg_off + sect_size;

		if (!(update_node_cfg(file, volhdr, cfg_hdr_off, cfg_node_off,
				      new_cfg_off, node_hdr, node_info,
				      sect_size, &update)))
			goto bail;
	}

	/* Update volume disk header */
	if (opts.gid != -1 || opts.uid != -1 || opts.perms != -1 ||
	    opts.device_size || opts.convert != -1) {
		if (!(update_volume_header(file, volhdr, sect_size, vol_size,
					   &update)))
			goto bail;
	}

	if (!update) {
		printf("No changes made to the volume.\nAborting.\n");
		goto bail;
	}

	/* query_only option bails you here */
	if (opts.query_only) {
		printf("Changes not written to disk.\n");
		goto bail;
	}

	if (!opts.force_op) {
		printf("Proceed (y/N): ");
		proceed = getchar();
		if (toupper(proceed) != 'Y') {
			printf("Aborting operation.\n");
			goto bail;
		}
	}

	/* Check for heartbeat for any write operation */
	if (!check_heart_beat(&file, rawdev, volhdr, &nodemap, sect_size))
		goto bail;

	if (nodemap) {
		/* Exit as device is mounted on some node */
		get_node_names(file, volhdr, node_names, sect_size);
		printf("%s mounted on nodes:", opts.device);
		print_node_names(node_names, nodemap);
		printf("Aborting.\n");
		goto bail;
	}

	/* do not allow interrupts */
	ignore_signal = true;

	/* Write node cfgs */
	if (IS_VALID_NODE_NUM(opts.slot_num)) {
		if (!write_sectors(file, cfg_hdr_off, 1, sect_size,
				   (void *)node_hdr))
			goto bail;
		if (!write_sectors(file, cfg_node_off, 1, sect_size,
				   (void *)node_info))
			goto bail;
		if (!write_sectors(file, new_cfg_off, 1, sect_size,
				   (void *)node_hdr))
			goto bail;
	}

	/* Write volume disk header */
	if (opts.gid != -1 || opts.uid != -1 || opts.perms != -1 ||
	    opts.device_size || opts.convert != -1) {
		offset = volhdr->start_off;
		if (!write_sectors(file, offset, 1, sect_size, (void *)volhdr))
			goto bail;
	}

	printf("Changes written to disk.\n");

bail:
	safefree(volhdr);
	safefree(node_hdr);
	safefree(node_info);

	for (i = 0; i < OCFS_MAXIMUM_NODES; ++i)
		safefree(node_names[i]);

	safeclose(file);
	unbind_raw(rawminor);
	return 0;
}				/* main */


/*
 * read_options()
 *
 * tuneocfs [ -C ] [ -f ] [ -g gid] [ -L label] [ -m mount-point]
 *          [ -p permissions] [ -q ] [ -S size] [ -u uid] [ -V ] device
 *
 */
int read_options(int argc, char **argv)
{
	int i;
	int ret = 1;
	char tmp[128];
	char *p;
	__u64 fac = 1;
	long double size;

	if (argc < 2) {
		version(argv[0]);
		usage(argv[0]);
		ret = 0;
		goto bail;
	}

	for (i = 1; i < argc && ret; i++) {
		switch(*argv[i]) {
		case '-':
			switch(*++argv[i]) {
			case 'C':	/* clear all data blocks */
				opts.clear_data_blocks = true;
				break;

			case 'F':	/* force resize */
				opts.force_op = true;
				break;

			case 'g':	/* gid */
				++i;
				if (i < argc && argv[i])
					opts.gid = get_gid(argv[i]);
				else {
					fprintf(stderr, "Invalid group id.\n"
						"Aborting.\n");
					ret = 0;
				}
				break;

			case 'h':	/* help */
				version(argv[0]);
				usage(argv[0]);
				ret = 0;
				break;

			case 'l':	/* list node configs */
				opts.list_nodes = true;
				break;

			case 'n':	/* query */
				opts.query_only = true;
				break;

			case 'N':	/* node cfg slot */
				++i;
				if (i < argc && argv[i])
					opts.slot_num = atoi(argv[i]);
				else {
					fprintf(stderr,"Invalid node config slot.\n"
						"Aborting.\n");
					ret = 0;
				}
				break;

			case 'p':	/* permissions */
				++i;
				if (i < argc && argv[i]) {
					opts.perms = strtoul(argv[i], NULL, 8);
					opts.perms &= 0007777;
				} else {
					fprintf(stderr,"Invalid permissions.\n"
						"Aborting.\n");
					ret = 0;
				}
				break;

			case 'q':	/* quiet */
				opts.quiet = true;
				break;

			case 'S':	/* device size */
				++i;
				if (i < argc && argv[i]) {
					strcpy(tmp, argv[i]);
					size = strtold(tmp, &p);
					if (p)
						MULT_FACTOR(*p, fac);
					opts.device_size = (__u64)(size * fac);
				} else {
					fprintf(stderr,"Invalid device size.\n"
						"Aborting.\n");
					ret = 0;
				}
				break;

			case 'u':	/* uid */
				++i;
				if (i < argc && argv[i])
					opts.uid = get_uid(argv[i]);
				else {
					fprintf(stderr,"Invalid user id.\n"
						"Aborting.\n");
					ret = 0;
				}
				break;

			case 'V':	/* print version */
				version(argv[0]);
				ret = 0;
				break;

			case 'x':	/* used for debugocfs */
				opts.print_progress = true;
				break;

			case 'c':	/* convert */
				++i;
				if (i < argc && argv[i]) {
					opts.convert = atoi(argv[i]);
					if (opts.convert == OCFS_MAJOR_VERSION) {
						fprintf(stderr, "Conversion to"
							" V1 ocfs not yet"
							" supported.\n"
							"Aborting.\n");
						ret = 0;
					} else if (opts.convert < OCFS_MAJOR_VERSION || opts.convert > OCFS2_MAJOR_VERSION) {
						fprintf(stderr, "Invalid "
							"version.\n"
							"Aborting.\n");
						ret = 0;
					}
				} else {
					fprintf(stderr, "No version "
						"specified.\nAborting.\n");
					ret = 0;
				}
				break;

			default:
				fprintf(stderr, "Invalid switch -%c.\n"
					"Aborting.\n", *argv[i]);
				ret = 0;
				break;
			}
			break;

		default:
			strncpy(opts.device, argv[i], FILE_NAME_SIZE);
			break;
		}
	}

      bail:
	return ret;
}				/* read_options */


/*
 * validate_options()
 *
 */
bool validate_options(char *progname)
{
	if (opts.device[0] == '\0') {
		fprintf(stderr, "Error: Device not specified.\n");
		usage(progname);
		return 0;
	}

	if (opts.slot_num != OCFS_INVALID_NODE_NUM) {
		if (opts.slot_num < 0 || opts.slot_num >= OCFS_MAXIMUM_NODES) {
			fprintf(stderr, "Error: Invalid node config slot "
			       	"specified.\n");
			usage(progname);
			return 0;
		}
	}

	return 1;
}				/* validate_options */


/*
 * update_volume_header()
 *
 */
int update_volume_header(int file, ocfs_vol_disk_hdr *volhdr, __u32 sect_size,
			 __u64 vol_size, bool *update)
{
	int ret = 1;

	if (opts.uid != -1) {
		if (getpwuid(opts.uid)) {
			printf("Changing uid from %d to %d\n", volhdr->uid,
			       opts.uid);
			volhdr->uid = opts.uid;
			*update = true;
		} else {
			fprintf (stderr, "Error: Invalid uid %d\n", opts.uid); 
			ret = 0;
			goto bail;
		}
	}

	if (opts.gid != -1) {
		if (getgrgid(opts.gid)) {
			printf("Changing gid from %d to %d\n", volhdr->gid,
			       opts.gid);
			volhdr->gid = opts.gid;
			*update = true;
		} else {
			fprintf (stderr, "Error: Invalid gid %d\n", opts.gid); 
			ret = 0;
			goto bail;
		}
	}

	if (opts.perms != -1) {
		if (opts.perms >= 0000 && opts.perms <= 07777) {
			printf("Changing permissions from 0%o to 0%o\n",
			       volhdr->prot_bits, opts.perms);
			volhdr->prot_bits = opts.perms;
			*update = true;
		} else {
			fprintf (stderr, "Error permissions 0%o\n", opts.perms);
			ret = 0;
			goto bail;
		}
	}

	if (opts.device_size) {
		ret = process_new_volsize(file, volhdr, sect_size, vol_size,
					  update);
		if (ret)
			*update = true;
	}

	if (opts.convert != -1) {
		if (opts.convert == 1) {
			volhdr->minor_version = OCFS_MINOR_VERSION;
			volhdr->major_version = OCFS_MAJOR_VERSION;
		} else if (opts.convert == 2) {
			volhdr->minor_version = OCFS2_MINOR_VERSION;
			volhdr->major_version = OCFS2_MAJOR_VERSION;
		} else {
			fprintf (stderr, "Error: Invalid version %d\n", 
				 opts.convert); 
			ret = 0;
			goto bail;
		}
		*update = true;
	}
bail:
	return ret;
}				/* update_volume_header */

/*
 * update_node_cfg()
 *
 */
int update_node_cfg(int file, ocfs_vol_disk_hdr *volhdr, __u64 cfg_hdr_off,
		    __u64 cfg_node_off, __u64 new_cfg_off,
		    ocfs_node_config_hdr *node_hdr,
		    ocfs_disk_node_config_info *node_info, __u32 sect_size,
		    bool *update)
{
	bool ret = 0;
	ocfs_guid guid;

	if (!read_sectors(file, cfg_hdr_off, 1, sect_size, (void *)node_hdr))
		goto bail;

	if (!read_sectors(file, cfg_node_off, 1, sect_size, (void *)node_info))
		goto bail;

	memset(&guid, 0, sizeof(ocfs_guid));

	if (memcmp(&node_info->guid, &guid, sizeof(guid))) {
		printf("Clearing node number %d used by node %s\n",
		       opts.slot_num, node_info->node_name);
		node_hdr->num_nodes--;
		node_hdr->cfg_seq_num++;
		memset(node_info, 0, sizeof(ocfs_disk_node_config_info));
		*update = true;
	} else {
		fprintf(stderr, "Node number %d is not in use\n",
		       	opts.slot_num);
	}

	ret = 1;
bail:
	return ret;
}				/* update_node_cfg */

/*
 * handle_signal()
 */
void handle_signal(int sig)
{
	switch (sig) {
	case SIGTERM:
	case SIGINT:
		if (!ignore_signal) {
			fprintf(stderr, "\nOperation interrupted.\nAborting.\n");
			safeclose(file);
			unbind_raw(rawminor);
			exit(1);
		}
		else
		{
			signal(sig, handle_signal);
		}
		break;
	}
}				/* handle_signal */

/*
 * print_node_cfgs()
 *
 */
int print_node_cfgs(int file, ocfs_vol_disk_hdr *volhdr, __u32 sect_size)
{
	char *buf = NULL;
	char *p;
	int len;
	int ret = 0;
	int i;
	ocfs_disk_node_config_info *conf;

	len = volhdr->node_cfg_size;
	if (!(buf = (char *) MemAlloc(len)))
		goto bail;
	else
		memset(buf, 0, len);

	if (!SetSeek(file, volhdr->node_cfg_off))
		goto bail;

	if (!(Read(file, len, buf)))
		goto bail;

	p = buf + (sect_size * 2);

	printf ("%2s %-32s %-15s %-7s %-s\n", "#", "Name", "IP Address", "IP Port", "Node GUID");
	printf ("%2s %-32s %-15s %-7s %-s\n", "=", "================================",
	       	"===============", "=======", "================================");
	for (i = 0; i < OCFS_MAXIMUM_NODES; ++i, p += sect_size) {
		conf = (ocfs_disk_node_config_info *)p;
		if (!conf->node_name[0])
			continue;
		printf ("%2d %-32s %-15s %-7d %*s\n", i, conf->node_name,
		       	conf->ipc_config.ip_addr, conf->ipc_config.ip_port,
		       	GUID_LEN, conf->guid.guid);
	}

	ret = 1;
      bail:
	safefree(buf);
	return ret;
}				/* print_node_cfgs */


/*
 * process_new_volsize()
 *
 */
int process_new_volsize(int file, ocfs_vol_disk_hdr *volhdr, __u32 sect_size,
		       	__u64 vol_size, bool *update)
{
	__u64 end_free_sz;
	__u64 new_data_sz;
	__u64 old_num_blks, new_num_blks;
	__u64 max_vol_sz, min_vol_sz;
	__u8 *bitmap = NULL;
	__u32 num_sectors;
	__u32 new_bitmap_sz;
	__u32 old_num_bytes, new_num_bytes;
	__u32 i;
	__u32 byte_ind, bit_ind;
	__u8 *p;
	int ret = 0;
	char str1[100], str2[100];

	if (vol_size > volhdr->device_size) {
		end_free_sz = OCFS_NUM_END_SECTORS * sect_size;
		new_data_sz = vol_size - volhdr->data_start_off - end_free_sz;

		new_num_blks = (__u64)(new_data_sz / volhdr->cluster_size);
		old_num_blks = volhdr->num_clusters;

		if (new_num_blks == old_num_blks) {
			fprintf(stderr, "No change in number of blocks (%llu)."
				"\nAborting.\n", old_num_blks);
			goto bail;
		}

		new_bitmap_sz = OCFS_BUFFER_ALIGN(((new_num_blks + 7) / 8),
						  sect_size);
		if (new_bitmap_sz > OCFS_MAX_BITMAP_SIZE) {
			max_vol_sz = OCFS_MAX_BITMAP_SIZE * 8 *
				     volhdr->cluster_size;
			max_vol_sz += volhdr->data_start_off + end_free_sz;

			num_to_str(volhdr->cluster_size, str1);
			num_to_str(max_vol_sz, str2);
			fprintf(stderr, "With a %s block size, the max "
				"volume size can be %s.\nAborting.\n",
				str1, str2);
			goto bail;
		}

		if (!opts.quiet) {
			printf("Increasing volume size from %llu bytes to %llu bytes.\n",
			       volhdr->device_size, vol_size);
			printf("Increasing number of blocks from %llu to %llu."
			       "\n", volhdr->num_clusters, new_num_blks);
		}

		volhdr->device_size = vol_size;
		volhdr->num_clusters = new_num_blks;
		*update = true;

	} else if (vol_size < volhdr->device_size) {
		end_free_sz = OCFS_NUM_END_SECTORS * sect_size;
		new_data_sz = vol_size - volhdr->data_start_off - end_free_sz;

		new_num_blks = new_data_sz / volhdr->cluster_size;
		old_num_blks = volhdr->num_clusters;

		if (new_num_blks == old_num_blks) {
			fprintf(stderr, "No change in number of blocks (%llu)."
				"\nAborting.\n", old_num_blks);
			goto bail;
		}

		if (!(bitmap = (char *) MemAlloc(OCFS_MAX_BITMAP_SIZE)))
			goto bail;

		/* read the bitmap */
		num_sectors = OCFS_MAX_BITMAP_SIZE / sect_size;
		if (!read_sectors(file, volhdr->bitmap_off, num_sectors,
				  sect_size, (void *)bitmap))
			goto bail;

#define GET_LAST_BYTE(num_bytes, num_blocks)		\
		do {					\
			num_bytes = (num_blocks) / 8;	\
			if ((num_blocks) % 8)		\
				++(num_bytes);		\
		} while (0)

		GET_LAST_BYTE(new_num_bytes, new_num_blks);
		GET_LAST_BYTE(old_num_bytes, old_num_blks);

		for (i = old_num_bytes, p = bitmap + old_num_bytes;
		     i > new_num_bytes - 1; --i, --p)
			if (*p)
				break;

		byte_ind = i;
		if (byte_ind > new_num_bytes - 1) {
			char x = 0x80;
			for (i = 8; i > 0; --i, x >>= 1) {
				if (*p & x)
					break;
			}

			bit_ind = i;

			min_vol_sz = (byte_ind - 1) * 8 * volhdr->cluster_size;
			min_vol_sz += bit_ind * volhdr->cluster_size;
			min_vol_sz += volhdr->data_start_off + end_free_sz;

			fprintf(stderr, "Due to disk usage, the volume size "
				"cannot be smaller than %llu bytes.\nAborting.\n",
				min_vol_sz);
			goto bail;
		}

		if (!opts.quiet) {
			printf("Decreasing volume size from %llu bytes to %llu bytes.\n",
			       volhdr->device_size, vol_size);
			printf("Decreasing number of blocks from %llu to %llu."
			       "\n", volhdr->num_clusters, new_num_blks);
		}

		volhdr->device_size = vol_size;
		volhdr->num_clusters = new_num_blks;
		*update = true;
	}

	ret = 1;

bail:
	safefree(bitmap);
	return ret;
}				/* process_new_volsize */
