/*
 * ocfsfile.c
 *
 * 
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

#ifdef __KERNEL__
#include <ocfs.h>
#else
#include <libocfs.h>
#endif

/* Tracing */
#define OCFS_DEBUG_CONTEXT      OCFS_DEBUG_CONTEXT_FILEINFO

/*
 * ocfs_set_disposition_information()
 *
 * Called during file deletion. It checks for the attributes and looks up
 * in the OIN list if the file is present. If it finds the file, it deletes
 * the partition on ths disk and also removes the OIN from the list.
 */
int ocfs_set_disposition_information (struct inode *dir, struct dentry *dentry)
{
	int status = 0;
	bool main_resAcquired = false;
	ocfs_inode *OIN = NULL;
	ocfs_super *osb = NULL;
	struct inode *inode;
	__u64 parentOff, fileOff;

	LOG_ENTRY ();

	if (!dentry->d_inode) {
		LOG_ERROR_STATUS (status = -EFAIL);
		goto finally;
	}
	inode = dentry->d_inode;
	osb = ((ocfs_super *)(dir->i_sb->u.generic_sbp));
	OCFS_ASSERT(IS_VALID_OSB(osb));

	if (inode_data_is_oin (inode)) {
		OIN = ((ocfs_inode *)inode->u.generic_ip);
		if (OIN == NULL) {
			LOG_ERROR_STATUS (status = -EFAIL);
			goto finally;
		}
		OCFS_ASSERT(IS_VALID_OIN(OIN));

		if (OIN->open_hndl_cnt > 0) {
			LOG_TRACE_STR ("Cannot remove an open file");
			status = -EBUSY;
			goto finally;
		}

		ocfs_down_sem (&(OIN->main_res), true);
		main_resAcquired = true;

		/*
		   ** Check if the user wants to delete the file or not delete the file.
		   ** Do some checking to see if the file can even be deleted.
		 */
		if (OIN->oin_flags & OCFS_OIN_DELETE_ON_CLOSE) {
			LOG_TRACE_STR ("OCFS_OIN_DELETE_ON_CLOSE set");
			goto finally;
		}

		if (OIN->oin_flags & OCFS_OIN_ROOT_DIRECTORY) {
			LOG_TRACE_STR ("Cannot delete the root directory");
			status = -EPERM;
			goto finally;
		}

		OCFS_SET_FLAG (OIN->oin_flags, OCFS_OIN_DELETE_ON_CLOSE);

		if (main_resAcquired) {
			ocfs_up_sem (&(OIN->main_res));
			main_resAcquired = false;
		}

	}

	/* Call CreateModify with delete flag to free up the bitmap etc. */
	if (!ocfs_linux_get_inode_offset (dir, &parentOff, NULL)) {
		LOG_ERROR_STATUS (status = -ENOENT);
		goto finally;
	}

	if (S_ISDIR (inode->i_mode)) {
		if (!ocfs_linux_get_dir_entry_offset (osb, &fileOff, parentOff,
						      &(dentry->d_name), NULL)) {
			LOG_ERROR_STATUS (status = -ENOENT);
		}
	} else {
		if (!ocfs_linux_get_inode_offset (inode, &fileOff, NULL)) {
			LOG_ERROR_STATUS (status = -ENOENT);
		}
	}

	if (fileOff != -1)
		status = ocfs_create_modify_file (osb, parentOff, NULL, NULL, 0,
				  &fileOff, FLAG_FILE_DELETE, NULL, NULL);
	if (status < 0) {
		if (status != -ENOTEMPTY && status != -EPERM &&
		    status != -EBUSY && status != -EINTR)
			LOG_ERROR_STATUS(status);
		if (OIN) {
			ocfs_down_sem (&(OIN->main_res), true);
			OCFS_CLEAR_FLAG (OIN->oin_flags,
				       OCFS_OIN_DELETE_ON_CLOSE);
			OCFS_CLEAR_FLAG (OIN->oin_flags, OCFS_OIN_IN_USE);
			ocfs_up_sem (&(OIN->main_res));
		}
		goto finally;
	}

	if (OIN)
		ocfs_release_cached_oin (osb, OIN);

      finally:
	if (main_resAcquired) {
		ocfs_up_sem (&(OIN->main_res));
		main_resAcquired = false;
	}

	LOG_EXIT_STATUS (status);
	return (status);
}				/* ocfs_set_disposition_information */

/*
 * ocfs_set_rename_information()
 *
 */
int ocfs_set_rename_information (struct inode *old_dir,
			  struct dentry *old_dentry,
			  struct inode *new_dir, struct dentry *new_dentry)
{
	int status = 0;
	ocfs_inode *oldOIN = NULL;
	ocfs_file_entry *newfe = NULL;
	ocfs_file_entry *oldfe = NULL;
	__u64 oldOffset;
	__u64 newDirOff;
	__u64 oldDirOff;
	ocfs_super *osb = NULL;
	bool DeleteTargetOin = false;
        __u64 t;
	ocfs_inode *newOIN = NULL;

	LOG_ENTRY ();

	newfe = ocfs_allocate_file_entry ();
	if (newfe == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto finally;
	}

	oldfe = ocfs_allocate_file_entry ();
	if (oldfe == NULL) {
		LOG_ERROR_STATUS (status = -ENOMEM);
		goto finally;
	}

	osb = ((ocfs_super *)(old_dir->i_sb->u.generic_sbp));
	OCFS_ASSERT(IS_VALID_OSB(osb));

	/* old parent dir offset */
	if (!ocfs_linux_get_inode_offset (old_dir, &oldDirOff, NULL)) {
		LOG_ERROR_STATUS (status = -ENOENT);
		goto finally;
	}

	/* old file offset */
	if (!ocfs_linux_get_inode_offset (old_dentry->d_inode, &oldOffset,
					  &oldOIN)) {
		LOG_ERROR_STATUS (status = -ENOENT);
		goto finally;
	}

	if (S_ISDIR (old_dentry->d_inode->i_mode)) {
		/* overwrite oldOffset to get ptr to OCFS_FILE_ENTRY not DIR_NODE */
		if (!ocfs_linux_get_dir_entry_offset (osb, &oldOffset,
				      oldDirOff, &(old_dentry->d_name), NULL)) {
			LOG_ERROR_STATUS (status = -ENOENT);
			goto finally;
		}
	}

	if (oldOIN) {
		if (oldOIN->open_hndl_cnt != 0) {
			status = -EBUSY;
			goto finally;
		}
	}

	/* new parent dir offset */
	if (inode_data_is_oin (new_dir))
		newDirOff = ((ocfs_inode *)new_dir->u.generic_ip)->dir_disk_off;
	else
		newDirOff = GET_INODE_OFFSET (new_dir);

	/* Don't ever take the main resource for the OIN before this as */
	/* Locking hierarchy will be broken */
	if (new_dentry->d_inode != NULL &&
	    inode_data_is_oin (new_dentry->d_inode)) {
		/* overwriting an existing inode */
		newOIN = ((ocfs_inode *)new_dentry->d_inode->u.generic_ip);
		OCFS_ASSERT(IS_VALID_OIN(newOIN));

		if (!(newOIN->oin_flags & OCFS_OIN_IN_TEARDOWN) &&
		    !(newOIN->oin_flags & OCFS_OIN_DELETE_ON_CLOSE)) {
			/* OIN exists and it's not marked for deletion! */
			ocfs_down_sem (&(newOIN->main_res), true);
			OCFS_SET_FLAG (newOIN->oin_flags, OCFS_OIN_IN_USE);
			status = ocfs_verify_update_oin (osb, newOIN);
			if (status < 0)
				LOG_ERROR_STATUS (status);
			ocfs_up_sem (&(newOIN->main_res));
			DeleteTargetOin = true;
		}
	}

	status = ocfs_find_files_on_disk (osb, newDirOff,
				  &(new_dentry->d_name), newfe, NULL);

	if ((status < 0) && (status != -ENOENT)) {
		/* If we cannot find the file specified we should just */
		/* return the error... */
		LOG_ERROR_STATUS (status);
		goto finally;
	}

	ocfs_start_trans (osb);

	if (status >= 0) {
		/* Try and delete the file we found. */
		/* Call CreateModify with delete flag as we need to free up */
		/* the bitmap etc. */
		status = ocfs_del_file (osb, newDirOff, FLAG_RESET_VALID, newfe->this_sector);

		if (status < 0) {
			/* Delete this file entry, createmodify will create a new */
			/* one with the changed attributes. */
			/* This is dangerous as we can potentially fail in */
			/* CreateModify and we have no file left?? */
			/* TODO we should make this transactional such that */
			/* either we get the new file or the old file stays. */
			/* Also, we need to ensure nobdy has the file open currently. */
			LOG_ERROR_STATUS (status);
			goto finally;
		}
		// Delete the Oin if one exists
		if (DeleteTargetOin) {
			ocfs_release_cached_oin (osb, newOIN);
			ocfs_release_oin (newOIN, true);
		}
	}

	if (old_dir != new_dir) {
		/* Delete the file Entry only on the source directory */

		LOG_TRACE_STR ("Source & Target Directories are different");

		status = ocfs_read_file_entry (osb, oldfe, oldOffset);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}

		status = ocfs_del_file (osb, oldDirOff, FLAG_DEL_NAME, oldOffset);
		if (status < 0) {
			if (status != -ENOTEMPTY && status != -EINTR)
			LOG_ERROR_STATUS (status);
			goto finally;
		}

		oldfe->sync_flags &= ~OCFS_SYNC_FLAG_VALID;
                strncpy(oldfe->filename, new_dentry->d_name.name, new_dentry->d_name.len);
                oldfe->filename[new_dentry->d_name.len] = '\0';
		oldfe->filename_len = new_dentry->d_name.len;

		OcfsQuerySystemTime (&t);

		/* Initialize the lock state */

		// DISK_LOCK_SEQNUM(oldfe) = changeSeqNum;
		DISK_LOCK_CURRENT_MASTER (oldfe) = osb->node_num;
		DISK_LOCK_FILE_LOCK (oldfe) = OCFS_DLM_ENABLE_CACHE_LOCK;
		DISK_LOCK_LAST_WRITE (oldfe) = t;
		DISK_LOCK_LAST_READ (oldfe) = t;
		DISK_LOCK_READER_NODE (oldfe) = osb->node_num;
		DISK_LOCK_WRITER_NODE (oldfe) = osb->node_num;
		oldfe->modify_time = CURRENT_TIME;
		// oldfe->create_time = t;

		status = ocfs_create_file (osb, newDirOff, oldfe);
		if (status < 0) {
			if (status != -EINTR)
			LOG_ERROR_STATUS (status);
			goto finally;
		}

		status = ocfs_commit_trans (osb, osb->curr_trans_id);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}

		if (oldOIN) {
			// OCFS_SET_FLAG (oldOIN->oin_flags, OCFS_OIN_DELETE_ON_CLOSE);
			ocfs_release_cached_oin (osb, oldOIN);
			ocfs_release_oin (oldOIN, true);
			if (new_dentry->d_inode)
				fsync_inode_buffers(old_dentry->d_inode);
		}
		/* move the inode offset over to the new entry */
		if (S_ISDIR (old_dentry->d_inode->i_mode)) {
		SET_INODE_OFFSET (old_dentry->d_inode, oldfe->extents[0].disk_off);
		} else {
		SET_INODE_OFFSET (old_dentry->d_inode, oldfe->this_sector);
		}

	} else {
		/* Write the new file name to disk */
		LOG_TRACE_STR ("Source & Target Directories are same");
		status = ocfs_rename_file (osb, oldDirOff, &(new_dentry->d_name), oldOffset);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}

		status = ocfs_commit_trans (osb, osb->curr_trans_id);
		if (status < 0) {
			LOG_ERROR_STATUS (status);
			goto finally;
		}
	}

      finally:
	if (status < 0 && osb->trans_in_progress)
		ocfs_abort_trans (osb, osb->curr_trans_id);
	if (newfe)
		ocfs_release_file_entry (newfe);
	if (oldfe)
		ocfs_release_file_entry (oldfe);

	LOG_EXIT_STATUS (status);
	return status;
}				/* ocfs_set_rename_information */
