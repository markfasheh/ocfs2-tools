/*
 * ocfs2_internals.h
 *
 * Kernel internal structures which we only export here for debug
 * purposes. In other words, this is stuff that userspace has no
 * business knowing.
 *
 * Copyright (C) 2005 Oracle.  All rights reserved.
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
 */

#ifndef _OCFS2_INTERNALS_H_
#define _OCFS2_INTERNALS_H_

#include "ocfs2-kernel/sparse_endian_types.h"
#include "ocfs2-kernel/ocfs2_lockid.h"

/*
 * Values taken from fs/ocfs2/dlm/dlmapi.h
 */
#define LKM_IVMODE      (-1)            /* invalid mode */
#define LKM_NLMODE      0               /* null lock */
#define LKM_CRMODE      1               /* concurrent read    unsupported */
#define LKM_CWMODE      2               /* concurrent write   unsupported */
#define LKM_PRMODE      3               /* protected read */
#define LKM_PWMODE      4               /* protected write    unsupported */
#define LKM_EXMODE      5               /* exclusive */

#define DLM_LVB_LEN  64

/*
 * Values taken from fs/ocfs2/ocfs2.h 
 */
#define OCFS2_LOCK_ATTACHED      (0x00000001) /* have we initialized
					       * the lvb */
#define OCFS2_LOCK_BUSY          (0x00000002) /* we are currently in
					       * dlm_lock */
#define OCFS2_LOCK_BLOCKED       (0x00000004) /* blocked waiting to
					       * downconvert*/
#define OCFS2_LOCK_LOCAL         (0x00000008) /* newly created inode */
#define OCFS2_LOCK_NEEDS_REFRESH (0x00000010)
#define OCFS2_LOCK_REFRESHING    (0x00000020)
#define OCFS2_LOCK_INITIALIZED   (0x00000040) /* track initialization
					       * for shutdown paths */
#define OCFS2_LOCK_FREEING       (0x00000080) /* help dlmglue track
					       * when to skip queueing
					       * a lock because it's
					       * about to be
					       * dropped. */
#define OCFS2_LOCK_QUEUED        (0x00000100) /* queued for downconvert */

enum ocfs2_ast_action {
	OCFS2_AST_INVALID = 0,
	OCFS2_AST_ATTACH,
	OCFS2_AST_CONVERT,
	OCFS2_AST_DOWNCONVERT,
};

enum ocfs2_unlock_action {
	OCFS2_UNLOCK_INVALID = 0,
	OCFS2_UNLOCK_CANCEL_CONVERT,
	OCFS2_UNLOCK_DROP_LOCK,
};

/*
 * Values taken from fs/ocfs2/dlmglue.h
 */
#define OCFS2_LVB_VERSION 2

/* "version 1" lvb, used in ocfs2 1.0 and 1.1 */
struct ocfs2_meta_lvb_v1 {
	__be32       lvb_old_seq;
	__be32       lvb_version;
	__be32       lvb_iclusters;
	__be32       lvb_iuid;
	__be32       lvb_igid;
	__be16       lvb_imode;
	__be16       lvb_inlink;
	__be64       lvb_iatime_packed;
	__be64       lvb_ictime_packed;
	__be64       lvb_imtime_packed;
	__be64       lvb_isize;
	__be32       lvb_reserved[2];
};

/* "version 2" lvb, used in ocfs2 1.3 */
struct ocfs2_meta_lvb_v2 {
	__be32       lvb_version;
	__be32       lvb_iclusters;
	__be32       lvb_iuid;
	__be32       lvb_igid;
	__be64       lvb_iatime_packed;
	__be64       lvb_ictime_packed;
	__be64       lvb_imtime_packed;
	__be64       lvb_isize;
	__be16       lvb_imode;
	__be16       lvb_inlink;
	__be32       lvb_reserved[3];
};

#endif		/* _OCFS2_INTERNALS_H_ */
