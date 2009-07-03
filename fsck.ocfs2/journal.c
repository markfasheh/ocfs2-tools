/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * Copyright (C) 2000 Andreas Dilger
 * Copyright (C) 2000 Theodore Ts'o
 * Copyright (C) 2004,2008 Oracle.  All rights reserved.
 *
 * Parts of the code are based on fs/jfs/journal.c by Stephen C. Tweedie
 * Copyright (C) 1999 Red Hat Software
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License version 2 as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * --
 * This replays the jbd2 journals for each slot.  First all the journals are
 * walked to detect inconsistencies.  Only journals with no problems will be
 * replayed.  IO errors during replay will just result in partial journal
 * replay, just like jbd2 does in the kernel.  Journals that don't pass
 * consistency checks, like having overlapping blocks or strange fields, are
 * ignored and left for later passes to clean up.  

 * XXX
 * 	future passes need to guarantee journals exist and are the same size 
 * 	pass fsck trigger back up, write dirty fs, always zap/write
 * 	revocation code is totally untested
 * 	some setup errors, like finding the dlm system inode, are fatal
 */

#include <stdint.h>
#include <string.h>
#include <inttypes.h>

#include "ocfs2/byteorder.h"
#include "ocfs2/ocfs2.h"

#include "fsck.h"
#include "journal.h"
#include "pass1.h"
#include "problem.h"
#include "util.h"

static const char *whoami = "journal recovery";

struct journal_info {
	int			ji_slot;
	unsigned		ji_replay:1;

	uint64_t		ji_ino;
	struct rb_root		ji_revoke;
	journal_superblock_t	*ji_jsb;
	uint64_t		ji_jsb_block;
	ocfs2_cached_inode	*ji_cinode;

	unsigned		ji_set_final_seq:1;
	uint32_t		ji_final_seq;

	/* we keep our own bitmap for detecting overlapping journal blocks */
	ocfs2_bitmap		*ji_used_blocks;
};

struct revoke_entry {
	struct rb_node	r_node;
	uint64_t	r_block;
	uint32_t	r_seq;
};

static int seq_gt(uint32_t x, uint32_t y)
{
	int32_t diff = x - y;
	return diff > 0;
}
static int seq_geq(uint32_t x, uint32_t y)
{
	int32_t diff = x - y;
	return diff >= 0;
}

static errcode_t revoke_insert(struct rb_root *root, uint64_t block,
				uint32_t seq)
{
	struct rb_node ** p = &root->rb_node;
	struct rb_node * parent = NULL;
	struct revoke_entry *re;

	while (*p)
	{
		parent = *p;
		re = rb_entry(parent, struct revoke_entry, r_node);

		if (block < re->r_block)
			p = &(*p)->rb_left;
		else if (block > re->r_block)
			p = &(*p)->rb_right;
		else {
			if (seq_gt(seq, re->r_seq))
				re->r_seq = seq;
			return 0;
		}
	}

	re = malloc(sizeof(struct revoke_entry));
	if (re == NULL)
		return OCFS2_ET_NO_MEMORY;

	re->r_block = block;
	re->r_seq = seq;

	rb_link_node(&re->r_node, parent, p);
	rb_insert_color(&re->r_node, root);

	return 0;
}

static int revoke_this_block(struct rb_root *root, uint64_t block, 
		 	     uint32_t seq)
{
	struct rb_node *node = root->rb_node;
	struct revoke_entry *re;

	while (node) {
		re = rb_entry(node, struct revoke_entry, r_node);

		if (block < re->r_block)
			node = node->rb_left;
		else if (block > re->r_block)
			node = node->rb_right;
		else {
			/* only revoke if we've recorded a revoke entry for
			 * this block that is <= the seq that we're interested
			 * in */
			if (re && !seq_gt(seq, re->r_seq)) {
				verbosef("%"PRIu64" is revoked\n", block);
				return 1;
			}
		}
	}

	return 0;
}

static void revoke_free_all(struct rb_root *root)
{
	struct revoke_entry *re;
	struct rb_node *node;

	while((node = rb_first(root)) != NULL) {
		re = rb_entry(node, struct revoke_entry, r_node);
		rb_erase(node, root);
		free(re);
	}
}

static errcode_t add_revoke_records(struct journal_info *ji, char *buf,
				    size_t max, uint32_t seq)
{
	journal_revoke_header_t jr;
	uint32_t *blkno;  /* XXX 640k ought to be enough for everybody */
	size_t i, num;
	errcode_t err = 0;

	memcpy(&jr, buf, sizeof(jr));
	jr.r_count = be32_to_cpu(jr.r_count);

	if (jr.r_count < sizeof(jr) || jr.r_count > max) {
		verbosef("corrupt r_count: %X", jr.r_count);
		return OCFS2_ET_BAD_JOURNAL_REVOKE;
	}

	num = (jr.r_count - sizeof(jr)) / sizeof(blkno);
	blkno = (uint32_t *)(buf + sizeof(jr));

	for (i = 0; i < num; i++, blkno++) {
		err = revoke_insert(&ji->ji_revoke, be32_to_cpu(*blkno), seq);
		if (err)
			break;
	}

	return err;
}

static uint64_t jwrap(journal_superblock_t *jsb, uint64_t block)
{
	uint64_t diff = jsb->s_maxlen - jsb->s_first;

	if (diff == 0) /* ugh */
		return 0;

	while (block >= jsb->s_maxlen)
		block -= diff;

	return block;
}

static errcode_t count_tags(ocfs2_filesys *fs, journal_superblock_t *jsb,
			    char *buf, uint64_t *nr_ret)
{
	char *tagp, *last;
	journal_block_tag_t *tag;
	int tag_bytes = ocfs2_journal_tag_bytes(jsb);
	uint64_t nr = 0;

	if (jsb->s_blocksize < sizeof(journal_header_t) + tag_bytes)
		return OCFS2_ET_BAD_JOURNAL_TAG;

	tagp = &buf[sizeof(journal_header_t)];
	last = &buf[jsb->s_blocksize - tag_bytes];

	for(; tagp <= last; tagp += tag_bytes) {
		tag = (journal_block_tag_t *)tagp;
		nr++;
		if (ocfs2_block_out_of_range(fs, 
					     ocfs2_journal_tag_block(tag, tag_bytes)))
			return OCFS2_ET_BAD_JOURNAL_TAG;

		if (tag->t_flags & cpu_to_be32(JBD2_FLAG_LAST_TAG))
			break;
		if (!(tag->t_flags & cpu_to_be32(JBD2_FLAG_SAME_UUID)))
			tagp += 16;
	}

	*nr_ret = nr;
	return 0;
}

static errcode_t lookup_journal_block(ocfs2_filesys *fs, 
				      struct journal_info *ji, 
				      uint64_t blkoff,
				      uint64_t *blkno,
				      int check_dup)
{
	errcode_t ret;
	uint64_t contig;
	int was_set;

	ret = ocfs2_extent_map_get_blocks(ji->ji_cinode, blkoff, 1, blkno,
					  &contig, NULL);
	if (ret) {
		com_err(whoami, ret, "while looking up logical block "
			"%"PRIu64" in slot %d's journal", blkoff, ji->ji_slot);
		goto out;
	}

	if (check_dup) {
		ocfs2_bitmap_set(ji->ji_used_blocks, *blkno, &was_set);
		if (was_set)  {
			printf("Logical block %"PRIu64" in slot %d's journal "
			       "maps to block %"PRIu64" which has already "
			       "been used in another journal.\n", blkoff,
			       ji->ji_slot, *blkno);
			ret = OCFS2_ET_DUPLICATE_BLOCK;
		}
	}

out:
	return ret;
}

static errcode_t read_journal_block(ocfs2_filesys *fs, 
				    struct journal_info *ji, 
				    uint64_t blkoff, 
				    char *buf,
				    int check_dup)
{
	errcode_t err;
	uint64_t	blkno;

	err = lookup_journal_block(fs, ji, blkoff, &blkno, check_dup);
	if (err)
		return err;

	err = ocfs2_read_blocks(fs, blkno, 1, buf);
	if (err)
		com_err(whoami, err, "while reading block %"PRIu64" of slot "
			"%d's journal", blkno, ji->ji_slot);

	return err;
}

static errcode_t replay_blocks(ocfs2_filesys *fs, struct journal_info *ji,
			       char *buf, uint64_t seq, uint64_t *next_block)
{
	char *tagp;
	journal_block_tag_t *tag;
	size_t i, num;
	char *io_buf = NULL;
	errcode_t err, ret = 0;
	int tag_bytes = ocfs2_journal_tag_bytes(ji->ji_jsb);
	uint32_t t_flags;
	uint64_t block64;
		
	tagp = buf + sizeof(journal_header_t);
	num = (ji->ji_jsb->s_blocksize - sizeof(journal_header_t)) / 
		tag_bytes;

	ret = ocfs2_malloc_blocks(fs->fs_io, 1, &io_buf);
	if (ret) {
		com_err(whoami, ret, "while allocating a block buffer");
		goto out;
	}

	for(i = 0; i < num; i++, tagp += tag_bytes, (*next_block)++) {
		tag = (journal_block_tag_t *)tagp;
		t_flags = be32_to_cpu(tag->t_flags);
		block64 = ocfs2_journal_tag_block(tag, tag_bytes);

		*next_block = jwrap(ji->ji_jsb, *next_block);

		verbosef("recovering journal block %"PRIu64" to disk block "
			 "%"PRIu64"\n", *next_block, block64);

		if (revoke_this_block(&ji->ji_revoke, block64, seq))
			goto skip_io;

		err = read_journal_block(fs, ji, *next_block, io_buf, 1);
		if (err) {
			ret = err;
			goto skip_io;
		}

		if (t_flags & JBD2_FLAG_ESCAPE) {
			uint32_t magic = cpu_to_be32(JBD2_MAGIC_NUMBER);
			memcpy(io_buf, &magic, sizeof(magic));
		}

		err = io_write_block(fs->fs_io, block64, 1, io_buf);
		if (err)
			ret = err;

	skip_io:
		if (t_flags & JBD2_FLAG_LAST_TAG)
			i = num; /* be sure to increment next_block */
		if (!(t_flags & JBD2_FLAG_SAME_UUID))
			tagp += 16;
	}
	
out:
	if (io_buf)
		ocfs2_free(&io_buf);
	return ret;
}

static errcode_t walk_journal(ocfs2_filesys *fs, int slot, 
			      struct journal_info *ji, char *buf, int recover)
{
	errcode_t err, ret = 0;
	uint32_t next_seq;
	uint64_t next_block, nr;
	journal_superblock_t *jsb = ji->ji_jsb;
	journal_header_t jh;

	next_seq = jsb->s_sequence;
	next_block = jsb->s_start;

	/* s_start == 0 when we have nothing to do */
	if (next_block == 0)
		return 0;

	/* ret is set when bad tags are seen in the first scan and when there
	 * are io errors in the recovery scan.  Only stop walking the journal
	 * when bad tags are seen in the first scan. */
	while(recover || !ret) {
		verbosef("next_seq %"PRIu32" final_seq %"PRIu32" next_block "
			 "%"PRIu64"\n", next_seq, ji->ji_final_seq,
			 next_block);

		if (recover && seq_geq(next_seq, ji->ji_final_seq))
			break;

		/* only mark the blocks used on the first pass */
		err = read_journal_block(fs, ji, next_block, buf, !recover);
		if (err) {
			ret = err;
			break;
		}

		next_block = jwrap(jsb, next_block + 1);

		memcpy(&jh, buf, sizeof(jh));
		jh.h_magic = be32_to_cpu(jh.h_magic);
		jh.h_blocktype = be32_to_cpu(jh.h_blocktype);
		jh.h_sequence = be32_to_cpu(jh.h_sequence);

		verbosef("jh magic %x\n", jh.h_magic);

		if (jh.h_magic != JBD2_MAGIC_NUMBER)
			break;

		verbosef("jh block %x\n", jh.h_blocktype);
		verbosef("jh seq %"PRIu32"\n", jh.h_sequence);

		if (jh.h_sequence != next_seq)
			break;

		switch(jh.h_blocktype) {
		case JBD2_DESCRIPTOR_BLOCK:
			verbosef("found a desc type %x\n", jh.h_blocktype);
			/* replay the blocks described in the desc block */
			if (recover) {
				err = replay_blocks(fs, ji, buf, next_seq, 
						    &next_block);
				if (err)
					ret = err;
				continue;
			}

			/* just record the blocks as used and carry on */ 
			err = count_tags(fs, jsb, buf, &nr);
			if (err)
				ret = err;
			else
				next_block = jwrap(jsb, next_block + nr);
			break;

		case JBD2_COMMIT_BLOCK:
			verbosef("found a commit type %x\n", jh.h_blocktype);
			next_seq++;
			break;

		case JBD2_REVOKE_BLOCK:
			verbosef("found a revoke type %x\n", jh.h_blocktype);
			add_revoke_records(ji, buf, jsb->s_blocksize,
					   next_seq);
			break;

		default:
			verbosef("unknown type %x\n", jh.h_blocktype);
			break;
		}
	}

	verbosef("done scanning with seq %"PRIu32"\n", next_seq);

	if (!recover) {
		ji->ji_set_final_seq = 1;
		ji->ji_final_seq = next_seq;
	} else if (ji->ji_final_seq != next_seq) {
		printf("Replaying slot %d's journal stopped at seq %"PRIu32" "
		       "but an initial scan indicated that it should have "
		       "stopped at seq %"PRIu32"\n", ji->ji_slot, next_seq,
		       ji->ji_final_seq);
		if (ret == 0)
			err = OCFS2_ET_IO;
	}

	return ret;
}

static errcode_t prep_journal_info(ocfs2_filesys *fs, int slot,
			           struct journal_info *ji)
{
	errcode_t err;


	err = ocfs2_malloc_blocks(fs->fs_io, 1, &ji->ji_jsb);
	if (err)
		com_err(whoami, err, "while allocating space for slot %d's "
			    "journal superblock", slot);

	err = ocfs2_lookup_system_inode(fs, JOURNAL_SYSTEM_INODE,
					slot, &ji->ji_ino);
	if (err) {
		com_err(whoami, err, "while looking up the journal inode for "
			"slot %d", slot);
		goto out;
	}

	err = ocfs2_read_cached_inode(fs, ji->ji_ino, &ji->ji_cinode);
	if (err) {
		com_err(whoami, err, "while reading cached inode %"PRIu64" "
			"for slot %d's journal", ji->ji_ino, slot);
		goto out;
	}

	if (!(ji->ji_cinode->ci_inode->id1.journal1.ij_flags &
	      OCFS2_JOURNAL_DIRTY_FL))
		goto out;

	err = lookup_journal_block(fs, ji, 0, &ji->ji_jsb_block, 1);
	if (err)
		goto out;

	/* XXX be smarter about reading in the whole super block if it
	 * spans multiple blocks */
	err = ocfs2_read_journal_superblock(fs, ji->ji_jsb_block, 
					    (char *)ji->ji_jsb);
	if (err) {
		com_err(whoami, err, "while reading block %"PRIu64" as slot "
			"%d's journal super block", ji->ji_jsb_block,
			ji->ji_slot);
		goto out;
	}

	ji->ji_replay = 1;

	verbosef("slot: %d jsb start %u maxlen %u\n", slot,
		 ji->ji_jsb->s_start, ji->ji_jsb->s_maxlen);
out:
	return err;
}

/*
 * We only need to replay the journals if the inode's flag is set and s_start
 * indicates that there is actually pending data in the journals.
 *
 * In the simple case of an unclean shutdown we don't want to have to build up
 * enough state to be able to truncate the inodes waiting in the orphan dir.
 * ocfs2 in the kernel only fixes up the orphan dirs if the journal dirty flag
 * is set.  So after replaying the journals we clear s_startin the journals to
 * stop a second journal replay but leave the dirty bit set so that the kernel
 * will truncate the orphaned inodes. 
 */
errcode_t o2fsck_should_replay_journals(ocfs2_filesys *fs, int *should,
					int *has_dirty)
{
	uint16_t i, max_slots;
	char *buf = NULL;
	uint64_t blkno;
	errcode_t ret;
	ocfs2_cached_inode *cinode = NULL;
	int is_dirty;
	uint64_t contig;
	journal_superblock_t *jsb;

	*should = 0;
	max_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;

	ret = ocfs2_malloc_block(fs->fs_io, &buf);
	if (ret) {
		com_err(whoami, ret, "while allocating room to read journal "
			    "blocks");
		goto out;
	}

	jsb = (journal_superblock_t *)buf;

	for (i = 0; i < max_slots; i++) {
		ret = ocfs2_lookup_system_inode(fs, JOURNAL_SYSTEM_INODE, i,
						&blkno);
		if (ret) {
			com_err(whoami, ret, "while looking up the journal "
				"inode for slot %d", i);
			goto out;
		}

		if (cinode) {
			ocfs2_free_cached_inode(fs, cinode);
			cinode = NULL;
		}
		ret = ocfs2_read_cached_inode(fs, blkno, &cinode);
		if (ret) {
			com_err(whoami, ret, "while reading cached inode "
				"%"PRIu64" for slot %d's journal", blkno, i);
			goto out;
		}

		is_dirty = cinode->ci_inode->id1.journal1.ij_flags &
			   OCFS2_JOURNAL_DIRTY_FL;
		verbosef("slot %d JOURNAL_DIRTY_FL: %d\n", i, is_dirty);
		if (!is_dirty)
			continue;
		else
			*has_dirty = 1;

		ret = ocfs2_extent_map_get_blocks(cinode, 0, 1, &blkno,
						  &contig, NULL);
		if (ret) {
			com_err(whoami, ret, "while looking up the journal "
				"super block in slot %d's journal", i);
			goto out;
		}

		/* XXX be smarter about reading in the whole super block if it
		 * spans multiple blocks */
		ret = ocfs2_read_journal_superblock(fs, blkno, buf);
		if (ret) {
			com_err(whoami, ret, "while reading the journal "
				"super block in slot %d's journal", i);
			goto out;
		}

		if (jsb->s_start)
			*should = 1;
	}

out:
	if (buf)
		ocfs2_free(&buf);
	if (cinode)
		ocfs2_free_cached_inode(fs, cinode);
	return ret;
	
}

/* Try and replay the slots journals if they're dirty.  This only returns
 * a non-zero error if the caller should not continue. */
errcode_t o2fsck_replay_journals(ocfs2_filesys *fs, int *replayed)
{
	errcode_t err = 0, ret = 0;
	struct journal_info *jis, *ji;
	journal_superblock_t *jsb;
	char *buf = NULL;
	int journal_trouble = 0;
	uint16_t i, max_slots;
	ocfs2_bitmap *used_blocks = NULL;

	max_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;

	ret = ocfs2_block_bitmap_new(fs, "journal blocks",
				     &used_blocks);
	if (ret) {
		com_err(whoami, ret, "while allocating journal block bitmap"); 
		goto out;
	}

	ret = ocfs2_malloc_blocks(fs->fs_io, 1, &buf);
	if (ret) {
		com_err(whoami, ret, "while allocating room to read journal "
			    "blocks");
		goto out;
	}

	ret = ocfs2_malloc0(sizeof(struct journal_info) * max_slots, &jis);
	if (ret) {
		com_err(whoami, ret, "while allocating an array of block "
			 "numbers for journal replay");
		goto out;
	}

	printf("Checking each slot's journal.\n");

	for (i = 0, ji = jis; i < max_slots; i++, ji++) {
		ji->ji_used_blocks = used_blocks;
		ji->ji_revoke = RB_ROOT;
		ji->ji_slot = i;

		/* sets ji->ji_replay */
		err = prep_journal_info(fs, i, ji);
		if (err) {
			printf("Slot %d seems to have a corrupt journal.\n",
			       i);
			journal_trouble = 1;
			continue;
		}

		if (!ji->ji_replay) {
			verbosef("slot %d is clean\n", i);
			continue;
		}

		err = walk_journal(fs, i, ji, buf, 0);
		if (err) {
			printf("Slot %d's journal can not be replayed.\n", i);
			journal_trouble = 1;
		}
	}

	for (i = 0, ji = jis; i < max_slots; i++, ji++) {
		if (!ji->ji_replay)
			continue;

		printf("Replaying slot %d's journal.\n", i);

		err = walk_journal(fs, i, ji, buf, 1);
		if (err) {
			journal_trouble = 1;
			continue;
		} 

		jsb = ji->ji_jsb;
		/* reset the journal */
		jsb->s_start = 0;

		if (ji->ji_set_final_seq)
			jsb->s_sequence = ji->ji_final_seq + 1;

		/* we don't write back a clean 'mounted' bit here.  That would
		 * have to also include having recovered the orphan dir.  we
		 * updated s_start, though, so we won't replay the journal
		 * again. */
		err = ocfs2_write_journal_superblock(fs,
						     ji->ji_jsb_block,
						     (char *)ji->ji_jsb);
		if (err) {
			com_err(whoami, err, "while writing slot %d's journal "
				"super block", i);
			journal_trouble = 1;
		} else {
			printf("Slot %d's journal replayed successfully.\n",
			       i);
			*replayed = 1;
		}
	}

	/* this is awkward, but we want fsck -n to tell us as much as it
	 * can so we don't want to ask to proceed here.  */
	if (journal_trouble)
		printf("*** There were problems replaying journals.  Be "
		       "careful in telling fsck to make repairs to this "
		       "filesystem.\n");

	ret = 0;

out:
	if (jis) {
		for (i = 0, ji = jis; i < max_slots; i++, ji++) {
			if (ji->ji_jsb)
				ocfs2_free(&ji->ji_jsb);
			if (ji->ji_cinode)
				ocfs2_free_cached_inode(fs, 
							ji->ji_cinode);
			revoke_free_all(&ji->ji_revoke);
		}
		ocfs2_free(&jis);
	}

	if (buf)
		ocfs2_free(&buf);
	if (used_blocks)
		ocfs2_bitmap_free(used_blocks);

	return ret;
}

struct journal_check_info {
	errcode_t		i_error;
	uint32_t		i_clusters;
	ocfs2_fs_options	i_features;
};

struct journal_check_context {
	int				jc_max_slots;
	int				jc_this_slot;
	uint32_t			jc_max_clusters; /* Size of the
							    largest journal
							    found */
	ocfs2_fs_options		jc_max_features; /* Union all
							    features in
							    good journals */
	struct journal_check_info	*jc_info;	/* One per slot */
};

static errcode_t check_journals_func(o2fsck_state *ost,
				     ocfs2_cached_inode *ci,
				     struct journal_check_context *jc)
{
	errcode_t err, ret;
	ocfs2_filesys *fs = ost->ost_fs;
	uint64_t contig;
	uint64_t blkno;
	char *buf = NULL;
	journal_superblock_t *jsb;
	struct journal_check_info *ji = &(jc->jc_info[jc->jc_this_slot]);

	ret = ocfs2_malloc_blocks(fs->fs_io, 1, &buf);
	if (ret)
		goto out;

	err = ocfs2_extent_map_get_blocks(ci, 0, 1, &blkno, &contig, NULL);
	if (err) {
		ji->i_error = err;
		goto out;
	}

	ji->i_clusters = ci->ci_inode->i_clusters;

	err = ocfs2_read_journal_superblock(fs, blkno, buf);
	if (err) {
		ji->i_error = err;
		goto out;
	}

	jsb = (journal_superblock_t *)buf;
	ji->i_features.opt_compat = jsb->s_feature_compat;
	ji->i_features.opt_ro_compat = jsb->s_feature_ro_compat;
	ji->i_features.opt_incompat = jsb->s_feature_incompat;

	if (!ji->i_clusters) {
		ji->i_error = OCFS2_ET_JOURNAL_TOO_SMALL;
		goto out;
	}

	jc->jc_max_clusters = ocfs2_max(jc->jc_max_clusters,
					ci->ci_inode->i_clusters);
	jc->jc_max_features.opt_compat |= jsb->s_feature_compat;
	jc->jc_max_features.opt_ro_compat |= jsb->s_feature_ro_compat;
	jc->jc_max_features.opt_incompat |= jsb->s_feature_incompat;
	ji->i_error = 0;

out:
	if (buf)
		ocfs2_free(&buf);

	return ret;
}

static errcode_t fix_journals_func(o2fsck_state *ost,
				   ocfs2_cached_inode *ci,
				   struct journal_check_context *jc)
{
	errcode_t err, ret = 0;
	ocfs2_filesys *fs = ost->ost_fs;
	char fname[OCFS2_MAX_FILENAME_LEN];
	struct journal_check_info *ji = &(jc->jc_info[jc->jc_this_slot]);
	uint32_t min_clusters =
			ocfs2_clusters_in_bytes(fs,
						OCFS2_MIN_JOURNAL_SIZE);

	ocfs2_sprintf_system_inode_name(fname, OCFS2_MAX_FILENAME_LEN,
					JOURNAL_SYSTEM_INODE,
					jc->jc_this_slot);

	if (ji->i_error &&
	    (ji->i_error != OCFS2_ET_JOURNAL_TOO_SMALL) &&
	    (ji->i_error != OCFS2_ET_UNSUPP_FEATURE) &&
	    (ji->i_error != OCFS2_ET_RO_UNSUPP_FEATURE)) {
		if (prompt(ost, PY, PR_JOURNAL_FILE_INVALID,
			    "journal file %s is invalid, regenerate it?",
			    fname)) {
			err = ocfs2_make_journal(fs, ci->ci_blkno,
						 jc->jc_max_clusters,
						 &jc->jc_max_features);

			ji->i_error = err;
		}
		goto out;
	}

	if ((ji->i_error == OCFS2_ET_UNSUPP_FEATURE) ||
	    (ji->i_error == OCFS2_ET_RO_UNSUPP_FEATURE)) {
		if (prompt(ost, PN, PR_JOURNAL_UNKNOWN_FEATURE,
			   "journal file %s has unknown features.  "
			   "However, other journals have only known "
			   "features, so this is likely a corruption.  "
			   "If you think your filesystem may be newer "
			   "than this version of fsck.ocfs2, say N here "
			   "and grab the latest version of fsck.ocfs2.  "
			   "Reset the journal features to match other "
			   "journals?",
			   fname)) {
			err = ocfs2_make_journal(fs, ci->ci_blkno,
						 ocfs2_max(ji->i_clusters,
							   min_clusters),
						 &jc->jc_max_features);
			if (!err) {
				ji->i_features.opt_compat =
					jc->jc_max_features.opt_compat;
				ji->i_features.opt_ro_compat =
					jc->jc_max_features.opt_ro_compat;
				ji->i_features.opt_incompat =
					jc->jc_max_features.opt_incompat;
			}
			ji->i_error = err;
		}
	} else if ((ji->i_features.opt_compat !=
		    jc->jc_max_features.opt_compat) ||
		   (ji->i_features.opt_ro_compat !=
		    jc->jc_max_features.opt_ro_compat) ||
		   (ji->i_features.opt_incompat !=
		    jc->jc_max_features.opt_incompat)) {
		if (prompt(ost, PY, PR_JOURNAL_MISSING_FEATURE,
			   "journal file %s is missing features that "
			   "are set on other journal files.  Set these "
			   "features?",
			   fname)) {
			err = ocfs2_make_journal(fs, ci->ci_blkno,
						 ocfs2_max(ji->i_clusters,
							   min_clusters),
						 &jc->jc_max_features);
			if (!err) {
				ji->i_features.opt_compat =
					jc->jc_max_features.opt_compat;
				ji->i_features.opt_ro_compat =
					jc->jc_max_features.opt_ro_compat;
				ji->i_features.opt_incompat =
					jc->jc_max_features.opt_incompat;
			}
			ji->i_error = err;
		}
	}

	if (ji->i_clusters != jc->jc_max_clusters) {
		if (prompt(ost, PY, PR_JOURNAL_TOO_SMALL,
			   "journal file %s is too small, extend it?",
			   fname)) {
			err = ocfs2_make_journal(fs, ci->ci_blkno,
						 jc->jc_max_clusters,
						 &ji->i_features);
			ji->i_error = err;
		}
	}

out:
	return ret;
}

static errcode_t check_journal_walk(o2fsck_state *ost,
				    errcode_t (*func)(o2fsck_state *ost,
						      ocfs2_cached_inode *ci,
						      struct journal_check_context *jc),
				    struct journal_check_context *jc)
{
	errcode_t ret = 0;
	uint64_t blkno;
	ocfs2_filesys *fs = ost->ost_fs;
	uint16_t i, max_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots;
	ocfs2_cached_inode *ci = NULL;

	for (i = 0; i < max_slots; i++) {
		ret = ocfs2_lookup_system_inode(fs, JOURNAL_SYSTEM_INODE, i,
						&blkno);
		if (ret)
			break;

		ret = ocfs2_read_cached_inode(fs, blkno, &ci);
		if (ret)
			break;

		jc->jc_this_slot = i;
		ret = func(ost, ci, jc);
		if (ret)
			break;
	}

	if (ci)
		ocfs2_free_cached_inode(fs, ci);

	return ret;
}

errcode_t o2fsck_check_journals(o2fsck_state *ost)
{
	errcode_t ret;
	int i;
	ocfs2_filesys *fs = ost->ost_fs;
	int have_one_good_journal = 0, problem_is_consistent = 1;
	errcode_t known_problem = 0;
	struct journal_check_context jc = {
		.jc_max_slots = OCFS2_RAW_SB(fs->fs_super)->s_max_slots,
	};
	struct journal_check_info *ji;

	ret = ocfs2_malloc0(sizeof(struct journal_check_info) *
			    jc.jc_max_slots,
			    &jc.jc_info);
	if (ret) {
		com_err(whoami, ret, "while checking journals");
		goto out;
	}

	ret = check_journal_walk(ost, check_journals_func, &jc);
	if (ret) {
		com_err(whoami, ret, "while checking journals");
		goto out;
	}

	/*
	 * We now know the state of all our journals.  If we have at least
	 * one good journal, we have a sane state to fix the others from.
	 * We require all our journals to have identical configuration.
	 * Any inconsistencies (invalid size, bad feature flags) are
	 * probably corruption or a failed tunefs.
	 *
	 * If we don't have a good journal, but all the journals have the
	 * exact same problem, we may be able to handle it as well.  We
	 * currently know how to handle these problems:
	 *
	 * JOURNAL_TOO_SMALL
	 *
	 * We simply allocate a default journal size.
	 *
	 * UNSUPP_FEATURE & RO_UNSUPP_FEATURE
	 *
	 * If one journal has an unsupported feature bit set, it's probably
	 * corruption.  If all the journals have the exact same feature
	 * bit set, it's certainly a feature we don't understand, and we
	 * want the user to upgrade their fsck.
	 */
	for (i = 0; i < jc.jc_max_slots; i++) {
		ji = &jc.jc_info[i];

		if (!ji->i_error) {
			have_one_good_journal = 1;
			continue;
		}

		if ((ji->i_error != OCFS2_ET_JOURNAL_TOO_SMALL) &&
		    (ji->i_error != OCFS2_ET_UNSUPP_FEATURE) &&
		    (ji->i_error != OCFS2_ET_RO_UNSUPP_FEATURE)) {
			problem_is_consistent = 0;
			continue;
		}

		if (known_problem) {
			if (known_problem != ji->i_error)
				problem_is_consistent = 0;
			continue;
		}

		known_problem = ji->i_error;
	}

	if (!have_one_good_journal) {
		if (!problem_is_consistent || !known_problem) {
			ret = jc.jc_info[0].i_error;
			com_err(whoami, ret, "while checking journals");
			goto out;
		}
		if ((known_problem == OCFS2_ET_UNSUPP_FEATURE) ||
		    (known_problem == OCFS2_ET_RO_UNSUPP_FEATURE)) {
			com_err(whoami, known_problem,
				"on all journals.  Please upgrade to the "
				"latest version of fsck.ocfs2");
			ret = known_problem;
			goto out;
		}

		if (known_problem != OCFS2_ET_JOURNAL_TOO_SMALL) {
			ret = known_problem;
			com_err(whoami, ret, "for all journals");
			goto out;
		}

		/* Force a valid cluster count for the journals */
		jc.jc_max_clusters =
			ocfs2_clusters_in_bytes(fs,
						OCFS2_MIN_JOURNAL_SIZE);
	}

	ret = check_journal_walk(ost, fix_journals_func, &jc);

out:
	if (jc.jc_info)
		ocfs2_free(&jc.jc_info);

	return ret;
}

static errcode_t ocfs2_clear_journal_flag(ocfs2_filesys *fs,
					  struct ocfs2_dinode *di,
					  int slot)
{
	errcode_t ret = 0;

	if (!(di->i_flags & OCFS2_VALID_FL) ||
	    !(di->i_flags & OCFS2_SYSTEM_FL) ||
	    !(di->i_flags & OCFS2_JOURNAL_FL))
		return OCFS2_ET_INVALID_ARGUMENT;

	if (!(di->id1.journal1.ij_flags & OCFS2_JOURNAL_DIRTY_FL))
		goto bail;

	di->id1.journal1.ij_flags &= ~OCFS2_JOURNAL_DIRTY_FL;

	ret = ocfs2_write_inode(fs, di->i_blkno, (char *)di);
	if (!ret)
		printf("Slot %d's journal dirty flag removed\n", slot);

bail:
	return ret;
}

errcode_t o2fsck_clear_journal_flags(o2fsck_state *ost)
{
	if (!ost->ost_has_journal_dirty)
		return 0;

	return handle_slots_system_file(ost->ost_fs,
					JOURNAL_SYSTEM_INODE,
					ocfs2_clear_journal_flag);
}
