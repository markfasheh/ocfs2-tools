/*
 * dump.c
 *
 * dumps ocfs2 structures
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
 * Authors: Sunil Mushran
 */

#include <main.h>

extern dbgfs_gbls gbls;

/*
 * dump_super_block()
 *
 */
void dump_super_block(FILE *out, ocfs2_super_block *sb)
{
	int i;
	char *str;

	fprintf(out, "\tRevision: %u.%u\n", sb->s_major_rev_level, sb->s_minor_rev_level);
	fprintf(out, "\tMount Count: %u   Max Mount Count: %u\n", sb->s_mnt_count,
	       sb->s_max_mnt_count);

	fprintf(out, "\tState: %u   Errors: %u\n", sb->s_state, sb->s_errors);

	str = ctime((time_t*)&sb->s_lastcheck);
	fprintf(out, "\tCheck Interval: %u   Last Check: %s", sb->s_checkinterval, str);

	fprintf(out, "\tCreator OS: %u\n", sb->s_creator_os);
	fprintf(out, "\tFeature Compat: %u   Incompat: %u   RO Compat: %u\n",
	       sb->s_feature_compat, sb->s_feature_incompat,
	       sb->s_feature_ro_compat);

	fprintf(out, "\tRoot Blknum: %llu   System Dir Blknum: %llu\n",
		(unsigned long long)sb->s_root_blkno,
		(unsigned long long)sb->s_system_dir_blkno);

	fprintf(out, "\tBlock Size Bits: %u   Cluster Size Bits: %u\n",
	       sb->s_blocksize_bits, sb->s_clustersize_bits);

	fprintf(out, "\tMax Nodes: %u\n", sb->s_max_nodes);
	fprintf(out, "\tLabel: %s\n", sb->s_label);
	fprintf(out, "\tUUID: ");
	for (i = 0; i < 16; i++)
		fprintf(out, "%02X", sb->s_uuid[i]);
	fprintf(out, "\n");

	return ;
}				/* dump_super_block */

/*
 * dump_local_alloc()
 *
 */
void dump_local_alloc (FILE *out, ocfs2_local_alloc *loc)
{
	fprintf(out, "\tLocal Bitmap Offset: %u   Size: %u\n",
	       loc->la_bm_off, loc->la_size);

	fprintf(out, "\tTotal: %u   Used: %u   Clear: %u\n",
	       loc->la_bm_bits, loc->la_bits_set,
	       (loc->la_bm_bits - loc->la_bits_set));

	return ;
}				/* dump_local_alloc */

/*
 * dump_inode()
 *
 */
void dump_inode(FILE *out, ocfs2_dinode *in)
{
	struct passwd *pw;
	struct group *gr;
	char *str;
	__u16 mode;
	GString *flags = NULL;

	if (S_ISREG(in->i_mode))
		str = "regular";
	else if (S_ISDIR(in->i_mode))
		str = "directory";
	else if (S_ISCHR(in->i_mode))
		str = "char device";
	else if (S_ISBLK(in->i_mode))
		str = "block device";
	else if (S_ISFIFO(in->i_mode))
		str = "fifo";
	else if (S_ISLNK(in->i_mode))
		str = "symbolic link";
	else if (S_ISSOCK(in->i_mode))
		str = "socket";
	else
		str = "unknown";

	mode = in->i_mode & (S_IRWXU | S_IRWXG | S_IRWXO);

	flags = g_string_new(NULL);
	if (in->i_flags & OCFS2_VALID_FL)
		g_string_append (flags, "valid ");
	if (in->i_flags & OCFS2_UNUSED2_FL)
		g_string_append (flags, "unused2 ");
	if (in->i_flags & OCFS2_ORPHANED_FL)
		g_string_append (flags, "orphan ");
	if (in->i_flags & OCFS2_UNUSED3_FL)
		g_string_append (flags, "unused3 ");
	if (in->i_flags & OCFS2_SYSTEM_FL)
		g_string_append (flags, "system ");
	if (in->i_flags & OCFS2_SUPER_BLOCK_FL)
		g_string_append (flags, "superblock ");
	if (in->i_flags & OCFS2_LOCAL_ALLOC_FL)
		g_string_append (flags, "localalloc ");
	if (in->i_flags & OCFS2_BITMAP_FL)
		g_string_append (flags, "allocbitmap ");
	if (in->i_flags & OCFS2_JOURNAL_FL)
		g_string_append (flags, "journal ");
	if (in->i_flags & OCFS2_DLM_FL)
		g_string_append (flags, "dlm ");

	fprintf(out, "\tInode: %llu   Mode: 0%0o   Generation: %u\n",
	        (unsigned long long)in->i_blkno, mode, in->i_generation);

	fprintf(out, "\tType: %s   Flags: %s\n", str, flags->str);

	pw = getpwuid(in->i_uid);
	gr = getgrgid(in->i_gid);
	fprintf(out, "\tUser: %d (%s)   Group: %d (%s)   Size: %llu\n",
	       in->i_uid, (pw ? pw->pw_name : "unknown"),
	       in->i_gid, (gr ? gr->gr_name : "unknown"),
	       (unsigned long long)in->i_size);

	fprintf(out, "\tLinks: %u   Clusters: %u\n", in->i_links_count, in->i_clusters);

	dump_disk_lock (out, &(in->i_disk_lock));

	str = ctime((time_t*)&in->i_ctime);
	fprintf(out, "\tctime: 0x%llx -- %s", (unsigned long long)in->i_ctime, str);
	str = ctime((time_t*)&in->i_atime);
	fprintf(out, "\tatime: 0x%llx -- %s", (unsigned long long)in->i_atime, str);
	str = ctime((time_t*)&in->i_mtime);
	fprintf(out, "\tmtime: 0x%llx -- %s", (unsigned long long)in->i_mtime, str);
	str = ctime((time_t*)&in->i_dtime);
	fprintf(out, "\tdtime: 0x%llx -- %s", (unsigned long long)in->i_dtime, str);

	fprintf(out, "\tLast Extblk: %llu\n", (unsigned long long)in->i_last_eb_blk);
	fprintf(out, "\tSub Alloc Node: %u   Sub Alloc Blknum: %llu\n",
	       in->i_suballoc_node, (unsigned long long)in->i_suballoc_blkno);

	if (in->i_flags & OCFS2_BITMAP_FL)
		fprintf(out, "\tBitmap Total: %u   Used: %u   Clear: %u\n",
		       in->id1.bitmap1.i_total, in->id1.bitmap1.i_used,
		       (in->id1.bitmap1.i_total - in->id1.bitmap1.i_used));

	if (flags)
		g_string_free (flags, 1);
	return ;
}				/* dump_inode */

/*
 * dump_disk_lock()
 *
 */
void dump_disk_lock (FILE *out, ocfs2_disk_lock *dl)
{
#if 0
	ocfs2_super_block *sb = &((gbls.superblk)->id2.i_super);
	int i, j, k;
	__u32 node_map;
#endif

	fprintf(out, "\tLock Master: %u   Level: 0x%0x\n",
		dl->dl_master, dl->dl_level);

#if 0
	fprintf(out, "\tLock Master: %u   Level: 0x%0x   Seqnum: %llu\n",
	       dl->dl_master, dl->dl_level, (unsigned long long)dl->dl_seq_num);

	fprintf(out, "\tLock Map: ");
	for (i = 0, j = 0; i < 8 && j < sb->s_max_nodes; ++i) {
		if (i)
			fprintf(out, "               ");
		node_map = dl->dl_node_map[i];
		for (k = 0; k < 32 && j < sb->s_max_nodes; k++, j++) {
			fprintf (out, "%d", ((node_map & (1 << k)) ? 1 : 0));
			if (!((k + 1) % 8))
				fprintf (out, " ");
		}
		fprintf (out, "\n");
	}
#endif

	return ;
}				/* dump_disk_lock */

/*
 * dump_extent_list()
 *
 */
void dump_extent_list (FILE *out, ocfs2_extent_list *ext)
{
	ocfs2_extent_rec *rec;
	int i;

	fprintf(out, "\tTree Depth: %u   Count: %u   Next Free Rec: %u\n",
		ext->l_tree_depth, ext->l_count, ext->l_next_free_rec);

	if (!ext->l_next_free_rec)
		goto bail;

	fprintf(out, "\t## File Offset   Num Clusters   Disk Offset\n");

	for (i = 0; i < ext->l_next_free_rec; ++i) {
		rec = &(ext->l_recs[i]);
		fprintf(out, "\t%-2d %-11u   %-12u   %llu\n", i, rec->e_cpos,
			rec->e_clusters, (unsigned long long)rec->e_blkno);
	}

bail:
	return ;
}				/* dump_extent_list */

/*
 * dump_extent_block()
 *
 */
void dump_extent_block (FILE *out, ocfs2_extent_block *blk)
{
	fprintf (out, "\tSubAlloc Blknum: %llu   SubAlloc Node: %u\n",
		 (unsigned long long)blk->h_suballoc_blkno, blk->h_suballoc_node);

	fprintf (out, "\tBlknum: %llu   Parent: %llu   Next Leaf: %llu\n",
		 (unsigned long long)blk->h_blkno,
		 (unsigned long long)blk->h_parent_blk,
		 (unsigned long long)blk->h_next_leaf_blk);

	return ;
}				/* dump_extent_block */

/*
 * dump_dir_entry()
 *
 */
void dump_dir_entry (FILE *out, GArray *arr)
{
	struct ocfs2_dir_entry *rec;
	int i;

	fprintf(out, "\t%-15s %-4s %-4s %-2s %-4s\n",
		"Inode", "Rlen", "Nlen", "Ty", "Name");

	for (i = 0; i < arr->len; ++i) {
		rec = &(g_array_index(arr, struct ocfs2_dir_entry, i));
		fprintf(out, "\t%-15llu %-4u %-4u %-2u %s\n",
			(unsigned long long)rec->inode, rec->rec_len,
			rec->name_len, rec->file_type, rec->name);
	}

	return ;
}				/* dump_dir_entry */

/*
 * dump_config()
 *
 */
void dump_config (FILE *out, char *buf)
{
	char *p;
	ocfs_node_config_hdr *hdr;
	ocfs_node_config_info *node;
	ocfs2_super_block *sb = &((gbls.superblk)->id2.i_super);
	__u16 port;
	char addr[32];
	struct in_addr ina;
	int i, j;

	hdr = (ocfs_node_config_hdr *)buf;

	fprintf(out, "\tVersion: %u   Num Nodes: %u   Last Node: %u   Seqnum: %llu\n",
		hdr->version, hdr->num_nodes, hdr->last_node,
		(unsigned long long)hdr->cfg_seq_num);

	dump_disk_lock (out, &(hdr->disk_lock));

	fprintf(out, "\t%-3s %-32s %-15s %-6s %s\n",
		"###", "Name", "IP Address", "Port", "UUID");

	p = buf + (2 << gbls.blksz_bits);
	for (i = 0; i < sb->s_max_nodes; ++i) {
		node = (ocfs_node_config_info *)p;
		if (!*node->node_name)
			continue;

		port  = htonl(node->ipc_config.ip_port);

		ina.s_addr = node->ipc_config.addr_u.ip_addr4;
		strcpy (addr, inet_ntoa(ina));

		fprintf(out, "\t%3u %-32s %-15s %-6u ", i, node->node_name,
			addr, port);
		for (j = 0; j < OCFS2_GUID_LEN; j++)
			fprintf(out, "%c", node->guid.guid[j]);
		fprintf(out, "\n");
		p += (1 << gbls.blksz_bits);
	}

	return ;
}				/* dump_config */

/*
 * dump_publish()
 *
 */
void dump_publish (FILE *out, char *buf)
{
	ocfs_publish *pub;
	char *p;
	GString *pub_flag;
	ocfs2_super_block *sb = &((gbls.superblk)->id2.i_super);
	__u32 i, j;

	fprintf(out, "\t%-3s %-3s %-3s %-3s %-15s %-15s %-15s %-15s %-*s %-s\n",
		"###", "Mnt", "Vot", "Dty", "LockId", "Seq", "Comm Seq", "Time",
		sb->s_max_nodes, "Map", "Type");

	p = buf + ((2 + 4 + sb->s_max_nodes) << gbls.blksz_bits);
	for (i = 0; i < sb->s_max_nodes; ++i) {
		pub = (ocfs_publish *)p;

		pub_flag = g_string_new (NULL);
		get_publish_flag (pub->vote_type, pub_flag);

		fprintf(out, "\t%3d  %1u   %1u   %1u  %-15llu %-15llu %-15llu %-15llu ",
			i, pub->mounted, pub->vote, pub->dirty,
			(unsigned long long)pub->lock_id,
			(unsigned long long)pub->publ_seq_num,
			(unsigned long long)pub->comm_seq_num,
			(unsigned long long)pub->time);

		for (j = 0; j < sb->s_max_nodes; j++)
			fprintf (out, "%d",
				 ((pub->vote_map[j / sizeof(pub->vote_map[0])] &
				   (1 << (j % sizeof(pub->vote_map[0])))) ? 1 : 0));

		fprintf(out, " %-s\n", pub_flag->str);

		g_string_free (pub_flag, 1);

		p += (1 << gbls.blksz_bits);
	}

	return ;	
}				/* dump_publish */

/*
 * dump_vote()
 *
 */
void dump_vote (FILE *out, char *buf)
{
	ocfs_vote *vote;
	char *p;
	GString *vote_flag;
	ocfs2_super_block *sb = &((gbls.superblk)->id2.i_super);
	__u32 i;

	fprintf(out, "\t%-3s %-2s %-1s %-15s %-15s %-s\n",
		"###", "NV", "O", "LockId", "Seq", "Type");

	p = buf + ((2 + 4 + sb->s_max_nodes + sb->s_max_nodes) << gbls.blksz_bits);
	for (i = 0; i < sb->s_max_nodes; ++i) {
		vote = (ocfs_vote *)p;

		vote_flag = g_string_new (NULL);
		get_vote_flag (vote->type, vote_flag);

		fprintf(out, "\t%3u %-2u %-1u %-15llu %-15llu %-s\n", i,
			vote->node, vote->open_handle,
			(unsigned long long)vote->lock_id,
			(unsigned long long)vote->vote_seq_num, vote_flag->str);

		g_string_free (vote_flag, 1);
		p += (1 << gbls.blksz_bits);
	}

	return ;
}				/* dump_vote */

/*
 * dump_jbd_header()
 *
 */
void dump_jbd_header (FILE *out, journal_header_t *header)
{
	GString *jstr = NULL;

	jstr = g_string_new (NULL);
	get_journal_blktyp (ntohl(header->h_blocktype), jstr);

	fprintf (out, "\tSeq: %u   Type: %d (%s)\n", ntohl(header->h_sequence),
		 ntohl(header->h_blocktype), jstr->str);

	if (jstr)
		g_string_free (jstr, 1);
	return;
}				/* dump_jbd_header */

/*
 * dump_jbd_superblock()
 *
 */
void dump_jbd_superblock (FILE *out, journal_superblock_t *jsb)
{
	int i;

	fprintf (out, "\tBlock 0: Journal Superblock\n");

	dump_jbd_header (out, &(jsb->s_header));

	fprintf (out, "\tBlocksize: %u   Total Blocks: %u   First Block: %u\n",
		 jsb->s_blocksize, jsb->s_maxlen, jsb->s_first);
	fprintf (out, "\tFirst Commit ID: %u   Start Log Blknum: %u\n",
		 jsb->s_sequence, jsb->s_start);

	fprintf (out, "\tError: %d\n", jsb->s_errno);

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

	fprintf (out, "\n");

	return;
}				/* dump_jbd_superblock */

/*
 * dump_jbd_block()
 *
 */
void dump_jbd_block (FILE *out, journal_header_t *header, __u64 blknum)
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

	fprintf (out, "\tBlock %llu: ", (unsigned long long)blknum);

	switch (ntohl(header->h_blocktype)) {
	case JFS_DESCRIPTOR_BLOCK:
		fprintf (out, "Journal Descriptor\n");
		dump_jbd_header (out, header);

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
		dump_jbd_header (out, header);
		break;

	case JFS_REVOKE_BLOCK:							/*TODO*/
		fprintf(out, "Journal Revoke Block\n");
		dump_jbd_header (out, header);
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
	fprintf (out, "\n");

	if (tagflg)
		g_string_free (tagflg, 1);

	return;
}				/* dump_jbd_block */

/*
 * dump_jbd_metadata()
 *
 */
void dump_jbd_metadata (FILE *out, int type, char *buf, __u64 blknum)
{
	fprintf (out, "\tBlock %llu: ", (unsigned long long)blknum);
	switch (type) {
	case 1:
		fprintf(out, "Inode\n");
		dump_inode (out, (ocfs2_dinode *)buf);
		fprintf (out, "\n");
		break;
	case 2:
		fprintf(out, "Extent\n");
		dump_extent_block (out, (ocfs2_extent_block *)buf);
		fprintf (out, "\n");
		break;
	default:
		fprintf (out, "TODO\n\n");
		break;
	}

	return ;
}				/* dump_jbd_metadata */

/*
 * dump_jbd_unknown()
 *
 */
void dump_jbd_unknown (FILE *out, __u64 start, __u64 end)
{
	if (start == end - 1)
		fprintf (out, "\tBlock %llu: ", (unsigned long long)start);
	else
		fprintf (out, "\tBlock %llu to %llu: ", (unsigned long long)start,
			 (unsigned long long)(end - 1));
	fprintf (out, "Unknown -- Probably Data\n\n");

	return ;
}				/* dump_jbd_unknown */
