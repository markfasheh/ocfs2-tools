/*
 * utils.c
 *
 * ocfs file system check utility
 *
 * Copyright (C) 2003, 2004 Oracle.  All rights reserved.
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
extern char *usage_str; 

extern int prn_len;
extern int cnt_err;
extern int cnt_wrn;
extern int cnt_obj;
extern bool int_err;
extern bool prn_err;

/*
 * usage()
 *
 */
void usage(void)
{
	printf("%s\n", usage_str);
}				/* usage */

void init_global_context(void)
{
	char *tmp;

	memset(&OcfsGlobalCtxt, 0, sizeof(ocfs_global_ctxt));
	OcfsGlobalCtxt.obj_id.type = OCFS_TYPE_GLOBAL_DATA;
	OcfsGlobalCtxt.obj_id.size = sizeof (ocfs_global_ctxt);
	OcfsGlobalCtxt.pref_node_num = 31;
	OcfsGlobalCtxt.node_name = "user-tool";
	OcfsGlobalCtxt.comm_info.type = OCFS_UDP;
	OcfsGlobalCtxt.comm_info.ip_addr = "0.0.0.0";
	OcfsGlobalCtxt.comm_info.ip_port = OCFS_IPC_DEFAULT_PORT;
	OcfsGlobalCtxt.comm_info.ip_mask = NULL;
	OcfsGlobalCtxt.comm_info_read = true;
	memset(&OcfsGlobalCtxt.guid.id.host_id, 'f', HOSTID_LEN);
	memset(&OcfsGlobalCtxt.guid.id.mac_id,  '0', MACID_LEN);

	tmp = getenv("debug_level");
	if (tmp)
		debug_level = atoi(tmp);
	tmp = getenv("debug_context");
	if (tmp)
		debug_context = atoi(tmp);
	tmp = getenv("debug_exclude");
	if (tmp)
		debug_exclude = atoi(tmp);
}


/*
 * confirm_changes()
 *
 */
int confirm_changes(__u64 off, ocfs_disk_structure *s, char *buf, int idx, GHashTable *bad)
{
	int ret = -1;
	char *yesno, *loc;
	int fd = ctxt.fd;

	yesno = malloc(USER_INPUT_MAX);
	if (!yesno) {
		LOG_INTERNAL();
		goto bail;
	}

	if (s->output(buf, idx, bad, stdout)==-1) {
		//fprintf(stderr, "at least one bad field found\n");
	}

	printf("\n\nDo you really want to write your changes out? : ");

	if (fgets(yesno, USER_INPUT_MAX, stdin) == NULL) {
		ret = -1;
		goto bail;
	}

	if ((loc = rindex(yesno, '\n')) != NULL)
		*loc = '\0';

       	if (strcasecmp(yesno, "yes")==0 || strcasecmp(yesno, "y")==0) {
		if (s->write(fd, buf, off, idx)==-1)
			LOG_ERROR("failed to write data to disk");
		else {
			GHashTable *tmp = NULL;
			ret = s->verify(fd, buf, idx, &tmp);
			if (tmp != NULL)
				g_hash_table_destroy(tmp);
		}
	}

bail:
	safefree(yesno);
	return ret;
}				/* confirm_changes */

static char *saved_block = NULL;
/*
 * read_print_struct()
 *
 */
int read_print_struct(ocfs_disk_structure *s, char *buf, __u64 off, int idx, GHashTable **bad)
{
	int ret = 0;
	int fd = ctxt.fd;
	
	if (saved_block == NULL)
		saved_block = malloc(512);

	if (saved_block == NULL)
		return -1;

	if (s->read(fd, buf, off, idx)==-1) {
		LOG_ERROR("failed to read data");
		return -2;
	}

	memcpy(saved_block, buf, 512);

	if (s->sig_match) {
		if (s->sig_match(buf, idx)==-EINVAL) {
			LOG_ERROR("Bad signature found");
			ret = -1;
			if (ctxt.write_changes) {
				if (!*bad)
					*bad = g_hash_table_new(g_direct_hash, g_direct_equal);
				ret = confirm_changes(off, s, buf, idx, *bad);
				LOG_PRINT("Fixed");
			} else
				LOG_PRINT("To fix, rerun with -w");
			/* restore the original if the new one was not written */
			if (ret == -1)
				memcpy(buf, saved_block, 512);
		}
	}

	if (s->verify(fd, buf, idx, bad)==-1) {
		LOG_ERROR("structure failed verification");
		ret = -1;
	}

	if (ret == -1 || (ret == 0 && ctxt.verbose)) {
		if (s->output(buf, idx, *bad, stdout)==-1) {
			//fprintf(stderr, "at least one bad field found\n");
			//ret = -1;
		}
	}

	return ret;
}				/* read_print_struct */


/*
 * get_device_size()
 *
 */
int get_device_size(int fd)
{
	int ret = -1;
	__u32 numblks;
	struct stat buf;

	if (fstat(fd, &buf) == -1) {
		printf("%s: %s\n", ctxt.device, strerror(errno));
		goto bail;
	}

	if (ctxt.dev_is_file) {		/* used during testing */
		ctxt.device_size = buf.st_size;
		goto finito;
	} else if (S_ISCHR(buf.st_mode)) {
		char *junk;
		__u64 hi, lo, new, delta, last;
		int ret;

		junk = malloc_aligned(512);
		hi = 0xfffffffffffffd00;
		lo = 0ULL;
		new = hi >> 1;

		ctxt.device_size = 0;
		do {
			last = new;
			myseek64(fd, new, SEEK_SET);
			ret = read(fd, junk, 512);
			if (ret == 512) {
				// go higher
				ctxt.device_size = (new + 512);
				lo = new;
				delta = (hi - lo) >> 1;
				new = hi - delta;
				new &= 0xfffffffffffffd00;
			} else {
				// go lower
				hi = new;
				delta = (hi - lo) >> 1;
				new = lo + delta;
				new &= 0xfffffffffffffd00;
			}
			
			if (last == new || hi <= lo)
				break;
		} while (1);
		while (ret == 512)
		{
			ctxt.device_size = (new + 512);
			myseek64(fd, new, SEEK_SET);
			ret = read(fd, junk, 512);
			new += 512;
		}
		goto finito;
	} else {
		if (ioctl(fd, BLKGETSIZE, &numblks) == -1) {
			printf("%s: %s\n", ctxt.device, strerror(errno));
			goto bail;
		} 
		ctxt.device_size = numblks;
		ctxt.device_size *= OCFS_SECTOR_SIZE;
		goto finito;
	}

finito:
	ret = 0;
bail:
	return ret;
}				/* get_device_size */


void handle_signal(int sig)
{
    switch (sig) {
    case SIGTERM:
    case SIGINT:
	myclose(ctxt.fd);
	unbind_raw(ctxt.raw_minor);
	exit(1);
    }
}


/*
 * check_heart_beat()
 *
 */
int check_heart_beat(int *file, __u64 publ_off, __u32 sect_size)
{
	char *publish = NULL;
	ocfs_super osb;
	int ret = 0;
	int i;
	int waittime;
	char *node_names[OCFS_MAXIMUM_NODES];
	__u32 nodemap;

	for (i = 0; i < OCFS_MAXIMUM_NODES; ++i)
		node_names[i] = NULL;

	memset (&osb, 0, sizeof(ocfs_super));

	if (!read_publish(*file, publ_off, sect_size, (void **)&publish)) {
		LOG_INTERNAL();
		goto bail;
	}

	/* alloc osb and pop sect_size */
	osb.sect_size = sect_size;

        /* call ocfs_update_publish_map(first_time = true) */
	ocfs_update_publish_map (&osb, (void *)publish, true);

	/* sleep(OCFS_NM_HEARTBEAT_TIME * 10) */
	printf("Checking heart beat on volume ");
	waittime = (OCFS_NM_HEARTBEAT_TIME/1000);
	waittime = (waittime ? waittime : 1);
	for (i = 0; i < OCFS_HBT_WAIT; ++i) {
		printf(".");
		fflush(stdout);
		sleep(waittime);
	}
   
	/* Close and re-open device to force disk read */
	myclose(*file);

	if ((*file = myopen(ctxt.raw_device, ctxt.flags)) == -1) {
		LOG_INTERNAL();
		goto bail;
	}

	memset (publish, 0, sect_size);
	if (!read_publish(*file, publ_off, sect_size, (void **)&publish)) {
		LOG_INTERNAL();
		goto bail;
	}

	/* call ocfs_update_publish_map(first_time = false) */
	ocfs_update_publish_map (&osb, (void *)publish, false);

	printf("\r                                                \r");
	fflush(stdout);

	/* OCFS currently supports upto 32 nodes */
	nodemap = LO(osb.publ_map);
	if (!nodemap)
		goto success;

	/* Get names of all the nodes */
	get_node_names(*file, ctxt.hdr, node_names, sect_size);

	/* Prints the ones which are mounted */
	printf("%s is mounted on nodes:", ctxt.device);
	print_node_names(node_names, nodemap);

	if (ctxt.write_changes) {
		ctxt.write_changes = false;
		printf("umount volume on node(s) before running fsck -w\n");
		printf("Continuing in read-only mode\n");
	}

	printf("As %s is mounted on one or more nodes, fsck.ocfs may "
	       "display false-positive errors\n", ctxt.device);

success:
	ret = 1;
bail:
	for (i = 0; i < OCFS_MAXIMUM_NODES; ++i)
		free(node_names[i]);

	free_aligned(publish);
	return ret;
}				/* check_heart_beat */

/*
 * read_publish()
 *
 */
int read_publish(int file, __u64 publ_off, __u32 sect_size, void **buf)
{
	int ret = 0;
	__u32 pub_len;

	pub_len = OCFS_MAXIMUM_NODES * sect_size;

	if (!*buf) {
		if (!(*buf = malloc_aligned(pub_len))) {
			LOG_INTERNAL();
			goto bail;
		}
	}

	if (myseek64(file, publ_off, SEEK_SET) == -1) {
		LOG_INTERNAL();
		goto bail;
	}

	if (myread(file, *buf, pub_len) == -1) {
		LOG_INTERNAL();
		goto bail;
	}
			
	ret = 1;

bail:
	return ret;
}				/* read_publish */

/*
 * get_node_names()
 *
 */
int get_node_names(int file, ocfs_vol_disk_hdr *volhdr, char **node_names,
		   __u32 sect_size)
{
	char *buf = NULL;
	char *p;
	int len;
	int ret = 0;
	int i;
	ocfs_disk_node_config_info *conf;

	len = volhdr->node_cfg_size;
	if (!(buf = (char *) malloc_aligned(len))) {
		LOG_INTERNAL();
		goto bail;
	} else
		memset(buf, 0, len);

	if (myseek64(file, volhdr->node_cfg_off, SEEK_SET) == -1) {
		LOG_INTERNAL();
		goto bail;
	}

	if (myread(file, buf, len) == -1) {
		LOG_INTERNAL();
		goto bail;
	}

	p = buf + (sect_size * 2);
	for (i = 0; i < OCFS_MAXIMUM_NODES; ++i, p += sect_size) {
		conf = (ocfs_disk_node_config_info *)p;
		if (conf->node_name[0])
			node_names[i] = strdup(conf->node_name);
	}

	ret = 1;
bail:
	free_aligned(buf);
	return ret;
}				/* get_node_names */


/*
 * print_node_names()
 *
 */
void print_node_names(char **node_names, __u32 nodemap)
{
	int i, j;
	char comma = '\0';

	for (j = 1, i = 0; i < OCFS_MAXIMUM_NODES; ++i, j <<= 1) {
		if (nodemap & j) {
			if (node_names[i])
				printf("%c %s", comma, node_names[i]);
			else
				printf("%c %d", comma, i);
			comma = ',';
		}
	}
	printf("\n");
}				/* print_node_names */

/*
 * print_free_bits()
 *
 */
void print_free_bits(GArray *bits, char *str)
{
	__u32 i, j, k;
	__u32 bit1, bit2;

	if (!bits)
		goto bail;

#if 0
	for (i = 0; i < bits->len; ++i) {
		bit1 = g_array_index(bits, __u32, i);
		if (i == 0)
			printf("\nbits: ");
		printf("%d, ", bit1);
	}
	printf("\n");
#endif

	for (i = 0; i < bits->len;) {
		bit1 = g_array_index(bits, __u32, i);
		for (k = 0, j = i + 1; j < bits->len; ++k, ++j) {
			bit2 = g_array_index(bits, __u32, j);
			if (bit1 + k + 1 != bit2)
				break;
		}
		if (k)
			LOG_ERROR("Bits %u-%u are unset in the %s bitmap",
				  bit1, bit1 + k, str);
		else
			LOG_ERROR("Bit %u is unset in the %s bitmap", bit1, str);
		i += k + 1;
	}

bail:
	return ;
}				/* print_free_bits */

/*
 * check_global_bitmap()
 *
 */
int check_global_bitmap(int fd)
{
	int ret = -1;
	bitmap_data *bm1;
	bitmap_data *bm2;
	__u8 *vol_bm = NULL;
	__u32 i;
	__u32 j;
	GArray *bits = NULL;
	
	bits = g_array_new(false, true, sizeof(__u32));

	/* sorting the global bitmap data on alloc_node and bit_num */
	qsort(ctxt.vol_bm_data->data, ctxt.vol_bm_data->len,
	      sizeof(bitmap_data), &qsort_compare);
#if 0
	for (i = 0; i < ctxt.vol_bm_data->len; ++i) {
		bm1 = &(g_array_index(ctxt.vol_bm_data, bitmap_data, i));
		printf("BOO: bit=%u, num=%u, blk=%u.%u, fe=%u.%u\n", bm1->bitnum,
		       bm1->num, HILO(bm1->fss_off), HILO(bm1->parent_off));
	}
#endif

	/* walk the list and check for any duplicates */
	for (i = 0; i < ctxt.vol_bm_data->len; ++i) {
		bm1 = &(g_array_index(ctxt.vol_bm_data, bitmap_data, i));
		for (j = i + 1; j < ctxt.vol_bm_data->len; ++j) {
			bm2 = &(g_array_index(ctxt.vol_bm_data, bitmap_data, j));
			if (bm2->bitnum == bm1->bitnum) {
				LOG_ERROR("Block %u.%u (bit# %u) allocated "
					  "to File Entries %u.%u and %u.%u",
					  HILO(bm1->fss_off), bm1->bitnum,
					  HILO(bm1->parent_off),
					  HILO(bm2->parent_off));
				continue;
			} else
				break;
		}
	}

	/* make a temp copy of the volume bitmap */
	if ((vol_bm = malloc_aligned(VOL_BITMAP_BYTES)) == NULL) {
		LOG_INTERNAL();
		goto bail;
	} else
		memcpy(vol_bm, ctxt.vol_bm, VOL_BITMAP_BYTES);

	/* clearing all the allocated bits in the global bitmap */
	for (i = 0; i < ctxt.vol_bm_data->len; ++i) {
		bm1 = &(g_array_index(ctxt.vol_bm_data, bitmap_data, i));
		j = __test_and_clear_bit(bm1->bitnum, (unsigned long *)vol_bm);
		if (!j) {
			if (!test_bit(bm1->bitnum, (unsigned long *)ctxt.vol_bm))
				g_array_append_val(bits, bm1->bitnum);
		}
	}

	print_free_bits(bits, "global");

#ifdef STILL_DEBUGGING
	/* cross check... ensure no bit in the global bitmap is set */
	/* The first 1MB in the bitmap is for the system fe's */
	j = VOL_BITMAP_BYTES / ctxt.hdr->cluster_size;
	for (i = j; i < ctxt.hdr->num_clusters; ++i) {
		if (test_bit(i, vol_bm))
			LOG_ERROR("Bit %u in the global bitmap is "
				  "unaccounted", i);
	}
#endif

	ret = 0;
bail:
	if (bits)
		g_array_free(bits, true);
	free_aligned(vol_bm);
	return ret;
}				/* check_global_bitmap */


/*
 * check_node_bitmaps()
 *
 * Checks extent and directory bitmaps for all nodes
 *
 */
int check_node_bitmaps(int fd, GArray *bm_data, __u8 **node_bm,
		       __u32 *node_bm_sz, char *str)
{
	int ret = -1;
	bitmap_data *bm1;
	bitmap_data *bm2;
	__u8 *temp_bm[OCFS_MAXIMUM_NODES];
	__u32 i;
	__u32 j;

	/* sorting the node bitmap data on alloc_node and bit_num */
	qsort(bm_data->data, bm_data->len, sizeof(bitmap_data), &qsort_compare);
#ifdef STILL_DEBUGGING
	for (i = 0; i < bm_data->len; ++i) {
		bm1 = &(g_array_index(bm_data, bitmap_data, i));
	}
#endif
	for (i = 0; i < bm_data->len; ++i) {
		bm1 = &(g_array_index(bm_data, bitmap_data, i));
		for (j = i + 1; j < bm_data->len; ++j) {
			bm2 = &(g_array_index(bm_data, bitmap_data, j));
			if (bm2->alloc_node != bm1->alloc_node)
				break;
			if (bm2->bitnum == bm1->bitnum) {
				LOG_ERROR("Block %u.%u (bit# %u) allocated "
					  "to %s %u.%u and %u.%u on node %u",
					  HILO(bm1->fss_off), bm1->bitnum,
					  str, HILO(bm1->parent_off),
					  HILO(bm2->parent_off), bm1->alloc_node);
				continue;
			} else
				break;
		}
	}

	/* make a temp copy of the node bitmaps */
	for (i = 0; i < OCFS_MAXIMUM_NODES; ++i) {
		if (!node_bm_sz[i]) {
			temp_bm[i] = NULL;
			continue;
		}
		if ((temp_bm[i] = malloc_aligned(node_bm_sz[i])) == NULL) {
			LOG_INTERNAL();
			goto bail;
		} else
			memcpy(temp_bm[i], node_bm[i], node_bm_sz[i]);
	}

	/* clearing all the allocated bits in the extent bitmap */
	for (i = 0; i < bm_data->len; ++i) {
		bm1 = &(g_array_index(bm_data, bitmap_data, i));
		if (!temp_bm[bm1->alloc_node]) {
			LOG_ERROR("%s bitmap for node %d not allocated but "
				  "structure at offset %u.%u suggests otherwise",
				  str, bm1->alloc_node, HILO(bm1->fss_off));
			continue;
		}
		j = __test_and_clear_bit(bm1->bitnum, (unsigned long *)temp_bm[bm1->alloc_node]);
		if (!j) {
			if (!test_bit(bm1->bitnum, (unsigned long *)node_bm[bm1->alloc_node]))
				LOG_ERROR("Bit %u is unset in the %s bitmap "
					  "of node %d", bm1->bitnum, str,
					  bm1->alloc_node);
		}
	}

#ifdef STILL_DEBUGGING
	/* cross check... ensure no bit in the extent/directory bitmap is set */
	for (i = 0; i < OCFS_MAXIMUM_NODES; ++i) {
		if (!temp_bm[i])
			continue;
		len = node_bm_sz[i] * 8;
		for (j = 0; j < len; ++j) {
			if (test_bit(j, temp_bm[i]))
				LOG_ERROR("Bit %u in the %s bitmap of node "
					  "%d is unaccounted", j, str, i);
		}
	}
#endif

	ret = 0;
bail:
	for (i = 0; i < OCFS_MAXIMUM_NODES; ++i)
		free_aligned(temp_bm[i]);
	return ret;
}				/* check_node_bitmaps */


/*
 * qsort_compare()
 *
 */
int qsort_compare(const void *q1, const void *q2)
{
	bitmap_data *bm1 = (bitmap_data *)q1;
	bitmap_data *bm2 = (bitmap_data *)q2;
	__s32 ret;

	ret = bm1->alloc_node - bm2->alloc_node;
	if (!ret)
		ret = bm1->bitnum - bm2->bitnum;

	return ret;
}				/* qsort_compare */

static int fe_compare_func(const void *m1, const void *m2);

/* if we ever rewrite this as a shared library or 
 * parallelized fsck we will have to change this */
ocfs_dir_node *globaldir = NULL;

static int fe_compare_func(const void *m1, const void *m2)
{
	ocfs_file_entry *fe1, *fe2;
	__u8 idx1, idx2;
	int ret;

	if (globaldir == NULL) {
		LOG_INTERNAL();
		exit(0);
	}

	idx1 = *(__u8 *)m1;
	idx2 = *(__u8 *)m2;

	fe1 = (ocfs_file_entry *) ((char *)FIRST_FILE_ENTRY(globaldir) + (idx1 * OCFS_SECTOR_SIZE));
	fe2 = (ocfs_file_entry *) ((char *)FIRST_FILE_ENTRY(globaldir) + (idx2 * OCFS_SECTOR_SIZE));

	if (IS_FE_DELETED(fe1->sync_flags) ||
	    (!(fe1->sync_flags & OCFS_SYNC_FLAG_VALID)) ||
	    IS_FE_DELETED(fe2->sync_flags) ||
	    (!(fe2->sync_flags & OCFS_SYNC_FLAG_VALID)))
		return 0;

	ret = strncmp(fe1->filename, fe2->filename, 255);
	
	return -ret;
}


/*
 * traverse_dir_nodes()
 *
 */
void traverse_dir_nodes(int fd, __u64 offset, char *dirpath)
{
	int i;
	int ret;
	char *dirbuf = NULL;
	ocfs_file_entry *febuf = NULL;
	__u64 dir_offset;
	__u64 off;
	ocfs_disk_structure *dirst;
	ocfs_disk_structure *fest;
	GHashTable *bad;
	ocfs_dir_node *dir;
	__u8 *index = NULL;
	int deleted_files;
   
	dirst = &dirnode_t;
	fest = &fileent_t;
	bad = NULL;

	if ((index = malloc(256)) == NULL) {
		LOG_INTERNAL();
		goto bail;
	}

	if ((dirbuf = malloc_aligned(DIR_NODE_SIZE)) == NULL) {
		LOG_INTERNAL();
		goto bail;
	}

	if ((febuf = (ocfs_file_entry *) malloc_aligned(OCFS_SECTOR_SIZE)) == NULL) {
		LOG_INTERNAL();
		goto bail;
	}

	dir_offset = offset;
	dir = (ocfs_dir_node *)dirbuf;

	CLEAR_AND_PRINT(dirpath);

	while (1) {
		ret = read_print_struct(dirst, dirbuf, dir_offset, 0, &bad);

		if (bad)
			g_hash_table_destroy(bad);

		if (ret == -1) {
			LOG_ERROR("failed to read directory at offset %u.%u",
				  HILO(dir_offset));
			goto bail;
		}

		/* check the dir->index integrity */
		globaldir = dir;
		memcpy(index, dir->index, dir->num_ent_used);
		qsort(index, dir->num_ent_used, sizeof(__u8), fe_compare_func);

		if (memcmp(index, dir->index, dir->num_ent_used) != 0) {
			__u8 *idxtmp = NULL;

			LOG_ERROR("Bad dir index found");
			printf("\n");
			for (i=0; i<254; ++i) {
				if (dir->index[i] != index[i]) {
					ocfs_file_entry *f1, *f2;
					f1 = (ocfs_file_entry *)((char *)dirbuf + 512 + (dir->index[i]*512));
					f2 = (ocfs_file_entry *)((char *)dirbuf + 512 + (index[i]*512));
					printf("%d, %s: %d, %s\n", dir->index[i], f1->filename, index[i], f2->filename);
				}
			}
			if (ctxt.write_changes) {
				if ((idxtmp = malloc(256)) == NULL) {
					LOG_INTERNAL();
					goto bail;
				}
				memcpy(idxtmp, dir->index, 256);
				memcpy(dir->index, index, dir->num_ent_used);
				if (dirst->write(ctxt.fd, dirbuf, dir_offset, 0) == -1) {
					LOG_ERROR("failed to write at offset %u.%u",
						  HILO(dir_offset));
					memcpy(dir->index, idxtmp, 256);
				}
				safefree(idxtmp);
				LOG_PRINT("Fixed");
			} else
				LOG_PRINT("To fix, rerun with -w");
		}

		/* check the undeletable dir bug, BUG #3016598 */
		for (i=0, deleted_files=0; i < dir->num_ent_used; i++) {
			if (IS_FE_DELETED((FILEENT(dir, i))->sync_flags))
				deleted_files++;
		}
		if (dir->num_ent_used && dir->num_ent_used == deleted_files) {
			/* we hit the bug... fix by zeroing num_ent_used */
			LOG_ERROR("Undeletable directory found");
			if (ctxt.write_changes) {
				dir->num_ent_used = 0;
				if (dirst->write(ctxt.fd, dirbuf, dir_offset, 0)==-1) {
					LOG_ERROR("failed to write at offset %u/%u", dir_offset);
					dir->num_ent_used = 1;
				}
				LOG_PRINT("Fixed");
			} else
				LOG_PRINT("To fix, rerun with -w");
		}

		/* Add bitmap entry for the dirnode itself */
		add_bm_data(dir->alloc_file_off, 1, dir->alloc_node, 
			    dir_offset,
			    (dir->alloc_node == OCFS_INVALID_NODE_NUM ? bm_filedata : bm_dir));

		// TODO: add in directory editing here
		for (i = 0; i < dir->num_ent_used; i++) {
			off = dir_offset;
			off += OCFS_SECTOR_SIZE;	/* move past the dirnode header */
			off += (OCFS_SECTOR_SIZE * dir->index[i]);

			ret = read_print_struct(fest, (char *)febuf, off, 0, &bad);
			if (bad)
				g_hash_table_destroy(bad);
			if (ret == -1) {
				LOG_ERROR("failed to read file entry at offset %u.%u",
					  HILO(off));
				continue;
			}

			// TODO: add in file entry editing here

			if (!IS_FE_DELETED(febuf->sync_flags))
				check_file_entry(fd, febuf, off, false, dirpath);
		}

		/* is there another directory chained off of this one? */
		if (dir->next_node_ptr == -1)
			break;		// nope, we're done
		else
			dir_offset = dir->next_node_ptr;	// keep going
	}

bail:
	free_aligned(dirbuf);
	free_aligned(febuf);
	safefree(index);
}				/* traverse_dir_nodes */

/*
 * handle_one_cdsl_entry()
 *
 */
void handle_one_cdsl_entry(int fd, ocfs_file_entry *fe, __u64 offset)
{
}				/* handle_one_cdsl_entry */


/*
 * check_file_entry()
 *
 */
void check_file_entry(int fd, ocfs_file_entry *fe, __u64 offset,
		      bool systemfile, char *dirpath)
{
	void *buf = NULL;
	int indx = 0;
	int val = 0;
	char *path = NULL;

	if (systemfile)
		val = 3;
	else {
		if (fe->attribs & OCFS_ATTRIB_FILE_CDSL)
			val = 1;
		else if (fe->attribs & OCFS_ATTRIB_DIRECTORY)
			val = 2;
		else if (fe->attribs & (OCFS_ATTRIB_REG | OCFS_ATTRIB_SYMLINK))
			val = 3;
		else {
			LOG_ERROR ("unknown attribs %x at offset %u.%u",
				   fe->attribs, HILO(offset));
			goto bail;
		}
	}

	++cnt_obj;
	if (val == 2)
		path = g_strdup_printf("%s%s/", dirpath, fe->filename);
	else
		path = g_strdup_printf("%s%s", dirpath, fe->filename);

	switch (val) {
	case 1:
		CLEAR_AND_PRINT(path);
		handle_one_cdsl_entry(fd, fe, offset);
		break;

	case 2:
		if (fe->extents[0].disk_off) {
			handle_leaf_extents(fd, fe->extents, 1,
				 	OCFS_INVALID_NODE_NUM, fe->this_sector);
			traverse_dir_nodes(fd, fe->extents[0].disk_off, path);
		} else
			LOG_ERROR("Invalid dir entry at %u.%u", HILO(offset));
		break;

	case 3:
		CLEAR_AND_PRINT(path);
		if (fe->local_ext)
			handle_leaf_extents(fd, fe->extents,
					    OCFS_MAX_FILE_ENTRY_EXTENTS,
					    OCFS_INVALID_NODE_NUM, fe->this_sector);
		else {
			if ((buf = malloc_aligned(MAX_EXTENTS * OCFS_SECTOR_SIZE)) == NULL) {
				LOG_INTERNAL();
				goto bail;
			}

			traverse_fe_extents(fd, fe, buf, &indx);

			/* check ext->next_data_ext */
			check_next_data_ext(fe, buf, indx);

			/* check fe->last_ext_ptr */
			check_fe_last_data_ext(fe, buf, indx);
		}
		break;

	default:
		break;
	}

bail:
	safefree(path);
	free_aligned(buf);
	return ;
}				/* check_file_entry */

/*
 * add_bm_data()
 *
 */
bitmap_data * add_bm_data(__u64 start, __u64 len, __s32 alloc_node,
			  __u64 parent_offset, int type)
{
	bitmap_data *bm = NULL;
	__u32 bitnum = 0;
	__u32 num = 0;
	void *buf = NULL;
	void *p;
	int i;

	switch (type) {
	case bm_extent:
		bitnum = start >> OCFS_LOG_SECTOR_SIZE;
		num = len;
		break;

	case bm_dir:
		bitnum = start / OCFS_DEFAULT_DIR_NODE_SIZE;
		num = len;
		break;

	case bm_symlink:
		break;

	case bm_filedata:
		bitnum = (start - ctxt.hdr->data_start_off) >>
				ctxt.cluster_size_bits;
		num = len >> ctxt.cluster_size_bits;
		break;

	default:
		break;
	}

	if (num == 0)
		goto bail;

	if ((buf = malloc(sizeof(bitmap_data) * num)) == NULL) {
		LOG_INTERNAL();
		goto bail;
	}

	for (i = 0, p = buf; i < num; ++i) {
		bm = (bitmap_data *)p;
		bm->bitnum = bitnum + i;
		bm->fss_off = start;
		bm->alloc_node = alloc_node;
		bm->parent_off = parent_offset;
		p += sizeof(bitmap_data);
	}

	bm = (bitmap_data *)buf;

	switch (type) {
	case bm_dir:
		g_array_append_vals(ctxt.dir_bm_data, bm, num);
		break;

	case bm_extent:
		g_array_append_vals(ctxt.ext_bm_data, bm, num);
		break;

	case bm_filedata:
		g_array_append_vals(ctxt.vol_bm_data, bm, num);
		break;

	default:
		break;
	}

bail:
	return bm;
}				/* add_bm_data */


/*
 * handle_leaf_extents()
 *
 */
int handle_leaf_extents (int fd, ocfs_alloc_ext *arr, int num, __u32 node,
			 __u64 parent_offset)
{
	int i;
	int ret = 0;

	for (i = 0; i < num; i++) {
		if (arr[i].disk_off)
			if (!add_bm_data(arr[i].disk_off, arr[i].num_bytes,
					 node, parent_offset, bm_filedata))
				ret = -1;
	}

	return ret;
}				/* handle_leaf_extents */


/*
 * traverse_extent()
 *
 */
void traverse_extent(int fd, ocfs_extent_group * exthdr, int flag, void *buf,
		     int *indx)
{
	ocfs_extent_group *ext = NULL;
	int i;
	int j;
	__u64 len;
	int ret;
	int type;
	ocfs_disk_structure *disk_struct;
	GHashTable *bad;

	if (*indx >= MAX_EXTENTS) {
		LOG_ERROR("Too many extents after ext=%u.%u",
			  HILO(exthdr->this_ext));
		goto bail;
	}

	for (i = 0; i < exthdr->next_free_ext; ++i) {
		if (!exthdr->extents[i].disk_off)
			continue;

		ext = (ocfs_extent_group *) (buf + (*indx * OCFS_SECTOR_SIZE));
		++*indx;

		if (flag == OCFS_EXTENT_HEADER)
			disk_struct = &exthdr_t;
		else
			disk_struct = &extdat_t;

		ret = read_print_struct(disk_struct, (char *)ext,
					exthdr->extents[i].disk_off, 0, &bad);

		if (bad)
			g_hash_table_destroy(bad);

		if (ret == -1) {
			LOG_ERROR("failed to read extent at offset %u.%u",
				  HILO(exthdr->extents[i].disk_off));
			goto bail;
		}

		// TODO: add in extent editing here

		/* check up_hdr_node_ptr */
		if (exthdr->this_ext != ext->up_hdr_node_ptr) {
			LOG_ERROR("up_hdr_node_ptr %u.%u in extent %u.%u "
				  "should be %u.%u", HILO(ext->up_hdr_node_ptr),
				  HILO(ext->this_ext), HILO(exthdr->this_ext));
		}

		/* check first file offset */
		if (exthdr->extents[i].file_off != ext->extents[0].file_off) {
			LOG_ERROR("extents[0].file_off=%u.%u in extent %u.%u "
				  "should be %u.%u", HILO(ext->extents[0].file_off),
				  HILO(ext->this_ext), HILO(exthdr->extents[i].file_off));
		}

		/* check total bytes */
		for (j = 0, len = 0; j < OCFS_MAX_DATA_EXTENTS; j++)
			len += ext->extents[j].num_bytes;

		if (exthdr->extents[i].num_bytes != len) {
			LOG_ERROR("total num_bytes in extent %u.%u is %u.%u "
				  "but should be %u.%u", HILO(ext->this_ext),
				  HILO(len), HILO(exthdr->extents[i].num_bytes));
		}

		/* Add bitmap entry for the extent itself */
		add_bm_data(ext->alloc_file_off, 1, ext->alloc_node, ext->this_ext,
			    bm_extent);

		if (flag == OCFS_EXTENT_HEADER) {
			type = ext->granularity ? OCFS_EXTENT_HEADER : OCFS_EXTENT_DATA;
			traverse_extent(fd, ext, type, buf, indx);
		} else {
			handle_leaf_extents(fd, ext->extents, ext->next_free_ext,
					    OCFS_INVALID_NODE_NUM, ext->this_ext);
		}
	}

bail:
	return ;
}				/* traverse_extent */


/*
 * traverse_fe_extents()
 *
 */
void traverse_fe_extents(int fd, ocfs_file_entry *fe, void *buf, int *indx)
{
	int i;
	int j;
	int ret;
	__u64 len;
	ocfs_extent_group *ext = NULL;
	int type;
	ocfs_disk_structure *disk_struct;
	GHashTable *bad;

	if (*indx >= MAX_EXTENTS) {
		LOG_ERROR("error too many extents in fe at offset %u.%u",
			  HILO(fe->this_sector));
		goto bail;
	}

	for (i = 0; i < fe->next_free_ext; i++) {
		if (!fe->extents[i].disk_off)
			continue;

		ext = (ocfs_extent_group *) (buf + (*indx * OCFS_SECTOR_SIZE));
		++*indx;

		if (fe->granularity)
			disk_struct = &exthdr_t;
		else
			disk_struct = &extdat_t;

		ret = read_print_struct(disk_struct, (char *)ext, 
					fe->extents[i].disk_off, 0, &bad);
		if (bad)
			g_hash_table_destroy(bad);

		if (ret == -1) {
			LOG_ERROR("failed to read extent at offset %u.%u",
				  HILO(fe->extents[i].disk_off));
			goto bail;
		}

		// TODO: add in extent editing here
		if (fe->this_sector != ext->up_hdr_node_ptr) {
			LOG_ERROR("up_hdr_node_ptr %u.%u in extent %u.%u "
				  "should be %u.%u", HILO(ext->up_hdr_node_ptr),
				  HILO(ext->this_ext), HILO(fe->this_sector));
		}

		/* check first file offset */
		if (fe->extents[i].file_off != ext->extents[0].file_off) {
			LOG_ERROR("extents[0].file_off=%u.%u in extent %u.%u "
				  "should be %u.%u", HILO(ext->extents[0].file_off),
				  HILO(ext->this_ext), HILO(fe->extents[i].file_off));
		}

		/* check total bytes */
		for (j = 0, len = 0; j < OCFS_MAX_DATA_EXTENTS; j++)
			len += ext->extents[j].num_bytes;

		if (fe->extents[i].num_bytes != len) {
			LOG_ERROR("total num_bytes in extent %u.%u is %u.%u "
				  "but should be %u.%u", HILO(ext->this_ext),
				  HILO(len), HILO(fe->extents[i].num_bytes));
		}

		/* Add bitmap entry for the extent itself */
		add_bm_data(ext->alloc_file_off, 1, ext->alloc_node, ext->this_ext,
			    bm_extent);

		if (fe->granularity) {
			type = ext->granularity ? OCFS_EXTENT_HEADER : OCFS_EXTENT_DATA;
			traverse_extent(fd, ext, type, buf, indx);
		} else {
			handle_leaf_extents(fd, ext->extents, ext->next_free_ext,
					    OCFS_INVALID_NODE_NUM, ext->this_ext);
		}
	}

bail:
	return ;
}				/* traverse_fe_extents */


/*
 * check_next_data_ext()
 *
 */
int check_next_data_ext(ocfs_file_entry *fe, void *buf, int indx)
{
	void *ptr;
	int i;
	__u64 next_data_ext;
	ocfs_extent_group *ext;
	int ret = 0;

	ptr = buf + ((indx - 1) * OCFS_SECTOR_SIZE);

	for (i = indx - 1, next_data_ext = 0; i >= 0; --i,
	     ptr -= OCFS_SECTOR_SIZE) {
		ext = (ocfs_extent_group *)ptr;

		if (ext->type != OCFS_EXTENT_DATA)
			continue;

		if (ext->next_data_ext != next_data_ext) {
			LOG_ERROR("ext->next_data_ext=%u.%u in extent "
				  "%u.%u instead of %u.%u",
				  HILO(ext->next_data_ext),
				  HILO(ext->this_ext),
				  HILO(next_data_ext));
			ret = -1;
		}
		next_data_ext = ext->this_ext;
	}

	return ret;
}				/* check_next_data_ext */

/*
 * check_fe_last_data_ext()
 *
 */
int check_fe_last_data_ext(ocfs_file_entry *fe, void *buf, int indx)
{
	ocfs_extent_group *ext;
	int ret = 0;

	ext = (ocfs_extent_group *) (buf + ((indx - 1) * OCFS_SECTOR_SIZE));

	if (fe->last_ext_ptr != ext->this_ext) {
		LOG_ERROR("fe->last_ext_ptr=%u.%u in fe %u.%u "
			  "instead of %u.%u", HILO(fe->last_ext_ptr),
			  HILO(fe->this_sector), HILO(ext->this_ext));
		ret = -1;
	}

	return ret;
}				/* check_fe_last_data_ext */
