
/*
 * system.c
 *
 * creates system files and root dir during format
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
 * Authors: Kurt Hackel
 *
 */

#include <format.h>
#include <signal.h>
#include <libgen.h>

extern ocfs_alloc_bm global_bm;
extern char *bm_buf;
extern int bm_size;
extern int major_version;
extern int minor_version;

static int ocfs_create_root_file_entry(int file, ocfs_vol_disk_hdr *volhdr);
static int ocfs_create_bitmap_file_entry(int file, ocfs_vol_disk_hdr *volhdr);

typedef struct _ocfs_file_entry_v2
{
	ocfs_disk_lock disk_lock;       // DISKLOCK
	__u8 signature[8];              // CHAR[8]
	bool local_ext;		        // BOOL
	__u8 next_free_ext;             // NUMBER RANGE(0,OCFS_MAX_FILE_ENTRY_EXTENTS) 
	__s8 next_del;                  // DIRNODEINDEX
	__s32 granularity;	        // NUMBER RANGE(-1,3)
	__u8 filename[OCFS_MAX_FILENAME_LENGTH];  // CHAR[OCFS_MAX_FILENAME_LENGTH]
	__u16 filename_len;               // NUMBER RANGE(0,OCFS_MAX_FILENAME_LENGTH)
	__u64 file_size;                  // NUMBER RANGE(0,ULONG_LONG_MAX)
	__u64 alloc_size;		        // NUMBER RANGE(0,ULONG_LONG_MAX)
	__u64 create_time;                // DATE
	__u64 modify_time;	        // DATE
	ocfs_alloc_ext extents[OCFS_MAX_FILE_ENTRY_EXTENTS];  // EXTENT[OCFS_MAX_FILE_ENTRY_EXTENTS]
	__u64 dir_node_ptr;               // NUMBER RANGE(0,ULONG_LONG_MAX)
	__u64 this_sector;                // NUMBER RANGE(0,ULONG_LONG_MAX)
	__u64 last_ext_ptr;               /* NUMBER RANGE(0,ULONG_LONG_MAX)
					     Points to the last
					     allocated extent */
	__u32 sync_flags;		  // NUMBER RANGE(0,0)
	__u32 link_cnt;                   // NUMBER RANGE(0,UINT_MAX)
	__u32 attribs;                    // ATTRIBS
	__u32 prot_bits;                  // PERMS
	__u32 uid;                        // UID
	__u32 gid;                        // GID
	__u16 dev_major;                  // NUMBER RANGE(0,65535)   
	__u16 dev_minor;                  // NUMBER RANGE(0,65535)
        __u8 fe_reserved1[4];		  // UNUSED
	union {
		__u64 fe_private;
		__u64 child_dirnode;		  // NUMBER RANGE(0,ULONG_LONG_MAX)
		struct _bitinfo {
			__u32 used_bits;
			__u32 total_bits;
		} bitinfo;
	} u;
/* sizeof(fe) = 496 bytes */
} ocfs_file_entry_v2;

int ocfs_init_global_alloc_bm (__u32 num_bits, int file, ocfs_vol_disk_hdr *volhdr)
{
	int ret = 0;

	bm_size = (__u32) OCFS_SECTOR_ALIGN (num_bits / 8);
	bm_buf = MemAlloc(bm_size);
	if (bm_buf == NULL)
		return 0;
	memset(bm_buf, 0, bm_size);
	ocfs_initialize_bitmap (&global_bm, bm_buf, num_bits);

	ret = 1;
	if (volhdr) {
    		if (!SetSeek(file, volhdr->bitmap_off) ||
		    !Read(file, bm_size, bm_buf))
			ret = 0;
	}

	return ret;
}

/* in version 1, this is a ocfs_bitmap_lock.
   in version 2, this is an ocfs_file_entry which uses
   alloc_size for total bits, and file_size for used bits. */
int ocfs_update_bm_lock_stats(int file)
{
	int status = 0;
	void *buf = NULL;

	buf = MemAlloc(OCFS_SECTOR_SIZE);
	if (buf == NULL)
		goto bail;
	
	if (!SetSeek(file, OCFS_BITMAP_LOCK_OFFSET))
		goto bail;

	if (!Read(file, OCFS_SECTOR_SIZE, buf))
		goto bail;

	if (major_version == OCFS_MAJOR_VERSION) {
		ocfs_bitmap_lock *bm_lock = (ocfs_bitmap_lock *) buf;
		memset(buf, 0, OCFS_SECTOR_SIZE);  // why?
        	bm_lock->used_bits = ocfs_count_bits(&global_bm);
	} else if (major_version == OCFS2_MAJOR_VERSION) {
		ocfs_file_entry_v2 *fe = (ocfs_file_entry_v2 *) buf;
		fe->u.bitinfo.used_bits = ocfs_count_bits(&global_bm);
	}

	if (SetSeek(file, OCFS_BITMAP_LOCK_OFFSET)) {
		if (Write(file, OCFS_SECTOR_SIZE, buf)) {
			status = 1;
    			fsync(file);
		}
	}
bail:	
	safefree(buf);
	return status;
}

__u32 ocfs_alloc_from_global_bitmap (__u64 file_size, ocfs_vol_disk_hdr *volhdr)
{
	__u32 startbit = 0, numbits = 0;

	file_size = OCFS_ALIGN(file_size, volhdr->cluster_size);
	numbits = (__u32) (file_size / volhdr->cluster_size);

	startbit = ocfs_find_clear_bits (&global_bm, numbits, 0, 0);
	if (startbit != (__u32)-1)
		ocfs_set_bits (&global_bm, startbit, numbits);
	return startbit;
}


int ocfs_create_root_directory (int file, ocfs_vol_disk_hdr * volhdr)
{
	int status = 0;
	__u64 orphan_off = 0ULL, journal_off = 0ULL;
	__u32 i, j, fileid, bit, root_bit;
       	__u32 max = (major_version == OCFS2_MAJOR_VERSION) ? 
		OCFS_JOURNAL_SYSFILE : OCFS_CLEANUP_LOG_SYSFILE;
	ocfs_dir_node *dir = NULL;
	ocfs_file_entry *fe = NULL;
	__u64 data_off;

	fe = MemAlloc(512);
	if (fe == NULL)
		goto bail;

	dir = MemAlloc(OCFS_DEFAULT_DIR_NODE_SIZE);
	if (dir == NULL)
		goto bail;

	/* reserve system file bits in global */
	bit = ocfs_alloc_from_global_bitmap (ONE_MEGA_BYTE, volhdr);
	if (bit == (__u32) -1)
		goto bail;

	volhdr->internal_off = (bit * volhdr->cluster_size) + volhdr->data_start_off;

	/* reserve root dir bits in global */
	root_bit = ocfs_alloc_from_global_bitmap (OCFS_DEFAULT_DIR_NODE_SIZE, volhdr);
	if (root_bit == (__u32)-1)
		goto bail;
	
	volhdr->root_off = (root_bit * volhdr->cluster_size) + volhdr->data_start_off;
	ocfs_init_dirnode(dir, volhdr->root_off);
	dir->dir_node_flags |= DIR_NODE_FLAG_ROOT;

	if (!SetSeek(file, volhdr->root_off))
		goto bail;
	if (!Write(file, OCFS_DEFAULT_DIR_NODE_SIZE, (void *) dir))
		goto bail;
	fsync(file);

	/* for v2, need to reserve space for orphan dirs: 32 x 128k */
	/* and space for first 4 journals: 4 x 8mb */
	if (major_version == OCFS2_MAJOR_VERSION) {
		bit = ocfs_alloc_from_global_bitmap (32*OCFS_DEFAULT_DIR_NODE_SIZE, volhdr);
		if (bit == (__u32)-1)
			goto bail;
		orphan_off = (bit * volhdr->cluster_size) + volhdr->data_start_off;
		bit = ocfs_alloc_from_global_bitmap (4*OCFS_JOURNAL_DEFAULT_SIZE, volhdr);
		if (bit == (__u32)-1)
			goto bail;
		journal_off = (bit * volhdr->cluster_size) + volhdr->data_start_off;
	}

	/* create all appropriate system file types for this ocfs version */
	/* v2 will create orphan, journal, and local alloc + v1 types */
	for (i = 0; i < OCFS_MAXIMUM_NODES; i++) {
		for (j = OCFS_VOL_MD_SYSFILE; j <= max; j++) {
			fileid = (j*OCFS_MAXIMUM_NODES) + i;
			data_off = 0ULL;

			// only first 4 journals allocated
			// all others must use tuneocfs
			if (j == OCFS_JOURNAL_SYSFILE) {
				if (i < 4)
					data_off = journal_off;
			} else if (j == OCFS_ORPHAN_DIR_SYSFILE)
				data_off = orphan_off;

			if (!ocfs_init_sysfile (file, volhdr, fileid, fe, data_off))
				goto bail;
		}
		orphan_off += OCFS_DEFAULT_DIR_NODE_SIZE;
		journal_off += OCFS_JOURNAL_DEFAULT_SIZE;
	}

	if (major_version == OCFS2_MAJOR_VERSION) {
		if (!ocfs_create_root_file_entry(file, volhdr))
			goto bail;
		if (!ocfs_create_bitmap_file_entry(file, volhdr))
			goto bail;
	}
	
	status = 1;

bail:
	safefree (dir);
	safefree (fe);
	return status;
}

#define OCFS_ROOT_FILE_ENTRY_OFF (3 * OCFS_SECTOR_SIZE)

static int ocfs_create_root_file_entry(int file, ocfs_vol_disk_hdr *volhdr)
{
	int ret = 0;
	ocfs_file_entry_v2 *fe;

	fe = MemAlloc(OCFS_SECTOR_SIZE);
	if (fe == NULL)
		goto bail;
	
	memset(fe, 0, OCFS_SECTOR_SIZE);
	strcpy(&fe->filename[0], "root");
	fe->filename_len = strlen(fe->filename);
	fe->local_ext = true;
	fe->granularity = -1;
	strcpy (fe->signature, OCFS_FILE_ENTRY_SIGNATURE);
	SET_VALID_BIT (fe->sync_flags);
	fe->sync_flags &= ~(OCFS_SYNC_FLAG_CHANGE);
	fe->last_ext_ptr = 0;
        fe->next_del = INVALID_DIR_NODE_INDEX;
	fe->this_sector = OCFS_ROOT_FILE_ENTRY_OFF;
	fe->alloc_size = 0ULL;
	fe->file_size = 0ULL;
	fe->next_free_ext = 0;
	fe->uid = volhdr->uid;
	fe->gid = volhdr->gid;
	fe->prot_bits = volhdr->prot_bits;
        fe->attribs = OCFS_ATTRIB_DIRECTORY;
	fe->u.child_dirnode = volhdr->root_off;

	if (!SetSeek(file, OCFS_ROOT_FILE_ENTRY_OFF))
		goto bail;
	if (!Write(file, OCFS_SECTOR_SIZE, (void *) fe))
		goto bail;
	fsync(file);
	ret = 1;
bail:
	safefree (fe);
	return ret;
}

static int ocfs_create_bitmap_file_entry(int file, ocfs_vol_disk_hdr *volhdr)
{
	int ret = 0;
	ocfs_file_entry_v2 *fe;

	fe = MemAlloc(OCFS_SECTOR_SIZE);
	if (fe == NULL)
		goto bail;
	
	memset(fe, 0, OCFS_SECTOR_SIZE);
	strcpy(&fe->filename[0], "global-bitmap");
	fe->filename_len = strlen(fe->filename);
	fe->local_ext = true;
	fe->granularity = -1;
	strcpy (fe->signature, OCFS_FILE_ENTRY_SIGNATURE);
	SET_VALID_BIT (fe->sync_flags);
	fe->sync_flags &= ~(OCFS_SYNC_FLAG_CHANGE);
	fe->last_ext_ptr = 0;
        fe->next_del = INVALID_DIR_NODE_INDEX;
	fe->this_sector = OCFS_BITMAP_LOCK_OFFSET;
	fe->alloc_size = OCFS_MAX_BITMAP_SIZE;		// max possible bytes in bitmap
	fe->file_size = (volhdr->num_clusters + 7) / 8;	// valid bytes in actual bitmap
	fe->next_free_ext = 0;
	fe->uid = volhdr->uid;
	fe->gid = volhdr->gid;
	fe->prot_bits = volhdr->prot_bits;
	fe->u.bitinfo.used_bits = 0;			// used bits in bitmap
	fe->u.bitinfo.total_bits = volhdr->num_clusters;// total valid bits in bitmap

       	fe->extents[0].disk_off = volhdr->bitmap_off;
       	fe->extents[0].file_off = 0ULL;
       	fe->extents[0].num_bytes = OCFS_MAX_BITMAP_SIZE;
	fe->next_free_ext = 1;
	
	if (!SetSeek(file, OCFS_BITMAP_LOCK_OFFSET))
		goto bail;
	if (!Write(file, OCFS_SECTOR_SIZE, (void *) fe))
		goto bail;
	fsync(file);
	ret = 1;
bail:
	safefree (fe);
	return ret;
}


void ocfs_init_dirnode(ocfs_dir_node *dir, __u64 disk_off)
{
	memset(dir, 0, OCFS_DEFAULT_DIR_NODE_SIZE);
	strcpy (dir->signature, OCFS_DIR_NODE_SIGNATURE);
	dir->num_ents = 254;
	dir->node_disk_off = disk_off;
	dir->alloc_file_off = disk_off;
	dir->alloc_node = OCFS_INVALID_NODE_NUM;
	dir->free_node_ptr = INVALID_NODE_POINTER;
	dir->next_node_ptr = INVALID_NODE_POINTER;
	dir->indx_node_ptr = INVALID_NODE_POINTER;
	dir->next_del_ent_node = INVALID_NODE_POINTER;
	dir->head_del_ent_node = INVALID_NODE_POINTER;
	dir->first_del = INVALID_DIR_NODE_INDEX;
	dir->index_dirty = 0;
	DISK_LOCK_CURRENT_MASTER (dir) = OCFS_INVALID_NODE_NUM;
}

int ocfs_init_sysfile (int file, ocfs_vol_disk_hdr *volhdr, __u32 file_id, 
			      ocfs_file_entry *fe, __u64 data)
{
	int status = 0;
	char *filename;
	ocfs_local_alloc *alloc;
	__u64 off;
//	__u32 orphan_bit;
	ocfs_dir_node *orphan_dir = NULL;
	__u8 next_free_ext = 0;

	memset (fe, 0, 512);
	filename = &(fe->filename[0]);	
	off = (file_id * OCFS_SECTOR_SIZE) + volhdr->internal_off;

	if ((file_id >= OCFS_FILE_DIR_ALLOC) &&
	    (file_id < (OCFS_FILE_DIR_ALLOC + 32))) {
		sprintf (filename, "%s%d", OCFS_DIR_FILENAME, file_id);
	} else if ((file_id >= OCFS_FILE_DIR_ALLOC_BITMAP) &&
		   (file_id < (OCFS_FILE_DIR_ALLOC_BITMAP + 32))) {
		sprintf (filename, "%s%d", OCFS_DIR_BITMAP_FILENAME, file_id);
	} else if ((file_id >= OCFS_FILE_FILE_ALLOC) &&
		   (file_id < (OCFS_FILE_FILE_ALLOC + 32))) {
		sprintf (filename, "%s%d", OCFS_FILE_EXTENT_FILENAME, file_id);
	} else if ((file_id >= OCFS_FILE_FILE_ALLOC_BITMAP) &&
		   (file_id < (OCFS_FILE_FILE_ALLOC_BITMAP + 32))) {
		sprintf (filename, "%s%d", OCFS_FILE_EXTENT_BITMAP_FILENAME,
			 file_id);
	} else if ((file_id >= LOG_FILE_BASE_ID)
		   && (file_id < (LOG_FILE_BASE_ID + 32))) {
		sprintf (filename, "%s%d", OCFS_RECOVER_LOG_FILENAME, file_id);
	} else if ((file_id >= CLEANUP_FILE_BASE_ID) &&
		   (file_id < (CLEANUP_FILE_BASE_ID + 32))) {
		sprintf (filename, "%s%d", OCFS_CLEANUP_LOG_FILENAME, file_id);
	} else if ((file_id >= OCFS_FILE_VOL_META_DATA) &&
		   (file_id < (OCFS_FILE_VOL_META_DATA + 32))) {
		sprintf (filename, "%s", "VolMetaDataFile");
	} else if ((file_id >= OCFS_FILE_VOL_LOG_FILE) &&
		   (file_id < (OCFS_FILE_VOL_LOG_FILE + 32))) {
		sprintf (filename, "%s", "VolMetaDataLogFile");
	} else if ((file_id >= OCFS_VOL_BITMAP_FILE) &&
		   (file_id < (OCFS_VOL_BITMAP_FILE + 32))) {
		// markf likes to be special ;-)
		alloc = (ocfs_local_alloc *) fe;
	        strcpy (alloc->signature, OCFS_LOCAL_ALLOC_SIGNATURE);
	        alloc->this_sector = off;
	        alloc->node_num = file_id - OCFS_VOL_BITMAP_FILE;
		goto do_write;
	} else if ((file_id >= OCFS_ORPHAN_DIR) &&
		   (file_id < (OCFS_ORPHAN_DIR + 32))) {
		// hackery
		ocfs_file_entry_v2 *fev2 = (ocfs_file_entry_v2 *)fe;

		sprintf(filename, "%s%d", OCFS_ORPHAN_DIR_FILENAME, file_id);
	        fev2->attribs = OCFS_ATTRIB_DIRECTORY;
	        fev2->next_del = INVALID_DIR_NODE_INDEX;
	        fev2->u.child_dirnode = data;

		orphan_dir = (ocfs_dir_node *) MemAlloc(OCFS_DEFAULT_DIR_NODE_SIZE);
		if (orphan_dir == NULL)
			return 0;
//		orphan_bit = (__u32)((data - volhdr->data_start_off) / volhdr->cluster_size);
		ocfs_init_dirnode(orphan_dir, data);
		DISK_LOCK_CURRENT_MASTER (orphan_dir) = file_id - OCFS_ORPHAN_DIR;
		DISK_LOCK_FILE_LOCK (orphan_dir) = OCFS_DLM_ENABLE_CACHE_LOCK;
		orphan_dir->dir_node_flags |= DIR_NODE_FLAG_ORPHAN;
		
		if (SetSeek(file, data))
	                if (Write(file, OCFS_DEFAULT_DIR_NODE_SIZE, (void *) orphan_dir)) {
				status = 1;
				fsync(file);
			}
		safefree(orphan_dir);
		if (!status)
			return status;
	} else if ((file_id >= OCFS_JOURNAL_FILE) && 
		   (file_id < (OCFS_JOURNAL_FILE + 32))) {
		sprintf(filename, "%s%d", OCFS_JOURNAL_FILENAME, file_id);
		
		// first 4 will have 8mb, rest will have nothing yet
		if (data) {
			fe->alloc_size = OCFS_JOURNAL_DEFAULT_SIZE;
			fe->file_size = OCFS_JOURNAL_DEFAULT_SIZE;
	        	fe->extents[0].disk_off = data;
	        	fe->extents[0].file_off = 0ULL;
	        	fe->extents[0].num_bytes = OCFS_JOURNAL_DEFAULT_SIZE;
			next_free_ext = 1;
			if (!ocfs_replacement_journal_create(file, data))
				return 0;
		}
	} else {
		fprintf(stderr, "eeeeek! fileid=%d\n", file_id);
		exit(1);
	}

	fe->local_ext = true;
	fe->granularity = -1;
	strcpy (fe->signature, OCFS_FILE_ENTRY_SIGNATURE);
	SET_VALID_BIT (fe->sync_flags);
	fe->sync_flags &= ~(OCFS_SYNC_FLAG_CHANGE);
	fe->last_ext_ptr = 0;
	fe->this_sector = off;
	fe->next_free_ext = next_free_ext;

do_write:
	if (SetSeek(file, off))
		if (Write(file, OCFS_SECTOR_SIZE, (void *) fe)) {
			status = 1;
			fsync(file);
		}
	return status;
}
