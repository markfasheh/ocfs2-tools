/*
 * extfinder.c
 *
 * Lists the free extent sizes of an ocfs volume
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

#include "debugocfs.h"
#include "glib.h"
#include <stdio.h>
#include <stdlib.h>

int bmrun_reversesort(const void *a, const void *b);
void print_global_bitmap_runs(int fd, void *buf);
int read_vol_disk_header(int fd, ocfs_vol_disk_hdr * v);

int extents_to_print = 0;
int verbose = 0;

int fd = -1;
char rawdev[255];
int rawminor = 0;

typedef struct _bitmap_run
{
	int start;
	int size;
} bitmap_run;


#define MAX_BITMAP_RUNS 10


int bmrun_reversesort(const void *a, const void *b)
{
	bitmap_run *one, *two;
	one = (bitmap_run *)a;	
	two = (bitmap_run *)b;
	if (one->size < two->size)
		return 1;
	else if (one->size == two->size)
		return 0;
	else 
		return -1;
}


void print_global_bitmap_runs(int fd, void *buf)
{
//	__u64 dso, cs, num;
	char *bmbuf = NULL;
	int bufsz;
	ocfs_vol_disk_hdr * v = (ocfs_vol_disk_hdr *)buf;
	int non_sysfile, sysfile;
	int i;
	bitmap_run run;
	GArray *arr = NULL;
	
	arr = g_array_new(TRUE, TRUE, sizeof(bitmap_run));
	if (!arr) {
		LOG_ERROR("out of memory");
		goto bail;
	}

//	dso = v->data_start_off;
//	cs = v->cluster_size;
//	num = v->num_clusters;
//	bufsz = (num+7)/8;
	bufsz = 1024 * 1024;
	bmbuf = (char *)malloc_aligned(bufsz); 
	if (!bmbuf) {
		LOG_ERROR("out of memory");
		goto bail;
	}

	if (lseek64(fd, v->bitmap_off, SEEK_SET) == -1) {
		LOG_ERROR("%s", strerror(errno));
		goto bail;
	}
		
	if (read(fd, bmbuf, bufsz) == -1) {
		LOG_ERROR("%s", strerror(errno));
		goto bail;
	}

	if (verbose) {
		printf("bitmap_off = %llu\n", v->bitmap_off);
		printf("data_start_off = %llu\n", v->data_start_off);
		printf("cluster_size = %llu\n", v->cluster_size);
		printf("num_clusters = %llu\n", v->num_clusters);
	}
   
	sysfile = ((8 * ONE_MEGA_BYTE) / v->cluster_size);
	non_sysfile = v->num_clusters - sysfile;
	if (non_sysfile < 0)
			non_sysfile = 0;

	run.start = -1;
	run.size = 0;
	for (i = 0; i < non_sysfile; i++) {
		if (!test_bit(i, (unsigned long *)bmbuf)) {
			run.size++;
			if (run.start==-1)
				run.start = i;
		}
		else {
			if (run.start != -1 && run.size != 0) {
				g_array_append_val(arr, run);
				run.start = -1;
				run.size = 0;
			}
		}
	}

	if (run.start != -1)
		g_array_append_val(arr, run);

	printf("Runs of contiguous free space available (decending order)\n");
	printf("Run #\tLength (KB)\tStarting bit number\n");
	printf("=====\t===========\t===================\n");
	if (arr->len > 0) {
		qsort(arr->data, arr->len, sizeof(bitmap_run), bmrun_reversesort);
    		for (i = 0; i < (arr->len > extents_to_print ?
				 extents_to_print : arr->len); i++) {
			bitmap_run *run = &g_array_index(arr, bitmap_run, i);
			__u64 kb = (((__u64)run->size) * v->cluster_size) / 1024ULL;
			printf("%5d\t%11llu\t%-9d\n", i+1, kb, run->start);
		}
	}
bail:
	if (arr)
		g_array_free(arr, FALSE);

	if (bmbuf)
		free_aligned(bmbuf);
	return ;
}

int read_vol_disk_header(int fd, ocfs_vol_disk_hdr * v)
{
	int ret = -1;

	if (lseek64(fd, 0, SEEK_SET) == -1) {
		LOG_ERROR("%s", strerror(errno));
		goto bail;
	}

	if (read(fd, v, 512) == -1) {
		LOG_ERROR("%s", strerror(errno));
		goto bail;
	}

	if (strncmp(v->signature, "OracleCFS", strlen("OracleCFS")) != 0) {
		LOG_ERROR("not a valid ocfs partition");
		goto bail;
	}

	ret = 0;

bail:
	return ret;
}

void usage()
{
    printf("usage: extfinder /dev/device\n");
}

void version(char *prog)
{
    printf("%s %s %s (build %s)\n", prog,
	   OCFS_BUILD_VERSION, OCFS_BUILD_DATE,
	   OCFS_BUILD_MD5);
}

void handle_signal(int sig)
{
    switch (sig) {
    case SIGTERM:
    case SIGINT:
	if (fd != -1)
	    close(fd);
	if (rawminor)
	    unbind_raw(rawminor);
	exit(1);
    }
}

/* uh, main */
int main(int argc, char **argv)
{
    ocfs_vol_disk_hdr *diskHeader = NULL;

#define INSTALL_SIGNAL(sig)					\
    do {							\
	if (signal(sig, handle_signal) == SIG_ERR) {		\
	    fprintf(stderr, "Could not set " #sig "\n");	\
	    goto bail;						\
	}							\
    } while (0)

    INSTALL_SIGNAL(SIGTERM);
    INSTALL_SIGNAL(SIGINT);

    init_raw_cleanup_message();

    version(argv[0]);

    if (argc < 2) {
	usage();
	goto bail;
    }

    if (argc==3)
	extents_to_print = atoi(argv[2]);

    if (extents_to_print <= 0)
        extents_to_print = MAX_BITMAP_RUNS;

    diskHeader = (ocfs_vol_disk_hdr *) malloc_aligned(512);
    if (!diskHeader) {
	    LOG_ERROR("out of memory");
	    goto bail;
    }

    if (bind_raw(argv[1], &rawminor, rawdev, sizeof(rawdev)) == -1)
	    goto bail;

    fd = open(rawdev, O_RDONLY);
    if (fd == -1) {
	usage();
	goto bail;
    }

    if (read_vol_disk_header(fd, diskHeader) == -1)
	    goto bail;

    print_global_bitmap_runs(fd, diskHeader);

bail:
    if (diskHeader)
	    free_aligned(diskHeader);
    if (fd != -1)
	    close(fd);
    if (rawminor)
	    unbind_raw(rawminor);
    exit(0);
}
