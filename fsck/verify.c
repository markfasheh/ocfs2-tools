/*
 * verify.c
 *
 * verification checks for ocfs file system check utility
 *
 * Copyright (C) 2003 Oracle.  All rights reserved.
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
 * Authors: Kurt Hackel, Sunil Mushran
 */

#include "fsck.h"


extern ocfsck_context ctxt;

/*
 * test_member_range()
 *
 */
int test_member_range(ocfs_class *cl, const char *name, char *buf)
{
	ocfs_class_member *mbr;
	int i, ret = -1;

	mbr = find_class_member(cl, name, &i);
	if (mbr->valid(buf, &(mbr->type)) != 0)
		ret = i;
	return ret;
}				/* test_member_range */

/*
 * check_outside_bounds()
 *
 */
int check_outside_bounds(char *buf, int structsize)
{
	int i;
	/* check for oddities */
	for (i=structsize; i<512; i++)
		if (buf[i] != 0)
			return -1;
	return 0;
}				/* check_outside_bounds */

/*
 * verify_nodecfghdr()
 *	 ocfs_disk_lock disk_lock;
 *	 char signature[NODE_CONFIG_SIGN_LEN];
 *	 __u32 version;
 *	 __u32 num_nodes;
 *	 __u32 last_node;
 *	 __u64 cfg_seq_num;
 */
int verify_nodecfghdr (int fd, char *buf, int idx, GHashTable **bad)
{
	int ret = 0;
	ocfs_node_config_hdr *hdr;
	ocfs_layout_t *lay;
	ocfs_class *cl;
	ocfs_class_member *mbr;
	int i;

	hdr = (ocfs_node_config_hdr *)buf;
	lay = find_nxt_hdr_struct(node_cfg_hdr, 0);
	cl = lay->kind->cls;
	*bad = g_hash_table_new(g_direct_hash, g_direct_equal);

	if (check_outside_bounds(buf, sizeof(ocfs_node_config_hdr)) == -1)
		LOG_WARNING("nonzero bytes after the disk structure");

	ret = verify_disk_lock (fd, buf, idx, bad);

	if (hdr->version != NODE_CONFIG_VER) {
		mbr = find_class_member(cl, "version", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}

	if (hdr->num_nodes > OCFS_MAXIMUM_NODES) {
		mbr = find_class_member(cl, "num_nodes", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}

	if (g_hash_table_size(*bad) == 0)
		ret = 0;

	return ret;
}				/* verify_nodecfghdr */

/*
 * verify_nodecfginfo()
 *	ocfs_disk_lock disk_lock;
 *	char node_name[MAX_NODE_NAME_LENGTH + 1];
 *	ocfs_guid guid;
 *	ocfs_ipc_config_info ipc_config;
 */
int verify_nodecfginfo (int fd, char *buf, int idx, GHashTable **bad)
{
	int ret = 0;
	ocfs_disk_node_config_info *node;
	ocfs_layout_t *lay;
	ocfs_class *cl;

	node = (ocfs_disk_node_config_info *)buf;
	lay = find_nxt_hdr_struct(node_cfg_info, 0);
	cl = lay->kind->cls;
	*bad = g_hash_table_new(g_direct_hash, g_direct_equal);

	if (check_outside_bounds(buf, sizeof(ocfs_disk_node_config_info)) == -1)
		LOG_WARNING("nonzero bytes after the disk structure");

	ret = verify_disk_lock (fd, buf, idx, bad);

	if (g_hash_table_size(*bad) == 0)
		ret = 0;

	return ret;
}				/* verify_nodecfginfo */

/*
 * verify_system_file_entry()
 *
 */
int verify_system_file_entry (int fd, char *buf, int idx, GHashTable **bad, char *fname, int type)
{
	int ret, i;
	ocfs_file_entry *fe;
	ocfs_class_member *mbr;
	ocfs_class *cl;

	cl = &ocfs_file_entry_class;
	fe = (ocfs_file_entry *)buf;

	ret = verify_file_entry (fd, buf, idx, bad);

	if (strncmp(fe->filename, fname, strlen(fname)) != 0)
	{
		mbr = find_class_member(cl, "filename", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
		ret = -1;
	}

	check_file_entry(fd, fe, fe->this_sector, true, "$");

	if (fe->disk_lock.curr_master != ((type+idx) % OCFS_MAXIMUM_NODES) &&
	    fe->disk_lock.curr_master != OCFS_INVALID_NODE_NUM &&
	    fe->disk_lock.file_lock != OCFS_DLM_NO_LOCK) {
		LOG_ERROR("bug 3038188 found! system file locked "
			  "by another node: file=%s type=%d idx=%d node=%d",
			  fname, type, idx, fe->disk_lock.curr_master);
		LOG_ERROR("solution: unmount on all nodes except %d, then "
			  "touch a file in any directory on node %d",
			  ((type+idx) % OCFS_MAXIMUM_NODES), ((type+idx) % OCFS_MAXIMUM_NODES));
	}

 	return ret;
}				/* verify_system_file_entry */

#include <unistd.h>
#include <linux/unistd.h>


/*
 * load_sysfile_bitmap()
 *
 */
static int load_sysfile_bitmap (int fd, char *buf, int idx, bool dirbm)
{
	int ret = -1;
	ocfs_file_entry *fe;
	loff_t prev;
	char *ptr;
	int fileid;

	fe = (ocfs_file_entry *)buf;

	if (fe->file_size <= 0) {
		ret = 0;
		goto bail;
	}
	
	if (dirbm) {
		if ((ctxt.dir_bm[idx] = malloc_aligned(fe->alloc_size)) == NULL) {
			LOG_INTERNAL();
			goto bail;
		}
		memset(ctxt.dir_bm[idx], 0, fe->alloc_size);
		ctxt.dir_bm_sz[idx] = fe->file_size;
		ptr = ctxt.dir_bm[idx];
		fileid = idx + OCFS_FILE_DIR_ALLOC_BITMAP;
	} else {
		if ((ctxt.ext_bm[idx] = malloc_aligned(fe->alloc_size)) == NULL) {
			LOG_INTERNAL();
			goto bail;
		}
		memset(ctxt.ext_bm[idx], 0, fe->alloc_size);
		ctxt.ext_bm_sz[idx] = fe->file_size;
		ptr = ctxt.ext_bm[idx];
		fileid = idx + OCFS_FILE_FILE_ALLOC_BITMAP;
	}

	if ((prev=myseek64(fd, 0ULL, SEEK_CUR)) == -1) {
		LOG_INTERNAL();
		goto bail;
	}

	ret = ocfs_read_system_file(ctxt.vcb, fileid, ptr, fe->alloc_size,
				    (__u64)0);
#if 0
	if (ret >= 0) {
		ocfs_alloc_bm bm;
		int freebits, firstclear;

		LOG_VERBOSE("succeeded\n");
		ocfs_initialize_bitmap(&bm, ptr, (__u32) (fe->file_size * 8));
		freebits = ocfs_count_bits(&bm);
		firstclear = ocfs_find_clear_bits(&bm, 1, 0, 0);
		LOG_VERBOSE("freebits=%d firstclear=%d numbits=%llu\n",
			    freebits, firstclear, fe->file_size * 8);
	} else {
		LOG_VERBOSE("failed\n");
	}
#endif

	if ((prev=myseek64(fd, prev, SEEK_SET)) == -1) {
		LOG_INTERNAL();
		goto bail;
	}

      bail:
	return ret;
}				/* load_sysfile_bitmap */

/*
 * verify_dir_alloc_bitmap()
 *
 */
int verify_dir_alloc_bitmap (int fd, char *buf, int idx, GHashTable **bad)
{
	char fname[30];
	sprintf(fname, "%s%d", "DirBitMapFile", idx+OCFS_FILE_DIR_ALLOC_BITMAP);
	load_sysfile_bitmap(fd, buf, idx, true);
	return verify_system_file_entry (fd, buf, idx, bad, fname, OCFS_FILE_DIR_ALLOC_BITMAP);
}				/* verify_dir_alloc_bitmap */

/*
 * verify_file_alloc_bitmap()
 *
 */
int verify_file_alloc_bitmap (int fd, char *buf, int idx, GHashTable **bad)
{
	char fname[30];
	sprintf(fname, "%s%d", "ExtentBitMapFile", idx+OCFS_FILE_FILE_ALLOC_BITMAP);
	load_sysfile_bitmap(fd, buf, idx, false);
	return verify_system_file_entry (fd, buf, idx, bad, fname, OCFS_FILE_FILE_ALLOC_BITMAP);
}				/* verify_file_alloc_bitmap */

/*
 * verify_dir_alloc()
 *
 */
int verify_dir_alloc (int fd, char *buf, int idx, GHashTable **bad)
{
	char fname[30];
	sprintf(fname, "%s%d", "DirFile", idx+OCFS_FILE_DIR_ALLOC);
	return verify_system_file_entry (fd, buf, idx, bad, fname, OCFS_FILE_DIR_ALLOC);
}				/* verify_dir_alloc */

/*
 * verify_file_alloc()
 *
 */
int verify_file_alloc (int fd, char *buf, int idx, GHashTable **bad)
{
	char fname[30];
	sprintf(fname, "%s%d", "ExtentFile", idx+OCFS_FILE_FILE_ALLOC);
	return verify_system_file_entry (fd, buf, idx, bad, fname, OCFS_FILE_FILE_ALLOC);
}				/* verify_file_alloc */

/*
 * verify_vol_metadata()
 *
 */
int verify_vol_metadata (int fd, char *buf, int idx, GHashTable **bad)
{
	char fname[30];
	sprintf(fname, "%s", "VolMetaDataFile");  // no file #
	return verify_system_file_entry (fd, buf, idx, bad, fname, OCFS_FILE_VOL_META_DATA);
}				/* verify_vol_metadata */

/*
 * verify_vol_metadata_log()
 *
 */
int verify_vol_metadata_log (int fd, char *buf, int idx, GHashTable **bad)
{
	char fname[30];
	sprintf(fname, "%s", "VolMetaDataLogFile");  // no file #
	return verify_system_file_entry (fd, buf, idx, bad, fname, OCFS_FILE_VOL_LOG_FILE);
}				/* verify_vol_metadata_log */

/*
 * verify_cleanup_log()
 *
 */
int verify_cleanup_log (int fd, char *buf, int idx, GHashTable **bad)
{
	char fname[30];
	sprintf(fname, "%s%d", "CleanUpLogFile", idx+CLEANUP_FILE_BASE_ID);
	return verify_system_file_entry (fd, buf, idx, bad, fname, CLEANUP_FILE_BASE_ID);
}				/* verify_cleanup_log */

/*
 * verify_recover_log()
 *
 */
int verify_recover_log (int fd, char *buf, int idx, GHashTable **bad)
{
	char fname[30];
	sprintf(fname, "%s%d", "RecoverLogFile", idx+LOG_FILE_BASE_ID);
	return verify_system_file_entry (fd, buf, idx, bad, fname, LOG_FILE_BASE_ID);
}				/* verify_recover_log */

/*
 * verify_volume_bitmap()
 *
 */
int verify_volume_bitmap (int fd, char *buf, int idx, GHashTable **bad)
{
	return 0;
}				/* verify_volume_bitmap */

/*
 * verify_publish_sector()
 *
 */
int verify_publish_sector (int fd, char *buf, int idx, GHashTable **bad)
{
	int i;
	ocfs_publish *pub;
	ocfs_class *cl;

	pub = (ocfs_publish *)buf;
	cl = &ocfs_publish_class;
	*bad = g_hash_table_new(g_direct_hash, g_direct_equal);

	if ((i = test_member_range(cl, "time", buf)) != -1)
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	if ((i = test_member_range(cl, "vote", buf)) != -1)
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	if ((i = test_member_range(cl, "dirty", buf)) != -1)
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	if ((i = test_member_range(cl, "vote_type", buf)) != -1)
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	if ((i = test_member_range(cl, "vote_map", buf)) != -1)
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	if ((i = test_member_range(cl, "publ_seq_num", buf)) != -1)
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	if ((i = test_member_range(cl, "dir_ent", buf)) != -1)
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	if ((i = test_member_range(cl, "comm_seq_num", buf)) != -1)
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));

	if (g_hash_table_size(*bad) == 0)
		return 0;
	return -1;
}				/* verify_publish_sector */

/*
 * verify_vote_sector()
 *
 */
int verify_vote_sector (int fd, char *buf, int idx, GHashTable **bad)
{
	int i;
	ocfs_vote *vote;
	ocfs_class *cl;

	vote = (ocfs_vote *)buf;
	cl = &ocfs_vote_class;
	*bad = g_hash_table_new(g_direct_hash, g_direct_equal);

	if ((i = test_member_range(cl, "vote", buf)) != -1)
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	if ((i = test_member_range(cl, "vote_seq_num", buf)) != -1)
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	if ((i = test_member_range(cl, "dir_ent", buf)) != -1)
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	if ((i = test_member_range(cl, "open_handle", buf)) != -1)
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));

	if (g_hash_table_size(*bad) == 0)
		return 0;
	return -1;
}				/* verify_vote_sector */

/*
 * verify_dir_node()
 *
 */
int verify_dir_node (int fd, char *buf, int idx, GHashTable **bad)
{
	int ret = 0;
//	ocfs_dir_node *dir;
	ocfs_class *cl;
//	int i;
//	ocfs_class_member *mbr;
//	int j;
//	__u64 size = 0;
//	__u64 off = 0;
//	char tmpstr[255];

	cl = &ocfs_dir_node_class;
	*bad = g_hash_table_new(g_direct_hash, g_direct_equal);

	if (check_outside_bounds(buf, sizeof(ocfs_dir_node)) == -1)
		LOG_WARNING("nonzero bytes after the disk structure");

	/* ocfs_disk_lock disk_lock; */ 
	ret = verify_disk_lock (fd, buf, idx, bad);

	/* __u64 alloc_file_off;     */ 
	/* __u32 alloc_node;         */ 
	/* __u64 free_node_ptr;      */ 
	/* __u64 node_disk_off;      */ 
	/* __s64 next_node_ptr;      */ 
	/* __s64 indx_node_ptr;      */ 
	/* __s64 next_del_ent_node;  */ 
	/* __s64 head_del_ent_node;  */ 
	/* __u8 first_del;           */
	/* __u8 num_del;             */
	/* __u8 num_ents;	     */
	/* __u8 depth;		     */
	/* __u8 num_ent_used;	     */
	/* __u8 dir_node_flags;	     */
	/* __u8 sync_flags;          */
	/* __u8 index[256];          */
	/* __u8 index_dirty;         */
	/* __u8 bad_off;             */
	/* __u64 num_tot_files;      */
	/* __u8 reserved[119];       */
	/* __u8 file_ent[1];         */

	if (g_hash_table_size(*bad) == 0)
		ret = 0;

	return 0;
}				/* verify_dir_node */

/*
 * verify_file_entry()
 *
 */
int verify_file_entry (int fd, char *buf, int idx, GHashTable **bad)
{
	int ret = 0;
	ocfs_file_entry *fe;
	ocfs_class *cl;
	ocfs_class_member *mbr;
	int i;
	int j;
	__u64 size = 0;
	__u64 off = 0;
	char tmpstr[255];

	fe = (ocfs_file_entry *)buf;
	cl = &ocfs_file_entry_class;
	*bad = g_hash_table_new(g_direct_hash, g_direct_equal);

	if (check_outside_bounds(buf, sizeof(ocfs_file_entry)) == -1)
		LOG_WARNING("nonzero bytes after the disk structure");

	/* ocfs_disk_lock disk_lock; */
	ret = verify_disk_lock (fd, buf, idx, bad);

	/* bool local_ext;     */
	/* __s32 granularity;  */
	if ((fe->local_ext && fe->granularity != -1) ||
	    (!fe->local_ext && fe->granularity < 0)) {
		mbr = find_class_member(cl, "local_ext", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
		mbr = find_class_member(cl, "granularity", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}

	/* __u8 next_free_ext; */
	if (fe->next_free_ext > OCFS_MAX_FILE_ENTRY_EXTENTS) {
		mbr = find_class_member(cl, "next_free_ext", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}

	/* __s8 next_del; */
	/* __u8 filename[OCFS_MAX_FILENAME_LENGTH]; */
	/* __u16 filename_len; */
	
	for (j = 0; j < OCFS_MAX_FILE_ENTRY_EXTENTS; j++)
		size += fe->extents[j].num_bytes;

	/* __u64 file_size;    */
	if (!(fe->attribs & OCFS_ATTRIB_DIRECTORY) && fe->file_size > size) {
		mbr = find_class_member(cl, "file_size", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}
	
	/* __u64 alloc_size;   */
	if (!(fe->attribs & OCFS_ATTRIB_DIRECTORY) && fe->alloc_size != size) {
		mbr = find_class_member(cl, "alloc_size", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}

	/* __u64 create_time;  */
	/* __u64 modify_time;  */

	/* ocfs_alloc_ext extents[OCFS_MAX_FILE_ENTRY_EXTENTS]; */
	for (j = 0, off = 0; j < OCFS_MAX_FILE_ENTRY_EXTENTS; ++j) {
		if (!fe->extents[j].num_bytes)
			continue;

		if (fe->extents[j].file_off != off) {
			sprintf(tmpstr, "extents[%d].file_off", j);
			mbr = find_class_member(cl, tmpstr, &i);
			g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
		}

		off += fe->extents[j].num_bytes;

		if (fe->extents[j].num_bytes && !fe->extents[j].disk_off) {
			sprintf(tmpstr, "extents[%d].num_bytes", j);
			mbr = find_class_member(cl, tmpstr, &i);
			g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
		}
	}

	/* __u64 dir_node_ptr; */

	/* __u64 this_sector;  */
	if (!fe->this_sector) {
		mbr = find_class_member(cl, "this_sector", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}

	/* __u64 last_ext_ptr; */
	if (!fe->local_ext && !fe->last_ext_ptr) {
		mbr = find_class_member(cl, "last_ext_ptr", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}

	/* __u32 sync_flags;  */
	/* __u32 link_cnt;  */
	/* __u32 attribs;   */
	/* __u32 prot_bits; */
	/* __u32 uid;       */
	/* __u32 gid;       */
	/* __u16 dev_major; */
	/* __u16 dev_minor; */

	if (g_hash_table_size(*bad) == 0)
		ret = 0;

	return 0;
}				/* verify_file_entry */

/*
 * verify_extent_group()
 *
 */
int verify_extent_group (int fd, char *buf, int idx, GHashTable **bad, int type, __u64 up_ptr)
{
	ocfs_extent_group *ext;
	ocfs_class *cl;
	ocfs_class_member *mbr;
	int i, j;
	char mname[255];
	__u64 len;

	ext = (ocfs_extent_group *)buf;
	cl = &ocfs_extent_group_class;
	*bad = g_hash_table_new(g_direct_hash, g_direct_equal);

	if (ext->type != type) {
		mbr = find_class_member(cl, "type", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}

	if ((i = test_member_range(cl, "next_free_ext", buf)) != -1)
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));

	if ((i = test_member_range(cl, "curr_sect", buf)) != -1)
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));

	if ((i = test_member_range(cl, "max_sects", buf)) != -1)
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));

	if ((i = test_member_range(cl, "alloc_node", buf)) != -1)
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));

	if ((i = test_member_range(cl, "this_ext", buf)) != -1)
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));

	if ((i = test_member_range(cl, "next_data_ext", buf)) != -1)
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));

	if ((i = test_member_range(cl, "alloc_file_off", buf)) != -1)
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));

	if ((i = test_member_range(cl, "last_ext_ptr", buf)) != -1)
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));

	if ((i = test_member_range(cl, "granularity", buf)) != -1)
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));

	// make sure the up_ptr isn't b0rk3n
	if (up_ptr != 0ULL && ext->up_hdr_node_ptr != up_ptr) {
		i = test_member_range(cl, "up_hdr_node_ptr", buf);
		if (i != -1)
			g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}

	len = ext->extents[0].file_off;
	for (j = 0; j < OCFS_MAX_DATA_EXTENTS; j++) {
		if (!ext->extents[j].num_bytes)
			continue;

		if (ext->extents[j].file_off != len) {
			sprintf(mname, "extents[%d].file_off", j);
			mbr = find_class_member(cl, mname, &i);
			g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
		}

		len += ext->extents[j].num_bytes;

		if (ext->extents[j].num_bytes && !ext->extents[j].disk_off) {
			sprintf(mname, "extents[%d].num_bytes", i);
			mbr = find_class_member(cl, mname, &i);
			g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
		}
	}

	if (g_hash_table_size(*bad) == 0)
		return 0;

	return -1;
}				/* verify_extent_group */

/*
 * verify_extent_header()
 *
 */
int verify_extent_header (int fd, char *buf, int idx, GHashTable **bad)
{
	return verify_extent_group(fd, buf, idx, bad, OCFS_EXTENT_HEADER, 0ULL);
}				/* verify_extent_header */

/*
 * verify_extent_data()
 *
 */
int verify_extent_data (int fd, char *buf, int idx, GHashTable **bad)
{
	return verify_extent_group(fd, buf, idx, bad, OCFS_EXTENT_DATA, 0ULL);
}				/* verify_extent_data */



// TODO: FIXME error handling
/*
 * load_volume_bitmap()
 *
 */
int load_volume_bitmap(void)
{
	loff_t old;
	int ret = -1;

	/* assumes the hdr has already been verified */
	if (ctxt.hdr->bitmap_off == 0) {
		LOG_INTERNAL();
		goto bail;
	}

	if ((old = myseek64(ctxt.fd, 0, SEEK_CUR)) == -1) {
		LOG_INTERNAL();
		goto bail;
	}

	if (myseek64(ctxt.fd, ctxt.hdr->bitmap_off, SEEK_SET) == -1) {
		LOG_INTERNAL();
		goto bail;
	}

//	if (myread(ctxt.fd, ctxt.vol_bm, (ctxt.hdr->num_clusters + 7 / 8)) == -1) {
	if (myread(ctxt.fd, ctxt.vol_bm, VOL_BITMAP_BYTES) == -1) {
		LOG_INTERNAL();
		goto bail;
	}

	if (myseek64(ctxt.fd, old, SEEK_SET) == -1) {
		LOG_INTERNAL();
		goto bail;
	}

	ret = 0;

      bail:
	return ret;
}				/* load_volume_bitmap */

/*
 * verify_vol_disk_header()
 *
 */
int verify_vol_disk_header(int fd, char *buf, int idx, GHashTable **bad)
{
	int len, ret = -1;
	__u64 j;
	ocfs_dir_node *dir = NULL;
	ocfs_vol_disk_hdr *hdr;
	ocfs_layout_t *lay;
	ocfs_class *cl;
	ocfs_class_member *mbr;
	int i;

	dir = (ocfs_dir_node *) malloc_aligned(512);
	hdr = (ocfs_vol_disk_hdr *)buf;
	lay = find_nxt_hdr_struct(vol_disk_header, 0);
	cl = lay->kind->cls;
	*bad = g_hash_table_new(g_direct_hash, g_direct_equal);

	if (check_outside_bounds(buf, sizeof(ocfs_vol_disk_hdr)) == -1) {
		LOG_WARNING("nonzero bytes after the disk header structure");
	}

	if (hdr->minor_version != OCFS_MINOR_VERSION)
	{
		mbr = find_class_member(cl, "minor_version", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}
	if (hdr->major_version != OCFS_MAJOR_VERSION)
	{
		mbr = find_class_member(cl, "major_version", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}
	if (strncmp(hdr->signature, OCFS_VOLUME_SIGNATURE, MAX_VOL_SIGNATURE_LEN) != 0)
	{
		mbr = find_class_member(cl, "signature", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}
	len = strnlen(hdr->mount_point, MAX_MOUNT_POINT_LEN);
	if (len == MAX_MOUNT_POINT_LEN || len < OCFSCK_MIN_MOUNT_POINT_LEN)
	{
		mbr = find_class_member(cl, "mount_point", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}
	if (hdr->bitmap_off != OCFSCK_BITMAP_OFF)
	{
		mbr = find_class_member(cl, "bitmap_off", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}
	if (hdr->publ_off != OCFSCK_PUBLISH_OFF)
	{
		mbr = find_class_member(cl, "publ_off", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}
	if (hdr->vote_off != OCFSCK_VOTE_OFF)
	{
		mbr = find_class_member(cl, "vote_off", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}
	if (hdr->node_cfg_off != OCFSCK_AUTOCONF_OFF)
	{
		mbr = find_class_member(cl, "node_cfg_off", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}
	if (hdr->node_cfg_size !=  OCFSCK_AUTOCONF_SIZE)
	{
		mbr = find_class_member(cl, "node_cfg_size", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}
	if (hdr->new_cfg_off != OCFSCK_NEW_CFG_OFF)
	{
		mbr = find_class_member(cl, "new_cfg_off", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}
	if (hdr->data_start_off != OCFSCK_DATA_START_OFF)
	{
		mbr = find_class_member(cl, "data_start_off", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}
	if (hdr->internal_off != OCFSCK_INTERNAL_OFF)
	{
		mbr = find_class_member(cl, "internal_off", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}
	if (hdr->num_nodes != (__u64)OCFS_MAXIMUM_NODES)
	{
		mbr = find_class_member(cl, "num_nodes", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}
	if (hdr->serial_num != 0ULL)
	{
		mbr = find_class_member(cl, "serial_num", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}
	if (hdr->start_off != 0ULL)
	{
		mbr = find_class_member(cl, "start_off", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}
	if (hdr->root_bitmap_off != 0ULL)
	{
		mbr = find_class_member(cl, "root_bitmap_off", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}
	if (hdr->root_bitmap_size != 0ULL)
	{
		mbr = find_class_member(cl, "root_bitmap_size", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}
	if (hdr->root_size != 0ULL)
	{
		mbr = find_class_member(cl, "root_size", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}
	if (hdr->dir_node_size != 0ULL)
	{
		mbr = find_class_member(cl, "dir_node_size", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}
	if (hdr->file_node_size != 0ULL)
	{
		mbr = find_class_member(cl, "file_node_size", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}
	if ((i = test_member_range(cl, "excl_mount", buf)) != -1)
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	if ((i = test_member_range(cl, "uid", buf)) != -1)
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	if ((i = test_member_range(cl, "gid", buf)) != -1)
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	if ((i = test_member_range(cl, "prot_bits", buf)) != -1)
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));

	if (hdr->device_size > ctxt.device_size)
	{
		mbr = find_class_member(cl, "device_size", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}
	
	ctxt.cluster_size_bits = 11; // 2048
	for (j = OCFSCK_LO_CLUSTER_SIZE; j <= OCFSCK_HI_CLUSTER_SIZE; j *= 2)
	{
		ctxt.cluster_size_bits++;
		if (hdr->cluster_size == j)
		{
			if (j * hdr->num_clusters > ctxt.device_size - OCFSCK_NON_DATA_AREA ||
			    hdr->num_clusters < 1 || hdr->num_clusters > OCFSCK_MAX_CLUSTERS)
			{
				mbr = find_class_member(cl, "num_clusters", &i);
				g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
			}
			break;
		}
	}
	if (j > OCFSCK_HI_CLUSTER_SIZE)
	{
		mbr = find_class_member(cl, "cluster_size", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}

	/* the root off is *always* at OCFSCK_ROOT_OFF */
	/* and we can no longer do the signature check because */
	/* it may be bad and we want to be able to change it later */
	if (hdr->root_off != OCFSCK_ROOT_OFF)
	{
		mbr = find_class_member(cl, "root_off", &i);
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	}


	if (g_hash_table_size(*bad) == 0)
	{
		memcpy(ctxt.hdr, buf, OCFS_SECTOR_SIZE);
		ctxt.vcb = get_fake_vcb(ctxt.fd, ctxt.hdr, /* node# */ 0);
		if ((ret = load_volume_bitmap()) != 0)
			LOG_ERROR("failed to read volume bitmap");
	}
	if (dir)
		free_aligned(dir);
	return ret;
}				/* verify_vol_disk_header */

/*
 * verify_vol_label()
 *
 */
int verify_vol_label (int fd, char *buf, int idx, GHashTable **bad)
{
	ocfs_vol_label *lbl;

	lbl = (ocfs_vol_label *)buf;

	if (check_outside_bounds(buf, sizeof(ocfs_vol_label)) == -1) {
		LOG_WARNING("nonzero bytes after the volume label structure");
	}

	return verify_disk_lock (fd, buf, idx, bad);
}				/* verify_vol_label */

/*
 * verify_disk_lock()
 *
 */
int verify_disk_lock (int fd, char *buf, int idx, GHashTable **bad)
{
	ocfs_disk_lock *lock;
	ocfs_class *cl;
	int i;

	lock = (ocfs_disk_lock *)buf;
	cl = &ocfs_disk_lock_class;
	*bad = g_hash_table_new(g_direct_hash, g_direct_equal);

	if ((i = test_member_range(cl, "curr_master", buf)) != -1)
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	if ((i = test_member_range(cl, "writer_node_num", buf)) != -1)
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	if ((i = test_member_range(cl, "reader_node_num", buf)) != -1)
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	if ((i = test_member_range(cl, "oin_node_map", buf)) != -1)
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));
	if ((i = test_member_range(cl, "file_lock", buf)) != -1)
		g_hash_table_insert(*bad, GINT_TO_POINTER(i), GINT_TO_POINTER(1));

	if (g_hash_table_size(*bad) == 0)
		return 0;
	return -1;
}				/* verify_disk_lock */
