/*
 * fsck.h
 *
 * Function prototypes, macros, etc. for related 'C' files
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
 * Authors: Kurt Hackel, Sunil Mushran
 */

#ifndef FSCK_H
#define FSCK_H

#define _GNU_SOURCE

#define _LARGEFILE64_SOURCE

#include <stdio.h>
#include <time.h>
#include <sys/stat.h>
#include <glib.h>
#include <unistd.h>
#include <string.h>
#include <grp.h>
#include <pwd.h>
#include <sys/types.h>
#include <asm/types.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#include <libocfs.h>
#include <bindraw.h>

#include "classes.h"
#include "sig.h"
#include "fsck_print.h"
#include "verify.h"
#include "defaults.h"
#include "layout.h"

extern bool prn_err;
extern bool int_err;
extern int  cnt_err;
extern int cnt_wrn;


#define MAX_EXTENTS	2048
#define OCFS_HBT_WAIT	10

#define MAX_NODES			OCFS_MAXIMUM_NODES
#define MAX_SYSTEM_FILES		(CLEANUP_FILE_BASE_ID + OCFS_MAXIMUM_NODES) /* 193? */
#define DIR_NODE_SIZE			(1024 * 128)
#define IS_INVALID_FIELD_NUM(c,n)	((n) >= (c)->num_members || (n) < 0)
#define USER_INPUT_MAX			1024
#define BITS_PER_BYTE			8
#define VOL_BITMAP_BYTES		(1024 * 1024)

#define DISK_OFF_TO_BIT_NUMBER(off)      \
		((off - ctxt.hdr->data_start_off) >> ctxt.cluster_size_bits)
#define BIT_NUMBER_TO_DISK_OFF(num)      \
		((num << ctxt.cluster_size_bits) + ctxt.hdr->data_start_off)
#define NUM_BYTES_TO_NUM_BITS(bytes)     \
		(bytes >> ctxt.cluster_size_bits)
#define MAX_BITS                         \
		(ctxt.hdr->num_clusters * BITS_PER_BYTE)

#define safefree(a)		\
do {				\
	if (a)			\
		free(a);	\
	(a) = NULL;		\
} while (0)

#define LOG_INTERNAL()						\
	do {							\
		prn_err = true; int_err = true;			\
		fprintf(stdout, "\nINTERNAL ERROR: ");		\
		fprintf(stdout, "%s, %d", __FILE__, __LINE__);	\
		fflush(stdout);					\
	} while (0)

/* override libocfs definition */
#undef LOG_ERROR
#define LOG_ERROR(fmt, arg...)					\
	do {							\
		prn_err = true; cnt_err++;			\
		fprintf(stdout, "\nERROR: ");			\
		fprintf(stdout, fmt, ## arg);			\
		fprintf(stdout, ", %s, %d", __FILE__, __LINE__);\
		fflush(stdout);					\
	} while (0)

#define LOG_WARNING(fmt, arg...)				\
	do {							\
		prn_err = true; cnt_wrn++;			\
		fprintf(stdout, "\nWARNING: ");			\
		fprintf(stdout, fmt, ## arg);			\
		fflush(stdout);					\
	} while (0)

#define LOG_PRINT(fmt, arg...)					\
	do {							\
		prn_err = true;					\
		fprintf(stdout, "\n");				\
		fprintf(stdout, fmt, ## arg);			\
		fflush(stdout);					\
	} while (0)

#define CLEAR_AND_PRINT(fmt, arg...)					\
do {									\
	if (!ctxt.quiet) {						\
		gchar *_a = g_strdup_printf(fmt, ## arg);		\
		if (ctxt.verbose)					\
			printf("\n%s\n", _a);				\
		else {							\
			int _j = strlen(_a);				\
			int _l = prn_len - _j;				\
			char _n = (prn_err ? '\n' : '\r');		\
			if (_l > 0) {					\
				gchar *_s = g_strnfill(_l, ' ');	\
				printf("%c%s%s", _n, _a, _s);		\
				prn_len = _j;				\
				free(_s);				\
			} else						\
				prn_len = printf("%c%s", _n, _a);	\
			prn_err = false;				\
		}							\
		free(_a);						\
	}								\
} while (0)

typedef struct _filedata
{
    ocfs_io_runs *array;
    __u32 num;
    mode_t mode;
    uid_t user;
    gid_t group;
    unsigned int major;
    unsigned int minor;
    char *linkname;
} filedata;


void usage(void);
void handle_signal(int sig);
void init_global_context(void);
int parse_fsck_cmdline(int argc, char **argv);
int confirm_changes(__u64 off, ocfs_disk_structure *s, char *buf, int idx,
		    GHashTable *bad);
int read_print_struct(ocfs_disk_structure *s, char *buf, __u64 off, int idx,
		      GHashTable **bad);
void *mem_alloc(int len);
int fsck_initialize(char **buf);
int comp_nums(const void *q1, const void *q2);
int comp_bits(const void *q1, const void *q2);
void find_unset_bits(__u8 *vol_bm, char *bitmap);
void find_set_bits(__u8 *vol_bm, char *bitmap);
int check_global_bitmap(int fd);
int check_node_bitmaps(int fd, GArray *bm_data, __u8 **node_bm,
		       __u32 *node_bm_sz, char *str);
void handle_one_cdsl_entry(int fd, ocfs_file_entry *fe, __u64 offset);
int handle_leaf_extents (int fd, ocfs_alloc_ext *arr, int num, __u32 node,
			 __u64 parent_offset);
void traverse_dir_nodes(int fd, __u64 offset, char *path);
void check_file_entry(int fd, ocfs_file_entry *fe, __u64 offset, int slot,
		      bool systemfile, char *path);
void traverse_extent(int fd, ocfs_extent_group * exthdr, int flag, void *buf,
		     int *indx);
void traverse_fe_extents(int fd, ocfs_file_entry *fe, void *buf, int *indx);
int traverse_local_extents(int fd, ocfs_file_entry *fe);
int check_next_data_ext(ocfs_file_entry *fe, void *buf, int indx);
int check_fe_last_data_ext(ocfs_file_entry *fe, void *buf, int indx);
int get_device_size(int fd);
int check_heart_beat(int *file, __u64 publ_off, __u32 sect_size);
int read_publish(int file, __u64 publ_off, __u32 sect_size, void **buf);
int get_node_names(int file, ocfs_vol_disk_hdr *volhdr, char **node_names,
		   __u32 sect_size);
void print_node_names(char **node_names, __u32 nodemap);
void print_gbl_alloc_errs(void);
void print_bit_ranges(GArray *bits, char *str1, char *str2);
void print_filenames(GArray *files);

////////////////////////
void ocfs_extent_map_init (ocfs_extent_map * map);

void ocfs_extent_map_destroy (ocfs_extent_map * map);

#define ocfs_extent_map_get_count(map)  ((map)->count)

bool ocfs_extent_map_add (ocfs_extent_map * map, __s64 virtual,
			  __s64 physical, __s64 sectors);

void ocfs_extent_map_remove (ocfs_extent_map * map, __s64 virtual,
			     __s64 sectors);

bool ocfs_extent_map_lookup (ocfs_extent_map * map, __s64 virtual,
			     __s64 * physical, __s64 * sectors, __u32 * index);

bool ocfs_extent_map_next_entry (ocfs_extent_map * map, __u32 index,
				 __s64 * virtual, __s64 * physical,
				 __s64 * sectors);
//////////////////////


enum {
	bm_extent,
	bm_dir,
	bm_symlink,
	bm_filedata,
	bm_global
};

typedef struct _bitmap_data
{
	__u32 bitnum;
	__s32 alloc_node;
	__u64 fss_off;		/* file system structure offset */
	__u64 parent_off;	/* offset of the fs structure housing the extent */
	__u32 fnum;
} bitmap_data;

typedef struct _str_data
{
	__u32 num;
	char *str;
} str_data;

typedef struct _ocfsck_context
{
	char device[OCFS_MAX_FILENAME_LENGTH];
	char raw_device[OCFS_MAX_FILENAME_LENGTH];
	int raw_minor;
	int flags;
	int fd;
	ocfs_super *vcb;
	bool write_changes;
	bool verbose;
	bool modify_all;
	bool quiet;
	bool no_hb_chk;
	bool dev_is_file;
	ocfs_vol_disk_hdr *hdr;
	__u8 *vol_bm;
	__u8 *dir_bm[OCFS_MAXIMUM_NODES];
	__u8 *ext_bm[OCFS_MAXIMUM_NODES];
	__u32 dir_bm_sz[OCFS_MAXIMUM_NODES];
	__u32 ext_bm_sz[OCFS_MAXIMUM_NODES];
	__u64 device_size;
	__u64 offset;
	int cluster_size_bits;
	GArray *vol_bm_data;
	GArray *dir_bm_data;
	GArray *ext_bm_data;
	GArray *filenames;
} ocfsck_context;

//int ocfs_lookup_file_allocation (ocfs_super * osb, ocfs_inode * oin,
//				 __s64 Vbo, __s64 * Lbo, __u32 sectors);

loff_t myseek64(int fd, loff_t off, int whence);
int myread(int file, char *buf, __u32 len);
int mywrite(int file, char *buf, __u32 len);
int myopen(char *path, int flags);
void myclose(int file);
void read_system_file(int fd, ocfs_vol_disk_hdr * v, int fileid,
		      ocfs_file_entry * fe);
int write_system_file(int fd, ocfs_vol_disk_hdr * v, int fileid,
		      ocfs_file_entry * fe);
void read_cdsl_data(int fd, void *data, __u64 offset);

int ocfs_find_clear_bits (ocfs_alloc_bm * bitmap, __u32 numBits,
			  __u32 offset, __u32 sysonly);

int print_class_member(char *buf, ocfs_class *cl, ocfs_class_member *mbr,
		       FILE *out, bool bad);
int _print_class(char *buf, ocfs_class *cl, FILE *out, bool num, GHashTable *ht);
int print_class(char *buf, ocfs_class *cl, FILE *out, GHashTable *ht);
bitmap_data * add_bm_data(__u64 start, __u64 len, __s32 alloc_node, 
			  __u64 parent_offset, int type);
int add_str_data(GArray *sd, __u32 num, char *str);

int read_one_sector(int fd, char *buf, __u64 offset, int idx);
int write_one_sector(int fd, char *buf, __u64 offset, int idx);
int read_dir_node(int fd, char *buf, __u64 offset, int idx);
int write_dir_node (int fd, char *buf, __u64 offset, int idx);
int read_volume_bitmap (int fd, char *buf, __u64 offset, int idx);
int write_volume_bitmap (int fd, char *buf, __u64 offset, int idx);

int check_dir_index (char *dirbuf, __u64 dir_offset);
int check_num_del (char *dirbuf, __u64 dir_offset);
int fix_num_del (char *dirbuf, __u64 dir_offset);
int fix_fe_offsets(char *dirbuf, __u64 dir_offset);

#endif /* FSCK_H */
