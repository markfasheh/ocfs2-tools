/*
 * ocfsgenalloc.c
 *
 * Allocate and free file system structures.
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
#define OCFS_DEBUG_CONTEXT    OCFS_DEBUG_CONTEXT_EXTENT



/*
 * ocfs_read_file_entry()
 *
 * This function reads the File Entry from the disk.
 *
 * Returns 0 on success, < 0 on error
 */
int ocfs_read_file_entry (ocfs_super * osb, ocfs_file_entry * FileEntry,
		   __u64 DiskOffset)
{
	int status = 0;

	LOG_ENTRY_ARGS ("(osb=%p, fileentry=%p, offset=%u.%u)\n", osb, FileEntry,
			HI (DiskOffset), LO (DiskOffset));

	OCFS_ASSERT (FileEntry);
	OCFS_ASSERT (osb);

	/* Size of File Entry is one sector */
	status =
	    ocfs_read_metadata (osb, FileEntry, (__u32) osb->sect_size, DiskOffset);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
	}

	LOG_EXIT_STATUS (status);
	return (status);
}				/* ocfs_read_file_entry */


/*
 * ocfs_write_file_entry()
 *
 * This function writes the File Entry to the disk.
 *
 * Returns 0 on success, < 0 on error
 */
int ocfs_write_file_entry (ocfs_super * osb, ocfs_file_entry * FileEntry, __u64 Offset)
{
	int status = 0;

	LOG_ENTRY ();

	OCFS_ASSERT (FileEntry);
	OCFS_ASSERT (osb);

	LOG_TRACE_ARGS ("File offset on the disk is %u.%u\n", HI (Offset),
			LO (Offset));

	/* size of File Entry is one sector */
	if ((DISK_LOCK_FILE_LOCK (FileEntry) == OCFS_DLM_ENABLE_CACHE_LOCK) &&
	    (DISK_LOCK_CURRENT_MASTER (FileEntry) == osb->node_num) &&
	    (Offset >= osb->vol_layout.bitmap_off)) {
		status =
		    ocfs_write_metadata (osb, FileEntry, (__u32) osb->sect_size,
				       Offset);
	} else {
		status =
		    ocfs_write_disk (osb, FileEntry, (__u32) osb->sect_size,
				   Offset);
	}

	if (status < 0) {
		LOG_ERROR_STATUS (status);
	}

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_write_file_entry */

/*
 * ocfs_remove_extent_map_entry()
 *
 * Remove an entry from the extent map
 */
void ocfs_remove_extent_map_entry (ocfs_super * osb,
			  ocfs_extent_map * Map, __s64 Vbo, __u32 ByteCount)
{
	LOG_ENTRY ();

	if ((ByteCount) && (ByteCount != 0xFFFFFFFF)) {
		ByteCount--;
		ByteCount >>= OCFS_LOG_SECTOR_SIZE;
		ByteCount++;
	}

	Vbo >>= OCFS_LOG_SECTOR_SIZE;

	ocfs_extent_map_remove ((ocfs_extent_map *) Map, (__s64) Vbo,
			     (__s64) ByteCount);

	LOG_EXIT ();
	return;
}				/* ocfs_remove_extent_map_entry */

/* ocfs_allocate_new_data_node()
 *
 */
int ocfs_allocate_new_data_node (ocfs_super * osb,
		     ocfs_file_entry * FileEntry,
		     __u64 actualDiskOffset,
		     __u64 actualLength,
		     ocfs_extent_group * ExtentHeader, __u64 * NewExtentOffset)
{
	int status = 0;
	__u8 *tempBuf = NULL;
	__u32 length;
	__u32 k, i;
	__u32 depth;
	ocfs_extent_group *IterExtentHeader = NULL, *IterExtent;

	__u32 allocSize;
	__u64 upHeaderPtr;
	__u64 physicalOffset;
	__u64 fileOffset = 0;
	__u64 numSectorsAlloc = 0;
	__u64 lastExtPointer;

	LOG_ENTRY ();

	if (ExtentHeader != NULL) {
		allocSize = (__u32) ((NUM_SECTORS_IN_LEAF_NODE +
				    ExtentHeader->granularity) *
				   OCFS_SECTOR_SIZE);

		/* allocate contiguous blocks on disk */
		status = ocfs_alloc_node_block (osb, allocSize, &physicalOffset,
					 &fileOffset, (__u64 *) & numSectorsAlloc,
					 osb->node_num, DISK_ALLOC_EXTENT_NODE);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}

		if (fileOffset == 0) {
			LOG_ERROR_ARGS ("offset=0, file=%s", FileEntry->filename);
		}

		k = ExtentHeader->next_free_ext;
		ExtentHeader->extents[k].file_off = FileEntry->alloc_size;
		ExtentHeader->extents[k].num_bytes = actualLength;
		ExtentHeader->extents[k].disk_off = physicalOffset;
		ExtentHeader->next_free_ext++;
		depth = ExtentHeader->granularity;
		upHeaderPtr = ExtentHeader->this_ext;
	} else {
		allocSize =
		    ((NUM_SECTORS_IN_LEAF_NODE +
		      FileEntry->granularity) * OCFS_SECTOR_SIZE);

		/* Allocate contiguous blocks on disk */
		status = ocfs_alloc_node_block (osb, allocSize, &physicalOffset,
					 &fileOffset, (__u64 *) & numSectorsAlloc,
					 osb->node_num, DISK_ALLOC_EXTENT_NODE);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}

		if (fileOffset == 0) {
			LOG_ERROR_ARGS ("offset=0, file=%s", FileEntry->filename);
		}

		k = FileEntry->next_free_ext;
		FileEntry->extents[k].file_off = FileEntry->alloc_size;
		FileEntry->extents[k].num_bytes = actualLength;
		FileEntry->extents[k].disk_off = physicalOffset;
		FileEntry->next_free_ext++;
		depth = FileEntry->granularity;
		upHeaderPtr = FileEntry->this_sector;
	}

	/* Common code between grow and this func. */

	length = (__u32) OCFS_ALIGN (allocSize, osb->sect_size);
	tempBuf = ocfs_malloc (length);
	if (tempBuf == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto finally;
	}

	memset (tempBuf, 0, length);

	lastExtPointer = FileEntry->last_ext_ptr;

	/* Fill in all the headers on the way to the leaf node */
	for (i = 0; i < depth; i++) {
		IterExtentHeader = (ocfs_extent_group *)
			           (tempBuf + (OCFS_SECTOR_SIZE * i));

		IterExtentHeader->last_ext_ptr = lastExtPointer;
		IterExtentHeader->up_hdr_node_ptr = upHeaderPtr;

		strcpy (IterExtentHeader->signature,
			OCFS_EXTENT_HEADER_SIGNATURE);

		IterExtentHeader->type = OCFS_EXTENT_HEADER;
		IterExtentHeader->granularity = (depth - 1 - i);
		IterExtentHeader->extents[0].disk_off =
		    (__u64) (physicalOffset + (OCFS_SECTOR_SIZE * (i + 1)));
		IterExtentHeader->extents[0].file_off = FileEntry->alloc_size;
		IterExtentHeader->extents[0].num_bytes = actualLength;
		IterExtentHeader->next_free_ext = 1;
		IterExtentHeader->alloc_file_off =
		    fileOffset + (OCFS_SECTOR_SIZE * i);
		IterExtentHeader->alloc_node = osb->node_num;
		IterExtentHeader->this_ext =
		    (__u64) (physicalOffset + (OCFS_SECTOR_SIZE * i));

		upHeaderPtr = IterExtentHeader->this_ext;
		lastExtPointer = IterExtentHeader->this_ext;
	}

	/* Fill in the leaf branch of the extent tree */
	IterExtent = (ocfs_extent_group *)
		     (tempBuf + (OCFS_SECTOR_SIZE * depth));
	IterExtent->this_ext = (__u64) (physicalOffset +
				      (OCFS_SECTOR_SIZE * depth));
	IterExtent->last_ext_ptr = lastExtPointer;
	IterExtent->up_hdr_node_ptr = upHeaderPtr;
	(*NewExtentOffset) = IterExtent->this_ext;

	if ((depth) &&
	    (IterExtent->this_ext != IterExtentHeader->extents[0].disk_off)) {
		LOG_ERROR_ARGS ("depth=%d, this_ext=%u.%u, disk_off=%u.%u",
				depth, HI(IterExtent->this_ext),
				LO(IterExtent->this_ext),
				HI(IterExtentHeader->extents[0].disk_off),
				LO(IterExtentHeader->extents[0].disk_off));
	}

	strcpy (IterExtent->signature, OCFS_EXTENT_DATA_SIGNATURE);

	IterExtent->extents[0].file_off = FileEntry->alloc_size;
	IterExtent->extents[0].num_bytes = actualLength;
	IterExtent->extents[0].disk_off = actualDiskOffset;
	IterExtent->curr_sect = 1;
	IterExtent->max_sects = NUM_SECTORS_IN_LEAF_NODE;
	IterExtent->next_free_ext = 1;
	IterExtent->type = OCFS_EXTENT_DATA;
	IterExtent->alloc_file_off = fileOffset + (OCFS_SECTOR_SIZE * depth);
	IterExtent->alloc_node = osb->node_num;

	FileEntry->last_ext_ptr = IterExtent->this_ext;

	/* Write the extents to disk */
	status = ocfs_write_disk (osb, tempBuf, allocSize, physicalOffset);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	if (ExtentHeader != NULL) {
		/* This has to be in the end... */
		status = ocfs_write_disk (osb, ExtentHeader, OCFS_SECTOR_SIZE,
					ExtentHeader->this_ext);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}

		for (i = ExtentHeader->granularity + 1;
		     i < FileEntry->granularity; i++) {
			status =
			    ocfs_read_extent (osb, ExtentHeader,
					    ExtentHeader->up_hdr_node_ptr,
					    EXTENT_HEADER);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto finally;
			}

			if (ExtentHeader->next_free_ext == 0) {
				LOG_ERROR_STATUS (status = -EFAIL);
				goto finally;
			}

			k = ExtentHeader->next_free_ext - 1;

			ExtentHeader->extents[k].num_bytes += actualLength;

			status = ocfs_write_sector (osb, ExtentHeader,
						  ExtentHeader->this_ext);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto finally;
			}
		}
		k = FileEntry->next_free_ext - 1;
		FileEntry->extents[k].num_bytes += actualLength;
	}
      finally:
	ocfs_safefree (tempBuf);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_allocate_new_data_node */

/* ocfs_add_to_last_data_node()
 *
 */
int ocfs_add_to_last_data_node (ocfs_super * osb,
		   ocfs_inode * oin,
		   ocfs_file_entry * FileEntry,
		   __u64 actualDiskOffset,
		   __u64 actualLength, __u32 * ExtentIndex, bool * IncreaseDepth)
{
	int status = 0;
	__u32 k = 0, i;
	__u32 length;
	__u8 *buffer = NULL;
	ocfs_extent_group *OcfsExtent = NULL, *OcfsExtentHeader = NULL;
	ocfs_extent_group *AllocExtentBuf = NULL;
	bool UpdateParent = true;
	__u64 newExtentOff;

	LOG_ENTRY ();

	*IncreaseDepth = false;
	length = (__u32) OCFS_ALIGN (sizeof (ocfs_extent_group), osb->sect_size);
	buffer = ocfs_malloc (length);
	if (buffer == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto bail;
	}

	status = ocfs_read_extent (osb, buffer, FileEntry->last_ext_ptr,
				   EXTENT_DATA);
	if (status < 0) {
		LOG_ERROR_STATUS (status = -EINVAL);
		goto bail;
	}

	OcfsExtent = (ocfs_extent_group *) buffer;

	/* Read the last enxtent and keep traversing upward till we find a */
	/* free extent or we are at the top and need to create another level. */
	if (OcfsExtent->next_free_ext > OCFS_MAX_DATA_EXTENTS) {
		LOG_ERROR_STATUS(status = -EINVAL);
		goto bail;
	}

	k = OcfsExtent->next_free_ext - 1;

	LOG_TRACE_ARGS ("Using local_ext for extent Entry = %u\n", k);

	/* Check here a case where the new allocation can be joined with */
	/* the last Extent */
	if (OcfsExtent->next_free_ext >= 1) {
		if (OCFS_EXTENT_MERGEABLE
		    (&(OcfsExtent->extents[k]), actualDiskOffset)) {
			/* We can join the extents, just increase the len of extent */
			OcfsExtent->extents[k].num_bytes += actualLength;
			status = 0;
			goto bail;
		}
	} else {
		LOG_ERROR_ARGS ("next_free_ext=%d", OcfsExtent->next_free_ext);
	}

	/* We cannot merge give him the next extent */
	k = OcfsExtent->next_free_ext;

	if (k == OCFS_MAX_DATA_EXTENTS) {
		__u64 up_hdr_node_ptr = 0;

		if (FileEntry->granularity == 0) {
			if (FileEntry->next_free_ext ==
			    OCFS_MAX_FILE_ENTRY_EXTENTS) {
				(*IncreaseDepth) = true;
				goto bail;
			} else {
				status = ocfs_allocate_new_data_node (osb, FileEntry,
						     actualDiskOffset,
						     actualLength, NULL,
						     &newExtentOff);
				if (status < 0) {
					LOG_ERROR_STATUS (status);
					goto bail;
				}
				OcfsExtent->next_data_ext = newExtentOff;
				UpdateParent = false;
				FileEntry->last_ext_ptr = newExtentOff;
				status =
				    ocfs_write_sector (osb, OcfsExtent,
						     OcfsExtent->this_ext);
				if (status < 0) {
					LOG_ERROR_STATUS (status);
					goto bail;
				}
			}
		} else {
			i = 0;

			length =
			    (__u32) OCFS_ALIGN (sizeof (ocfs_extent_group),
						osb->sect_size);
			AllocExtentBuf = OcfsExtentHeader =
			    ocfs_malloc (length);
			if (OcfsExtentHeader == NULL) {
				LOG_ERROR_STATUS (status = -ENOMEM);
				goto bail;
			}

			up_hdr_node_ptr = OcfsExtent->up_hdr_node_ptr;

			for (i = 0; i < FileEntry->granularity; i++) {
				memset (OcfsExtentHeader, 0,
					sizeof (ocfs_extent_group));

				status =
				    ocfs_read_extent (osb, OcfsExtentHeader,
						    up_hdr_node_ptr, EXTENT_HEADER);
				if (status < 0) {
					LOG_ERROR_STATUS (status);
					goto bail;
				}

				if (OcfsExtentHeader->granularity != i) {
					LOG_ERROR_STATUS(status = -EINVAL);
					goto bail;
				}

				if (OcfsExtentHeader->next_free_ext >
				    OCFS_MAX_DATA_EXTENTS) {
					LOG_ERROR_STATUS(status = -EINVAL);
					goto bail;
				}

				if (OcfsExtentHeader->next_free_ext ==
				    OCFS_MAX_DATA_EXTENTS) {
					up_hdr_node_ptr =
					    OcfsExtentHeader->up_hdr_node_ptr;
					continue;
				} else {
					break;
				}
			}

			if (i == FileEntry->granularity) {
				if (FileEntry->next_free_ext ==
				    OCFS_MAX_FILE_ENTRY_EXTENTS) {
					(*IncreaseDepth) = true;
					goto bail;
				} else {
					status = ocfs_allocate_new_data_node (osb, FileEntry,
							     actualDiskOffset,
							     actualLength, NULL,
							     &newExtentOff);
					if (status < 0) {
						LOG_ERROR_STATUS (status);
						goto bail;
					}
					OcfsExtent->next_data_ext =
					    newExtentOff;
					UpdateParent = false;
					FileEntry->last_ext_ptr = newExtentOff;
					status =
					    ocfs_write_sector (osb, OcfsExtent,
							     OcfsExtent->
							     this_ext);
					if (status < 0) {
						LOG_ERROR_STATUS (status);
						goto bail;
					}
				}
				goto bail;
			} else {
				status = ocfs_allocate_new_data_node (osb, FileEntry,
						     actualDiskOffset,
						     actualLength,
						     OcfsExtentHeader,
						     &newExtentOff);
				if (status < 0) {
					LOG_ERROR_STATUS (status);
					goto bail;
				}
				OcfsExtent->next_data_ext = newExtentOff;
				UpdateParent = false;
				FileEntry->last_ext_ptr = newExtentOff;
				status =
				    ocfs_write_sector (osb, OcfsExtent,
						     OcfsExtent->this_ext);
				if(status < 0) {
					LOG_ERROR_STATUS (status);
					goto bail;
				}
			}
		}
	} else {
		/* FileOffset for the new Extent will be equal to the previous */
		/* allocation size of file */
		OcfsExtent->extents[k].file_off = FileEntry->alloc_size;
		OcfsExtent->extents[k].num_bytes = actualLength;
		OcfsExtent->extents[k].disk_off = actualDiskOffset;
		OcfsExtent->next_free_ext++;
	}

      bail:
	if (status >= 0 && !(*IncreaseDepth) && UpdateParent) {
		status =
		    ocfs_write_sector (osb, OcfsExtent, OcfsExtent->this_ext);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}

		for (i = 0; i < FileEntry->granularity; i++) {
			OcfsExtentHeader = (ocfs_extent_group *) buffer;
			status = ocfs_read_extent (osb, buffer,
						 OcfsExtentHeader->up_hdr_node_ptr,
						 EXTENT_HEADER);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto finally;
			}

			OcfsExtentHeader = (ocfs_extent_group *) buffer;

			if (OcfsExtentHeader->next_free_ext == 0) {
				LOG_ERROR_STATUS (status = -EFAIL);
				goto finally;
			}

			k = OcfsExtentHeader->next_free_ext - 1;

			OcfsExtentHeader->extents[k].num_bytes +=
			    actualLength;

			status = ocfs_write_sector (osb, OcfsExtentHeader,
						  OcfsExtentHeader->this_ext);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto finally;
			}
		}

		k = FileEntry->next_free_ext - 1;

		FileEntry->extents[k].num_bytes += actualLength;
	}

      finally:
	ocfs_safefree (buffer);
	ocfs_safefree (AllocExtentBuf);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_add_to_last_data_node */

/* ocfs_update_last_data_extent()
 *
 */
int ocfs_update_last_data_extent (ocfs_super * osb,
		      ocfs_file_entry * FileEntry, __u64 NextDataOffset)
{
	int status = 0;
	__u32 length = 0;
	__u8 *buffer = NULL;
	ocfs_extent_group *OcfsExtent;

	LOG_ENTRY ();

	length = (__u32) OCFS_ALIGN (sizeof (ocfs_extent_group), osb->sect_size);
	buffer = ocfs_malloc (length);
	if (buffer == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto finally;
	}

	status = ocfs_read_extent (osb, buffer, FileEntry->last_ext_ptr,
				   EXTENT_DATA);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	OcfsExtent = (ocfs_extent_group *) buffer;

	if (OcfsExtent->next_data_ext != 0) {
		LOG_ERROR_ARGS ("fe->last_ext_ptr=%u.%u, next_data_ext=%u.%u",
				HI(FileEntry->last_ext_ptr),
				LO(FileEntry->last_ext_ptr),
				HI(OcfsExtent->next_data_ext),
				LO(OcfsExtent->next_data_ext));
	}

	OcfsExtent->next_data_ext = NextDataOffset;

	status = ocfs_write_sector (osb, buffer, FileEntry->last_ext_ptr);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

      finally:
	ocfs_safefree (buffer);

	LOG_EXIT_STATUS (status);
	return (status);
}				/* ocfs_update_last_data_extent */


/*
 * ocfs_update_uphdrptr()
 *
 */
int ocfs_update_uphdrptr(ocfs_super *osb, ocfs_file_entry *fe,
			 __u64 new_up_hdr_ptr)
{
	int status = 0;
	int len;
	int i;
	__u8 *buffer = NULL;
	ocfs_extent_group *extent;
	__u64 offset;

	LOG_ENTRY ();

	len = OCFS_ALIGN (sizeof (ocfs_extent_group), osb->sect_size);
	buffer = ocfs_malloc (len);
	if (buffer == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto bail;
	}

	for (i = 0; i < OCFS_MAX_FILE_ENTRY_EXTENTS; ++i) {
		offset = fe->extents[i].disk_off;

		status = ocfs_read_sector (osb, buffer, offset);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto bail;
		}

		extent = (ocfs_extent_group *) buffer;

		if (extent->up_hdr_node_ptr != fe->this_sector)
			LOG_ERROR_ARGS ("fe->this_sector=%u.%u, uphdrptr=%u.%u",
					HI(fe->this_sector),
					LO(fe->this_sector),
					HI(extent->up_hdr_node_ptr),
					LO(extent->up_hdr_node_ptr));

		extent->up_hdr_node_ptr = new_up_hdr_ptr;

		status = ocfs_write_sector (osb, buffer, offset);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto bail;
		}
	}

      bail:
	ocfs_safefree (buffer);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_update_uphdrptr */


/* ocfs_grow_extent_tree()
 *
 */
int ocfs_grow_extent_tree (ocfs_super * osb,
		ocfs_file_entry * FileEntry,
		__u64 actualDiskOffset, __u64 actualLength)
{
	int status = 0;
	__s32 k, i;
	__u32 length = 0;
	__u32 numSectorsAlloc = 0;
	__u8 *buffer = NULL;
	ocfs_extent_group *OcfsExtent = NULL;
	ocfs_extent_group *ExtentHeader = NULL;
	__u64 physicalOffset;
	__u64 fileOffset = 0;
	__u64 upHeaderPtr, lastExtentPtr;
	__u32 AllocSize;
	__u64 new_up_hdr_ptr = 0;

	LOG_ENTRY ();

	AllocSize = ((FileEntry->granularity + 2) * OCFS_SECTOR_SIZE);

	/* Allocate the space from the Extent file. This function should */
	/* return contigous disk blocks requested. */
	status = ocfs_alloc_node_block (osb, AllocSize, &physicalOffset,
				 &fileOffset, (__u64 *) & numSectorsAlloc,
				 osb->node_num, DISK_ALLOC_EXTENT_NODE);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	if (fileOffset == 0) {
		LOG_TRACE_ARGS ("offset=0, file=%s\n", FileEntry->filename);
	}

	if (physicalOffset == 0) {
		LOG_ERROR_STATUS(status = -ENOMEM);
		goto finally;
	}

	length = (__u32) OCFS_ALIGN (AllocSize, osb->sect_size);
	buffer = ocfs_malloc (length);
	if (buffer == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto finally;
	}

	memset (buffer, 0, length);

	OcfsExtent = (ocfs_extent_group *) buffer;

	/* Copy the File Entry information in to the newly allocated sector */
	for (k = 0; k < OCFS_MAX_FILE_ENTRY_EXTENTS; k++) {
		OcfsExtent->extents[k].file_off =
		    FileEntry->extents[k].file_off;
		OcfsExtent->extents[k].num_bytes =
		    FileEntry->extents[k].num_bytes;
		OcfsExtent->extents[k].disk_off =
		    FileEntry->extents[k].disk_off;
	}

	OcfsExtent->last_ext_ptr = FileEntry->last_ext_ptr;

	lastExtentPtr = FileEntry->last_ext_ptr;

	OcfsExtent->this_ext = new_up_hdr_ptr = physicalOffset;
	OcfsExtent->alloc_file_off = fileOffset;
	OcfsExtent->alloc_node = osb->node_num;
	OcfsExtent->next_data_ext = 0;

	FileEntry->local_ext = false;
	FileEntry->granularity++;

	LOG_TRACE_ARGS ("Granularity is: %d\n", FileEntry->granularity);

	/* If granularity is zero now, the for loop will not execute. */
	/* First time a file is created ,granularity = -1 and local_ext flag */
	/* is set to true */

	upHeaderPtr = FileEntry->this_sector;

	for (i = 0; i < FileEntry->granularity; i++) {
		ExtentHeader =
		    (ocfs_extent_group *) (buffer + (OCFS_SECTOR_SIZE * i));
		ExtentHeader->type = OCFS_EXTENT_HEADER;
		ExtentHeader->granularity = (FileEntry->granularity - 1) - i;

		strcpy (ExtentHeader->signature, OCFS_EXTENT_HEADER_SIGNATURE);

		if (i == 0) {
			ExtentHeader->extents[OCFS_MAX_FILE_ENTRY_EXTENTS].
			    disk_off = physicalOffset + OCFS_SECTOR_SIZE;
			ExtentHeader->extents[OCFS_MAX_FILE_ENTRY_EXTENTS].
			    file_off = FileEntry->alloc_size;
			ExtentHeader->extents[OCFS_MAX_FILE_ENTRY_EXTENTS].
			    num_bytes = actualLength;

			ExtentHeader->next_free_ext =
			    OCFS_MAX_FILE_ENTRY_EXTENTS + 1;
			ExtentHeader->this_ext = physicalOffset;
			ExtentHeader->last_ext_ptr = lastExtentPtr;
			ExtentHeader->up_hdr_node_ptr = upHeaderPtr;

			upHeaderPtr = ExtentHeader->this_ext;
			lastExtentPtr = ExtentHeader->this_ext;
		} else {
			ExtentHeader->extents[0].disk_off =
			    physicalOffset + (OCFS_SECTOR_SIZE * (i + 1));
			ExtentHeader->extents[0].file_off =
			    FileEntry->alloc_size;
			ExtentHeader->extents[0].num_bytes = actualLength;
			ExtentHeader->next_free_ext = 1;
			ExtentHeader->alloc_file_off =
			    fileOffset + (OCFS_SECTOR_SIZE * i);
			ExtentHeader->alloc_node = osb->node_num;
			ExtentHeader->this_ext =
			    physicalOffset + (OCFS_SECTOR_SIZE * i);
			ExtentHeader->up_hdr_node_ptr = upHeaderPtr;
			ExtentHeader->last_ext_ptr = lastExtentPtr;

			upHeaderPtr = ExtentHeader->this_ext;
			lastExtentPtr = ExtentHeader->this_ext;
		}
	}

	/* Update the Data Segment */
	OcfsExtent = (ocfs_extent_group *) (buffer + (OCFS_SECTOR_SIZE *
						      FileEntry->granularity));

	i = (FileEntry->granularity) ? 0 : OCFS_MAX_FILE_ENTRY_EXTENTS;

	LOG_TRACE_ARGS ("EntryAvailable is: %d\n", OcfsExtent->next_free_ext);

	/* For the time being we are assuming that the newly allocated Extent */
	/* will have one more entry to accomodate the latest allocation */

	strcpy (OcfsExtent->signature, OCFS_EXTENT_DATA_SIGNATURE);

	OcfsExtent->extents[i].file_off = FileEntry->alloc_size;
	OcfsExtent->extents[i].num_bytes = actualLength;
	OcfsExtent->extents[i].disk_off = actualDiskOffset;
	OcfsExtent->curr_sect = 1;
	OcfsExtent->max_sects = NUM_SECTORS_IN_LEAF_NODE;
	OcfsExtent->type = OCFS_EXTENT_DATA;
	OcfsExtent->next_free_ext = i + 1;
	OcfsExtent->alloc_file_off =
	    fileOffset + (FileEntry->granularity * OCFS_SECTOR_SIZE);
	OcfsExtent->alloc_node = osb->node_num;
	OcfsExtent->this_ext =
	    physicalOffset + (FileEntry->granularity * OCFS_SECTOR_SIZE);
	OcfsExtent->up_hdr_node_ptr = upHeaderPtr;
	OcfsExtent->last_ext_ptr = lastExtentPtr;
	OcfsExtent->next_data_ext = 0;

	upHeaderPtr = OcfsExtent->this_ext;
	lastExtentPtr = OcfsExtent->this_ext;

	/* We assume that the AllocSize passed in is Sector aligned */

	status = ocfs_write_disk (osb, buffer, AllocSize, physicalOffset);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	/* Update the Previous Last Data Extent with this new Data Extent Pointer */
	if (FileEntry->last_ext_ptr != 0) {
		status =
		    ocfs_update_last_data_extent (osb, FileEntry, OcfsExtent->this_ext);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}
	}

	/* Update the uphdrptr of the three extents pointed to by fe */
	if (FileEntry->granularity > 0) {
		status = ocfs_update_uphdrptr(osb, FileEntry, new_up_hdr_ptr);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}
	}

	/* Clear all the extent information from File Entry */
	for (i = 0; i < OCFS_MAX_FILE_ENTRY_EXTENTS; i++) {
		FileEntry->extents[i].file_off = 0;
		FileEntry->extents[i].num_bytes = 0;
		FileEntry->extents[i].disk_off = 0;
	}

	/* Update the File Entry Extent */
	FileEntry->local_ext = false;

	FileEntry->extents[0].file_off = 0;
	FileEntry->extents[0].num_bytes = FileEntry->alloc_size +
	    actualLength;
	FileEntry->extents[0].disk_off = physicalOffset;
	FileEntry->last_ext_ptr = lastExtentPtr;
	FileEntry->next_free_ext = 1;

      finally:
	ocfs_safefree (buffer);

	LOG_EXIT_STATUS (status);
	return (status);
}				/* ocfs_grow_extent_tree */

/*
 * ocfs_allocate_extent()
 *
 */
int ocfs_allocate_extent (ocfs_super * osb,
		    ocfs_inode * oin,
		    ocfs_file_entry * FileEntry,
		    __u64 actualDiskOffset, __u64 actualLength)
{
	int status = 0;
	bool IncreaseTreeDepth = false;
	__u32 k = 0;

	LOG_ENTRY ();

	/* Don't do an Assertion on oin as it can NULL also in some cases. */
	OCFS_ASSERT (FileEntry);

	if (!IS_VALID_FILE_ENTRY (FileEntry)) {
		LOG_ERROR_STATUS(status = -EINVAL);
		goto finally;
	}

	if (FileEntry->local_ext) {
		/* We are still using the local extents of File Entry */
		if (FileEntry->next_free_ext > OCFS_MAX_FILE_ENTRY_EXTENTS) {
			LOG_ERROR_STATUS(status = -EINVAL);
			goto finally;
		}

		if (FileEntry->next_free_ext >= 1) {
			k = FileEntry->next_free_ext - 1;

			LOG_TRACE_ARGS
			    ("Using local_ext for extent Entry = %u\n", k);

			/* Check here a case where the new allocation can be */
			/* joined with the last extent. */
			if (OCFS_EXTENT_MERGEABLE
			    (&FileEntry->extents[k], actualDiskOffset)) {
				/* We can join the extents, just increase the len of extent */
				FileEntry->extents[k].num_bytes += actualLength;
				status = 0;
				goto finally;
			}
		}

		/* We cannot merge give him the next extent */
		k = FileEntry->next_free_ext;
		if (k == OCFS_MAX_FILE_ENTRY_EXTENTS) {
			IncreaseTreeDepth = true;
		} else {
			/* file_off for the new extent will be equal to the previous */
			/* allocation size of file */
			FileEntry->extents[k].file_off = FileEntry->alloc_size;
			FileEntry->extents[k].num_bytes = actualLength;
			FileEntry->extents[k].disk_off = actualDiskOffset;
			FileEntry->next_free_ext++;

			status = 0;
			goto finally;
		}
	} else {
		if (FileEntry->granularity > 3)
			LOG_ERROR_ARGS ("granularity=%d", FileEntry->granularity);

		/* This File is no longer using Local Extents */
		status = ocfs_add_to_last_data_node (osb, oin, FileEntry,
						     actualDiskOffset, actualLength,
						     &k, &IncreaseTreeDepth);
		if (status < 0) {
			LOG_ERROR_STATUS(status);
			goto finally;
		}
	}

	if (IncreaseTreeDepth) {
		status = ocfs_grow_extent_tree (osb, FileEntry,
						actualDiskOffset, actualLength);
		if (status < 0) {
			LOG_ERROR_STATUS(status);
			goto finally;
		}
	}

      finally:
	if ((status == 0) && (oin != NULL)) {
		__s64 Vbo = 0;
		__s64 Lbo = 0;

		/* Add this Entry in to extent map. If a new mapping run to be added */
		/* overlaps an existing mapping run, ocfs_add_extent_map_entry merges */
		/* them into a single mapping run.So just adding this entry will be fine. */
		Vbo = FileEntry->alloc_size;
		Lbo = actualDiskOffset;

		/* Add the Entry to the extent map list */
		if (!ocfs_add_extent_map_entry (osb, &oin->map, Vbo, Lbo,
						actualLength))
			LOG_ERROR_STATUS (status = -EFAIL);
	}

	/* ?? We should update the Filesize and allocation size here */

	LOG_EXIT_STATUS (status);
	return (status);
}				/* ocfs_allocate_extent */

#if 0
/*
 * ocfs_check_for_extent_merge()
 *
 * In order to optimize the usage of Extents Per file, ocfs tries to merge
 * contigous allocations of same file. This function is called whever a
 * new allocation is added in order to check if it is possible to merge
 * it with the previous allocation.
 *
 * Returns true if possible, false, if not.
 */
bool ocfs_check_for_extent_merge (ocfs_alloc_ext * LastExtent, __u64 ActualDiskOffset)
{
	bool ret = false;

	LOG_ENTRY ();

	/* Check here a case where the new allocation can be */
	/* joined with the last extent. */

	if ((LastExtent->disk_off +
	     LastExtent->num_bytes) == ActualDiskOffset) {
		ret = true;
	}

	LOG_EXIT_ULONG (ret);
	return ret;
}				/* ocfs_check_for_extent_merge */
#endif

/*
 * ocfs_get_next_extent_map_entry()
 *
 * This routine looks up the existing mapping of VBO to LBO for a  file.
 * The information it queries is either stored in the extent map field
 * of the oin or is stored in the allocation file and needs to be
 * retrieved, decoded and updated in the extent map.
 *
 */
bool ocfs_get_next_extent_map_entry (ocfs_super * osb,
			   ocfs_extent_map * Map,
			   __u32 RunIndex, __s64 * Vbo, __s64 * Lbo,
			   __u32 * SectorCount)
{
	bool Results;
	__s64 LiVbo;
	__s64 LiLbo;
	__s64 LiSectorCount;

	LOG_ENTRY ();

	LiLbo = 0;

	Results = ocfs_extent_map_next_entry (Map, RunIndex, &LiVbo, &LiLbo,
					  &LiSectorCount);

	*Vbo = ((__s64) LiVbo) << OCFS_LOG_SECTOR_SIZE;

	if (((__u32) LiLbo) != -1) {
		*Lbo = ((__s64) LiLbo) << OCFS_LOG_SECTOR_SIZE;
	} else {
		Results = false;
		*Lbo = 0;
	}

	*SectorCount = ((__u32) LiSectorCount) << OCFS_LOG_SECTOR_SIZE;

	if ((*SectorCount == 0) && (LiSectorCount != 0)) {
		*SectorCount = (__u32) - 1;	/* it overflowed */
		Results = false;
	}

	LOG_EXIT_ULONG (Results);
	return Results;
}				/* ocfs_get_next_extent_map_entry  */


/*
 * ocfs_update_all_headers()
 *
 */
int ocfs_update_all_headers (ocfs_super * osb, ocfs_extent_group * AllocExtent,
			     __u64 FileSize, ocfs_file_entry *fe)
{
	int status = 0;
	__u64 upHeaderPtr;
	__u32 i = 0;
	ocfs_extent_group *ExtentHeader = NULL;
	ocfs_file_entry *FileEntry = NULL;

	LOG_ENTRY ();

	status = ocfs_write_sector (osb, AllocExtent, AllocExtent->this_ext);
	if (status < 0) {
		LOG_ERROR_STATUS(status);
		goto finally;
	}

	ExtentHeader = AllocExtent;

	while (1) {
		upHeaderPtr = ExtentHeader->up_hdr_node_ptr;

		memset (ExtentHeader, 0, OCFS_SECTOR_SIZE);

		status = ocfs_read_sector (osb, ExtentHeader, upHeaderPtr);
		if (status < 0) {
			LOG_ERROR_STATUS(status = -EINVAL);
			goto finally;
		}

		if (IS_VALID_EXTENT_HEADER (ExtentHeader)) {
			for (i = 0; i < AllocExtent->next_free_ext; i++) {
				if ((AllocExtent->extents[i].file_off +
				     AllocExtent->extents[i].num_bytes) >
				    FileSize) {
					if (AllocExtent->extents[i].file_off >
					    FileSize) {
						AllocExtent->extents[i].
						    file_off =
						    AllocExtent->extents[i].
						    num_bytes = 0;
						AllocExtent->next_free_ext = i;
						break;
					} else {
						AllocExtent->extents[i].
						    num_bytes -=
						    ((AllocExtent->extents[i].
						      file_off +
						      AllocExtent->extents[i].
						      num_bytes) - FileSize);
						AllocExtent->next_free_ext =
						    i + 1;
						break;
					}
				}
			}
			status = ocfs_write_sector (osb, AllocExtent, AllocExtent->this_ext);
			continue;
		} else {
			FileEntry = fe;

			if (!IS_VALID_FILE_ENTRY (FileEntry)) {
				LOG_ERROR_STATUS(status = -EFAIL);
				goto finally;
			}

			for (i = 0; i < FileEntry->next_free_ext; i++) {
				if ((FileEntry->extents[i].file_off +
				     FileEntry->extents[i].num_bytes) >
				    FileSize) {
					if (FileEntry->extents[i].file_off >
					    FileSize) {
						FileEntry->extents[i].file_off =
						    FileEntry->extents[i].
						    num_bytes = 0;
						FileEntry->next_free_ext = i;
						break;
					} else {
						FileEntry->extents[i].
						    num_bytes -=
						    ((FileEntry->extents[i].
						      file_off +
						      FileEntry->extents[i].
						      num_bytes) - FileSize);
						FileEntry->next_free_ext =
						    i + 1;
						break;
					}
				}
			}
			break;
		}
	}

finally:
	LOG_EXIT_STATUS (status);
	return status;
}  /* ocfs_update_all_headers */


/*
 * _write_free_extent_log()
 *  removed most logging from this function as it overwhelms printk on redhat.
 */

int _write_free_extent_log(ocfs_super *osb, ocfs_cleanup_record *CleanupLogRec, 
                                  __u32 len, __u32 fileoff, __u32 nodenum, __u32 thistype)
{
        __u32 numUpdt;
        int status=0;
        ocfs_free_bitmap *fb;

//	LOG_ENTRY ();
//	LOG_TRACE_ARGS("len = %u, fileoff = %u, nodenum = %u\n", len, fileoff, nodenum);

	if (thistype == DISK_ALLOC_EXTENT_NODE)
	        LOG_TRACE_ARGS("Removing metadata at alloc_fileoff=%u, nodenum=%u\n", fileoff, nodenum);
        numUpdt = CleanupLogRec->rec.free.num_free_upds; 
        if (numUpdt >= FREE_LOG_SIZE) { 
                status = ocfs_write_node_log (osb, (ocfs_log_record *) CleanupLogRec, 
                                              osb->node_num, LOG_CLEANUP); 
                if (status < 0) { 
                        LOG_ERROR_STATUS (status);
                        return status;
                } 
                numUpdt = CleanupLogRec->rec.free.num_free_upds = 0; 
        }
        fb = &(CleanupLogRec->rec.free.free_bitmap[numUpdt]); 
        fb->length = len; 
        fb->file_off = fileoff; 
	//fb->type = DISK_ALLOC_VOLUME;
        fb->type = thistype; 
        fb->node_num = nodenum; 
        (CleanupLogRec->rec.free.num_free_upds)++; 

	// LOG_EXIT_STATUS (status);
        return status;
} 			/* _write_free_extent_log */

 
int _squish_extent_entries(ocfs_super *osb, ocfs_alloc_ext *extarr, __u8 *freeExtent, 
                                  ocfs_cleanup_record *CleanupLogRec, __u64 FileSize, bool flag) 
{
        int status = 0;
	bool FirstTime = true;
        ocfs_alloc_ext *ext; 
        __u32 i, csize = osb->vol_layout.cluster_size, 
            numBitsAllocated = 0, bitmapOffset = 0, 
            firstfree = *freeExtent;
        __u64 bytes, foff, doff, 
            dstart = osb->vol_layout.data_start_off, 
            diskOffsetTobeFreed, lengthTobeFreed = 0, 
            actualSize = 0, origLength = 0;

	LOG_ENTRY ();

        firstfree = *freeExtent;
        for (i = 0; i < firstfree; i++) { 
                ext = &(extarr[i]); 
                bytes = ext->num_bytes; 
                foff = ext->file_off; 
                doff = ext->disk_off; 
                actualSize = (bytes + foff);
                if (flag || actualSize > FileSize) { 
                        if (flag || foff >= FileSize) { 
                                if (!flag && FirstTime) { 
                                        *freeExtent = i; 
                                        FirstTime = false; 
                                } 
                                numBitsAllocated = (__u32) (bytes/csize); 
                                bitmapOffset = (__u32) ((doff - dstart) / csize); 
                                ext->num_bytes = ext->disk_off = ext->file_off = 0; 
                        } else { 
                                if (FirstTime) { 
                                        *freeExtent = i + 1; 
                                        FirstTime = false; 
                                } 
                                origLength = bytes; 
                                ext->num_bytes = bytes = FileSize - foff; 
                                lengthTobeFreed = origLength - bytes; 
                                if (lengthTobeFreed == 0) { 
                                        continue; 
                                } 
                                numBitsAllocated = (__u32) (lengthTobeFreed / csize); 
                                diskOffsetTobeFreed = doff + bytes; 
                                bitmapOffset = (__u32) ((diskOffsetTobeFreed - dstart) / csize); 
                        } 
                        status = _write_free_extent_log(osb, CleanupLogRec, numBitsAllocated, 
                                                        bitmapOffset, -1, DISK_ALLOC_VOLUME); 
                        if (status < 0) {
				LOG_ERROR_STATUS (status);
                                break;
			}
                }
        } 

	LOG_EXIT_STATUS (status);
        return status; 
}

/* used by ocfs_kill_this_tree and ocfs_split_this_tree */
/* This value needs to be removed in a future version and set to
 * granularity + 1, dynamically */
#define OCFS_TREE_STACK_SIZE 8

/*
 * ocfs_kill_this_tree
 *
 * Given an extent_group (can be a DAT or header), delete everything,
 * including itself, it's children, and any data blocks they point to.
 * Works fine with any granularity (up to 4, in which case we'd need
 * more stack space)
 */

/* We can't recurse, so we keep a simple stack of ocfs_extent_groups. */
int ocfs_kill_this_tree(ocfs_super *osb, ocfs_extent_group *extent_grp, ocfs_cleanup_record *CleanupLogRec) {
       int status = -EFAIL;
       int i;
       __u32 victim;
       __u32 size = OCFS_ALIGN (sizeof (ocfs_extent_group), osb->sect_size);
       __u32 csize = osb->vol_layout.cluster_size;
       __u64 dstart = osb->vol_layout.data_start_off;
       __u32 num_sectors = 0, bitmap_offset = 0;
       ocfs_alloc_ext *ext;
       ocfs_extent_group * grp_stack[OCFS_TREE_STACK_SIZE];
       ocfs_extent_group * AllocExtent; /* convenience, points to TOS */
       int tos = 0;

       LOG_ENTRY();

       for (i =0; i < OCFS_TREE_STACK_SIZE; i++)
               grp_stack[i] = NULL;

       grp_stack[tos] = extent_grp;

       do {
               AllocExtent = grp_stack[tos];

               if (!IS_VALID_EXTENT_DATA(AllocExtent) &&
                   !IS_VALID_EXTENT_HEADER(AllocExtent)) {
                       LOG_ERROR_STR("Invalid extent group!");
                       goto bail;
               }

               if (IS_VALID_EXTENT_DATA(AllocExtent)) {
                       LOG_TRACE_ARGS("found some data to free (%u.%u)\n", HI(AllocExtent->this_ext), LO(AllocExtent->this_ext));
                       for(i = 0; i < AllocExtent->next_free_ext; i++) {
                               /* Free the data associated with each header */
                               ext = &AllocExtent->extents[i];
                               num_sectors = (__u32) (ext->num_bytes / csize);
                               bitmap_offset = (__u32) ((ext->disk_off - dstart) / csize);
                               status = _write_free_extent_log(osb, CleanupLogRec, num_sectors, bitmap_offset, -1, DISK_ALLOC_VOLUME);
                               if (status < 0) {
                                       LOG_ERROR_STATUS (status);
                                       goto bail;
                               }
                       }
                       /* Pop one off the stack */
                       tos--;
               } else {
                       /* Ok, we're a header. */

                       /* Did we already kill all his children, or
                        * are they already dead? */
                       if (AllocExtent->next_free_ext == 0) {
                               tos--;
                               LOG_TRACE_ARGS("Popping this header (%u.%u)\n",
HI(AllocExtent->this_ext), LO(AllocExtent->this_ext), AllocExtent->next_free_ext);
                               goto free_meta;
                       }

                       /* We're gonna read in our last used extent
                        * and put him at the top of the stack. We
                        * also update our next_free_ext so that next
                        * time we read in the next to last one and so
                        * on until we've finished all of them
                        */

                       /* grow the stack, only allocating mem if we
                        * haven't already */
                       tos++;
                       if (grp_stack[tos] == NULL)
                               grp_stack[tos] = ocfs_malloc(size);
                       else
                               memset(grp_stack[tos], 0, size);
                       victim = AllocExtent->next_free_ext - 1;
                       ext = &AllocExtent->extents[victim];

                       status = ocfs_read_sector(osb, grp_stack[tos], ext->disk_off);
                       if (status < 0) {
                               LOG_ERROR_STATUS (status);
                               goto bail;
                       }
                       AllocExtent->next_free_ext--;
                       LOG_TRACE_ARGS("Pushing this header (%u.%u)\n", HI(grp_stack[tos]->this_ext), LO(grp_stack[tos]->this_ext));

                       /* We only want to free on our way up the tree */
                       continue;
               }

       free_meta:
               /* Free the metadata associated with this extent group */
               status = _write_free_extent_log(osb, CleanupLogRec, 1, AllocExtent->alloc_file_off, AllocExtent->alloc_node, DISK_ALLOC_EXTENT_NODE);
               if (status < 0) {
                       LOG_ERROR_STATUS (status);
                       goto bail;
               }
       } while (tos >= 0);

       status = 0;
bail:
       /* Free the stack. We never free the bottom of the stack
        * because we were passed that guy from the caller */
       for(i = 1; i < OCFS_TREE_STACK_SIZE; i++)
               ocfs_safefree(grp_stack[i]);

       LOG_EXIT_STATUS (status);
       return(status);
}


int ocfs_fix_extent_group(ocfs_super *osb, ocfs_extent_group *group) {
       ocfs_alloc_ext *ext;
       int status=-EFAIL;
       int i;

       LOG_ENTRY ();

       if (!group) {
               LOG_ERROR_STR("Invalid extent group (NULL)!");
               goto bail;
       }

       if (!IS_VALID_EXTENT_DATA(group) &&
           !IS_VALID_EXTENT_HEADER(group)) {
               LOG_ERROR_STR("Invalid extent group!");
               goto bail;
       }

       ext = group->extents;

       for(i=group->next_free_ext; i < OCFS_MAX_DATA_EXTENTS; i++) {
               ext[i].num_bytes = 0;
               ext[i].disk_off = 0;
               ext[i].file_off = 0;
       }

       /* Alright, write this guy back out now */
       if (osb != NULL) {
               status = ocfs_write_sector (osb, group, group->this_ext);

               if (status < 0) {
                       LOG_ERROR_STATUS (status);
                       goto bail;
               }
       }
       status=0;
bail:
       LOG_EXIT_STATUS (status);

       return(status);
}

/*
 * ocfs_split_this_tree
 *
 * Given an extent_group (DAT or HDR) takes the new alloc_size from fe
 * and splits this tree into two parts, one of which is deleted.
 *
 */
int ocfs_split_this_tree(ocfs_super * osb, ocfs_extent_group *extent_grp,
                   ocfs_cleanup_record *CleanupLogRec, ocfs_file_entry *fe) {
       int status = -EFAIL;
       __u64 newsize = fe->alloc_size;
       ocfs_alloc_ext *ext;
       ocfs_extent_group * grp_stack[OCFS_TREE_STACK_SIZE];
       ocfs_extent_group * AllocExtent; /* convenience, points to TOS */
       ocfs_extent_group *tmp = NULL, *tmp2 = NULL;
       int tos = 0;
       int i, victim;
       __u64 bytes, foff, doff, orig_bytes, dstart = osb->vol_layout.data_start_off, total_bytes, csize = osb->vol_layout.cluster_size;
       __u32 num_sectors, bitmap_offset, size;
       bool done = false;
       int gran = fe->granularity;

       LOG_ENTRY();

       size = sizeof (ocfs_cleanup_record);
       size = (__u32) OCFS_ALIGN (size, OCFS_PAGE_SIZE);

       /* This is a similar hack to the one below, untested for gran = 3 files
          because I can't recreate one. */
       if (gran == 3) {
               LOG_ERROR_STR("Truncating file with granularity 3, this is not tested and may be unsafe!");
               LOG_TRACE_STR("Found a granularity 3 tree, trimming it.\n");
               tmp2 = ocfs_malloc(size);

               if (tmp2 == NULL) {
                       LOG_ERROR_STATUS(status);
                       goto bail;
               }

               for(i = (extent_grp->next_free_ext - 1); i>=0; i--) {
                       ext = &extent_grp->extents[i];

                       status = ocfs_read_sector(osb, tmp2, ext->disk_off);
                       if (status < 0) {
                               LOG_ERROR_STATUS (status);
                               goto bail;
                       }

                       if (ext->file_off >= newsize) {
                               /* Trim this whole subtree */
                               status = ocfs_kill_this_tree(osb, tmp2, CleanupLogRec);
                               if (status < 0) {
                                       LOG_ERROR_STATUS (status);
                                       goto bail;
                               }
                               ext->file_off = 0;
                               ext->disk_off = 0;
                               ext->num_bytes = 0;
                               extent_grp->next_free_ext = i;
                       } else  { /* This is the one we want to split. */
                               ext->num_bytes = newsize - ext->file_off;
                               break;
                       }
               }
               /* Write out our new top of the tree duder */
               status = ocfs_write_sector(osb, extent_grp, extent_grp->this_ext);

               /* Make our new TOS the header we want to split. */
               extent_grp = tmp2;
               LOG_TRACE_STR("Ok, continuing as if granularity = 2");

               /* We want to do the next bit of stuff too */
               gran = 2;
       }

       /* get rid of everything from the top level HDR that we can, then
          proceeed as if we're granularity 1 (which we know works) */
       if (gran == 2) {
               LOG_TRACE_STR("Found a granularity 2 tree, trimming it.\n");
               tmp = ocfs_malloc(size);

               if (tmp == NULL) {
                       LOG_ERROR_STATUS(status);
                       goto bail;
               }

               for(i = (extent_grp->next_free_ext - 1); i>=0; i--) {
                       ext = &extent_grp->extents[i];

                       status = ocfs_read_sector(osb, tmp, ext->disk_off);
                       if (status < 0) {
                               LOG_ERROR_STATUS (status);
                               goto bail;
                       }

                       if (ext->file_off >= newsize) {
                               /* Trim this whole subtree */
                               status = ocfs_kill_this_tree(osb, tmp, CleanupLogRec);
                               if (status < 0) {
                                       LOG_ERROR_STATUS (status);
                                       goto bail;
                               }
                               ext->file_off = 0;
                               ext->disk_off = 0;
                               ext->num_bytes = 0;
                               extent_grp->next_free_ext = i;
                       } else  { /* This is the one we want to split. */
                               ext->num_bytes = newsize - ext->file_off;
                               break;
                       }
               }
               /* Write out our new top of the tree duder */
               status = ocfs_write_sector(osb, extent_grp, extent_grp->this_ext);

               /* Make our new TOS the header we want to split. */
               extent_grp = tmp;
               LOG_TRACE_STR("Ok, continuing as if granularity = 1");

               /* Right now, we don't use 'gran' below here, but just
                * in case */
               gran = 1;
       }

       for (i =0; i < OCFS_TREE_STACK_SIZE; i++)
               grp_stack[i] = NULL;

       grp_stack[tos] = extent_grp;

       /* Ok, find the splitting point (can be a DAT or HDR) */
       do {
               AllocExtent = grp_stack[tos];

               if (!IS_VALID_EXTENT_DATA(AllocExtent) &&
                   !IS_VALID_EXTENT_HEADER(AllocExtent)) {
                       LOG_ERROR_STR("Invalid extent group!");
                       goto bail;
               }

               if (IS_VALID_EXTENT_DATA(AllocExtent)) {
                       /* shall we just do away with him? */
                       LOG_TRACE_STR("Found a whole data extent!");
                       /* changed this from > to >= */
                       if (AllocExtent->extents[0].file_off >= newsize) {
                               LOG_TRACE_ARGS("Killing this data extent (%u, %u)\n", HI(AllocExtent->this_ext), LO(AllocExtent->this_ext));
                               /* Boundary case - what if this guy is
                                * the last DAT we should delete
                                * (i.e., split no more ;) */
                               status = ocfs_kill_this_tree(osb, AllocExtent, CleanupLogRec);
                               if (status < 0) {
                                       LOG_ERROR_STATUS (status);
                                       goto bail;
                               }

                       } else {
                               /* Alright, we know for sure that
                                * we're splitting in this guy. */
                               LOG_TRACE_ARGS("Splitting this data extent (%u, %u)\n", HI(AllocExtent->this_ext), LO(AllocExtent->this_ext));
                               fe->last_ext_ptr= AllocExtent->this_ext;
                               AllocExtent->next_data_ext = 0;
                               /* total_bytes is used below to know
                                * how much total we've whacked off
                                * this extent*/
                               total_bytes = 0;

                               /* there is a chance the split is at a
                                * header boundary. this will catch
                                * it: */
                               ext = &AllocExtent->extents[AllocExtent->next_free_ext - 1];
                               if ((ext->file_off + ext->num_bytes)==newsize){
                                       LOG_TRACE_STR("Ok, hit that boundary in the DAT");
                                       goto fix_headers;
                               }
                               /* Either kill the data or resize it */
                               for(i = (AllocExtent->next_free_ext - 1); i>=0;
i--) {
                                       ext = &AllocExtent->extents[i];

                                       /* changed this from > to >= */
                                       /* Do we delete it completely? */
                                       if (ext->file_off >= newsize) {
                                               total_bytes+=ext->num_bytes;

                                               num_sectors = (__u32) (ext->num_bytes / csize);
                                               bitmap_offset = (__u32) ((ext->disk_off - dstart) / csize);
                                               ext->file_off = 0;
                                               ext->num_bytes = 0;
                                               ext->disk_off = 0;
                                       } else {
                                               /* Do we shrink it? */
                                               orig_bytes = ext->num_bytes;
                                               doff = ext->disk_off;
                                               foff = ext->file_off;
                                               bytes = ext->num_bytes = newsize - foff;
                                               num_sectors = (__u32) ((orig_bytes - bytes) / csize);
                                               bitmap_offset = (__u32) (((doff
+ bytes) - dstart) / csize);
                                               //we want to exit the for loop now
                                               total_bytes+= (orig_bytes - bytes);
                                               done = true;
                                       }
                                       status = _write_free_extent_log(osb, CleanupLogRec, num_sectors, bitmap_offset, -1, DISK_ALLOC_VOLUME);
                                       if (status < 0) {
                                               LOG_ERROR_STATUS (status);
                                               goto bail;
                                       }

                                       if (done) {
                                               AllocExtent->next_free_ext=i+1;
                                               break;
                                       }
                               } /* For loop */

                               LOG_TRACE_ARGS("Writing that data extent back out to disk now (%u,%u)\n", HI(AllocExtent->this_ext), LO(AllocExtent->this_ext));
			       /* Either way, we need to write this back out*/
                               status = ocfs_write_sector (osb, AllocExtent, AllocExtent->this_ext);
                               if (status < 0) {
                                       LOG_ERROR_STATUS(status);
                                       goto bail;
                               }

                               LOG_TRACE_ARGS("Fixing the headers above us! (tos=%d)\n", tos);
                       fix_headers:
                               /*And here we should fix the headers above us*/
                               tos--;
                               while (tos >= 0) {
                                       LOG_TRACE_ARGS("at top of loop, tos=%d\n", tos);
                                       AllocExtent = grp_stack[tos];
                                       victim = AllocExtent->next_free_ext;
                                       AllocExtent->next_free_ext++;
                                       /* need to also update
                                        * numbytes on these guys */
                                       ext = &AllocExtent->extents[victim];
                                       ext->num_bytes-= total_bytes;
                                       status = ocfs_fix_extent_group(osb, AllocExtent);
                                       if (status < 0) {
                                               LOG_ERROR_STATUS(status);
                                               goto bail;
                                       }
                                       tos--;
                               }
                               LOG_TRACE_STR("breaking to end the function now!");
                               break;
                       }
               } else {
                       /* It's a header extent */

                       /* Did we already kill all his children, or
                         * are they already dead? */
                        if (AllocExtent->next_free_ext == 0) {
                               /*Ok, we're done with this guy, pop the stack*/
                               tos--;
                                LOG_TRACE_ARGS("Popping this header (%u.%u)\n", HI(AllocExtent->this_ext), LO(AllocExtent->this_ext), AllocExtent->next_free_ext);

                               status = _write_free_extent_log(osb, CleanupLogRec, 1, AllocExtent->alloc_file_off, AllocExtent->alloc_node, DISK_ALLOC_EXTENT_NODE);
                               if (status < 0) {
                                       LOG_ERROR_STATUS (status);
                                       goto bail;
                               }
                               continue;
                       }
                       /* changed this from > to >= */
                       /* Do we just delete this whole part of the tree? */
                       if (AllocExtent->extents[0].file_off >= newsize) {
                               LOG_TRACE_ARGS("whacking this tree: (%u.%u)\n",
HI(AllocExtent->this_ext), LO(AllocExtent->this_ext));

                               if (AllocExtent->extents[0].file_off ==newsize)
                                       done = true;

                               ocfs_kill_this_tree(osb, AllocExtent,CleanupLogRec);
                               tos--;
                               if (tos < 0) {
                                       LOG_ERROR_STR("End of stack reached.");
                                       goto bail;
                               }
                               /* I just have to fix my parent,
                                * right? Yes, but only because our
                                * max granularity is 2. if it were
                                * more, we'd have to fix his
                                * parents parent. */

                               victim = grp_stack[tos]->next_free_ext;
                               grp_stack[tos]->extents[victim].file_off = 0;
                               grp_stack[tos]->extents[victim].num_bytes = 0;
                               grp_stack[tos]->extents[victim].disk_off = 0;
                               grp_stack[tos]->next_free_ext--;

                               /* Here's an interesting boundary
                                * case. What if we're truncating on a
                                * boundary between two headers and
                                * this is the one we just deleted. In
                                * that case we're done, but need to
                                * write the parent out before we leave
                                * again, this bit of code depends on
                                * granularity of 2. */
                               if (done) {
                                       LOG_TRACE_STR("Found a boundary header, almost done (gonna quit)");
                                       status = ocfs_fix_extent_group(osb, grp_stack[tos]);
                                       if (status < 0) {
                                               LOG_ERROR_STATUS(status);
                                               goto bail;
                                       }
                                       /* decrement tos so we dont
                                        * trigger an error
                                        * condition */
                                       tos--;
                                       break;
                               }
                               /* Ok, we're not a boundary case, continue */
                               continue;
                       }

                        /* grow the stack, only allocating mem if we
                         * haven't already */
                       tos++;
                       if (grp_stack[tos] == NULL)
                               grp_stack[tos] = ocfs_malloc(size);
                       else
                               memset(grp_stack[tos], 0, size);

                       /* Go one extent to the left */
                       AllocExtent->next_free_ext--;
                       victim = AllocExtent->next_free_ext;
                       ext = &AllocExtent->extents[victim];

                       status = ocfs_read_sector(osb, grp_stack[tos], ext->disk_off);
                       if (status < 0) {
                               LOG_ERROR_STATUS (status);
                               goto bail;
                       }

                       LOG_TRACE_ARGS("Pushing this group (%u.%u)\n", HI(grp_stack[tos]->this_ext), LO(grp_stack[tos]->this_ext));

                       /* We only want to free on our way up the tree */
                       continue;
               }

               tos--;
       } while (tos >= 0);

       if (tos >= 0)
               LOG_ERROR_ARGS("Quitting main loop while top of stack >= 0 (tos=%d)\n", tos);

       status=0;
bail:
       ocfs_safefree(tmp);
       ocfs_safefree(tmp2);
        /* Free the stack. We never free the bottom of the stack
         * because we were passed that guy from the caller */
        for(i = 1; i < OCFS_TREE_STACK_SIZE; i++)
                ocfs_safefree(grp_stack[i]);

       LOG_EXIT_STATUS (status);
       return(status);
}

/*
 * ocfs_update_last_ext_ptr
 *
 *  Travel all the way to the rightmost DAT and set fe->last_ext_ptr to it.
 */
int ocfs_update_last_ext_ptr(ocfs_super *osb, ocfs_file_entry *fe) {
       int status = -EFAIL;
       ocfs_extent_group *AllocExtent = NULL;
       __u32 size;
       __u64 next_ext;
       int victim;

       LOG_ENTRY ();

       if (fe->next_free_ext == 0) {
               LOG_TRACE_STR("setting to zero as there isn't any used extents");
               fe->last_ext_ptr = 0;
       }

       size = OCFS_ALIGN (sizeof (ocfs_extent_group), osb->sect_size);
       AllocExtent = ocfs_malloc (size);

       victim = fe->next_free_ext - 1;
       status = ocfs_read_sector(osb, AllocExtent, fe->extents[victim].disk_off);
       if (status < 0) {
               LOG_ERROR_STATUS(status);
               goto bail;
       }

       if (!IS_VALID_EXTENT_DATA(AllocExtent) &&
           !IS_VALID_EXTENT_HEADER(AllocExtent)) {
               LOG_ERROR_STR("Invalid extent group!");
               goto bail;
       }

       while (!IS_VALID_EXTENT_DATA(AllocExtent)) {
               if (!IS_VALID_EXTENT_HEADER(AllocExtent)) {
                       LOG_ERROR_STR("Invalid extent group!");
                       goto bail;
               }

               next_ext = AllocExtent->extents[AllocExtent->next_free_ext - 1].disk_off;
               status = ocfs_read_sector(osb, AllocExtent, next_ext);
               if (status < 0) {
                       LOG_ERROR_STATUS(status);
                       goto bail;
               }
       }

       fe->last_ext_ptr = AllocExtent->this_ext;
       status = 0;
bail:
       ocfs_safefree(AllocExtent);

       LOG_EXIT_STATUS(status);
       return(status);
}
 
/*
 * ocfs_free_extents_for_truncate()
 *
 */
int ocfs_free_extents_for_truncate (ocfs_super * osb, ocfs_file_entry * FileEntry)
{
	int status = 0;
	__u32 size;
	ocfs_cleanup_record *CleanupLogRec = NULL;
	ocfs_extent_group *AllocExtent = NULL;
        __u64 alloc_size;
        int i, j;
        bool updated_lep; /* used to mark whether fe->last_ext_ptr has
                           * been updated */

	LOG_ENTRY ();

        alloc_size = FileEntry->alloc_size;
	size = sizeof (ocfs_cleanup_record);
	size = (__u32) OCFS_ALIGN (size, OCFS_PAGE_SIZE);

	CleanupLogRec = ocfs_malloc (size);
	if (CleanupLogRec == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto finally;
	}

	CleanupLogRec->rec.free.num_free_upds = 0;
	CleanupLogRec->log_id = osb->curr_trans_id;
	CleanupLogRec->log_type = LOG_FREE_BITMAP;

        /* local extents */
	if (FileEntry->local_ext) {
               LOG_TRACE_STR("local extents, calling _squish_extent_entries");
                status = _squish_extent_entries(osb, FileEntry->extents, &FileEntry->next_free_ext, CleanupLogRec, alloc_size, false);
               LOG_TRACE_ARGS("return from _squish_extent_entries, status=%d",
status);

                if (status < 0) {
                        LOG_ERROR_STATUS (status);
                        goto finally;
                }
                goto write_log;
        }

        LOG_TRACE_ARGS("non-local extents. taking that code path, truncating to alloc_size of (%u.%u)\n", HI(alloc_size), LO(alloc_size));
        /* non-local extents */
        updated_lep = false;
	size = OCFS_ALIGN (sizeof (ocfs_extent_group), osb->sect_size);
	AllocExtent = ocfs_malloc (size);
	if (AllocExtent == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto finally;
	}

	memset (AllocExtent, 0, size);

        /* Loop backwards through only the used free extent headers here */
        for (i = (FileEntry->next_free_ext - 1); i >= 0; i--) {
                LOG_TRACE_ARGS("at top of loop, i = %d\n", i);
                /* Go ahead and read that bit of the tree - we'll need it. */
                status = ocfs_read_metadata (osb, AllocExtent, osb->sect_size,
                                             FileEntry->extents[i].disk_off);
		if (status < 0) {
			LOG_ERROR_STATUS(status);
			goto finally;
		}

               /* Figure out, do we want to kill this whole tree? */
               if (FileEntry->extents[i].file_off >= alloc_size) {
                       LOG_TRACE_ARGS("Found an entire tree to delete!\n");

                       status = ocfs_kill_this_tree(osb, AllocExtent, CleanupLogRec);
                       if (status < 0) {
                               LOG_ERROR_STATUS(status);
				goto finally;
			}
                        /* Ok, update the FileEntry */
                        FileEntry->extents[i].file_off = 0;
                        FileEntry->extents[i].disk_off = 0;
                        FileEntry->extents[i].num_bytes = 0;
                        FileEntry->next_free_ext = i;
                } else { /* Ok, we only want part of it. */
                        /* Actually, the truth is we might NOT want to
                         * split this tree, but we call this function
                         * anyways in order to update last_ext_ptr. */

                       LOG_TRACE_ARGS("Splitting this tree!\n");
                       status = ocfs_split_this_tree(osb, AllocExtent, CleanupLogRec, FileEntry);
                       if (status < 0) {
                               LOG_ERROR_STATUS(status);
				goto finally;
			}
                       /* Ok, update the FileEntry */
                       LOG_TRACE_ARGS("Alright. num_bytes = (%u,%u), alloc_size = (%u,%u) file_off = (%u,%u)\n", HI(FileEntry->extents[i].num_bytes), LO(FileEntry->extents[i].num_bytes), HI(alloc_size), LO(alloc_size), HI(FileEntry->extents[i].file_off), LO(FileEntry->extents[i].file_off));
                       FileEntry->extents[i].num_bytes = alloc_size;
                       for (j=0; j < i; j++)
                               FileEntry->extents[i].num_bytes += FileEntry->extents[j].num_bytes;

                       FileEntry->next_free_ext = i + 1;
                       /* We're done - we can't split more than one
                        * parts of the tree. */
                       updated_lep = true;
                       break;
               }
       }

       /* Ok, trunc to zero is a special case */
       if (alloc_size == 0) {
               FileEntry->last_ext_ptr = 0;
               FileEntry->granularity = -1;
               FileEntry->local_ext = true;
               updated_lep = true;
       }

       if (!updated_lep) {
               LOG_TRACE_STR("Updating FileEntry->last_ext_ptr");
               status = ocfs_update_last_ext_ptr(osb, FileEntry);
               if (status < 0) {
                       LOG_ERROR_STATUS(status);
			goto finally;
		}
	}
	LOG_TRACE_ARGS("non-local extents, out of loop now, i = %d\n", i);

write_log:
       /* There may be some log records that haven't been written out
        * by _write_free_extent_log() yet, so finish up here.*/

	if (CleanupLogRec->rec.free.num_free_upds > 0) {
		status = ocfs_write_node_log (osb, (ocfs_log_record *) CleanupLogRec,
					      osb->node_num, LOG_CLEANUP);
		if (status < 0) {
			LOG_ERROR_STATUS(status);
			goto finally;
		}
	}

      finally:
	ocfs_safefree (AllocExtent);
	ocfs_safefree (CleanupLogRec);

	LOG_EXIT_ULONG (status);
	return status;
}  /* ocfs_free_extents_for_truncate */

/*
 * ocfs_lookup_file_allocation()
 *
 * This routine looks up the existing mapping of VBO to LBO for a  file.
 * The information it queries is either stored in the extent map field
 * of the oin or is stored in the allocation file and needs to be retrieved,
 * decoded and updated in the extent map.
 *
 */
int ocfs_lookup_file_allocation (ocfs_super * osb, ocfs_inode * oin, __s64 Vbo,
				 __s64 * Lbo, __u32 ByteCount, __u32 * NumIndex,
				 void **Buffer)
{
	int status = 0;
	ocfs_file_entry *FileEntry = NULL;
	__u32 allocSize = 0;
	__u64 length = 0, remainingLength = 0;
	__u8 *buffer = NULL;
	__u32 Runs;
	__s64 localVbo;
	ocfs_extent_group *OcfsExtent = NULL;
	ocfs_io_runs *IoRuns = NULL;

	LOG_ENTRY ();

	OCFS_ASSERT (osb);
	OCFS_ASSERT (oin);
	*Buffer = NULL;

	if (Vbo >= oin->alloc_size) {
		goto READ_ENTRY;
	}

	/* special case: just one byte - also happens to be the *only* 
	 * way in which this func is currently called */
	if (ByteCount == 1) {
		status = -ESPIPE;
		/* return the blocknum directly, no need to alloc ioruns */
		if (ocfs_lookup_extent_map_entry (osb, &oin->map, Vbo, Lbo, 
						  &length, &Runs)) {
			status = 0;
			goto no_iorun_exit;
		}
	} else {
		status = ocfs_extent_map_load (osb, &(oin->map), Buffer, Vbo, 
					       ByteCount, &Runs);
	}
	if (status >= 0) {
		/* If status is success, we found the needed extent map */
		goto finally;
	}

      READ_ENTRY:
	if (*Buffer) {
		ocfs_free (*Buffer);
		*Buffer = NULL;
	}

	remainingLength = ByteCount;
	length = 0;
	localVbo = Vbo;

	/*  We are looking for a Vbo, but it is not in the Map or not Valid. */
	/*  Thus we have to go to the disk, and update the Map */

	/* Read the file Entry corresponding to this */
	status = ocfs_get_file_entry (osb, &FileEntry, oin->file_disk_off);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	if (!IS_VALID_FILE_ENTRY (FileEntry)) {
		LOG_ERROR_STATUS (status = -EINVAL);
		goto finally;
	}

	if (Vbo >= (__s64) FileEntry->alloc_size) {
		LOG_TRACE_ARGS ("fe=%u.%u, vbo=%u.%u, fe->alloc_sz=%u.%u, "
			       	"oin->alloc_size=%u.%u\n",
			       	HILO (FileEntry->this_sector), HILO (Vbo),
			       	HILO (FileEntry->alloc_size), HILO (oin->alloc_size));
		status = -ESPIPE;
		goto finally;
	}

	if (FileEntry->local_ext) {
		status = ocfs_update_extent_map (osb, &oin->map, FileEntry,
						 NULL, NULL, 1);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}
	} else {
		/* Extents are branched and we are no longer using Local Extents */
		/* for this File Entry. */
		allocSize = (NUM_SECTORS_IN_LEAF_NODE +
			     FileEntry->granularity) * OCFS_SECTOR_SIZE;
		length = OCFS_ALIGN (allocSize, osb->sect_size);
		buffer = ocfs_malloc ((__u32) length);
		if (buffer == NULL) {
			LOG_ERROR_STATUS (status = -ENOMEM);
			goto finally;
		}

		OcfsExtent = (ocfs_extent_group *) buffer;

		status = ocfs_get_leaf_extent (osb, FileEntry, localVbo, OcfsExtent);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}

		while (1) {
			status =
			    ocfs_update_extent_map (osb, &oin->map, OcfsExtent,
						 &localVbo, &remainingLength,
						 2);
			if (status < 0) {
				LOG_ERROR_STATUS(status);
				goto finally;
			}

			if (remainingLength > 0) {
				if (!OcfsExtent->next_data_ext) {
					LOG_ERROR_ARGS ("vbo=%u.%u, "
						"oin->alloc_size=%u.%u, thisext=%u.%u",
						HILO(localVbo), HILO(oin->alloc_size),
						HILO(OcfsExtent->this_ext));
					status = -ESPIPE;
					goto finally;
				}

				status = ocfs_read_extent (osb, OcfsExtent,
							 OcfsExtent->next_data_ext,
							 EXTENT_DATA);
				if (status < 0) {
					LOG_ERROR_STATUS(status);
					goto finally;
				}
			} else {
				break;
			}
		}
	}

	if (ByteCount == 1) {
		status = -ESPIPE;
		if (ocfs_lookup_extent_map_entry (osb, &oin->map, Vbo, Lbo, 
						  &length, &Runs)) {
			status = 0;
		}
		goto no_iorun_exit;
	} else {
		status = ocfs_extent_map_load (osb, &(oin->map), Buffer, Vbo, 
					       ByteCount, &Runs);
	}

	if (status < 0) {
		LOG_ERROR_STATUS (status);
	}

finally:
	if (status >= 0) {
		IoRuns = (ocfs_io_runs *) (*Buffer);
		*(NumIndex) = Runs;
		*(Lbo) = IoRuns[0].disk_off;
	}

no_iorun_exit:
	/* Should send a null for IoRuns in case of onl 1 extent */
	LOG_TRACE_ARGS ("Num of Runs is: %d\n", Runs);

	ocfs_safefree (buffer);

	ocfs_release_file_entry (FileEntry);

	LOG_EXIT_STATUS (status);
	return (status);
}				/* ocfs_lookup_file_allocation */

/* ocfs_extent_map_load()
 *
 */
int ocfs_extent_map_load (ocfs_super * osb,
		     ocfs_extent_map * Map,
		     void **Buffer, __s64 Vbo, __u64 ByteCount, __u32 * RetRuns)
{
	int status = -EFAIL;
	ocfs_io_runs *IoRuns = NULL;
	__u32 BufferOffset;
	__u32 ioExtents = OCFS_MAX_DATA_EXTENTS;
	__u64 length = 0, remainingLength = 0;
	__u32 Runs, Index, ioRunSize;
	__s64 localLbo;
	__s64 localVbo;

	LOG_ENTRY ();

	ioRunSize = OCFS_MAX_DATA_EXTENTS * sizeof (ocfs_io_runs);
	IoRuns = ocfs_malloc (ioRunSize);
	if (IoRuns == NULL) {
		LOG_ERROR_STATUS(status = -ENOMEM);
		goto bail;
	}

	remainingLength = ByteCount;
	Runs = 0;
	length = 0;
	localVbo = Vbo;
	BufferOffset = 0;

	while (ocfs_lookup_extent_map_entry
	       (osb, Map, localVbo, &localLbo, &length, &Index)) {
		IoRuns[Runs].disk_off = localLbo;
		IoRuns[Runs].byte_cnt = length;
		IoRuns[Runs].offset = BufferOffset;

		if (length >= remainingLength) {
			IoRuns[Runs].byte_cnt = remainingLength;
			status = 0;
			Runs++;
			break;
		} else {
			Runs++;
			if (Runs >= ioExtents) {
				status = ocfs_adjust_allocation (&IoRuns, &ioRunSize);
				if (status < 0) {
					LOG_ERROR_STATUS(status);
					goto bail;
				}

				ioExtents *= 2;
			}
			localVbo += length;
			BufferOffset += length;
			remainingLength -= length;
			continue;
		}
	}

	(*RetRuns) = Runs;
	(*Buffer) = IoRuns;

      bail:
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_extent_map_load */


/*
 * ocfs_lookup_extent_map_entry()
 *
 * search for an VBO in the extent map passed on.
 *
 * Returns true if entry available in the extent map, false otherwise.
 */
bool ocfs_lookup_extent_map_entry (ocfs_super * osb,
			  ocfs_extent_map * Map,
			  __s64 Vbo, __s64 * Lbo, __u64 * SectorCount, __u32 * Index)
{
	bool ret;
	__s64 LiLbo = 0;
	__s64 LiSectorCount = 0;
	__u64 remainder = 0;

	LOG_ENTRY ();

	/* Sector Align the vbo */
	remainder = Vbo & OCFS_MOD_SECTOR_SIZE;

	ret = ocfs_extent_map_lookup (Map, (Vbo >> OCFS_LOG_SECTOR_SIZE), &LiLbo,
				   &LiSectorCount, Index);
	if ((__u32) LiLbo != -1) {
		*Lbo = (((__s64) LiLbo) << (__s64) OCFS_LOG_SECTOR_SIZE);
		if (ret) {
			*Lbo += remainder;
		}
	} else {
		ret = false;
		*Lbo = 0;
	}

	*SectorCount = LiSectorCount;
	if (*SectorCount) {
		*SectorCount <<= (__s64) OCFS_LOG_SECTOR_SIZE;
		if (*SectorCount == 0) {
			*SectorCount = (__u32) - 1;
		}

		if (ret) {
			*SectorCount -= remainder;
		}
	}

	LOG_EXIT_ULONG (ret);
	return ret;
}				/* ocfs_lookup_extent_map_entry */


/*
 * ocfs_adjust_allocation()
 *
 * It gets called if the number of runs is more than a default number and
 * so will free up the previously allocated memory and allocated twice the
 * prevously allocated memory.
 *
 */
int ocfs_adjust_allocation (ocfs_io_runs ** IoRuns, __u32 * ioRunSize)
{
	int status = 0;
	__u32 runSize = 0;
	ocfs_io_runs *localIoRuns = NULL;

	LOG_ENTRY ();

	OCFS_ASSERT (IoRuns);

	runSize = (*ioRunSize) * 2;
	localIoRuns = ocfs_malloc (runSize);
	if (localIoRuns == NULL) {
		LOG_ERROR_STATUS(status = -ENOMEM);
		goto bail;
	}

	memcpy (localIoRuns, *IoRuns, *ioRunSize);
	ocfs_free (*IoRuns);
	*IoRuns = localIoRuns;
	*ioRunSize = runSize;

	/* Don't free localIoRuns here */
bail:
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_adjust_allocation */

/* ocfs_update_extent_map()
 *
 */
int ocfs_update_extent_map (ocfs_super * osb, ocfs_extent_map * Map,
			    void *Buffer, __s64 * localVbo,
			    __u64 * remainingLength, __u32 Flag)
{
	int status = -EFAIL;
	ocfs_file_entry *FileEntry;
	ocfs_extent_group *OcfsExtent;
	__s64 tempVbo;
	__s64 tempLbo;
	__u64 tempSize;
	bool Ret;
	int j;

	LOG_ENTRY ();

	if (Flag == 1) {
		FileEntry = (ocfs_file_entry *) Buffer;

		OCFS_ASSERT (FileEntry->local_ext);

		for (j = 0; j < FileEntry->next_free_ext; j++) {
			tempVbo = FileEntry->extents[j].file_off;
			tempLbo = FileEntry->extents[j].disk_off;
			tempSize = FileEntry->extents[j].num_bytes;

			/* Add the Extent to extent map list */
			Ret =
			    ocfs_add_extent_map_entry (osb, Map, tempVbo, tempLbo,
						   tempSize);
			if (!Ret) {
				LOG_ERROR_STATUS (status = -ENOMEM);
				goto bail;
			}
		}
		status = 0;
	} else {
		__u64 localLength = 0;

		OcfsExtent = (ocfs_extent_group *) Buffer;

		for (j = 0; j < OcfsExtent->next_free_ext; j++) {
			if ((__s64) (OcfsExtent->extents[j].file_off +
				   OcfsExtent->extents[j].num_bytes) >
			    (*localVbo)) {
				tempVbo = OcfsExtent->extents[j].file_off;
				tempLbo = OcfsExtent->extents[j].disk_off;
				tempSize = OcfsExtent->extents[j].num_bytes;

				/* Add the Extent to extent map list */
				Ret =
				    ocfs_add_extent_map_entry (osb, Map, tempVbo,
							   tempLbo, tempSize);
				if (!Ret) {
					LOG_ERROR_STATUS (status = -ENOMEM);
					goto bail;
				}

				localLength =
				    (tempSize - ((*localVbo) - tempVbo));

				/* Since we have read the disk we should add some */
				/* more Entries to the extent map list */
				if (localLength >= (*remainingLength)) {
					(*remainingLength) = 0;
					status = 0;
					goto bail;
				} else {
					(*remainingLength) -= localLength;
					(*localVbo) += localLength;
				}
			}
		}

		if ((OcfsExtent->next_free_ext != OCFS_MAX_DATA_EXTENTS) &&
		    (*remainingLength)) {
			LOG_ERROR_ARGS ("next_free_extent=%d, rem_len=%u.%u",
				OcfsExtent->next_free_ext, HI(*remainingLength),
				LO(*remainingLength));
		} else
			status = 0;
	}

      bail:
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_update_extent_map */

/*
 * ocfs_add_extent_map_entry()
 *
 * adds an entry to an extent map
 *
 * Returns true on success, false otherwise.
 */
bool ocfs_add_extent_map_entry (ocfs_super * osb,
		       ocfs_extent_map * Map, __s64 Vbo, __s64 Lbo, __u64 ByteCount)
{
	bool ret;

	LOG_ENTRY ();

	/* Convert the Bytes in to number of Sectors */
	if (ByteCount) {
		/* Round up sectors */
		ByteCount--;
		ByteCount >>= OCFS_LOG_SECTOR_SIZE;
		ByteCount++;
	}

	/* Make the ByteOffsets in to Sector numbers. */
	/* In case of 512 byte sectors the OcfsLogOf gives back a value of 9. */
	/* And by doing a right shift of 9 bits we are actually dividing */
	/* the value by 512. */
	Vbo >>= OCFS_LOG_SECTOR_SIZE;
	Lbo >>= OCFS_LOG_SECTOR_SIZE;

	ret = ocfs_extent_map_add (Map, ((__s64) Vbo), ((__s64) Lbo), ((__s64) ByteCount));
	if (!ret)
		LOG_ERROR_ARGS ("fileoff=%u.%u, diskoff=%u.%u, len=%u.%u",
				HI(Vbo), LO(Vbo), HI(Lbo), LO(Lbo),
				HI(ByteCount), LO(ByteCount));

	LOG_EXIT_ULONG (ret);
	return ret;
}				/* ocfs_add_extent_map_entry */

/* ocfs_get_leaf_extent()
 *
 */
int ocfs_get_leaf_extent (ocfs_super * osb,
	       ocfs_file_entry * FileEntry,
	       __s64 Vbo, ocfs_extent_group * OcfsDataExtent)
{
	int status = 0, tempstat;
	__u32 i, j, length;
	ocfs_extent_group *ExtentHeader = NULL;
	__u64 childDiskOffset = 0;

	LOG_ENTRY ();

	for (i = 0; i < FileEntry->next_free_ext; i++) {
		if ((__s64) (FileEntry->extents[i].file_off +
			   FileEntry->extents[i].num_bytes) > Vbo) {
			childDiskOffset = FileEntry->extents[i].disk_off;
			break;
		}
	}

	if (childDiskOffset == 0) {
		LOG_ERROR_STATUS (status = -EINVAL);
		goto finally;
	}

	if (FileEntry->granularity >= 1) {
		length = osb->sect_size;
		ExtentHeader = ocfs_malloc ((__u32) length);
		if (ExtentHeader == NULL) {
			LOG_ERROR_STATUS (status = -ENOMEM);
			goto finally;
		}
	}

	for (i = 0; i < FileEntry->granularity; i++) {
		tempstat = ocfs_read_extent (osb, ExtentHeader, childDiskOffset,
					     EXTENT_HEADER);
		if (tempstat < 0) {
			LOG_ERROR_STATUS (status = tempstat);
			goto finally;
		}

		for (j = 0; j < ExtentHeader->next_free_ext; j++) {
			if ((__s64) (ExtentHeader->extents[j].file_off +
				   ExtentHeader->extents[j].num_bytes) > Vbo)
			{
				childDiskOffset =
			 	    ExtentHeader->extents[j].disk_off;
				break;
			}
		}
	}

	tempstat = ocfs_read_extent (osb, OcfsDataExtent, childDiskOffset,
				     EXTENT_DATA);
	if (tempstat < 0) {
		LOG_ERROR_STATUS (status = tempstat);
		goto finally;
	}

      finally:
	ocfs_safefree (ExtentHeader);

	LOG_EXIT_STATUS (status);
	return (status);
}				/* ocfs_get_leaf_extent */
