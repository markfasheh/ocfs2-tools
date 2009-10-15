/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * fileio.c
 *
 * I/O to files.  Part of the OCFS2 userspace library.
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
 * Ideas taken from e2fsprogs/lib/ext2fs/fileio.c
 *   Copyright (C) 1997 Theodore Ts'o.
 */

#define _XOPEN_SOURCE 600  /* Triggers XOPEN2K in features.h */
#define _LARGEFILE64_SOURCE

#include <string.h>
#include <limits.h>
#include <inttypes.h>

#include "ocfs2/ocfs2.h"

struct read_whole_context {
	char		*buf;
	char		*ptr;
	int		size;
	int		offset;
	errcode_t	errcode;
};

static int read_whole_func(ocfs2_filesys *fs,
			   uint64_t blkno,
			   uint64_t bcount,
			   uint16_t ext_flags,
			   void *priv_data)
{
	struct read_whole_context *ctx = priv_data;

	if (ext_flags & OCFS2_EXT_UNWRITTEN) {
		memset(ctx->ptr, 0, fs->fs_blocksize);
		ctx->errcode = 0;
	} else
		ctx->errcode = ocfs2_read_blocks(fs, blkno, 1, ctx->ptr);

	if (ctx->errcode)
		return OCFS2_BLOCK_ABORT;

	ctx->ptr += fs->fs_blocksize;
	ctx->offset += fs->fs_blocksize;

	return 0;
}

static errcode_t ocfs2_inline_data_read(struct ocfs2_dinode *di, void *buf,
					uint32_t count, uint64_t offset,
					uint32_t *got)
{
	struct ocfs2_inline_data *id;
	uint8_t *p;

	if (!(di->i_dyn_features & OCFS2_INLINE_DATA_FL))
		return OCFS2_ET_INVALID_ARGUMENT;

	id = &(di->id2.i_data);
	*got = 0;

	if (offset > id->id_count)
		return 0;

	p = (__u8 *) &(id->id_data);
	p += offset;

	*got = ocfs2_min((di->i_size - offset), (uint64_t)count);
	memcpy(buf, p, *got);

	return 0;
}

errcode_t ocfs2_read_whole_file(ocfs2_filesys *fs,
				uint64_t blkno,
				char **buf,
				int *len)
{
	struct read_whole_context	ctx;
	errcode_t			retval;
	char *inode_buf;
	struct ocfs2_dinode *di;

	/* So the caller can see nothing was read */
	*len = 0;
	*buf = NULL;

	retval = ocfs2_malloc_block(fs->fs_io, &inode_buf);
	if (retval)
		return retval;

	retval = ocfs2_read_inode(fs, blkno, inode_buf);
	if (retval)
		goto out_free;

	di = (struct ocfs2_dinode *)inode_buf;

	/* Arbitrary limit for our malloc */
	retval = OCFS2_ET_INVALID_ARGUMENT;
	if (di->i_size > INT_MAX) 
		goto out_free;

	retval = ocfs2_malloc_blocks(fs->fs_io,
				     ocfs2_blocks_in_bytes(fs, di->i_size),
				     buf);
	if (retval)
		goto out_free;

	if (di->i_dyn_features & OCFS2_INLINE_DATA_FL)
		return ocfs2_inline_data_read(di, *buf, di->i_size, 0,
					      (uint32_t *)len);

	ctx.buf = *buf;
	ctx.ptr = *buf;
	ctx.size = di->i_size;
	ctx.offset = 0;
	ctx.errcode = 0;
	retval = ocfs2_block_iterate(fs, blkno, 0,
				     read_whole_func, &ctx);

	*len = ctx.size;
	if (ctx.offset < ctx.size)
		*len = ctx.offset;

out_free:
	ocfs2_free(&inode_buf);

	if (!(*len)) {
		ocfs2_free(buf);
		*buf = NULL;
	}

	if (retval)
		return retval;
	return ctx.errcode;
}


errcode_t ocfs2_file_read(ocfs2_cached_inode *ci, void *buf, uint32_t count,
			  uint64_t offset, uint32_t *got)
{
	ocfs2_filesys	*fs = ci->ci_fs;
	errcode_t	ret = 0;
	char		*ptr = (char *) buf;
	uint32_t	wanted_blocks;
	uint64_t	contig_blocks;
	uint64_t	v_blkno;
	uint64_t	p_blkno;
	uint32_t	tmp;
	uint64_t	num_blocks;
	uint16_t	extent_flags;

	if (ci->ci_inode->i_dyn_features & OCFS2_INLINE_DATA_FL)
		return ocfs2_inline_data_read(ci->ci_inode, buf, count,
					      offset, got);

	/* o_direct requires aligned io */
	tmp = fs->fs_blocksize - 1;
	if ((count & tmp) || (offset & (uint64_t)tmp) ||
	    ((unsigned long)ptr & tmp))
		return OCFS2_ET_INVALID_ARGUMENT;

	wanted_blocks = count >> OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;
	v_blkno = offset >> OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;
	*got = 0;

	num_blocks = (ci->ci_inode->i_size + fs->fs_blocksize - 1) >>
			OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;

	if (v_blkno >= num_blocks)
		return 0;

	if (v_blkno + wanted_blocks > num_blocks)
		wanted_blocks = (uint32_t) (num_blocks - v_blkno);

	while(wanted_blocks) {
		ret = ocfs2_extent_map_get_blocks(ci, v_blkno, 1,
						  &p_blkno, &contig_blocks,
						  &extent_flags);
		if (ret)
			return ret;

		if (contig_blocks > wanted_blocks)
			contig_blocks = wanted_blocks;

		if (!p_blkno || extent_flags & OCFS2_EXT_UNWRITTEN) {
			/*
			 * we meet with a hole or an unwritten extent,
			 * so just empty the content.
			 */
			memset(ptr, 0, contig_blocks * fs->fs_blocksize);
		} else {
			ret = ocfs2_read_blocks(fs, p_blkno, contig_blocks,
						ptr);
			if (ret)
				return ret;
		}

		*got += (contig_blocks <<
			 OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits);
		wanted_blocks -= contig_blocks;

		if (wanted_blocks) {
			ptr += (contig_blocks <<
				OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits);
			v_blkno += (uint64_t)contig_blocks;
		} else {
			if (*got + offset > ci->ci_inode->i_size)
				*got = (uint32_t) (ci->ci_inode->i_size - offset);
			/* break */
		}
	}

	return ret;
}

/*
 * Emtpy the blocks on the disk.
 */
static errcode_t empty_blocks(ocfs2_filesys *fs,
			      uint64_t start_blk,
			      uint64_t num_blocks)
{
	errcode_t ret;
	char *buf = NULL;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret)
		goto bail;

	memset(buf, 0, fs->fs_blocksize);

	while (num_blocks) {
		ret = io_write_block(fs->fs_io, start_blk, 1, buf);
		if (ret)
			goto bail;

		num_blocks--;
		start_blk++;
	}

bail:
	if (buf)
		ocfs2_free(&buf);

	return ret;
}

static errcode_t ocfs2_inline_data_write(struct ocfs2_dinode *di, void *buf,
					 uint32_t count, uint64_t offset)
{
	struct ocfs2_inline_data *id;
	uint8_t *p;

	if (!(di->i_dyn_features & OCFS2_INLINE_DATA_FL))
		return OCFS2_ET_INVALID_ARGUMENT;

	id = &(di->id2.i_data);

	if (offset + count > id->id_count)
		return OCFS2_ET_NO_SPACE;

	p = (__u8 *) &(id->id_data);
	p += offset;

	memcpy(p, buf, count);

	return 0;
}

static errcode_t ocfs2_file_block_write(ocfs2_cached_inode *ci,
					void *buf, uint32_t count,
					uint64_t offset, uint32_t *wrote)
{
	ocfs2_filesys	*fs = ci->ci_fs;
	errcode_t	ret = 0;
	char		*ptr = (char *) buf;
	uint32_t	wanted_blocks;
	uint64_t	contig_blocks;
	uint64_t	v_blkno;
	uint64_t	p_blkno, p_start, p_end;
	uint64_t	begin_blocks = 0, end_blocks = 0;
	uint32_t	tmp;
	uint64_t	num_blocks;
	int		bs_bits = OCFS2_RAW_SB(fs->fs_super)->s_blocksize_bits;
	uint32_t	n_clusters, cluster_begin, cluster_end;
	uint64_t	bpc = fs->fs_clustersize/fs->fs_blocksize;
	int		insert = 0;
	uint16_t	extent_flags = 0;

	/* o_direct requires aligned io */
	tmp = fs->fs_blocksize - 1;
	if ((count & tmp) || (offset & (uint64_t)tmp) ||
	    ((unsigned long)ptr & tmp))
		return OCFS2_ET_INVALID_ARGUMENT;

	wanted_blocks = count >> bs_bits;
	v_blkno = offset >> bs_bits;
	*wrote = 0;

	num_blocks = (ci->ci_inode->i_size + fs->fs_blocksize - 1) >> bs_bits;

	if (v_blkno >= num_blocks)
		return 0;

	if (v_blkno + wanted_blocks > num_blocks)
		wanted_blocks = (uint32_t) (num_blocks - v_blkno);

	while(wanted_blocks) {
		ret = ocfs2_extent_map_get_blocks(ci, v_blkno, 1,
						  &p_blkno, &contig_blocks,
						  &extent_flags);
		if (ret)
			return ret;

		if (contig_blocks > wanted_blocks)
			contig_blocks = wanted_blocks;

		begin_blocks = 0;
		end_blocks = 0;
		p_end = 0;
	 	if (!p_blkno) {
			/*
			 * We meet with a hole here, so we allocate clusters
			 * and empty the both ends in case.
			 *
			 * We will postpone the extent insertion after we
			 * successfully write the extent block, so that and
			 * problems happens in block writing would not affect
			 * the file.
			 */
			cluster_begin = ocfs2_blocks_to_clusters(fs, v_blkno);
			cluster_end = ocfs2_blocks_to_clusters(fs,
						v_blkno + contig_blocks -1);
			n_clusters = cluster_end - cluster_begin + 1;
			ret = ocfs2_new_clusters(fs, 1, n_clusters, &p_start,
						 &n_clusters);
			if (ret || n_clusters == 0)
				return ret;

			begin_blocks = v_blkno & (bpc - 1);
			p_blkno = p_start + begin_blocks;
			contig_blocks = n_clusters * bpc - begin_blocks;
			if (contig_blocks > wanted_blocks) {
				end_blocks = contig_blocks - wanted_blocks;
				contig_blocks = wanted_blocks;
				p_end = p_blkno + wanted_blocks;
			}

			insert = 1;
		} else if (extent_flags & OCFS2_EXT_UNWRITTEN) {
			begin_blocks = v_blkno & (bpc - 1);
			p_start = p_blkno - begin_blocks;
			p_end = p_blkno + wanted_blocks;
			end_blocks = (p_end & (bpc - 1)) ?
						 bpc - (p_end & (bpc - 1 )) : 0;
		}

		if (begin_blocks) {
			/*
			 * The user don't write the first blocks,
			 * so we have to empty them.
			 */
			ret = empty_blocks(fs, p_start, begin_blocks);
			if (ret)
				return ret;
		}

		if (end_blocks) {
			/*
			 * we don't need to write that many blocks,
			 * so empty the blocks at the bottom.
			 */
			ret = empty_blocks(fs, p_end, end_blocks);
			if (ret)
				return ret;
		}

		ret = io_write_block(fs->fs_io, p_blkno, contig_blocks, ptr);
		if (ret)
			return ret;

		if (insert) {
			ret = ocfs2_cached_inode_insert_extent(ci,
					ocfs2_blocks_to_clusters(fs,v_blkno),
					p_start, n_clusters, 0);
			if (ret) {
				/*
				 * XXX: We don't wan't to overwrite the error
				 * from insert_extent().  But we probably need
				 * to BE LOUDLY UPSET.
				 */
				ocfs2_free_clusters(fs, n_clusters, p_start);
				return ret;
			}

			/* save up what we have done. */
			ret = ocfs2_write_cached_inode(fs, ci);
			if (ret)
				return ret;

			ret = ocfs2_extent_map_get_blocks(ci, v_blkno, 1,
						&p_blkno, NULL, NULL);
			/* now we shouldn't find a hole. */
			if (!p_blkno || p_blkno != p_start + begin_blocks)
				ret = OCFS2_ET_INTERNAL_FAILURE;
			if (ret)
				return ret;

			insert = 0;
		} else if (extent_flags & OCFS2_EXT_UNWRITTEN) {
			cluster_begin = ocfs2_blocks_to_clusters(fs, v_blkno);
			cluster_end = ocfs2_blocks_to_clusters(fs,
						v_blkno + contig_blocks -1);
			n_clusters = cluster_end - cluster_begin + 1;
			ret = ocfs2_mark_extent_written(fs, ci->ci_inode,
					cluster_begin, n_clusters,
					p_blkno & ~(bpc - 1));
			if (ret)
				return ret;
			/*
			 * We don't cache in the library right now, so any
			 * work done in mark_extent_written won't be reflected
			 * in our now stale copy. So refresh it.
			 */
			ret = ocfs2_refresh_cached_inode(fs, ci);
			if (ret)
				return ret;
		}

		*wrote += (contig_blocks << bs_bits);
		wanted_blocks -= contig_blocks;

		if (wanted_blocks) {
			ptr += (contig_blocks << bs_bits);
			v_blkno += (uint64_t)contig_blocks;
		} else {
			if (*wrote + offset > ci->ci_inode->i_size)
				*wrote = (uint32_t) (ci->ci_inode->i_size - offset);
			/* break */
		}

	}

	return ret;
}

static inline int ocfs2_size_fits_inline_data(struct ocfs2_dinode *di,
					      uint64_t new_size)
{
	if (new_size <= di->id2.i_data.id_count)
		return 1;
	return 0;
}

static void ocfs2_expand_last_dirent(char *start, uint16_t old_size,
				     uint16_t new_size)
{
	struct ocfs2_dir_entry *de;
	struct ocfs2_dir_entry *prev_de;
	char *de_buf, *limit;
	uint16_t bytes = new_size - old_size;

	limit = start + old_size;
	de_buf = start;
	de = (struct ocfs2_dir_entry *)de_buf;
	do {
		prev_de = de;
		de_buf += de->rec_len;
		de = (struct ocfs2_dir_entry *)de_buf;
	} while (de_buf < limit);

	prev_de->rec_len += bytes;
}

errcode_t ocfs2_convert_inline_data_to_extents(ocfs2_cached_inode *ci)
{
	errcode_t ret;
	uint32_t bytes, n_clusters;
	uint64_t p_start;
	char *inline_data = NULL;
	struct ocfs2_dinode *di = ci->ci_inode;
	ocfs2_filesys *fs = ci->ci_fs;
	uint64_t bpc = fs->fs_clustersize/fs->fs_blocksize;
	unsigned int new_size;

	if (di->i_size) {
		ret = ocfs2_malloc_block(fs->fs_io, &inline_data);
		if (ret)
			goto out;

		ret = ocfs2_inline_data_read(di, inline_data,
					     fs->fs_blocksize,
					     0, &bytes);
		if (ret)
			goto out;
	}

	ocfs2_dinode_new_extent_list(fs, di);
	di->i_dyn_features &= ~OCFS2_INLINE_DATA_FL;

	ret = ocfs2_new_clusters(fs, 1, 1, &p_start, &n_clusters);
	if (ret || n_clusters == 0)
		goto out;

	ret = empty_blocks(fs, p_start, bpc);
	if (ret)
		goto out;

	if (di->i_size) {
		if (S_ISDIR(di->i_mode)) {
			if (ocfs2_supports_dir_trailer(fs))
				new_size = ocfs2_dir_trailer_blk_off(fs);
			else
				new_size = fs->fs_blocksize;
			ocfs2_expand_last_dirent(inline_data, di->i_size,
						 new_size);
			if (ocfs2_supports_dir_trailer(fs))
				ocfs2_init_dir_trailer(fs, di, p_start,
						       inline_data);

			di->i_size = fs->fs_blocksize;
			ret = ocfs2_write_dir_block(fs, di, p_start,
						    inline_data);
		} else
			ret = io_write_block(fs->fs_io, p_start,
					     1, inline_data);
		if (ret)
			goto out;
	}

	ret = ocfs2_cached_inode_insert_extent(ci, 0, p_start, n_clusters, 0);
	if (ret)
		goto out;

	ret = ocfs2_write_cached_inode(fs, ci);
out:
	if (inline_data)
		ocfs2_free(&inline_data);
	return ret;
}

static errcode_t ocfs2_try_to_write_inline_data(ocfs2_cached_inode *ci,
						void *buf, uint32_t count,
						uint64_t offset)
{
	int ret;
	uint64_t end = offset + count;
	ocfs2_filesys *fs = ci->ci_fs;
	struct ocfs2_dinode *di = ci->ci_inode;

	/* Handle inodes which already have inline data 1st. */
	if (di->i_dyn_features & OCFS2_INLINE_DATA_FL) {
		if (ocfs2_size_fits_inline_data(ci->ci_inode, end))
			goto do_inline_write;

		/*
		 * The write won't fit - we have to give this inode an
		 * inline extent list now.
		 */
		ret = ocfs2_convert_inline_data_to_extents(ci);
		if (!ret)
			ret = OCFS2_ET_CANNOT_INLINE_DATA;
		goto out;
	}

	if (di->i_clusters > 0 || end > ocfs2_max_inline_data(fs->fs_blocksize))
		return OCFS2_ET_CANNOT_INLINE_DATA;

	ocfs2_set_inode_data_inline(fs, ci->ci_inode);
	ci->ci_inode->i_dyn_features |= OCFS2_INLINE_DATA_FL;

do_inline_write:
	ret = ocfs2_inline_data_write(di, buf, count, offset);
	if (ret)
		goto out;

	ret = ocfs2_write_cached_inode(fs, ci);
out:
	return ret;
}

errcode_t ocfs2_file_write(ocfs2_cached_inode *ci,
			   void *buf, uint32_t count,
			   uint64_t offset, uint32_t *wrote)
{
	errcode_t ret;
	ocfs2_filesys *fs = ci->ci_fs;

	if (ocfs2_support_inline_data(OCFS2_RAW_SB(fs->fs_super))) {
		ret = ocfs2_try_to_write_inline_data(ci, buf, count, offset);
		if (!ret || ret != OCFS2_ET_CANNOT_INLINE_DATA)
			goto out;
	}

	ret = ocfs2_file_block_write(ci, buf, count, offset, wrote);
out:
	return ret;
}

/*
 * FIXME: port the reset of e2fsprogs/lib/ext2fs/fileio.c
 */


#ifdef DEBUG_EXE
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <errno.h>

static uint64_t read_number(const char *num)
{
	uint64_t val;
	char *ptr;

	val = strtoull(num, &ptr, 0);
	if (!ptr || *ptr)
		return 0;

	return val;
}

static void print_usage(void)
{
	fprintf(stderr,
		"Usage: debug_fileio [-i <start_blkno>] <filename> <path_to_find>\n");
}


static void dump_filebuf(const char *buf, int len)
{
	int rc, offset;

	offset = 0;
	while (offset < len) {
		rc = write(STDOUT_FILENO, buf + offset, len - offset);
		if (rc < 0) {
			fprintf(stderr, "Write error: %s\n",
				strerror(errno));
			return;
		} else if (rc) {
			offset += rc;
		} else {
			fprintf(stderr, "Wha?  Unexpected EOF\n");
			return;
		}
	}
	return;
}

extern int opterr, optind;
extern char *optarg;

int main(int argc, char *argv[])
{
	errcode_t ret;
	uint64_t blkno, result_blkno;
	int c, len;
	char *filename, *lookup_path, *buf;
	char *filebuf;
	char *p;
	char lookup_name[256];
	ocfs2_filesys *fs;

	blkno = 0;

	initialize_ocfs_error_table();

	while ((c = getopt(argc, argv, "i:")) != EOF) {
		switch (c) {
			case 'i':
				blkno = read_number(optarg);
				if (blkno <= OCFS2_SUPER_BLOCK_BLKNO) {
					fprintf(stderr,
						"Invalid inode block: %s\n",
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

	if (optind >= argc) {
		fprintf(stderr, "Missing filename\n");
		print_usage();
		return 1;
	}
	filename = argv[optind];
	optind++;

	if (optind >= argc) {
		fprintf(stdout, "Missing path to lookup\n");
		print_usage();
		return 1;
	}
	lookup_path = argv[optind];

	ret = ocfs2_open(filename, OCFS2_FLAG_RO, 0, 0, &fs);
	if (ret) {
		com_err(argv[0], ret,
			"while opening file \"%s\"", filename);
		goto out;
	}

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret) {
		com_err(argv[0], ret,
			"while allocating inode buffer");
		goto out_close;
	}

	if (!blkno)
		blkno = OCFS2_RAW_SB(fs->fs_super)->s_root_blkno;

	for (p = lookup_path; *p == '/'; p++);

	lookup_path = p;

	for (p = lookup_path; ; p++) {
		if (*p && *p != '/')
			continue;

		memcpy(lookup_name, lookup_path, p - lookup_path);
		lookup_name[p - lookup_path] = '\0';
		ret = ocfs2_lookup(fs, blkno, lookup_name,
				   strlen(lookup_name), NULL,
				   &result_blkno);
		if (ret) {
			com_err(argv[0], ret,
				"while looking up \"%s\" in inode %"PRIu64
			       	" on \"%s\"\n",
				lookup_name, blkno, filename);
			goto out_free;
		}

		blkno = result_blkno;

		for (; *p == '/'; p++);

		lookup_path = p;

		if (!*p)
			break;
	}

	if (ocfs2_check_directory(fs, blkno) != OCFS2_ET_NO_DIRECTORY) {
		com_err(argv[0], ret, "\"%s\" is not a file", filename);
		goto out_free;
	}

	ret = ocfs2_read_whole_file(fs, blkno, &filebuf, &len);
	if (ret) {
		com_err(argv[0], ret,
			"while reading file \"%s\" -- read %d bytes",
			filename, len);
		goto out_free_filebuf;
	}
	if (!len)
		fprintf(stderr, "boo!\n");

	dump_filebuf(filebuf, len);

out_free_filebuf:
	if (len)
		ocfs2_free(&filebuf);

out_free:
	ocfs2_free(&buf);

out_close:
	ret = ocfs2_close(fs);
	if (ret) {
		com_err(argv[0], ret,
			"while closing file \"%s\"", filename);
	}

out:
	return 0;
}
#endif  /* DEBUG_EXE */
