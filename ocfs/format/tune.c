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

/* getopt stuff */
int getopt(int argc, char *const argv[], const char *optstring);
extern char *optarg;
extern int optind, opterr, optopt;

void init_global_context(void);
int create_orphan_dirs(ocfs_vol_disk_hdr *volhdr, bool *update, int fd);
int ocfs_init_orphan_dir(ocfs_super *osb, int node_num, char *filename);

ocfs_super *osb = NULL;
struct super_block sb;
ocfs_vol_disk_hdr *vdh = NULL;
ocfs_global_ctxt OcfsGlobalCtxt;

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
	.convert = -1,
	.disk_hb = 0,
	.hb_timeo = 0
};

bool ignore_signal = false;
int file = 0;
int rawminor = 0;
char rawdev[FILE_NAME_SIZE];

char *usage_string =
"usage: %s [-d ms] [-F] [-g gid] [-h] [-l] [-n] [-N nodenum] [-p permissions] "
"[-q] [-S size] [-t ms] [-u uid] [-V] device\n\n"
"	-d disk heartbeat in ms\n"
"	-F Force resize existing OCFS volume\n"
"	-g Group ID for the root directory\n"
"	-h Help\n"
"	-l List all the node config slots\n"
"	-n Query only\n"
"	-N Node config slot be to be cleared\n"
"	-p Permissions for the root directory\n"
"	-q Quiet execution\n"
"	-S Volume size, e.g., 50G (M for mega, G for giga, T for tera)\n"
"	-t heartbeat timeout in ms\n"
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
	    opts.device_size || opts.convert != -1 || opts.disk_hb || opts.hb_timeo) {
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

	/* Make orphaned inode dirs for version 2 upgrade. Do this
	 * before the header is updated as libocfs is looking for a
	 * 1.2 versioned filesystem! */
	if (opts.convert == OCFS2_MAJOR_VERSION) {
		if (create_orphan_dirs(volhdr, &update, file))
			goto bail;
	}

	/* Write volume disk header */
	if (opts.gid != -1 || opts.uid != -1 || opts.perms != -1 ||
	    opts.device_size || opts.convert != -1 || opts.disk_hb || opts.hb_timeo) {
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
 * "usage: %s [-d ms] [-F] [-g gid] [-h] [-l] [-n] [-N nodenum] [-p permissions] "
 * "[-q] [-S size] [-t ms] [-u uid] [-V] device\n\n"
 *
 */
int read_options(int argc, char **argv)
{
	int ret = 1;
	int c;
	char *p;
	__u64 fac = 1;
	long double size;

	if (argc < 2) {
		version(argv[0]);
		usage(argv[0]);
		ret = 0;					  
		goto bail;
	}

	while(1) {
		c = getopt(argc, argv, "CFhlnqVd:g:N:p:S:t:u:");
		if (c == -1)
			break;

		switch (c) {
		case 'd':	/* disk heartbeat */
			opts.disk_hb = strtoul(optarg, NULL, 0);
			break;

		case 'C':	/* clear all data blocks */
			opts.clear_data_blocks = true;
			break;

		case 'F':	/* force resize */
			opts.force_op = true;
			break;

		case 'g':	/* gid */
			opts.gid = get_gid(optarg);
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
			opts.slot_num = atoi(optarg);
			break;

		case 'p':	/* permissions */
			opts.perms = strtoul(optarg, NULL, 8);
			opts.perms &= 0007777;
			break;

		case 'q':	/* quiet */
			opts.quiet = true;
			break;

		case 'S':	/* device size */
			size = strtold(optarg, &p);
			if (p)
				MULT_FACTOR(*p, fac);
			opts.device_size = (__u64)(size * fac);
			break;

		case 't':	/* node timeout */
			opts.hb_timeo = strtoul(optarg, NULL, 0);
			break;

		case 'u':	/* uid */
			opts.uid = get_uid(optarg);
			break;

		case 'V':	/* print version */
			version(argv[0]);
			ret = 0;
			break;

		case 'x':	/* used for debugocfs */
			opts.print_progress = true;
			break;

		case 'c':	/* convert */
			opts.convert = atoi(optarg);
			if (opts.convert == OCFS_MAJOR_VERSION) {
				fprintf(stderr, "Conversion to V1 ocfs not yet supported.\nAborting.\n");
				ret = 0;
			} else if (opts.convert < OCFS_MAJOR_VERSION || opts.convert > OCFS2_MAJOR_VERSION) {
				fprintf(stderr, "Invalid version.\nAborting.\n");
				ret = 0;
			}
			break;

		default:
			break;
		}
	}

	if (ret && optind < argc && argv[optind])
			strncpy(opts.device, argv[optind], FILE_NAME_SIZE);

bail:
	return ret;
}				/* read_options */


/*
 * validate_options()
 *
 */
bool validate_options(char *progname)
{
	__u32 miss_cnt;
	__u32 min_hbt;

	if (opts.device[0] == '\0') {
		fprintf(stderr, "Error: Device not specified.\n");
		usage(progname);
		return 0;
	}

	if (opts.slot_num != OCFS_INVALID_NODE_NUM) {
		if (opts.slot_num < 0 || opts.slot_num >= OCFS_MAXIMUM_NODES) {
			fprintf(stderr, "Error: Node config slot should be "
				"between 0 and 31.\n");
			return 0;
		}
	}

	if (opts.disk_hb && !IS_VALID_DISKHB(opts.disk_hb)) {
		fprintf(stderr, "Error: Disk heartbeat should be between %d "
			"and %d.\n", OCFS_MIN_DISKHB, OCFS_MAX_DISKHB);
		return 0;
	}

	if (opts.hb_timeo && !IS_VALID_HBTIMEO(opts.hb_timeo)) {
		fprintf(stderr, "Error: Node timeout should be between %d "
			"and %d.\n", OCFS_MIN_HBTIMEO, OCFS_MAX_HBTIMEO);
		return 0;
	}

	if (opts.disk_hb || opts.hb_timeo) {
		if (!(opts.disk_hb && opts.hb_timeo)) {
			fprintf(stderr, "Error: Both node timeout and disk "
				"heartbeat need to be specified.\n");
		       	return 0;
		}

		miss_cnt = opts.hb_timeo / opts.disk_hb;
		if (miss_cnt < MIN_MISS_COUNT_VALUE) {
			min_hbt = opts.disk_hb * MIN_MISS_COUNT_VALUE;
			fprintf(stderr, "Error: For %d ms disk heartbeat, "
			       	"node timeout cannot be less than %d ms.\n",
			       	opts.disk_hb, min_hbt);
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
		if (opts.convert == OCFS_MAJOR_VERSION) {
			volhdr->minor_version = OCFS_MINOR_VERSION;
			volhdr->major_version = OCFS_MAJOR_VERSION;
		} else if (opts.convert == OCFS2_MAJOR_VERSION) {
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

	if (opts.disk_hb) {
		printf("Changing disk heartbeat from %u ms to %u ms\n",
		       volhdr->disk_hb, opts.disk_hb);
		volhdr->disk_hb = opts.disk_hb;
		*update = true;
	}

	if (opts.hb_timeo) {
		printf("Changing node timeout from %u ms to %u ms\n",
		       volhdr->hb_timeo, opts.hb_timeo);
		volhdr->hb_timeo = opts.hb_timeo;
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

void init_global_context()
{
	char *tmp;

	memset(&OcfsGlobalCtxt, 0, sizeof(ocfs_global_ctxt));
	OcfsGlobalCtxt.obj_id.type = OCFS_TYPE_GLOBAL_DATA;
	OcfsGlobalCtxt.obj_id.size = sizeof (ocfs_global_ctxt);
	OcfsGlobalCtxt.pref_node_num = 31;
	OcfsGlobalCtxt.node_name = "user-tool";
	OcfsGlobalCtxt.comm_info.type = OCFS_UDP;
	OcfsGlobalCtxt.comm_info.ip_addr = "0.0.0.0";
	OcfsGlobalCtxt.comm_info.ip_port = OCFS_IPC_DEFAULT_PORT;
	OcfsGlobalCtxt.comm_info.ip_mask = NULL;
	OcfsGlobalCtxt.comm_info_read = true;
	memset(&OcfsGlobalCtxt.guid.id.host_id, 'f', HOSTID_LEN);
	memset(&OcfsGlobalCtxt.guid.id.mac_id,  '0', MACID_LEN);

	tmp = getenv("debug_level");
	if (tmp)
		debug_level = atoi(tmp);
	tmp = getenv("debug_context");
	if (tmp)
		debug_context = atoi(tmp);
	tmp = getenv("debug_exclude");
	if (tmp)
		debug_exclude = atoi(tmp);
}


/* these are needed for the function below as they are used in OCFS2 */
#define  OCFS_DEFAULT_DIR_NODE_SECTS (256)
#define  OCFS_ORPHAN_DIR_FILENAME          "OrphanDir"
#define  DIR_NODE_FLAG_ORPHAN         0x2
#define  OCFS_SECTOR_SIZE 512
enum {
    OCFS2_INVALID_SYSFILE = -1,
    OCFS2_VOL_MD_SYSFILE = 0,
    OCFS2_VOL_MD_LOG_SYSFILE,
    OCFS2_DIR_SYSFILE,
    OCFS2_DIR_BM_SYSFILE,
    OCFS2_FILE_EXTENT_SYSFILE,
    OCFS2_FILE_EXTENT_BM_SYSFILE,
    OCFS2_RECOVER_LOG_SYSFILE,
    OCFS2_CLEANUP_LOG_SYSFILE,
    OCFS2_VOL_BM_SYSFILE,
    OCFS2_ORPHAN_DIR_SYSFILE,
    OCFS2_NUM_SYSFILES
};
#define OCFS_ORPHAN_DIR              (OCFS2_ORPHAN_DIR_SYSFILE     * OCFS_MAXIMUM_NODES)

int ocfs_init_orphan_dir(ocfs_super *osb, int node_num, char *filename)
{
	int status = 0;
	__u64 offset = 0;
	ocfs_file_entry *fe = NULL;
	__u32 file_id;
	__u64 bitmap_off;
	__u64 file_off = 0;
	ocfs_dir_node *new_dir = NULL;
	__u64 numsects;

	LOG_ENTRY_ARGS ("(node_num = %u)\n", node_num);

	memset(filename, 0, OCFS_MAX_FILENAME_LENGTH);

	/* I prefer to just use node_num, but we should prolly be consistent
	 * with the rest of the system files. */
	file_id = OCFS_ORPHAN_DIR + node_num;
	offset = (file_id * osb->sect_size) + osb->vol_layout.root_int_off;
	sprintf(filename, "%s%d", OCFS_ORPHAN_DIR_FILENAME, file_id);

	/* Alright, allocate and create the dirnode */
	status = ocfs_alloc_node_block (osb, OCFS_DEFAULT_DIR_NODE_SIZE, 
					&bitmap_off, &file_off, &numsects, 
					osb->node_num, DISK_ALLOC_DIR_NODE);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	new_dir = (ocfs_dir_node *) malloc_aligned(OCFS_DEFAULT_DIR_NODE_SIZE);
	if (!new_dir) {
		status = -ENOMEM;
		goto leave;
	}
	memset(new_dir, 0, OCFS_DEFAULT_DIR_NODE_SIZE);

	ocfs_initialize_dir_node (osb, new_dir, bitmap_off, file_off, 
				  osb->node_num);
	DISK_LOCK_CURRENT_MASTER (new_dir) = node_num;
	DISK_LOCK_FILE_LOCK (new_dir) = OCFS_DLM_ENABLE_CACHE_LOCK;
	new_dir->dir_node_flags |= DIR_NODE_FLAG_ORPHAN;

	status = ocfs_write_force_disk(osb, new_dir, 
				       OCFS_DEFAULT_DIR_NODE_SIZE, bitmap_off);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	/* Alright, now setup the file entry. */
	fe = malloc_aligned(osb->sect_size);
	if (!fe) {
		status = -ENOMEM;
		goto leave;
	}
	memset (fe, 0, osb->sect_size);

	fe->local_ext = true;
	fe->granularity = -1;
	strcpy (fe->signature, OCFS_FILE_ENTRY_SIGNATURE);
	fe->next_free_ext = 0;
	memcpy (fe->filename, filename, strlen (filename));
	(fe->filename)[strlen (filename)] = '\0';
	SET_VALID_BIT (fe->sync_flags);
	fe->sync_flags &= ~(OCFS_SYNC_FLAG_CHANGE);
	fe->this_sector = offset;
	fe->last_ext_ptr = 0;

	/* do we do this or leave it like all the other sysfiles (0) */
	fe->attribs = OCFS_ATTRIB_DIRECTORY;

	fe->alloc_size = osb->vol_layout.dir_node_size;
	fe->extents[0].disk_off = bitmap_off;
	fe->file_size = osb->vol_layout.dir_node_size;
	fe->next_del = INVALID_DIR_NODE_INDEX;

	status = ocfs_write_force_disk(osb, fe, osb->sect_size, offset);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

leave:
	if (fe)
		free_aligned(fe);

	if (new_dir)
		free_aligned(new_dir);

	LOG_EXIT_STATUS(status);
	return(status);
}

int create_orphan_dirs(ocfs_vol_disk_hdr *volhdr, bool *update, int fd)
{
	int i;
	int ret = 0;
	bool mounted = false;
	char *filename = NULL;

	init_global_context();

	if ((vdh = malloc_aligned(1024)) == NULL) {
		fprintf(stderr, "failed to alloc %d bytes!  exiting!\n", 1024);
		ret = ENOMEM;
		goto done;
	}

	memset(vdh, 0, 1024);
	memset(&sb, 0, sizeof(struct super_block));
	sb.s_dev = fd;

	ret = ocfs_read_disk_header ((__u8 **)&vdh, &sb);
	if (ret < 0) {
		fprintf(stderr, "failed to read header\n");
		goto done;
	}

	ret = ocfs_mount_volume (&sb, false);
	if (ret < 0) {
		fprintf(stderr, "failed to mount\n");
		goto done;
	}
	mounted = true;
	osb = sb.u.generic_sbp;

	filename = malloc(OCFS_MAX_FILENAME_LENGTH);
	if (!filename) {
		fprintf(stderr, "failed to alloc %d bytes!  exiting!\n", 
			OCFS_MAX_FILENAME_LENGTH);
		ret = ENOMEM;
		goto done;
	}

	for(i = 0; i < OCFS_MAXIMUM_NODES; i++) {
		ret = ocfs_init_orphan_dir(osb, i, filename);
		if (ret < 0) {
			fprintf(stderr,"Could not create orphan directory!\n");
			ret = ENOMEM;
			goto done;
		}
		*update = true;
	}
done:
	if (mounted) {
		int tmp;
		if ((tmp = ocfs_dismount_volume (&sb)) < 0) {
			fprintf(stderr, "dismount failed, ret = %d\n", tmp);
			if (ret == 0)
				ret = tmp;
		}
	}

	if (vdh)
		free_aligned(vdh);

	if (filename)
		free(vdh);
		
	return(ret);
}
