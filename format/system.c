#include <format.h>
#include <signal.h>
#include <libgen.h>

extern ocfs_alloc_bm global_bm;
extern char *bm_buf;
extern int bm_size;
extern int major_version;
extern int minor_version;


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

int ocfs_update_bm_lock_stats(int file)
{
	int status = 0;
	ocfs_bitmap_lock *bm_lock = NULL;

	bm_lock = (ocfs_bitmap_lock *) MemAlloc(512);
	if (bm_lock == NULL)
		return 0;
	
	memset((char *)bm_lock, 0, OCFS_SECTOR_SIZE);
        bm_lock->used_bits = ocfs_count_bits(&global_bm);
	if (SetSeek(file, OCFS_BITMAP_LOCK_OFFSET))
		if (Write(file, OCFS_SECTOR_SIZE, (void *) bm_lock)) {
			status = 1;
    			fsync(file);
		}
	
	safefree(bm_lock);
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
	ocfs_init_dirnode(dir, volhdr->root_off, root_bit);
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

	status = 1;

bail:
	safefree (dir);
	safefree (fe);
	return status;
}

void ocfs_init_dirnode(ocfs_dir_node *dir, __u64 disk_off, __u32 bit_off)
{
	memset(dir, 0, OCFS_DEFAULT_DIR_NODE_SIZE);
	strcpy (dir->signature, OCFS_DIR_NODE_SIGNATURE);
	dir->num_ents = 254;
	dir->node_disk_off = disk_off;
	dir->alloc_file_off = bit_off;
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
	__u32 orphan_bit;
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
		sprintf(filename, "%s%d", OCFS_ORPHAN_DIR_FILENAME, file_id);
	        fe->attribs = OCFS_ATTRIB_DIRECTORY;
	        fe->alloc_size = OCFS_DEFAULT_DIR_NODE_SIZE;
	        fe->file_size = OCFS_DEFAULT_DIR_NODE_SIZE;
	        fe->next_del = INVALID_DIR_NODE_INDEX;
	        fe->extents[0].disk_off = data;

		orphan_dir = (ocfs_dir_node *) MemAlloc(OCFS_DEFAULT_DIR_NODE_SIZE);
		if (orphan_dir == NULL)
			return 0;
		orphan_bit = (__u32)((data - volhdr->data_start_off) / volhdr->cluster_size);
		ocfs_init_dirnode(orphan_dir, data, orphan_bit);
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
