/*
 * discontig_bg.h
 *
 * Function prototypes, macros, etc. for related 'C' files
 *
 * Copyright (C) 2010 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef _FSWRECK_DISCONTIG_BG_H_
#define _FSWRECK_DISCONTIG_BG_H_

void mess_up_discontig_bg(ocfs2_filesys *fs, enum fsck_type type,
			  uint16_t slotnum);
#endif		/* _FSWRECK_DISCONTIG_BG_H_ */
