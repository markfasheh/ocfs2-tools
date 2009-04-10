/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * unix_io.c
 *
 * I/O routines for the OCFS2 userspace library.
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
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
 * Portions of this code from e2fsprogs/lib/ext2fs/unix_io.c
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001,
 *  	2002 by Theodore Ts'o.
 */

#define _XOPEN_SOURCE 600  /* Triggers ISOC99, UNIX98 in features.h */
#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE /* Because libc really doesn't want us using O_DIRECT? */

#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#ifdef __linux__
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#endif
#include <inttypes.h>

#include "ocfs2/kernel-rbtree.h"

#include "ocfs2/ocfs2.h"


/*
 * The cache looks up blocks in two ways:
 *
 * 1) If it needs a new block, it gets one off of ic->ic_lru.  The blocks
 *    attach to that list via icb->icb_list.
 *
 * 2) If it wants to look up an existing block, it gets it from
 *    ic->ic_lookup.  The blocks are attached vai icb->icb_node.
 */
struct io_cache_block {
	struct rb_node icb_node;
	struct list_head icb_list;
	uint64_t icb_blkno;
	char *icb_buf;
};

struct io_cache {
	size_t ic_nr_blocks;
	struct list_head ic_lru;
	struct rb_root ic_lookup;

	/* Housekeeping */
	struct io_cache_block *ic_metadata_buffer;
	char *ic_data_buffer;
};

struct _io_channel {
	char *io_name;
	int io_blksize;
	int io_flags;
	int io_error;
	int io_fd;
	struct io_cache *io_cache;
};

static errcode_t unix_io_read_block(io_channel *channel, int64_t blkno,
				    int count, char *data)
{
	int ret;
	ssize_t size, tot, rd;
	uint64_t location;

	/* -ative means count is in bytes */
	size = (count < 0) ? -count : count * channel->io_blksize;
	location = blkno * channel->io_blksize;

	tot = 0;
	while (tot < size) {
		rd = pread64(channel->io_fd, data + tot,
			     size - tot, location + tot);
		ret = OCFS2_ET_IO;
		if (rd < 0) {
			channel->io_error = errno;
			goto out;
		}

		if (!rd) 
			goto out;

		tot += rd;
	}

	ret = 0;

out:
	if (!ret && tot != size) {
		ret = OCFS2_ET_SHORT_READ;
		memset(data + tot, 0, size - tot);
	}

	return ret;
}

static errcode_t unix_io_write_block(io_channel *channel, int64_t blkno,
				     int count, const char *data)
{
	int ret;
	ssize_t size, tot, wr;
	uint64_t location;

	/* -ative means count is in bytes */
	size = (count < 0) ? -count : count * channel->io_blksize;
	location = blkno * channel->io_blksize;

	tot = 0;
	while (tot < size) {
		wr = pwrite64(channel->io_fd, data + tot,
 			      size - tot, location + tot);
		ret = OCFS2_ET_IO;
		if (wr < 0) {
			channel->io_error = errno;
			goto out;
		}

		if (!wr) 
			goto out;

		tot += wr;
	}

	ret = 0;
out:
	if (!ret && (tot != size))
		ret = OCFS2_ET_SHORT_WRITE;

	return ret;
}



/*
 * See if the rbtree has a block for the given block number.
 *
 * The rb_node garbage lets insertion share the search.  Trivial callers
 * pass NULL.
 */
static struct io_cache_block *io_cache_lookup(struct io_cache *ic,
					      uint64_t blkno)
{
	struct rb_node *p = ic->ic_lookup.rb_node;
	struct io_cache_block *icb;

	while (p) {
		icb = rb_entry(p, struct io_cache_block, icb_node);
		if (blkno < icb->icb_blkno) {
			p = p->rb_left;
		} else if (blkno > icb->icb_blkno) {
			p = p->rb_right;
		} else
			return icb;
	}

	return NULL;
}

static void io_cache_insert(struct io_cache *ic,
			    struct io_cache_block *insert_icb)
{
	struct rb_node **p = &ic->ic_lookup.rb_node;
	struct rb_node *parent = NULL;
	struct io_cache_block *icb = NULL;

	while (*p) {
		parent = *p;
		icb = rb_entry(parent, struct io_cache_block, icb_node);
		if (insert_icb->icb_blkno < icb->icb_blkno) {
			p = &(*p)->rb_left;
			icb = NULL;
		} else if (insert_icb->icb_blkno > icb->icb_blkno) {
			p = &(*p)->rb_right;
			icb = NULL;
		} else
			assert(0);  /* We erased it, remember? */
	}

	rb_link_node(&insert_icb->icb_node, parent, p);
	rb_insert_color(&insert_icb->icb_node, &ic->ic_lookup);
}

static void io_cache_seen(struct io_cache *ic, struct io_cache_block *icb)
{
	/* Move to the front of the LRU */
	list_del(&icb->icb_list);
	list_add_tail(&icb->icb_list, &ic->ic_lru);
}

static void io_cache_disconnect(struct io_cache *ic,
				struct io_cache_block *icb)
{
	/*
	 * This icb should longer be looked up.
	 * If icb->icb_blkno is UINT64_MAX, it's already disconnected.
	 */
	if (icb->icb_blkno != UINT64_MAX) {
		rb_erase(&icb->icb_node, &ic->ic_lookup);
		memset(&icb->icb_node, 0, sizeof(struct rb_node));
		icb->icb_blkno = UINT64_MAX;
	}
}

static struct io_cache_block *io_cache_pop_lru(struct io_cache *ic)
{
	struct io_cache_block *icb;

	icb = list_entry(ic->ic_lru.next, struct io_cache_block, icb_list);
	io_cache_disconnect(ic, icb);

	return icb;
}

static errcode_t io_cache_read_one_block(io_channel *channel, int64_t blkno,
					 char *data)
{
	errcode_t ret = 0;
	struct io_cache *ic = channel->io_cache;
	struct io_cache_block *icb;

	icb = io_cache_lookup(ic, blkno);
	if (icb)
		goto found;

	/* Ok, this blkno isn't in the cache.  Steal something. */
	icb = io_cache_pop_lru(ic);

	/*
	 * If the read fails, we leave the block at the end of the LRU
	 * and out of the lookup tree.
	 */
	ret = unix_io_read_block(channel, blkno, 1, icb->icb_buf);
	if (ret)
		goto out;

	icb->icb_blkno = blkno;
	io_cache_insert(ic, icb);

found:
	memcpy(data, icb->icb_buf, channel->io_blksize);
	io_cache_seen(ic, icb);

out:
	return ret;
}

static errcode_t io_cache_read_block(io_channel *channel, int64_t blkno,
				     int count, char *data)

{
	int i;
	errcode_t ret = 0;

	for (i = 0; i < count; i++, blkno++, data += channel->io_blksize) {
		ret = io_cache_read_one_block(channel, blkno, data);
		if (ret)
			break;
	}

	return ret;
}

static errcode_t io_cache_write_one_block(io_channel *channel,
					  int64_t blkno, const char *data)
{
	errcode_t ret;
	struct io_cache *ic = channel->io_cache;
	struct io_cache_block *icb;

	icb = io_cache_lookup(ic, blkno);
	if (icb)
		goto found;

	/* Ok, this blkno isn't in the cache.  Steal something. */
	icb = io_cache_pop_lru(ic);

	icb->icb_blkno = blkno;
	io_cache_insert(ic, icb);

found:
	memcpy(icb->icb_buf, data, channel->io_blksize);
	io_cache_seen(ic, icb);

	ret = unix_io_write_block(channel, blkno, 1, icb->icb_buf);
	if (ret)
		io_cache_disconnect(ic, icb);

	return ret;
}

static errcode_t io_cache_write_block(io_channel *channel, int64_t blkno,
				      int count, const char *data)

{
	int i;
	errcode_t ret = 0;

	for (i = 0; i < count; i++, blkno++, data += channel->io_blksize) {
		ret = io_cache_write_one_block(channel, blkno, data);
		if (ret)
			break;
	}

	return ret;
}

static void io_free_cache(struct io_cache *ic)
{
	if (ic) {
		if (ic->ic_data_buffer)
			ocfs2_free(&ic->ic_data_buffer);
		if (ic->ic_metadata_buffer)
			ocfs2_free(&ic->ic_metadata_buffer);
		ocfs2_free(&ic);
	}
}

void io_destroy_cache(io_channel *channel)
{
	if (channel->io_cache) {
		io_free_cache(channel->io_cache);
		channel->io_cache = NULL;
	}
}

errcode_t io_init_cache(io_channel *channel, size_t nr_blocks)
{
	int i;
	struct io_cache *ic;
	char *dbuf;
	struct io_cache_block *icb_list;
	errcode_t ret;

	ret = ocfs2_malloc0(sizeof(struct io_cache), &ic);
	if (ret)
		goto out;

	ic->ic_nr_blocks = nr_blocks;
	ic->ic_lookup = RB_ROOT;
	INIT_LIST_HEAD(&ic->ic_lru);

	ret = ocfs2_malloc_blocks(channel, nr_blocks, &ic->ic_data_buffer);
	if (ret)
		goto out;

	ret = ocfs2_malloc0(sizeof(struct io_cache_block) * nr_blocks,
			    &ic->ic_metadata_buffer);
	if (ret)
		goto out;

	icb_list = ic->ic_metadata_buffer;
	dbuf = ic->ic_data_buffer;
	for (i = 0; i < nr_blocks; i++) {
		icb_list[i].icb_blkno = UINT64_MAX;
		icb_list[i].icb_buf = dbuf;
		dbuf += channel->io_blksize;
		list_add_tail(&icb_list[i].icb_list, &ic->ic_lru);
	}

	channel->io_cache = ic;

out:
	if (ret)
		io_free_cache(ic);

	return ret;
}

errcode_t io_init_cache_size(io_channel *channel, size_t bytes)
{
	size_t blocks;

	blocks = (bytes + (channel->io_blksize - 1)) / channel->io_blksize;

	return io_init_cache(channel, blocks);
}


static errcode_t io_validate_o_direct(io_channel *channel)
{
	errcode_t ret = OCFS2_ET_UNEXPECTED_BLOCK_SIZE;
	int block_size;
	char *blk;

	for (block_size = io_get_blksize(channel);
	     block_size <= OCFS2_MAX_BLOCKSIZE;
	     block_size <<= 1) {
		io_set_blksize(channel, block_size);
		ret = ocfs2_malloc_block(channel, &blk);
		if (ret)
			break;

		ret = unix_io_read_block(channel, 0, 1, blk);
		ocfs2_free(&blk);
		if (!ret)
			break;
	}

	return ret;
}

errcode_t io_open(const char *name, int flags, io_channel **channel)
{
	errcode_t ret;
	io_channel *chan = NULL;
#ifdef __linux__
	struct stat stat_buf;
	struct utsname ut;
#endif

	if (!name || !*name)
		return OCFS2_ET_BAD_DEVICE_NAME;

	ret = ocfs2_malloc0(sizeof(struct _io_channel), &chan);
	if (ret)
		return ret;

	ret = ocfs2_malloc(strlen(name)+1, &chan->io_name);
	if (ret)
		goto out_chan;
	strcpy(chan->io_name, name);
	chan->io_blksize = OCFS2_MIN_BLOCKSIZE;
	chan->io_flags = (flags & OCFS2_FLAG_RW) ? O_RDWR : O_RDONLY;
	if (!(flags & OCFS2_FLAG_BUFFERED))
		chan->io_flags |= O_DIRECT;
	chan->io_error = 0;

	chan->io_fd = open64(name, chan->io_flags);
	if (chan->io_fd < 0) {
		/* chan will be freed, don't bother with chan->io_error */
		if (errno == ENOENT)
			ret = OCFS2_ET_NAMED_DEVICE_NOT_FOUND;
		else
			ret = OCFS2_ET_IO;
		goto out_name;
	}

	if (!(flags & OCFS2_FLAG_BUFFERED)) {
		ret = io_validate_o_direct(chan);
		if (ret)
			goto out_close;  /* FIXME: bindraw here */
	}

	/* Workaround from e2fsprogs */
#ifdef __linux__
#undef RLIM_INFINITY
#if (defined(__alpha__) || ((defined(__sparc__) || defined(__mips__)) && (SIZEOF_LONG == 4)))
#define RLIM_INFINITY	((unsigned long)(~0UL>>1))
#else
#define RLIM_INFINITY  (~0UL)
#endif
	/*
	 * Work around a bug in 2.4.10-2.4.18 kernels where writes to
	 * block devices are wrongly getting hit by the filesize
	 * limit.  This workaround isn't perfect, since it won't work
	 * if glibc wasn't built against 2.2 header files.  (Sigh.)
	 * 
	 */
	if ((flags & OCFS2_FLAG_RW) &&
	    (uname(&ut) == 0) &&
	    ((ut.release[0] == '2') && (ut.release[1] == '.') &&
	     (ut.release[2] == '4') && (ut.release[3] == '.') &&
	     (ut.release[4] == '1') && (ut.release[5] >= '0') &&
	     (ut.release[5] < '8')) &&
	    (fstat(chan->io_fd, &stat_buf) == 0) &&
	    (S_ISBLK(stat_buf.st_mode))) {
		struct rlimit	rlim;
		
		rlim.rlim_cur = rlim.rlim_max = (unsigned long) RLIM_INFINITY;
		setrlimit(RLIMIT_FSIZE, &rlim);
		getrlimit(RLIMIT_FSIZE, &rlim);
		if (((unsigned long) rlim.rlim_cur) <
		    ((unsigned long) rlim.rlim_max)) {
			rlim.rlim_cur = rlim.rlim_max;
			setrlimit(RLIMIT_FSIZE, &rlim);
		}
	}
#endif

	*channel = chan;
	return 0;

out_close:
	/* Ignore the return, leave the original error */
	close(chan->io_fd);

out_name:
	ocfs2_free(&chan->io_name);

out_chan:
	ocfs2_free(&chan);

	*channel = NULL;
	return ret;
}

errcode_t io_close(io_channel *channel)
{
	errcode_t ret = 0;

	io_destroy_cache(channel);

	if (close(channel->io_fd) < 0)
		ret = errno;

	ocfs2_free(&channel->io_name);
	ocfs2_free(&channel);

	return ret;
}

int io_get_error(io_channel *channel)
{
	return channel->io_error;
}

errcode_t io_set_blksize(io_channel *channel, int blksize)
{
	if (blksize % OCFS2_MIN_BLOCKSIZE)
		return OCFS2_ET_INVALID_ARGUMENT;

	if (!blksize)
		blksize = OCFS2_MIN_BLOCKSIZE;

	if (channel->io_blksize != blksize)
		channel->io_blksize = blksize;

	return 0;
}

int io_get_blksize(io_channel *channel)
{
	return channel->io_blksize;
}

int io_get_fd(io_channel *channel)
{
	return channel->io_fd;
}

errcode_t io_read_block(io_channel *channel, int64_t blkno, int count,
			char *data)
{
	if (channel->io_cache)
		return io_cache_read_block(channel, blkno, count, data);
	else
		return unix_io_read_block(channel, blkno, count, data);
}

errcode_t io_write_block(io_channel *channel, int64_t blkno, int count,
			 const char *data)
{
	if (channel->io_cache)
		return io_cache_write_block(channel, blkno, count, data);
	else
		return unix_io_write_block(channel, blkno, count, data);
}


#ifdef DEBUG_EXE
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <limits.h>

static int64_t read_number(const char *num)
{
	int64_t val;
	char *ptr;

	val = strtoll(num, &ptr, 0);
	if (!ptr || *ptr)
		return 0;

	return val;
}

static void dump_u32(uint32_t *val)
{
	unsigned int i;
	uint8_t *bytes = (uint8_t *)val;

	for (i = 0; i < sizeof(uint32_t); i++)
		fprintf(stdout, "%02X", bytes[i]);
}

static void dump_block(int64_t blkno, int blksize, char *buf)
{
	size_t i;
	uint32_t *vals = (uint32_t *)buf;

	fprintf(stdout, "Dumping block %"PRId64" (%d bytes):\n", blkno,
		blksize);

	for (i = 0; i < (blksize / sizeof(uint32_t)); i++) {
		if (!(i % 4)) {
			if (i)
				fprintf(stdout, "\n");
			fprintf(stdout, "0x%08zu\t", i * sizeof(uint32_t));
		}
		dump_u32(&vals[i]);
		fprintf(stdout, " ");
	}
	fprintf(stdout, "\n");
}

static void print_usage(void)
{
	fprintf(stderr,
		"Usage: unix_io [-b <blkno>] [-c <count>] [-B <blksize>]\n"
	       	"               <filename>\n");
}

extern int opterr, optind;
extern char *optarg;

int main(int argc, char *argv[])
{
	errcode_t ret;
	int c;
	int64_t blkno, count, blksize;
	char *filename;
	io_channel *channel;
	char *blks;

	/* Some simple defaults */
	blksize = 512;
	blkno = 0;
	count = 1;

	initialize_ocfs_error_table();

	while((c = getopt(argc, argv, "b:c:B:")) != EOF) {
		switch (c) {
			case 'b':
				blkno = read_number(optarg);
				if (blkno < 0) {
					fprintf(stderr,
						"Invalid blkno: %s\n",
						optarg);
					print_usage();
					return 1;
				}
				break;

			case 'c':
				count = read_number(optarg);
				if (!count) {
					fprintf(stderr, 
						"Invalid count: %s\n",
						optarg);
					print_usage();
					return 1;
				}
				break;

			case 'B':
				blksize = read_number(optarg);
				if (!blksize) {
					fprintf(stderr, 
						"Invalid blksize: %s\n",
						optarg);
					print_usage();
					return 1;
				}
				break;

			default:
				print_usage();
				return 1;
				break;
		}
	}

	if (blksize % OCFS2_MIN_BLOCKSIZE) {
		fprintf(stderr, "Invalid blocksize: %"PRId64"\n", blksize);
		print_usage();
		return 1;
	}
	if (count < 0) {
		if (-count > (int64_t)INT_MAX) {
			fprintf(stderr, "Count is too large: %"PRId64"\n",
				count);
			print_usage();
			return 1;
		}
		count = -count / blksize;
	} else  {
		if ((count * blksize) > INT_MAX) {
			fprintf(stderr, "Count is too large: %"PRId64"\n",
				count);
			print_usage();
			return 1;
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "Missing filename\n");
		print_usage();
		return 1;
	}

	filename = argv[optind];

	ret = io_open(filename, OCFS2_FLAG_RO, &channel);
	if (ret) {
		com_err(argv[0], ret,
			"while opening file \"%s\"", filename);
		goto out;
	}

	ret = ocfs2_malloc_blocks(channel, (int)count, &blks);
	if (ret) {
		com_err(argv[0], ret,
			"while allocating %"PRId64" blocks", count);
		goto out_channel;
	}

	ret = io_read_block(channel, blkno, (int)count, blks);
	if (ret) {
		com_err(argv[0], ret,
			"while reading %"PRId64" blocks at block %"PRId64" (%s)",
			count, blkno,
			strerror(io_get_error(channel)));
		goto out_blocks;
	}

	for (c = 0; c < count; c++)
		dump_block(blkno + c, blksize, blks + (c * blksize));

out_blocks:
	ocfs2_free(&blks);

out_channel:
	ret = io_close(channel);
	if (ret) {
		com_err(argv[0], ret,
			"while closing file \"%s\"", filename);
	}

out:
	return 0;
}
#endif  /* DEBUG_EXE */
