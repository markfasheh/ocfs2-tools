/*
 * format.c
 *
 * ocfs format utility
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
 * Authors: Neeraj Goyal, Suchit Kaura, Kurt Hackel, Sunil Mushran,
 *          Manish Singh, Wim Coekaerts
 */

#include <format.h>
#include <signal.h>
#include <libgen.h>

bool in_data_blocks = false;
bool format_intr = false;

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

#define OCFS_MKFS_V2 "mkfs.ocfs2"
int major_version = OCFS_MAJOR_VERSION;
int minor_version = OCFS_MINOR_VERSION;
__u64 sect_count = 0;
__u64 format_size = 0;
int rawminor = 0;
char rawdev[FILE_NAME_SIZE];
int file = 0;

char *usage_string =
"usage: %s -b block-size [-C] [-F] [-g gid] [-h] -L volume-label "
"-m mount-path [-n] [-p permissions] [-q] [-u uid] [-V] device\n\n"
"	-b Block size in kilo bytes\n"
"	-C Clear all data blocks\n"
"	-F Force format existing OCFS volume\n"
"	-g GID for the root directory\n"
"	-h Help\n"
"	-L Volume label\n"
"	-m Path where this device will be mounted\n"
"	-n Query only\n"
"	-p Permissions for the root directory\n"
"	-q Quiet execution\n"
"	-u UID for the root directory\n"
"	-V Print version and exit\n";


/*
 * main()
 *
 */
int main(int argc, char **argv)
{
    ocfs_vol_disk_hdr *volhdr = NULL;
    __u32 sect_size = OCFS_SECTOR_SIZE;
    __u64 vol_size = 0;
    char vol_id[MAX_VOL_ID_LENGTH];
    __u64 offset;
    __u64 publ_off = 0;
    __u64 data_start_off = 0;
    bool ocfs_vol = false;
    __u32 nodemap = 0;
    char *node_names[OCFS_MAXIMUM_NODES];
    char *base;
    char *progname = NULL;

#define INSTALL_SIGNAL(sig) 						\
	do { 								\
		if (signal(sig, HandleSignal) == SIG_ERR) {		\
			fprintf(stderr, "Could not set " #sig "\n");	\
			goto bail;					\
		}							\
	} while(0)

    INSTALL_SIGNAL(SIGTERM);
    INSTALL_SIGNAL(SIGINT);
    
    init_raw_cleanup_message();

    if (!(volhdr = (ocfs_vol_disk_hdr *) MemAlloc(sect_size)))
	    goto bail;
    else
	    memset(volhdr, 0, sect_size);

    progname = strdup(argv[0]);
    if (!progname)
	    goto bail;
    base = basename(progname);
    if (!base)
	    goto bail;

    if (strcmp(base, OCFS_MKFS_V2) == 0) {
	    major_version = OCFS2_MAJOR_VERSION;
	    minor_version = OCFS2_MINOR_VERSION;
    }
    free (progname);

    /* Read the options */
    if (!ReadOptions(argc, argv))
	goto bail;

    /* Validate the options */
    if (!ValidateOptions())
	goto bail;

    /* Generate Volume ID */
    if (!GenerateVolumeID(vol_id, sizeof(vol_id)))
	goto bail;

    /* Open the supplied disk */
    if (!(file = OpenDisk(opts.device)))
	goto bail;

    /* Get the partition Information */
    if (!(GetDiskGeometry(file, &vol_size, &sect_size)))
	goto bail;

    if (vol_size < OCFS_MIN_VOL_SIZE) {
	fprintf(stderr, "Error: %s at %lluMB is smaller than %uMB.\n"
		"Aborting.\n", opts.device, (vol_size/(1024*1024)),
	       	(OCFS_MIN_VOL_SIZE/(1024*1024)));
	    goto bail;
    }

    /* close and reopen after binding to raw */
    safeclose(file);

    /* bind device to raw */
    if (bind_raw(opts.device, &rawminor, rawdev, sizeof(rawdev)))
	goto bail;

    /* Open the supplied disk */
    if (!(file = OpenDisk(rawdev)))
	goto bail;

    /* Initialize the DiskHeader structure */
    if (!(InitVolumeDiskHeader(volhdr, sect_size, vol_size, &data_start_off)))
	goto bail;

#ifdef DEBUG
	ShowDiskHdrVals(volhdr);
#endif

     /* query_only option bails you here */
    if (opts.query_only)
	goto bail;

    /* OCFS formatted disks will not be overwritten without the -f option */
    if (!CheckForceFormat(file, &publ_off, &ocfs_vol, sect_size))
	goto bail;

    /* If valid ocfs volume, check for any heart beating node(s) */
    if (ocfs_vol) {
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
    }

    if (opts.clear_data_blocks)
	    format_size = (volhdr->device_size / sect_size) -
		    		OCFS_NUM_END_SECTORS;
    else
	    format_size = CLEAR_DATA_BLOCK_SIZE + (data_start_off / sect_size);

    /* Clear the Volume header in the first sector of the volume */
    PRINT_PROGRESS();
    PRINT_VERBOSE("Clearing volume header sectors...");
    fflush(stdout);
    offset = volhdr->start_off;
    if (!ClearSectors(file, offset, 1, sect_size))
	goto bail;

    /* Write the Volume Label in the 2nd and 3rd sectors */
    offset = volhdr->start_off + sect_size;
    if (!WriteVolumeLabel(file, vol_id, sizeof(vol_id), offset, sect_size))
	goto bail;

    /* Clear sectors 4 to 8 */
    offset = volhdr->start_off + (3 * sect_size);
    if (!ClearSectors(file, offset, 5, sect_size))
	goto bail;

    sect_count += 8;
    PRINT_VERBOSE("\rCleared volume header sectors                 \n");

    /* Initialize Node config header */
    PRINT_PROGRESS();
    PRINT_VERBOSE("Clearing node config sectors...");
    fflush(stdout);
    offset = volhdr->node_cfg_off;
    if (!InitNodeConfHdr(file, offset, sect_size))
	goto bail;

    /* Clear Node config info */
    offset = volhdr->node_cfg_off + (2 * sect_size);
    if (!ClearSectors(file, offset, OCFS_MAXIMUM_NODES, sect_size))
	goto bail;

    /* Clear NewConf */
    offset = volhdr->node_cfg_off + volhdr->node_cfg_size;
    if (!ClearSectors(file, offset, 4, sect_size))
	goto bail;
    fsync(file);

    sect_count += OCFS_MAXIMUM_NODES + 6;
    PRINT_VERBOSE("\rCleared node config sectors                 \n");

    /* Clear Publish Sectors */
    PRINT_PROGRESS();
    PRINT_VERBOSE("Clearing publish sectors...");
    fflush(stdout);
    if (!ClearSectors(file, volhdr->publ_off, OCFS_MAXIMUM_NODES, sect_size))
	goto bail;
    fsync(file);

    sect_count += OCFS_MAXIMUM_NODES;
    PRINT_VERBOSE("\rCleared publish sectors                     \n");

    /* Clear Vote Sectors */
    PRINT_PROGRESS();
    PRINT_VERBOSE("Clearing vote sectors...");
    fflush(stdout);
    if (!ClearSectors(file, volhdr->vote_off, OCFS_MAXIMUM_NODES, sect_size))
	goto bail;
    fsync(file);

    sect_count += OCFS_MAXIMUM_NODES;
    PRINT_VERBOSE("\rCleared vote sectors                        \n");

    /* Clear the Bitmap */
    PRINT_PROGRESS();
    PRINT_VERBOSE("Clearing bitmap sectors...");
    fflush(stdout);
    if (!ClearBitmap(file, volhdr))
	goto bail;
    fsync(file);

    sect_count += (OCFS_MAX_BITMAP_SIZE / sect_size);
    PRINT_VERBOSE("\rCleared bitmap sectors                      \n");

    /* Clear OCFS_NUM_FREE_SECTORS */
    PRINT_PROGRESS();
    fflush(stdout);
    offset = volhdr->bitmap_off + OCFS_MAX_BITMAP_SIZE;
    if (!ClearSectors(file, offset, OCFS_NUM_FREE_SECTORS, sect_size))
	goto bail;
    fsync(file);

    sect_count += OCFS_NUM_FREE_SECTORS;

    /* Clear the data blocks */
    if (!ClearDataBlocks(file, volhdr, sect_size))
	goto bail;
    fsync(file);

    /* Write volume header */
    PRINT_VERBOSE("Writing volume header...");
    fflush(stdout);
    sigblock(sigmask(SIGINT));   /* blocking ctrl-c */
    offset = volhdr->start_off;
    if (!WriteVolumeHdr(file, volhdr, offset, sect_size))
	goto bail;

    sect_count = format_size;
    PRINT_PROGRESS();
    PRINT_VERBOSE("\rWrote volume header                         \n");
    fflush(stdout);

  bail:
    safefree(volhdr);
    safeclose(file);
    unbind_raw(rawminor);
    return 0;
}				/* main */


/*
 * CheckForceFormat()
 *
 */
int CheckForceFormat(int file, __u64 *publ_off, bool *ocfs_vol, __u32 sect_size)
{
	__u32 len;
	char *buf;
	int ret = 0;
	ocfs_vol_disk_hdr *volhdr = NULL;
	char format;

	len = sect_size;
	if (!(buf = (char *) MemAlloc(len)))
		goto bail;
	else
		memset(buf, 0, len);

	/* Seek to 1st sector */
	if (!SetSeek(file, 0))
		goto bail;

	/* Read the Information of first sector on disk */
	if (!(Read(file, len, buf)))
		goto bail;

	volhdr = (ocfs_vol_disk_hdr *) buf;

	if ((!memcmp(volhdr->signature, OCFS_VOLUME_SIGNATURE,
		     strlen(OCFS_VOLUME_SIGNATURE)))) {
		*ocfs_vol = true;
    		*publ_off = volhdr->publ_off;
	} else {
		*ocfs_vol = false;
		*publ_off = (__u64) 0;
	}

	/* Ensure forceformat flag is set to prevent overwriting */
	if (*ocfs_vol) {
		if (opts.force_op)
			ret = 1;
		else {
			if (opts.print_progress) {
				fprintf(stderr, "Error: Use -F to format "
					"existing OCFS volume.\n");
				goto bail;
			}
			printf("Format existing OCFS volume (y/N): ");
			format = getchar();
			if (format == 'y' || format == 'Y')
				ret = 1;
			else
				printf("Aborting.\n");
		}
	} else
		ret = 1;

      bail:
	safefree(buf);
	return ret;
}				/* CheckForceFormat */


/*
 * WriteVolumeHdr()
 *
 */
int WriteVolumeHdr(int file, ocfs_vol_disk_hdr * volhdr, __u64 offset,
		   __u32 sect_size)
{
    int ret = 0;

    if (!SetSeek(file, offset))
	goto bail;

    if (!Write(file, sect_size, (void *) volhdr))
	goto bail;

    ret = 1;
  bail:
    return ret;
}				/* WriteVolumeHdr */


/*
 * WriteVolumeLabel()
 *
 */
int WriteVolumeLabel(int file, char *volid, __u32 volidlen, __u64 offset,
		     __u32 sect_size)
{
    ocfs_vol_label *pLabel;
    __u32 len;
    char *buf;
    int ret = 0;

    len = 2 * sect_size;
    if (!(buf = (char *) MemAlloc(len)))
	goto bail;
    else
	memset(buf, 0, len);

    /* Populate volume label */
    InitVolumeLabel((ocfs_vol_label *) buf, sect_size, volid, volidlen);

    /* Set the bitmap lock to invalid_master */
    pLabel = (ocfs_vol_label *) (buf + sect_size);
    pLabel->disk_lock.curr_master = -1;

    if (!SetSeek(file, offset))
	goto bail;

    if (!Write(file, len, buf))
	goto bail;

    ret = 1;
  bail:
    safefree(buf);
    return ret;
}				/* WriteVolumeLabel */


/*
 * InitNodeConfHdr()
 *
 */
int InitNodeConfHdr(int file, __u64 offset, __u32 sect_size)
{
    __u32 len;
    char *buf;
    int ret = 0;

    len = 2 * sect_size;
    if (!(buf = (char *) MemAlloc(len)))
	goto bail;
    else
	memset(buf, 0, len);

    /* Set node config header */
    SetNodeConfigHeader((ocfs_node_config_hdr *) buf);

    /* Set the File pointer to node config sector */
    if (!SetSeek(file, offset))
	goto bail;

    if (!(Write(file, len, buf)))
	goto bail;

    ret = 1;
  bail:
    safefree(buf);
    return ret;
}				/* InitNodeConfHdr */


/*
 * ClearSectors()
 *
 */
int ClearSectors(int file, __u64 strtoffset, __u32 noofsects, __u32 sect_size)
{
    __u32 len;
    char *buf = NULL;
    __u32 i;
    int ret = 0;

    len = sect_size;
    if (!(buf = (char *) MemAlloc(len)))
	goto bail;
    else
	memset(buf, 0, len);

    if (!SetSeek(file, strtoffset))
	goto bail;

    for (i = 0; i < noofsects; ++i)
    {
	if (!Write(file, len, buf))
	    goto bail;
    }

    ret = 1;
  bail:
    safefree(buf);
    return ret;
}				/* ClearSectors */


/*
 * ClearBitmap()
 *
 */
int ClearBitmap(int file, ocfs_vol_disk_hdr * volhdr)
{
    __u32 len;
    char *buf = NULL;
    int ret = 0;

    len = OCFS_MAX_BITMAP_SIZE;
    if (!(buf = (char *) MemAlloc(len)))
	goto bail;
    else
	memset(buf, 0, len);

    /* Initialize the bitmap on disk */
    if (!SetSeek(file, volhdr->bitmap_off))
	goto bail;

    if (!(Write(file, len, buf)))
	goto bail;

    ret = 1;
  bail:
    safefree(buf);
    return ret;
}				/* ClearBitmap */


/*
 * ClearDataBlocks()
 *
 */
int ClearDataBlocks(int file, ocfs_vol_disk_hdr * volhdr, __u32 sect_size)
{
	__u64 blocks = 0;
	__u64 bytes = 0;
	__u64 len;
	__u64 offset;
	__u64 i;
	char *buf = NULL;
	int ret = 0;

	len = CLEAR_DATA_BLOCK_SIZE * sect_size;
	if (!(buf = (char *) MemAlloc(len)))
		goto bail;
	else
		memset(buf, 0, len);

	offset = volhdr->device_size - volhdr->data_start_off;
	blocks = offset / len;
	bytes = offset % len;

	offset = volhdr->data_start_off;
	if (!SetSeek(file, offset))
		goto bail;

	if (!opts.clear_data_blocks) {
		PRINT_PROGRESS();
		PRINT_VERBOSE("Clearing data block...");
		fflush (stdout);
		/* clear first 1M of data blocks */
		if (!(Write(file, len, buf)))
			goto bail;
		fsync(file);
		PRINT_VERBOSE("\rCleared data block              \n");
		fflush (stdout);
		sect_count += CLEAR_DATA_BLOCK_SIZE;
		ret = 1;
		goto bail;
	}

	if (blocks) {
		in_data_blocks = true;
		for (i = 0; i < blocks; i++, offset += len) {
			PRINT_PROGRESS();
			PRINT_VERBOSE("\rClearing data block %llu of %llu",
				      i, blocks);
			fflush(stdout);
			if (!(Write(file, len, buf)))
				goto bail;
			if (!(i % 20))
				fsync(file);
			sect_count += CLEAR_DATA_BLOCK_SIZE;
			if (format_intr)
				break;
		}
	}

	if (!format_intr && bytes) {
		if (!(Write(file, bytes, buf)))
			goto bail;
	}

	PRINT_PROGRESS();

	if (format_intr)
		printf("\nFormatting interrupted..... volume may "
		       "not be usable                    \n");
	else
		PRINT_VERBOSE("\rCleared data blocks                         "
			      "                 \n");
	fflush(stdout);

	ret = 1;
      bail:
	safefree(buf);
	return ret;
}				/* ClearDataBlocks */


/*
 * ReadOptions()
 *
 */
int ReadOptions(int argc, char **argv)
{
	int i;
	int ret = 1;

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
			case 'b':	/* block size */
				++i;
				if (i < argc && argv[i])
					opts.block_size = atoi(argv[i]);
				else {
					fprintf(stderr, "Invalid block size.\n"
						"Aborting.\n");
					ret = 0;
				}
				break;

			case 'C':	/* clear all data blocks */
				opts.clear_data_blocks = true;
				break;

			case 'F':	/* force format */
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

			case 'L':	/* volume label */
				++i;
				if (i < argc && argv[i])
					strncpy(opts.volume_label, argv[i],
						MAX_VOL_LABEL_LEN);
				else {
					fprintf(stderr,"Invalid volume label.\n"
						"Aborting.\n");
					ret = 0;
				}
				break;

			case 'h':       /* help */
				version(argv[0]);
				usage(argv[0]);
				ret = 0;
				break;

			case 'm':	/* mount point */
				++i;
				if (i < argc && argv[i])
					strncpy(opts.mount_point, argv[i],
						FILE_NAME_SIZE);
				else {
					fprintf(stderr,"Invalid mount point.\n"
						"Aborting.\n");
					ret = 0;
				}
				break;

			case 'n':	/* query */
				opts.query_only = true;
				break;

			case 'p':	/* permissions */
				++i;
				if (i < argc && argv[i]) {
					opts.perms = strtoul(argv[i],
								  NULL, 8);
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

			default:
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
}				/* ReadOptions */


/*
 * InitVolumeDiskHeader()
 *
 */
int InitVolumeDiskHeader(ocfs_vol_disk_hdr *volhdr, __u32 sect_size, __u64 vol_size,
			 __u64 * data_start_off)
{
    __u64 DataSize = 0;
    __u64 num_blocks = 0;
    __u32 VolHdrSize = 0;
    __u32 NodeConfSize = 0;
    __u32 NewConfSize = 0;
    __u32 PublishSize = 0;
    __u32 VoteSize = 0;
    __u32 BitmapSize = 0;
    __u64 BegFreeSize = 0;
    __u64 EndFreeSize = 0;
    __u32 tmpbitmap;
    __u32 block_size = opts.block_size;
    char *MountPoint = opts.mount_point;

/*
**                 node_cfg_off                  publ_off
** start_off            |                            |
**  |                   |              new_cfg_off   |
**  |Sector    Sector   |                      |     |
**  V  0         1-7    V                      V     V
**  ----------------------------------------------------------------------- ...
**  |       |  |  |  |  |   |                  |     |                    |
**  |Volume |  |  |  |  | 2 |OCFS_MAXIMUM_NODES|  4  | OCFS_MAXIMUM_NODES |
**  |Header | 1| 2| 3|  |sct|sectors           | sct | sectors            |
**  |       |  |  |  |  |   |                  |     |                    |
**  ----------------------------------------------------------------------- ...
**          |  |  |  |   <--- node_cfg_size -->
**          |  |  |  V
**          V  |  |  Not Used(4,5,6,7)
**   VolLabel+ |  V
**    VOL_LOCK | NMMAP+
**             V   NMLOCK
**    BitMap Lock
**
**                  
**                    bitmap_off
** vote_off               |                                     data_start_off 
**   |                    |                                              |
**   V                    V                                              V
** ..--------------------------------------------------------------------- ...
**   |                    |                      |                       |
**   | OCFS_MAXIMUM_NODES | OCFS_MAX_BITMAP_SIZE | OCFS_NUM_FREE_SECTORS |
**   | sectors            | sectors              | sectors               |
**   |                    |                      |                       |
** ..--------------------------------------------------------------------- ...
**
**
** If we define OCFS_MAX_DIRECTORY_ENTRIES to be 5000 files and keep 1024
** sectors free. The total disk space required by Ocfs is appx. 3MB.
*/

    memset(volhdr, 0, sizeof(ocfs_vol_disk_hdr));

    volhdr->minor_version = minor_version;
    volhdr->major_version = major_version;

    memcpy(volhdr->signature, OCFS_VOLUME_SIGNATURE,
	   strlen(OCFS_VOLUME_SIGNATURE));
    strncpy(volhdr->mount_point, MountPoint, MAX_MOUNT_POINT_LEN - 1);

    volhdr->device_size = vol_size;
    volhdr->start_off = 0;
    volhdr->num_nodes = OCFS_MAXIMUM_NODES;
    volhdr->root_size = 0;
    volhdr->cluster_size = (block_size * 1024);

    /* uid/gid/perms */
    volhdr->uid = opts.uid;
    volhdr->gid = opts.gid;
    volhdr->prot_bits = opts.perms;
    volhdr->excl_mount = NOT_MOUNTED_EXCLUSIVE;

    /* Calculate various sizes */
    VolHdrSize = 8 * sect_size;
    NodeConfSize = (2 + volhdr->num_nodes) * sect_size;
    NewConfSize = 4 * sect_size;
    PublishSize = volhdr->num_nodes * sect_size;
    VoteSize = volhdr->num_nodes * sect_size;
    BitmapSize = OCFS_MAX_BITMAP_SIZE;
    BegFreeSize = OCFS_NUM_FREE_SECTORS * sect_size;
    EndFreeSize = OCFS_NUM_END_SECTORS * sect_size;

    /* Fill in volhdr */
    volhdr->node_cfg_off = volhdr->start_off + VolHdrSize;
    volhdr->node_cfg_size = NodeConfSize;
    volhdr->new_cfg_off = volhdr->node_cfg_off + NodeConfSize;
    volhdr->publ_off = volhdr->new_cfg_off + NewConfSize;
    volhdr->vote_off = volhdr->publ_off + PublishSize;
    volhdr->bitmap_off = volhdr->vote_off + VoteSize;
    volhdr->root_off = 0;
    volhdr->root_bitmap_off = 0;
    volhdr->root_bitmap_size = 0;
    volhdr->data_start_off = volhdr->bitmap_off + BitmapSize + BegFreeSize;
#define OCFS_DATA_START_ALIGN		4096
    volhdr->data_start_off = OCFS_ALIGN(volhdr->data_start_off,
				       	OCFS_DATA_START_ALIGN);

    /* calculate number of data blocks */
    *data_start_off = volhdr->data_start_off;
    DataSize = volhdr->device_size - volhdr->data_start_off - EndFreeSize;

    num_blocks = DataSize / volhdr->cluster_size;
    tmpbitmap = OCFS_BUFFER_ALIGN(((num_blocks + 7) / 8), sect_size);
    if (tmpbitmap > BitmapSize)
    {
	fprintf(stderr, "%dKB block size is too small to format "
		"the entire disk.\nPlease specify a larger value.\n",
		block_size);
	return 0;
    }

    volhdr->num_clusters = num_blocks;

    return 1;
}				/* InitVolumeDiskHeader */


/*
 * InitVolumeLabel()
 *
 * Populate ocfs_vol_label
 */
void InitVolumeLabel(ocfs_vol_label * vollbl, __u32 sect_size, char *id,
		     __u32 id_len)
{
    /* Set the volume label */
    strcpy(vollbl->label, opts.volume_label);
    vollbl->label_len = strlen(opts.volume_label);

    /* Set the volume id */
    memcpy(vollbl->vol_id, id, id_len);
    vollbl->vol_id_len = id_len;

    /* Set the volume lock to invalid_master */
    vollbl->disk_lock.curr_master = -1;

    return;
}				/* InitVolumeLabel */


/*
 * SetNodeConfigHeader()
 *
 */
void SetNodeConfigHeader(ocfs_node_config_hdr * nodehdr)
{
    strcpy(nodehdr->signature, NODE_CONFIG_HDR_SIGN);

    nodehdr->version = NODE_CONFIG_VER;
    nodehdr->num_nodes = 0;
    nodehdr->disk_lock.curr_master = -1;
    nodehdr->last_node = 0;

    return;
}				/* SetNodeConfigHeader */


/*
 * ShowDiskHdrVals()
 *
 */
void ShowDiskHdrVals(ocfs_vol_disk_hdr * voldiskhdr)
{
    printf("signature        = %s\n"
	   "mount_point      = %s\n"
	   "serial_num       = %llu\n"
	   "device_size      = %llu\n"
	   "num_nodes        = %llu\n"
	   "cluster_size     = %llu\n"
	   "num_clusters     = %llu\n"
	   "start_off        = %llu\n"
	   "node_cfg_off     = %llu\n"
	   "node_cfg_size    = %llu\n"
	   "new_cfg_off      = %llu\n"
	   "publ_off         = %llu\n"
	   "vote_off         = %llu\n"
	   "bitmap_off       = %llu\n"
	   "root_bitmap_off  = %llu\n"
	   "root_bitmap_size = %llu\n"
	   "data_start_off   = %llu\n"
	   "root_off         = %llu\n"
	   "root_size        = %llu\n"
	   "dir_node_size    = %llu\n"
	   "file_node_size   = %llu\n"
	   "internal_off     = %llu\n"
	   "uid              = %u\n"
	   "gid              = %u\n"
	   "prot_bits        = %u\n"
	   "excl_mount       = %d\n",
	   voldiskhdr->signature,
	   voldiskhdr->mount_point,
	   voldiskhdr->serial_num,
	   voldiskhdr->device_size,
	   voldiskhdr->num_nodes,
	   voldiskhdr->cluster_size,
	   voldiskhdr->num_clusters,
	   voldiskhdr->start_off,
	   voldiskhdr->node_cfg_off,
	   voldiskhdr->node_cfg_size,
	   voldiskhdr->new_cfg_off,
	   voldiskhdr->publ_off,
	   voldiskhdr->vote_off,
	   voldiskhdr->bitmap_off,
	   voldiskhdr->root_bitmap_off,
	   voldiskhdr->root_bitmap_size,
	   voldiskhdr->data_start_off,
	   voldiskhdr->root_off,
	   voldiskhdr->root_size,
	   voldiskhdr->dir_node_size,
	   voldiskhdr->file_node_size,
	   voldiskhdr->internal_off,
	   voldiskhdr->uid,
	   voldiskhdr->gid,
	   voldiskhdr->prot_bits,
	   voldiskhdr->excl_mount);
    fflush(stdout);

    return;
}				/* ShowDiskHdrVals */


/*
 * HandleSignal()
 */
void HandleSignal(int sig)
{
	switch (sig) {
	case SIGTERM:
	case SIGINT:
		if (in_data_blocks && opts.clear_data_blocks) {
			signal(SIGTERM, SIG_IGN);
			signal(SIGINT, SIG_IGN);
			format_intr = true;
		} else {
			fprintf(stderr, "\nError: Volume not formatted due " 
				"to interruption.\n");
    			safeclose(file);
    			unbind_raw(rawminor);
			exit(1);
		}
		break;
	}
} /* HandleSignal */

