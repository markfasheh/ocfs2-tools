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
void read_journal (char *buf, __u64 buflen)
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
			printf("block %d: ", blocknum);
			print_super_block((journal_superblock_t *) block);
		} else if (header->h_magic == ntohl(JFS_MAGIC_NUMBER)) {
			if (last_metadata > 0) {
				print_metadata_blocks(last_metadata, 
						      blocknum - 1);
				last_metadata = 0;
			}
			printf("block %d: ", blocknum);
			print_jbd_block(header);
		} else {
			if (last_metadata == 0)
				last_metadata = blocknum;
//			continue;
		}
		
		p += blksize;
		len -= blksize;
	}

	if (last_metadata > 0)
		print_metadata_blocks(last_metadata, blocknum);

	return ;
}				/* read_journal */

#define PRINT_FIELD(name, size, field)   printf("\t" #field ":\t\t" size \
					 "\n", ntohl(name->field))

/*
 * print_header()
 *
 */
void print_header (journal_header_t *header, char *hdr)
{
	printf("\t%s->h_magic:\t\t0x%x\n", hdr, ntohl(header->h_magic));
	printf("\t%s->h_blocktype:\t\t%u ", hdr, ntohl(header->h_blocktype));

	switch (ntohl(header->h_blocktype)) {
	case JFS_DESCRIPTOR_BLOCK:
		printf("(JFS_DESCRIPTOR_BLOCK)");
		break;
	case JFS_COMMIT_BLOCK:
		printf("(JFS_COMMIT_BLOCK)");
		break;
	case JFS_SUPERBLOCK_V1:
		printf("(JFS_SUPERBLOCK_V1)");
		break;
	case JFS_SUPERBLOCK_V2:
		printf("(JFS_SUPERBLOCK_V2)");
		break;
	case JFS_REVOKE_BLOCK:
		printf("(JFS_REVOKE_BLOCK)");
		break;
	}
	printf("\n");
	printf("\t%s->h_sequence:\t\t%u\n", hdr, ntohl(header->h_sequence));
	return;
}				/* print_header */

/*
 * print_super_block()
 *
 */
void print_super_block (journal_superblock_t *sb) 
{
	int i;

	printf("Journal Superblock\n");

	print_header(&(sb->s_header), "s_header");

	PRINT_FIELD(sb, "%u", s_blocksize);
	PRINT_FIELD(sb, "%u", s_maxlen);
	PRINT_FIELD(sb, "%u", s_first);
	PRINT_FIELD(sb, "%u", s_sequence);
	PRINT_FIELD(sb, "%u", s_start);
	PRINT_FIELD(sb, "%d", s_errno);
	PRINT_FIELD(sb, "%u", s_feature_compat);
	PRINT_FIELD(sb, "%u", s_feature_incompat);
	PRINT_FIELD(sb, "%u", s_feature_ro_compat);

	printf("\ts_uuid[16]:\t\t");
	for(i = 0; i < 16; i++)
		printf("%x ", sb->s_uuid[i]);
	printf("\n");


	PRINT_FIELD(sb, "%u", s_nr_users);
	PRINT_FIELD(sb, "%u", s_dynsuper);
	PRINT_FIELD(sb, "%u", s_max_transaction);
	PRINT_FIELD(sb, "%u", s_max_trans_data);

	return;
}				/* print_super_block */


/*
 * print_metadata_blocks()
 *
 */
void print_metadata_blocks (int start, int end)
{
	if (start == end)
		printf("block %d: ", start);
	else
		printf("block %d --> block %d: ", start, end);
	printf("Filesystem Metadata\n\n");
	return;
}				/* print_metadata_blocks */

/*
 * print_tag_flag()
 *
 */
void print_tag_flag (__u32 flags)
{

	if (flags == 0) {
		printf("(none)");
		goto done;
	}
	if (flags & JFS_FLAG_ESCAPE)
		printf("JFS_FLAG_ESCAPE ");
	if (flags & JFS_FLAG_SAME_UUID)
		printf("JFS_FLAG_SAME_UUID ");
	if (flags & JFS_FLAG_DELETED)
		printf("JFS_FLAG_DELETED ");
	if (flags & JFS_FLAG_LAST_TAG)
		printf("JFS_FLAG_LAST_TAG");
done:
	return;
}				/* print_tag_flag */

/*
 * print_jbd_block()
 *
 */
void print_jbd_block (journal_header_t *header)
{
	int i;
	int j;
	int count = 0;
	/* for descriptors */
	journal_block_tag_t *tag;
	journal_revoke_header_t *revoke;
	char *blk = (char *) header;
	__u32 *blocknr;
	char *uuid;

	switch(ntohl(header->h_blocktype)) {
	case JFS_DESCRIPTOR_BLOCK:
		printf("Journal Descriptor\n");
		print_header(header, "hdr");
		for(i = sizeof(journal_header_t); i < (1 << gbls.blksz_bits);
		    i+=sizeof(journal_block_tag_t)) {
			tag = (journal_block_tag_t *) &blk[i];
			printf("\ttag[%d]->t_blocknr:\t\t%u\n", count,
			       ntohl(tag->t_blocknr));
			printf("\ttag[%d]->t_flags:\t\t", count);
			print_tag_flag(ntohl(tag->t_flags));
			printf("\n");
			if (tag->t_flags & htonl(JFS_FLAG_LAST_TAG))
				break;

			/* skip the uuid. */
			if (!(tag->t_flags & htonl(JFS_FLAG_SAME_UUID))) {
				uuid = &blk[i + sizeof(journal_block_tag_t)];
				printf("\ttag[%d] uuid:\t\t", count);
				for(j = 0; j < 16; j++)
					printf("%x ", uuid[j]);
				printf("\n");
				i += 16;
			}
			count++;
		}
		break;

	case JFS_COMMIT_BLOCK:
		printf("Journal Commit Block\n");
		print_header(header, "hdr");
		break;

	case JFS_REVOKE_BLOCK:
		printf("Journal Revoke Block\n");
		print_header(header, "r_header");
		revoke = (journal_revoke_header_t *) blk;
		printf("\tr_count:\t\t%d\n", ntohl(revoke->r_count));
		count = (ntohl(revoke->r_count) - 
			 sizeof(journal_revoke_header_t)) / sizeof(__u32);
		blocknr = (__u32 *) &blk[sizeof(journal_revoke_header_t)];
		for(i = 0; i < count; i++)
			printf("\trevoke[%d]:\t\t%u\n", i, ntohl(blocknr[i]));
		break;

	default:
		printf("Unknown block type\n");
		break;
	}

	return;
}				/* print_jbd_block */
