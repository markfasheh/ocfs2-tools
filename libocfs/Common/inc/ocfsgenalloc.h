/*
 * ocfsgenalloc.h
 *
 * Function prototypes for related 'C' file.
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
 * Authors: Neeraj Goyal, Suchit Kaura, Kurt Hackel, Sunil Mushran,
 *          Manish Singh, Wim Coekaerts
 */

#ifndef _OCFSGENALLOC_H_
#define _OCFSGENALLOC_H_

enum {
	EXTENT_HEADER,
	EXTENT_DATA
};

#define OCFS_EXTENT_MERGEABLE(ext, off)	\
			(((ext)->disk_off + (ext)->num_bytes) == (off))

#define ocfs_read_extent(osb, ext, off, typ)				\
({									\
	int __status;							\
	ocfs_extent_group *__ext;					\
	__status = ocfs_read_disk(osb, (ext), (osb)->sect_size, off);	\
	if (__status >= 0) {						\
		__ext = (ocfs_extent_group *) (ext);			\
		if ((typ) == EXTENT_HEADER) {				\
			if (!IS_VALID_EXTENT_HEADER(__ext))		\
				__status = -EFAIL;			\
		}							\
		if ((typ) == EXTENT_DATA) {				\
			if (!IS_VALID_EXTENT_DATA(__ext))		\
				__status = -EFAIL;			\
		}							\
	}								\
	__status;							\
})

int _write_free_extent_log(ocfs_super *osb, ocfs_cleanup_record *CleanupLogRec,
		   __u32 len, __u32 fileoff, __u32 nodenum, __u32 thistype);

int ocfs_kill_this_tree(ocfs_super *osb, ocfs_extent_group *extent_grp,
			ocfs_cleanup_record *CleanupLogRec);

int ocfs_lookup_file_allocation (ocfs_super * osb,
			  ocfs_inode * oin,
			  __s64 Vbo,
			  __s64 * Lbo,
			  __u32 ByteCount, __u32 * NumIndex, void **Buffer);


int ocfs_read_file_entry (ocfs_super * osb,
		   ocfs_file_entry * FileEntry, __u64 DiskOffset);

int ocfs_write_file_entry (ocfs_super * osb, ocfs_file_entry * FileEntry, __u64 Offset);

void ocfs_remove_extent_map_entry (ocfs_super * osb,
			  ocfs_extent_map * Map, __s64 Vbo, __u32 ByteCount);

int ocfs_allocate_new_data_node (ocfs_super * osb,
		     ocfs_file_entry * FileEntry,
		     __u64 actualDiskOffset,
		     __u64 actualLength,
		     ocfs_extent_group * ExtentHeader, __u64 * NewExtentOffset);

int ocfs_add_to_last_data_node (ocfs_super * osb,
		   ocfs_inode * oin,
		   ocfs_file_entry * FileEntry,
		   __u64 actualDiskOffset,
		   __u64 actualLength, __u32 * ExtentIndex, bool * IncreaseDepth);

int ocfs_update_last_data_extent (ocfs_super * osb,
		      ocfs_file_entry * FileEntry, __u64 NextDataOffset);

int ocfs_update_uphdrptr(ocfs_super *osb, ocfs_file_entry *fe,
			 __u64 new_up_hdr_ptr);

int ocfs_grow_extent_tree (ocfs_super * osb,
		ocfs_file_entry * FileEntry,
		__u64 actualDiskOffset, __u64 actualLength);

int ocfs_allocate_extent (ocfs_super * osb,
		    ocfs_inode * oin,
		    ocfs_file_entry * FileEntry,
		    __u64 actualDiskOffset, __u64 actualLength);

bool ocfs_get_next_extent_map_entry (ocfs_super * osb,
			   ocfs_extent_map * Map,
			   __u32 RunIndex,
			   __s64 * Vbo, __s64 * Lbo, __u32 * SectorCount);

int ocfs_update_all_headers (ocfs_super * osb,
		  ocfs_extent_group * AllocExtent, __u64 FileSize, ocfs_file_entry *fe);

int ocfs_free_extents_for_truncate (ocfs_super * osb, ocfs_file_entry * FileEntry);

int ocfs_get_leaf_extent (ocfs_super * osb,
	       ocfs_file_entry * FileEntry,
	       __s64 Vbo, ocfs_extent_group * OcfsDataExtent);

int ocfs_adjust_allocation (ocfs_io_runs ** IoRuns, __u32 * ioRunSize);

bool ocfs_lookup_extent_map_entry (ocfs_super * osb,
			  ocfs_extent_map * Map,
			  __s64 Vbo, __s64 * Lbo, __u64 * SectorCount, __u32 * Index);

bool ocfs_add_extent_map_entry (ocfs_super * osb,
		       ocfs_extent_map * Map, __s64 Vbo, __s64 Lbo, __u64 ByteCount);

int ocfs_update_extent_map (ocfs_super * osb,
		     ocfs_extent_map * Map,
		     void *Buffer,
		     __s64 * localVbo, __u64 * remainingLength, __u32 Flag);

int ocfs_extent_map_load (ocfs_super * osb,
		     ocfs_extent_map * Map,
		     void **Buffer, __s64 Vbo, __u64 ByteCount, __u32 * RetRuns);

                                                                                
int ocfs_fix_extent_group(ocfs_super *osb, ocfs_extent_group *group);

int ocfs_update_last_ext_ptr(ocfs_super *osb, ocfs_file_entry *fe);

int ocfs_split_this_tree(ocfs_super * osb, ocfs_extent_group *extent_grp,
                        ocfs_cleanup_record *CleanupLogRec, ocfs_file_entry *fe);

int _squish_extent_entries(ocfs_super *osb, ocfs_alloc_ext *extarr, __u8 *freeExtent, ocfs_cleanup_record *CleanupLogRec, __u64 FileSize, bool flag);

/*
 * ocfs_force_get_file_entry()
 *
 */
static inline int ocfs_force_get_file_entry (ocfs_super * osb, ocfs_file_entry ** FileEntry,
		       __u64 DiskOffset, bool force)
{
	if (!FileEntry)
		return -EFAIL;
	*FileEntry = ocfs_allocate_file_entry ();
	if (!*FileEntry)
		return -ENOMEM;
	if (force)
		return ocfs_read_force_disk (osb, *FileEntry, osb->sect_size,
					  DiskOffset);
	else
		return ocfs_read_file_entry (osb, *FileEntry, DiskOffset);
}  /* ocfs_force_get_file_entry */


/*
 * ocfs_force_put_file_entry()
 *
 */
static inline int ocfs_force_put_file_entry (ocfs_super * osb, ocfs_file_entry * FileEntry,
		       bool force)
{
	if (!FileEntry)
		return -EFAIL;
	if (force)
		return ocfs_write_force_disk (osb, FileEntry, osb->sect_size,
					   FileEntry->this_sector);
	else
		return ocfs_write_file_entry (osb, FileEntry,
					   FileEntry->this_sector);
}  /* ocfs_force_put_file_entry */


#define ocfs_get_file_entry(x,y,z)  ocfs_force_get_file_entry(x,y,z,false)
#define ocfs_put_file_entry(x,y)    ocfs_force_put_file_entry(x,y,false)


#endif				/* _OCFSGENALLOC_H_ */
