/*
 * ocfsgencreate.c
 *
 * Does lots of things sort-of associated with creating a file.
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
#define OCFS_DEBUG_CONTEXT  OCFS_DEBUG_CONTEXT_CREATE

/*
 * ocfs_verify_update_oin()
 *
 * This function searches the cached oin list for a volume for a given
 * filename. We currently cache all the oin's. We should hash this list.
 *
 */ 
int ocfs_verify_update_oin (ocfs_super * osb, ocfs_inode * oin)
{
	int status = 0;
	ocfs_file_entry *fe = NULL;
	ocfs_lock_res *pLockRes;
	struct inode *inode = NULL;
#ifndef USERSPACE_TOOL
        struct list_head *iter;
        struct list_head *temp_iter;
	struct dentry *dentry;
#endif
        int disk_len;

	/* We are setting the oin Updated flag in the end. */
	LOG_ENTRY ();

	OCFS_ASSERT (oin);

	status = ocfs_get_file_entry (osb, &fe, oin->file_disk_off);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	/* Make sure that what we found is not a directory. */
	if (!(oin->oin_flags & OCFS_OIN_DIRECTORY)) {
		/* Add checks as needed */
		if (IS_FE_DELETED(fe->sync_flags) ||
		    (!(fe->sync_flags & OCFS_SYNC_FLAG_VALID))) {
			LOG_TRACE_ARGS ("Invalid file: %*s, syncflg=0x%X\n",
				       	fe->filename_len, fe->filename,
				       	fe->sync_flags);
			OCFS_SET_FLAG (oin->oin_flags, OCFS_OIN_INVALID);
			/* ?? I think we should remove the oin here from the oin list */
			status = -ENOENT;
			goto leave;
		}

                disk_len = strlen(fe->filename);
		inode = oin->inode;
		if (inode == NULL) {
			LOG_TRACE_ARGS ("oin has no matching inode: %*s\n",
				       	fe->filename_len, fe->filename);
			OCFS_SET_FLAG (oin->oin_flags, OCFS_OIN_INVALID);
			status = -ENOENT;
			goto leave;
		}
		
#ifndef USERSPACE_TOOL
                status = -ENOENT;
		dentry = NULL;
                list_for_each_safe (iter, temp_iter, &(inode->i_dentry)) {
                        dentry = list_entry (iter, struct dentry, d_alias);
                        if (dentry->d_name.len == disk_len &&
                            strncmp(dentry->d_name.name, fe->filename, disk_len)==0)
                        {
                                status = 0;
                        }
                }
		if (status < 0) {
			LOG_TRACE_ARGS ("name did not match inode: %*s\n",
				       	fe->filename_len, fe->filename);
			OCFS_SET_FLAG (oin->oin_flags, OCFS_OIN_INVALID);
			goto leave;
		}
#endif

		if ((oin->alloc_size != (__s64) fe->alloc_size) ||
		    (inode->i_size != (__s64) fe->file_size) ||
		    (oin->chng_seq_num != DISK_LOCK_SEQNUM (fe)) ||
		    inode->i_uid != fe->uid ||
		    inode->i_gid != fe->gid || inode->i_mode != fe->prot_bits) {

			if (oin->alloc_size > (__s64)fe->alloc_size) {
				ocfs_extent_map_destroy (&oin->map);
				ocfs_extent_map_init (&oin->map);
			}

			LOG_TRACE_STR
			    ("Allocsize, filesize or seq no did not match");
			oin->alloc_size = fe->alloc_size;
			inode->i_size = fe->file_size;
			oin->chng_seq_num = DISK_LOCK_SEQNUM (fe);

			inode->i_blocks = (inode->i_size + 512) >> 9;
			inode->i_uid = fe->uid;
			inode->i_gid = fe->gid;
			inode->i_mode = fe->prot_bits;
			inode->i_blksize = (__u32) osb->vol_layout.cluster_size;
		        inode->i_ctime = fe->create_time;
        		inode->i_atime = fe->modify_time;
        		inode->i_mtime = fe->modify_time;

			if (!S_ISDIR (inode->i_mode)) {
				truncate_inode_pages (inode->i_mapping, 0);
			}

			switch (fe->attribs & (~OCFS_ATTRIB_FILE_CDSL)) {
			    case OCFS_ATTRIB_DIRECTORY:
				    inode->i_size = OCFS_DEFAULT_DIR_NODE_SIZE;
				    inode->i_blocks = (inode->i_size + 512) >> 9;
				    inode->i_mode |= S_IFDIR;
				    break;
			    case OCFS_ATTRIB_SYMLINK:
				    inode->i_mode |= S_IFLNK;
				    break;
			    case OCFS_ATTRIB_REG:
				    inode->i_mode |= S_IFREG;
				    break;
			    case OCFS_ATTRIB_CHAR:
			    case OCFS_ATTRIB_BLOCK:
			    case OCFS_ATTRIB_FIFO:
			    case OCFS_ATTRIB_SOCKET:
			    {
				    kdev_t kdev;

				    if (fe->attribs == OCFS_ATTRIB_CHAR)
					    inode->i_mode |= S_IFCHR;
				    else if (fe->attribs == OCFS_ATTRIB_BLOCK)
					    inode->i_mode |= S_IFBLK;
				    else if (fe->attribs == OCFS_ATTRIB_FIFO)
					    inode->i_mode |= S_IFIFO;
				    else if (fe->attribs == OCFS_ATTRIB_SOCKET)
					    inode->i_mode |= S_IFSOCK;

				    inode->i_rdev = NODEV;
				    kdev = MKDEV (fe->dev_major, fe->dev_minor);
				    init_special_inode (inode, inode->i_mode,
							kdev_t_to_nr (kdev));
				    break;
			    }
			    default:
			    	    LOG_ERROR_ARGS ("attribs=%d", fe->attribs);
				    inode->i_mode |= S_IFREG;
				    break;
			}

			if (fe->local_ext) {
				__s64 tempVbo;
				__s64 tempLbo;
				__u64 tempSize;
				__u32 j;

				/* Add the Extents to extent map */
				for (j = 0; j < fe->next_free_ext; j++) {
					tempVbo = fe->extents[j].file_off;
					tempLbo = fe->extents[j].disk_off;
					tempSize = fe->extents[j].num_bytes;

					if (!ocfs_add_extent_map_entry (osb,
							&oin->map, tempVbo, tempLbo,
							tempSize))
						goto leave;
				}
			}
		}

		pLockRes = oin->lock_res;
		ocfs_get_lockres (pLockRes);

		/* ??? we need to the lock resource before updating it */
		if (pLockRes) {
			pLockRes->lock_type = DISK_LOCK_FILE_LOCK (fe);
			pLockRes->master_node_num = DISK_LOCK_CURRENT_MASTER (fe);
			pLockRes->oin_openmap = DISK_LOCK_OIN_MAP (fe);
			pLockRes->last_write_time = DISK_LOCK_LAST_WRITE (fe);
			pLockRes->last_read_time = DISK_LOCK_LAST_READ (fe);
			pLockRes->reader_node_num = DISK_LOCK_READER_NODE (fe);
			pLockRes->writer_node_num = DISK_LOCK_WRITER_NODE (fe);
		}

		ocfs_put_lockres (pLockRes);

		status = 0;
	} else {
		/* Update for the DIRECTORY */
	}

      leave:
	if (status == 0)
		OIN_UPDATED (oin);

	ocfs_release_file_entry (fe);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_verify_update_oin */

/*
 * ocfs_find_contiguous_space_from_bitmap()
 *
 * This function looks for free space in the volume based on the bitmap.
 * It looks for contiguous space only and if it founds the space available
 * it returns cluster bitmap offset. Each bit in Cluster bitmap represents
 * memory equal to cluster size (specified during format).
 *
 * TODO: The Bitmap stuff needs to be changed for handling more than 32 bits...
 * Although we can go upto 4k(clustersize) * 8 * 4M(max 32 bits for now...)
 *
 * Returns 0 on success, < 0 on error.
 */
int ocfs_find_contiguous_space_from_bitmap (ocfs_super * osb,
			       __u64 file_size,
			       __u64 * cluster_off, __u64 * cluster_count, bool sysfile)
{
	int status = 0, tmpstat;
	__u32 size = 0, ByteOffset = 0, ClusterCount = 0;
	__u64 ByteCount = 0;
	__u32 LargeAlloc = 0;
	static __u32 LargeAllocOffset = 0;
	static __u32 SmallAllocOffset = 0;
	__u64 startOffset = 0;
	bool bLockAcquired = false;
	ocfs_lock_res *pLockResource = NULL;
        ocfs_file_entry *fe = NULL;
        ocfs_bitmap_lock *bm_lock = NULL;

	LOG_ENTRY ();

	OCFS_ASSERT (osb);

	ocfs_down_sem (&(osb->vol_alloc_lock), true);

	/* Get the allocation lock here */
        fe = ocfs_allocate_file_entry();
        if (!fe) {
                LOG_ERROR_STATUS (status = -ENOMEM);
                goto leave;
        }
        bm_lock = (ocfs_bitmap_lock *)fe;

	status = ocfs_acquire_lock (osb, OCFS_BITMAP_LOCK_OFFSET,
			     OCFS_DLM_EXCLUSIVE_LOCK, 0, &pLockResource,
			     fe);
	if (status < 0) {
		if (status != -EINTR)
			LOG_ERROR_STATUS (status);
		goto leave;
	}

	bLockAcquired = true;

 	LOG_TRACE_STR ("LOCK");
 
	ByteCount = file_size;

	/* Calculate the size in Bytes */
	size = (__u32) OCFS_SECTOR_ALIGN ((osb->cluster_bitmap.size) / 8);

	startOffset = osb->vol_layout.bitmap_off;
	status = ocfs_read_metadata (osb, osb->cluster_bitmap.buf, size,
				     startOffset);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	/* Round off the byte count to next clustersize (bytes per cluster) */
	ByteCount += (ByteCount % (osb->vol_layout.cluster_size)) ?
	    (osb->vol_layout.cluster_size -
	     (ByteCount % (osb->vol_layout.cluster_size))) : 0;

	if (ByteCount == 0) {
		LOG_ERROR_STR ("DISK_FULL?: Bytecount==0");
		status = 0;
		goto leave;
	}

	ClusterCount =
	    (__u32) ((__u64) ByteCount / (__u64) osb->vol_layout.cluster_size);

	if (sysfile ? (ClusterCount > osb->vol_layout.num_clusters) :
			(ClusterCount > (osb->vol_layout.num_clusters - 
		        ((8 * ONE_MEGA_BYTE) / osb->vol_layout.cluster_size)))){
		LOG_ERROR_STR ("Disk Full");
		status = -ENOSPC;
		goto leave;
	}

	/* This function will check for clear bits in the Bitmap for consecutive */
	/* clear bits equal to ClusterCount */

	/* If we create a chunk that is larger than 5% of the disksize, then start */
	/* allocation at 5%, so that small files stay in the beginning as much as possible */
	
	if (ClusterCount > (osb->vol_layout.num_clusters / 20)) {
		LargeAlloc = 1;
		LargeAllocOffset = (osb->vol_layout.num_clusters / 20);
	}
	

	ByteOffset = ocfs_find_clear_bits (&osb->cluster_bitmap, ClusterCount,
					LargeAlloc ? LargeAllocOffset :
					SmallAllocOffset, sysfile ? 0 :
					((8 * ONE_MEGA_BYTE) / osb->vol_layout.cluster_size));

	/* if fails we should try again from the beginning of the disk. */
	/* in the end we pass # of bits we want to keep for system file extention only */
	/* right now if we run out of diskspace, we still have 8mb free for a systemfile */

	if (ByteOffset == -1 && LargeAlloc) {
		osb->cluster_bitmap.failed++;
		ByteOffset = ocfs_find_clear_bits (&osb->cluster_bitmap,
					ClusterCount, 0,
					sysfile ? 0 :
						((8 * ONE_MEGA_BYTE) /
						 osb->vol_layout.cluster_size));
	}

	/* It returns -1 on failure, otherwise ByteOffset points at the */
	/* location inb bitmap from where there are ClusterCount no of bits */
	/* are free.  */

	if (ByteOffset == -1) {
		if (sysfile)
			LOG_ERROR_ARGS (
				"Systemfile cannot allocate contiguously %u blocks",
				ClusterCount);
		status = -ENOSPC;
		goto leave;
	}

#ifdef SMART_ALLOC
	/* FIXME : we try to be smart and keep track of the last offset we were at
	 * need to add the same in delfile so that we put it lower again
	 */
	if (LargeAlloc) {
		osb->cluster_bitmap.ok_retries++;
		LargeAllocOffset = ByteOffset + ClusterCount;
	} else {
		SmallAllocOffset = ByteOffset + ClusterCount;
	}
#endif

	ocfs_set_bits (&osb->cluster_bitmap, ByteOffset, ClusterCount);

	LOG_TRACE_ARGS("gb_s: bit=%d, len=%d\n", ByteOffset, ClusterCount);

	startOffset = osb->vol_layout.bitmap_off;

	status =
	    ocfs_write_metadata (osb, osb->cluster_bitmap.buf, size, startOffset);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}
        
        /* write the bitmap size info to the lock sector */
        /* TODO: optimize by making this part of ocfs_release_lock 
         * for now, it will be back-to-back writes to same sector */
        bm_lock->used_bits = ocfs_count_bits(&osb->cluster_bitmap);
        status = ocfs_write_force_disk(osb, bm_lock, OCFS_SECTOR_SIZE,
				       OCFS_BITMAP_LOCK_OFFSET);
        if (status < 0) {
                LOG_ERROR_STATUS (status);
                goto leave;
        }

	*cluster_off = ByteOffset;
	*cluster_count = ClusterCount;
	status = 0;

      leave:
	ocfs_up_sem (&(osb->vol_alloc_lock));
	if (bLockAcquired) {
		tmpstat = ocfs_release_lock (osb, OCFS_BITMAP_LOCK_OFFSET,
				OCFS_DLM_EXCLUSIVE_LOCK, 0, pLockResource, fe);
		if (tmpstat < 0)
			LOG_ERROR_STATUS (tmpstat);
		LOG_TRACE_STR ("UNLOCK");
	}
	ocfs_put_lockres (pLockResource);
	ocfs_release_file_entry(fe);
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_find_contiguous_space_from_bitmap */

/* ocfs_create_oin_from_entry()
 *
 */
int
ocfs_create_oin_from_entry (ocfs_super * osb,
		    ocfs_file_entry * fe,
		    ocfs_inode ** new_oin,
		    __u64 parent_dir_off, ocfs_inode * parent_oin)
{
	int status = 0;
	__u64 allocSize = 0;
	__u64 endofFile = 0;
	ocfs_inode *oin;
	__u64 lockId;
	int j;
	__s64 tempVbo;
	__s64 tempLbo;
	__u64 tempSize;
	ocfs_extent_group *buffer = NULL;
	ocfs_extent_group *pOcfsExtent;
	bool bRet;

	LOG_ENTRY ();

	/* First insert on the sector node tree... */

	/* Check for state on the disk and notify master */
	*new_oin = NULL;

	/* We have a new file on disk , so create an oin for the file */
	status = ocfs_create_new_oin (&oin, &allocSize, &endofFile, NULL, osb);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	oin->parent_dirnode_off = parent_dir_off;
	oin->chng_seq_num = DISK_LOCK_SEQNUM (fe);
	oin->alloc_size = fe->alloc_size;

	if (fe->this_sector == 0)
		LOG_ERROR_STR ("this_sector=0");

	/* The oins gets Linked into the osb in this function */

	if (fe->attribs & OCFS_ATTRIB_DIRECTORY) {
		lockId = fe->extents[0].disk_off;
		status = ocfs_initialize_oin (oin, osb,
				OCFS_OIN_DIRECTORY | OCFS_OIN_IN_USE, NULL,
				fe->this_sector, lockId);
		if (status < 0) {
			if (status != -EINTR)
				LOG_ERROR_STATUS (status);
			goto leave;
		}
		oin->dir_disk_off = fe->extents[0].disk_off;
	} else {
		status = ocfs_initialize_oin (oin, osb, OCFS_OIN_IN_USE, NULL,
					    fe->this_sector, fe->this_sector);
		if (status < 0) {
			if (status != -EINTR)
				LOG_ERROR_STATUS (status);
			goto leave;
		}

		if (fe->local_ext) {
			for (j = 0; j < fe->next_free_ext; j++) {
				tempVbo = fe->extents[j].file_off;
				tempLbo = fe->extents[j].disk_off;
				tempSize = fe->extents[j].num_bytes;

				/* Add the Extent to extent map */
				bRet = ocfs_add_extent_map_entry (osb, &oin->map,
						tempVbo, tempLbo, tempSize);
				if (!bRet) {
					LOG_ERROR_STATUS (status = -ENOMEM);
					goto leave;
				}
			}
		} else {
			__u32 alloSize;
			__u32 length;

			/* Extents are branched and we are no longer using */
			/* Local Extents for this File Entry. */
			alloSize =
			    (NUM_SECTORS_IN_LEAF_NODE +
			     fe->granularity) * OCFS_SECTOR_SIZE;

			length = (__u32) OCFS_ALIGN (alloSize, osb->sect_size);
			buffer = ocfs_malloc (length);
			if (buffer == NULL) {
				LOG_ERROR_STATUS (status = -ENOMEM);
				goto leave;
			}

			pOcfsExtent = (ocfs_extent_group *) buffer;

			status = ocfs_get_leaf_extent (osb, fe, 0, pOcfsExtent);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto leave;
			}

			while (1) {
				if (!IS_VALID_EXTENT_DATA (pOcfsExtent)) {
					LOG_ERROR_STATUS(status = -EFAIL);
					goto leave;
				}

				for (j = 0; j < pOcfsExtent->next_free_ext; j++) {
					tempVbo = pOcfsExtent->extents[j].file_off;
					tempLbo = pOcfsExtent->extents[j].disk_off;
					tempSize = pOcfsExtent->extents[j].num_bytes;

					/* Add the Extent to extent map */
					bRet =
					    ocfs_add_extent_map_entry (osb,
								   &oin->map,
								   tempVbo,
								   tempLbo,
								   tempSize);
					if (!bRet) {
						LOG_ERROR_STATUS (status =
								  -ENOMEM);
						goto leave;
					}
				}

				if (pOcfsExtent->next_data_ext > 0) {
					if (!pOcfsExtent->next_data_ext) {
						LOG_ERROR_STATUS (status = -EFAIL);
						goto leave;
					}

					status =
					    ocfs_read_sector (osb, pOcfsExtent,
							    pOcfsExtent->
							    next_data_ext);
					if (status < 0) {
						LOG_ERROR_STATUS (status);
						goto leave;
					}
				} else
					break;
			}
		}
	}

	*new_oin = oin;
      leave:
	if (!*new_oin)
		ocfs_release_oin (oin, true);

	ocfs_safefree (buffer);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_create_oin_from_entry */

/* ocfs_find_files_on_disk()
 *
 */
int
ocfs_find_files_on_disk (ocfs_super * osb,
		 __u64 parent_off,
		 struct qstr * file_name,
		 ocfs_file_entry * fe, ocfs_file * ofile)
{
	int status = -ENOENT;
	ocfs_dir_node *pDirNode = NULL;
	__u64 thisDirNode, lockId;
	int tmpstat;
	bool bRet;
	ocfs_lock_res *lockres = NULL;
	bool bReadDirNode = true;
	ocfs_file_entry *dirfe = NULL;
	bool lock_acq = false;

	LOG_ENTRY_ARGS ("(osb=0x%p, poff=%u.%u, fe=0x%p)\n",
			osb, parent_off, fe);

	lockId = parent_off;

	dirfe = ocfs_allocate_file_entry();
	if (dirfe == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto leave;
	}

	/* Get a shared lock on the directory... */
	status = ocfs_acquire_lock (osb, lockId, OCFS_DLM_SHARED_LOCK, FLAG_DIR,
				    &lockres, dirfe);
	if (status < 0) {
		if (status != -EINTR)
			LOG_ERROR_STATUS (status);
		goto leave;
	}

	lock_acq = true;

	if ((ofile == NULL)
	    || ((ofile != NULL) && (ofile->curr_dir_buf == NULL))) {
		pDirNode = ocfs_allocate_dirnode();	
		if (pDirNode == NULL) {
			LOG_ERROR_STATUS (status = -ENOMEM);
			goto leave;
		}

		if (ofile != NULL) {
			ofile->curr_dir_buf = pDirNode;
		}
	} else {
		pDirNode = ofile->curr_dir_buf;
	}

	if ((ofile != NULL) && (ofile->curr_dir_off > 0)) {
		thisDirNode = ofile->curr_dir_off;
		if (pDirNode->node_disk_off == thisDirNode) {
			bReadDirNode = false;
		}
	} else {
		thisDirNode = parent_off;
	}

	if (bReadDirNode) {
		status = ocfs_read_dir_node (osb, pDirNode, thisDirNode);
		if (status < 0) {
			/* Volume should be disabled in this case */
			LOG_ERROR_STATUS (status);
			goto leave;
		}
	}

	status = -ENOENT;

	/* if file_name is null here, it means that we want to walk the */
	/* directory for all files if it is not null, it means we want */
	/* a particular file */
	if (file_name == NULL) {
		bRet = ocfs_walk_dir_node (osb, pDirNode, fe, ofile);
		if (bRet)
			status = 0;
	} else {
		bRet = ocfs_search_dir_node (osb, pDirNode, file_name, fe, ofile);
		if (bRet)
			status = 0;
	}

	if (status >= 0 && (fe->attribs & OCFS_ATTRIB_FILE_CDSL)) {
		/* Return the relevant CDSL for this node */
		ocfs_find_create_cdsl (osb, fe);
	}

      leave:
	if (lock_acq) {
		tmpstat = ocfs_release_lock (osb, lockId, OCFS_DLM_SHARED_LOCK,
					     FLAG_DIR, lockres, dirfe);
		if (tmpstat < 0) {
			LOG_ERROR_STATUS (tmpstat);
			/* Volume should be disabled in this case */
		}
	}

	ocfs_release_file_entry(dirfe);
	ocfs_put_lockres (lockres);

	if (ofile == NULL && pDirNode) {
		ocfs_release_dirnode (pDirNode);
	}

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_find_files_on_disk */

/* ocfs_initialize_dir_node()
 *
 */
void ocfs_initialize_dir_node (ocfs_super * osb, ocfs_dir_node * dir_node,
			       __u64 bitmap_off, __u64 file_off, __u32 node)
{
	LOG_ENTRY ();

	memset (dir_node, 0, sizeof (ocfs_dir_node));
	strcpy (dir_node->signature, OCFS_DIR_NODE_SIGNATURE);

	dir_node->num_ents = (__u8) osb->max_dir_node_ent;
	dir_node->node_disk_off = bitmap_off;
	dir_node->alloc_file_off = file_off;
	dir_node->alloc_node = node;

	DISK_LOCK_CURRENT_MASTER (dir_node) = OCFS_INVALID_NODE_NUM;

	dir_node->free_node_ptr = INVALID_NODE_POINTER;
	dir_node->next_node_ptr = INVALID_NODE_POINTER;
	dir_node->indx_node_ptr = INVALID_NODE_POINTER;
	dir_node->next_del_ent_node = INVALID_NODE_POINTER;
	dir_node->head_del_ent_node = INVALID_NODE_POINTER;

	dir_node->first_del = INVALID_DIR_NODE_INDEX;
	dir_node->index_dirty = 0;

	LOG_EXIT ();
	return;
}				/* ocfs_initialize_dir_node */

/* ocfs_delete_file_entry()
 *
 */
int
ocfs_delete_file_entry (ocfs_super * osb,
		 ocfs_file_entry * fe, __u64 parent_off, __s32 log_node_num)
{
	int status = 0;
	__u32 size;
	ocfs_cleanup_record *pCleanupLogRec = NULL;

	LOG_ENTRY ();

	size = sizeof (ocfs_cleanup_record);
	size = (__u32) OCFS_ALIGN (size, OCFS_PAGE_SIZE);

	pCleanupLogRec = ocfs_malloc (size);
	if (pCleanupLogRec == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto leave;
	}

	/* Now start writing the cleanup log of the filentry master. */
	/* It is this node for normal cases and or the node we are doing */
	/* recovery for. */

	pCleanupLogRec->log_id = osb->curr_trans_id;
	pCleanupLogRec->log_type = LOG_DELETE_ENTRY;

	pCleanupLogRec->rec.del.node_num = log_node_num;
	pCleanupLogRec->rec.del.ent_del = fe->this_sector;
	pCleanupLogRec->rec.del.parent_dirnode_off = parent_off;
	pCleanupLogRec->rec.del.flags = 0;

	status = ocfs_write_node_log (osb, (ocfs_log_record *) pCleanupLogRec,
				   log_node_num, LOG_CLEANUP);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	if (fe->link_cnt) {
		/* Decrement Link count when implementing links... TODO*/
		OCFS_SET_FLAG (fe->sync_flags, OCFS_SYNC_FLAG_NAME_DELETED);
		fe->sync_flags &= (~OCFS_SYNC_FLAG_VALID);
	} else {
		OCFS_SET_FLAG (fe->sync_flags, OCFS_SYNC_FLAG_MARK_FOR_DELETION);
		fe->sync_flags &= (~OCFS_SYNC_FLAG_VALID);
	}

	status = ocfs_write_file_entry (osb, fe, fe->this_sector);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	/* Free up all the bits in the bitmap. */
	if (fe->attribs & OCFS_ATTRIB_DIRECTORY) {
		/* Write to the cleanup record which bits need to be freed for */
		/* the ocfs_dir_node */

		/* Iterate through all the dir nodes for this directory and free them */

		/* TODO Free the index nodes too. */

		status = ocfs_free_directory_block (osb, fe, log_node_num);
		if (status < 0)
			LOG_ERROR_STATUS (status);
		goto leave;
	} else {
		/* Write to the cleanup record which bits need to be freed for */
		/* the cluster bitmap */
		status = ocfs_free_file_extents (osb, fe, log_node_num);
		if (status < 0)
			LOG_ERROR_STATUS (status);
		goto leave;
	}

      leave:
	ocfs_safefree (pCleanupLogRec);

	LOG_EXIT_STATUS (status);
	return (status);
}				/* ocfs_delete_file_entry */

/* ocfs_rename_file()
 *
 */
int
ocfs_rename_file (ocfs_super * osb,
		__u64 parent_off, struct qstr * file_name, __u64 file_off)
{
	int status = 0;
	int tmpstat;
	ocfs_dir_node *pLockNode = NULL;
	ocfs_file_entry *fe = NULL;
	ocfs_file_entry *lockfe = NULL;
	__u64 changeSeqNum = 0;
	bool bAcquiredLock = false;
	__u32 lockFlags = 0;
	ocfs_lock_res *pLockResource = NULL;
	__u64 lockId = 0;
	bool bParentLockAcquired = false;
	__u32 parentLockFlags;
	ocfs_lock_res *pParentLockResource = NULL;
	__u64 parentLockId;
	__u32 index;

	LOG_ENTRY ();

	parentLockId = parent_off;
	parentLockFlags = (FLAG_FILE_CREATE | FLAG_DIR);
	status = ocfs_acquire_lock (osb, parentLockId, OCFS_DLM_EXCLUSIVE_LOCK,
				    (__u32) parentLockFlags, &pParentLockResource,
				    NULL);
	if (status < 0) {
		if (status != -EINTR)
			LOG_ERROR_STATUS (status);
		goto leave;
	}

	bParentLockAcquired = true;

	status = ocfs_get_file_entry (osb, &fe, file_off);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	/* lock file ent for a dir is out in the 1st extent, this_sector for file */
	if (fe->attribs & OCFS_ATTRIB_DIRECTORY) {
		lockId = fe->extents[0].disk_off;
		lockFlags = (FLAG_DIR | FLAG_FILE_RENAME);
		lockfe = ocfs_allocate_file_entry(); 
		if (lockfe == NULL) {
			LOG_ERROR_STATUS (status = -ENOMEM);
			goto leave;
		}
	} else {
		lockId = fe->this_sector;
		lockFlags = FLAG_FILE_RENAME;
		lockfe = fe;
	}

	status = ocfs_acquire_lock (osb, lockId, OCFS_DLM_EXCLUSIVE_LOCK,
				    lockFlags, &pLockResource, lockfe);
	if (status < 0) {
		if (status != -EINTR)
			LOG_ERROR_STATUS (status);
		goto leave;
	}

	bAcquiredLock = true;

	/* Change the name and write it back.... */
        fe->filename[0] = '\0';
	strncpy (fe->filename, file_name->name, file_name->len);
	fe->filename[file_name->len] = '\0';

	DISK_LOCK_SEQNUM (fe) = changeSeqNum;

	/* Set the Valid bit here */
	SET_VALID_BIT (fe->sync_flags);
	fe->sync_flags &= ~(OCFS_SYNC_FLAG_CHANGE);

	status = ocfs_get_file_entry (osb, (ocfs_file_entry **) & pLockNode,
				      fe->dir_node_ptr);
	pLockNode->index_dirty = 1;
	pLockNode->bad_off = (fe->this_sector - fe->dir_node_ptr) / osb->sect_size;
	pLockNode->bad_off -= 1;

	for (index = 0; index < pLockNode->num_ent_used; index++) {
		if (pLockNode->index[index] == pLockNode->bad_off)
			break;
	}

	if (index < pLockNode->num_ent_used) {
		memmove (&pLockNode->index[index], &pLockNode->index[index + 1],
			 pLockNode->num_ent_used - (index + 1));
		pLockNode->index[pLockNode->num_ent_used - 1] = pLockNode->bad_off;

		status = ocfs_write_file_entry (osb, (ocfs_file_entry *) pLockNode,
						fe->dir_node_ptr);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto leave;
		}
	}

	status = ocfs_write_file_entry (osb, fe, fe->this_sector);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	LOG_TRACE_ARGS ("sector=%u.%u, name=%s\n", HI (fe->this_sector),
			LO (fe->this_sector), fe->filename);

	/* Update the disk as the other node will not see this file directory */
	if (DISK_LOCK_FILE_LOCK (pLockNode) < OCFS_DLM_ENABLE_CACHE_LOCK) {
		status = ocfs_force_put_file_entry (osb, fe, true);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto leave;
		}
	}

	if (index < pLockNode->num_ent_used) {
		status = ocfs_reindex_dir_node (osb, fe->dir_node_ptr, NULL);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto leave;
		}
	}

      leave:

	if (bAcquiredLock) {
		tmpstat = ocfs_release_lock (osb, lockId, OCFS_DLM_EXCLUSIVE_LOCK,
					    lockFlags, pLockResource, lockfe);
		if (tmpstat < 0)
			LOG_ERROR_STATUS (tmpstat);
	}

	if (bParentLockAcquired) {
		tmpstat = ocfs_release_lock (osb, parentLockId,
				OCFS_DLM_EXCLUSIVE_LOCK, parentLockFlags,
				pParentLockResource, NULL);
		if (tmpstat < 0)
			LOG_ERROR_STATUS (tmpstat);
	}

	if (lockfe != fe)
		ocfs_release_file_entry (lockfe);
	ocfs_release_file_entry (fe);
	ocfs_release_file_entry ((ocfs_file_entry *) pLockNode);
	ocfs_put_lockres (pLockResource);
	ocfs_put_lockres (pParentLockResource);
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_rename_file */

/* ocfs_del_file()
 *
 */
int
ocfs_del_file (ocfs_super * osb, __u64 parent_off, __u32 flags, __u64 file_off)
{
	int status = 0;
	int tmpstat;
	ocfs_file_entry *fe = NULL;
	__u32 size = 0;
	ocfs_dir_node *pLockNode = NULL;
	__u32 lockFlags = 0;
	bool bAcquiredLock = false;
	ocfs_lock_res *pLockResource = NULL;
	ocfs_cleanup_record *pCleanupLogRec = NULL;
	ocfs_log_record *pOcfsLogRec;
	__u64 lockId = 0;
	__u32 log_node_num = OCFS_INVALID_NODE_NUM;
	bool empty = false;

	LOG_ENTRY_ARGS ("(osb=0x%p, poff=%u.%u, fl=%u, foff=%u.%u)\n",
			osb, HILO(parent_off), flags, HILO(file_off));

	status = ocfs_get_file_entry (osb, &fe, file_off);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	if (fe->attribs & OCFS_ATTRIB_DIRECTORY) {
		lockId = fe->extents[0].disk_off;
		lockFlags = (FLAG_FILE_DELETE | FLAG_DIR);
		pLockNode = (ocfs_dir_node *) ocfs_allocate_file_entry ();
		if (pLockNode == NULL) {
			LOG_ERROR_STATUS (status = -ENOMEM);
			goto leave;
		}
	} else {
		lockId = fe->this_sector;
		lockFlags = (FLAG_FILE_DELETE);
		pLockNode = (ocfs_dir_node *)fe;
	}
	status = ocfs_acquire_lock (osb, lockId, OCFS_DLM_EXCLUSIVE_LOCK,
			lockFlags, &pLockResource, (ocfs_file_entry *) pLockNode);
	if (status < 0) {
		if (status != -EINTR && status != -EBUSY)
			LOG_ERROR_STATUS (status);
		goto leave;
	}

	bAcquiredLock = true;

	size = sizeof (ocfs_cleanup_record);
	size = (__u32) OCFS_ALIGN (size, OCFS_PAGE_SIZE);

	pCleanupLogRec = ocfs_malloc (size);
	if (pCleanupLogRec == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto leave;
	}

	log_node_num = osb->node_num;

	if (flags & FLAG_DEL_NAME) {
		/* Now start writing the cleanup log of the filentry master. */
		/* It is this node for normal cases and or the node we are doing */
		/* recovery for. */
		pCleanupLogRec->log_id = osb->curr_trans_id;
		pCleanupLogRec->log_type = LOG_DELETE_ENTRY;

		pCleanupLogRec->rec.del.node_num = log_node_num;
		pCleanupLogRec->rec.del.ent_del = fe->this_sector;
		pCleanupLogRec->rec.del.parent_dirnode_off = parent_off;
		pCleanupLogRec->rec.del.flags = 0;

		status = ocfs_write_node_log (osb,
				(ocfs_log_record *) pCleanupLogRec,
				log_node_num, LOG_CLEANUP);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto leave;
		}
		status = 0;
		goto leave;
	}

	/* Ask for a lock on the file to ensure there are no open oin's */
	/* on the file on any node */
	if (fe->attribs & OCFS_ATTRIB_DIRECTORY) {
		status = ocfs_is_dir_empty (osb, pLockNode, &empty);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto leave;
		}

		if ((!empty) && !(flags & FLAG_DEL_NAME)) {
			status = -ENOTEMPTY;
			goto leave;
		}
	}

	pOcfsLogRec = (ocfs_log_record *) pCleanupLogRec;

	pOcfsLogRec->log_id = osb->curr_trans_id;
	pOcfsLogRec->log_type = LOG_MARK_DELETE_ENTRY;

	pOcfsLogRec->rec.del.node_num = log_node_num;
	pOcfsLogRec->rec.del.ent_del = fe->this_sector;
	pOcfsLogRec->rec.del.parent_dirnode_off = parent_off;

	if (flags & FLAG_RESET_VALID) {
		pOcfsLogRec->rec.del.flags = FLAG_RESET_VALID;
	} else {
		pOcfsLogRec->rec.del.flags = 0;
	}

	status = ocfs_write_log (osb, pOcfsLogRec, LOG_RECOVER);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	status = ocfs_delete_file_entry (osb, fe, parent_off, log_node_num);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	/* free up fileentry */

      leave:

        /* NEW: adding a fake release lock for the dead file entry here */
        /* need this to alert dentry-owners on other nodes */
        /* Release the file lock if we acquired it */
	if (bAcquiredLock && lockFlags != 0 && lockId != 0) {
		tmpstat = ocfs_release_lock (osb, lockId, OCFS_DLM_EXCLUSIVE_LOCK,
					    lockFlags, pLockResource,
					    (ocfs_file_entry *)pLockNode);
		if (tmpstat < 0)
			LOG_ERROR_STATUS (tmpstat);
	}

	if ((fe != (ocfs_file_entry *) pLockNode))
		ocfs_release_file_entry ((ocfs_file_entry *) pLockNode);

	ocfs_release_file_entry (fe);
	ocfs_safefree (pCleanupLogRec);
	ocfs_put_lockres (pLockResource);
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_del_file */

/* ocfs_extend_file()
 *
 */
int ocfs_extend_file (ocfs_super * osb, __u64 parent_off,
		ocfs_inode * oin, __u64 file_size, __u64 * file_off)
{
	int status = 0;
	int tmpstat;
	ocfs_dir_node *pLockNode = NULL;
	ocfs_file_entry *fileEntry = NULL;
	__u64 tempOffset = 0;
	__u64 allocSize = 0;
	__u32 size;
	__u64 bitmapOffset = 0;
	__u64 numClustersAlloc = 0;
	__u64 lockId = 0;
	__u32 lockFlags = 0;
	bool bFileLockAcquired = false;
	bool bAcquiredLock = false;
	ocfs_lock_res *pLockResource = NULL;
	__u64 changeSeqNum = 0;
	__u64 actualDiskOffset = 0;
	__u64 actualLength = 0;
	bool bCacheLock = false;

	LOG_ENTRY ();

	if (file_size == 0)
		goto leave;

	fileEntry = ocfs_allocate_file_entry ();
	if (fileEntry == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto leave;
	}

	status = ocfs_read_file_entry (osb, fileEntry, *file_off);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	if (!IS_VALID_FILE_ENTRY(fileEntry)) {
		LOG_ERROR_ARGS ("Invalid fe at offset %u.%u", HI(*file_off),
				LO(*file_off));
		status = -EFAIL;
		goto leave;
	}

	/* Grab a lock on the entry found if we have more than 1 extents and */
	/* also make this node the master*/

	/* now we always take an EXTEND lock */
	lockId = fileEntry->this_sector;
	lockFlags = FLAG_FILE_EXTEND;
	bFileLockAcquired = true;
	pLockNode = (ocfs_dir_node *)fileEntry;

	if ((DISK_LOCK_FILE_LOCK (fileEntry) == OCFS_DLM_ENABLE_CACHE_LOCK) &&
	    (DISK_LOCK_CURRENT_MASTER (fileEntry) == osb->node_num)) {
		bCacheLock = true;
	}

	status = ocfs_acquire_lock (osb, lockId,
				    (bCacheLock ? OCFS_DLM_ENABLE_CACHE_LOCK :
				                  OCFS_DLM_EXCLUSIVE_LOCK),
				    lockFlags, &pLockResource,
				    (ocfs_file_entry *) pLockNode);
	if (status < 0) {
		if (status != -EINTR)
			LOG_ERROR_STATUS (status);
		goto leave;
	}

	bAcquiredLock = true;

	if (bCacheLock) {
		DISK_LOCK_FILE_LOCK (fileEntry) = OCFS_DLM_ENABLE_CACHE_LOCK;
		DISK_LOCK_CURRENT_MASTER (fileEntry) = osb->node_num;
	}

	if (file_size > (__s64) fileEntry->alloc_size) {
		allocSize = file_size - fileEntry->alloc_size;

		/* TODO: We can add something here so that after 2-3 allocations, */
		/* we give a lot more disk space to the file than the allocSize so */
		/* in order to try to use the Extents of File Entry only and ofcourse */
		/* the file will have more contigous disk space. */
		{
			__u64 tempSize = fileEntry->alloc_size;

			if (tempSize > ONE_MEGA_BYTE)
				tempSize = ONE_MEGA_BYTE;
			allocSize += (tempSize * 2);
			if (allocSize < fileEntry->alloc_size / 100) {
				allocSize = fileEntry->alloc_size / 100;
				allocSize = OCFS_ALIGN(allocSize, (10*ONE_MEGA_BYTE));
			}
		}

		status = ocfs_find_contiguous_space_from_bitmap (osb, allocSize,
					&bitmapOffset, &numClustersAlloc, false);
		if (status < 0) {
			if (status != -ENOSPC)
				LOG_ERROR_STATUS (status);
			goto leave;
		}

		actualDiskOffset =
		    (bitmapOffset * osb->vol_layout.cluster_size) +
		    osb->vol_layout.data_start_off;
		actualLength =
		    (__u64) (numClustersAlloc * osb->vol_layout.cluster_size);

		LOG_TRACE_ARGS ("ocfs: extend %s fe=%u.%u (%u.%u + %u.%u = %u.%u)\n",
		       fileEntry->filename, HILO(fileEntry->this_sector),
		       HILO(fileEntry->alloc_size), HILO(actualLength),
		       HILO(fileEntry->alloc_size + actualLength));

		/* note: ok if oin is null here, not used in ocfs_allocate_extent */
		status = ocfs_allocate_extent (osb, oin, fileEntry,
					actualDiskOffset, actualLength);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto leave;
		}

		/* update the total allocation size here */
		fileEntry->alloc_size += actualLength;

		if (oin) {
			ocfs_down_sem (&(oin->main_res), true);
			oin->alloc_size = fileEntry->alloc_size;
			ocfs_up_sem (&(oin->main_res));
		}

		/* no need to do OCFS_SECTOR_ALIGN once the allocation size is correct. */
		DISK_LOCK_SEQNUM (fileEntry) = changeSeqNum;
	}

	/* Update tha file size and add the new one to old one. */
	fileEntry->file_size = file_size;

	/* Set the Valid bit and reset the change bit here... TODO */
	SET_VALID_BIT (fileEntry->sync_flags);
	fileEntry->sync_flags &= ~(OCFS_SYNC_FLAG_CHANGE);

	fileEntry->modify_time = CURRENT_TIME;

	tempOffset = fileEntry->this_sector;
	size = (__u32) OCFS_ALIGN (sizeof (ocfs_file_entry), osb->sect_size);

	status = ocfs_write_file_entry (osb, (ocfs_file_entry *) fileEntry,
					tempOffset);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	/* Update all open oins */

	/* Our local update is done, if somebody had asked for a bdcast lock  */
	/* He shd set the state */

      leave:
	if (bAcquiredLock) {
		if (bFileLockAcquired)
			lockFlags |= FLAG_FILE_UPDATE_OIN;

		tmpstat = ocfs_release_lock (osb, lockId, OCFS_DLM_EXCLUSIVE_LOCK,
					     lockFlags, pLockResource, fileEntry);
		if (tmpstat < 0)
			LOG_ERROR_STATUS (tmpstat);
	}

	ocfs_release_file_entry (fileEntry);
	ocfs_put_lockres (pLockResource);
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_extend_file */

/* ocfs_change_file_size()
 *
 */
int ocfs_change_file_size (ocfs_super * osb,
		    __u64 parent_off,
		    ocfs_inode * oin,
		    __u64 file_size, __u64 * file_off, struct iattr *attr)
{
	int status = 0;
	int tmpstat;
	ocfs_dir_node *pLockNode = NULL;
	ocfs_file_entry *fileEntry = NULL;
	__u64 dirOffset = 0;
	__u32 size;
	bool bFileLockAcquired = false;
	bool bAcquiredLock = false;
	ocfs_lock_res *pLockResource = NULL;
	__u64 changeSeqNum = 0;
	__u64 lockId = 0;
	__u32 lockFlags = 0;
	bool bCacheLock = false;

	LOG_ENTRY ();

	fileEntry = ocfs_allocate_file_entry ();
	if (fileEntry == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto leave;
	}

	status = ocfs_read_file_entry (osb, fileEntry, *file_off);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	if (!IS_VALID_FILE_ENTRY(fileEntry)) {
		LOG_ERROR_ARGS ("Invalid fe at offset %u.%u", HI(*file_off),
				LO(*file_off));
		status = -EFAIL;
		goto leave;
	}

	/* Acquire the Lock using TCP/IP and disk based locking */
	if ((DISK_LOCK_FILE_LOCK (fileEntry) == OCFS_DLM_ENABLE_CACHE_LOCK) &&
	    (DISK_LOCK_CURRENT_MASTER (fileEntry) == osb->node_num)) {
		bCacheLock = true;
	}

	/* now we always take an UPDATE lock */
	lockId = fileEntry->this_sector;
	lockFlags = FLAG_FILE_UPDATE;
	bFileLockAcquired = true;
	pLockNode = (ocfs_dir_node *)fileEntry;

	status = ocfs_acquire_lock (osb, lockId,
				    (bCacheLock ? OCFS_DLM_ENABLE_CACHE_LOCK :
				    		  OCFS_DLM_EXCLUSIVE_LOCK),
				    lockFlags, &pLockResource,
				    (ocfs_file_entry *) pLockNode);
	if (status < 0) {
		if (status != -EINTR)
			LOG_ERROR_STATUS (status);
		goto leave;
	}

	bAcquiredLock = true;

	if (bCacheLock) {
		DISK_LOCK_FILE_LOCK (fileEntry) = OCFS_DLM_ENABLE_CACHE_LOCK;
		DISK_LOCK_CURRENT_MASTER (fileEntry) = osb->node_num;
	}

	fileEntry->modify_time = CURRENT_TIME;

	DISK_LOCK_SEQNUM (fileEntry) = changeSeqNum;
	if (attr->ia_valid & ATTR_SIZE)
		fileEntry->file_size = attr->ia_size;
	if (attr->ia_valid & ATTR_UID)
		fileEntry->uid = attr->ia_uid;
	if (attr->ia_valid & ATTR_GID)
		fileEntry->gid = attr->ia_gid;
	if (attr->ia_valid & ATTR_MODE)
		fileEntry->prot_bits = attr->ia_mode & 0007777;
	if (attr->ia_valid & ATTR_CTIME)
		fileEntry->create_time = attr->ia_ctime;
	if (attr->ia_valid & ATTR_MTIME)
		fileEntry->modify_time = attr->ia_mtime;

	/* Set the valid bit here */
	SET_VALID_BIT (fileEntry->sync_flags);
	fileEntry->sync_flags &= ~(OCFS_SYNC_FLAG_CHANGE);

	dirOffset = fileEntry->this_sector;

	size = (__u32) OCFS_SECTOR_ALIGN (sizeof (ocfs_file_entry));
	status =
	    ocfs_write_file_entry (osb, (ocfs_file_entry *) fileEntry, dirOffset);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

      leave:
	if (bAcquiredLock) {
		if (bFileLockAcquired)
			lockFlags |= FLAG_FILE_UPDATE_OIN;

		tmpstat = ocfs_release_lock (osb, lockId, OCFS_DLM_EXCLUSIVE_LOCK,
					     lockFlags, pLockResource, fileEntry);
		if (tmpstat < 0)
			LOG_ERROR_STATUS (tmpstat);
	}

	ocfs_release_file_entry (fileEntry);
	ocfs_put_lockres (pLockResource);
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_change_file_size */

/* ocfs_get_dirnode()
 *
 */
int ocfs_get_dirnode(ocfs_super *osb, ocfs_dir_node *lockn, __u64 lockn_off,
		     ocfs_dir_node *dirn, bool *invalid_dirnode)
{
	int status = 0;
	__u64 node_off;
	bool hden = false;

	LOG_ENTRY_ARGS ("(lockn_off=%u.%u)\n", HILO (lockn_off));

	*invalid_dirnode = false;

	if (lockn->head_del_ent_node != INVALID_NODE_POINTER) {
		node_off = lockn->head_del_ent_node;
		hden = true;
	} else {
    		if (lockn->free_node_ptr == INVALID_NODE_POINTER)
			node_off = lockn_off;
		else
			node_off = lockn->free_node_ptr;
	}

	status = ocfs_read_dir_node (osb, dirn, node_off);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	status = ocfs_validate_dirnode (osb, dirn);
	if (status >= 0) {
		if (dirn->node_disk_off != lockn->head_del_ent_node)
			goto leave;

		if (dirn->num_ent_used < osb->max_dir_node_ent)
			goto leave;
	} else if (status != -EBADSLT) {
		LOG_ERROR_STATUS (status);
		goto leave;
	} else {
		*invalid_dirnode = true;
		status = 0;
	}

	/* dirn with no free slots and pointed to by head_del_ent_node */
	node_off = lockn_off;
	while (1) {
		*invalid_dirnode = false;

		status = ocfs_read_dir_node (osb, dirn, node_off);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto leave;
		}

		status = ocfs_validate_dirnode (osb, dirn);
		if (status >= 0) {
			if (dirn->num_ent_used < osb->max_dir_node_ent) {
				if (hden)
					ocfs_update_hden (lockn, dirn,
							  dirn->node_disk_off);
				goto leave;
			}
		} else if (status != -EBADSLT) {
			LOG_ERROR_STATUS (status);
			goto leave;
		} else {
			*invalid_dirnode = true;
			status = 0;
		}

		node_off = dirn->next_node_ptr;

		if (node_off == INVALID_NODE_POINTER) {
			if (hden && !*invalid_dirnode)
				ocfs_update_hden (lockn, dirn,
						  INVALID_NODE_POINTER);
			goto leave;
		}
	}

leave:
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_get_dirnode */

/* ocfs_create_directory()
 *
 */
int ocfs_create_directory (ocfs_super * osb, __u64 parent_off, ocfs_file_entry * fe)
{
	int status = 0;
	int tmpstat;
	ocfs_file_entry *fileEntry = NULL;
	ocfs_dir_node *PDirNode = NULL;
	ocfs_dir_node *PNewDirNode = NULL;
	ocfs_dir_node *pLockNode = NULL;
	__u64 allocSize = 0;
	__u64 bitmapOffset;
	__u64 numClustersAlloc = 0;
	__u64 fileOffset = 0;
	__u64 lockId = 0;
	ocfs_lock_res *pLockResource = NULL;
	__u32 lockFlags = 0;
	bool bAcquiredLock = false;
	bool invalid_dirnode = false;

	LOG_ENTRY ();

	fileEntry = fe;

	pLockNode = (ocfs_dir_node *) ocfs_allocate_file_entry ();
	if (pLockNode == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto leave;
	}

	lockId = parent_off;
	lockFlags = FLAG_FILE_CREATE | FLAG_DIR;

	status = ocfs_acquire_lock (osb, lockId, OCFS_DLM_EXCLUSIVE_LOCK,
				    lockFlags, &pLockResource,
				    (ocfs_file_entry *) pLockNode);
	if (status < 0) {
		if (status != -EINTR)
			LOG_ERROR_STATUS (status);
		goto leave;
	}

	bAcquiredLock = true;

	/* Zero out the entry for the file and rewrite it back to the disk */
	/* Also, the other nodes should update their cache bitmap for file */
	/* ent to mark this one as free now. */

	allocSize = osb->vol_layout.dir_node_size;

	status = ocfs_alloc_node_block (osb, allocSize, &bitmapOffset,
				&fileOffset, &numClustersAlloc, osb->node_num,
				DISK_ALLOC_DIR_NODE);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	/* update the total allocation size here */
	fileEntry->alloc_size = osb->vol_layout.dir_node_size;
	fileEntry->extents[0].disk_off = bitmapOffset;
	fileEntry->file_size = osb->vol_layout.dir_node_size;
	fileEntry->next_del = INVALID_DIR_NODE_INDEX;

	if (DISK_LOCK_FILE_LOCK (pLockNode) != OCFS_DLM_ENABLE_CACHE_LOCK)
		DISK_LOCK_FILE_LOCK (fileEntry) = OCFS_DLM_NO_LOCK;

	PDirNode = ocfs_allocate_dirnode();
	if (PDirNode == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto leave;
	}

	PNewDirNode = PDirNode;

	ocfs_initialize_dir_node (osb, PNewDirNode, bitmapOffset, fileOffset,
				  osb->node_num);

	DISK_LOCK_CURRENT_MASTER (PNewDirNode) = osb->node_num;
	DISK_LOCK_FILE_LOCK (PNewDirNode) = OCFS_DLM_ENABLE_CACHE_LOCK;
	PNewDirNode->dir_node_flags |= DIR_NODE_FLAG_ROOT;

	status = ocfs_write_metadata (osb, PNewDirNode,
				      osb->vol_layout.dir_node_size,
				      PNewDirNode->node_disk_off);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	// do we need to keep this???
	status = ocfs_write_dir_node (osb, PNewDirNode, -1);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	if (pLockResource->lock_type != OCFS_DLM_ENABLE_CACHE_LOCK) {
		status = ocfs_write_force_dir_node (osb, PNewDirNode, -1);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto leave;
		}
	}

	status = ocfs_get_dirnode(osb, pLockNode, parent_off, PDirNode,
				  &invalid_dirnode);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	if (DISK_LOCK_FILE_LOCK (pLockNode) != OCFS_DLM_ENABLE_CACHE_LOCK)
		DISK_LOCK_FILE_LOCK (fileEntry) = OCFS_DLM_NO_LOCK;

	OcfsQuerySystemTime (&DISK_LOCK_LAST_WRITE (fileEntry));
	OcfsQuerySystemTime (&DISK_LOCK_LAST_READ (fileEntry));

	DISK_LOCK_WRITER_NODE (fileEntry) = osb->node_num;
	DISK_LOCK_READER_NODE (fileEntry) = osb->node_num;

	status = ocfs_insert_file (osb, PDirNode, fileEntry, pLockNode,
				   pLockResource, invalid_dirnode);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	bAcquiredLock = false;

      leave:
	if (bAcquiredLock) {
		tmpstat = ocfs_release_lock (osb, lockId, OCFS_DLM_EXCLUSIVE_LOCK,
					     lockFlags, pLockResource,
					     (ocfs_file_entry *) pLockNode);
		if (tmpstat < 0)
			LOG_ERROR_STATUS (tmpstat);
	}
	ocfs_release_dirnode (PDirNode);
	ocfs_release_file_entry ((ocfs_file_entry *) pLockNode);
	ocfs_put_lockres (pLockResource);
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_create_directory */

/* ocfs_create_file()
 *
 */
int ocfs_create_file (ocfs_super * osb, __u64 parent_off, ocfs_file_entry * fe)
{
	int status = 0;
	int tmpstat;
	ocfs_file_entry *fileEntry = NULL;
	ocfs_dir_node *PDirNode = NULL;
	ocfs_dir_node *pLockNode = NULL;
	__u64 lockId = 0;
	ocfs_lock_res *pLockResource = NULL;
	__u32 lockFlags = 0;
	bool bAcquiredLock = false;
	bool invalid_dirnode = false;

	LOG_ENTRY_ARGS ("(osb=0x%p, poff=%u.%u, fe=0x%p)\n", osb,
		       	HILO(parent_off), fe);

	/* Zero out the entry for the file and rewrite it back to the disk */
	/* Also, the other nodes should update their cache bitmap for file */
	/* ent to mark this one as free now. */
	pLockNode = (ocfs_dir_node *) ocfs_allocate_file_entry ();
	if (pLockNode == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto leave;
	}

	lockId = parent_off;
	lockFlags = FLAG_FILE_CREATE | FLAG_DIR;

	status = ocfs_acquire_lock (osb, lockId, OCFS_DLM_EXCLUSIVE_LOCK,
				    lockFlags, &pLockResource,
				    (ocfs_file_entry *) pLockNode);
	if (status < 0) {
		if (status != -EINTR)
			LOG_ERROR_STATUS (status);
		goto leave;
	}

	bAcquiredLock = true;

	/* Change the name and write it back... */
	fileEntry = fe;

	PDirNode = ocfs_allocate_dirnode();
	if (PDirNode == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto leave;
	}

	status = ocfs_get_dirnode(osb, pLockNode, parent_off, PDirNode,
				  &invalid_dirnode);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	OcfsQuerySystemTime (&DISK_LOCK_LAST_WRITE (fileEntry));
	OcfsQuerySystemTime (&DISK_LOCK_LAST_READ (fileEntry));

	DISK_LOCK_WRITER_NODE (fileEntry) = osb->node_num;
	DISK_LOCK_READER_NODE (fileEntry) = osb->node_num;

	fileEntry->next_del = INVALID_DIR_NODE_INDEX;

	status = ocfs_insert_file (osb, PDirNode, fileEntry, pLockNode,
				   pLockResource, invalid_dirnode);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	bAcquiredLock = false;

      leave:
	if (bAcquiredLock) {
		tmpstat = ocfs_release_lock (osb, lockId, OCFS_DLM_EXCLUSIVE_LOCK,
					     lockFlags, pLockResource,
					     (ocfs_file_entry *) pLockNode);
		if (tmpstat < 0)
			LOG_ERROR_STATUS (tmpstat);
	}
	ocfs_release_dirnode (PDirNode);
	ocfs_release_file_entry ((ocfs_file_entry *) pLockNode);
	ocfs_put_lockres (pLockResource);
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_create_file */

/*
 * ocfs_create_modify_file()
 *
 * Looks up for the existence of the filename in the oins in memory
 * and the entries on the disk.
 */
int
ocfs_create_modify_file (ocfs_super * osb,
		  __u64 parent_off,
		  ocfs_inode * oin,
		  struct qstr * file_name,
		  __u64 file_size,
		  __u64 * file_off, __u32 flags, ocfs_file_entry * fe, struct iattr *attr)
{
	int status = 0;
	int tmpstat = 0;
	ocfs_file_entry *newfe = NULL;
	__u64 changeSeqNum = 0;
        __u64 t;

	LOG_ENTRY_ARGS ("(osb=0x%p, poff=%u.%u, fe=0x%p)\n",
			osb, HILO(parent_off), fe);

	ocfs_start_trans (osb);

	changeSeqNum = osb->curr_trans_id;
	switch (flags) {
	    case FLAG_FILE_EXTEND:
		    status = ocfs_extend_file (osb, parent_off, oin, file_size,
					       file_off);
		    if (status < 0) {
			    if (status != -ENOSPC && status != -EINTR)
			    	LOG_ERROR_STATUS (status);
			    goto leave;
		    }
		    break;

	    case FLAG_FILE_DELETE:
		    status = ocfs_del_file (osb, parent_off, 0, *file_off);
		    if (status < 0) {
			    if (status != -EINTR && status != -ENOTEMPTY &&
			       	status != -EBUSY)
				    LOG_ERROR_STATUS (status);
			    goto leave;
		    }
		    break;

	    case FLAG_FILE_CREATE_DIR:
		    if (fe == NULL) {
			    newfe = ocfs_allocate_file_entry ();
			    if (newfe == NULL) {
				    LOG_ERROR_STATUS (status = -ENOMEM);
				    goto leave;
			    }
		    } else {
			    newfe = fe;
		    }

		    /* Change the name and write it back... */
		    strncpy (newfe->filename, file_name->name, file_name->len);
                    newfe->filename[file_name->len]='\0';
		    newfe->filename_len = file_name->len;

		    /* Set the valid bit here */
		    SET_VALID_BIT (newfe->sync_flags);
		    newfe->sync_flags &= ~(OCFS_SYNC_FLAG_CHANGE);
		    newfe->attribs |= (OCFS_ATTRIB_DIRECTORY);

		    DISK_LOCK_SEQNUM (newfe) = changeSeqNum;

		    /* Initialize the lock state */
		    DISK_LOCK_CURRENT_MASTER (newfe) = osb->node_num;
		    DISK_LOCK_FILE_LOCK (newfe) = OCFS_DLM_ENABLE_CACHE_LOCK;
		    DISK_LOCK_READER_NODE (newfe) = osb->node_num;
		    DISK_LOCK_WRITER_NODE (newfe) = osb->node_num;
                    OcfsQuerySystemTime(&t);
                    DISK_LOCK_LAST_WRITE(newfe) = t;
                    DISK_LOCK_LAST_READ(newfe) = t;

		    newfe->create_time = newfe->modify_time = CURRENT_TIME;

		    status = ocfs_create_directory (osb, parent_off, newfe);
		    if (status >= 0) 
			    *file_off = newfe->this_sector;
		    else {
			    if (status != -EINTR)
			    	LOG_ERROR_STATUS (status);
			    goto leave;
		    }
		    break;

	    case FLAG_FILE_CREATE:
		    if (fe == NULL) {
			    newfe = ocfs_allocate_file_entry ();
			    if (newfe == NULL) {
				    LOG_ERROR_STATUS (status = -ENOMEM);
				    goto leave;
			    }
		    } else {
			    newfe = fe;
		    }

		    strncpy (newfe->filename, file_name->name, file_name->len);
                    newfe->filename[file_name->len]='\0';
		    newfe->filename_len = file_name->len;

		    /* Set the flag to use the local extents */
		    newfe->local_ext = true;
		    newfe->granularity = -1;
		    newfe->next_free_ext = 0;
		    newfe->last_ext_ptr = 0;

		    strcpy (newfe->signature, OCFS_FILE_ENTRY_SIGNATURE);

		    /* Set the valid bit here */
		    SET_VALID_BIT (newfe->sync_flags);
		    newfe->sync_flags &= ~(OCFS_SYNC_FLAG_CHANGE);

		    /* Initialize the lock state */
		    DISK_LOCK_SEQNUM (newfe) = changeSeqNum;

		    DISK_LOCK_CURRENT_MASTER (newfe) = osb->node_num;
		    DISK_LOCK_FILE_LOCK (newfe) = OCFS_DLM_ENABLE_CACHE_LOCK;
		    DISK_LOCK_READER_NODE (newfe) = osb->node_num;
		    DISK_LOCK_WRITER_NODE (newfe) = osb->node_num;
                    OcfsQuerySystemTime(&t);
                    DISK_LOCK_LAST_WRITE(newfe) = t;
                    DISK_LOCK_LAST_READ(newfe) = t;

		    newfe->create_time = newfe->modify_time = CURRENT_TIME;

		    status = ocfs_create_file (osb, parent_off, newfe);
		    if (status >= 0) 
			    *file_off = newfe->this_sector;
		    else {
			    if (status != -EINTR)
			    	LOG_ERROR_STATUS (status);
			    goto leave;
		    }
		    break;

	    case FLAG_FILE_DELETE_CDSL:
		    status = ocfs_delete_cdsl (osb, parent_off, fe);
		    if (status < 0) {
			    if (status != -EINTR)
			    	LOG_ERROR_STATUS (status);
			    goto leave;
		    }
		    break;

	    case FLAG_FILE_CREATE_CDSL:
		    status = ocfs_create_cdsl (osb, parent_off, fe);
		    if (status < 0) {
			    if (status != -EINTR)
			    	LOG_ERROR_STATUS (status);
			    goto leave;
		    }
		    break;

	    case FLAG_FILE_CHANGE_TO_CDSL:
		    status = ocfs_change_to_cdsl (osb, parent_off, fe);
		    if (status < 0) {
			    if (status != -EINTR)
			    	LOG_ERROR_STATUS (status);
			    goto leave;
		    }
		    break;

	    case FLAG_FILE_TRUNCATE:
		    status = ocfs_truncate_file (osb, *file_off, file_size, oin);
		    if (status < 0) {
			    if (status != -EINTR)
			    	LOG_ERROR_STATUS (status);
			    goto leave;
		    }
		    break;

	    case FLAG_FILE_UPDATE:
		    status = ocfs_change_file_size (osb, parent_off, oin, 
					    file_size, file_off, attr);
		    if (status < 0) {
			    if (status != -EINTR)
			    	LOG_ERROR_STATUS (status);
			    goto leave;
		    }
		    break;

	    default:
		    break;
	}

	status = ocfs_commit_trans (osb, osb->curr_trans_id);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

      leave:
	if (status < 0) {
		tmpstat = ocfs_abort_trans (osb, osb->curr_trans_id);
		if (tmpstat < 0) {
			LOG_ERROR_STATUS (tmpstat);
			/* VOL DISABLE TODO */
			status = tmpstat;
		}
	}

	osb->trans_in_progress = false;

	if ((newfe != fe))
		ocfs_release_file_entry (newfe);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_create_modify_file */


/*
 * ocfs_initialize_oin()
 *
 * Initialize a oin structure and file object. This function is called
 * whenever a file is recognized for the first time.
 */
int
ocfs_initialize_oin (ocfs_inode * oin,
		   ocfs_super * osb,
		   __u32 flags, struct file *file_obj, __u64 file_off, __u64 lock_id)
{
	int status = 0;

	LOG_ENTRY ();

	if (!(flags & OCFS_OIN_ROOT_DIRECTORY)) {
		status = ocfs_create_update_lock (osb, oin, lock_id, flags);
		if (status < 0) {
			/* This can be okay as the other node can tell us the */
			/* file was deleted. */
			goto leave;
		}
	}

        oin->dir_disk_off = 0;
	oin->osb = osb;
	INIT_LIST_HEAD (&(oin->next_ofile));
	oin->oin_flags |= flags;
	oin->open_hndl_cnt = 0;
	oin->file_disk_off = file_off;
	ocfs_extent_map_init (&oin->map);

      leave:
	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_initialize_oin */


/*
 * ocfs_create_delete_cdsl()
 *
 */
int ocfs_create_delete_cdsl (struct inode *inode, struct file *filp,
			     ocfs_super * osb, ocfs_cdsl * cdsl)
{
	int status = 0;
	struct qstr fileName;
	bool bAcquiredOSB = false;
	ocfs_file_entry *fe = NULL;
	__u64 tempSize = 0;
	__u64 fileEntry = 0;
	__u64 parent_off;

	LOG_ENTRY ();

	if (cdsl->name[0] == '\0') {
		LOG_ERROR_STATUS (status = -EINVAL);
		goto leave;
	}

	ocfs_down_sem (&(osb->osb_res), true);
	bAcquiredOSB = true;

	fileName.name = cdsl->name;
        fileName.len = strlen(cdsl->name);

	fe = ocfs_allocate_file_entry ();
	if (fe == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto leave;
	}

	memset (fe, 0, sizeof (ocfs_file_entry));
	if (!ocfs_linux_get_inode_offset (inode, &parent_off, NULL)) {
		LOG_ERROR_STATUS (status = -EFAIL);
		goto leave;
	}

	status = ocfs_find_files_on_disk (osb, parent_off, &fileName, fe, NULL);

	if (status >= 0) {
		if (cdsl->operation & OCFS_CDSL_CREATE) {
			/* Create a cdsl with a file/directory already present. */
			if ((cdsl->flags & OCFS_FLAG_CDSL_DIR) &&
			    (!(fe->attribs & OCFS_ATTRIB_DIRECTORY))) {
				status = -EEXIST;
				goto leave;
			}

			fe->attribs |= OCFS_ATTRIB_FILE_CDSL;

			/* Initialize the lock state */
			DISK_LOCK_SEQNUM (fe) = 0;
			DISK_LOCK_CURRENT_MASTER (fe) = OCFS_INVALID_NODE_NUM;
			DISK_LOCK_FILE_LOCK (fe) = OCFS_DLM_NO_LOCK;
			DISK_LOCK_READER_NODE (fe) = OCFS_INVALID_NODE_NUM;
			DISK_LOCK_WRITER_NODE (fe) = OCFS_INVALID_NODE_NUM;

			fe->modify_time = CURRENT_TIME;
			fe->create_time = fe->modify_time;

			status = ocfs_create_modify_file (osb, parent_off, NULL,
					NULL, tempSize, &fileEntry,
					FLAG_FILE_CHANGE_TO_CDSL, fe, NULL);
			if (status != -EINTR)
				LOG_ERROR_STATUS (status);
			goto leave;
		} else if ((cdsl->operation & OCFS_CDSL_DELETE)) {
			status = ocfs_create_modify_file (osb, parent_off, NULL,
					NULL, tempSize, &fileEntry,
					FLAG_FILE_DELETE_CDSL, fe, NULL);
			if (status == -EINTR)
				LOG_ERROR_STATUS (status);
			goto leave;
		} else {
			status = -EINVAL;
			goto leave;
		}
	}

	if ((status == -ENOENT) && (cdsl->operation & OCFS_CDSL_CREATE)) {
		memset (fe, 0, sizeof (ocfs_file_entry));
		memcpy (fe->filename, cdsl->name, strlen (cdsl->name));
		fe->filename_len = strlen (fe->filename);

		/* Set the flag to use the local extents */
		fe->local_ext = true;
		fe->granularity = -1;
		fe->next_free_ext = 0;
		fe->last_ext_ptr = 0;
		fe->attribs |= OCFS_ATTRIB_FILE_CDSL;
#ifdef USERSPACE_TOOL
		fe->uid = getuid();
		fe->gid = getgid();
#else
                fe->uid = current->fsuid;
                fe->gid = current->fsgid;
#endif
                fe->prot_bits = 0755; //mode & 0007777;
	

		if (cdsl->flags & OCFS_FLAG_CDSL_DIR) {
			fe->attribs |= OCFS_ATTRIB_DIRECTORY;
                }

		strcpy (fe->signature, OCFS_FILE_ENTRY_SIGNATURE);

		/* Set the valid bit here */

		SET_VALID_BIT (fe->sync_flags);
		fe->sync_flags &= ~(OCFS_SYNC_FLAG_CHANGE);

		/* Initialize the lock state */
		DISK_LOCK_SEQNUM (fe) = 0;
		DISK_LOCK_CURRENT_MASTER (fe) = OCFS_INVALID_NODE_NUM;
		DISK_LOCK_FILE_LOCK (fe) = OCFS_DLM_NO_LOCK;
		DISK_LOCK_READER_NODE (fe) = OCFS_INVALID_NODE_NUM;
		DISK_LOCK_WRITER_NODE (fe) = OCFS_INVALID_NODE_NUM;

		fe->modify_time = CURRENT_TIME;
		fe->create_time = fe->modify_time;

		status = ocfs_create_modify_file (osb, parent_off, NULL, NULL,
				tempSize, &fileEntry, FLAG_FILE_CREATE_CDSL,
				fe, NULL);

		if (status != -EINTR)
			LOG_ERROR_STATUS (status);
		goto leave;
	}

      leave:
	if (bAcquiredOSB) {
		ocfs_up_sem (&(osb->osb_res));
		bAcquiredOSB = false;
	}

	ocfs_release_file_entry (fe);

	LOG_EXIT_STATUS (status);
	return (status);
}  /* ocfs_create_delete_cdsl */


/*
 * ocfs_find_create_cdsl()
 *
 */
int ocfs_find_create_cdsl (ocfs_super * osb, ocfs_file_entry * fe)
{
	int status = 0;
	__u8 *buffer = NULL;
	__u64 cdslOffset;
	__u64 *cdslInfo;
	ocfs_file_entry *new_fe = NULL;
	__u32 length;
	ocfs_dir_node *dnode;
	ocfs_dir_node *new_dnode;

	LOG_ENTRY ();

	/* Read and see if we have a relevant entry for this node */

	new_fe = ocfs_allocate_file_entry ();
	if (new_fe == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto leave;
	}

	memcpy (new_fe, fe, sizeof (ocfs_file_entry));

	length = (8 * OCFS_MAXIMUM_NODES);
        length = OCFS_ALIGN (length, OCFS_SECTOR_SIZE);
	status = ocfs_read_disk_ex (osb, (void **) &buffer, length, length,
				    fe->extents[0].disk_off);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	cdslInfo = (__u64 *) buffer;
	cdslOffset = cdslInfo[osb->node_num];
	if (cdslOffset == 0) {
		__u64 physicalOffset, fileOffset, numSectorsAlloc, bitmapOffset,
		    numClustersAlloc;

		/* create the entry if one doesn't exist and modify the cdsl data */

		/* Allocate contiguous blocks on disk */
		status = ocfs_alloc_node_block (osb, OCFS_SECTOR_SIZE,
			&physicalOffset, &fileOffset, (__u64 *) & numSectorsAlloc,
			osb->node_num, DISK_ALLOC_EXTENT_NODE);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto leave;
		}

		if (fileOffset == 0) {
			LOG_TRACE_ARGS ("offset=0, file=%s\n", fe->filename);
		}

		new_fe->this_sector = physicalOffset;

		cdslInfo[osb->node_num] = cdslOffset = physicalOffset;

		if (new_fe->attribs & OCFS_ATTRIB_DIRECTORY) {
			status = ocfs_alloc_node_block (osb,
				osb->vol_layout.dir_node_size, &bitmapOffset,
				&fileOffset, &numClustersAlloc, osb->node_num,
				DISK_ALLOC_DIR_NODE);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto leave;
			}

			/* update the total allocation size here */
			new_fe->alloc_size = osb->vol_layout.dir_node_size;
			new_fe->extents[0].disk_off = bitmapOffset;
			new_fe->file_size = osb->vol_layout.dir_node_size;
			new_fe->next_del = INVALID_DIR_NODE_INDEX;

			dnode = ocfs_malloc (osb->vol_layout.dir_node_size);
			if (dnode == NULL) {
				LOG_ERROR_STATUS (status = -ENOMEM);
				goto leave;
			}

			new_dnode = dnode;
			memset (new_dnode, 0, osb->vol_layout.dir_node_size);

			ocfs_initialize_dir_node (osb, new_dnode,
				  bitmapOffset, fileOffset, osb->node_num);

			DISK_LOCK_CURRENT_MASTER (new_dnode) = osb->node_num;
			DISK_LOCK_FILE_LOCK (new_dnode) =
						OCFS_DLM_ENABLE_CACHE_LOCK;
			new_dnode->dir_node_flags |= DIR_NODE_FLAG_ROOT;

			status = ocfs_write_dir_node (osb, new_dnode, -1);
			if (status < 0) {
				LOG_ERROR_STATUS (status);
				goto leave;
			}
		} else {
			/* This is a file */
			new_fe->extents[0].disk_off = 0;
			new_fe->alloc_size = 0;
			new_fe->file_size = 0;
		}

		status = ocfs_write_file_entry (osb, new_fe,
						new_fe->this_sector);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto leave;
		}

		status = ocfs_write_disk (osb, (__s8 *) buffer, length,
					  fe->extents[0].disk_off);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto leave;
		}
		memcpy (fe, new_fe, OCFS_SECTOR_SIZE);

	} else {
		status = ocfs_read_disk (osb, (__s8 *) fe, OCFS_SECTOR_SIZE,
					 cdslOffset);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto leave;
		}
	}

      leave:
	ocfs_safefree (buffer);
	ocfs_release_file_entry (new_fe);
	LOG_EXIT_STATUS (status);
	return (status);
}  /* ocfs_find_create_cdsl */


#ifdef UNUSED_CODE
/*
 * ocfs_update_file_entry_slot()
 *
 */
int ocfs_update_file_entry_slot (ocfs_super * osb, ocfs_inode * oin,
				 ocfs_rw_mode rw_mode)
{
	int status = 0;
	ocfs_file_entry *fe = NULL;

	LOG_ENTRY ();

	fe = ocfs_allocate_file_entry ();
	if (fe == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto leave;
	}

	status = ocfs_read_file_entry (osb, (void *) fe, oin->file_disk_off);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	/* Update tick count if needed i.e., our slot time has expired */

	if (rw_mode == OCFS_WRITE) {
		OcfsQuerySystemTime (&DISK_LOCK_LAST_WRITE (fe));
		DISK_LOCK_WRITER_NODE (fe) = osb->node_num;
	} else {
		OcfsQuerySystemTime (&DISK_LOCK_LAST_READ (fe));
		DISK_LOCK_READER_NODE (fe) = osb->node_num;
	}

	status = ocfs_write_force_disk (osb, (void *) fe, osb->sect_size,
					oin->file_disk_off);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

      leave:
	ocfs_release_file_entry (fe);

	LOG_EXIT_STATUS (status);
	return (status);
}  /* ocfs_update_file_entry_slot */

/*
 * ocfs_check_lock_state()
 *
 */
void ocfs_check_lock_state (ocfs_super * osb, ocfs_inode * oin)
{
	int status = 0;
	ocfs_lock_res *pLockRes = NULL;
	__u64 readTimediff = 0;
	__u64 writeTimediff = 0;
	__u64 newTime = 0;

	LOG_ENTRY ();

	pLockRes = oin->lock_res;
	OCFS_ASSERT (pLockRes);
	ocfs_get_lockres (pLockRes);

	OcfsQuerySystemTime (&newTime);

	readTimediff = (__u64) (newTime - pLockRes->last_read_time);
	writeTimediff = (__u64) (newTime - pLockRes->last_write_time);

	/* Check the lock Id for which we are doing a open if somebody owns */
	/* a cache on it ask for a flush. If there is no cache but a master */
	/* which has a timestamp which is still in the slot */
	/* ??? Do we want to do Update master on open in this case or just */
	/* revert to Write Thru. Read caching can be enabled if we have a */
	/* lot of readers but no writers. In this case when a writer comes */
	/* it will need to update all readers so that they update their cache. */
	/* Slot for reader, slot for writers can solve the issue. */

	if (pLockRes->lock_type == OCFS_DLM_ENABLE_CACHE_LOCK) {
		if ((pLockRes->master_node_num == osb->node_num) &&
		    (writeTimediff > CACHE_LOCK_SLOT_TIME)) {
			oin->cache_enabled = true;
			ocfs_update_file_entry_slot (osb, oin, OCFS_WRITE);
			goto leave;
		} else {
			status = ocfs_break_cache_lock (osb, pLockRes);
			if (status < 0) {
				if (status != -EINTR)
					LOG_ERROR_STATUS (status);
				goto leave;
			}

			oin->cache_enabled = false;
			pLockRes->lock_type = OCFS_DLM_NO_LOCK;
			status = 0;
			goto leave;
		}
	}

	if ((pLockRes->lock_type <= OCFS_DLM_SHARED_LOCK) &&
	    (readTimediff > CACHE_LOCK_SLOT_TIME)) {
		if (writeTimediff > CACHE_LOCK_SLOT_TIME)
			oin->cache_enabled = true;

		ocfs_update_file_entry_slot (osb, oin, OCFS_READ);
		status = 0;
		goto leave;
	}

      leave:
	ocfs_put_lockres (pLockRes, osb);
	LOG_EXIT ();
	return;
}				/* ocfs_check_lock_state */
#endif /* UNUSED_CODE */



/*
 * ocfs_delete_cdsl()
 *
 */
int ocfs_delete_cdsl (ocfs_super * osb, __u64 parent_off, ocfs_file_entry * fe)
{
	int status = 0;
	int tmpstat;
	ocfs_file_entry *newfe = NULL;
	ocfs_dir_node *pLockNode = NULL;
	__u32 lockFlags = 0;
	bool bAcquiredLock = false;
	ocfs_lock_res *pLockResource = NULL;
	__u64 lockId = 0;
	bool bParentLockAcquired = false;
	__u32 parentLockFlags = 0;
	ocfs_lock_res *pParentLockResource = NULL;
	__u64 parentLockId = 0;

	LOG_ENTRY ();

	newfe = fe;
	if (newfe == NULL) {
		LOG_ERROR_STATUS (status = -EINVAL);
		goto leave;
	}

	if (newfe->link_cnt != 0) {
		LOG_ERROR_STATUS (status = -ENOTEMPTY);
		goto leave;
	}

	pLockNode = (ocfs_dir_node *) ocfs_allocate_file_entry ();
	if (pLockNode == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto leave;
	}

	parentLockId = parent_off;
	parentLockFlags = (FLAG_FILE_CREATE | FLAG_DIR);
	status = ocfs_acquire_lock (osb, parentLockId, OCFS_DLM_EXCLUSIVE_LOCK,
				    parentLockFlags, &pParentLockResource,
				    (ocfs_file_entry *) pLockNode);
	if (status < 0) {
		if (status != -EINTR)
			LOG_ERROR_STATUS (status);
		goto leave;
	}

	bParentLockAcquired = true;

	lockId = newfe->this_sector;
	lockFlags = (FLAG_FILE_DELETE);

	status = ocfs_acquire_lock (osb, lockId, OCFS_DLM_EXCLUSIVE_LOCK,
				    lockFlags, &pLockResource, newfe);
	if (status < 0) {
		if (status != -EINTR)
			LOG_ERROR_STATUS (status);
		goto leave;
	}

	bAcquiredLock = true;

	/* Check the file Entry and call delete if link count == 0 */
	if (newfe->link_cnt == 0) {
		/* Mark the file as being deleted */
		OCFS_SET_FLAG (fe->sync_flags, OCFS_SYNC_FLAG_MARK_FOR_DELETION);
		fe->sync_flags &= (~OCFS_SYNC_FLAG_VALID);

		status = ocfs_write_file_entry (osb, fe, fe->this_sector);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto leave;
		}

		/* Lock on directory shd be held by the node which either */
		/* died or this node... */

		status = ocfs_del_file_entry (osb, newfe, pLockNode);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto leave;
		}
	} else {
		status = -ENOTEMPTY;
		goto leave;
	}

      leave:
	/* Release the file lock if we acquired it */
	if (bAcquiredLock) {
		tmpstat = ocfs_release_lock (osb, lockId, OCFS_DLM_EXCLUSIVE_LOCK,
					    lockFlags, pLockResource, newfe);
		if (tmpstat < 0)
			LOG_ERROR_STATUS (tmpstat);
	}

	/* Release the file lock if we acquired it */
	if (bParentLockAcquired) {
		tmpstat = ocfs_release_lock (osb, parentLockId,
				OCFS_DLM_EXCLUSIVE_LOCK, parentLockFlags,
				pParentLockResource,
				(ocfs_file_entry *)pLockNode);
		if (tmpstat < 0)
			LOG_ERROR_STATUS (tmpstat);
	}

	ocfs_release_file_entry ((ocfs_file_entry *) pLockNode);
	ocfs_put_lockres (pLockResource);
	ocfs_put_lockres (pParentLockResource);
	LOG_EXIT_STATUS (status);
	return status;
}


/*
 * ocfs_change_to_cdsl()
 *
 */
int ocfs_change_to_cdsl (ocfs_super * osb, __u64 parent_off, ocfs_file_entry * fe)
{
	int status = 0, tmpstat = 0;
	ocfs_file_entry *new_fe = NULL;
	ocfs_dir_node *pLockNode = NULL;
	__u32 length;
	__u64 lockId = 0;
	__u64 offset;
	__u64 numClustersAlloc;
	ocfs_lock_res *lockres = NULL;
	__u32 lockFlags = 0;
	bool bAcquiredLock = false;
	__u8 *buffer = NULL;
	__u64 *cdslInfo;
	bool bCacheLock = false;

	/* Zero out the entry for the file and rewrite it back to the disk */
	/* Also, the other nodes should update their cache bitmap for file */
	/* ent to mark this one as free now. */

	LOG_ENTRY();

	if (fe == NULL) {
		LOG_ERROR_STATUS (status = -EINVAL);
		goto leave;
	}

	new_fe = ocfs_allocate_file_entry ();
	if (new_fe == NULL) {
		LOG_ERROR_STATUS(status = -ENOMEM);
		goto leave;
	}

	memcpy (new_fe, fe, sizeof (ocfs_file_entry));

	if ((DISK_LOCK_FILE_LOCK (new_fe) == OCFS_DLM_ENABLE_CACHE_LOCK)
	    && (DISK_LOCK_CURRENT_MASTER (new_fe) == osb->node_num)) {
		bCacheLock = true;
	}

	lockId = new_fe->this_sector;
	lockFlags = FLAG_FILE_CHANGE_TO_CDSL;
	pLockNode = (ocfs_dir_node *) new_fe;

	status = ocfs_acquire_lock (osb, lockId,
				    (bCacheLock ? OCFS_DLM_ENABLE_CACHE_LOCK :
				    		  OCFS_DLM_EXCLUSIVE_LOCK),
				    lockFlags, &lockres,
				    (ocfs_file_entry *) pLockNode);
	if (status < 0) {
		if (status != -EINTR)
			LOG_ERROR_STATUS (status);
		goto leave;
	}

	bAcquiredLock = true;

	if (bCacheLock) {
		DISK_LOCK_FILE_LOCK (new_fe) = OCFS_DLM_ENABLE_CACHE_LOCK;
		DISK_LOCK_CURRENT_MASTER (new_fe) = osb->node_num;
	}

	length = (8 * OCFS_MAXIMUM_NODES);
	length = OCFS_ALIGN (length, OCFS_PAGE_SIZE);

	status = ocfs_find_contiguous_space_from_bitmap (osb, length,
				&offset, &numClustersAlloc, false);
	if (status < 0) {
		LOG_ERROR_STATUS(status);
		goto leave;
	}

	new_fe->extents[0].disk_off = (offset * osb->vol_layout.cluster_size) +
					osb->vol_layout.data_start_off;
	new_fe->extents[0].num_bytes = numClustersAlloc *
					osb->vol_layout.cluster_size;
	new_fe->extents[0].file_off = 0;

	new_fe->alloc_size = new_fe->file_size = new_fe->extents[0].num_bytes;

	new_fe->attribs |= OCFS_ATTRIB_FILE_CDSL;

	/* Initialize the table with 0 */
	buffer = ocfs_malloc (length);
	if (buffer == NULL) {
		LOG_ERROR_STATUS(status = -ENOMEM);
		goto leave;
	}

	memset (buffer, 0, length);
	cdslInfo = (__u64 *) buffer;

	/* Point entry for this node to the file entry we have */

	{
		__u64 physicalOffset, fileOffset, numSectorsAlloc;

		/* create the entry if one doesn't exist and modify the cdsl data */

		/* Allocate contiguous blocks on disk */
		status =
		    ocfs_alloc_node_block (osb, OCFS_SECTOR_SIZE, &physicalOffset,
				    &fileOffset, (__u64 *) & numSectorsAlloc,
				    osb->node_num, DISK_ALLOC_EXTENT_NODE);
		if (status < 0) {
			LOG_ERROR_STATUS(status);
			goto leave;
		}

		fe->this_sector = physicalOffset;

		*(cdslInfo + osb->node_num) = physicalOffset;

		/* Write the new file entry to the disk */
		status = ocfs_write_file_entry (osb, fe, physicalOffset);
		if (status < 0) {
			LOG_ERROR_STATUS(status);
			goto leave;
		}
	}

	status =
	    ocfs_write_disk (osb, (__s8 *) buffer, length,
			   new_fe->extents[0].disk_off);
	if (status < 0) {
		LOG_ERROR_STATUS(status);
		goto leave;
	}

	OcfsQuerySystemTime (&DISK_LOCK_LAST_WRITE (new_fe));
	OcfsQuerySystemTime (&DISK_LOCK_LAST_READ (new_fe));
	DISK_LOCK_WRITER_NODE (new_fe) = osb->node_num;
	DISK_LOCK_READER_NODE (new_fe) = osb->node_num;

	/* Write the file entry with the cdsl back */
	status = ocfs_write_file_entry (osb, new_fe, new_fe->this_sector);
	if (status < 0) {
		LOG_ERROR_STATUS(status);
		goto leave;
	}

      leave:
	if (bAcquiredLock) {
		tmpstat = ocfs_release_lock (osb, lockId, OCFS_DLM_EXCLUSIVE_LOCK,
					     lockFlags, lockres, new_fe);
		if (tmpstat < 0)
			LOG_ERROR_STATUS (tmpstat);
	}

	ocfs_release_file_entry (new_fe);
	ocfs_safefree (buffer);
	ocfs_put_lockres (lockres);
	LOG_EXIT_STATUS(status);
	return status;
}  /* ocfs_change_to_cdsl */


/*
 * ocfs_create_cdsl()
 *
 */
int ocfs_create_cdsl (ocfs_super * osb, __u64 parent_off, ocfs_file_entry * fe)
{
	int status = 0, tmpstat = 0;
	ocfs_file_entry *fileEntry = NULL;
	ocfs_dir_node *PDirNode = NULL, *pLockNode = NULL;
	__u32 length;
	__u64 lockId = 0, bitmapOffset, numClustersAlloc;
	ocfs_lock_res *pLockResource = NULL;
	__u32 lockFlags = 0;
	bool bAcquiredLock = false;
	__u8 *buffer = NULL;
	bool invalid_dirnode = false;

	LOG_ENTRY ();

	/* Zero out the entry for the file and rewrite it back to the disk */ 
	/* Also, the other nodes should update their cache bitmap for file */
	/* ent to mark this one as free now. */

	pLockNode = (ocfs_dir_node *) ocfs_allocate_file_entry ();
	if (pLockNode == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto leave;
	}

	lockId = parent_off;
	lockFlags = FLAG_FILE_CREATE | FLAG_DIR;

	status = ocfs_acquire_lock (osb, lockId, OCFS_DLM_EXCLUSIVE_LOCK,
			lockFlags, &pLockResource, (ocfs_file_entry *) pLockNode);
	if (status < 0) {
		if (status != -EINTR)
			LOG_ERROR_STATUS (status);
		goto leave;
	}
	bAcquiredLock = true;

	/* Change the name and write it back... */
	if (fe == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto leave;
	}

	fileEntry = fe;
	length = (8 * OCFS_MAXIMUM_NODES);
	length = OCFS_ALIGN (length, OCFS_PAGE_SIZE);

	status = ocfs_find_contiguous_space_from_bitmap (osb, length,
				&bitmapOffset, &numClustersAlloc, false);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	fileEntry->extents[0].disk_off =
	    (bitmapOffset * osb->vol_layout.cluster_size)
	    + osb->vol_layout.data_start_off;
	fileEntry->extents[0].num_bytes =
	    numClustersAlloc * osb->vol_layout.cluster_size;
	fileEntry->extents[0].file_off = 0;

	fileEntry->alloc_size = fileEntry->file_size =
	    fileEntry->extents[0].num_bytes;

	/* Initialize the table with 0 */
	buffer = ocfs_malloc (length);
	if (buffer == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto leave;
	}

	memset (buffer, 0, length);

	status =
	    ocfs_write_disk (osb, (__s8 *) buffer, length,
			   fileEntry->extents[0].disk_off);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	PDirNode = ocfs_allocate_dirnode();
	if (PDirNode == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto leave;
	}

	status = ocfs_get_dirnode(osb, pLockNode, parent_off, PDirNode,
				  &invalid_dirnode);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	OcfsQuerySystemTime (&DISK_LOCK_LAST_WRITE (fileEntry));
	OcfsQuerySystemTime (&DISK_LOCK_LAST_READ (fileEntry));
	DISK_LOCK_WRITER_NODE (fileEntry) = osb->node_num;
	DISK_LOCK_READER_NODE (fileEntry) = osb->node_num;

	status = ocfs_insert_file (osb, PDirNode, fileEntry, pLockNode,
				   pLockResource, invalid_dirnode);
	if (status < 0) {
		LOG_ERROR_STATUS(status);
		goto leave;
	}

	bAcquiredLock = false;

      leave:
	if (bAcquiredLock) {
		tmpstat = ocfs_release_lock (osb, lockId, OCFS_DLM_EXCLUSIVE_LOCK,
					     lockFlags, pLockResource,
					     (ocfs_file_entry *) pLockNode);
		if (tmpstat < 0)
			LOG_ERROR_STATUS (tmpstat);
	}

	ocfs_release_dirnode (PDirNode);
	ocfs_release_file_entry ((ocfs_file_entry *) pLockNode);
	ocfs_safefree (buffer);
	ocfs_put_lockres (pLockResource);
	LOG_EXIT_STATUS (status);
	return status;
}  /* ocfs_create_cdsl */


/*
 * ocfs_truncate_file()
 *
 */
int ocfs_truncate_file (ocfs_super * osb, __u64 file_off, __u64 file_size, ocfs_inode * oin)
{
	int status = 0, tmpstat;
	ocfs_file_entry *fe = NULL;
	__u64 lockId = 0;
	__u32 lockFlags = 0;
	bool bFileLockAcquired = false;
	bool bAcquiredLock = false;
	ocfs_lock_res *pLockResource = NULL;
	__u64 changeSeqNum = 0;
	bool bCacheLock = false;
	ocfs_dir_node *pLockNode = NULL;
        __u64 new_alloc_size;
        __u32 csize = osb->vol_layout.cluster_size;

	LOG_ENTRY ();

        new_alloc_size = OCFS_ALIGN(file_size, csize);

	fe = ocfs_allocate_file_entry ();
	if (fe == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto leave;
	}

	status = ocfs_read_file_entry (osb, fe, file_off);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	if (!IS_VALID_FILE_ENTRY(fe)) {
		LOG_ERROR_ARGS ("Invalid fe at offset %u.%u", HI(file_off),
				LO(file_off));
		status = -EFAIL;
		goto leave;
	}

	lockId = fe->this_sector;
	lockFlags = FLAG_FILE_TRUNCATE;
	bFileLockAcquired = true;
	pLockNode = (ocfs_dir_node *) fe;

	if ((DISK_LOCK_FILE_LOCK (fe) == OCFS_DLM_ENABLE_CACHE_LOCK)
	    && (DISK_LOCK_CURRENT_MASTER (fe) == osb->node_num)) {
		bCacheLock = true;
	}

	status = ocfs_acquire_lock (osb, lockId,
				    (bCacheLock ? OCFS_DLM_ENABLE_CACHE_LOCK :
				    		  OCFS_DLM_EXCLUSIVE_LOCK),
				    lockFlags, &pLockResource,
				    (ocfs_file_entry *) pLockNode);
	if (status < 0) {
		if (status != -EINTR)
			LOG_ERROR_STATUS (status);
		goto leave;
	}

	bAcquiredLock = true;

        LOG_TRACE_ARGS ("ocfs: truncate %s fe=%u.%u (%u.%u - %u.%u = %u.%u)\n",
	       fe->filename, HILO(fe->this_sector), HILO(fe->alloc_size),
	       HILO(fe->alloc_size - new_alloc_size), HILO(new_alloc_size));

	fe->file_size = file_size;
	fe->alloc_size = new_alloc_size;

	status = ocfs_free_extents_for_truncate (osb, fe);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	if (bCacheLock) {
		DISK_LOCK_FILE_LOCK (fe) = OCFS_DLM_ENABLE_CACHE_LOCK;
		DISK_LOCK_CURRENT_MASTER (fe) = osb->node_num;
	}

	DISK_LOCK_SEQNUM (fe) = changeSeqNum;
	SET_VALID_BIT (fe->sync_flags);
	fe->sync_flags &= ~(OCFS_SYNC_FLAG_CHANGE);
	fe->modify_time = CURRENT_TIME;

	status = ocfs_write_file_entry (osb, fe, fe->this_sector);
	if (status < 0) {
		LOG_ERROR_STATUS (status);
		goto leave;
	}

	if (oin) {
	/* if we updated correctly then we can update the OIN */
		ocfs_down_sem (&(oin->main_res), true);
		oin->alloc_size = new_alloc_size;
		ocfs_up_sem (&(oin->main_res));
	}

      leave:
	if (bAcquiredLock) {
		if (bFileLockAcquired)
			lockFlags |= FLAG_FILE_UPDATE_OIN;

		tmpstat = ocfs_release_lock (osb, lockId, OCFS_DLM_EXCLUSIVE_LOCK,
					     lockFlags, pLockResource, fe);
		if (tmpstat < 0)
			LOG_ERROR_STATUS (tmpstat);
	}

	ocfs_release_file_entry (fe);
	ocfs_put_lockres (pLockResource);
	LOG_EXIT_STATUS(status);
	return status;
}  /* ocfs_truncate_file */
