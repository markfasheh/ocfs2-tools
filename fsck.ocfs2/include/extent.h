/*
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
 */

#ifndef __O2FSCK_EXTENT_H__
#define __O2FSCK_EXTENT_H__

#include "fsck.h"

typedef errcode_t (check_leaf_er_func)(o2fsck_state *ost,
				       uint64_t owner,
				       struct ocfs2_extent_list *el,
				       struct ocfs2_extent_rec *er,
				       int *changed,
				       void *para);

struct extent_info {
	uint64_t		ei_max_size;
	uint64_t		ei_clusters;
	uint64_t		ei_last_eb_blk;
	uint16_t		ei_expected_depth;
	unsigned		ei_expect_depth:1;
	check_leaf_er_func	*chk_rec_func;
	void			*para;
};

errcode_t o2fsck_check_extents(o2fsck_state *ost,
                               struct ocfs2_dinode *di);

errcode_t check_el(o2fsck_state *ost, struct extent_info *ei,
		   uint64_t owner,
		   struct ocfs2_extent_list *el,
		   uint16_t max_recs, int *changed);
errcode_t o2fsck_check_extent_rec(o2fsck_state *ost,
				  uint64_t owner,
				  struct ocfs2_extent_list *el,
				  struct ocfs2_extent_rec *er,
				  int *changed,
				  void *para);

#endif /* __O2FSCK_EXTENT_H__ */

