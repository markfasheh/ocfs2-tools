/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * o2dlm.h
 *
 * Defines the userspace locking api
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
 */

#ifndef _O2DLM_H_
#define _O2DLM_H_

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <stdlib.h>

#include <kernel-list.h>

#include <et/com_err.h>

#if O2DLM_FLAT_INCLUDES
#include "o2dlm_err.h"
#else
#include <o2dlm/o2dlm_err.h>
#endif

#define O2DLM_LOCK_ID_MAX_LEN       32
#define O2DLM_DOMAIN_MAX_LEN        255

/* + null pointer */
#define O2DLM_MAX_FULL_DOMAIN_PATH (PATH_MAX + 1)

/* valid lock flags */
#define O2DLM_TRYLOCK      0x01
#define O2DLM_VALID_FLAGS  (O2DLM_TRYLOCK)

/* valid lock levels */
enum o2dlm_lock_level
{
	O2DLM_LEVEL_PRMODE,
	O2DLM_LEVEL_EXMODE
};

struct o2dlm_lock_res
{
	struct list_head      l_list;  /* to hang us off the locks list */
	char                  l_id[O2DLM_LOCK_ID_MAX_LEN]; /* 32 byte,
							    * null
							    * terminated
							    * string */
	int                   l_flags; /* limited set of flags */
	enum o2dlm_lock_level l_level; /* either PR or EX */
	int                   l_fd;    /* the fd returned by the open call */
};

struct o2dlm_ctxt
{
	struct list_head ct_locks;  /* the list of locks */
	char             ct_domain_path[O2DLM_MAX_FULL_DOMAIN_PATH]; /* domain
								      * dir */
	char             ct_ctxt_lock_name[O2DLM_LOCK_ID_MAX_LEN];
};

/* Expects to be given a path to the root of a valid ocfs2_dlmfs file
 * system and a domain identifier of length <= 255 characters including
 * the '\0' */
errcode_t o2dlm_initialize(const char *dlmfs_path,
			   const char *domain_name,
			   struct o2dlm_ctxt **ctxt);

/*
 * lock_name, is a valid lock name -- 32 bytes long including the null
 * character
 *
 * Returns: 0 if we got the lock we wanted
 */
errcode_t o2dlm_lock(struct o2dlm_ctxt *ctxt,
		     const char *lockid,
		     int flags,
		     enum o2dlm_lock_level level);

/* returns 0 on success */
errcode_t o2dlm_unlock(struct o2dlm_ctxt *ctxt,
		       char *lockid);

/* Read the LVB out of a lock.
 * 'len' is the amount to read into 'lvb'
 *
 * We can only read LVB_MAX bytes out of the lock, even if you
 * specificy a len larger than that.
 * 
 * If you want to know how much was read, then pass 'bytes_read'
 */
errcode_t o2dlm_read_lvb(struct o2dlm_ctxt *ctxt,
			 char *lockid,
			 char *lvb,
			 unsigned int len,
			 unsigned int *bytes_read);

errcode_t o2dlm_write_lvb(struct o2dlm_ctxt *ctxt,
			  char *lockid,
			  const char *lvb,
			  unsigned int len,
			  unsigned int *bytes_written);

/*
 * Unlocks all pending locks and frees the lock context.
 */
errcode_t o2dlm_destroy(struct o2dlm_ctxt *ctxt);
#endif /* _O2DLM_H_ */
