
#include <main.h>
#include <commands.h>
#include <dump.h>
#include <readfs.h>

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

/*
Inode: 32001   Type: directory    Mode:  0755   Flags: 0x0   Generation: 721849
User:     0   Group:     0   Size: 4096
File ACL: 0    Directory ACL: 0
Links: 10   Blockcount: 8
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
Links: 1   Blockcount: 1976
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

	printf("Inode: %llu   Type: %s   Mode: 0%0u   Flags: 0x%x   Generation: %u\n",
	       in->i_blkno, str, mode, in->i_flags, in->i_generation);

	pw = getpwuid(in->i_uid);
	gr = getgrgid(in->i_gid);
	printf("User: %d (%s)   Group: %d (%s)   Size: %llu\n",
	       in->i_uid, (pw ? pw->pw_name : "unknown"),
	       in->i_gid, (gr ? gr->gr_name : "unknown"),
	       in->i_size);

	printf("Links: %u   Blockcount: %u\n", in->i_links_count, in->i_clusters);

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

	return ;
}				/* dump_inode */
