/*
 * ocfsproc.h
 *
 * Function prototypes for related 'C' file.
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
 * Authors: Kurt Hackel, Sunil Mushran, Manish Singh, Wim Coekaerts
 */

#ifndef _OCFSPROC_H_
#define _OCFSPROC_H_

int ocfs_proc_init (void);

void ocfs_proc_deinit (void);

void ocfs_proc_add_volume (ocfs_super * osb);

void ocfs_proc_remove_volume (ocfs_super * osb);

#ifdef OCFSPROC_PRIVATE_DECLS
static int ocfs_proc_calc_metrics (char *page,
		     char **start, off_t off, int count, int *eof, int len);

#ifdef OCFS_LINUX_MEM_DEBUG
static int ocfs_proc_memallocs (char *page,
		   char **start, off_t off, int count, int *eof, void *data);
#endif

static int ocfs_proc_globalctxt(char *page, 
		char **start, off_t off, int count, int *eof, void *data);

static int ocfs_proc_dlm_stats(char *page, char **start, off_t off,
			       int count, int *eof, void *data);

static int ocfs_proc_version (char *page,
		 char **start, off_t off, int count, int *eof, void *data);

static int ocfs_proc_nodenum (char *page,
		 char **start, off_t off, int count, int *eof, void *data);

static int ocfs_proc_nodename (char *page,
		  char **start, off_t off, int count, int *eof, void *data);

static int ocfs_proc_mountpoint (char *page,
		    char **start, off_t off, int count, int *eof, void *data);

static int ocfs_proc_statistics (char *page,
		    char **start, off_t off, int count, int *eof, void *data);

static int ocfs_proc_hash_stats (char *page,
		  char **start, off_t off, int count, int *eof, void *data);

static int ocfs_proc_device (char *page, char **start, off_t off,
			     int count, int *eof, void *data);

static int ocfs_proc_lock_type_stats (char *page, char **start, off_t off,
				      int count, int *eof, void *data);

static int ocfs_proc_nodes (char *page, char **start, off_t off,
			    int count, int *eof, void *data);

#endif				/* OCFSPROC_PRIVATE_DECLS */

#endif				/* _OCFSPROC_H_ */
