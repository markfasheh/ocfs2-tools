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
#include <commands.h>
#include <dump.h>
#include <readfs.h>
#include <utils.h>

/*
 * dump_super_block()
 *
 */
void dump_super_block(ocfs2_super_block *sb)
{
	int i;
	char *str;

	printf("Revision: %u.%u\n", sb->s_major_rev_level, sb->s_minor_rev_level);
	printf("Mount Count: %u   Max Mount Count: %u\n", sb->s_mnt_count,
	       sb->s_max_mnt_count);

	printf("State: %u   Errors: %u\n", sb->s_state, sb->s_errors);

	str = ctime((time_t*)&sb->s_lastcheck);
	printf("Check Interval: %u   Last Check: %s", sb->s_checkinterval, str);

	printf("Creator OS: %u\n", sb->s_creator_os);
	printf("Feature Compat: %u   Incompat: %u   RO Compat: %u\n",
	       sb->s_feature_compat, sb->s_feature_incompat,
	       sb->s_feature_ro_compat);

	printf("Root Blknum: %llu   System Dir Blknum: %llu\n",
	       sb->s_root_blkno, sb->s_system_dir_blkno);

	printf("Block Size Bits: %u   Cluster Size Bits: %u\n",
	       sb->s_blocksize_bits, sb->s_clustersize_bits);

	printf("Max Nodes: %u\n", sb->s_max_nodes);
	printf("Label: %s\n", sb->s_label);
	printf("UUID: ");
	for (i = 0; i < 16; i++)
		printf("%02X ", sb->s_uuid[i]);
	printf("\n");

	return ;
}				/* dump_super_block */

/*
 * dump_inode()
 *
 */
void dump_inode(ocfs2_dinode *in)
{
	struct passwd *pw;
	struct group *gr;
	char *str;
	ocfs2_disk_lock *dl;
	int i;
	__u16 mode;
	GString *flags = NULL;

/*
Inode: 32001   Type: directory    Mode:  0755   Flags: 0x0   Generation: 721849
User:     0   Group:     0   Size: 4096
File ACL: 0    Directory ACL: 0
Links: 10   Clusters: 8
Fragment:  Address: 0    Number: 0    Size: 0
ctime: 0x40075ba0 -- Thu Jan 15 22:33:52 2004
atime: 0x40075b9d -- Thu Jan 15 22:33:49 2004
mtime: 0x40075ba0 -- Thu Jan 15 22:33:52 2004
BLOCKS:
(0):66040
TOTAL: 1

Inode: 64004   Type: regular    Mode:  0644   Flags: 0x0   Generation: 721925
User:     0   Group:     0   Size: 1006409
File ACL: 0    Directory ACL: 0
Links: 1   Clusters: 1976
Fragment:  Address: 0    Number: 0    Size: 0
ctime: 0x40075b9d -- Thu Jan 15 22:33:49 2004
atime: 0x40075b9d -- Thu Jan 15 22:33:49 2004
mtime: 0x40075b9d -- Thu Jan 15 22:33:49 2004
BLOCKS:
(0-11):132071-132082, (IND):132083, (12-245):132084-132317
TOTAL: 247
*/

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
		g_string_append (flags, "localbitmap ");
	if (in->i_flags & OCFS2_BITMAP_FL)
		g_string_append (flags, "globalbitmap ");
	if (in->i_flags & OCFS2_JOURNAL_FL)
		g_string_append (flags, "journal ");
	if (in->i_flags & OCFS2_DLM_FL)
		g_string_append (flags, "dlm ");

	printf("Inode: %llu   Type: %s   Mode: 0%0u   Flags: %s   Generation: %u\n",
	       in->i_blkno, str, mode, flags->str, in->i_generation);

	pw = getpwuid(in->i_uid);
	gr = getgrgid(in->i_gid);
	printf("User: %d (%s)   Group: %d (%s)   Size: %llu\n",
	       in->i_uid, (pw ? pw->pw_name : "unknown"),
	       in->i_gid, (gr ? gr->gr_name : "unknown"),
	       in->i_size);

	printf("Links: %u   Clusters: %u\n", in->i_links_count, in->i_clusters);

	dl = &(in->i_disk_lock);
	printf("Lock Master: %u   Level: 0x%0x   Seqnum: %llu\n",
	       dl->dl_master, dl->dl_level, dl->dl_seq_num);
	printf("Lock Node Map:");
	for (i = 0; i < 8; ++i)
		printf(" 0x%08x", dl->dl_node_map[i]);
	printf("\n");

	str = ctime((time_t*)&in->i_ctime);
	printf("ctime: 0x%llx -- %s", in->i_ctime, str);
	str = ctime((time_t*)&in->i_atime);
	printf("atime: 0x%llx -- %s", in->i_atime, str);
	str = ctime((time_t*)&in->i_mtime);
	printf("mtime: 0x%llx -- %s", in->i_mtime, str);
	str = ctime((time_t*)&in->i_dtime);
	printf("dtime: 0x%llx -- %s", in->i_dtime, str);

	printf("Last Extblk: %llu\n", in->i_last_eb_blk);
	printf("Sub Alloc Node: %u   Sub Alloc Blknum: %llu\n",
	       in->i_suballoc_node, in->i_suballoc_blkno); /* ?? */

	if (flags)
		g_string_free (flags, 1);
	return ;
}				/* dump_inode */

/*
 * dump_extent_list()
 *
 */
void dump_extent_list (ocfs2_extent_list *ext)
{
	ocfs2_extent_rec *rec;
	int i;

	printf("Tree Depth: %d   Count: %u   Next Free Rec: %u\n",
	       ext->l_tree_depth, ext->l_count, ext->l_next_free_rec);

	if (!ext->l_next_free_rec)
		goto bail;

	printf("## File Offset   Num Clusters   Disk Offset\n");

	for (i = 0; i < ext->l_next_free_rec; ++i) {
		rec = &(ext->l_recs[i]);
		printf("%-2d %-11u   %-12u   %llu\n", i, rec->e_cpos,
		       rec->e_clusters, rec->e_blkno);
	}

bail:
	return ;
}				/* dump_extent_list */

/*
 * dump_extent_block()
 *
 */
void dump_extent_block (ocfs2_extent_block *blk)
{
	printf ("SubAlloc Blknum: %llu   SubAlloc Node: %u\n",
	       	blk->h_suballoc_blkno, blk->h_suballoc_node);

	printf ("Blknum: %llu   Parent: %llu   Next Leaf: %llu\n",
		blk->h_blkno, blk->h_parent_blk, blk->h_next_leaf_blk);

	return ;
}				/* dump_extent_block */

/*
 * dump_dir_entry()
 *
 */
void dump_dir_entry (struct ocfs2_dir_entry *dir)
{
	char *p;
	struct ocfs2_dir_entry *rec;

	p = (char *)dir;

	printf("%-15s  %-6s  %-7s  %-4s  %-4s\n",
	       "Inode", "Reclen", "Namelen", "Type", "Name");

	while (1) {
		rec = (struct ocfs2_dir_entry *)p;
		if (!rec->inode)
			break;
		printf("%-15llu  %-6u  %-7u  %-4u  %*s\n", rec->inode,
		       rec->rec_len, rec->name_len, rec->file_type,
		       rec->name_len, rec->name);
		p += rec->rec_len;
	}

	return ;
}				/* dump_dir_entry */
