/*
 *
 * this is a temporary version of mkfs.ocfs2 to get us through for now
 *
 */


#define _LARGEFILE64_SOURCE
#define __USE_ISOC99


#include <errno.h>
#include <stdio.h>
#include <asm/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include <asm/bitops.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#define _GNU_SOURCE
#include <getopt.h>
#include "ocfs2_fs.h"
#include "ocfs2_disk_dlm.h"
#include "ocfs1_fs_compat.h"

typedef unsigned short kdev_t;

#include "kernel-list.h"

#include <signal.h>
#include <libgen.h>

#include <netinet/in.h>
#include "kernel-jbd.h"


extern char *optarg;
extern int optind, opterr, optopt;
extern void * memalign (size_t __alignment, size_t __size);

#warning eeeek need to implement these
#define cpu_to_le16(x)		(x)
#define cpu_to_le32(x)		(x)
#define cpu_to_le64(x)		(x)
#define le16_to_cpu(x)		(x)
#define le32_to_cpu(x)		(x)
#define le64_to_cpu(x)		(x)


#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#define BITCOUNT(x)     (((BX_(x)+(BX_(x)>>4)) & 0x0F0F0F0F) % 255)
#define BX_(x)          ((x) - (((x)>>1)&0x77777777) \
		             - (((x)>>2)&0x33333333) \
			     - (((x)>>3)&0x11111111))

#define MKFS_FATAL(fmt, arg...)		({ fprintf(stderr, "ERROR at %s, %d: " fmt ".  EXITING!!!\n", \
						   __FILE__, __LINE__, ##arg);  \
					   exit(1); \
					 })
#define MKFS_FATAL_STR(str)	MKFS_FATAL(str, "")
#define MKFS_WARN(fmt, arg...)		fprintf(stderr, "WARNING at %s, %d: " fmt ".\n", \
						   __FILE__, __LINE__, ##arg)
#define MKFS_WARN_STR(str)	MKFS_WARN(str, "")



#define MIN_RESERVED_TAIL_BLOCKS    8

#define LEADING_SPACE_BLOCKS	2  // we will put special strings in the v1 header blocks
#define SLOP_BLOCKS		0
#define FILE_ENTRY_BLOCKS	8
#define SUPERBLOCK_BLOCKS	1
#define PUBLISH_BLOCKS(i,min)	(i<min ? min : i)  // at least min
#define VOTE_BLOCKS(i,min)	(i<min ? min : i)  // at least min
#define AUTOCONF_BLOCKS(i,min)	((2+4) + (i<min ? min : i))  // at least 32, plus the other 6
#define NUM_LOCAL_SYSTEM_FILES  6

#define MAGIC_SUPERBLOCK_BLOCK_NUMBER  2

#define OCFS2_OS_LINUX           0
#define OCFS2_OS_HURD            1
#define OCFS2_OS_MASIX           2
#define OCFS2_OS_FREEBSD         3
#define OCFS2_OS_LITES           4

#define OCFS2_DFL_MAX_MNT_COUNT          20      /* Allow 20 mounts */
#define OCFS2_DFL_CHECKINTERVAL          0       /* Don't use interval check */

enum {
	sfi_journal,
	sfi_bitmap,
	sfi_local_alloc,
	sfi_dlm,
	sfi_other
};	

typedef struct _system_file_info {
	char *name;
	int type;
	int global;
	int dir;

} system_file_info;

system_file_info system_files[] = {
	{ "bad_blocks", sfi_other, 1, 0 },
	{ "global_inode_alloc", sfi_other, 1, 0 },
	{ "global_inode_alloc_bitmap", sfi_bitmap, 1, 0 },
	{ "dlm", sfi_dlm, 1, 0 },
	{ "global_bitmap", sfi_bitmap, 1, 0 },
	{ "orphan_dir", sfi_other, 1, 1 },
	{ "extent_alloc:%04d", sfi_other, 0, 0 },
	{ "extent_alloc_bitmap:%04d", sfi_bitmap, 0, 0 },
	{ "inode_alloc:%04d", sfi_other, 0, 0 },
	{ "inode_alloc_bitmap:%04d", sfi_bitmap, 0, 0 },
	{ "journal:%04d", sfi_journal, 0, 0 },
	{ "local_alloc:%04d", sfi_local_alloc, 0, 0 },
};

struct bitinfo {
	__u32 used_bits;
	__u32 total_bits;
};

typedef struct _system_file_disk_record
{
	__u64 fe_off;
	__u64 extent_off;
	__u64 extent_len;
	__u64 file_size;
	struct bitinfo bi;
	int flags;
	int links;
	int dir;
} system_file_disk_record;
	

typedef struct _alloc_bm
{
	void *buf;
	__u32 valid_bits;
	__u32 unit;
	__u32 unit_bits;
	char *name;
	__u64 fe_disk_off;
	system_file_disk_record *bm_record;
	system_file_disk_record *alloc_record;
} alloc_bm;

typedef struct _funky_dir
{
	__u64 disk_off;
	__u64 disk_len;
	void *buf;
	int buf_len;
	int last_off;
	__u64 fe_disk_off;
	int link_count;
	system_file_disk_record *record;
} funky_dir;

alloc_bm * initialize_bitmap (__u32 bits, __u32 unit_bits, char *name,
			      system_file_disk_record *bm_rec,
			      system_file_disk_record *alloc_rec);
void destroy_bitmap (alloc_bm *bm);
int find_clear_bits (alloc_bm * bitmap, __u32 numBits, __u32 offset);
int count_bits (alloc_bm * bitmap);
int alloc_bytes_from_bitmap (__u64 bytes, alloc_bm *bm, __u64 *start, __u64 *num);
int alloc_from_bitmap (__u64 numbits, alloc_bm *bm, __u64 *start, __u64 *num);
__u64 alloc_inode (int numblocks);
funky_dir * alloc_directory(void);
void add_entry_to_directory(funky_dir *dir, char *name, __u64 ino, __u8 type);
void adjust_volume_size(void);
void map_device(void);
void sync_device(void);
void unmap_device(void);
void init_format_time(void);
void format_superblock(system_file_disk_record *rec, system_file_disk_record *root_rec, system_file_disk_record *sys_rec);
void format_file(system_file_disk_record *rec);
void write_bitmap_data(alloc_bm *bm);
void write_directory_data(funky_dir *dir);
void format_leading_space(__u64 start);
void init_device(void);
void init_globals(void);
void usage(void);
void process_args(int argc, char **argv);
void generate_uuid(void);
static inline __u32 blocks_needed(void);
static inline __u32 system_dir_blocks_needed(void);
void replacement_journal_create(__u64 journal_off);
void adjust_autoconfig_publish_vote(system_file_disk_record *autoconfig_rec,
					system_file_disk_record *publish_rec,
					system_file_disk_record *vote_rec);
void write_autoconfig_header(system_file_disk_record *rec);
void init_record(system_file_disk_record *rec, int type, int dir);
void version(char *progname);



system_file_disk_record *record[NUM_SYSTEM_INODES];
// these 4 do not live in the record[] array
system_file_disk_record global_alloc_rec;  	// represents whole volume, not written to disk
system_file_disk_record superblock_rec;  	
system_file_disk_record root_dir_rec;
system_file_disk_record system_dir_rec;


__u32 pagesize_bits=0;
__u32 blocksize_bits=0;
__u32 cluster_size_bits=0;
__u32 blocksize=0;
__u32 cluster_size=0;
__u64 volume_size_in_bytes=0;
__u32 volume_size_in_clusters=0;
__u64 volume_size_in_blocks=0;
__u64 reserved_tail_size=0;
__u32 compat_flags = 0;
int initial_nodes=0;
int fd=-1;
void *mapping;
char *dev_name = NULL;
char *vol_label = NULL;
char *uuid = NULL;
gid_t default_gid = 0;
mode_t default_mode = 0;
uid_t default_uid = 0;
alloc_bm *global_bm=NULL;
alloc_bm *system_bm=NULL;
char *progname = NULL;
time_t format_time;



alloc_bm * initialize_bitmap (__u32 bits, __u32 unit_bits, char *name,
			      system_file_disk_record *bm_rec,
			      system_file_disk_record *alloc_rec)
{
	alloc_bm *bitmap;
	__u64 bitmap_len = bm_rec->extent_len;
	
	bitmap = malloc(sizeof(alloc_bm));
	if (bitmap == NULL)
		MKFS_FATAL("could not allocate memory for %s\n", name);
	memset(bitmap, 0, sizeof(alloc_bm));
	
	bitmap->buf = memalign(blocksize, bitmap_len);
	memset(bitmap->buf, 0, bitmap_len);

	bitmap->valid_bits = bits;
	bitmap->unit_bits = unit_bits;
	bitmap->unit = 1 << unit_bits;
	bitmap->name = strdup(name);

	bm_rec->file_size = bitmap_len;
	bm_rec->fe_off = 0ULL; // set later
	bm_rec->bi.used_bits = 0;
	bm_rec->bi.total_bits = bits;

	alloc_rec->file_size = bits << unit_bits;
	alloc_rec->fe_off = 0ULL; // set later

	bitmap->bm_record = bm_rec;
	bitmap->alloc_record = alloc_rec;

	return bitmap;
}

void destroy_bitmap (alloc_bm *bm)
{
	free(bm->buf);
	free(bm);
}


int find_clear_bits (alloc_bm * bitmap, __u32 numBits, __u32 offset)
{
	__u32 next_zero, off, count, size, first_zero = -1; 
	void *buffer;

	buffer = bitmap->buf;
	size = bitmap->valid_bits;
	count = 0;
	off = offset;

	while ((size - off + count >= numBits) &&
	       (next_zero = find_next_zero_bit (buffer, size, off)) != size) {
                if (next_zero >= bitmap->valid_bits)
                    break;

		if (next_zero != off) {
			first_zero = next_zero;
			off = next_zero + 1;
			count = 0;
		} else {
			off++;
			if (count == 0)
				first_zero = next_zero;
		}

		count++;

		if (count == numBits)
			goto bail;
	}
	first_zero = -1;

      bail:
	if (first_zero != (__u32)-1 && first_zero > bitmap->valid_bits) {
		fprintf(stderr, "um... first_zero>bitmap->valid_bits (%d > %d)",
			       first_zero, bitmap->valid_bits);
		first_zero = -1;
	}
	return first_zero;
}

int count_bits (alloc_bm * bitmap)
{
	__u32 size, count = 0, off = 0;
	unsigned char tmp;
	__u8 *buffer;

	buffer = bitmap->buf;

	size = (bitmap->valid_bits >> 3);

	while (off < size) {
		memcpy (&tmp, buffer, 1);
		count += BITCOUNT (tmp);
		off++;
		buffer++;
	}
	return count;
}


/* returns bytes to avoid any confusion */
int alloc_bytes_from_bitmap (__u64 bytes, alloc_bm *bm, __u64 *start, __u64 *num)
{
	__u32 startbit = 0, numbits = 0;
	char *p;

	numbits = (bytes + bm->unit - 1) >> bm->unit_bits;
	startbit = find_clear_bits (bm, numbits, 0);
	if (startbit == (__u32)-1)
		MKFS_FATAL("could not allocate %llu bits from %s bitmap\n", 
			   numbits, bm->name);
	*start = ((__u64)startbit) << bm->unit_bits;
	*num = ((__u64)numbits) << bm->unit_bits;
	bm->bm_record->bi.used_bits += numbits;
	p = mapping + *start;
	memset(p, 0, *num);
	while (numbits--)
		set_bit (startbit++, bm->buf);
	return 0;
}

/* returns bytes to avoid any confusion */
int alloc_from_bitmap (__u64 numbits, alloc_bm *bm, __u64 *start, __u64 *num)
{
	__u32 startbit = 0;
	char *p;
	
	startbit = find_clear_bits (bm, numbits, 0);
	if (startbit == (__u32)-1)
		MKFS_FATAL("could not allocate %llu bits from %s bitmap\n", 
			   numbits, bm->name);
	*start = ((__u64)startbit) << bm->unit_bits;
	*num = ((__u64)numbits) << bm->unit_bits;
	bm->bm_record->bi.used_bits += numbits;
	p = mapping + *start;
	memset(p, 0, *num);
	while (numbits--)
		set_bit (startbit++, bm->buf);
	return 0;
}

__u64 alloc_inode (int numblocks)
{
	__u64 ret, num;
	alloc_from_bitmap (numblocks, system_bm, &ret, &num);
	return ret;
}

funky_dir * alloc_directory(void)
{
	funky_dir *dir;

	dir = malloc(sizeof(funky_dir));
	if (!dir)
		MKFS_FATAL_STR("could not allocate memory for directory");
	memset(dir, 0, sizeof(funky_dir));
	return dir;
}
	
void add_entry_to_directory(funky_dir *dir, char *name, __u64 byte_off, __u8 type)
{
	struct ocfs2_dir_entry *de, *de1;
	int new_rec_len;
	void *newbuf, *p;
	int newsize, reclen, reallen;
	
	new_rec_len = OCFS2_DIR_REC_LEN(strlen(name));

	if (dir->buf) {
		de = (struct ocfs2_dir_entry *)(dir->buf + dir->last_off);
		reclen = le16_to_cpu(de->rec_len);
		reallen = OCFS2_DIR_REC_LEN(de->name_len);

		/* find an area with large enough reclen */
		if ((le64_to_cpu(de->inode) == 0 && reclen >= new_rec_len) ||
		    (reclen >= reallen + new_rec_len)) {
			if (le64_to_cpu(de->inode)) {
				// move ahead just past the last entry
				de1 = (struct ocfs2_dir_entry *) ((char *) de + reallen);
				// set the next entry's rec_len to the rest of the block
				de1->rec_len = cpu_to_le16(le16_to_cpu(de->rec_len) - reallen);
				// shorten the last entry
				de->rec_len = cpu_to_le16(reallen);  
				de = de1;
			}
			goto got_it;
		}
		/* no space, add more */
		newsize = dir->record->file_size + blocksize; // add one block
	} else
		newsize = blocksize;  // add one block
	
	newbuf = memalign(blocksize, newsize);
	if (newbuf == NULL) 
		MKFS_FATAL_STR("failed to grow directory");

	if (dir->buf) {
		memcpy(newbuf, dir->buf, dir->record->file_size);
		free(dir->buf);
		p = newbuf + dir->record->file_size;
		memset(p, 0, blocksize);
	} else {
		p = newbuf;
		memset(newbuf, 0, newsize);
	}

	dir->buf = newbuf;
	dir->record->file_size = newsize;

	de = (struct ocfs2_dir_entry *)p;
	de->inode = 0;
	de->rec_len = cpu_to_le16(blocksize);

got_it:
	de->name_len = strlen(name);
	de->inode = cpu_to_le64(byte_off >> blocksize_bits);
	de->file_type = type;
	strcpy(de->name, name);
	dir->last_off = ((char *)de - (char *)dir->buf);
	if (type == OCFS2_FT_DIR)
		dir->record->links++;
}



#define SYSTEM_FILE_NAME_MAX   40

static inline __u32 blocks_needed(void)
{
	__u32 num;
	
	/* 
	 * leading space ???
	 * superblock
	 * global bm fe
	 * system bm fe
	 * system alloc fe
	 * root inode fe
	 * system inode fe
	 * autoconf fe
	 * publish fe
	 * vote fe
	 * autoconf sectors
	 * publish sectors
	 * vote sectors
	 * (extent_alloc, extent_alloc_bitmap, inode_alloc, 
	 *    inode_alloc_bitmap, journal) x initial_nodes
	 * slop ;-)
         */
	num = LEADING_SPACE_BLOCKS;
	num += SUPERBLOCK_BLOCKS;
	num += FILE_ENTRY_BLOCKS;
	num += AUTOCONF_BLOCKS(initial_nodes, 32);
	num += PUBLISH_BLOCKS(initial_nodes, 32);
	num += VOTE_BLOCKS(initial_nodes, 32);
       	num += (initial_nodes * NUM_LOCAL_SYSTEM_FILES);
	num += SLOP_BLOCKS;
	return num;
}

static inline __u32 system_dir_blocks_needed(void)
{
	int bytes_needed = 0;
	int each = OCFS2_DIR_REC_LEN(SYSTEM_FILE_NAME_MAX);
	int entries_per_block = blocksize / each;
	
	/* blocks_needed() is way more than the number of filenames... */
	bytes_needed = (blocks_needed() + entries_per_block - 1 / entries_per_block) << blocksize_bits;
	return (bytes_needed + cluster_size - 1) >> cluster_size_bits;
}

void adjust_volume_size()
{
	__u32 max;
	__u64 vsize = volume_size_in_bytes - 
		(MIN_RESERVED_TAIL_BLOCKS << blocksize_bits);

	max = MAX(pagesize_bits, blocksize_bits);
	max = MAX(max, cluster_size_bits);
	vsize >>= max;
	vsize <<= max;
	volume_size_in_blocks = vsize >> blocksize_bits;
	volume_size_in_clusters = vsize >> cluster_size_bits;
	reserved_tail_size = volume_size_in_bytes - vsize;
	volume_size_in_bytes = vsize;
}

static inline size_t mmap_len(void);

/* total guess */
static inline size_t mmap_len(void)
{
	size_t ret;

	ret = initial_nodes * OCFS2_DEFAULT_JOURNAL_SIZE;
	ret += (40 * ONE_MEGA_BYTE);
	return ret;
}

void map_device()
{
	mapping = mmap(NULL, mmap_len(), PROT_READ | PROT_WRITE, MAP_NORESERVE | MAP_SHARED, fd, 0);
	if (mapping==MAP_FAILED)
		MKFS_FATAL("could not mmap the device: %s", strerror(errno));
}
void sync_device()
{
	if (msync(mapping, mmap_len(), MS_SYNC))
		MKFS_FATAL_STR("could not sync the device");
}

void unmap_device()
{
	if (munmap(mapping, mmap_len()))
		MKFS_FATAL_STR("could not munmap the device");
}


void init_format_time()
{
	format_time = time(NULL);
}



void format_superblock(system_file_disk_record *rec, system_file_disk_record *root_rec, system_file_disk_record *sys_rec)
{
	ocfs2_dinode *di;
	__u64 super_off = rec->fe_off;

	di = mapping + super_off;
	memset(di, 0, blocksize);

	/* many of these fields will be unused for now, but at least
	 * let's init them to some sane values */

	strcpy (di->i_signature, OCFS2_SUPER_BLOCK_SIGNATURE);
	di->i_suballoc_node = cpu_to_le16((__u16)-1);
	di->i_suballoc_blkno = cpu_to_le64(super_off >> blocksize_bits);

	di->i_atime = 0; // unused
	di->i_ctime = cpu_to_le64(format_time); // use this as s_wtime (write time)
	di->i_mtime = cpu_to_le64(format_time); // use this as s_mtime (mount time)
	di->i_blkno = cpu_to_le64(super_off >> blocksize_bits);
	di->i_flags = cpu_to_le32(OCFS2_VALID_FL | OCFS2_SYSTEM_FL | OCFS2_SUPER_BLOCK_FL);
	di->id2.i_super.s_major_rev_level = cpu_to_le16(OCFS2_MAJOR_REV_LEVEL);
	di->id2.i_super.s_minor_rev_level = cpu_to_le16(OCFS2_MINOR_REV_LEVEL);
	di->id2.i_super.s_root_blkno = cpu_to_le64(root_rec->fe_off >> blocksize_bits);
	di->id2.i_super.s_system_dir_blkno = cpu_to_le64(sys_rec->fe_off >> blocksize_bits);
	di->id2.i_super.s_mnt_count = 0;
	di->id2.i_super.s_max_mnt_count = cpu_to_le16(OCFS2_DFL_MAX_MNT_COUNT);
	di->id2.i_super.s_state = 0;
	di->id2.i_super.s_errors = 0;
	di->id2.i_super.s_lastcheck = cpu_to_le64(format_time);
	di->id2.i_super.s_checkinterval = cpu_to_le32(OCFS2_DFL_CHECKINTERVAL);
	di->id2.i_super.s_creator_os = cpu_to_le32(OCFS2_OS_LINUX);
	di->id2.i_super.s_blocksize_bits = cpu_to_le32(blocksize_bits);
	di->id2.i_super.s_clustersize_bits = cpu_to_le32(cluster_size_bits);
	di->id2.i_super.s_max_nodes = cpu_to_le32(initial_nodes);
	if (strlen(vol_label) > 63)
		MKFS_FATAL_STR("volume label > 63 bytes long");
	strcpy(di->id2.i_super.s_label, vol_label);
	memcpy(di->id2.i_super.s_uuid, uuid, 16);
}


void format_file(system_file_disk_record *rec)
{
	ocfs2_dinode *di;
	int mode;
	__u32 clusters;

	if (default_mode)
		mode = default_mode | (rec->dir ? S_IFDIR : S_IFREG);
	else
		mode = rec->dir ? 0755 | S_IFDIR: 0644 | S_IFREG;
	
	clusters = (rec->extent_len + cluster_size - 1) >> cluster_size_bits;

	di = mapping + rec->fe_off;
	memset(di, 0, blocksize);
	strcpy (di->i_signature, OCFS2_INODE_SIGNATURE);
	di->i_generation = 0;
	di->i_suballoc_node = cpu_to_le16(-1);
	di->i_suballoc_blkno = cpu_to_le64(rec->fe_off >> blocksize_bits);
	di->i_blkno = cpu_to_le64(rec->fe_off >> blocksize_bits);
	di->i_uid = cpu_to_le32(default_uid);
	di->i_gid = cpu_to_le32(default_gid);
	di->i_size = cpu_to_le64(rec->file_size);
	di->i_mode = cpu_to_le16(mode);
	di->i_links_count = cpu_to_le16(rec->links);
	di->i_flags = cpu_to_le32(rec->flags);
	di->i_atime = di->i_ctime = di->i_mtime = cpu_to_le64(format_time);
	di->i_dtime = 0;
	di->i_clusters = cpu_to_le32(clusters);
	if (rec->flags & OCFS2_LOCAL_ALLOC_FL) {
		di->id2.i_lab.la_size = 
			cpu_to_le16(ocfs2_local_alloc_size(blocksize));
		return;
	} 

	if (rec->flags & OCFS2_BITMAP_FL) {
		di->id1.bitmap1.i_used = cpu_to_le32(rec->bi.used_bits);
		di->id1.bitmap1.i_total = cpu_to_le32(rec->bi.total_bits);
	} 

	di->id2.i_list.l_count = cpu_to_le16(ocfs2_extent_recs_per_inode(blocksize));
	di->id2.i_list.l_next_free_rec = cpu_to_le16(0);
	di->id2.i_list.l_tree_depth = cpu_to_le16(0);

	if (rec->extent_len) {
		di->id2.i_list.l_next_free_rec = cpu_to_le16(1);
		di->id2.i_list.l_recs[0].e_cpos = 0;
		di->id2.i_list.l_recs[0].e_clusters = cpu_to_le32(clusters);
		di->id2.i_list.l_recs[0].e_blkno = cpu_to_le64(rec->extent_off >> blocksize_bits);
	}
}

void write_bitmap_data(alloc_bm *bm)
{
	system_file_disk_record *rec = bm->bm_record;
	memset(mapping + rec->extent_off, 0, rec->extent_len);
	memcpy(mapping + rec->extent_off, bm->buf, rec->file_size);
}

void write_directory_data(funky_dir *dir)
{
	system_file_disk_record *rec = dir->record;
	memset(mapping + rec->extent_off, 0, rec->extent_len);
	memcpy(mapping + rec->extent_off, dir->buf, rec->file_size);
}

void format_leading_space(__u64 start)
{
	int num_blocks = 2;  // 2 blocks were allocated
	ocfs1_vol_disk_hdr *hdr;
	ocfs1_vol_label *lbl;
	char *p;
	
	p = mapping + start;
	memset(p, 2, num_blocks << blocksize_bits);
	
	hdr = (ocfs1_vol_disk_hdr *)p;
	strcpy(hdr->signature, "this is an ocfs2 volume");
	strcpy(hdr->mount_point, "this is an ocfs2 volume");

	p += 512;
	lbl = (ocfs1_vol_label *)p;
	strcpy(lbl->label, "this is an ocfs2 volume");
	strcpy(lbl->cluster_name, "this is an ocfs2 volume");
}

void replacement_journal_create(__u64 journal_off)
{
	journal_superblock_t *sb;
	char *p;

	p = mapping + journal_off;
	/* zero out all 8mb and stamp this little sb header on it */
	sb = (journal_superblock_t *) p;
	memset(sb, 0, OCFS2_DEFAULT_JOURNAL_SIZE);

	sb->s_header.h_magic	 = htonl(JFS_MAGIC_NUMBER);
	sb->s_header.h_blocktype = htonl(JFS_SUPERBLOCK_V2);
	sb->s_blocksize	= htonl(blocksize);
	sb->s_maxlen	= htonl(OCFS2_DEFAULT_JOURNAL_SIZE >> blocksize_bits);
	if (blocksize == 512)
		sb->s_first	= htonl(2);
	else
		sb->s_first	= htonl(1);
	sb->s_start     = htonl(1);
	sb->s_sequence  = htonl(1);
	sb->s_errno     = htonl(0);
}

void init_device(void)
{
	fd = open(dev_name, O_RDWR);
	if (fd == -1)
		MKFS_FATAL("could not open device %d for read/write", dev_name);
}


#define ONE_GB_SHIFT    30

int initial_nodes_for_volume(__u64 size);

/* this is just silly guesswork if the user does not
 * provide a number for initial_nodes */
int initial_nodes_for_volume(__u64 size)
{
	int shift = ONE_GB_SHIFT;
	int ret, i;

	/*
	 * <1gb    ->  2 nodes
	 * <8gb    ->  4 nodes
	 * <64gb   ->  8 nodes
	 * <512gb  -> 16 nodes
	 * 512+gb  -> 32 nodes
	 */
 	
	for (i=0, shift = ONE_GB_SHIFT; i<4; i++, shift += 3) {
		size >>= shift;
		if (!size)
			break;
	}
	switch (i)
	{
		case 0:
			ret = 2;
			break;
		case 1:
			ret = 4;
			break;
		case 2:
			ret = 8;
			break;
		case 3:
			ret = 16;
			break;
		default:
			ret = 32;
			break;
	}
      	return ret;
}

void init_globals(void)
{
	size_t pagesize;
	int i;
	__u32 tmp;
	unsigned long long tmp2;
	
	pagesize = getpagesize();
	pagesize_bits = 0;
	for (i=32; i>=0; i--) {
		if (pagesize == (1U << i))
			pagesize_bits = i;
	}
	if (!pagesize_bits)
		MKFS_FATAL("could not get pagesize_bits for pagesize %d", pagesize);

	if (blocksize) {
		printf("blocksize was set manually: %lu\n", blocksize);
	} else {
		if (ioctl(fd, BLKSSZGET, &tmp) == -1)
			MKFS_FATAL_STR("could not get sector size for device");
		blocksize = tmp;
	}
	blocksize_bits = 0;
	for (i=32; i>=0; i--) {
		if ((1U << i) == blocksize)
			blocksize_bits = i;
	}
	if (!blocksize_bits)
		MKFS_FATAL("could not get blocksize_bits for blocksize %lu", blocksize);

	cluster_size_bits = 0;
	for (i=32; i>=0; i--) {
		if ((1U << i) == cluster_size)
			cluster_size_bits = i;
	}
	if (!cluster_size_bits)
		MKFS_FATAL("could not get cluster_size_bits for cluster_size %lu", cluster_size);

	/* these will be readjusted later */
	tmp2 = lseek64(fd, 0, SEEK_END);
	if (volume_size_in_bytes) {
		printf("volume size was set manually: %llu, real size: %llu\n", 
		       volume_size_in_bytes, tmp2);
	} else {
		volume_size_in_bytes = tmp2;
	}
	volume_size_in_clusters = volume_size_in_bytes >> cluster_size_bits;
	volume_size_in_blocks = (volume_size_in_clusters << cluster_size_bits) >> blocksize_bits;
	reserved_tail_size = 0;

	if (initial_nodes) {
		if (initial_nodes < 2 || initial_nodes > OCFS2_MAX_NODES)
			MKFS_FATAL("initial_nodes given (%lu) out of range", initial_nodes);
		printf("initial_nodes was set manually: %lu\n", initial_nodes);
	} else {
		initial_nodes = initial_nodes_for_volume(volume_size_in_bytes);
		printf("using %lu for initial_nodes\n", initial_nodes);
	}
}

void generate_uuid(void)
{
	int randfd = 0;
	int readlen = 0;
	int len = 0;

	if ((randfd = open("/dev/urandom", O_RDONLY)) == -1)
		MKFS_FATAL("error opening /dev/urandom: %s", strerror(errno));

	uuid = malloc(MAX_VOL_ID_LENGTH);
	if (!uuid)
		MKFS_FATAL_STR("could not allocate memory");

	while (readlen < MAX_VOL_ID_LENGTH)
	{
		if ((len = read(randfd, uuid + readlen, MAX_VOL_ID_LENGTH - readlen)) == -1)
			MKFS_FATAL("error reading from /dev/urandom: %s", strerror(errno));
		readlen += len;
	}
	
	close(randfd);
}


void usage(void)
{
	// "b:c:v:C:n:g:u:m:d:l:U:"
	fprintf(stderr, "usage: mkfs2 [--blocksize=bytes] [--mode=##] [--uuid=id]\n");
	fprintf(stderr, "             [--volumesize=bytes] [--compatflags=##]\n");
	fprintf(stderr, "             [--nodes=##] [--gid=##] [--uid=##]\n");
	fprintf(stderr, "             --clustersize=bytes --device=/dev/name\n");
	fprintf(stderr, "             --label=\"volume label\"\n");
	fprintf(stderr, "\n");
	exit(1);
}

void process_args(int argc, char **argv)
{
	int c;

	while (1) {
		static struct option long_options[] = {
			{"blocksize", 1, 0, 'b'},
			{"clustersize", 1, 0, 'c'},
			{"volumesize", 1, 0, 'v'},
			{"compatflags", 0, 0, 'C'},
			{"nodes", 1, 0, 'n'},
			{"gid", 1, 0, 'g'},
			{"uid", 1, 0, 'u'},
			{"mode", 1, 0, 'm'},
			{"device", 1, 0, 'd'},
			{"label", 1, 0, 'l'},
			{"uuid", 1, 0, 'U'},
			{0, 0, 0, 0}
		};
		c = getopt_long (argc, argv, "b:c:v:c:n:g:u:m:d:l:U:", long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
			case 'b':
				blocksize = strtoul(optarg, NULL, 10);
				break;
			case 'c':
				cluster_size = strtoul(optarg, NULL, 10);
				break;
			case 'v':
				volume_size_in_bytes = strtoull(optarg, NULL, 10);
				break;
			case 'C':
				compat_flags = strtoul(optarg, NULL, 10);
				break;
			case 'n':
				initial_nodes = strtoul(optarg, NULL, 10);
				break;
			case 'g':
				default_gid = strtoul(optarg, NULL, 10);
				break;
			case 'u':
				default_uid = strtoul(optarg, NULL, 10);
				break;
			case 'm':
				default_mode = strtoul(optarg, NULL, 0);
				break;
			case 'd':
				dev_name = strdup(optarg);
				break;
			case 'l':
				vol_label = strdup(optarg);
				break;
			case 'U':
				uuid = strdup(optarg);
				break;
			case '?':
			default:
				usage();
				break;
		}
	}

	if (optind < argc) {
		if (dev_name)
			free(dev_name);
		dev_name = strdup(argv[optind]);
	}
	if (!vol_label) {
		MKFS_WARN_STR("you must give a volume label");
		usage();
	}
	if (!dev_name) {
		MKFS_WARN_STR("you must give a volume label");
		usage();
	}
	if (!cluster_size) {
		MKFS_WARN_STR("you must give a cluster size");
		usage();
	}
}

void adjust_autoconfig_publish_vote(system_file_disk_record *autoconfig_rec,
					system_file_disk_record *publish_rec,
					system_file_disk_record *vote_rec)
{
	/* whole block was allocated to autoconfig, now divvy it up */
	__u64 apv_data = autoconfig_rec->extent_off;
	__u64 apv_data_len = autoconfig_rec->extent_len;
	__u64 vblocks, ablocks = AUTOCONF_BLOCKS(initial_nodes, 1), 
		pblocks = PUBLISH_BLOCKS(initial_nodes, 1);

	/* autoconf and publish get just enough, vote gets all the rest. */
	/* this way we can easily tune up to 32 nodes without having to  */
	/* move these, and still keep them contiguous all the time.      */
	vblocks = ((apv_data_len >> blocksize_bits) - ablocks - pblocks);

	autoconfig_rec->extent_off = apv_data;
	autoconfig_rec->file_size = 
		autoconfig_rec->extent_len = ablocks << blocksize_bits;

	publish_rec->extent_off = autoconfig_rec->extent_off + autoconfig_rec->extent_len;
	publish_rec->file_size =
		publish_rec->extent_len = pblocks << blocksize_bits;

	vote_rec->extent_off = publish_rec->extent_off + publish_rec->extent_len;
	vote_rec->file_size = 
		vote_rec->extent_len = vblocks << blocksize_bits;
}

void write_autoconfig_header(system_file_disk_record *rec)
{
	ocfs_node_config_hdr *hdr;

	// first sector of the whole dlm block is a header
	hdr = (mapping + rec->extent_off);
	memset(hdr, 0, blocksize);
	strcpy(hdr->signature, OCFS2_NODE_CONFIG_HDR_SIGN);
	hdr->version = OCFS2_NODE_CONFIG_VER;
	hdr->num_nodes = 0;
	hdr->disk_lock.dl_master = -1;
	hdr->last_node = 0;
}
void init_record(system_file_disk_record *rec, int type, int dir)
{
	memset(rec, 0, sizeof(system_file_disk_record));
	rec->flags = OCFS2_VALID_FL | OCFS2_SYSTEM_FL;
	rec->dir = dir;
	if (dir)
		rec->links = 0;
	else
		rec->links = 1;
	rec->bi.used_bits = rec->bi.total_bits = 0;
	rec->flags = (OCFS2_VALID_FL | OCFS2_SYSTEM_FL);

	switch (type) {
		case sfi_journal:
			rec->flags |= OCFS2_JOURNAL_FL;
			break;
		case sfi_bitmap:
			rec->flags |= OCFS2_BITMAP_FL;
			break;
		case sfi_local_alloc:
			rec->flags |= OCFS2_LOCAL_ALLOC_FL;
			break;
		case sfi_dlm:
			rec->flags |= OCFS2_DLM_FL;
			break;
		case sfi_other:
			break;
	}
}


int main(int argc, char **argv)
{
	__u64 allocated;
	__u32 need;
	char fname[SYSTEM_FILE_NAME_MAX];
	int i, j, num;
	__u64 leading_space;
	funky_dir *orphan_dir;
	funky_dir *root_dir;
	funky_dir *system_dir;
	system_file_disk_record *tmprec, *tmprec2;

	progname = strdup(argv[0]);
	process_args(argc, argv);
	init_format_time();
	init_device();
	init_globals();
	adjust_volume_size();
	map_device();
	generate_uuid();

	/*
	 * ALLOCATE STUFF
	 */

	// dummy record representing the whole volume
	init_record(&global_alloc_rec, sfi_other, 0);
	global_alloc_rec.extent_off = 0;
	global_alloc_rec.extent_len = volume_size_in_bytes;

	init_record(&superblock_rec, sfi_other, 0);
	init_record(&root_dir_rec, sfi_other, 1);
	init_record(&system_dir_rec, sfi_other, 1);

	for (i=0; i<NUM_SYSTEM_INODES; i++) {
		num = (system_files[i].global ? 1 : initial_nodes);
		record[i] = malloc(sizeof(system_file_disk_record) * num);
		if (record[i] == NULL)
			MKFS_FATAL_STR("could not allocate memory for system file disk records");
		for (j=0; j < num; j++)
			init_record(&record[i][j], system_files[i].type, system_files[i].dir);
	}

	root_dir = alloc_directory();
	system_dir = alloc_directory();
	orphan_dir = alloc_directory();

	/*
	 * INITIALIZE BITMAPS
	 */
	
	/* create an alloc_bm for the global bitmap and align bytes up to next whole cluster. 
	   extent_off is not yet known, since it must be allocated from itself.  */
	need = (volume_size_in_clusters+7) >> 3;  
	need = ((need + cluster_size - 1) >> cluster_size_bits) << cluster_size_bits;
	tmprec = &(record[GLOBAL_BITMAP_SYSTEM_INODE][0]);
	tmprec->extent_off = 0; // need to fill this in later
	tmprec->extent_len = need;

	global_bm = initialize_bitmap (volume_size_in_clusters, cluster_size_bits,
				       "global bitmap", tmprec, &global_alloc_rec);

	/* assign some space from global_bm to system_bm for data and bitmap blocks */
	tmprec = &(record[GLOBAL_INODE_ALLOC_SYSTEM_INODE][0]);
	tmprec2 = &(record[GLOBAL_INODE_ALLOC_BITMAP_SYSTEM_INODE][0]);
	need = blocks_needed(); 
	alloc_bytes_from_bitmap (need << blocksize_bits, global_bm, 
				 &(tmprec->extent_off), &(tmprec->extent_len));

	need = ((((need+7) >> 3) + cluster_size - 1) >> cluster_size_bits) << cluster_size_bits;
	alloc_bytes_from_bitmap (need, global_bm, &(tmprec2->extent_off), 
				 &(tmprec2->extent_len)); 
	
	/* create an alloc_bm for the system inode bitmap */
	system_bm = initialize_bitmap(tmprec->extent_len >> blocksize_bits, blocksize_bits, 
				      "system inode bitmap", tmprec2, tmprec);


	/*
	 * ALLOCATE INODES AND DIRECTORIES
	 */
	
	/* leading space */
	leading_space = alloc_inode(LEADING_SPACE_BLOCKS);
	if (leading_space != 0ULL)
		MKFS_FATAL("leading space blocks start at byte %llu, must start at 0\n", leading_space);


	/* superblock */
	superblock_rec.fe_off = alloc_inode(SUPERBLOCK_BLOCKS);
	if (superblock_rec.fe_off != (__u64)MAGIC_SUPERBLOCK_BLOCK_NUMBER << blocksize_bits)
		MKFS_FATAL("superblock starts at byte %llu, must start at %llu\n", 
			   superblock_rec.fe_off, MAGIC_SUPERBLOCK_BLOCK_NUMBER << blocksize_bits);


	/* root directory */	
	alloc_from_bitmap (1, global_bm, &root_dir_rec.extent_off, &root_dir_rec.extent_len);
	root_dir_rec.fe_off = alloc_inode(1);
	root_dir->record = &root_dir_rec;
	add_entry_to_directory(root_dir, ".", root_dir_rec.fe_off, OCFS2_FT_DIR);
	add_entry_to_directory(root_dir, "..", root_dir_rec.fe_off, OCFS2_FT_DIR);


	/* system directory */	
	need = system_dir_blocks_needed();
	alloc_from_bitmap (need, global_bm, &system_dir_rec.extent_off, &system_dir_rec.extent_len);
	system_dir_rec.fe_off = alloc_inode(1);
	system_dir->record = &system_dir_rec;
	add_entry_to_directory(system_dir, ".", system_dir_rec.extent_off, OCFS2_FT_DIR);
	add_entry_to_directory(system_dir, "..", system_dir_rec.extent_off, OCFS2_FT_DIR);
	/* alloc and add all local system file inodes to system directory */
	for (i=0; i<NUM_SYSTEM_INODES; i++) {
		num = (system_files[i].global) ? 1 : initial_nodes;
		for (j=0; j < num; j++) {
			record[i][j].fe_off = alloc_inode(1);
			sprintf(fname, system_files[i].name, j);
			add_entry_to_directory(system_dir, fname, record[i][j].fe_off, 
				       system_files[i].dir ?  OCFS2_FT_DIR : OCFS2_FT_REG_FILE);
		}
	}

	/* dlm area data */
	tmprec = &(record[DLM_SYSTEM_INODE][0]);
	need = (AUTOCONF_BLOCKS(initial_nodes, 32) +
		PUBLISH_BLOCKS(initial_nodes, 32) + 
		VOTE_BLOCKS(initial_nodes, 32));
        alloc_from_bitmap(need, global_bm, &tmprec->extent_off, &tmprec->extent_len);
	tmprec->file_size = need << blocksize_bits;


	/* orphan dir */
	tmprec = &record[ORPHAN_DIR_SYSTEM_INODE][0];
	orphan_dir->record = tmprec;
	alloc_from_bitmap (1, global_bm, &tmprec->extent_off, &tmprec->extent_len);
	add_entry_to_directory(orphan_dir, ".", tmprec->extent_off, OCFS2_FT_DIR);
	add_entry_to_directory(orphan_dir, "..", tmprec->extent_off, OCFS2_FT_DIR);


	/* finally, allocate (extent_off) the space for the global bitmap from itself */	
	tmprec = global_bm->bm_record;
	alloc_bytes_from_bitmap (tmprec->extent_len, global_bm, 
				 &(tmprec->extent_off), &allocated);
			

	/* 
	 * FORMAT BLOCKS
	 */
	format_leading_space(leading_space);
	format_superblock(&superblock_rec, &root_dir_rec, &system_dir_rec);

	format_file(&root_dir_rec);
	format_file(&system_dir_rec);
	
	for (i=0; i<NUM_SYSTEM_INODES; i++) {
		num = (system_files[i].global ? 1 : initial_nodes);
		for (j=0; j<num; j++) {
			tmprec = &(record[i][j]);
			if (system_files[i].type == sfi_journal) {
				alloc_bytes_from_bitmap(OCFS2_DEFAULT_JOURNAL_SIZE, global_bm, 
							&(tmprec->extent_off), &(tmprec->extent_len));
				replacement_journal_create(tmprec->extent_off);
				tmprec->file_size = tmprec->extent_len;
			}
			format_file(tmprec);
		}
	}
	
	/*
	 * WRITE BITMAPS
	 */
	write_bitmap_data(global_bm);
	write_bitmap_data(system_bm);

	/*
	 * WRITE DIRECTORIES
	 */
	write_directory_data(root_dir);
	write_directory_data(system_dir);
	write_directory_data(orphan_dir);

	write_autoconfig_header(&record[DLM_SYSTEM_INODE][0]);
	/*
	 * SYNC TO DISK
	 */
	sync_device();
	unmap_device();
	close(fd);

	return 0;
}


void version(char *progname)
{
	printf("%s %s %s (build %s)\n", progname,
					OCFS2_BUILD_VERSION,
					OCFS2_BUILD_DATE,
					OCFS2_BUILD_MD5);
	return;
}				/* version */


