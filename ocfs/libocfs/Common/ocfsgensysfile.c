/*
 * ocfsgensysfile.c
 *
 * Initialize, read, write, etc. system files.
 *
 * Copyright (C) 2002, 2003 Oracle.  All rights reserved.
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
 * Authors: Neeraj Goyal, Suchit Kaura, Kurt Hackel, Sunil Mushran,
 *          Manish Singh, Wim Coekaerts
 */

#ifdef __KERNEL__
#include <ocfs.h>
#else
#include <libocfs.h>
#endif

/* Tracing */
#define OCFS_DEBUG_CONTEXT    OCFS_DEBUG_CONTEXT_MISC

/*
 * ocfs_init_system_file()
 *
 */
int ocfs_init_system_file (ocfs_super * osb, __u32 file_id, char *filename,
			   ocfs_file_entry *fe)
{
	int status = 0;
	__u64 offset = 0;
	__u32 length = 0;

	LOG_ENTRY_ARGS ("(file_id = %u)\n", file_id);

	memset (filename, 0, sizeof (OCFS_MAX_FILENAME_LENGTH));

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
#ifdef LOCAL_ALLOC
	} else if ((file_id >= OCFS_VOL_BITMAP_FILE) && 
	      	   (file_id < (OCFS_FILE_VOL_LOG_FILE + 64))) {
		sprintf (filename, "%s", "VolBitMapFile");
#endif
	} else {
		sprintf (filename, "%s", "UKNOWNSysFile");
	}

	offset = (file_id * osb->sect_size) + osb->vol_layout.root_int_off;

	length = osb->sect_size;

	memset (fe, 0, sizeof (ocfs_file_entry));
	/*  Set the Flag to use the Local Extents */
	fe->local_ext = true;
	fe->granularity = -1;

	strcpy (fe->signature, OCFS_FILE_ENTRY_SIGNATURE);
	fe->next_free_ext = 0;

	/*  Add a file Name  */
	memcpy (fe->filename, filename, strlen (filename));
	(fe->filename)[strlen (filename)] = '\0';

	/*  Set the Valid bit here  */
	SET_VALID_BIT (fe->sync_flags);
	fe->sync_flags &= ~(OCFS_SYNC_FLAG_CHANGE);
	fe->this_sector = offset;
	fe->last_ext_ptr = 0;

	status = ocfs_write_disk (osb, (void *) fe, osb->sect_size, offset);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

      leave:
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_init_system_file */

/*
 * ocfs_read_system_file()
 *
 */
int ocfs_read_system_file (ocfs_super * osb,
		__u32 FileId, void *Buffer, __u64 Length, __u64 Offset)
{
	int status = 0;
	ocfs_file_entry *fe = NULL;
	void *extentBuffer = NULL;
	__u32 numExts = 0, i;
	ocfs_io_runs *IoRuns = NULL;
	__u64 templength;
	__u32 *tempBuffer;
	__u64 tempOffset = 0;
	bool bWriteThru = false;

	LOG_ENTRY_ARGS ("(FileId = %u)\n", FileId);

	if ((FileId == (__u32) (OCFS_FILE_VOL_LOG_FILE + osb->node_num)) ||
	    (FileId == (__u32) (OCFS_FILE_VOL_META_DATA + osb->node_num))) {
		bWriteThru = true;
	}

	/*  Read the File Entry corresponding to File Id */
	status = ocfs_force_get_file_entry (osb, &fe,
					(FileId * osb->sect_size) +
					osb->vol_layout.root_int_off,
					bWriteThru);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	if (!IS_VALID_FILE_ENTRY (fe)) {
		LOG_ERROR_STATUS(status = -EINVAL);
		goto leave;
	}

	status = ocfs_find_extents_of_system_file (osb, Offset, Length,
					  fe, &extentBuffer, &numExts);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	OCFS_ASSERT (extentBuffer);

	IoRuns = (ocfs_io_runs *) extentBuffer;
	tempOffset = 0;
	templength = 0;
	tempBuffer = Buffer;

	for (i = 0; i < numExts; i++) {
		tempBuffer += templength;
		/*  ?? need to align both the length and buffer and also */
		/* offset ( atleast the starting one) */
		tempOffset = IoRuns[i].disk_off;
		templength = IoRuns[i].byte_cnt;

		if (bWriteThru) {
			status = ocfs_read_disk (osb, (void *) tempBuffer,
						 (__u32) templength, tempOffset);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto leave;
			}
		} else {
			status = ocfs_read_metadata (osb, (void *) tempBuffer,
						(__u32) templength, tempOffset);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto leave;
			}
		}
	}

      leave:
	ocfs_release_file_entry (fe);
#ifdef SYSFILE_EXTMAP_FIX
	if (extentBuffer)
		vfree(extentBuffer);
#else
	ocfs_safefree (extentBuffer);
#endif

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_read_system_file */


/*
 * ocfs_write_system_file()
 *
 */
int ocfs_write_system_file (ocfs_super * osb,
		 __u32 FileId, void *Buffer, __u64 Length, __u64 Offset)
{
	int status = 0;
	ocfs_file_entry *fe = NULL;
	void *extentBuffer = NULL;
	__u32 numExts = 0, i;
	ocfs_io_runs *IoRuns = NULL;
	__u64 templength;
	__u32 *tempBuffer;
	__u64 tempOffset = 0;
	bool bWriteThru = false;

	LOG_ENTRY_ARGS ("(FileId = %u)\n", FileId);

	if ((FileId == (OCFS_FILE_VOL_LOG_FILE + osb->node_num)) ||
	    (FileId == (OCFS_FILE_VOL_META_DATA + osb->node_num))) {
		bWriteThru = true;
	}

	/*  Read the File Entry corresponding to File Id */
	status = ocfs_force_get_file_entry (osb, &fe,
					(FileId * osb->sect_size) +
					osb->vol_layout.root_int_off,
					bWriteThru);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}
	if (!IS_VALID_FILE_ENTRY (fe)) {
		LOG_ERROR_STATUS(status = -EINVAL);
		goto leave;
	}

	status = ocfs_find_extents_of_system_file (osb, Offset, Length,
					  fe, &extentBuffer, &numExts);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	OCFS_ASSERT (extentBuffer);

	IoRuns = (ocfs_io_runs *) extentBuffer;
	tempOffset = 0;
	templength = 0;
	tempBuffer = Buffer;

	for (i = 0; i < numExts; i++) {
		tempBuffer += templength;
		/*  ?? need to align both the length and buffer and also */
		/* offset ( atleast the starting one) */
		tempOffset = IoRuns[i].disk_off;
		templength = IoRuns[i].byte_cnt;
		/*  ?? Also need to read the data from the start of sector */
		/* and then munge it . */
		if (bWriteThru) {
			status =
			    ocfs_write_force_disk (osb, (void *) tempBuffer,
						(__u32) templength, tempOffset);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto leave;
			}
		} else {
			status =
			    ocfs_write_metadata (osb, (void *) tempBuffer,
					       (__u32) templength, tempOffset);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto leave;
			}
		}
	}

      leave:
	ocfs_release_file_entry (fe);
#ifdef SYSFILE_EXTMAP_FIX
       if (extentBuffer)
              vfree(extentBuffer);
#else
	ocfs_safefree (extentBuffer);
#endif

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_write_system_file */

/*
 * ocfs_file_to_disk_off()
 *
 */
__u64 ocfs_file_to_disk_off (ocfs_super * osb, __u32 FileId, __u64 Offset)
{
	int status = 0;
	__u64 StartOffset = 0;
	void *Buffer = NULL;
	ocfs_file_entry *fe = NULL;
	ocfs_io_runs *IoRuns;
	__u32 NumExts = 0;
	bool bWriteThru = false;

	LOG_ENTRY_ARGS ("(FileId = %u)\n", FileId);

	if ((FileId == (OCFS_FILE_VOL_LOG_FILE + osb->node_num)) ||
	    (FileId == (OCFS_FILE_VOL_META_DATA + osb->node_num))) {
		bWriteThru = true;
	}

	/*  Read the File Entry corresponding to File Id */
	status = ocfs_force_get_file_entry (osb, &fe,
					(FileId * osb->sect_size) +
					osb->vol_layout.root_int_off,
					bWriteThru);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}
	if (!IS_VALID_FILE_ENTRY (fe)) {
		LOG_ERROR_STATUS(status = -EINVAL);
		goto leave;
	}

	status = ocfs_find_extents_of_system_file (osb, Offset, osb->sect_size,
					  fe, &Buffer, &NumExts);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	OCFS_ASSERT (Buffer);

	IoRuns = (ocfs_io_runs *) Buffer;
	/*  Return the disk offset of first run . */
	StartOffset = (IoRuns[0].disk_off);

      leave:
	ocfs_release_file_entry (fe);
#ifdef SYSFILE_EXTMAP_FIX
	if (Buffer)
		vfree(Buffer);
#else
	ocfs_safefree (Buffer);
#endif

	LOG_EXIT_ARGS ("%u.%u", HI (StartOffset), LO (StartOffset));
	return StartOffset;
}				/* ocfs_file_to_disk_off */


/*
 * ocfs_get_system_file_size()
 *
 */
int ocfs_get_system_file_size (ocfs_super * osb, __u32 FileId, __u64 * Length, __u64 * AllocSize)
{
	int status = 0;
	ocfs_file_entry *fe = NULL;
	bool bWriteThru = false;
	__u64 offset;

	LOG_ENTRY_ARGS ("(FileId = %u)\n", FileId);

	if ((FileId == (OCFS_FILE_VOL_LOG_FILE + osb->node_num)) ||
	    (FileId == (OCFS_FILE_VOL_META_DATA + osb->node_num))) {
		bWriteThru = true;
	}
	*AllocSize = *Length = 0;

	offset = (FileId * osb->sect_size) + osb->vol_layout.root_int_off;

	status = ocfs_force_get_file_entry (osb, &fe, offset, bWriteThru);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	if (!IS_VALID_FILE_ENTRY (fe)) {
		LOG_ERROR_ARGS("offset=%u.%u", HI(offset), LO(offset));
		status = -EINVAL;
		goto leave;
	}

	*Length = (__u64) (fe->file_size);
	*AllocSize = (__u64) (fe->alloc_size);

      leave:
	ocfs_release_file_entry (fe);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_get_system_file_size */

/*
 * ocfs_extend_system_file()
 *
 */
int ocfs_extend_system_file (ocfs_super * osb, __u32 FileId, __u64 FileSize, ocfs_file_entry *fe)
{
	int status = 0;
	__u64 actualDiskOffset = 0, actualLength = 0;
	bool bWriteThru = false;
	bool local_fe = false;

	LOG_ENTRY_ARGS ("(FileId = %u, Size = %u.%u)\n", FileId, HI (FileSize),
			LO (FileSize));

	if ((FileId == (OCFS_FILE_VOL_LOG_FILE + osb->node_num)) ||
	    (FileId == (OCFS_FILE_VOL_META_DATA + osb->node_num))) {
		bWriteThru = true;
	}
	OCFS_ASSERT (osb);

	if (!fe) {
		local_fe = true;
		status = ocfs_force_get_file_entry (osb, &fe,
						(FileId * osb->sect_size) +
						osb->vol_layout.root_int_off,
						bWriteThru);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto leave;
		}
	}

	if (!IS_VALID_FILE_ENTRY (fe)) {
		LOG_ERROR_STATUS (status = -EINVAL);
		goto leave;
	}

	if (FileSize <= fe->alloc_size) {
		fe->file_size = FileSize;
	} else {
		/*  We need to allocate from bitmap */
		__u64 numClusterAlloc = 0, BitmapOffset = 0;

		status =
		    ocfs_find_contiguous_space_from_bitmap (osb,
						   FileSize - fe->alloc_size,
						   &BitmapOffset,
						   &numClusterAlloc, true);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto leave;
		}

		actualDiskOffset =
		    (BitmapOffset * osb->vol_layout.cluster_size) +
		    osb->vol_layout.data_start_off;
		actualLength =
		    (__u64) (numClusterAlloc * osb->vol_layout.cluster_size);

		status = ocfs_allocate_extent (osb, NULL, fe, actualDiskOffset,
					     actualLength);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto leave;
		}

		fe->alloc_size += actualLength;
		fe->file_size = FileSize;
	}

	if (!bWriteThru) {
		DISK_LOCK_CURRENT_MASTER (fe) = osb->node_num;
		DISK_LOCK_FILE_LOCK (fe) = OCFS_DLM_ENABLE_CACHE_LOCK;
	}

	status = ocfs_force_put_file_entry (osb, fe, bWriteThru);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

      leave:
	if (local_fe)
		ocfs_release_file_entry (fe);
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_extend_system_file */


/*
 * ocfs_find_extents_of_system_file()
 *
 * Searches for the extents in the file entry passed starting from
 * file offset up to the length specified.
 */
int ocfs_find_extents_of_system_file (ocfs_super * osb,
			 __u64 file_off,
			 __u64 Length,
			 ocfs_file_entry * fe, void **Buffer, __u32 * NumEntries)
{
	int status = -EFAIL;
	__u32 allocSize = 0, size;
	__u8 *buffer = NULL;
	__u32 k = 0, j;
	__u32 Runs, Runoffset;
	__u32 length;
	ocfs_extent_group *pOcfsExtent = NULL, *pOcfsExtentHeader = NULL;
	ocfs_io_runs *IoRuns;
	__u64 newOffset = 0, searchVbo, remainingLength = 0;

	LOG_ENTRY ();

	OCFS_ASSERT (osb);

	if (!IS_VALID_FILE_ENTRY (fe)) {
		LOG_ERROR_STATUS(status = -EFAIL);
		goto leave;
	}
#ifdef SYSFILE_EXTMAP_FIX
      if (fe->local_ext)
      {
              size = OCFS_MAX_FILE_ENTRY_EXTENTS * sizeof (ocfs_io_runs);
      }
      else
      {
              int pow = fe->granularity + 1;
              /* extent tree looks like
               *             fe[0]        fe[1]    fe[2]
               *        hdr[0]...hdr[17]  .....
               * dat[0]..dat[17]
               *
               * granularity of fe is tree height
               * so max runs (total of all leaves) is
               * 3 x 18 ^ (granularity+1)
               * (OCFS_MAX_DATA_EXTENTS = 18)
               *
               * g=0: 1296 bytes
               * g=1: 23328 bytes
               * g=2: 419904 bytes!
               */
              size = 3;
              while (pow)
              {
                      size *= OCFS_MAX_DATA_EXTENTS;
                      pow--;
              }
              size *= sizeof (ocfs_io_runs);
      }
      size = OCFS_ALIGN (size, osb->sect_size);
      IoRuns = vmalloc(size);
#else
	/* ??? need to allocate accoordingly ...as number of runs can be more */
	size = (OCFS_MAX_DATA_EXTENTS * sizeof (ocfs_io_runs));
	size = OCFS_ALIGN (size, osb->sect_size);

	IoRuns = ocfs_malloc (size);
#endif
	if (IoRuns == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto leave;
	}

	memset (IoRuns, 0, size);

	remainingLength = Length;
	Runs = 1;
	Runoffset = 0;
	newOffset = file_off;

	if (fe->local_ext) {
		for (j = 0; j < OCFS_MAX_FILE_ENTRY_EXTENTS; j++) {
			if ((fe->extents[j].file_off +
			     fe->extents[j].num_bytes) > newOffset) {
				IoRuns[Runoffset].disk_off =
				    fe->extents[j].disk_off +
				    (newOffset - fe->extents[j].file_off);
				IoRuns[Runoffset].byte_cnt =
				    (__u32) ((fe->extents[j].file_off +
					    fe->extents[j].num_bytes) -
					   newOffset);
				if (IoRuns[Runoffset].byte_cnt >=
				    remainingLength) {
					IoRuns[Runoffset].byte_cnt =
					    (__u32) remainingLength;
					status = 0;
					break;
				} else {
					newOffset += IoRuns[Runoffset].byte_cnt;
					remainingLength -=
					    IoRuns[Runoffset].byte_cnt;
					Runs++;
					Runoffset++;
				}
			}
		}

		*NumEntries = Runs;
		*Buffer = IoRuns;
		goto leave;
	} else {
		/* Extents are branched and we are no longer using Local Extents */
		/* for this File Entry. */

		allocSize = (NUM_SECTORS_IN_LEAF_NODE + fe->granularity) *
		    OCFS_SECTOR_SIZE;

		length = (__u32) OCFS_ALIGN (allocSize, osb->sect_size);

		buffer = ocfs_malloc (length);
		if (buffer == NULL) {
			LOG_ERROR_STATUS (status = -ENOMEM);
			goto leave;
		}

		while (1) {
			/* Keep going downwards looking for the Entry, till we hit */
			/* the last Data entry */
			for (k = 0; k < OCFS_MAX_FILE_ENTRY_EXTENTS; k++) {
				if ((__s64) (fe->extents[k].file_off +
					   fe->extents[k].num_bytes) >
				    newOffset) {
					break;
				}
			}

			if (k == OCFS_MAX_FILE_ENTRY_EXTENTS) {
				LOG_ERROR_STR ("data extents maxed");
			}

			memset (buffer, 0, length);

			if (fe->extents[k].disk_off == 0) {
				LOG_ERROR_STR ("disk_off=0");
			}

			status =
			    ocfs_read_metadata (osb, (void *) buffer, allocSize,
					      fe->extents[k].disk_off);

			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto leave;
			}

			pOcfsExtent = (ocfs_extent_group *) buffer;
			while (pOcfsExtent->type != OCFS_EXTENT_DATA) {
				__u64 diskoffset;

				if (!IS_VALID_EXTENT_HEADER (pOcfsExtent)) {
					LOG_ERROR_STATUS(status = -EFAIL);
					goto leave;
				}

				OCFS_GET_EXTENT ((__s64) newOffset, pOcfsExtent,
						 k);
				if (k == OCFS_MAX_DATA_EXTENTS) {
					LOG_ERROR_STR ("data extents maxed");
				}

				if (pOcfsExtent->extents[k].disk_off == 0) {
					LOG_ERROR_STR ("disk_off=0");
				}

				diskoffset = pOcfsExtent->extents[k].disk_off;

				memset (buffer, 0, length);

				status =
				    ocfs_read_metadata (osb, (void *) buffer,
						      allocSize,
						      diskoffset); 
				if (status < 0) {
					LOG_ERROR_STATUS (status);
					goto leave;
				}
				pOcfsExtent = (ocfs_extent_group *) buffer;
			}
			pOcfsExtentHeader = (ocfs_extent_group *) buffer;

			searchVbo = newOffset;

			OCFS_ASSERT (pOcfsExtentHeader->type ==
				     OCFS_EXTENT_DATA);

			if (!IS_VALID_EXTENT_DATA (pOcfsExtentHeader)) {
				LOG_ERROR_STATUS(status = -EFAIL);
				goto leave;
			}

			{
				for (j = 0; j < OCFS_MAX_DATA_EXTENTS; j++) {
					if ((pOcfsExtent->extents[j].file_off +
					     pOcfsExtent->extents[j].
					     num_bytes) > newOffset) {
						IoRuns[Runoffset].disk_off =
						    pOcfsExtent->extents[j].
						    disk_off + (newOffset -
								pOcfsExtent->
								extents[j].
								file_off);
						IoRuns[Runoffset].byte_cnt =
						    (__u32) ((pOcfsExtent->
							    extents[j].
							    file_off +
							    pOcfsExtent->
							    extents[j].
							    num_bytes) -
							   newOffset);

						if (IoRuns[Runoffset].
						    byte_cnt >=
						    remainingLength) {
							IoRuns[Runoffset].
							    byte_cnt = (__u32)
							    remainingLength;
							status = 0;
							break;
						} else {
							newOffset +=
							    IoRuns[Runoffset].
							    byte_cnt;
							remainingLength -=
							    IoRuns[Runoffset].
							    byte_cnt;
							Runs++;
							Runoffset++;
							if (Runs >=
							    OCFS_MAX_DATA_EXTENTS)
							{
								LOG_ERROR_ARGS ("Runs=%d", Runs);
							}
						}
					}
				}

				if (j == OCFS_MAX_DATA_EXTENTS) {
					continue;
				} else {
					*NumEntries = Runs;
					*Buffer = IoRuns;
					goto leave;
				}
			}
		}
	}

      leave:
	/* Don't free the IoRuns Memory here */
	ocfs_safefree (buffer);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_find_extents_of_system_file */

/*
 * ocfs_free_file_extents()
 *
 */
int ocfs_free_file_extents (ocfs_super * osb, ocfs_file_entry * fe, __s32 LogNodeNum)
{
	int status = 0;
	__u32 i, size, numUpdt = 0;
	__u32 numBitsAllocated = 0, bitmapOffset = 0;
	ocfs_cleanup_record *pCleanupLogRec = NULL;
	ocfs_extent_group *PAllocExtent = NULL;

	LOG_ENTRY ();

	size = sizeof (ocfs_cleanup_record);
	size = (__u32) OCFS_ALIGN (size, OCFS_PAGE_SIZE);

	pCleanupLogRec = ocfs_malloc (size);
	if (pCleanupLogRec == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto leave;
	}

	pCleanupLogRec->rec.free.num_free_upds = 0;
	pCleanupLogRec->log_id = osb->curr_trans_id;
	pCleanupLogRec->log_type = LOG_FREE_BITMAP;

	if (fe->local_ext) {
		for (i = 0; i < fe->next_free_ext; i++) {
			numBitsAllocated = (__u32) (fe->extents[i].num_bytes /
						  (osb->vol_layout.
						   cluster_size));

			bitmapOffset =
			    (__u32) ((fe->extents[i].disk_off -
				    osb->vol_layout.data_start_off) /
				   (osb->vol_layout.cluster_size));

			numUpdt = pCleanupLogRec->rec.free.num_free_upds;

			pCleanupLogRec->rec.free.free_bitmap[numUpdt].length =
			    numBitsAllocated;
			pCleanupLogRec->rec.free.free_bitmap[numUpdt].file_off =
			    bitmapOffset;
			pCleanupLogRec->rec.free.free_bitmap[numUpdt].type =
			    DISK_ALLOC_VOLUME;
			pCleanupLogRec->rec.free.free_bitmap[numUpdt].node_num =
			    -1;

			(pCleanupLogRec->rec.free.num_free_upds)++;
		}
	} else {
		size = OCFS_ALIGN (sizeof (ocfs_extent_group), osb->sect_size);

		PAllocExtent = ocfs_malloc (size);
		if (PAllocExtent == NULL) {
			LOG_ERROR_STATUS (status = -ENOMEM);
			goto leave;
		}

		memset (PAllocExtent, 0, size);

		for (i = 0; i < fe->next_free_ext; i++) {
			status = ocfs_read_extent (osb, PAllocExtent,
						   fe->extents[i].disk_off,
						   (fe->granularity ?
						    EXTENT_HEADER : EXTENT_DATA));
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto leave;
			}

			status = ocfs_kill_this_tree(osb, PAllocExtent, pCleanupLogRec);
			if (status < 0) {
				LOG_ERROR_STATUS(status);
				goto leave;
			}
		}
	}

	/* Write the log */
	if (pCleanupLogRec->rec.free.num_free_upds > 0) {
		status =
		    ocfs_write_node_log (osb, (ocfs_log_record *) pCleanupLogRec,
				      LogNodeNum, LOG_CLEANUP);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto leave;
		}
	}

      leave:
	ocfs_safefree (PAllocExtent);
	ocfs_safefree (pCleanupLogRec);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_free_file_extents */

/*
 * ocfs_write_map_file()
 *
 */
int ocfs_write_map_file (ocfs_super * osb)
{
	int status;
	__u32 RunsInMap;
	__u32 MapIndex;
	__u32 length;
	ocfs_offset_map *pMapBuffer = NULL;
	__u64 fileSize;
	__u64 allocSize;
	__u64 neededSize;
	__s64 foundVolOffset;
	__s64 foundlogOffset;
	__u32 numRec;
	ocfs_file_entry *fe = NULL;

	LOG_ENTRY ();

	RunsInMap = ocfs_extent_map_get_count (&osb->trans_map);

	LOG_TRACE_ARGS ("NumRuns in trans_map=%u\n", RunsInMap);

	if (RunsInMap == 0) {
		status = -EFAIL; /* valid error */
		goto leave;
	}

	neededSize =
	    OCFS_ALIGN ((RunsInMap * sizeof (ocfs_offset_map)), osb->sect_size);

	status = ocfs_get_system_file_size (osb,
				    (OCFS_FILE_VOL_META_DATA + osb->node_num),
				    &fileSize, &allocSize);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	if (allocSize < neededSize) {
		LOG_TRACE_ARGS ("allocSize(%u.%u) < neededSize(%u.%u)",
				HI(allocSize), LO(allocSize), HI(neededSize),
				LO(neededSize));
		status = ocfs_extend_system_file (osb,
				  (OCFS_FILE_VOL_META_DATA + osb->node_num),
				  neededSize, NULL);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto leave;
		}
	}

	pMapBuffer = ocfs_malloc (neededSize);
	if (pMapBuffer == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto leave;
	}

	numRec = 0;
	for (MapIndex = 0; MapIndex < RunsInMap; MapIndex++) {
		if (!ocfs_get_next_extent_map_entry (osb, &osb->trans_map, MapIndex,
						&foundVolOffset,
						&foundlogOffset, &length)) {
			/* It means this is a hole */
			continue;
		}

		pMapBuffer[numRec].length = length;
		pMapBuffer[numRec].actual_disk_off = foundVolOffset;
		pMapBuffer[numRec].log_disk_off = foundlogOffset;
		numRec++;
	}

	status = ocfs_force_get_file_entry (osb, &fe,
					((OCFS_FILE_VOL_META_DATA +
					  osb->node_num) * osb->sect_size) +
					osb->vol_layout.root_int_off, true);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	if (!IS_VALID_FILE_ENTRY (fe)) {
		LOG_ERROR_STATUS (status = -EINVAL);
		goto leave;
	}

	status = ocfs_write_force_disk (osb, (void *) pMapBuffer, neededSize,
					osb->log_meta_disk_off);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	fe->file_size = (numRec * sizeof (ocfs_offset_map));

	status = ocfs_force_put_file_entry (osb, fe, true);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

      leave:
	ocfs_release_file_entry (fe);

	ocfs_safefree (pMapBuffer);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_write_map_file */

