/*
 * ocfsgendirnode.c
 *
 * Allocate, free, read, write, find, etc. dirnodes.
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
#define OCFS_DEBUG_CONTEXT   OCFS_DEBUG_CONTEXT_DIRINFO

/*
 * ocfs_print_file_entry()
 *
 */
void ocfs_print_file_entry (ocfs_file_entry * fe)
{
	LOG_TRACE_ARGS ("This fe has name %s\n", fe->filename);
}				/* ocfs_print_file_entry */

/*
 * ocfs_print_dir_node()
 *
 */
void ocfs_print_dir_node (ocfs_super * osb, ocfs_dir_node * DirNode)
{
	int i;
	ocfs_file_entry *pOrigFileEntry;

	if (DirNode->dir_node_flags & DIR_NODE_FLAG_ROOT)
		LOG_TRACE_STR ("First dirnode of the dir");

	LOG_TRACE_ARGS ("signature: %s\n", DirNode->signature);

	LOG_TRACE_ARGS ("node_disk_off: %u.%u\n", HILO (DirNode->node_disk_off));

	LOG_TRACE_ARGS ("num_ents: %u, num_ent_used: %u\n", DirNode->num_ents,
		DirNode->num_ent_used);

	for (i = 0; i < DirNode->num_ent_used; i++) {
		pOrigFileEntry = FILEENT (DirNode, i);
		LOG_TRACE_ARGS ("filename: %s\n", pOrigFileEntry->filename);
	}
}				/* ocfs_print_dir_node */

/*
 * ocfs_alloc_node_block()
 *
 */
int ocfs_alloc_node_block (ocfs_super * osb,
		__u64 FileSize,
		__u64 * DiskOffset,
		__u64 * file_off, __u64 * NumClusterAlloc, __u32 NodeNum, __u32 Type)
{
	int status = 0;
	int tmpstat;
	__u64 fileSize = 0;
	__u64 offset = 0;
	__u64 length = 0;
	__u64 lockId = 0;
	__u64 numBytes = 0;
	__u64 allocSize = 0;
	__u64 prevFileSize = 0;
	__u64 extent;
	__u64 newFileSize;
	__u64 bitMapSize;
	__u8 *buffer = NULL;
	ocfs_alloc_bm DirAllocBitMap;
	__u32 numBits = 0;
	__u32 foundBit = -1;
	__u32 blockSize = 0;
	bool bLockAcquired = false;
	ocfs_lock_res *pLockResource = NULL;
	__u32 fileId = 0;
	__u32 extendFileId = 0;
	ocfs_log_record *pOcfsLogRec = NULL;
	ocfs_file_entry *fe = NULL;

	LOG_ENTRY ();

	fe = ocfs_allocate_file_entry();
	if (fe == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto leave;
	}

#ifdef PARANOID_LOCKS
	ocfs_down_sem (&(osb->dir_alloc_lock), true);
	ocfs_down_sem (&(osb->file_alloc_lock), true);
#endif
	ocfs_down_sem (&(osb->vol_alloc_lock), true);

	if (Type == DISK_ALLOC_DIR_NODE) {
		fileId = OCFS_FILE_DIR_ALLOC_BITMAP + NodeNum;
		blockSize = (__u32) osb->vol_layout.dir_node_size;
		extendFileId = OCFS_FILE_DIR_ALLOC + NodeNum;
	} else if (Type == DISK_ALLOC_EXTENT_NODE) {
		fileId = OCFS_FILE_FILE_ALLOC_BITMAP + NodeNum;
		extendFileId = OCFS_FILE_FILE_ALLOC + NodeNum;
		blockSize = (__u32) osb->vol_layout.file_node_size;
	}

	/* Allocate a block of size blocksize from the relevant file/bitmap */

	OCFS_ASSERT (blockSize);

	lockId = (fileId * OCFS_SECTOR_SIZE) + osb->vol_layout.root_int_off;

	/* Get a lock on the file */
	status =
	    ocfs_acquire_lock (osb, lockId, OCFS_DLM_EXCLUSIVE_LOCK,
			     FLAG_FILE_CREATE, &pLockResource, fe);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	bLockAcquired = true;

	numBytes = OCFS_ALIGN ((FileSize), blockSize);
	numBits = (__u32) (numBytes / blockSize);

//    while(1)
	{
		/* Read in the bitmap file for the dir alloc and look for the required */
		/* space, if found */
		fileSize = fe->file_size;
		allocSize = fe->alloc_size;

		prevFileSize = fileSize;

		if ((fileSize != 0) && (allocSize != 0)) {
			/* Round this off to dirnodesize */
			length = OCFS_ALIGN (allocSize, OCFS_PAGE_SIZE);

			buffer = vmalloc (length);
			if (buffer == NULL) {
				LOG_ERROR_STATUS (status = -ENOMEM);
				goto leave;
			}

			status =
			    ocfs_read_system_file (osb, fileId, buffer, allocSize,
					    offset);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto leave;
			}

			ocfs_initialize_bitmap (&DirAllocBitMap, (__u32 *) buffer,
					      (__u32) (fileSize * 8));

			/* Find the requisite number of bits... */

			/* This function will check for clear bits in the Bitmap for */
			/* consective clear bits equal to ClusterCount */
			foundBit =
			    ocfs_find_clear_bits (&DirAllocBitMap, (__u32) numBits,
					       0, 0);
		}

		/* It returns -1 on failure , otherwise ByteOffset points at the */
		/* location in bitmap from where there are ClusterCount no of bits */
		/* are free. */

		if (foundBit == -1) {
			/* if not found add more allocation to the file and try again. */

			/* Lets get a 1MB chunks every time or clustersize which ever */
			/* is greater or the number of bit asked */
			extent =
			    ((ONE_MEGA_BYTE) >
			     osb->vol_layout.
			     cluster_size) ? (ONE_MEGA_BYTE) : osb->vol_layout.
			    cluster_size;

			extent = (extent > (numBits * blockSize)) ? extent :
			    (numBits * blockSize);

			extent = OCFS_ALIGN (extent, ONE_MEGA_BYTE);

			status = ocfs_get_system_file_size (osb, (extendFileId),
						&newFileSize, &allocSize);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto leave;
			}

			/* This is for OUI optimzation to allocate more disk space for */
			/* directory allocations */

			if (allocSize > 0)
				extent *= 2;

			status =
			    ocfs_extend_system_file (osb, (extendFileId),
					      newFileSize + extent, NULL);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto leave;
			}

			newFileSize += extent;
			bitMapSize = newFileSize / (blockSize * 8);

			/* Calculate the new bitmap size */
			status = ocfs_extend_system_file (osb, fileId, bitMapSize, fe);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto leave;
			}

			/* ?? Free the buffer here */
			vfree (buffer);
			buffer = NULL;

			fileSize = fe->file_size;
			allocSize = fe->alloc_size;

			length = OCFS_ALIGN (allocSize, OCFS_PAGE_SIZE);

			buffer = vmalloc (length);
			if (buffer == NULL) {
				LOG_ERROR_STATUS (status = -ENOMEM);
				goto leave;
			}

			status =
			    ocfs_read_system_file (osb, fileId, buffer, allocSize,
					    offset);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto leave;
			}

			ocfs_initialize_bitmap (&DirAllocBitMap, (__u32 *) buffer,
					      (__u32) (fileSize * 8));

			foundBit = prevFileSize * 8;
//        continue;

		}
/*      else
      {
        break;
      }
*/
	}

	LOG_TRACE_ARGS ("byte offset=%d\n", foundBit);

	ocfs_set_bits (&DirAllocBitMap, (__u32) foundBit, (__u32) numBits);

	/* Log the change under current transid, */
	{
		__u32 size;

		size = sizeof (ocfs_log_record);
		size = (__u32) OCFS_ALIGN (size, osb->sect_size);

		if ((pOcfsLogRec = ocfs_malloc (size)) == NULL) {
			LOG_ERROR_STATUS (status = -ENOMEM);
			goto leave;
		}

		pOcfsLogRec->log_id = osb->curr_trans_id;
		pOcfsLogRec->log_type = LOG_TYPE_DISK_ALLOC;

		pOcfsLogRec->rec.alloc.length = numBits;
		pOcfsLogRec->rec.alloc.file_off = (foundBit * blockSize);
		pOcfsLogRec->rec.alloc.type = Type;
		pOcfsLogRec->rec.alloc.node_num = NodeNum;

		/* Log the original dirnode sector and the new cluster where the */
		/* info is stored */
		status = ocfs_write_log (osb, pOcfsLogRec, LOG_RECOVER);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto leave;
		}
	}

	/* Write the bitmap file back */
	status = ocfs_write_system_file (osb, fileId, buffer, allocSize, offset);
	if (status < 0) {
		LOG_ERROR_STATUS(status);
		goto leave;
	}

	*DiskOffset = ocfs_file_to_disk_off (osb, (extendFileId),
					(foundBit * blockSize));
	if (*DiskOffset == 0) {
		LOG_ERROR_STATUS(status = -EFAIL);
		goto leave;
	}

	*file_off = (__u64) ((__u64) foundBit * (__u64) blockSize);
	/* this can just fall through */
	if (*file_off == 0) {
		LOG_TRACE_ARGS ("offset=%u.%u, type=%x, blksz=%u, foundbit=%u\n",
			HI(*file_off), LO(*file_off), Type, blockSize, foundBit);
	}

      leave:
	
        ocfs_up_sem (&(osb->vol_alloc_lock));
#ifdef PARANOID_LOCKS
	ocfs_up_sem (&(osb->file_alloc_lock));
	ocfs_up_sem (&(osb->dir_alloc_lock));
#endif
	if (bLockAcquired) {
		tmpstat =
		    ocfs_release_lock (osb, lockId, OCFS_DLM_EXCLUSIVE_LOCK,
				     FLAG_FILE_CREATE, pLockResource, fe);
		if (tmpstat < 0)
			LOG_ERROR_STATUS (tmpstat);
	}

	ocfs_safevfree (buffer);
	ocfs_release_file_entry (fe);
	ocfs_safefree (pOcfsLogRec);
	ocfs_put_lockres (pLockResource);
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_alloc_node_block */

/*
 * ocfs_free_vol_block()
 *
 */
int ocfs_free_vol_block (ocfs_super * osb, ocfs_free_log * FreeLog, __u32 NodeNum, __u32 Type)
{
	int status = 0;
	__u64 fileSize = 0;
	__u64 offset = 0;
	__u64 length = 0;
	__u64 allocSize = 0;
	__u32 foundBit = -1;
	__u32 blockSize = 0;
	__u32 fileId = 0;
	__u32 extendFileId = 0;
	__u8 *buffer = NULL;
	ocfs_alloc_bm AllocBitMap;
	ocfs_alloc_bm *pTempBitMap;
	__u32 i;
	__u32 size;

	LOG_ENTRY ();

#ifdef PARANOID_LOCKS
	ocfs_down_sem (&(osb->dir_alloc_lock), true);
	ocfs_down_sem (&(osb->file_alloc_lock), true);
#endif
	ocfs_down_sem (&(osb->vol_alloc_lock), true);

	switch (Type) {
	    case DISK_ALLOC_DIR_NODE:
		    fileId = OCFS_FILE_DIR_ALLOC_BITMAP + NodeNum;
		    blockSize = (__u32) osb->vol_layout.dir_node_size;
		    extendFileId = OCFS_FILE_DIR_ALLOC + NodeNum;

		    if (!IS_VALID_NODE_NUM (NodeNum)) {
			    LOG_ERROR_STATUS(status = -EINVAL);
			    goto leave;
		    }
		    break;

	    case DISK_ALLOC_EXTENT_NODE:
		    fileId = OCFS_FILE_FILE_ALLOC_BITMAP + NodeNum;
		    extendFileId = OCFS_FILE_FILE_ALLOC + NodeNum;
		    blockSize = (__u32) osb->vol_layout.file_node_size;

		    if (!IS_VALID_NODE_NUM (NodeNum)) {
			    LOG_ERROR_STATUS(status = -EINVAL);
			    goto leave;
		    }
		    break;

	    case DISK_ALLOC_VOLUME:
		    break;

	    default:
		    goto leave;
	}

	if (Type == DISK_ALLOC_VOLUME) {
		size = (__u32) OCFS_SECTOR_ALIGN ((osb->cluster_bitmap.size) / 8);
		status = ocfs_read_metadata (osb, osb->cluster_bitmap.buf, size,
					   osb->vol_layout.bitmap_off);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto leave;
		}
		pTempBitMap = &osb->cluster_bitmap;
	} else {
		/* Read in the bitmap file for the dir alloc and look for the */
		/* required space, if found */

		status = ocfs_get_system_file_size (osb, fileId, &fileSize, &allocSize);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto leave;
		}

		/* Round this off to dirnodesize */
		length = OCFS_ALIGN (allocSize, OCFS_PAGE_SIZE);

                /* !!! vmalloc !!! */
		if ((buffer = vmalloc (length)) == NULL) {
			LOG_ERROR_STATUS (status = -ENOMEM);
			goto leave;
		}

		status =
		    ocfs_read_system_file (osb, fileId, buffer, allocSize, offset);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto leave;
		}

		ocfs_initialize_bitmap (&AllocBitMap, (__u32 *) buffer,
				      (__u32) (fileSize * 8));
		pTempBitMap = &AllocBitMap;
	}

	for (i = 0; i < FreeLog->num_free_upds; i++) {
		if (FreeLog->free_bitmap[i].file_off == 0 && Type == 0) {
			LOG_ERROR_ARGS ("offset=0, type=%x, blksz=%d", Type,
					blockSize);
		}

		if (Type == DISK_ALLOC_VOLUME)
			foundBit = (__u32) FreeLog->free_bitmap[i].file_off;
		else
			foundBit =
			    (__u32) (FreeLog->free_bitmap[i].file_off /
				   blockSize);

		ocfs_clear_bits (pTempBitMap, (__u32) foundBit,
			       (__u32) FreeLog->free_bitmap[i].length);

		LOG_TRACE_ARGS("gb_c: bit=%d, len=%u, i=%d\n", foundBit,
			       (__u32)FreeLog->free_bitmap[i].length, i);
	}

	/* Write a cleanup log here */

	if (Type == DISK_ALLOC_VOLUME) {
		size = (__u32) OCFS_SECTOR_ALIGN ((osb->cluster_bitmap.size) / 8);

#if 0
		/* I have absolutely no idea why this is done twice! */
		status = ocfs_write_disk (osb, osb->cluster_bitmap.buf,
					size, osb->vol_layout.bitmap_off);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto leave;
		}
#endif
		status = ocfs_write_metadata (osb, osb->cluster_bitmap.buf,
					    size, osb->vol_layout.bitmap_off);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto leave;
		}
	} else {
		status =
		    ocfs_write_system_file (osb, fileId, buffer, allocSize, offset);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto leave;
		}
	}
      leave:
	ocfs_up_sem (&(osb->vol_alloc_lock));
#ifdef PARANOID_LOCKS
	ocfs_up_sem (&(osb->file_alloc_lock));
	ocfs_up_sem (&(osb->dir_alloc_lock));
#endif
	ocfs_safevfree (buffer);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_free_vol_block */

/*
 * ocfs_free_node_block()
 *
 */
int ocfs_free_node_block (ocfs_super * osb, __u64 file_off, __u64 Length, __u32 NodeNum,
	       __u32 Type)
{
	int status = 0;
	int tmpstat;
	__u64 fileSize = 0;
	__u64 offset = 0;
	__u64 length = 0;
	__u64 lockId = 0;
	__u64 allocSize = 0;
	__u8 *buffer = NULL;
	ocfs_alloc_bm DirAllocBitMap;
	__u32 foundBit = -1;
	__u32 blockSize = 0;
	bool bLockAcquired = false;
	ocfs_lock_res *pLockResource = NULL;
	__u32 fileId = 0;
	__u32 extendFileId = 0;
	ocfs_file_entry *fe = NULL;

	LOG_ENTRY ();

	fe = ocfs_allocate_file_entry();
	if (fe == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto leave;
	}

	if (Type == DISK_ALLOC_DIR_NODE) {
		fileId = OCFS_FILE_DIR_ALLOC_BITMAP + NodeNum;
		blockSize = (__u32) osb->vol_layout.dir_node_size;
		extendFileId = OCFS_FILE_DIR_ALLOC + NodeNum;
	} else if (Type == DISK_ALLOC_EXTENT_NODE) {
		fileId = OCFS_FILE_FILE_ALLOC_BITMAP + NodeNum;
		extendFileId = OCFS_FILE_FILE_ALLOC + NodeNum;
		blockSize = (__u32) osb->vol_layout.file_node_size;
	}

	/* Allocate a block of size blocksize from the relevant file/bitmap */

	lockId = (fileId * OCFS_SECTOR_SIZE) + osb->vol_layout.root_int_off;

	/* Get a lock on the file */
	status = ocfs_acquire_lock (osb, lockId, OCFS_DLM_EXCLUSIVE_LOCK,
				    FLAG_FILE_CREATE, &pLockResource, fe);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	bLockAcquired = true;

	/* Read in the bitmap file for the dir alloc and look for the required */
	/* space, if found */
	status = ocfs_get_system_file_size (osb, fileId, &fileSize, &allocSize);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	/* Round this off to dirnodesize */
	length = OCFS_ALIGN (allocSize, OCFS_PAGE_SIZE);

	if ((buffer = ocfs_malloc (length)) == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto leave;
	}

	status = ocfs_read_system_file (osb, fileId, buffer, allocSize, offset);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	ocfs_initialize_bitmap (&DirAllocBitMap, (__u32 *) buffer,
			      (__u32) (fileSize * 8));

	foundBit = (__u32) (file_off / blockSize);
	ocfs_clear_bits (&DirAllocBitMap, (__u32) foundBit, (__u32) Length);

	status = ocfs_write_system_file (osb, fileId, buffer, allocSize, offset);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

      leave:
	if (bLockAcquired) {
		tmpstat = ocfs_release_lock (osb, lockId, OCFS_DLM_EXCLUSIVE_LOCK,
					     FLAG_FILE_CREATE, pLockResource, fe);
		if (tmpstat < 0)
			LOG_ERROR_STATUS (tmpstat);
	}

	ocfs_safefree (buffer);
	ocfs_release_file_entry (fe);
	ocfs_put_lockres (pLockResource);
	LOG_EXIT_STATUS (0);
	return 0;
}				/* ocfs_free_node_block */

/*
 * ocfs_free_directory_block()
 *
 */
int ocfs_free_directory_block (ocfs_super * osb, ocfs_file_entry * fe, __s32 LogNodeNum)
{
	int status = 0;
        ocfs_file_entry *dir_hdr_fe = NULL;
	ocfs_dir_node *PDirNode;
	__u32 size;
	__u32 numUpdt;
	__u64 currentDirNode;
	ocfs_cleanup_record *pCleanupLogRec = NULL;

	LOG_ENTRY ();

	size = sizeof (ocfs_cleanup_record);
	size = (__u32) OCFS_ALIGN (size, OCFS_PAGE_SIZE);

	if ((pCleanupLogRec = ocfs_malloc (size)) == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto leave;
	}

	pCleanupLogRec->rec.free.num_free_upds = 0;

	currentDirNode = fe->extents[0].disk_off;

	size = OCFS_SECTOR_SIZE;

	status = ocfs_get_file_entry (osb, &dir_hdr_fe, currentDirNode);
	if (status < 0 || dir_hdr_fe==NULL) {
                if (status >= 0)
                        status = -EFAIL;
		LOG_ERROR_STATUS (status);
		goto leave;
	}
        /* alloc a file entry, but use it as a dir node header. yeah. ok. */
        PDirNode = (ocfs_dir_node *)dir_hdr_fe;

	pCleanupLogRec->log_id = osb->curr_trans_id;
	pCleanupLogRec->log_type = LOG_FREE_BITMAP;

	while ((PDirNode->node_disk_off != INVALID_NODE_POINTER) &&
	       (IS_VALID_DIR_NODE (PDirNode))) {
		/* Add to the cleanup log */
		numUpdt = pCleanupLogRec->rec.free.num_free_upds;
		if (numUpdt >= FREE_LOG_SIZE) {
			status =
			    ocfs_write_node_log (osb,
					      (ocfs_log_record *)
					      pCleanupLogRec, LogNodeNum,
					      LOG_CLEANUP);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto leave;
			}
			numUpdt = pCleanupLogRec->rec.free.num_free_upds = 0;
		}

		pCleanupLogRec->rec.free.free_bitmap[numUpdt].length = 1;
		pCleanupLogRec->rec.free.free_bitmap[numUpdt].file_off =
		    PDirNode->alloc_file_off;
		pCleanupLogRec->rec.free.free_bitmap[numUpdt].type =
		    DISK_ALLOC_DIR_NODE;
		pCleanupLogRec->rec.free.free_bitmap[numUpdt].node_num =
		    PDirNode->alloc_node;
		(pCleanupLogRec->rec.free.num_free_upds)++;

		/* LOG_FREE_BITMAP */

		if (PDirNode->next_node_ptr != INVALID_NODE_POINTER) {
			status = ocfs_read_disk (osb, PDirNode,
						 OCFS_SECTOR_SIZE,
						 PDirNode->next_node_ptr);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto leave;
			}
			continue;
		} else {
			break;
		}
	}

	/* Write the log and break */
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
       	ocfs_release_file_entry(dir_hdr_fe);
	ocfs_safefree (pCleanupLogRec);
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_free_directory_block */

/*
 * ocfs_recover_dir_node()
 *
 */
int ocfs_recover_dir_node (ocfs_super * osb, __u64 OrigDirNodeOffset,
			   __u64 SavedDirNodeOffset)
{
	LOG_ENTRY ();

	LOG_EXIT_STATUS (0);
	return 0;
}				/* ocfs_recover_dir_node */

#if 0
/*
 * ocfs_read_dir_node()
 *
 */
int ocfs_read_dir_node (ocfs_super * osb, ocfs_dir_node * DirNode,
			__u64 NodeDiskOffset)
{
	int status = 0;

	LOG_ENTRY ();

	/* Read in the Dir Node from the disk into the buffer supplied */
	status = ocfs_read_disk (osb, DirNode, osb->vol_layout.dir_node_size,
				 NodeDiskOffset);
	if (status < 0) {
 		LOG_ERROR_ARGS ("status=%d, dirnodesz=%u.%u, off=%u.%u",
 				status, HILO(osb->vol_layout.dir_node_size),
 				HILO(NodeDiskOffset));
	}

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_read_dir_node */
#endif

/*
 * ocfs_write_force_dir_node()
 *
 */
int ocfs_write_force_dir_node (ocfs_super * osb,
		       ocfs_dir_node * DirNode, __s32 IndexFileEntry)
{
	int status = 0;

	LOG_ENTRY ();

	if (IndexFileEntry != -1) {
		/* Read in the Dir Node from the disk into the buffer supplied */
		status = ocfs_write_disk (osb,
					(__u8 *) (((__u8 *) DirNode) +
						 ((IndexFileEntry +
						   1) * osb->sect_size)),
					osb->sect_size,
					DirNode->node_disk_off +
					((IndexFileEntry +
					  1) * osb->sect_size));
		if (status < 0) {
			LOG_ERROR_STATUS (status);
		}
	}

	/* Write the first sector last */
	status = ocfs_write_disk (osb, DirNode, osb->sect_size,
				  DirNode->node_disk_off);
	if (status < 0)
		LOG_ERROR_STATUS (status);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_write_force_dir_node */

/*
 * ocfs_write_dir_node()
 *
 */
int ocfs_write_dir_node (ocfs_super * osb, ocfs_dir_node * DirNode, __s32 IndexFileEntry)
{
	int status = 0;
	__u64 offset;
	__u32 size;
	__u8 *buffer;
	bool bCacheWrite = false;
	bool bFileCacheWrite = false;

	LOG_ENTRY ();

	if ((DISK_LOCK_CURRENT_MASTER (DirNode) == osb->node_num) &&
	    (DISK_LOCK_FILE_LOCK (DirNode) == OCFS_DLM_ENABLE_CACHE_LOCK)) {
		bCacheWrite = true;
	}

	if (IndexFileEntry != -1) {
		ocfs_file_entry *fe = NULL;

		/* Read in the Dir Node from the disk into the buffer supplied */

		offset = DirNode->node_disk_off +
		    ((IndexFileEntry + 1) * osb->sect_size);
		size = (__u32) osb->sect_size;
		buffer = (__u8 *) (((__u8 *) DirNode) +
				  ((IndexFileEntry + 1) * osb->sect_size));
		fe = (ocfs_file_entry *) buffer;

		if ((DISK_LOCK_CURRENT_MASTER (fe) == osb->node_num) &&
		    (DISK_LOCK_FILE_LOCK (fe) == OCFS_DLM_ENABLE_CACHE_LOCK)) {
			bFileCacheWrite = true;
		}

		/* Write in the dir node */
		if (bFileCacheWrite) {
			status = ocfs_write_metadata (osb, buffer, size, offset);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
			}
#if 0
			if (!bCacheWrite) {
				status =
				    ocfs_write_disk (osb, buffer, size, offset);
				if (status < 0) {
					LOG_ERROR_STATUS (status);
				}
			}
#endif
		} else {
			status = ocfs_write_disk (osb, buffer, size, offset);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
			}
		}
	}

	/* Write the first sector last */
	offset = DirNode->node_disk_off;
	size = (__u32) OCFS_SECTOR_SIZE;

	/* Write the dir node */
	if (bCacheWrite) {
		status = ocfs_write_metadata (osb, DirNode, size, offset);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
		}
	} else {
		status = ocfs_write_disk (osb, DirNode, size, offset);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
		}
	}

	IF_TRACE (ocfs_print_dir_node (osb, DirNode));

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_write_dir_node */


/*
 * ocfs_walk_dir_node()
 *
 */
bool ocfs_walk_dir_node (ocfs_super * osb, ocfs_dir_node * DirNode,
			 ocfs_file_entry * found_fe, ocfs_file * OFile)
{
	__u32 start;
	__u32 i;
	ocfs_file_entry *fe;
	int status;
	bool bRet = false;

	LOG_ENTRY ();

	if (OFile != NULL)
		start = OFile->curr_byte_off;
	else
		start = 0;

	if (!IS_VALID_DIR_NODE (DirNode)) {
		bRet = false;
		goto bail;
	}

	while (1) {
		/* Iterate thru this dirnode and find a matching entry */
		for (i = start; i < DirNode->num_ent_used; i++) {
			fe = FILEENT (DirNode, i);

			if (IS_FE_DELETED(fe->sync_flags) ||
			    (!(fe->sync_flags & OCFS_SYNC_FLAG_VALID))) {
				continue;
			}

			/* Check to see if the name satisfies pattern */
			{
				if ((OFile == NULL)
				    && (fe->attribs & OCFS_ATTRIB_DIRECTORY)) {
					continue;
				}

				status = 0;
				memcpy ((void *) found_fe, (void *) fe,
					OCFS_SECTOR_SIZE);

				LOG_TRACE_ARGS
				    ("Returning entry: %u, name: %s\n", i,
				     fe->filename);

				if (OFile != NULL) {
					OFile->curr_dir_off =
					    DirNode->node_disk_off;
					OFile->curr_byte_off = i + 1;
				}

				bRet = true;
				goto bail;
			}
		}

		if (DirNode->next_node_ptr != -1) {
			status =
			    ocfs_read_dir_node (osb, DirNode,
					     DirNode->next_node_ptr);

			if (!IS_VALID_DIR_NODE (DirNode)) {
				bRet = false;
				goto bail;
			}
			start = 0;
			continue;
		} else {
			/* We are done... */
			break;
		}
	}

	if (OFile != NULL) {
		OFile->curr_dir_off = DirNode->node_disk_off;
		OFile->curr_byte_off = i + 1;
	}

      bail:
	LOG_EXIT_ULONG (bRet);
	return bRet;
}				/* ocfs_walk_dir_node */

/*
 * ocfs_search_dir_node()
 *
 */
bool ocfs_search_dir_node (ocfs_super * osb, ocfs_dir_node * DirNode,
			   struct qstr * SearchName, ocfs_file_entry * found_fe,
			   ocfs_file * OFile)
{
	__u32 start;
	__u32 index;
	ocfs_file_entry *fe;
	int status;
	bool bRet = false;

	LOG_ENTRY ();

	if (OFile != NULL)
		start = OFile->curr_byte_off;
	else
		start = 0;

	index = start;

	while (1) {
		/* Iterate thru this dirnode and find a matching entry */
		if (index < DirNode->num_ent_used) {
			if (ocfs_find_index (osb, DirNode, SearchName, (int *) &index)) {
				fe = FILEENT (DirNode, index);

				memcpy ((void *) found_fe, (void *) fe,
					OCFS_SECTOR_SIZE);
				if (OFile != NULL) {
					OFile->curr_dir_off =
					    DirNode->node_disk_off;
					OFile->curr_byte_off = index + 1;
				}
				bRet = true;
				goto bail;
			}
		}

		if (DirNode->next_node_ptr != -1) {
			status =
			    ocfs_read_dir_node (osb, DirNode,
					     DirNode->next_node_ptr);

			if (!IS_VALID_DIR_NODE (DirNode)) {
				bRet = false;
				goto bail;
			}

			index = 0;
			continue;
		} else {
			/* We are done... */
			break;
		}
	}

	if (OFile != NULL) {
		OFile->curr_dir_off = DirNode->node_disk_off;
		OFile->curr_byte_off = index + 1;
	}

      bail:
	LOG_EXIT_ULONG (bRet);
	return bRet;
}				/* ocfs_search_dir_node */


/*
 * ocfs_find_index()
 *
 */
bool ocfs_find_index (ocfs_super * osb, ocfs_dir_node * DirNode,
		      struct qstr * FileName, int *Index)
{
	int lowBnd, upBnd;
	ocfs_file_entry *fe;
	int res = -1, index = 0, start = 0;
	int ret = false;
        struct qstr q;

	LOG_ENTRY ();
	if (!IS_VALID_DIR_NODE (DirNode) || FileName==NULL) {
		ret = false;
		goto bail;
	}

	if (*Index > 0)
		start = *Index;

	if (DirNode->index_dirty) {
		for (index = start; index < DirNode->num_ent_used; index++) {
			fe = FILEENT (DirNode, index);
			if (IS_FE_DELETED(fe->sync_flags) ||
			    (!(fe->sync_flags & OCFS_SYNC_FLAG_VALID))) {
				continue;
			}
                        q.name = fe->filename;
                        q.len = strlen(fe->filename);
                        res = ocfs_compare_qstr(&q, FileName);
			if (!res) {
				*Index = index;
				ret = true;
				goto bail;
			}
		}
		*Index = index;
		ret = false;
		goto bail;
	}

	for (lowBnd = start, upBnd = (DirNode->num_ent_used - start); upBnd;
	     upBnd >>= 1) {
		index = lowBnd + (upBnd >> 1);

		fe = FILEENT (DirNode, index);

		if (IS_FE_DELETED(fe->sync_flags) ||
		    (!(fe->sync_flags & OCFS_SYNC_FLAG_VALID))) {
			for (index = lowBnd; index < (lowBnd + upBnd); index++) {
				fe = FILEENT (DirNode, index);
				if (IS_FE_DELETED(fe->sync_flags) ||
				    (!(fe->sync_flags & OCFS_SYNC_FLAG_VALID))) {
					continue;
				}
                               
                                q.name = fe->filename;
                                q.len = strlen(fe->filename);
                                res = ocfs_compare_qstr(&q, FileName);
				if (!res) {
					*Index = index;
					ret = true;
					goto bail;
				}
				if (res < 0) {
					*Index = index;
					ret = false;
					goto bail;
				}
			}
			*Index = lowBnd + upBnd - 1;
			ret = false;
			goto bail;
		}

                q.name = fe->filename;
                q.len = strlen(fe->filename);
                res = ocfs_compare_qstr(&q, FileName);
		if (!res) {
			*Index = index;
			ret = true;
			goto bail;
		}

		if (res > 0) {
			lowBnd = index + 1;
			--upBnd;
		}
	}

	*Index = index;

      bail:
	LOG_EXIT_ULONG (ret);
	return ret;
}				/* ocfs_find_index */

/*
 * ocfs_reindex_dir_node()
 *
 */
int ocfs_reindex_dir_node (ocfs_super * osb, __u64 DirNodeOffset, ocfs_dir_node * DirNode)
{
	int status = 0;
	ocfs_dir_node *pDirNode = NULL;
	ocfs_file_entry *pInsertEntry;
	ocfs_file_entry *fe;
	__u32 index;
	__u8 offset = 0;
	int res;

	LOG_ENTRY ();

	if (DirNode == NULL) {
		pDirNode = ocfs_allocate_dirnode();
		if (pDirNode == NULL) {
			LOG_ERROR_STATUS (status = -ENOMEM);
			goto leave;
		}

		status = ocfs_read_dir_node (osb, pDirNode, DirNodeOffset);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto leave;
		}
	} else {
		pDirNode = DirNode;
	}

	if (pDirNode->index_dirty) {
		offset = pDirNode->bad_off;
		pInsertEntry =
		    (ocfs_file_entry *) (FIRST_FILE_ENTRY (pDirNode) +
					 (offset * OCFS_SECTOR_SIZE));

		for (index = 0; index < pDirNode->num_ent_used; index++) {
			fe = FILEENT (pDirNode, index);

			if (IS_FE_DELETED(fe->sync_flags) ||
			    (!(fe->sync_flags & OCFS_SYNC_FLAG_VALID))) {
				continue;
			}

			res = strcmp (fe->filename, pInsertEntry->filename);
			if (res < 0) {
				break;
			}
		}

		if (index < (pDirNode->num_ent_used - 1)) {
			memmove (&pDirNode->index[index + 1],
				 &pDirNode->index[index],
				 pDirNode->num_ent_used - index);
			pDirNode->index[index] = offset;
		}

		pDirNode->index_dirty = 0;

		status = ocfs_write_dir_node (osb, pDirNode, -1);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto leave;
		}

	}
      leave:
	if (DirNode == NULL)
		ocfs_release_dirnode (pDirNode);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_reindex_dir_node */

/*
 * ocfs_insert_dir_node()
 *
 */
int ocfs_insert_dir_node (ocfs_super * osb,
	       ocfs_dir_node * DirNode,
	       ocfs_file_entry * InsertEntry,
	       ocfs_dir_node * LockNode, __s32 * IndexOffset)
{
	int status = 0;
	ocfs_file_entry *fe;
	int res = 0;
	int index = -1;
	ocfs_file_entry *lastEntry;
	ocfs_log_record *pLogRec = NULL;
	__u32 size;
	__u8 freeOffset;
        struct qstr q;

	LOG_ENTRY ();

	if (!IS_VALID_DIR_NODE (DirNode)) {
		LOG_ERROR_STATUS(status = -EINVAL);
		goto bail;
	}

	if (DirNode->index_dirty) {
		status = ocfs_reindex_dir_node (osb, DirNode->node_disk_off, DirNode);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto bail;
		}
	}

	if (DirNode->num_ent_used < osb->max_dir_node_ent) {
		if (DirNode->num_ent_used) {
                        q.name = InsertEntry->filename;
                        q.len = strlen(InsertEntry->filename);                    
			if (ocfs_find_index (osb, DirNode, &q, &index)) {
				/* Already inserted... */
				status = -EEXIST;
				goto bail;
			}

			if (index < DirNode->num_ent_used) {
				fe = FILEENT (DirNode, index);

				res = strcmp (fe->filename, InsertEntry->filename);
				if (res > 0) {
					/* We are greater than the entry in question we */
					/* shd be less than the one next to it */
					index++;
				}
			}
		} else {
			index = 0;
		}

		if (index < DirNode->num_ent_used)
			memmove (&DirNode->index[index + 1],
				 &DirNode->index[index],
				 DirNode->num_ent_used - index);

		if (DirNode->num_ent_used) {
			if (DirNode->num_del) {
				/* Insert at first deleted & change first deleted */
				freeOffset = DirNode->first_del;
				DirNode->num_del--;
				if (DirNode->num_del) {
					lastEntry =
					    (ocfs_file_entry
					     *) (FIRST_FILE_ENTRY (DirNode) +
						 (freeOffset *
						  OCFS_SECTOR_SIZE));
					DirNode->first_del =
					    lastEntry->next_del;
				}
			} else {
				/* Insert at end and change the index */
				freeOffset = DirNode->num_ent_used;
			}
		} else {
			freeOffset = 0;
		}

		lastEntry = (ocfs_file_entry *) (FIRST_FILE_ENTRY (DirNode) +
						 (freeOffset *
						  OCFS_SECTOR_SIZE));

		*IndexOffset = freeOffset;

		/* Put the entry at the end */
		InsertEntry->dir_node_ptr = DirNode->node_disk_off;

		memcpy (lastEntry, InsertEntry, osb->sect_size);

		OCFS_SET_FLAG (lastEntry->sync_flags, OCFS_SYNC_FLAG_VALID);

		lastEntry->this_sector = DirNode->node_disk_off +
		    ((freeOffset + 1) * OCFS_SECTOR_SIZE);
		InsertEntry->this_sector = lastEntry->this_sector;

		if (!(InsertEntry->sync_flags & OCFS_SYNC_FLAG_VALID)) {
			/* This is special for rename... */

			/* Log into recovery that this name only needs to be deleted if we fail */
			size = sizeof (ocfs_log_record);
			size = (__u32) OCFS_ALIGN (size, osb->sect_size);

			if ((pLogRec = ocfs_malloc (size)) == NULL) {
				LOG_ERROR_STATUS (status = -ENOMEM);
				goto bail;
			}

			/* Now start writing the cleanup log of the filentry master. */
			/* It is this node for normal cases and or the node we are doing */
			/* recovery for. */
			pLogRec->log_id = osb->curr_trans_id;
			pLogRec->log_type = LOG_DELETE_NEW_ENTRY;

			pLogRec->rec.del.node_num = osb->node_num;
			pLogRec->rec.del.ent_del = InsertEntry->this_sector;
			pLogRec->rec.del.parent_dirnode_off =
			    LockNode->node_disk_off;
			pLogRec->rec.del.flags = 0;

			status =
			    ocfs_write_node_log (osb, pLogRec, osb->node_num,
					      LOG_RECOVER);
			ocfs_safefree (pLogRec);
			if (status < 0)
				goto bail;
		}

		if (DISK_LOCK_FILE_LOCK (InsertEntry) ==
		    OCFS_DLM_ENABLE_CACHE_LOCK) {
			status = ocfs_write_metadata (osb, InsertEntry,
					OCFS_SECTOR_SIZE, InsertEntry->this_sector);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto bail;
			}
		}

		DirNode->index[index] = freeOffset;
		DirNode->num_ent_used++;
	} else {
		LOG_ERROR_STATUS (status = -ENOSPC);
		goto bail;
	}

      bail:
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_insert_dir_node */

/*
 * ocfs_del_file_entry()
 *
 */
int ocfs_del_file_entry (ocfs_super * osb, ocfs_file_entry * EntryToDel,
			 ocfs_dir_node * LockNode)
{
	int status = 0;
	int tmpstat = 0;
	__u32 offset;
	ocfs_dir_node *PDirNode = NULL;
	ocfs_file_entry *fe;
	ocfs_lock_res *dir_lres = NULL;
	__u64 dir_off;
	bool lock_acq = false;
	bool lock_rls = false;
	int index = 0;
	int length = 0;

	LOG_ENTRY ();

	dir_off = LockNode->node_disk_off;

	/* lock the dirnode */
	status = ocfs_acquire_lock (osb, dir_off, OCFS_DLM_EXCLUSIVE_LOCK,
			(FLAG_DIR | FLAG_FILE_CREATE), &dir_lres, (ocfs_file_entry *)LockNode);
	if (status < 0) {
		if (status != -EINTR)
			LOG_ERROR_STATUS (status);
		goto leave;
	}
	lock_acq = true;

	PDirNode = ocfs_allocate_dirnode();
	if (PDirNode == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto leave;
	}

	status = ocfs_read_dir_node (osb, PDirNode, EntryToDel->dir_node_ptr);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	offset = (EntryToDel->this_sector - PDirNode->node_disk_off) /
				OCFS_SECTOR_SIZE;

	offset -= 1;
	for (index = 0; index < PDirNode->num_ent_used; index++) {
		if (PDirNode->index[index] != offset)
			continue;

		fe = FILEENT (PDirNode, index);

		length = OCFS_SECTOR_SIZE;
		if (memcmp (fe, EntryToDel, length) == 0) {
			memmove (&PDirNode->index[index],
				 &PDirNode->index[index + 1],
				 PDirNode->num_ent_used - (index + 1));

			PDirNode->num_ent_used--;
			if (PDirNode->num_ent_used == 0) {
				PDirNode->num_del = 0;
			} else {
				/* Insert this dir node as one containing a deleted entry if the */
				/* count on the root dir node for deleted entries is 0 */
				if (PDirNode->num_del != 0) {
					PDirNode->num_del++;
					fe->sync_flags = OCFS_SYNC_FLAG_DELETED;
					fe->next_del = PDirNode->first_del;
					PDirNode->first_del = offset;
				} else {
					PDirNode->num_del++;
					fe->sync_flags = OCFS_SYNC_FLAG_DELETED;
					fe->next_del = INVALID_DIR_NODE_INDEX;
					PDirNode->first_del = offset;
				}
			}

			if (LockNode->head_del_ent_node == INVALID_NODE_POINTER) {
				if (LockNode->node_disk_off != PDirNode->node_disk_off)
					LockNode->head_del_ent_node = PDirNode->node_disk_off;
				else
					PDirNode->head_del_ent_node = PDirNode->node_disk_off;
			}

			/* clear the lock on disk */
			if (DISK_LOCK_FILE_LOCK (PDirNode) != OCFS_DLM_ENABLE_CACHE_LOCK) {
				ocfs_acquire_lockres (dir_lres);
				dir_lres->lock_type = OCFS_DLM_NO_LOCK;
				ocfs_release_lockres (dir_lres);

				lock_rls = true;

				if (LockNode->node_disk_off == PDirNode->node_disk_off)
					DISK_LOCK_FILE_LOCK (PDirNode) = OCFS_DLM_NO_LOCK;
				else
					DISK_LOCK_FILE_LOCK (LockNode) = OCFS_DLM_NO_LOCK;
			}

			status = ocfs_write_dir_node (osb, PDirNode, offset);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto leave;
			}

			if (LockNode->node_disk_off != PDirNode->node_disk_off) {
				if ((DISK_LOCK_CURRENT_MASTER (LockNode) ==
				     osb->node_num)
				    && (DISK_LOCK_FILE_LOCK (LockNode) ==
					OCFS_DLM_ENABLE_CACHE_LOCK))
					status =
					    ocfs_write_metadata (osb, LockNode,
							       osb->sect_size,
							       LockNode->
							       node_disk_off);
				else
					status =
					    ocfs_write_disk (osb, LockNode,
							   osb->sect_size,
							   LockNode->
							   node_disk_off);

				if (status < 0) {
					LOG_ERROR_STATUS (status);
					goto leave;
				}
			}
			if (lock_rls)
				lock_acq = false;
			goto leave;
		}
	}
      leave:
	if (lock_acq) {
		tmpstat = ocfs_release_lock (osb, dir_off, OCFS_DLM_EXCLUSIVE_LOCK,
					     (FLAG_DIR | FLAG_FILE_CREATE),
					     dir_lres, (ocfs_file_entry *)LockNode);
		if (tmpstat < 0)
			LOG_ERROR_STATUS (tmpstat);
	}

	ocfs_put_lockres (dir_lres);
	ocfs_release_dirnode (PDirNode);
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_del_file_entry */

/*
 * ocfs_insert_file()
 *
 */
int ocfs_insert_file (ocfs_super * osb, ocfs_dir_node * DirNode,
		      ocfs_file_entry * InsertEntry, ocfs_dir_node * LockNode,
		      ocfs_lock_res * LockResource, bool invalid_dirnode)
{
	int status = 0;
	__u64 bitmapOffset = 0;
	__u64 numClustersAlloc = 0;
	ocfs_dir_node *pNewDirNode = NULL;
	__s32 indexOffset = -1;

	LOG_ENTRY ();

	IF_TRACE (ocfs_print_dir_node (osb, DirNode));

	if (!IS_VALID_DIR_NODE (DirNode)) {
		LOG_ERROR_STATUS (status = -EFAIL);
		goto leave;
	}

	/* If we have a list of dir nodes go to the last dirnode */
	/* and insert in that. */

	/* We should not find this entry already inserted */
	if (!invalid_dirnode && DirNode->num_ent_used < osb->max_dir_node_ent) {
		status = ocfs_insert_dir_node (osb, DirNode, InsertEntry, LockNode,
					&indexOffset);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto leave;
		}
	} else {
		pNewDirNode = ocfs_allocate_dirnode();
		if (pNewDirNode == NULL) {
			LOG_ERROR_STATUS (status = -ENOMEM);
			goto leave;
		}

		if (DirNode->next_node_ptr != INVALID_NODE_POINTER) {
			status = ocfs_read_dir_node (osb, pNewDirNode,
					 DirNode->next_node_ptr);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto leave;
			}
		} else {
			__u64 fileOffset = 0;

			/* Allocate a new dir node */
			status =
			    ocfs_alloc_node_block (osb, osb->vol_layout.dir_node_size,
					    &bitmapOffset, &fileOffset,
					    &numClustersAlloc, osb->node_num,
					    DISK_ALLOC_DIR_NODE);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto leave;
			}
			memset (pNewDirNode, 0, osb->vol_layout.dir_node_size);
			ocfs_initialize_dir_node (osb, pNewDirNode, bitmapOffset,
					   fileOffset, osb->node_num);
		}

		if ((DISK_LOCK_CURRENT_MASTER (DirNode) == osb->node_num) &&
		    (DISK_LOCK_FILE_LOCK (DirNode) ==
		     OCFS_DLM_ENABLE_CACHE_LOCK)) {
			DISK_LOCK_CURRENT_MASTER (pNewDirNode) = osb->node_num;
			DISK_LOCK_FILE_LOCK (pNewDirNode) =
			    OCFS_DLM_ENABLE_CACHE_LOCK;
		}

		status = ocfs_insert_dir_node (osb, pNewDirNode, InsertEntry, LockNode,
					&indexOffset);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto leave;
		}

		if (LockNode->node_disk_off == DirNode->node_disk_off) {
			DirNode->free_node_ptr = pNewDirNode->node_disk_off;
		} else {
			LockNode->free_node_ptr = pNewDirNode->node_disk_off;
		}

		/* Insert in this dirnode and setup the pointers */
		DirNode->next_node_ptr = pNewDirNode->node_disk_off;

		status = ocfs_write_dir_node (osb, pNewDirNode, indexOffset);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto leave;
		}
		indexOffset = -1;
	}

	if (LockNode->node_disk_off == DirNode->node_disk_off) {
		if (DISK_LOCK_FILE_LOCK (DirNode) != OCFS_DLM_ENABLE_CACHE_LOCK) {
			ocfs_acquire_lockres (LockResource);
			LockResource->lock_type = OCFS_DLM_NO_LOCK;
			ocfs_release_lockres (LockResource);
			/* Reset the lock on the disk */
			DISK_LOCK_FILE_LOCK (DirNode) = OCFS_DLM_NO_LOCK;
		}
	} else {
		if (DISK_LOCK_FILE_LOCK (LockNode) != OCFS_DLM_ENABLE_CACHE_LOCK) {
			ocfs_acquire_lockres (LockResource);
			LockResource->lock_type = OCFS_DLM_NO_LOCK;
			ocfs_release_lockres (LockResource);
			/* Reset the lock on the disk */
			DISK_LOCK_FILE_LOCK (LockNode) = OCFS_DLM_NO_LOCK;
		}
	}

	status = ocfs_write_dir_node (osb, DirNode, indexOffset);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	if (LockNode->node_disk_off != DirNode->node_disk_off) {
		if ((DISK_LOCK_CURRENT_MASTER (LockNode) == osb->node_num) &&
		    (DISK_LOCK_FILE_LOCK (LockNode) ==
		     OCFS_DLM_ENABLE_CACHE_LOCK))
			status =
			    ocfs_write_metadata (osb, LockNode, osb->sect_size,
					       LockNode->node_disk_off);
		else
			status =
			    ocfs_write_disk (osb, LockNode, osb->sect_size,
					   LockNode->node_disk_off);

		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto leave;
		}
	}
      leave:
	ocfs_release_dirnode (pNewDirNode);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_insert_file */


/*
 * ocfs_validate_dir_index()
 *
 */
int ocfs_validate_dir_index (ocfs_super *osb, ocfs_dir_node *dirnode)
{
	ocfs_file_entry *fe = NULL;
	int status = 0;
	__u8 i;
	__u8 offset;
	__u8 *ind = NULL;

	LOG_ENTRY_ARGS ("(osb=0x%p, dn=0x%p)\n", osb, dirnode);

	if ((ind = (__u8 *)ocfs_malloc (256)) == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto bail;
	}

	memset(ind, 0, 256);

	for (i = 0; i < dirnode->num_ent_used; ++i) {
		offset = dirnode->index[i];
		if (offset > 253 || ind[offset]) {
			status = -EBADSLT;
			break;
		} else
			ind[offset] = 1;

		fe = (ocfs_file_entry *) (FIRST_FILE_ENTRY (dirnode) +
					  (offset * OCFS_SECTOR_SIZE));

		if (!fe->sync_flags) {
			status = -EBADSLT;
			break;
		}
	}

	if (status == -EBADSLT)
		LOG_ERROR_ARGS ("corrupted index in dirnode=%u.%u",
			       	HILO(dirnode->node_disk_off));

bail:
	ocfs_safefree (ind);
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_validate_dir_index */


/*
 * ocfs_validate_num_del()
 *
 */
int ocfs_validate_num_del (ocfs_super *osb, ocfs_dir_node *dirnode)
{
	ocfs_file_entry *fe = NULL;
	int i;
	int j;
	int status = 0;
	__u8 offset;
	__u8 *ind = NULL;
	char tmpstr[3];
	__u64 tmpoff;

	LOG_ENTRY_ARGS ("(osb=0x%p, dn=0x%p)\n", osb, dirnode);

	if (!dirnode->num_del)
		goto bail;

	if ((ind = (__u8 *)ocfs_malloc (256)) == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto bail;
	}

	memset(ind, 0, 256);

	offset = dirnode->first_del;
	for (i = 0; i < dirnode->num_del; ++i) {
		if (offset > 253) {
			status = -EBADSLT;
			break;
		}

		/* check if offset is in index and hence invalid */
		for (j = 0; j < dirnode->num_ent_used; ++j) {
			if (dirnode->index[j] == offset) {
				status = -EBADSLT;
				break;
			}
		}

		/* check for circular list */
		if (ind[offset]) {
			status = -EBADSLT;
			break;
		} else
			ind[offset] = 1;

		fe = (ocfs_file_entry *) (FIRST_FILE_ENTRY (dirnode) +
					  (offset * OCFS_SECTOR_SIZE));

		/* file has to be deleted to be in the list */
		if (fe->sync_flags) {
			status = -EBADSLT;
			break;
		}

		offset = (__u8)fe->next_del;
	}

	if (status == -EBADSLT) {
		if (i) {
			strncpy (tmpstr, "fe", sizeof(tmpstr));
			tmpoff = fe->this_sector;
		} else {
			strncpy (tmpstr, "dn", sizeof(tmpstr));
			tmpoff = dirnode->node_disk_off;
		}

		LOG_ERROR_ARGS ("bad offset=%u in %s=%u.%u", offset, tmpstr,
			       	HILO(tmpoff));
	}

bail:
	ocfs_safefree (ind);
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_validate_num_del */
