/*
 * Get label. Used by both mount and umount.
 */
#define _GNU_SOURCE 
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <endian.h>
#include <sys/stat.h>
#include <errno.h>

#include "linux_fs.h"
#include "get_label_uuid.h"

/*
 * See whether this device has (the magic of) a RAID superblock at the end.
 * If so, it probably is, or has been, part of a RAID array.
 *
 * For the moment this test is switched off - it causes problems.
 * "Checking for a disk label should only be done on the full raid,
 *  not on the disks that form the raid array. This test causes a lot of
 *  problems when run on my striped promise fasttrak 100 array."
 */

#if BYTE_ORDER == BIG_ENDIAN
#define INT32_FROM_LE(val)        ((unsigned int) ( \
    (((unsigned int) (val) & (unsigned int) 0x000000ffU) << 24) | \
    (((unsigned int) (val) & (unsigned int) 0x0000ff00U) <<  8) | \
    (((unsigned int) (val) & (unsigned int) 0x00ff0000U) >>  8) | \
    (((unsigned int) (val) & (unsigned int) 0xff000000U) >> 24)))
#else
#define INT32_FROM_LE(val) (val)
#endif

typedef struct {
	unsigned int md_magic;
} mdp_super_t;
#ifndef MD_SB_MAGIC
#define MD_SB_MAGIC		0xa92b4efc
#endif
#ifndef MD_RESERVED_BYTES
#define MD_RESERVED_BYTES 65536L
#endif
#ifndef MD_NEW_SIZE_BYTES
#define MD_NEW_SIZE_BYTES(x)		((x & ~(MD_RESERVED_BYTES - 1L)) - MD_RESERVED_BYTES)
#endif

static int
is_raid_partition(int fd)
{
        mdp_super_t mdsb;
        int n;
	struct stat sbuf;
 	if(fstat(fd, &sbuf))
	  return 2;
	if(!sbuf.st_size) {
		uint64_t bsize64;
		unsigned int bsize32;
		if(!ioctl(fd, BLKGETSIZE64, &bsize64))
			sbuf.st_size = bsize64;
		else if(!ioctl(fd, BLKGETSIZE, &bsize32))
			sbuf.st_size = bsize32;
	}
	if(!sbuf.st_size) return 3;
	/* hardcode 4096 here in various places,
	   because that's what it's defined to be.
	   Note that even if we used the actual kernel headers,
	   sizeof(mdp_super_t) is slightly larger in the 2.2 kernel on 64-bit
	   archs, so using that wouldn't work. */
	lseek(fd, MD_NEW_SIZE_BYTES(sbuf.st_size), SEEK_SET);
	n = 4096; if(sizeof(mdsb) < n) n = sizeof(mdsb);
        if(read(fd, &mdsb, n) != n)
	  return 4; /* error */
	mdsb.md_magic = INT32_FROM_LE(mdsb.md_magic);
	return (mdsb.md_magic == MD_SB_MAGIC); /* If this device has a
						  RAID superblock at
						  the end, it must be
						  part of a RAID
						  array. */
}

/* for now, only ext2, ext3, xfs, ocfs are supported */
int
get_label_uuid(const char *device, char **label, char *uuid) {
	int fd;
	int rv = 1;
	size_t namesize;
	struct ext2_super_block e2sb;
	struct xfs_super_block xfsb;
	struct jfs_super_block jfssb;
	struct ocfs_volume_header ovh;	/* Oracle */
	struct ocfs_volume_label olbl;
	union {
	  struct swap_header_v1_2 hdr;
	  char swap_data[1<<16];
	} swap_u;

	fd = open(device, O_RDONLY);
	if (fd < 0)
		return rv;

	/* If there is a RAID partition, or an error, ignore this partition */
	if (is_raid_partition(fd)) {
		close(fd);
		return rv;
	}

 	if(getpagesize() > sizeof(swap_u.swap_data)) {
 		errno = EFAULT;
 		return 1;
 	}

	if (lseek(fd, 1024, SEEK_SET) == 1024
	    && read(fd, (char *) &e2sb, sizeof(e2sb)) == sizeof(e2sb)
	    && (ext2magic(e2sb) == EXT2_SUPER_MAGIC)) {
		memcpy(uuid, e2sb.s_uuid, sizeof(e2sb.s_uuid));
		namesize = sizeof(e2sb.s_volume_name);
		if ((*label = calloc(namesize + 1, 1)) != NULL)
			memcpy(*label, e2sb.s_volume_name, namesize);
		rv = 0;
	}
	else if (lseek(fd, 0, SEEK_SET) == 0
	    && read(fd, (char *) &xfsb, sizeof(xfsb)) == sizeof(xfsb)
	    && (strncmp(xfsb.s_magic, XFS_SUPER_MAGIC, 4) == 0)) {
		memcpy(uuid, xfsb.s_uuid, sizeof(xfsb.s_uuid));
		namesize = sizeof(xfsb.s_fname);
		if ((*label = calloc(namesize + 1, 1)) != NULL)
			memcpy(*label, xfsb.s_fname, namesize);
		rv = 0;
	}
	else if (lseek(fd, 0, SEEK_SET) == 0
	    && read(fd, (char *) &ovh, sizeof(ovh)) == sizeof(ovh)
	    && (strncmp(ovh.signature, OCFS_MAGIC, sizeof(OCFS_MAGIC)) == 0)
	    && (lseek(fd, 512, SEEK_SET) == 512)
	    && read(fd, (char *) &olbl, sizeof(olbl)) == sizeof(olbl)) {
		uuid[0] = '\0';
		namesize = ocfslabellen(olbl);
		if ((*label = calloc(namesize + 1, 1)) != NULL)
			memcpy(*label, olbl.label, namesize);
		rv = 0;
	}
	else if (lseek(fd, JFS_SUPER1_OFF, SEEK_SET) == JFS_SUPER1_OFF
	    && read(fd, (char *) &jfssb, sizeof(jfssb)) == sizeof(jfssb)
	    && (strncmp(jfssb.s_magic, JFS_MAGIC, 4) == 0)) {
		if (assemble4le(jfssb.s_version) == 1) {
			/* old (OS/2 compatible) jfs filesystems don't
			   have UUIDs and only have a very small label. */
			memset(uuid, 0, 16);
			namesize = sizeof(jfssb.s_fpack);
			if ((*label = calloc(namesize + 1, 1)) != NULL)
				memcpy(*label, jfssb.s_fpack, namesize);
		} else {
			memcpy(uuid, jfssb.s_uuid, sizeof(jfssb.s_uuid));
			namesize = sizeof(jfssb.s_label);
			if ((*label = calloc(namesize + 1, 1)) != NULL)
			    memcpy(*label, jfssb.s_label, namesize);
		}
		rv = 0;
	}
	else if (lseek(fd, 0, SEEK_SET) == 0
		 && read(fd, &swap_u, getpagesize()) == getpagesize()
		 && !strncmp(swap_u.swap_data+getpagesize()-10, "SWAPSPACE2", strlen("SWAPSPACE2"))) {
	  memcpy(uuid, swap_u.hdr.uuid, sizeof(swap_u.hdr.uuid));
	  if(label)
	    *label = swap_u.hdr.volume_name[0]?strndup(swap_u.hdr.volume_name, sizeof(swap_u.hdr.volume_name)):NULL;
	  rv = 0;
	}
	  

	close(fd);
	return rv;
}
