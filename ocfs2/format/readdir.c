#define _LARGEFILE64_SOURCE
#define __USE_ISOC99

static inline void prefetch(const void *x) {;}
#define __KERNEL__
#define _LINUX_PREFETCH_H
#include <linux/list.h>
#undef __KERNEL__

#include <errno.h>
#include <stdio.h>
#include <asm/types.h>
//#include <sys/types.h>
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
#include <ocfs2_fs.h>
#include <ocfs1_fs_compat.h>
#include <time.h>



void *memalign(size_t boundary, size_t size);

typedef struct _thingy
{
	struct list_head list;
	__u64 disk_off;
	__u64 num_bytes;
	__u64 file_off;
} thingy;

typedef struct _bunchathingies
{
	struct list_head head;
	__u64 file_size;
	__u64 alloc_size;
	__u64 total_size;
	
} bunchathingies;


#ifndef O_DIRECT
#define O_DIRECT    	040000
#endif


int load_one_extent(bunchathingies *bleh, ocfs2_extent_rec *ext);
int load_local_extents(int fd, bunchathingies *bleh, ocfs2_dinode *fe);
int load_extents(int fd, bunchathingies *bleh, ocfs2_dinode *fe);

unsigned long long int strtoull (__const char *__restrict __nptr, char **__restrict __endptr, int __base);

int fe_main(int argc, char **argv);

int main(int argc, char **argv)
{
	char *buf, *p, *dev;
	int fd, num, entry=0, bufsize;
	struct ocfs2_dir_entry *de;
	__u64 offset, override_file_size = 0;
	ocfs2_dinode *fe;
	bunchathingies bleh;
	thingy *t;
	struct list_head *iter;
	size_t remaining, to_read;

	if (strstr(argv[0], "fe"))
		return fe_main(argc, argv);

	INIT_LIST_HEAD(&bleh.head);
	dev = argv[1];

	bufsize = 512;
	offset = strtoull(argv[2], NULL, 10);
	printf("offset is %llu\n", offset);
	buf = memalign(512, bufsize);
	if (argc>3)
		override_file_size = strtoull(argv[3], NULL, 10);

	fd = open(dev, O_RDONLY|O_DIRECT| O_LARGEFILE);
	lseek64(fd, offset, SEEK_SET);
	num = read(fd, buf, bufsize);
	fe = (ocfs2_dinode *)buf;
	if (load_extents(fd, &bleh, fe) < 0) {
		printf("eeeeek!\n");
		exit(1);
	}
	free(buf);

	if (override_file_size)
		bufsize = override_file_size;
	else
		bufsize = bleh.file_size;
	buf = memalign(512, bufsize);
	remaining = bleh.file_size;
	p = buf;
	list_for_each (iter, &bleh.head) {
		if (!remaining)
			break;
		t = list_entry (iter, thingy, list);
		to_read = t->num_bytes;
		if (to_read > remaining)
			to_read = remaining;
		printf("seeking to %llu\n", t->disk_off);
		lseek64(fd, t->disk_off, SEEK_SET);
		num = read(fd, p, to_read);
		if (num != to_read) {
			printf("eeek!  num=%lu to_read=%lu off=%llu\n",
			       num, to_read, t->disk_off);
			exit(1);
		}
		remaining -= to_read;
		p += to_read;
	}

	p = buf;
	while (1) {
		if (p >= buf + bufsize) {
			printf("done.\n");
			break;
		}

		de = (struct ocfs2_dir_entry *)p;
		if (de->rec_len==0 || de->inode==0 || de->name_len==0) {
			printf("BAD OR END: inode=%llu, rec_len=%d, name_len=%d, file_type=%d, name='%*s'\n",
				de->inode, de->rec_len, de->name_len, de->file_type, de->name_len, de->name);
			break;
		}
		printf("entry #%d: inode=%llu, rec_len=%d, name_len=%d, file_type=%hhu, name='%*s'\n",
		       ++entry, de->inode, de->rec_len, de->name_len, de->file_type, de->name_len,
		       de->name);
		p += de->rec_len;
	}
	free(buf);
	close(fd);
	return 0;
}

int load_extents(int fd, bunchathingies *bleh, ocfs2_dinode *fe)
{
	int ret = 0;

	bleh->file_size = fe->i_size;
#warning change this
	bleh->alloc_size = fe->i_clusters * 4096;
	bleh->total_size = 0;
	
	if (fe->id2.i_list.l_tree_depth == -1)
		ret = load_local_extents(fd, bleh, fe);
	else
		ret = -1;
	
	if (bleh->total_size != bleh->alloc_size ||
	    bleh->total_size < bleh->file_size) {
		printf("eeeek!  totalsize=%llu allocsize=%llu filesize=%llu\n",
		       bleh->total_size, bleh->alloc_size, bleh->file_size);
		exit(1);
	}
	return ret;
}

int load_local_extents(int fd, bunchathingies *bleh, ocfs2_dinode *fe)
{
	int i;
	printf("load_local_extents: l_next_free_rec=%u\n", fe->id2.i_list.l_next_free_rec);
	for (i=0; i<fe->id2.i_list.l_next_free_rec; i++)
		load_one_extent(bleh, &(fe->id2.i_list.l_recs[i]));
	return 0;
}

int load_one_extent(bunchathingies *bleh, ocfs2_extent_rec *ext)
{
	thingy *t = (thingy *)malloc(sizeof(thingy));
#warning change all this
	printf("loading one extent: diskoff=%llu fileoff=%llu numbytes=%llu tot=%llu\n",
	       ext->e_blkno << 9, ext->e_cpos << 12, ext->e_clusters << 12, bleh->total_size);

	t->disk_off = ext->e_blkno << 9;
	t->file_off = ext->e_cpos << 12;
	t->num_bytes = ext->e_clusters << 12;
	list_add_tail(&t->list, &bleh->head);
	bleh->total_size += ext->e_clusters << 12;
	return 0;
}

int fe_main(int argc, char **argv)
{
	char *buf, *dev;
	int fd, num, bufsize;
	__u64 offset;
	ocfs2_dinode *fe;
	bunchathingies bleh;
	int i;
	
	INIT_LIST_HEAD(&bleh.head);
	dev = argv[1];

	bufsize = 512;
	offset = strtoull(argv[2], NULL, 10);
	printf("offset is %llu\n", offset);
	buf = memalign(512, bufsize);
	fd = open(dev, O_RDONLY|O_DIRECT| O_LARGEFILE);
	lseek64(fd, offset, SEEK_SET);
	num = read(fd, buf, bufsize);
	fe = (ocfs2_dinode *)buf;
	printf("signature: \"%-8s\"\n", fe->i_signature);
	printf("generation: %lu\n", fe->i_generation);
	printf("suballoc_node: %lu\n", fe->i_suballoc_node);
	printf("suballoc_blkno: %llu\n", fe->i_suballoc_blkno);
//	ocfs_disk_lock disk_lock;
	printf("uid: %lu\n", fe->i_uid);
	printf("gid: %lu\n", fe->i_gid);
	printf("size: %llu\n", fe->i_size);
	printf("mode: %u\n", fe->i_mode);
	printf("nlink: %u\n", fe->i_links_count);
	printf("flags: %lu\n", fe->i_flags);
	printf("atime: %s", ctime(&fe->i_atime));
	printf("ctime: %s", ctime(&fe->i_ctime));
	printf("mtime: %s", ctime(&fe->i_mtime));
	printf("dtime: %s", ctime(&fe->i_dtime));
	printf("blkno: %llu\n", fe->i_blkno);
	printf("clusters: %lu\n", fe->i_clusters);
	printf("tree_depth: %d\n", fe->id2.i_list.l_tree_depth);
	printf("next_free_ext: %u\n", fe->id2.i_list.l_next_free_rec);
	printf("extent count: %u\n", fe->id2.i_list.l_count);
	//printf("last_ext_ptr: %llu\n", fe->last_ext_ptr);
	
	printf("bitinfo: used=%lu total=%lu\n", fe->id1.bitmap1.i_used, fe->id1.bitmap1.i_total);

	printf("superinfo: \n");
	printf("   major: %u\n", fe->id2.i_super.s_major_rev_level);
	printf("   minor: %u\n", fe->id2.i_super.s_minor_rev_level);
	printf("   mnt_count: %u\n", fe->id2.i_super.s_mnt_count);
	printf("   max_mnt_count: %u\n", fe->id2.i_super.s_max_mnt_count);
	printf("   state: %u\n", fe->id2.i_super.s_state);
	printf("   errors: %u\n", fe->id2.i_super.s_errors);
	printf("   checkinterval: %lu\n", fe->id2.i_super.s_checkinterval);
	printf("   lastcheck: %s", ctime(&fe->id2.i_super.s_lastcheck));
	printf("   creator_os: %lu\n", fe->id2.i_super.s_creator_os);
	printf("   feature_compat: %lu\n", fe->id2.i_super.s_feature_compat);
	printf("   feature_incompat: %lu\n", fe->id2.i_super.s_feature_incompat);
	printf("   feature_rocompat: %lu\n", fe->id2.i_super.s_feature_ro_compat);
	printf("   root_blkno: %llu\n", fe->id2.i_super.s_root_blkno);
	printf("   system_dir_blkno: %llu\n", fe->id2.i_super.s_system_dir_blkno);
	printf("   label: %-64s\n", fe->id2.i_super.s_label);
	printf("   uuid: ");
	for (i=0; i<16; i++)
		printf("%02x ", fe->id2.i_super.s_uuid[i]);
	printf("\n");

	printf("extents: \n");
	for (i=0; i<fe->id2.i_list.l_next_free_rec; i++) {
		printf("   fileoff: %lu\n", fe->id2.i_list.l_recs[i].e_cpos);
		printf("   clusters: %lu\n", fe->id2.i_list.l_recs[i].e_clusters);
		printf("   blkno: %llu\n", fe->id2.i_list.l_recs[i].e_blkno);
	}

	free(buf);
	close(fd);
	return 0;
}
