/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * o2cb_dlm.h
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

#ifndef _O2CB_DLM_H_
#define _O2CB_DLM_H_

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/* this is the top-level dir containing all the lock "files" I don't
   really expect it will end up being in this exact place */
#define O2CB_USER_DLM_LOCK_PATH    "/dev/ocfs2/dlm/"
#define O2CB_LOCK_ID_MAX_LEN       32
#define O2CB_DLM_OPEN_MODE         0664

/* need to pull in the userland list.h header for this */
struct o2cb_dlm_lock_ctxt
{
	struct list_head ct_locks;  /* the ordered list of locks */
};

/* valid lock flags */
#define O2CB_DLM_TRYLOCK      O_NONBLOCK

/* valid lock levels */
enum o2cb_lock_level
{
	O2CB_DLM_LEVEL_PRMODE,
	O2CB_DLM_LEVEL_EXMODE
};

struct o2cb_lock
{
	struct list_head     l_list;  /* to hang us off the locks list */
	char                 *l_id;   /* 32 byte, null terminated string */
	int                  l_flags; /* limited set of flags */
	enum o2cb_lock_level l_level; /* either PR or EX */
	int                  l_fd;    /* the fd returned by the open call */
};


/*
 *  Should this be called by the lib on the first init call
 *  or should it be called directly by the program (fsck, etc)
 *  when it tries to start up?  Or should this whole task be 
 *  done in an init script?  
 *  
 *  This function needs to load the userdlm.o module
 *  and mount its pseudo-fs in some location.  Anything else???
 *
 *  Returns:  0 on success
 *            -EIO if the fs isn't ready
 */     
int o2cb_dlm_mount(void);

/*
 * This should malloc a o2cb_dlm_lock_ctxt and init the locks
 * list_head
 *
 * Returns: 0 on success
 *          -ENOMEM if we couldn't malloc
 */
o2cb_dlm_lock_ctxt * o2cb_dlm_initialize(void);

/*
 * Takes a valid o2cb_dlm_lock_ctxt and lockid and attempts to
 * open("O2CB_DLM_LOCK_PATH/lockid", flags|level|O_CREAT, mode).
 * Validates the id, flags, and level as best it can.
 * Mallocs one new o2cb_lock and stuffs the id, flags, level,
 * and new lock_fd into it, then list_add_tail's it to the end
 * of the supplied ctxt.
 *
 * lock_name, is a valid ocfs2 lock name -- 32 bytes long, null terminated.
 *
 * Returns: 0 if we got the lock we wanted
 *          -ENOMEM if we couldn't malloc
 *          -EIO if you never did a prepare()
 *          -EBUSY if flags==O2CB_DLM_TRYLOCK and we didn't get the lock
 *          -EINVAL if any of the 3 params was bad
 */
int o2cb_dlm_lock(o2cb_dlm_lock_ctxt *ctxt,
		  char *lockid,
		  int flags,
		  enum o2cb_lock_level level);

/* 
 * Looks up the lock id in your ctxt->locks list.
 * If found, calls close(lock_fd) and frees the o2cb_lock
 *
 * Returns: 0 on success 
 *          -EINVAL if the ctxt or ino is bad
 */
int o2cb_dlm_unlock(o2cb_dlm_lock_ctxt *ctxt,
		    char *lockid);

/*
 * Frees the lock context
 * 
 * Returns: 0 on success
 *          -EBUSY if there are still locks remaining on the list
 *          -EINVAL if ctxt is bad
 */
int o2cb_dlm_destroy(o2cb_dlm_lock_ctxt *ctxt);

#endif /* _O2CB_DLM_H_ */
