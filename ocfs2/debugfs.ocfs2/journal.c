/*
 * journal.c
 *
 * reads the journal file
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
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
 * Authors: Sunil Mushran, Mark Fasheh
 */

#include <main.h>
#include <commands.h>
#include <dump.h>
#include <readfs.h>
#include <utils.h>
#include <journal.h>

extern dbgfs_gbls gbls;

/*
 * read_journal()
 *
 */
void read_journal (char *buf, __u64 buflen, FILE *out)
{
	char *block;
	int blocknum;
	journal_header_t *header;
	int last_metadata = 0;
	__u32 blksize = 1 << gbls.blksz_bits;
	__u64 len;
	char *p;

	len = buflen;
	p = buf;
	blocknum = -1;

	while (len) {
		blocknum++;
		block = p;
		header = (journal_header_t *) block;
		if (blocknum == 0) {
			fprintf (out, "block %d: ", blocknum);
			print_super_block ((journal_superblock_t *) block, out);
		} else if (header->h_magic == ntohl(JFS_MAGIC_NUMBER)) {
			if (last_metadata > 0) {
				print_metadata_blocks(last_metadata, 
						      blocknum - 1, out);
				last_metadata = 0;
			}
			fprintf (out, "block %d: ", blocknum);
			print_jbd_block (header, out);
		} else {
			if (last_metadata == 0)
				last_metadata = blocknum;
//			continue;
		}
		
		p += blksize;
		len -= blksize;
	}

	if (last_metadata > 0)
		print_metadata_blocks(last_metadata, blocknum, out);

	return ;
}				/* read_journal */

/*
 * print_header()
 *
 */
void print_header (journal_header_t *header, FILE *out)
{
	GString *jstr = NULL;

	jstr = g_string_new (NULL);
	get_journal_blktyp (ntohl(header->h_blocktype), jstr);

	fprintf (out, "\tSeq: %u   Type: %d (%s)\n", ntohl(header->h_sequence),
		 ntohl(header->h_blocktype), jstr->str);

	if (jstr)
		g_string_free (jstr, 1);
	return;
}				/* print_header */

/*
 * print_super_block()
 *
 */
void print_super_block (journal_superblock_t *jsb, FILE *out) 
{
	int i;

	fprintf (out, "Journal Superblock\n");

	print_header (&(jsb->s_header), out);

	fprintf (out, "\tBlocksize: %u   Total Blocks: %u   First Block: %u\n",
		 jsb->s_blocksize, jsb->s_maxlen, jsb->s_first);
	fprintf (out, "\tFirst Commit ID: %u   Start Log Blknum: %u\n",
		 jsb->s_sequence, jsb->s_start);

	fprintf (out, "\tError: %u\n", jsb->s_errno);

	fprintf (out, "\tFeatures Compat: %u   Incompat: %u   RO Compat: %u\n",
		 jsb->s_feature_compat, jsb->s_feature_incompat,
		 jsb->s_feature_ro_compat);

	fprintf (out, "\tJournal UUID: ");
	for(i = 0; i < 16; i++)
		fprintf (out, "%02X", jsb->s_uuid[i]);
	fprintf (out, "\n");

	fprintf (out, "\tFS Share Cnt: %u   Dynamic Superblk Blknum: %u\n",
		 jsb->s_nr_users, jsb->s_dynsuper);

	fprintf (out, "\tPer Txn Block Limit    Journal: %u    Data: %u\n",
		 jsb->s_max_transaction, jsb->s_max_trans_data);

	return;
}				/* print_super_block */


/*
 * print_metadata_blocks()
 *
 */
void print_metadata_blocks (int start, int end, FILE *out)
{
	if (start == end)
		fprintf(out, "block %d: ", start);
	else
		fprintf(out, "block %d --> block %d: ", start, end);
	fprintf(out, "Filesystem Metadata\n\n");
	return;
}				/* print_metadata_blocks */

/*
 * print_jbd_block()
 *
 */
void print_jbd_block (journal_header_t *header, FILE *out)
{
	int i;
	int j;
	int count = 0;
	GString *tagflg = NULL;
	/* for descriptors */
	journal_block_tag_t *tag;
	journal_revoke_header_t *revoke;
	char *blk = (char *) header;
	__u32 *blocknr;
	char *uuid;

	tagflg = g_string_new (NULL);

	switch (ntohl(header->h_blocktype)) {
	case JFS_DESCRIPTOR_BLOCK:
		fprintf (out, "Journal Descriptor\n");
		print_header (header, out);

		fprintf (out, "\t%3s %-15s %-s\n", "No.", "Blocknum", "Flags");

		for (i = sizeof(journal_header_t); i < (1 << gbls.blksz_bits);
		     i+=sizeof(journal_block_tag_t)) {
			tag = (journal_block_tag_t *) &blk[i];

			get_tag_flag(ntohl(tag->t_flags), tagflg);
			fprintf (out, "\t%2d. %-15u %-s\n",
				 count, ntohl(tag->t_blocknr), tagflg->str);
			g_string_truncate (tagflg, 0);

			if (tag->t_flags & htonl(JFS_FLAG_LAST_TAG))
				break;

			/* skip the uuid. */
			if (!(tag->t_flags & htonl(JFS_FLAG_SAME_UUID))) {
				uuid = &blk[i + sizeof(journal_block_tag_t)];
				fprintf (out, "\tUUID: ");
				for(j = 0; j < 16; j++)
					fprintf (out, "%02X",uuid[j]);
				fprintf (out, "\n");
				i += 16;
			}
			count++;
		}
		break;

	case JFS_COMMIT_BLOCK:
		fprintf(out, "Journal Commit Block\n");
		print_header (header, out);
		break;

	case JFS_REVOKE_BLOCK:							/*TODO*/
		fprintf(out, "Journal Revoke Block\n");
		print_header (header, out);
		revoke = (journal_revoke_header_t *) blk;

		fprintf(out, "\tr_count:\t\t%d\n", ntohl(revoke->r_count));
		count = (ntohl(revoke->r_count) - 
			 sizeof(journal_revoke_header_t)) / sizeof(__u32);
		blocknr = (__u32 *) &blk[sizeof(journal_revoke_header_t)];
		for(i = 0; i < count; i++)
			fprintf(out, "\trevoke[%d]:\t\t%u\n", i, ntohl(blocknr[i]));
		break;

	default:
		fprintf(out, "Unknown Block Type\n");
		break;
	}

	if (tagflg)
		g_string_free (tagflg, 1);

	return;
}				/* print_jbd_block */
