
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

	printf("\trevision = %u.%u\n", sb->s_major_rev_level, sb->s_minor_rev_level);
	printf("\tmount count = %u\n", sb->s_mnt_count);
	printf("\tmax mount cnt = %u\n", sb->s_max_mnt_count);
	printf("\tstate = %u\n", sb->s_state);
	printf("\terrors = %u\n", sb->s_errors);
	printf("\tcheck interval = %u\n", sb->s_checkinterval);
	printf("\tlast check = %llu\n", sb->s_lastcheck);
	printf("\tcreator os = %u\n", sb->s_creator_os);
	printf("\tfeature compat = %u\n", sb->s_feature_compat);
	printf("\tfeature incompat = %u\n", sb->s_feature_incompat);
	printf("\tfeature ro compat = %u\n", sb->s_feature_ro_compat);
	printf("\troot blknum = %llu\n", sb->s_root_blkno);
	printf("\tsys dir blknum = %llu\n", sb->s_system_dir_blkno);
	printf("\tblksize bits = %u\n", sb->s_blocksize_bits);
	printf("\tclustersize bits = %u\n", sb->s_clustersize_bits);
	printf("\tmax nodes = %u\n", sb->s_max_nodes);
	printf("\tlabel = %s\n", sb->s_label);
	printf("\tuuid = ");
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
	char *s;

	printf("\tsignature = %s\n", in->i_signature);
	printf("\tgeneration = %u\n", in->i_generation);
	printf("\tsuballoc node = %u\n", in->i_suballoc_node); /* ?? */
	printf("\tsuballoc blkno = %llu\n", in->i_suballoc_blkno);
	pw = getpwuid(in->i_uid);
	printf("\tuid = %u (%s)\n", in->i_uid, (pw ? pw->pw_name : "unknown"));
	gr = getgrgid(in->i_gid);
	printf("\tgid = %u (%s)\n", in->i_gid, (gr ? gr->gr_name : "unknown"));
	printf("\tsize = %llu\n", in->i_size);
	printf("\tmode = 0%0u\n", in->i_mode);
	printf("\tlinks cnt = %u\n", in->i_links_count);
	printf("\tflags = %u\n", in->i_flags);

	s = ctime((time_t*)&in->i_atime);
	printf("\tatime = %s", s);
	s = ctime((time_t*)&in->i_ctime);
	printf("\tctime = %s", s);
	s = ctime((time_t*)&in->i_mtime);
	printf("\tmtime = %s", s);
	s = ctime((time_t*)&in->i_dtime);

	printf("\tdtime = %s", s);
	printf("\tblock num = %llu\n", in->i_blkno);
	printf("\tclusters = %u\n", in->i_clusters);
	printf("\tlast extblk = %llu\n", in->i_last_eb_blk);

	return ;
}				/* dump_inode */
