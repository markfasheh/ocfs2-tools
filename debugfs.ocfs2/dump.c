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
#include <inttypes.h>

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

	fprintf(out, "\tRoot Blknum: %"PRIu64"   System Dir Blknum: %"PRIu64"\n",
		sb->s_root_blkno,
		sb->s_system_dir_blkno);

	fprintf(out, "\tFirst Cluster Group Blknum: %"PRIu64"\n",
		sb->s_first_cluster_group);

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
	char tmp_str[30];

	if (S_ISREG(in->i_mode))
		str = "Regular";
	else if (S_ISDIR(in->i_mode))
		str = "Directory";
	else if (S_ISCHR(in->i_mode))
		str = "Char Device";
	else if (S_ISBLK(in->i_mode))
		str = "Block Device";
	else if (S_ISFIFO(in->i_mode))
		str = "FIFO";
	else if (S_ISLNK(in->i_mode))
		str = "Symbolic Link";
	else if (S_ISSOCK(in->i_mode))
		str = "Socket";
	else
		str = "Unknown";

	mode = in->i_mode & (S_IRWXU | S_IRWXG | S_IRWXO);

	flags = g_string_new(NULL);
	if (in->i_flags & OCFS2_VALID_FL)
		g_string_append (flags, "Valid ");
	if (in->i_flags & OCFS2_UNUSED2_FL)
		g_string_append (flags, "Unused2 ");
	if (in->i_flags & OCFS2_ORPHANED_FL)
		g_string_append (flags, "Orphan ");
	if (in->i_flags & OCFS2_UNUSED3_FL)
		g_string_append (flags, "Unused3 ");
	if (in->i_flags & OCFS2_SYSTEM_FL)
		g_string_append (flags, "System ");
	if (in->i_flags & OCFS2_SUPER_BLOCK_FL)
		g_string_append (flags, "Superblock ");
	if (in->i_flags & OCFS2_LOCAL_ALLOC_FL)
		g_string_append (flags, "Localalloc ");
	if (in->i_flags & OCFS2_BITMAP_FL)
		g_string_append (flags, "Allocbitmap ");
	if (in->i_flags & OCFS2_JOURNAL_FL)
		g_string_append (flags, "Journal ");
	if (in->i_flags & OCFS2_DLM_FL)
		g_string_append (flags, "DLM ");
	if (in->i_flags & OCFS2_CHAIN_FL)
		g_string_append (flags, "Chain ");

	fprintf(out, "\tInode: %"PRIu64"   Mode: 0%0o   Generation: %u\n",
	        in->i_blkno, mode, in->i_generation);

	fprintf(out, "\tType: %s   Flags: %s\n", str, flags->str);

	pw = getpwuid(in->i_uid);
	gr = getgrgid(in->i_gid);
	fprintf(out, "\tUser: %d (%s)   Group: %d (%s)   Size: %"PRIu64"\n",
	       in->i_uid, (pw ? pw->pw_name : "unknown"),
	       in->i_gid, (gr ? gr->gr_name : "unknown"),
	       in->i_size);

	fprintf(out, "\tLinks: %u   Clusters: %u\n", in->i_links_count, in->i_clusters);

	dump_disk_lock (out, &(in->i_disk_lock));

	str = ctime((time_t*)&in->i_ctime);
	fprintf(out, "\tctime: 0x%"PRIx64" -- %s", in->i_ctime, str);
	str = ctime((time_t*)&in->i_atime);
	fprintf(out, "\tatime: 0x%"PRIx64" -- %s", in->i_atime, str);
	str = ctime((time_t*)&in->i_mtime);
	fprintf(out, "\tmtime: 0x%"PRIx64" -- %s", in->i_mtime, str);
	str = ctime((time_t*)&in->i_dtime);
	fprintf(out, "\tdtime: 0x%"PRIx64" -- %s", in->i_dtime, str);

	fprintf(out, "\tLast Extblk: %"PRIu64"\n", in->i_last_eb_blk);
	if (in->i_suballoc_node == -1)
		strcpy(tmp_str, "Global");
	else
		sprintf(tmp_str, "%d", in->i_suballoc_node);
	fprintf(out, "\tSub Alloc Node: %s   Sub Alloc Bit: %u\n",
		tmp_str, in->i_suballoc_bit);

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
	fprintf(out, "\tLock Master: %u   Level: 0x%0x\n",
		dl->dl_master, dl->dl_level);

	return ;
}				/* dump_disk_lock */

/*
 * dump_extent_list()
 *
 */
void dump_chain_list (FILE *out, ocfs2_chain_list *cl)
{
	ocfs2_chain_rec *rec;
	int i;

	fprintf(out, "\tClusters Per Group: %u   Bits Per Cluster: %u\n",
		cl->cl_cpg, cl->cl_bpc);

	fprintf(out, "\tCount: %u   Next Free Record: %u\n",
		cl->cl_count, cl->cl_next_free_rec);

	if (!cl->cl_next_free_rec)
		goto bail;

	for (i = 0; i < cl->cl_next_free_rec; ++i) {
		rec = &(cl->cl_recs[i]);
		fprintf(out, "\t## Bits Total    Bits Free      Disk Offset\n");

		fprintf(out, "\t%-2d %-11u   %-12u   %"PRIu64"\n", i, rec->c_total,
			rec->c_free, rec->c_blkno);
		traverse_chain(out, rec->c_blkno);
		fprintf(out, "\n");
	}

bail:
	return ;
}				/* dump_chain_list */

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
		fprintf(out, "\t%-2d %-11u   %-12u   %"PRIu64"\n",
		       	i, rec->e_cpos, rec->e_clusters, rec->e_blkno);
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
	fprintf (out, "\tSubAlloc Bit: %u   SubAlloc Node: %u\n",
		 blk->h_suballoc_bit, blk->h_suballoc_node);

	fprintf (out, "\tBlknum: %"PRIu64"   Next Leaf: %"PRIu64"\n",
		 blk->h_blkno, blk->h_next_leaf_blk);

	return ;
}				/* dump_extent_block */

void traverse_chain(FILE *out, __u64 blknum)
{
	ocfs2_group_desc *bg;
	char *buf = NULL;
	__u32 buflen;

	buflen = 1 << gbls.blksz_bits;
	if (!(buf = memalign(buflen, buflen)))
		DBGFS_FATAL("%s", strerror(errno));

	do {
		if ((read_group (gbls.dev_fd, blknum, buf, buflen)) == -1) {
			printf("Not a group descriptor\n");
			goto bail;
		}
		bg = (ocfs2_group_desc *)buf;
		dump_group_descriptor(out, bg);
		blknum = bg->bg_next_group;
	} while (blknum);
	
bail:
	safefree (buf);
	return ;
}

/*
 * dump_group_descriptor()
 *
 */
void dump_group_descriptor (FILE *out, ocfs2_group_desc *blk)
{

	fprintf (out, "\tBlknum: %"PRIu64"   Next Group %"PRIu64"\n",
		 blk->bg_blkno,
		 blk->bg_next_group);

	fprintf (out, "\tFree Bits Count: %u   Group Bits: %u   "
		 "Group Size: %u\n",
		 blk->bg_free_bits_count,
		 blk->bg_bits,
		 blk->bg_size);

	fprintf (out, "\tParent Chain: %u   Parent Dinode: %"PRIu64"  "
		 "Generation: %u\n",
		 blk->bg_chain,
		 blk->bg_parent_dinode,
		 blk->bg_generation);

	return ;
}				/* dump_group_descriptor */

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
		fprintf(out, "\t%-15"PRIu64" %-4u %-4u %-2u %s\n",
			rec->inode, rec->rec_len,
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

	fprintf(out, "\tVersion: %u   Num Nodes: %u   Last Node: %u   "
		"Seqnum: %"PRIu64"\n",
		hdr->version, hdr->num_nodes, hdr->last_node, hdr->cfg_seq_num);

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

		fprintf(out, "\t%3d  %1u   %1u   %1u  %-15"PRIu64" "
			"%-15"PRIu64" %-15"PRIu64" %-15"PRIu64" ",
			i, pub->mounted, pub->vote, pub->dirty, pub->lock_id,
			pub->publ_seq_num, pub->comm_seq_num, pub->time);

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

		fprintf(out, "\t%3u %-2u %-1u %-15"PRIu64" %-15"PRIu64" %-s\n", i,
			vote->node, vote->open_handle, vote->lock_id,
			vote->vote_seq_num, vote_flag->str);

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
		 ntohl(jsb->s_blocksize), ntohl(jsb->s_maxlen), 
		 ntohl(jsb->s_first));
	fprintf (out, "\tFirst Commit ID: %u   Start Log Blknum: %u\n",
		 ntohl(jsb->s_sequence), ntohl(jsb->s_start));

	fprintf (out, "\tError: %d\n", ntohl(jsb->s_errno));

	/* XXX not sure what to do about swabbing these */
	fprintf (out, "\tFeatures Compat: %u   Incompat: %u   RO Compat: %u\n",
		 jsb->s_feature_compat, jsb->s_feature_incompat,
		 jsb->s_feature_ro_compat);

	fprintf (out, "\tJournal UUID: ");
	for(i = 0; i < 16; i++)
		fprintf (out, "%02X", jsb->s_uuid[i]);
	fprintf (out, "\n");

	fprintf (out, "\tFS Share Cnt: %u   Dynamic Superblk Blknum: %u\n",
		 ntohl(jsb->s_nr_users), ntohl(jsb->s_dynsuper));

	fprintf (out, "\tPer Txn Block Limit    Journal: %u    Data: %u\n",
		 ntohl(jsb->s_max_transaction), ntohl(jsb->s_max_trans_data));

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

	fprintf (out, "\tBlock %"PRIu64": ", blknum);

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
	fprintf (out, "\tBlock %"PRIu64": ", blknum);
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
		fprintf (out, "\tBlock %"PRIu64": ", start);
	else
		fprintf (out, "\tBlock %"PRIu64" to %"PRIu64": ",
			 start, (end - 1));
	fprintf (out, "Unknown -- Probably Data\n\n");

	return ;
}				/* dump_jbd_unknown */
