/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dlm.h
 *
 * Interface the OCFS2 userspace library to the userspace DLM library
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License, version 2,  as published by the Free Software Foundation.
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
 * Authors: Mark Fasheh
 */

#ifndef _DLM_H
#define _DLM_H

errcode_t ocfs2_lock_down_cluster(ocfs2_filesys *fs);
errcode_t ocfs2_release_cluster(ocfs2_filesys *fs);

errcode_t ocfs2_initialize_dlm(ocfs2_filesys *fs);
errcode_t ocfs2_shutdown_dlm(ocfs2_filesys *fs);

errcode_t ocfs2_super_lock(ocfs2_filesys *fs);
errcode_t ocfs2_super_unlock(ocfs2_filesys *fs);

errcode_t ocfs2_meta_lock(ocfs2_filesys *fs,
			  ocfs2_cached_inode *inode,
			  enum o2dlm_lock_level level,
			  int flags);
errcode_t ocfs2_meta_unlock(ocfs2_filesys *fs,
			    ocfs2_cached_inode *ci);

#endif  /* _DLM_H */
