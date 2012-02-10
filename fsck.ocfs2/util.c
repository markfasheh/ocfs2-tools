/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License, version 2,  as published by the Free Software Foundation.
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
 * --
 *
 * Little helpers that are used by all passes.
 *
 * XXX
 * 	pull more in here.. look in include/pass?.h for incongruities
 *
 */
#include <inttypes.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "ocfs2/ocfs2.h"


#include "util.h"

void o2fsck_write_inode(o2fsck_state *ost, uint64_t blkno,
			struct ocfs2_dinode *di)
{
	errcode_t ret;
	const char *whoami = __FUNCTION__;

	if (blkno != di->i_blkno) {
		com_err(whoami, OCFS2_ET_INTERNAL_FAILURE, "when asked to "
			"write an inode with an i_blkno of %"PRIu64" to block "
			"%"PRIu64, (uint64_t)di->i_blkno, blkno);
		return;
	}

	ret = ocfs2_write_inode(ost->ost_fs, blkno, (char *)di);
	if (ret) {
		com_err(whoami, ret, "while writing inode %"PRIu64,
			(uint64_t)di->i_blkno);
		ost->ost_saw_error = 1;
	}
}

void o2fsck_mark_cluster_allocated(o2fsck_state *ost, uint32_t cluster)
{
	int was_set = 0;
	errcode_t ret;
	const char *whoami = __FUNCTION__;

	o2fsck_bitmap_set(ost->ost_allocated_clusters, cluster, &was_set);

	if (!was_set)
		return;

	if (!ost->ost_duplicate_clusters) {
		fprintf(stderr,
			"Duplicate clusters detected.  Pass 1b will be run\n");

		ret = ocfs2_cluster_bitmap_new(ost->ost_fs,
					       "duplicate clusters",
					       &ost->ost_duplicate_clusters);
		if (ret) {
			com_err(whoami, ret,
				"while allocating duplicate cluster bitmap");
			o2fsck_abort();
		}
	}

	verbosef("Cluster %"PRIu32" is allocated to more than one object\n",
		 cluster);
	ocfs2_bitmap_set(ost->ost_duplicate_clusters, cluster, NULL);
}

void o2fsck_mark_clusters_allocated(o2fsck_state *ost, uint32_t cluster,
				    uint32_t num)
{
	while(num--)
		o2fsck_mark_cluster_allocated(ost, cluster++);
}

void o2fsck_mark_cluster_unallocated(o2fsck_state *ost, uint32_t cluster)
{
	int was_set;

	o2fsck_bitmap_clear(ost->ost_allocated_clusters, cluster, &was_set);
}

errcode_t o2fsck_type_from_dinode(o2fsck_state *ost, uint64_t ino,
				  uint8_t *type)
{
	char *buf = NULL;
	errcode_t ret;
	struct ocfs2_dinode *dinode;
	const char *whoami = __FUNCTION__;

	*type = 0;

	ret = ocfs2_malloc_block(ost->ost_fs->fs_io, &buf);
	if (ret) {
		com_err(whoami, ret, "while allocating an inode buffer to "
			"read and discover the type of inode %"PRIu64, ino);
		goto out;
	}

	ret = ocfs2_read_inode(ost->ost_fs, ino, buf);
	if (ret) {
		com_err(whoami, ret, "while reading inode %"PRIu64" to "
			"discover its file type", ino);
		goto out;
	}

	dinode = (struct ocfs2_dinode *)buf; 
	*type = ocfs2_type_by_mode[(dinode->i_mode & S_IFMT)>>S_SHIFT];

out:
	if (buf)
		ocfs2_free(&buf);
	return ret;
}

size_t o2fsck_bitcount(unsigned char *bytes, size_t len)
{
	static unsigned char nibble_count[16] = {
		0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4
	};
	size_t count = 0;

	for (; len--; bytes++) {
		count += nibble_count[*bytes >> 4];
		count += nibble_count[*bytes & 0xf];
	}

	return count;
}

errcode_t handle_slots_system_file(ocfs2_filesys *fs,
				   int type,
				   errcode_t (*func)(ocfs2_filesys *fs,
						     struct ocfs2_dinode *di,
						     int slot))
{
	errcode_t ret;
	uint64_t blkno;
	int slot, max_slots;
	char *buf = NULL;
	struct ocfs2_dinode *di;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		goto bail;

	di = (struct ocfs2_dinode *)buf;

	max_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;

	for (slot = 0; slot < max_slots; slot++) {
		ret = ocfs2_lookup_system_inode(fs,
						type,
						slot, &blkno);
		if (ret)
			goto bail;

		ret = ocfs2_read_inode(fs, blkno, buf);
		if (ret)
			goto bail;

		if (func) {
			ret = func(fs, di, slot);
			if (ret)
				goto bail;
		}
	}

bail:

	if (buf)
		ocfs2_free(&buf);
	return ret;
}

void o2fsck_init_resource_track(struct o2fsck_resource_track *rt,
				io_channel *channel)
{
	struct rusage r;

	gettimeofday(&rt->rt_real_time, 0);

	io_get_stats(channel, &rt->rt_io_stats);

	memset(&r, 0, sizeof(struct rusage));
	getrusage(RUSAGE_SELF, &r);
	rt->rt_user_time = r.ru_utime;
	rt->rt_sys_time = r.ru_stime;
}

static inline float timeval_in_secs(struct timeval *tv)
{
	return tv->tv_sec + ((float)(tv->tv_usec) / 1000000);
}

static inline void diff_timeval(struct timeval *tv1, struct timeval *tv2)
{
	tv1->tv_sec -=  tv2->tv_sec;
	if (tv1->tv_usec < tv2->tv_usec) {
		tv1->tv_usec = 1000000 - tv2->tv_usec + tv1->tv_usec;
		tv1->tv_sec--;
	} else
		tv1->tv_usec -= tv2->tv_usec;
}

static inline void add_timeval(struct timeval *tv1, struct timeval *tv2)
{
	tv1->tv_sec +=  tv2->tv_sec;
	tv1->tv_usec += tv2->tv_usec;
	if (tv1->tv_usec > 1000000) {
		tv1->tv_sec++;
		tv1->tv_usec -= 1000000;
	}
}

void o2fsck_add_resource_track(struct o2fsck_resource_track *rt1,
			       struct o2fsck_resource_track *rt2)
{
	struct ocfs2_io_stats *io1 = &rt1->rt_io_stats;
	struct ocfs2_io_stats *io2 = &rt2->rt_io_stats;

	add_timeval(&rt1->rt_real_time, &rt2->rt_real_time);
	add_timeval(&rt1->rt_user_time, &rt2->rt_user_time);
	add_timeval(&rt1->rt_sys_time, &rt2->rt_sys_time);

	io1->is_bytes_read += io2->is_bytes_read;
	io1->is_bytes_written += io2->is_bytes_written;
	io1->is_cache_hits += io2->is_cache_hits;
	io1->is_cache_misses += io2->is_cache_misses;
	io1->is_cache_inserts += io2->is_cache_inserts;
	io1->is_cache_removes += io2->is_cache_removes;

}

void o2fsck_compute_resource_track(struct o2fsck_resource_track *rt,
				   io_channel *channel)
{
	struct rusage r;
	struct timeval time_end;
	struct ocfs2_io_stats _ios, *ios = &_ios;
	struct ocfs2_io_stats *rtio = &rt->rt_io_stats;

	getrusage(RUSAGE_SELF, &r);
	gettimeofday(&time_end, 0);

	diff_timeval(&r.ru_utime, &rt->rt_user_time);
	diff_timeval(&r.ru_stime, &rt->rt_sys_time);
	diff_timeval(&time_end, &rt->rt_real_time);

	memcpy(&rt->rt_user_time, &r.ru_utime, sizeof(struct timeval));
	memcpy(&rt->rt_sys_time, &r.ru_stime, sizeof(struct timeval));
	memcpy(&rt->rt_real_time, &time_end, sizeof(struct timeval));

	io_get_stats(channel, ios);

	rtio->is_bytes_read = ios->is_bytes_read - rtio->is_bytes_read;
	rtio->is_bytes_written = ios->is_bytes_written - rtio->is_bytes_written;
	rtio->is_cache_hits = ios->is_cache_hits - rtio->is_cache_hits;
	rtio->is_cache_misses = ios->is_cache_misses - rtio->is_cache_misses;
	rtio->is_cache_inserts = ios->is_cache_inserts - rtio->is_cache_inserts;
	rtio->is_cache_removes = ios->is_cache_removes - rtio->is_cache_removes;
}

void o2fsck_print_resource_track(char *pass, o2fsck_state *ost,
				 struct o2fsck_resource_track *rt,
				 io_channel *channel)
{
	struct ocfs2_io_stats *rtio = &rt->rt_io_stats;
	uint64_t total_io, cache_read;
	float rtime_s, utime_s, stime_s, walltime;
	uint32_t rtime_m, utime_m, stime_m;

	if (!ost->ost_show_stats)
		return ;

	if (pass && !ost->ost_show_extended_stats)
		return;

#define split_time(_t, _m, _s)			\
	do {					\
		(_s) = timeval_in_secs(&_t);	\
		(_m) = (_s) / 60;		\
		(_s) -= ((_m) * 60);		\
	} while (0);

	split_time(rt->rt_real_time, rtime_m, rtime_s);
	split_time(rt->rt_user_time, utime_m, utime_s);
	split_time(rt->rt_sys_time, stime_m, stime_s);

	walltime = timeval_in_secs(&rt->rt_real_time) -
		timeval_in_secs(&rt->rt_user_time);

	/* TODO: Investigate why user time is sometimes > wall time*/
	if (walltime < 0)
		walltime = 0;

	cache_read = (uint64_t)rtio->is_cache_hits * io_get_blksize(channel);
	total_io = rtio->is_bytes_read + rtio->is_bytes_written;

	if (!pass)
		printf("  Cache size: %uMB\n",
		       mbytes(io_get_cache_size(channel)));

	printf("  I/O read disk/cache: %lluMB / %lluMB, write: %lluMB, "
	       "rate: %.2fMB/s\n", mbytes(rtio->is_bytes_read),
	       mbytes(cache_read), mbytes(rtio->is_bytes_written),
	       (double)(mbytes(total_io) / walltime));

	printf("  Times real: %dm%.3fs, user: %dm%.3fs, sys: %dm%.3fs\n",
	       rtime_m, rtime_s, utime_m, utime_s, stime_m, stime_s);
}

/* Number of blocks available in the I/O cache */
static int cache_blocks;
/*
 * Number of blocks we've currently cached.  This is an imperfect guess
 * designed for pre-caching.  Code can keep slurping blocks until
 * o2fsck_worth_caching() returns 0.
 */
static int blocks_cached;

void o2fsck_init_cache(o2fsck_state *ost, enum o2fsck_cache_hint hint)
{
	errcode_t ret;
	uint64_t blocks_wanted, av_blocks;
	int leave_room;
	ocfs2_filesys *fs = ost->ost_fs;
	int max_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;
	uint64_t pages_wanted, avpages;

	switch (hint) {
		case O2FSCK_CACHE_MODE_FULL:
			leave_room = 1;
			blocks_wanted = fs->fs_blocks;
			break;
		case O2FSCK_CACHE_MODE_JOURNAL:
			/*
			 * We need enough blocks for all the journal
			 * data.  Let's guess at 256M journals.
			 */
			leave_room = 0;

			blocks_wanted = (uint64_t)max_slots * 1024 * 1024 * 256;
			blocks_wanted = ocfs2_bytes_to_blocks(fs,
							      blocks_wanted);
			break;
		case O2FSCK_CACHE_MODE_NONE:
			return;
		default:
			assert(0);
	}

	verbosef("Want %"PRIu64" blocks for the I/O cache\n",
		 blocks_wanted);

	/*
	 * leave_room means that we don't want our cache to be taking
	 * all available memory.  So we try to get twice as much as we
	 * want; if that works, we know that getting exactly as much as
	 * we want is going to be safe.
	 */
	if (leave_room)
		blocks_wanted <<= 1;

	if (blocks_wanted > INT_MAX)
		blocks_wanted = INT_MAX;

	av_blocks = blocks_wanted;
	avpages = sysconf(_SC_AVPHYS_PAGES);
	pages_wanted = blocks_wanted * fs->fs_blocksize / getpagesize();
	if (pages_wanted > avpages)
		av_blocks = avpages * getpagesize() / fs->fs_blocksize;

	while (blocks_wanted > 0) {
		io_destroy_cache(fs->fs_io);

		verbosef("Asking for %"PRIu64" blocks of I/O cache\n",
			 blocks_wanted);
		if (blocks_wanted > av_blocks)
			blocks_wanted = av_blocks;
		ret = io_init_cache(fs->fs_io, blocks_wanted);
		if (!ret) {
			/*
			 * We want to pin our cache; there's no point in
			 * having a large cache if half of it is in swap.
			 * However, some callers may not be privileged
			 * enough, so once we get down to a small enough
			 * number (512 blocks), we'll stop caring.
			 */
			ret = io_mlock_cache(fs->fs_io);
			if (ret && (blocks_wanted <= 512))
				ret = 0;
		}
		if (!ret) {
			verbosef("Got %"PRIu64" blocks\n", blocks_wanted);
			/*
			 * We've found an allocation that works.  If
			 * we're not leaving room, we're done.  But if
			 * we're leaving room, we clear leave_room and go
			 * around again.  We expect to succeed there.
			 */
			if (!leave_room) {
				cache_blocks = blocks_wanted;
				break;
			}

			verbosef("Leaving room for other %s\n", "allocations");
			leave_room = 0;
		}

		blocks_wanted >>= 1;
	}
}

int o2fsck_worth_caching(int blocks_to_read)
{
	if ((blocks_to_read + blocks_cached) > cache_blocks)
		return 0;

	blocks_cached += blocks_to_read;
	return 1;
}

void o2fsck_reset_blocks_cached(void)
{
	blocks_cached = 0;
}

void __o2fsck_bitmap_set(ocfs2_bitmap *bitmap, uint64_t bitno, int *oldval,
			 const char *where)
{
	errcode_t ret;

	ret = ocfs2_bitmap_set(bitmap, bitno, oldval);
	if (ret) {
		com_err(where, ret, "while trying to set bit %"PRIu64,
			bitno);
		o2fsck_abort();
	}
}

void __o2fsck_bitmap_clear(ocfs2_bitmap *bitmap, uint64_t bitno, int *oldval,
			   const char *where)
{
	errcode_t ret;

	ret = ocfs2_bitmap_clear(bitmap, bitno, oldval);
	if (ret) {
		com_err(where, ret, "while trying to clear bit %"PRIu64,
			bitno);
		o2fsck_abort();
	}
}

/*
 * What if we're somewhere we can't set an error and we need to abort fsck?
 * We don't want to just exit(1), as we may have some cluster locks, etc.
 * If we SIGTERM ourselves, our signal handler should do the right thing.
 */
void o2fsck_abort(void)
{
	fprintf(stderr, "Aborting\n");
	kill(getpid(), SIGTERM);
}
