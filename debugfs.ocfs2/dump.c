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
	if (in->i_flags & OCFS2_HEARTBEAT_FL)
		g_string_append (flags, "Heartbeat ");
	if (in->i_flags & OCFS2_CHAIN_FL)
		g_string_append (flags, "Chain ");

	fprintf(out, "\tInode: %"PRIu64"   Mode: 0%0o   Generation: %u\n",
	        in->i_blkno, mode, in->i_generation);

	fprintf(out, "\tFS Generation: %u\n",in->i_fs_generation);

	fprintf(out, "\tType: %s   Flags: %s\n", str, flags->str);

	pw = getpwuid(in->i_uid);
	gr = getgrgid(in->i_gid);
	fprintf(out, "\tUser: %d (%s)   Group: %d (%s)   Size: %"PRIu64"\n",
	       in->i_uid, (pw ? pw->pw_name : "unknown"),
	       in->i_gid, (gr ? gr->gr_name : "unknown"),
	       in->i_size);

	fprintf(out, "\tLinks: %u   Clusters: %u\n", in->i_links_count, in->i_clusters);

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
		fprintf(out, "\tBitmap Total: %u   Used: %u   Free: %u\n",
		       in->id1.bitmap1.i_total, in->id1.bitmap1.i_used,
		       (in->id1.bitmap1.i_total - in->id1.bitmap1.i_used));

	if (flags)
		g_string_free (flags, 1);
	return ;
}				/* dump_inode */


/*
 * dump_chain_list()
 *
 */
void dump_chain_list (FILE *out, ocfs2_chain_list *cl)
{
	ocfs2_chain_rec *rec;
	int i;

	fprintf(out, "\tClusters per Group: %u   Bits per Cluster: %u\n",
		cl->cl_cpg, cl->cl_bpc);

	fprintf(out, "\tCount: %u   Next Free Rec: %u\n",
		cl->cl_count, cl->cl_next_free_rec);

	if (!cl->cl_next_free_rec)
		goto bail;

	fprintf(out, "\t##   %-10s   %-10s   %-10s   %s\n",
		"Total", "Free", "Used", "Block#");
	
	for (i = 0; i < cl->cl_next_free_rec; ++i) {
		rec = &(cl->cl_recs[i]);
		fprintf(out, "\t%-2d   %-10u   %-10u   %-10u   %"PRIu64"\n",
			i, rec->c_total, rec->c_free,
			(rec->c_total - rec->c_free), rec->c_blkno);
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

	fprintf(out, "\t## %-11s   %-12s   %-s\n", "Offset", "Clusters", "Block#");

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

/*
 * dump_group_descriptor()
 *
 */
void dump_group_descriptor (FILE *out, ocfs2_group_desc *grp, int index)
{
	if (!index) {
		fprintf (out, "\tGroup Chain: %u   Parent Inode: %"PRIu64"  "
			 "Generation: %u\n",
			 grp->bg_chain,
			 grp->bg_parent_dinode,
			 grp->bg_generation);
		fprintf(out, "\t##   %-15s   %-6s   %-6s   %-6s   %-6s\n",
			"Block#", "Total", "Free", "Used", "Size");
	}

	fprintf(out, "\t%-2d   %-15"PRIu64"   %-6u   %-6u   %-6u   %-6u\n",
		index, grp->bg_blkno, grp->bg_bits, grp->bg_free_bits_count,
		(grp->bg_bits - grp->bg_free_bits_count), grp->bg_size);

	return ;
}				/* dump_group_descriptor */

/*
 * dump_dir_entry()
 *
 */
int  dump_dir_entry (struct ocfs2_dir_entry *rec, int offset, int blocksize,
		     char *buf, void *priv_data)
{
	list_dir_opts *ls = (list_dir_opts *)priv_data;
	char tmp = rec->name[rec->name_len];
	ocfs2_dinode *di;
	char perms[20];
	char timestr[40];

	rec->name[rec->name_len] = '\0';

	if (!ls->long_opt) {
		fprintf(ls->out, "\t%-15"PRIu64" %-4u %-4u %-2u %s\n", rec->inode,
			rec->rec_len, rec->name_len, rec->file_type, rec->name);
	} else {
		memset(ls->buf, 0, ls->fs->fs_blocksize);
		ocfs2_read_inode(ls->fs, rec->inode, ls->buf);
		di = (ocfs2_dinode *)ls->buf;

		inode_perms_to_str(di->i_mode, perms, sizeof(perms));
		inode_time_to_str(di->i_mtime, timestr, sizeof(timestr));

		fprintf(ls->out, "\t%-15"PRIu64" %10s %3u %5u %5u %15"PRIu64" %s %s\n",
		       	rec->inode, perms, di->i_links_count, di->i_uid, di->i_gid,
			di->i_size, timestr, rec->name);
	}

	rec->name[rec->name_len] = tmp;

	return 0;
}

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
	ocfs2_super_block *sb = OCFS2_RAW_SB(gbls.fs->fs_super);

	tagflg = g_string_new (NULL);

	fprintf (out, "\tBlock %"PRIu64": ", blknum);

	switch (ntohl(header->h_blocktype)) {
	case JFS_DESCRIPTOR_BLOCK:
		fprintf (out, "Journal Descriptor\n");
		dump_jbd_header (out, header);

		fprintf (out, "\t%3s %-15s %-s\n", "No.", "Blocknum", "Flags");

		for (i = sizeof(journal_header_t); i < (1 << sb->s_blocksize_bits);
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
