/*
 * frmtport.c
 *
 * ocfs format utility functions
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

ocfs_global_ctxt OcfsGlobalCtxt;
__u32 debug_context = 0;
__u32 debug_level = 0;
__u32 debug_exclude = 0;

extern char *usage_string;
extern ocfs_options opts;

/*
 * MemAlloc()
 *
 */
void *MemAlloc(__u32 size)
{
    void *buf = NULL;

    if (!(buf = malloc_aligned(size)))
	fprintf(stderr, "Error allocating %u bytes of memory.\n", size);

    return buf;
}				/* MemAlloc */

/*
 * OpenDisk()
 *
 */
int OpenDisk(char *device)
{
    int file;
    mode_t oldmode;

    oldmode = umask(0000);
    file = open(device, O_RDWR | O_CREAT, 0777);
    umask(oldmode);
    if (file == -1)
    {
	fprintf(stderr, "Error opening device %s.\n%s\n", device,
		strerror(errno));
	file = 0;
    }

    return file;
}				/* OpenDisk */


/*
 * GetDiskGeometry()
 *
 */
int GetDiskGeometry(int file, __u64 * vollength, __u32 * sect_size)
{
    /* SM: should be signed... struct hd_struct.nr_sects */
    /* MS: the above only applies to 2.4.9 */
    unsigned long devicesize;
    int ret = 0;

    if (ioctl(file, BLKGETSIZE, &devicesize) == -1)
    {
	fprintf(stderr, "Error reading size of %s device.\n%s\n", opts.device,
		strerror(errno));
	goto bail;
    }

#if defined(USE_SECTOR_SIZE_IOCTL)
    if (ioctl(file, BLKSSZGET, sect_size) == -1)
    {
	fprintf(stderr, "Error reading the sector size for %s device.\n%s\n",
		opts.device, strerror(errno));
	goto bail;
    }
#else
    *sect_size = OCFS_SECTOR_SIZE;
#endif

    if (devicesize && *sect_size)
    {
	*vollength = devicesize;
	*vollength *= *sect_size;
	ret = 1;
    }
    else
	fprintf(stderr, "Invalid device specified %s\n", opts.device);

  bail:
    return ret;
}				/* GetDiskGeometry */


/*
 * SetSeek()
 *
 */
int SetSeek(int file, __u64 offset)
{
    off_t ext_offset;
    int ret = 0;

    ext_offset = (off_t) offset;

    if (lseek(file, ext_offset, SEEK_SET) == -1)
	fprintf(stderr, "Error setting file pointer to (%u).\n%s\n",
		ext_offset, strerror(errno));
    else
	ret = 1;

    return ret;
}				/* SetSeek */


/*
 * Read()
 *
 */
int Read(int file, __u32 size, char *buf)
{
    int ret = 0;

    if (read(file, buf, size) == -1)
	fprintf(stderr, "Error reading device %s.\n%s\n",
		opts.device, strerror(errno));
    else
	ret = 1;

    return ret;
}				/* Read */


/*
 * Write()
 *
 */
int Write(int file, __u32 size, char *buf)
{
    int ret = 0;

    if (write(file, buf, size) == -1)
	fprintf(stderr, "Error writing to device %s.\n%s\n", opts.device,
		strerror(errno));
    else
	ret = 1;

    return ret;
}				/* Write */


/*
 * GenerateVolumeID()
 *
 * Volume id is used by ocfs to identify the volume in ipc messages.
 */
int GenerateVolumeID(char *volid, int volidlen)
{
    int ret = 0;

    if (volidlen < MAX_VOL_ID_LENGTH)
    {
	fprintf(stderr, "internal error [%s], [%d]\n", __FILE__, __LINE__);
	goto bail;
    }

    /* Read random bytes */
    if (!GetRandom(volid, volidlen))
	goto bail;

    ret = 1;

  bail:
    return ret;
}				/* GenerateVolumeID */

/*
 * version()
 *
 */
void version(char *progname)
{
	printf("%s %s %s (build %s)\n", progname,
					OCFS_BUILD_VERSION,
					OCFS_BUILD_DATE,
					OCFS_BUILD_MD5);
	return;
}				/* version */

/*
 * usage()
 *
 */
void usage(char *progname)
{
	printf(usage_string, progname);
	return ;
}				/* usage */


/*
 * ValidateOptions()
 *
 */
int ValidateOptions(void)
{
	int valid_vals[] = {4, 8, 16, 32, 64, 128, 256, 512, 1024, 0};
	int i;

	if (opts.device[0] == '\0') {
		fprintf(stderr, "Error: Device not specified.\n");
		return 0;
	}

	for (i = 0; valid_vals[i]; ++i) {
		if (opts.block_size == valid_vals[i])
			break;
	}

	if (!valid_vals[i]) {
		fprintf(stderr, "Error: Invalid block size.\n");
		return 0;
	}

	if (opts.query_only)
		return 1;

	if (opts.volume_label[0] == '\0') {
		fprintf(stderr, "Error: Volume label not specified.\n");
		return 0;
	}

	if (opts.mount_point[0] == '\0') {
		fprintf(stderr, "Error: Mount point not specified.\n");
		return 0;
	}

	return 1;
}				/* ValidateOptions */


/*
 * GetRandom()
 *
 * Returns random bytes of length randlen in buffer randbuf.
 */
int GetRandom(char *randbuf, int randlen)
{
    int randfd = 0;
    int ret = 0;
    int readlen = 0;
    int len = 0;

    if ((randfd = open("/dev/urandom", O_RDONLY)) == -1)
    {
	fprintf(stderr, "Error opening /dev/urandom.\n%s\n", strerror(errno));
	goto bail;
    }

    if (randlen > 0)
    {
	while (readlen < randlen)
	{
	    if ((len =
		 read(randfd, randbuf + readlen, randlen - readlen)) == -1)
	    {
		fprintf(stderr, "Error reading /dev/urandom.\n%s\n",
			strerror(errno));
		goto bail;
	    }
	    readlen += len;
	}
    }

    ret = 1;

  bail:
    if (randfd > 0)
	close(randfd);

    return ret;
}				/* GetRandom */


/*
 * ReadPublish()
 *
 */
int ReadPublish(int file, __u64 publ_off, __u32 sect_size, void **buf)
{
	int ret = 0;
	__u32 pub_len;

	pub_len = OCFS_MAXIMUM_NODES * sect_size;

	/* Alloc memory to read publish sectors */
	if (!*buf) {
		if (!(*buf = MemAlloc(pub_len)))
			goto bail;
	}

	if (!SetSeek(file, publ_off))
		goto bail;

	/* read OCFS_MAXIMUM_NODES sectors starting publ_off */
	if (!(Read(file, pub_len, *buf)))
		goto bail;

	ret = 1;

      bail:
	return ret;
}				/* ReadPubllish */


/*
 * get_uid()
 *
 */
uid_t get_uid(char *id)
{
	struct passwd *pw = NULL;
	uid_t uid = 0;

	if (isdigit(*id))
		uid = atoi(id);
	else {
		pw = getpwnam(id);
		if (pw)
			uid = pw->pw_uid;
	}

	return uid;
}				/* get_uid */


/*
 * get_gid()
 *
 */
uid_t get_gid(char *id)
{
	struct group *gr = NULL;
	gid_t gid = 0;

	if (isdigit(*id))
		gid = atoi(id);
	else {
		gr = getgrnam(id);
		if (gr)
			gid = gr->gr_gid;
	}

	return gid;
}				/* get_gid */


/*
 * read_sectors()
 *
 */
int read_sectors(int file, __u64 strtoffset, __u32 noofsects, __u32 sect_size,
		 void *buf)
{
	__u32 i;
	char *p;
	int ret = 0;

	if (!SetSeek(file, strtoffset))
		goto bail;

	for (i = 0, p = buf; i < noofsects; ++i, p += sect_size) {
		if (!Read(file, sect_size, p))
			goto bail;
	}

	ret = 1;

      bail:
	return ret;
}				/* read_sectors */


/*
 * write_sectors()
 *
 */
int write_sectors(int file, __u64 strtoffset, __u32 noofsects, __u32 sect_size,
		  void *buf)
{
	__u32 i;
	char *p;
	int ret = 0;

	if (!SetSeek(file, strtoffset))
		goto bail;

	for (i = 0, p = buf; i < noofsects; ++i, p += sect_size) {
		if (!Write(file, sect_size, (void *)p))
			goto bail;
	}

	ret = 1;

      bail:
	return ret;
}				/* write_sectors */


/*
 * validate_volume_size()
 *
 */
int validate_volume_size(__u64 given_vol_size, __u64 actual_vol_size)
{
	int ret = 0;
	char str1[100], str2[100];

	if (given_vol_size < OCFS_MIN_VOL_SIZE) {
		num_to_str(given_vol_size, str1);
		num_to_str(OCFS_MIN_VOL_SIZE, str2);
		fprintf(stderr, "The size specified, %s, is smaller than "
			"the minimum size, %s.\nAborting.\n", str1, str2);
		goto bail;
	}

	if (given_vol_size > actual_vol_size) {
		num_to_str(given_vol_size, str1);
		num_to_str(actual_vol_size, str2);
		fprintf(stderr, "The size specified, %s, is larger than "
			"the device size, %s.\nAborting.\n",
			str1, str2);
		goto bail;
	}

	ret = 1;

      bail:
	return ret;
}				/* validate_volume_size */


/*
 * num_to_str()
 *
 */
void num_to_str(__u64 num, char *numstr)
{
	__u64 newnum;
	int i = 0;
	char append[] = " KMGT";
	int len;

	len = strlen(append);

	for (i = 0, newnum = num; i < len && newnum > 1023; ++i)
		newnum /= (__u64)1024;

	sprintf(numstr, "%llu%c", newnum, append[i]);

	return ;
}				/* num_to_str */


/*
 * is_ocfs_volume()
 *
 */
int is_ocfs_volume(int file, ocfs_vol_disk_hdr *volhdr, bool *ocfs_vol,
		   __u32 sect_size)
{
	char *buf;
	int ret = 0;

	buf = (char *)volhdr;
	memset(buf, 0, sect_size);

	if (!SetSeek(file, 0))
		goto bail;

	if (!(Read(file, sect_size, buf)))
		goto bail;

	volhdr = (ocfs_vol_disk_hdr *) buf;

	if ((!memcmp(volhdr->signature, OCFS_VOLUME_SIGNATURE,
		     strlen(OCFS_VOLUME_SIGNATURE))))
		*ocfs_vol = true;
	else
		*ocfs_vol = false;

	ret = 1;

      bail:
	return ret;
}				/* is_ocfs_volume */

/*
 * check_heart_beat()
 *
 */
int check_heart_beat(int *file, char *device, ocfs_vol_disk_hdr *volhdr,
		     __u32 *nodemap, __u32 sect_size)
{
	char *publish = NULL;
	ocfs_super osb;
	int ret = 0;
	int i;
	int waittime;

	memset (&osb, 0, sizeof(ocfs_super));

	if (!ReadPublish(*file, volhdr->publ_off, sect_size, (void **)&publish))
		goto bail;

	/* alloc osb and pop sect_size */
	osb.sect_size = sect_size;

        /* call ocfs_update_publish_map(first_time = true) */
	ocfs_update_publish_map (&osb, (void *)publish, true);

	/* sleep(OCFS_NM_HEARTBEAT_TIME * 10) */
	PRINT_VERBOSE("Checking heart beat on volume ");
	waittime = (OCFS_NM_HEARTBEAT_TIME/1000);
	waittime = (waittime ? waittime : 1);
	for (i = 0; i < OCFS_HBT_WAIT; ++i) {
		PRINT_VERBOSE(".");
		fflush(stdout);
		sleep(waittime);
	}
   
	/* Close and re-open device to force disk read */
	safeclose(*file);
	if (!(*file = OpenDisk(device)))
		goto bail;

	memset (publish, 0, sect_size);
	if (!ReadPublish(*file, volhdr->publ_off, sect_size, (void **)&publish))
		goto bail;

	/* call ocfs_update_publish_map(first_time = false) */
	ocfs_update_publish_map (&osb, (void *)publish, false);

	PRINT_VERBOSE("\r                                                \r");
	fflush(stdout);

	*nodemap = LO(osb.publ_map);

	ret = 1;
      bail:
	safefree(publish);
	return ret;
}				/* check_heart_beat */

/*
 * get_node_names()
 *
 */
int get_node_names(int file, ocfs_vol_disk_hdr *volhdr, char **node_names,
		   __u32 sect_size)
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
	for (i = 0; i < OCFS_MAXIMUM_NODES; ++i, p += sect_size) {
		conf = (ocfs_disk_node_config_info *)p;
		if (conf->node_name[0])
			node_names[i] = strdup(conf->node_name);
	}

	ret = 1;
      bail:
	safefree(buf);
	return ret;
}				/* get_node_names */


/*
 * print_node_names()
 *
 */
void print_node_names(char **node_names, __u32 nodemap)
{
	int i, j;
	char comma = '\0';

	for (j = 1, i = 0; i < OCFS_MAXIMUM_NODES; ++i, j <<= 1) {
		if (nodemap & j) {
			if (node_names[i])
				printf("%c %s", comma, node_names[i]);
			else
				printf("%c %d", comma, i);
			comma = ',';
		}
	}
	printf("\n");
}				/* print_node_names */

