/*
 * print.c
 *
 * stdout printing support for debugocfs
 *
 * Copyright (C) 2002 Oracle Corporation.  All rights reserved.
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
 * Author: Kurt Hackel
 */

#include "debugocfs.h"

extern char *optarg;
extern int optind, opterr, optopt;

extern int filenum;
extern user_args args;
extern __u32 OcfsDebugCtxt;
extern __u32 OcfsDebugLevel;

void print_time(__u64 * sec);
void print_file_attributes(__u32 attribs);
void print_protection_bits(__u32 prot);
void print_global_bitmap(int fd, void *buf);
void print_system_file(int fd, ocfs_vol_disk_hdr * v, int fileid);
void print_file_data(int fd, ocfs_file_entry * fe);
void print_alloc_log(ocfs_alloc_log * rec);
void print_dir_log(ocfs_dir_log * rec);
void print_recovery_log(ocfs_recovery_log * rec);
void print_lock_log(ocfs_lock_log * rec);
void print_bcast_rel_log(ocfs_bcast_rel_log * rec);
void print_delete_log(ocfs_delete_log * rec);
void print_free_log(ocfs_free_log * rec);
void print_extent_rec(ocfs_free_extent_log * rec);


typedef __u64 (*bit2off_func)(int bitnum, void *data);
__u64 global_bm_bitnum_to_offset(int bitnum, void *data);
void print_bitmap(char *bmbuf, int bmsize, bit2off_func func, void *data);

void print_vol_label(void *buf)
{
    ocfs_vol_label *v = (ocfs_vol_label *)buf;
    print_disk_lock(&v->disk_lock);
    printf("\tlabel = %s\n", v->label);
    printf("\tlabel_len = %u\n", v->label_len);
}

void print_vol_disk_header(void *buf)
{
    ocfs_vol_disk_hdr * v = (ocfs_vol_disk_hdr *)buf;
    printf("\tversion = %u.%u\n", v->major_version, v->minor_version);
    printf("\tsignature = %s\n", v->signature);
    printf("\tmount_point = %s\n", v->mount_point);
    printf("\tserial_num = %llu\n", v->serial_num);
    printf("\tdevice_size = %llu\n", v->device_size);
    printf("\tstart_off = %llu\n", v->start_off);
    printf("\tbitmap_off = %llu\n", v->bitmap_off);
    printf("\tpubl_off = %llu\n", v->publ_off);
    printf("\tvote_off = %llu\n", v->vote_off);
    printf("\troot_bitmap_off = %llu\n", v->root_bitmap_off);
    printf("\tdata_start_off = %llu\n", v->data_start_off);
    printf("\troot_bitmap_size = %llu\n", v->root_bitmap_size);
    printf("\troot_off = %llu\n", v->root_off);
    printf("\troot_size = %llu\n", v->root_size);
    printf("\tcluster_size = %llu\n", v->cluster_size);
    printf("\tnum_nodes = %llu\n", v->num_nodes);
    printf("\tnum_clusters = %llu\n", v->num_clusters);
    printf("\tdir_node_size = %llu\n", v->dir_node_size);
    printf("\tfile_node_size = %llu\n", v->file_node_size);
    printf("\tinternal_off = %llu\n", v->internal_off);
    printf("\tnode_cfg_off = %llu\n", v->node_cfg_off);
    printf("\tnode_cfg_size = %llu\n", v->node_cfg_size);
    printf("\tnew_cfg_off = %llu\n", v->new_cfg_off);
    printf("\tprot_bits = 0%o\n", v->prot_bits);
    printf("\tuid = %u\n", v->uid);
    printf("\tgid = %u\n", v->gid);
    printf("\texcl_mount = %d\n", v->excl_mount);
    printf("\tdisk_hb = %d\n", v->disk_hb);
    printf("\thb_timeo = %d\n", v->hb_timeo);
}

__u64 global_bm_bitnum_to_offset(int bitnum, void *data)
{
    ocfs_vol_disk_hdr *v = (ocfs_vol_disk_hdr *)data;
    return (((__u64)bitnum * v->cluster_size) + v->data_start_off);
}

void print_bitmap(char *bmbuf, int bmsize, bit2off_func func, void *data)
{    
    int i;
    __u64 off;

    printf("\tSET\n");
    for (i=0; i<bmsize; i++)
    {
        off = func(i, data);
        if (test_bit(i, (unsigned long *)bmbuf))
        {
            printf("\t\t%llu (%d)\n", off, i);
        }
    }
    printf("\tUNSET\n");
    for (i=0; i<bmsize; i++)
    {
        off = func(i, data);
        if (!test_bit(i, (unsigned long *)bmbuf))
        {
            printf("\t\t%llu (%d)\n", off, i);
        }
    }
}

void print_global_bitmap(int fd, void *buf)
{
    __u64 dso, cs, num;
    char *bmbuf;
    int bufsz;
    ocfs_vol_disk_hdr * v = (ocfs_vol_disk_hdr *)buf;

    dso = v->data_start_off;
    cs = v->cluster_size;
    num = v->num_clusters;
    bufsz = (num+7)/8;
    bufsz = OCFS_ALIGN(bufsz, 512);
    bmbuf = (char *)malloc_aligned(bufsz); 
    myseek64(fd, v->bitmap_off, SEEK_SET);
    read(fd, bmbuf, bufsz);

    printf("\tbitmap_off = %llu\n", v->bitmap_off);
    printf("\tdata_start_off = %llu\n", v->data_start_off);
    printf("\tcluster_size = %llu\n", v->cluster_size);
    printf("\tnum_clusters = %llu\n", v->num_clusters);
    print_bitmap(bmbuf, num, global_bm_bitnum_to_offset, v);
    free_aligned(bmbuf);
}

void print_dir_node(void *buf)
{
    int i;
    ocfs_dir_node * d = (ocfs_dir_node *)buf;

    print_disk_lock(&d->disk_lock);
    printf("\talloc_file_off = %llu\n", d->alloc_file_off);
    printf("\talloc_node = %u\n", d->alloc_node);
    printf("\tfree_node_ptr = ");
    print_node_pointer(d->free_node_ptr);
    printf("\tnode_disk_off = ");
    print_node_pointer(d->node_disk_off);
    printf("\tnext_node_ptr = ");
    print_node_pointer(d->next_node_ptr);
    printf("\tindx_node_ptr = ");
    print_node_pointer(d->indx_node_ptr);
    printf("\tnext_del_ent_node = ");
    print_node_pointer(d->next_del_ent_node);
    printf("\thead_del_ent_node = ");
    print_node_pointer(d->head_del_ent_node);
    printf("\tfirst_del = %d\n", d->first_del);
    printf("\tnum_del = %d\n", d->num_del);
    printf("\tnum_ents = %d\n", d->num_ents);
    printf("\tdepth = %d\n", d->depth);
    printf("\tnum_ent_used = %d\n", d->num_ent_used);
    printf("\tdir_node_flags = %s\n",
	   d->
	   dir_node_flags & DIR_NODE_FLAG_ROOT ? "DIR_NODE_FLAG_ROOT" : "");
    printf("\tsync_flags = 0x%x\n", d->sync_flags);
    printf("\tindex_dirty = %s\n", d->index_dirty ? "true" : "false");
    printf("\tbad_off = %d\n", d->bad_off);

    printf("\tindex = ");
    for (i = 0; i < 256; i++)
    {
	if (i % 16 == 0 && i != 0)
	    printf("\n\t\t");
	printf("%3u ", d->index[i]);
    }
    printf("\n");
}

void print_file_entry(void *buf)
{
    ocfs_file_entry * fe = (ocfs_file_entry *)buf;
    char fname[OCFS_MAX_FILENAME_LENGTH + 1];
    int i;
    ocfs_alloc_ext *ext;

    strncpy(fname, fe->filename, OCFS_MAX_FILENAME_LENGTH);
    fname[OCFS_MAX_FILENAME_LENGTH] = '\0';
    print_disk_lock(&fe->disk_lock);
    printf("\tlocal_ext = %s\n", fe->local_ext ? "true" : "false");
    printf("\tgranularity = %d\n", fe->granularity);
    printf("\tfilename = %s\n", fname);
    printf("\tfilename_len = %d\n", fe->filename_len);
    if (args.twoFourbyte)
    	printf("\tfile_size = %u.%u\n", HILO(fe->file_size));
    else
    	printf("\tfile_size = %llu\n", fe->file_size);
    if (args.twoFourbyte)
        printf("\talloc_size = %u.%u\n", HILO(fe->alloc_size));
    else
        printf("\talloc_size = %llu\n", fe->alloc_size);
    printf("\tattribs = ");
    print_file_attributes((__u32) fe->attribs);
    printf("\tprot_bits = ");
    print_protection_bits((__u32) fe->prot_bits);
    printf("\tuid = %d\n", fe->uid);
    printf("\tgid = %d\n", fe->gid);
    printf("\tcreate_time = ");
    print_time(&(fe->create_time));
    printf("\tmodify_time = ");
    print_time(&(fe->modify_time));
    printf("\tdir_node_ptr = ");
    print_node_pointer(fe->dir_node_ptr);
    printf("\tthis_sector = ");
    print_node_pointer(fe->this_sector);
    printf("\tlast_ext_ptr = ");
    print_node_pointer(fe->last_ext_ptr);
    printf("\tsync_flags = ");
    print_synch_flags(fe->sync_flags);
    printf("\tlink_cnt = %u\n", fe->link_cnt);
    printf("\tnext_del = %d\n", fe->next_del);
    printf("\tnext_free_ext = %u\n", fe->next_free_ext);

    for (i = 0; i < OCFS_MAX_FILE_ENTRY_EXTENTS; i++)
    {
	ext = &(fe->extents[i]);
	if (args.twoFourbyte) {
	    printf("\textent[%d].file_off = %u.%u\n", i, HILO(ext->file_off));
	    printf("\textent[%d].num_bytes = %u.%u\n", i, HILO(ext->num_bytes));
	    printf("\textent[%d].disk_off = %u.%u\n", i, HILO(ext->disk_off));
	} else {
	    printf("\textent[%d].file_off = %llu\n", i, ext->file_off);
	    printf("\textent[%d].num_bytes = %llu\n", i, ext->num_bytes);
	    printf("\textent[%d].disk_off = %llu\n", i, ext->disk_off);
	}
    }

    printf("\n");
}

void print_extent_ex(void *buf)
{
	print_extent(buf, 1, args.twoFourbyte);
}

void print_extent(void *buf, int twolongs, bool prev_ptr_error)
{
    ocfs_extent_group * exthdr = (ocfs_extent_group *)buf;
    char sig[10];
    int i;
    __u64 len;
    char err[200];
    ocfs_alloc_ext *ext;

#define ERROR_STR	"<========== ERROR"

    strncpy(sig, exthdr->signature, sizeof(sig));
    printf("\tsignature = %s\n", sig);
    printf("\tnext_free_ext = %d\n", exthdr->next_free_ext);
    printf("\tcurr_sect = %u\n", exthdr->curr_sect);
    printf("\tmax_sects = %u\n", exthdr->max_sects);
    printf("\ttype = %u\n", exthdr->type);
    printf("\tgranularity = %d\n", exthdr->granularity);
    printf("\talloc_node = %u\n", exthdr->alloc_node);

    *err = '\0';
    if (prev_ptr_error)
	strcpy(err, ERROR_STR);

    if (twolongs) {
	printf("\tthis_ext = %u.%u\n", HILO(exthdr->this_ext));
	printf("\tnext_data_ext = %u.%u\n", HILO(exthdr->next_data_ext));
	printf("\talloc_file_off = %u.%u\n", HILO(exthdr->alloc_file_off));
	printf("\tlast_ext_ptr = %u.%u\n", HILO(exthdr->last_ext_ptr));
	printf("\tup_hdr_node_ptr = %u.%u %s\n", HILO(exthdr->up_hdr_node_ptr), err);
    } else {
	printf("\tthis_ext = %llu\n", exthdr->this_ext);
	printf("\tnext_data_ext = %llu\n", exthdr->next_data_ext);
	printf("\talloc_file_off = %llu\n", exthdr->alloc_file_off);
	printf("\tlast_ext_ptr = %llu\n", exthdr->last_ext_ptr);
	printf("\tup_hdr_node_ptr = %llu %s\n", exthdr->up_hdr_node_ptr, err);
    }

    len = exthdr->extents[0].file_off;

    for (i = 0; i < OCFS_MAX_DATA_EXTENTS; i++) {
	ext = &(exthdr->extents[i]);
	if (ext->file_off == 0)
		len = 0;
	*err = '\0';
        if (len != ext->file_off)
		sprintf(err, "%s(%llu, %llu)", ERROR_STR, len, len - ext->file_off);
	if (twolongs) {
		printf("\textent[%d].file_off = %u.%u %s\n", i, HILO(ext->file_off), err);
		printf("\textent[%d].num_bytes = %u.%u\n", i, HILO(ext->num_bytes));
		printf("\textent[%d].disk_off = %u.%u\n", i, HILO(ext->disk_off));
	} else {
		printf("\textent[%d].file_off = %llu %s\n", i, ext->file_off, err);
		printf("\textent[%d].num_bytes = %llu\n", i, ext->num_bytes);
		printf("\textent[%d].disk_off = %llu\n", i, ext->disk_off);
	}
	len += ext->num_bytes;
    }

    printf("\n");
}

void print_vote_sector(void *buf)
{
    ocfs_vote * vote = (ocfs_vote *)buf;
    printf("\tseq_num = %llu\n", vote->vote_seq_num);
    printf("\tdir_ent = %llu\n", vote->dir_ent);
    printf("\topen_handle = %s\n", vote->open_handle ? "Yes" : "No");
    {
	int j;

	for (j = 0; j < MAX_NODES; j++)
	{
	    if (args.voteNodes[j])
	    {
		printf("\tVote%d = ", j);
		print_vote_type(vote->vote[j]);
	    }
	}
    }
    printf("\n");
}

void print_publish_sector(void *buf)
{
    ocfs_publish * pub = (ocfs_publish *)buf;
    printf("\ttime = ");
    printf("%u.%u\n", HI(pub->time), LO(pub->time));
    printf("\tvote = %s\n", pub->vote ? "Yes" : "No");
    printf("\tdirty = %s\n", pub->dirty ? "Yes" : "No");
    printf("\tvote_type = ");
    print_publish_flags(pub->vote_type);
    printf("\tvote_map = ");
    print___u64_as_bitmap(pub->vote_map);
    printf("\tseq_num = %llu\n", pub->publ_seq_num);
    printf("\tdir_ent = %llu\n", pub->dir_ent);
    {
	int j;

	printf("\thbm = ");
	for (j = 0; j < MAX_NODES; j++)
	{
	    if (args.publishNodes[j])
		printf("%u ", pub->hbm[j]);
	}
	printf("\n");
    }
    printf("\n");
}

void print_cdsl_offsets(void *buf)
{
    __u64 * off = (__u64 *)buf;
    int i;

    for (i = 0; i < 32; i++)
    {
	printf("\tOffset[%d] = %llu\n", i, off[i]);
    }
}

void print_disk_lock(void *buf)
{
    ocfs_disk_lock * l = (ocfs_disk_lock *)buf;

    printf("\tcurr_master = ");
    if (l->curr_master == -1)
	printf("INVALID_MASTER\n");
    else
	printf("%d\n", l->curr_master);
    printf("\tfile_lock = ");
    print_lock_type(l->file_lock);
    printf("\toin_node_map = ");
    print___u64_as_bitmap(l->oin_node_map);
    printf("\tseq_num = %llu\n", l->dlock_seq_num);
}

int ocfs_find_clear_bits (ocfs_alloc_bm * bitmap, __u32 numBits, __u32 offset, __u32 sysonly);

void print_system_file(int fd, ocfs_vol_disk_hdr * v, int fileid)
{
    ocfs_super *vcb = NULL;
    void *buf = NULL;
    int nodenum, size;
    __u64 diskoff; 
    int type = OCFS_INVALID_SYSFILE;
    ocfs_file_entry *fe = NULL;
    char *desc;
    __u64 fileSize, allocSize;
    int status;

    /* find the physical disk offset */
    diskoff = (__u64) (fileid * 512) + v->internal_off;
   
    type = OCFS_FILE_NUM_TO_SYSFILE_TYPE(fileid);
    switch (type)
    {
        case OCFS_VOL_MD_SYSFILE:
            desc = "vol_metadata";
            nodenum = fileid - OCFS_FILE_VOL_META_DATA;
            break;
        case OCFS_VOL_MD_LOG_SYSFILE:
            desc = "vol_metadata_log";
            nodenum = fileid - OCFS_FILE_VOL_LOG_FILE;
            break;
        case OCFS_DIR_SYSFILE:
            desc = "dir_alloc";
            nodenum = fileid - OCFS_FILE_DIR_ALLOC;
            break;
        case OCFS_DIR_BM_SYSFILE:
            desc = "dir_alloc_bitmap";
            nodenum = fileid - OCFS_FILE_DIR_ALLOC_BITMAP;
            break;
        case OCFS_FILE_EXTENT_SYSFILE:
            desc = "file_extent";
            nodenum = fileid - OCFS_FILE_FILE_ALLOC;
            break;
        case OCFS_FILE_EXTENT_BM_SYSFILE:
            desc = "file_extent_bitmap";
            nodenum = fileid - OCFS_FILE_FILE_ALLOC_BITMAP;
            break;
        case OCFS_RECOVER_LOG_SYSFILE:
            desc = "recover_log";
            nodenum = fileid - LOG_FILE_BASE_ID;
            break;
        case OCFS_CLEANUP_LOG_SYSFILE:
            desc = "cleanup_log";
            nodenum = fileid - CLEANUP_FILE_BASE_ID;
            break;
        default:
	    printf("error!!!!!  bad system file number!\n");
            return;
    }

    vcb = get_fake_vcb(fd, v, nodenum);
    printf("%s_%d:\n", desc, nodenum);
    printf("\tfile_number = %d\n", fileid);
    printf("\tdisk_offset = %llu\n", diskoff);
    if (!ocfs_force_get_file_entry(vcb, &fe, diskoff, true))
    {   
        print_file_entry(fe);
        ocfs_release_file_entry(fe);
    }

    status = ocfs_get_system_file_size(vcb, fileid, &fileSize, &allocSize);
    if (status >= 0)
    {
        printf("\tfile_size = %d\n", fileSize);
        printf("\talloc_size = %d\n", allocSize);

        if (type==OCFS_DIR_BM_SYSFILE || type==OCFS_FILE_EXTENT_BM_SYSFILE)
        {
	    size = OCFS_ALIGN(allocSize, 512);
	    buf = (void *) malloc_aligned(size);
            if (allocSize)
            {
	        status = ocfs_read_system_file(vcb, fileid, buf, allocSize, (__u64)0);
	        if (status >= 0)
	        {
		    ocfs_alloc_bm bm;
		    int freebits, firstclear;
    
		    ocfs_initialize_bitmap(&bm, (__u32 *) buf, (__u32) (fileSize * 8));
		    freebits = ocfs_count_bits(&bm);
		    firstclear = ocfs_find_clear_bits(&bm, 1, 0, 0);
    
		    printf("\tTotalBits = %u\n", ((__u32) fileSize * 8));
		    printf("\tFreeBits = %u\n", freebits);
		    printf("\tUsedBits = %u\n", ((__u32) fileSize * 8) - freebits);
		    printf("\tFirstClearBit = %u\n", firstclear);
	        }
            }
        }
        /* two types of log files */
        else if (type==OCFS_CLEANUP_LOG_SYSFILE || type==OCFS_RECOVER_LOG_SYSFILE)
        {
	    ocfs_log_record *lr;
	    ocfs_cleanup_record *cr;
	    int st;
            __u64 logsize;
    
	    if (fileid < CLEANUP_FILE_BASE_ID)
	    {
	        /* LOG_FILE */
	        nodenum = fileid - LOG_FILE_BASE_ID;
	        logsize = 512;
	    }
	    else
	    {
	        /* CLEANUP_FILE */
	        nodenum = fileid - CLEANUP_FILE_BASE_ID;
	        logsize = sizeof(ocfs_cleanup_record);
	        logsize = OCFS_ALIGN(logsize, 512);
	    }
    
	    vcb = get_fake_vcb(fd, v, nodenum);
	    buf = malloc_aligned(logsize);
	    memset(buf, 0, logsize);
            if (allocSize)
            {
	        st = ocfs_read_system_file(vcb, (__u32) fileid, buf, logsize, (__u64)0);
    
	        if (type == OCFS_RECOVER_LOG_SYSFILE)
	        {
	            lr = (ocfs_log_record *) buf;
	            printf("\tlog_id = %llu\n", lr->log_id);
	            printf("\tlog_type = ");
	            print_log_type(lr->log_type);
	            print_record((void *) &(lr->rec), lr->log_type);
	        }
	        else // if (type == OCFS_CLEANUP_LOG_SYSFILE)
	        {
	            cr = (ocfs_cleanup_record *) buf;
	            printf("\tlog_id = %llu\n", cr->log_id);
	            printf("\tlog_type = ");
	            print_log_type(cr->log_type);
	            print_record((void *) &(cr->rec), cr->log_type);
	        }
            }
        }
        else
        {
        }
    }

    if (buf != NULL)
	free_aligned(buf);
    if (vcb != NULL)
	free(vcb);
}


void print_file_data(int fd, ocfs_file_entry * fe)
{
    char buf[101];

    if (fe->local_ext && fe->extents[0].disk_off != 0)
    {
	myseek64(fd, fe->extents[0].disk_off, SEEK_SET);
	read(fd, buf, 100);
	buf[100] = '\0';
	printf("\tFileData = %s\n", buf);
    }
}

void handle_one_file_entry(int fd, ocfs_file_entry *fe, void *buf)
{
    const char *parent = (const char *)buf;

    printf("\tFile%d = %s%s%s\n", filenum++, parent, fe->filename,
        fe->attribs & OCFS_ATTRIB_DIRECTORY ? "/" : "");
    if (fe->attribs & OCFS_ATTRIB_DIRECTORY)
    {
        if (fe->extents[0].disk_off)
        {
            char *newparent = (char *) malloc(strlen(parent) + strlen(fe->filename) + 2);
            sprintf(newparent, "%s%s/", parent, fe->filename);
            walk_dir_nodes(fd, fe->extents[0].disk_off, newparent, NULL);
            free(newparent);
        }
    }
}

/* helper functions for pretty-printing various flags */

void print_node_pointer(__u64 ptr)
{
    if (ptr == INVALID_NODE_POINTER)
	printf("INVALID_NODE_POINTER\n");
    else {
        if (args.twoFourbyte)
	    printf("%u.%u\n", HILO(ptr));
	else
	    printf("%llu\n", ptr);
    }
}

void print___u64_as_bitmap(__u64 x)
{
    int pos = 0;

    while (pos < 32)
    {
	printf("%d", (x & (1 << pos)) ? 1 : 0);
	pos++;
    }
    printf("\n");
}

void print_time(__u64 * sec)
{
    printf("%s", ctime((const time_t *) sec));
}


void print_lock_type(__u8 lock)
{
    if (lock == OCFS_DLM_NO_LOCK)
        printf("OCFS_DLM_NO_LOCK\n");
    else if (lock == OCFS_DLM_EXCLUSIVE_LOCK)
        printf("OCFS_DLM_EXCLUSIVE_LOCK\n");
    else if (lock == OCFS_DLM_SHARED_LOCK)
        printf("OCFS_DLM_SHARED_LOCK\n");
    else if (lock == OCFS_DLM_ENABLE_CACHE_LOCK)
        printf("OCFS_DLM_ENABLE_CACHE_LOCK\n");
    else
        printf("UNKNOWN LOCK TYPE\n");
}

void print_file_attributes(__u32 attribs)
{
    if (attribs & OCFS_ATTRIB_DIRECTORY)
	printf("OCFS_ATTRIB_DIRECTORY ");
    if (attribs & OCFS_ATTRIB_FILE_CDSL)
	printf("OCFS_ATTRIB_FILE_CDSL ");
    if (attribs & OCFS_ATTRIB_CHAR)
	printf("OCFS_ATTRIB_CHAR ");
    if (attribs & OCFS_ATTRIB_BLOCK)
	printf("OCFS_ATTRIB_BLOCK ");
    if (attribs & OCFS_ATTRIB_REG)
	printf("OCFS_ATTRIB_REG ");
    if (attribs & OCFS_ATTRIB_FIFO)
	printf("OCFS_ATTRIB_FIFO ");
    if (attribs & OCFS_ATTRIB_SYMLINK)
	printf("OCFS_ATTRIB_SYMLINK ");
    if (attribs & OCFS_ATTRIB_SOCKET)
	printf("OCFS_ATTRIB_SOCKET ");

    printf("\n");
}

void print_vote_type(int type)
{
    if (type & FLAG_VOTE_NODE)
	printf("FLAG_VOTE_NODE ");
    if (type & FLAG_VOTE_OIN_UPDATED)
	printf("FLAG_VOTE_OIN_UPDATED ");
    if (type & FLAG_VOTE_OIN_ALREADY_INUSE)
	printf("FLAG_VOTE_OIN_ALREADY_INUSE ");
    if (type & FLAG_VOTE_UPDATE_RETRY)
	printf("FLAG_VOTE_UPDATE_RETRY ");
    if (type & FLAG_VOTE_FILE_DEL)
	printf("FLAG_VOTE_FILE_DEL ");

    printf("(0x%08x)", type);

    printf("\n");
}

void print_log_type(int type)
{
    if (type == LOG_TYPE_DISK_ALLOC)
        printf("LOG_TYPE_DISK_ALLOC\n");
    else if (type == LOG_TYPE_DIR_NODE)
        printf("LOG_TYPE_DIR_NODE\n");
    else if (type == LOG_TYPE_RECOVERY)
        printf("LOG_TYPE_RECOVERY\n");
    else if (type == LOG_CLEANUP_LOCK)
        printf("LOG_CLEANUP_LOCK\n");
    else if (type == LOG_TYPE_TRANS_START)
        printf("LOG_TYPE_TRANS_START\n");
    else if (type == LOG_TYPE_TRANS_END)
        printf("LOG_TYPE_TRANS_END\n");
    else if (type == LOG_RELEASE_BDCAST_LOCK)
        printf("LOG_RELEASE_BDCAST_LOCK\n");
    else if (type == LOG_DELETE_ENTRY)
        printf("LOG_DELETE_ENTRY\n");
    else if (type == LOG_MARK_DELETE_ENTRY)
        printf("LOG_MARK_DELETE_ENTRY\n");
    else if (type == LOG_FREE_BITMAP)
        printf("LOG_FREE_BITMAP\n");
    else if (type == LOG_UPDATE_EXTENT)
        printf("LOG_UPDATE_EXTENT\n");
    else if (type == LOG_DELETE_NEW_ENTRY)
        printf("LOG_DELETE_NEW_ENTRY\n");
    else
        printf("unknown log type (%d)\n", type);
}

void print_alloc_log(ocfs_alloc_log * rec)
{
    printf("\tlength = %llu\n", rec->length);
    printf("\tfile_off = %llu\n", rec->file_off);
    printf("\ttype = %u\n", rec->type);
    printf("\tnode_num = %u\n", rec->node_num);
}

void print_dir_log(ocfs_dir_log * rec)
{
    printf("\torig_off = %llu\n", rec->orig_off);
    printf("\tsaved_off = %llu\n", rec->saved_off);
    printf("\tlength = %llu\n", rec->length);
}

void print_recovery_log(ocfs_recovery_log * rec)
{
    printf("\tnode_num = %llu\n", rec->node_num);
}

void print_lock_log(ocfs_lock_log * rec)
{
    int i;

    printf("\tnum_lock_upds = %u\n", rec->num_lock_upds);
    for (i = 0; i < rec->num_lock_upds && i < LOCK_UPDATE_LOG_SIZE; i++)
    {
	printf("\torig_off[%d] = %llu\n", i, rec->lock_upd[i].orig_off);
	printf("\tnew_off[%d] = %llu\n", i, rec->lock_upd[i].new_off);
    }
}
void print_bcast_rel_log(ocfs_bcast_rel_log * rec)
{
    printf("\tlock_id = %llu\n", rec->lock_id);
}

void print_delete_log(ocfs_delete_log * rec)
{
    printf("\tnode_num = %llu\n", rec->node_num);
    printf("\tent_del = %llu\n", rec->ent_del);
    printf("\tparent_dirnode_off = %llu\n", rec->parent_dirnode_off);
    printf("\tflags = %u\n", rec->flags);
}

void print_free_log(ocfs_free_log * rec)
{
    int i;

    printf("\tnum_free_upds = %u\n", rec->num_free_upds);
    for (i = 0; i < rec->num_free_upds && i < FREE_LOG_SIZE; i++)
    {
	printf("\tlength = %llu\n", rec->free_bitmap[i].length);
	printf("\tfile_off = %llu\n", rec->free_bitmap[i].file_off);
	printf("\ttype = %u\n", rec->free_bitmap[i].type);
	printf("\tnode_num = %u\n", rec->free_bitmap[i].node_num);
    }
}
void print_extent_rec(ocfs_free_extent_log * rec)
{
    printf("\tindex = %u\n", rec->index);
    printf("\tdisk_off = %llu\n", rec->disk_off);
}

void print_record(void *rec, int type)
{
    switch (type)
    {
	case LOG_TYPE_DISK_ALLOC:
	    print_alloc_log((ocfs_alloc_log *) rec);
	    break;
	case LOG_TYPE_DIR_NODE:
	    print_dir_log((ocfs_dir_log *) rec);
	    break;
	case LOG_TYPE_RECOVERY:
	    print_recovery_log((ocfs_recovery_log *) rec);
	    break;
	case LOG_CLEANUP_LOCK:
	    print_lock_log((ocfs_lock_log *) rec);
	    break;
	case LOG_RELEASE_BDCAST_LOCK:
	    print_bcast_rel_log((ocfs_bcast_rel_log *) rec);
	    break;
	case LOG_DELETE_ENTRY:
	case LOG_MARK_DELETE_ENTRY:
	    print_delete_log((ocfs_delete_log *) rec);
	    break;
	case LOG_FREE_BITMAP:
	    print_free_log((ocfs_free_log *) rec);
	    break;
	case LOG_UPDATE_EXTENT:
	    print_extent_rec((ocfs_free_extent_log *) rec);
	    break;

	case LOG_TYPE_TRANS_START:
	case LOG_TYPE_TRANS_END:
	default:
	    /* print nothing */
	    break;
    }
}

void print_synch_flags(int flags)
{
    if (flags == 0)
    {
	printf("OCFS_SYNC_FLAG_DELETED\n");
	return;
    }
    if (flags & OCFS_SYNC_FLAG_VALID)
	printf("OCFS_SYNC_FLAG_VALID ");
    if (flags & OCFS_SYNC_FLAG_CHANGE)
	printf("OCFS_SYNC_FLAG_CHANGE ");
    if (flags & OCFS_SYNC_FLAG_MARK_FOR_DELETION)
	printf("OCFS_SYNC_FLAG_MARK_FOR_DELETION ");
    if (flags & OCFS_SYNC_FLAG_NAME_DELETED)
	printf("OCFS_SYNC_FLAG_NAME_DELETED ");

    printf("\n");
}

void print_publish_flags(int type)
{
    if (type & FLAG_FILE_CREATE)
	printf("FLAG_FILE_CREATE ");
    if (type & FLAG_FILE_EXTEND)
	printf("FLAG_FILE_EXTEND ");
    if (type & FLAG_FILE_DELETE)
	printf("FLAG_FILE_DELETE ");
    if (type & FLAG_FILE_RENAME)
	printf("FLAG_FILE_RENAME ");
    if (type & FLAG_FILE_UPDATE)
	printf("FLAG_FILE_UPDATE ");
    if (type & FLAG_FILE_CREATE_DIR)
	printf("FLAG_FILE_CREATE_DIR ");
    if (type & FLAG_FILE_UPDATE_OIN)
	printf("FLAG_FILE_UPDATE_OIN ");
    if (type & FLAG_FILE_RELEASE_MASTER)
	printf("FLAG_FILE_RELEASE_MASTER ");
    if (type & FLAG_CHANGE_MASTER)
	printf("FLAG_CHANGE_MASTER ");
    if (type & FLAG_ADD_OIN_MAP)
	printf("FLAG_ADD_OIN_MAP ");
    if (type & FLAG_DIR)
	printf("FLAG_DIR ");
    if (type & FLAG_DEL_NAME)
	printf("FLAG_DEL_NAME ");
    if (type & FLAG_RESET_VALID)
	printf("FLAG_RESET_VALID ");
    if (type & FLAG_FILE_RELEASE_CACHE)
	printf("FLAG_FILE_RELEASE_CACHE ");
    if (type & FLAG_FILE_CREATE_CDSL)
	printf("FLAG_FILE_CREATE_CDSL ");
    if (type & FLAG_FILE_DELETE_CDSL)
	printf("FLAG_FILE_DELETE_CDSL ");
    if (type & FLAG_FILE_CHANGE_TO_CDSL)
	printf("FLAG_FILE_CHANGE_TO_CDSL ");
    if (type & FLAG_FILE_TRUNCATE)
	printf("FLAG_FILE_TRUNCATE ");

    printf("(0x%08x)", type);

    printf("\n");
}

void print_protection_bits(__u32 prot)
{
    if (prot & S_ISUID)
	printf("S_ISUID ");
    if (prot & S_ISGID)
	printf("S_ISGID ");
    if (prot & S_ISVTX)
	printf("S_ISVTX ");
    if (prot & S_IRUSR)
	printf("S_IRUSR ");
    if (prot & S_IWUSR)
	printf("S_IWUSR ");
    if (prot & S_IXUSR)
	printf("S_IXUSR ");
    if (prot & S_IRGRP)
	printf("S_IRGRP ");
    if (prot & S_IWGRP)
	printf("S_IWGRP ");
    if (prot & S_IXGRP)
	printf("S_IXGRP ");
    if (prot & S_IROTH)
	printf("S_IROTH ");
    if (prot & S_IWOTH)
	printf("S_IWOTH ");
    if (prot & S_IXOTH)
	printf("S_IXOTH ");

    printf("\n");
}


